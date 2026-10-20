#include "ControllerPrompts.h"
#include "ConsoleBridgeWin32.h"
#include "EngineAccess.h"
#include "FrameObserverHook.h"
#include "Logger.h"
#include "SafeMemory.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>

namespace preyvr::dll {
namespace {
// Steam PreyDll SHA256 7d6e322f...05311a7. tools/re/verify_controller_prompts.py
// checks every span below offline, and what the frame check relies on besides:
// the cvar's registration, CGame::Init's CUIManager and the Steam device.
//
// CUIManager's scheme setter, (ui, scheme). Returns at once when [ui+0x28] is
// already `scheme`; otherwise stores it (kSetSchemeStore) and tells the scheme
// listeners -- DaniellePDA's SetControlScheme and prompt rebuild among them --
// and the UI event OnControlSchemeChanged. The tracker's pad branch calls it
// just so for every XInput event.
constexpr std::uintptr_t kSetSchemeRva = 0x182D7D0;
constexpr std::uint8_t kSetSchemeCode[] = {
    0x48,0x8B,0xC4,0x53,0x55,0x56,0x48,0x83,0xEC,0x60,0x8B,0x59,0x28,0x8B,0xF2,0x48,
    0x8B,0xE9,0x3B,0xDA,0x0F,0x84,0xEB,0x01,0x00,0x00};
constexpr std::uintptr_t kSetSchemeStoreRva = 0x182D806;
constexpr std::uint8_t kSetSchemeStoreCode[] = {0x89,0x51,0x28};
// hud_forceControllerGlyphs's change callback, whole. The scheme every prompt
// reads is [gGame]+0x1A8 (CUIManager) +0x28. A keyboard/mouse scheme (1, 2)
// becomes 5 when input vtable +0x1A8 finds a Steam Controller device, else 3.
constexpr std::uintptr_t kGlyphCallbackRva = 0x1726630;
constexpr std::uint8_t kGlyphCallbackCode[] = {
    0x48,0x83,0xEC,0x28,0x48,0x8B,0x01,0xFF,0x50,0x10,0x85,0xC0,0x74,0x4F,0x48,0x8B,
    0x05,0xFB,0x01,0x4F,0x01,0x48,0x8B,0x88,0xA8,0x01,0x00,0x00,0x8B,0x41,0x28,0xFF,
    0xC8,0x83,0xF8,0x01,0x77,0x37,0x48,0x8B,0x0D,0x7B,0x73,0xB2,0x00,0x48,0x8B,0x01,
    0xFF,0x90,0xA8,0x01,0x00,0x00,0xBA,0x03,0x00,0x00,0x00,0xB9,0x05,0x00,0x00,0x00,
    0x48,0x85,0xC0,0x0F,0x45,0xD1,0x48,0x8B,0x0D,0xC3,0x01,0x4F,0x01,0x48,0x8B,0x89,
    0xA8,0x01,0x00,0x00,0x48,0x83,0xC4,0x28,0xE9,0x43,0x71,0x10,0x00};
// The last-input tracker's keyboard/mouse branch: [gGameCVars]+0x924 is the
// cvar; zero sends scheme 2 through the setter.
constexpr std::uintptr_t kTrackerForceRva = 0x182D43C;
constexpr std::uint8_t kTrackerForceCode[] = {
    0x48,0x8B,0x05,0xED,0x93,0x3E,0x01,0x83,0xB8,0x24,0x09,0x00,0x00,0x00,0x75,0x0E,
    0xBA,0x02,0x00,0x00,0x00,0x48,0x8D,0x4B,0xF0,0xE8,0x76,0x03,0x00,0x00};
// The globals the callback and the tracker read RIP-relative, and their fields.
constexpr std::uintptr_t kGameRva = 0x2C16840;
constexpr std::uintptr_t kGameCVarsRva = 0x2C16830;
constexpr std::uintptr_t kGameUiManager = 0x1A8;
constexpr std::uintptr_t kUiScheme = 0x28;
constexpr std::uintptr_t kForceGlyphs = 0x924;

// CUIManager+0x28: 1 and 2 keyboard and mouse, 3 the Xbox pad, 4 the
// PlayStation pad (hud_playstationContext), 5 the Steam Controller. Only 1, 2
// and 5 are put back; anything else is not a scheme this check knows to move.
constexpr std::int32_t kPadScheme = 3;
bool Strays(std::int32_t scheme) { return scheme == 1 || scheme == 2 || scheme == 5; }
// A scheme put back is not put back again for this long: a device that keeps
// selecting another one (a Steam Input controller's own keys) costs ten setter
// calls a second, not one a frame.
constexpr unsigned long long kCorrectionIntervalMs = 100;
constexpr unsigned long long kReapplyIntervalMs = 1000;
constexpr unsigned long long kCorrectionLogIntervalMs = 5000;

using SetSchemeFn = void(__fastcall*)(void* ui, int scheme);
enum : int { kGuardUnchecked = 0, kGuardOn = 1, kGuardRefused = 2, kGuardFaulted = 3 };

std::mutex gMutex;
// -1 is none. `wanted` is what the lane asks for, `applied` the last value read
// back, `failed` the last one that was not, `sending` the one in the console slot.
int gWanted = -1, gApplied = -1, gSending = -1, gFailed = -1;
unsigned long long gCount = 0;
// Command worker, under gMutex: the frame check's bookkeeping.
unsigned long long gNextReapply = 0, gCorrectedLogged = 0, gNextCorrectionLog = 0;
bool gGuardLogged = false;
// Set by the command worker while this lane's 1 is in force; read each frame.
std::atomic<bool> gEnforce{false};
// Written by the frame check, read by the command worker. gForceSeen is the
// cvar as last read (-1 unread); gLastStray the scheme last put back.
std::atomic<int> gGuard{kGuardUnchecked}, gForceSeen{-1}, gLastStray{-1};
std::atomic<unsigned long long> gCorrected{0};
unsigned long long gNextCorrection = 0; // frame thread only

void Log(const std::string& line) { lifecycle::Log("preyvr_prompts " + line); }
const char* Command(int value) { return value ? "hud_forceControllerGlyphs 1" : "hud_forceControllerGlyphs 0"; }

bool Matches(std::uintptr_t base, std::uintptr_t rva, const std::uint8_t* code, std::size_t size)
{
    return SafeEqual(reinterpret_cast<const void*>(base + rva), code, size);
}

bool GuardCodeMatches(std::uintptr_t base)
{
    return Matches(base, kSetSchemeRva, kSetSchemeCode, sizeof(kSetSchemeCode)) &&
           Matches(base, kSetSchemeStoreRva, kSetSchemeStoreCode, sizeof(kSetSchemeStoreCode)) &&
           Matches(base, kGlyphCallbackRva, kGlyphCallbackCode, sizeof(kGlyphCallbackCode)) &&
           Matches(base, kTrackerForceRva, kTrackerForceCode, sizeof(kTrackerForceCode));
}

bool CallSetScheme(SetSchemeFn setScheme, std::uintptr_t ui, int scheme)
{
    __try {
        setScheme(reinterpret_cast<void*>(ui), scheme);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The frame check against the image at `base`; `setScheme` is that image's own
// setter in the DLL, a stand-in in the test.
void ServiceFrame(std::uintptr_t base, SetSchemeFn setScheme, unsigned long long now)
{
    if (!gEnforce.load(std::memory_order_acquire)) { return; }
    int guard = gGuard.load(std::memory_order_acquire);
    if (guard == kGuardUnchecked) {
        guard = GuardCodeMatches(base) ? kGuardOn : kGuardRefused;
        gGuard.store(guard, std::memory_order_release);
    }
    if (guard != kGuardOn) { return; }
    std::uintptr_t game = 0, ui = 0, cvars = 0;
    std::int32_t scheme = 0, force = 0;
    if (!SafeRead(base + kGameRva, game) || game == 0 || !SafeRead(game + kGameUiManager, ui) || ui == 0 ||
        !SafeRead(ui + kUiScheme, scheme) || !SafeRead(base + kGameCVarsRva, cvars) || cvars == 0 ||
        !SafeRead(cvars + kForceGlyphs, force)) {
        return;
    }
    gForceSeen.store(force != 0 ? 1 : 0, std::memory_order_release);
    // A cleared cvar is the command worker's to set again: until then the
    // laser's next mouse move would only take the scheme straight back.
    if (force == 0 || !Strays(scheme) || now < gNextCorrection) { return; }
    gNextCorrection = now + kCorrectionIntervalMs;
    if (!CallSetScheme(setScheme, ui, kPadScheme)) {
        gGuard.store(kGuardFaulted, std::memory_order_release);
        return;
    }
    gLastStray.store(scheme, std::memory_order_release);
    gCorrected.fetch_add(1, std::memory_order_acq_rel);
}

// The queue/readback handshake on the shared slot.
void Advance()
{
    if (gSending >= 0) {
        if (VerifiedRendererSettingCount() == gCount) { return; }
        const bool verified = VerifiedRendererSettingResult() == 0;
        Log("force_controller_glyphs=" + std::to_string(gSending) +
            (verified ? " result=0 detail=verified_readback" : " result=failed detail=readback_timeout_or_mismatch"));
        if (verified) { gApplied = gSending; }
        else { gFailed = gSending; }
        gSending = -1;
    }
    if (gWanted < 0 || gWanted == gApplied || gWanted == gFailed) { return; }
    // Console commands drain on observed frames; queued before that, this would
    // hold the shared slot for nothing.
    if (FrameObserverStatus() != static_cast<DWORD>(FrameObserverRuntimeStatus::enabled)) { return; }
    const auto count = VerifiedRendererSettingCount();
    const DWORD queued = QueueVerifiedRendererSetting(Command(gWanted));
    if (queued == 0) {
        gSending = gWanted;
        gCount = count;
    } else if (queued != static_cast<DWORD>(ConsoleBridgeResult::busy)) {
        gFailed = gWanted;
        Log("force_controller_glyphs=" + std::to_string(gWanted) + " result=" + std::to_string(queued) +
            " detail=queue_refused");
    }
}

void LogGuard(unsigned long long now)
{
    const int guard = gGuard.load(std::memory_order_acquire);
    if ((guard == kGuardRefused || guard == kGuardFaulted) && !gGuardLogged) {
        gGuardLogged = true;
        Log(guard == kGuardRefused ? "scheme_guard result=refused detail=code_mismatch scheme=native"
                                   : "scheme_guard result=failed detail=setter_fault disarmed=1");
    }
    const auto corrected = gCorrected.load(std::memory_order_acquire);
    if (corrected != gCorrectedLogged && now >= gNextCorrectionLog) {
        gCorrectedLogged = corrected;
        gNextCorrectionLog = now + kCorrectionLogIntervalMs;
        Log("scheme_guard corrected=" + std::to_string(corrected) +
            " from=" + std::to_string(gLastStray.load(std::memory_order_acquire)) + " to=3");
    }
}

void ServiceLane(unsigned long long now)
{
    std::lock_guard lock(gMutex);
    // The frame check read the cvar as 0 while this lane had it at 1: something
    // else cleared it. Set again, at most once a second.
    if (gWanted == 1 && gApplied == 1 && gSending < 0 && gForceSeen.load(std::memory_order_acquire) == 0 &&
        now >= gNextReapply) {
        gNextReapply = now + kReapplyIntervalMs;
        gApplied = -1;
        Log("force_controller_glyphs=0 detail=cleared_outside_lane reapplying=1");
    }
    Advance();
    const bool enforce = gWanted == 1 && gApplied == 1 && gSending < 0;
    if (enforce && !gEnforce.load(std::memory_order_acquire)) { gForceSeen.store(-1, std::memory_order_release); }
    gEnforce.store(enforce, std::memory_order_release);
    LogGuard(now);
}
} // namespace

DWORD SetControllerPrompts(unsigned enabled)
{
    if (enabled > 1) { return 1; }
    std::lock_guard lock(gMutex);
    // Off puts Prey's default back only where this lane set it, or may have.
    if (enabled) { gWanted = 1; }
    else {
        gWanted = gApplied == 1 || gSending == 1 || gFailed == 1 ? 0 : -1;
        gEnforce.store(false, std::memory_order_release); // the frame check stops now, not next tick
    }
    gFailed = -1; // a new request is tried again
    return 0;
}

unsigned ControllerPromptsEnabled()
{
    std::lock_guard lock(gMutex);
    return gWanted == 1 ? 1u : 0u;
}

void ServiceControllerPrompts() { ServiceLane(GetTickCount64()); }

void ServiceControllerPromptsFrame()
{
    if (!gEnforce.load(std::memory_order_acquire)) { return; }
    const auto base = PreyBase();
    if (base != 0) { ServiceFrame(base, reinterpret_cast<SetSchemeFn>(base + kSetSchemeRva), GetTickCount64()); }
}

std::string ControllerPromptsReport()
{
    std::lock_guard lock(gMutex);
    const auto value = [](int v) { return v < 0 ? std::string("none") : std::to_string(v); };
    const char* guards[] = {"unchecked", "on", "refused", "faulted"};
    std::ostringstream out;
    out << " wanted=" << value(gWanted) << " applied=" << value(gApplied) << " sending=" << value(gSending)
        << " failed=" << value(gFailed) << " guard=" << guards[gGuard.load(std::memory_order_acquire)]
        << " corrected=" << gCorrected.load(std::memory_order_acquire)
        << " lastFrom=" << value(gLastStray.load(std::memory_order_acquire));
    return out.str();
}
} // namespace preyvr::dll
