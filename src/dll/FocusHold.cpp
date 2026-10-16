#include "FocusHold.h"
#include "Bootstrap.h"
#include "ConsoleBridgeWin32.h"
#include "FrameObserverHook.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "preyvr/EngineMap.h"
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

namespace preyvr::dll {
namespace {
// Steam PreyDll SHA256 7d6e322f...05311a7. tools/re/verify_focus_hold.py checks
// the gate, the import slot and the renderer's window field offline.
//
// CSystem::Update's inactive-window loop. rdi is renderer->GetHWND(): CSystem
// vtable +0x298, then renderer vtable +0x6C0 = 0xF50A50, mov rax,[rcx+0xAC18].
//   0xDF58E0  mov rdi,rax; test rax,rax; je skip; mov ebx,esi; nop
//   0xDF58F0  call [rip+0xE7E11A]   ; USER32!GetActiveWindow, slot 0x1C73A10
//             cmp rax,rdi; je skip  ; the render window is active: no wait
//   ...       pump messages; Sleep(5); inc ebx; cmp ebx,5; jl 0xDF58F0
constexpr std::uintptr_t kThrottleRva = 0xDF58E0;
constexpr std::uint8_t kThrottleCode[] = {
    0x48,0x8B,0xF8,0x48,0x85,0xC0,0x0F,0x84,0xC5,0x00,0x00,0x00,0x8B,0xDE,0x66,0x90,
    0xFF,0x15,0x1A,0xE1,0xE7,0x00,0x48,0x3B,0xC7,0x0F,0x84,0xB2,0x00,0x00,0x00};
constexpr std::uintptr_t kThrottleSleepRva = 0xDF599B;
constexpr std::uint8_t kThrottleSleepCode[] = {
    0xB9,0x05,0x00,0x00,0x00,0xFF,0x15,0xF2,0xD9,0xE7,0x00,0xFF,0xC3,0x83,0xFB,0x05,
    0x0F,0x8C,0x3F,0xFF,0xFF,0xFF};
// Where kThrottleCode's rel32 points, in .rdata. The loop is the only reader of
// this slot in the image, so answering it changes nothing else.
constexpr std::uintptr_t kActiveWindowSlotRva = 0x1C73A10;
// CD3D9Renderer's window, stored from CreateWindowExA("CryENGINE") at 0xF541DF.
constexpr std::uintptr_t kRendererWindow = 0xAC18;
constexpr char kWindowClass[] = "CryENGINE";

// **CryAudio mutes on focus loss** (headset run 2026-10-17: no sound minimised).
// Every audio request runs through CAudioTranslationLayer's dispatcher on the
// audio thread, the ATL in rcx and the request in rdx; it writes the request's
// status at +0x20. Class 1 (data +0x10) goes to the manager handler, where type
// (data +0x18) 0x10 runs the "lose_focus" trigger and the middleware's
// OnLoseFocus, and 0x20 the "get_focus" pair. Neither reads more of the request
// than its type, so a focus request can be issued from a two-field stand-in.
constexpr std::uintptr_t kAudioDispatchRva = 0xD5B950;
constexpr std::uint8_t kAudioDispatchCode[] = {
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,
    0x48,0x83,0xEC,0x20,0x48,0x8B,0x72,0x28,0x33,0xED,0x48,0x8B,0xFA,0x48,0x8B,0xD9,
    0x48,0x85,0xF6,0x0F,0x84,0x51,0x02,0x00,0x00,0x8B,0x4E,0x10,0x83,0xE9,0x01};
// Class 1: mov rcx,rbx; call 0xD5A630; mov [rdi+0x20],eax.
constexpr std::uintptr_t kAudioManagerCallRva = 0xD5BBBD;
constexpr std::uint8_t kAudioManagerCallCode[] = {0x48,0x8B,0xCB,0xE8,0x6B,0xEA,0xFF,0xFF,0x89,0x47,0x20};
// The manager handler: type from [[rdx+0x28]+0x18], 0x20 compared directly, up to
// 0x10 through the table at 0xD5AE68.
constexpr std::uintptr_t kAudioManagerRva = 0xD5A630;
constexpr std::uint8_t kAudioManagerCode[] = {
    0x48,0x89,0x5C,0x24,0x08,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x50,0x48,0x8B,0x5A,
    0x28,0x48,0x8B,0xF9,0xBE,0x03,0x00,0x00,0x00,0x8B,0x43,0x18,0x3D,0x00,0x10,0x00,
    0x00,0x0F,0x87,0xBC,0x03,0x00,0x00,0x0F,0x84,0x92,0x03,0x00,0x00,0x83,0xF8,0x20,
    0x0F,0x87,0x8D,0x01,0x00,0x00,0x0F,0x84,0x33,0x01,0x00,0x00,0x83,0xF8,0x10};
constexpr std::uintptr_t kAudioManagerTableRva = 0xD5AE68;
// Type 0x10: reads the lose_focus trigger ID (0x2983664) after the muted-bit test.
constexpr std::uintptr_t kAudioLoseFocusRva = 0xD5A6EA;
constexpr std::uint8_t kAudioLoseFocusCode[] = {
    0x8B,0x87,0x38,0x02,0x00,0x00,0xA8,0x01,0x0F,0x85,0x5D,0x07,0x00,0x00,0x4C,0x8B,
    0x47,0x38,0x8B,0x15,0x62,0x8F,0xC2,0x01};
// Type 0x20: OnGetFocus (middleware +0x38), then the get_focus trigger ID (0x2983668).
constexpr std::uintptr_t kAudioGetFocusRva = 0xD5A79F;
constexpr std::uint8_t kAudioGetFocusCode[] = {
    0x8B,0x81,0x38,0x02,0x00,0x00,0xA8,0x01,0x0F,0x85,0xA8,0x06,0x00,0x00,0x48,0x8B,
    0x89,0x40,0x02,0x00,0x00,0x48,0x8B,0x01,0xFF,0x50,0x38,0x4C,0x8B,0x47,0x38,0x8B,
    0xF0,0x8B,0x15,0xA2,0x8E,0xC2,0x01};
constexpr std::uintptr_t kRequestStatus = 0x20, kRequestData = 0x28, kDataClass = 0x10, kDataManagerType = 0x18;
constexpr std::uint32_t kClassManager = 1, kLoseFocus = 0x10, kGetFocus = 0x20, kStatusSuccess = 1;

enum : DWORD {
    kOk = 0, kBadValue = 1, kNotPinned = 2, kNoModule = 3, kCodeMismatch = 4,
    kSlotMismatch = 5, kNoWindow = 6, kProtectFailed = 7, kHookFailed = 8,
};

using GetActiveWindowFn = HWND(WINAPI*)();

// The answer runs on Prey's main thread and reads only atomics. Everything else
// belongs to the command worker, under gMutex.
std::atomic<GetActiveWindowFn> gReal{nullptr};
std::atomic<HWND> gWindow{nullptr};
std::atomic<bool> gHeld{false};
std::atomic<unsigned long long> gCalls{0}, gAnswered{0};
std::mutex gMutex;
bool gInstalled = false, gWanted = false, gPendingLogged = false;
DWORD gRefusal = kOk;
// g_pauseOnLoseFocus: -1 is "not set by the hold".
int gPauseWanted = -1, gPauseApplied = -1, gPauseSending = -1, gPauseFailed = -1;
unsigned long long gPauseCount = 0;
bool gStateKnown = false, gForeground = false, gMinimized = false;

using AudioDispatchFn = void(__fastcall*)(void* atl, void* request);
using AudioManagerFn = std::uint32_t(__fastcall*)(void* atl, void* request);
std::atomic<AudioDispatchFn> gAudioOriginal{nullptr};
std::atomic<AudioManagerFn> gAudioManager{nullptr};
// 1 focused, 0 not, -1 unknown. Native: what the engine last asked the audio
// for. Applied: what the audio layer last processed.
std::atomic<int> gAudioNative{-1}, gAudioApplied{-1};
std::atomic<unsigned long long> gAudioSwallowed{0}, gAudioIssued{0};
bool gAudioTried = false, gAudioHooked = false;
DWORD gAudioRefusal = kOk;

void Log(const std::string& line) { lifecycle::Log("preyvr_focus " + line); }

std::string Hex(const void* value)
{
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << reinterpret_cast<std::uintptr_t>(value);
    return out.str();
}

std::uintptr_t PreyDllBase() { return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll")); }
// Replaced by the tests, which build a fake image.
std::uintptr_t (*gModuleBase)() = &PreyDllBase;

HWND WINAPI AnswerGetActiveWindow()
{
    const GetActiveWindowFn real = gReal.load(std::memory_order_acquire);
    const HWND active = real != nullptr ? real() : nullptr;
    gCalls.fetch_add(1, std::memory_order_relaxed);
    const HWND window = gWindow.load(std::memory_order_acquire);
    if (gHeld.load(std::memory_order_acquire) && window != nullptr && active != window) {
        gAnswered.fetch_add(1, std::memory_order_relaxed);
        return window;
    }
    return active;
}

bool ReadManagerType(void* request, std::uint32_t& type)
{
    __try {
        const auto data = *reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(request) + kRequestData);
        if (data == 0 || *reinterpret_cast<const std::uint32_t*>(data + kDataClass) != kClassManager) { return false; }
        type = *reinterpret_cast<const std::uint32_t*>(data + kDataManagerType);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// One focus request through the game's own handler, on the audio thread, as the
// dispatcher would run it.
void IssueAudioFocus(void* atl, int focused)
{
    alignas(16) std::uint8_t data[0x20]{};
    alignas(16) std::uint8_t request[0x30]{};
    const std::uint32_t managerClass = kClassManager, type = focused != 0 ? kGetFocus : kLoseFocus;
    const auto dataAddress = reinterpret_cast<std::uintptr_t>(data);
    std::memcpy(data + kDataClass, &managerClass, sizeof(managerClass));
    std::memcpy(data + kDataManagerType, &type, sizeof(type));
    std::memcpy(request + kRequestData, &dataAddress, sizeof(dataAddress));
    gAudioManager.load(std::memory_order_acquire)(atl, request);
    gAudioApplied.store(focused, std::memory_order_release);
    gAudioIssued.fetch_add(1, std::memory_order_relaxed);
}

// While held the audio keeps focus: a lose-focus request is marked done without
// running. Otherwise the audio follows what the engine asked for last, so a loss
// from before the hold is undone, and a release while unfocused mutes as the game
// would have.
void __fastcall AnswerAudioRequest(void* atl, void* request)
{
    const AudioDispatchFn original = gAudioOriginal.load(std::memory_order_acquire);
    std::uint32_t type = 0;
    if (ReadManagerType(request, type) && (type == kLoseFocus || type == kGetFocus)) {
        const int focused = type == kGetFocus ? 1 : 0;
        gAudioNative.store(focused, std::memory_order_release);
        if (focused == 0 && gHeld.load(std::memory_order_acquire)) {
            *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(request) + kRequestStatus) = kStatusSuccess;
            gAudioSwallowed.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        original(atl, request);
        gAudioApplied.store(focused, std::memory_order_release);
        return;
    }
    original(atl, request);
    const int wanted = gHeld.load(std::memory_order_acquire) ? 1 : gAudioNative.load(std::memory_order_acquire);
    if (wanted >= 0 && wanted != gAudioApplied.load(std::memory_order_acquire)) { IssueAudioFocus(atl, wanted); }
}

bool SpanMatches(std::uintptr_t at, const std::uint8_t* bytes, std::size_t size)
{
    __try {
        return std::memcmp(reinterpret_cast<const void*>(at), bytes, size) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadDword(std::uintptr_t at, std::uint32_t& out)
{
    __try {
        out = *reinterpret_cast<const std::uint32_t*>(at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool AudioCodeMatches(std::uintptr_t base)
{
    std::uint32_t loseCase = 0;
    return SpanMatches(base + kAudioDispatchRva, kAudioDispatchCode, sizeof(kAudioDispatchCode)) &&
           SpanMatches(base + kAudioManagerCallRva, kAudioManagerCallCode, sizeof(kAudioManagerCallCode)) &&
           SpanMatches(base + kAudioManagerRva, kAudioManagerCode, sizeof(kAudioManagerCode)) &&
           SpanMatches(base + kAudioLoseFocusRva, kAudioLoseFocusCode, sizeof(kAudioLoseFocusCode)) &&
           SpanMatches(base + kAudioGetFocusRva, kAudioGetFocusCode, sizeof(kAudioGetFocusCode)) &&
           ReadDword(base + kAudioManagerTableRva + kLoseFocus * 4, loseCase) && loseCase == kAudioLoseFocusRva;
}

// Once per process; the detour passes everything through when not held. The
// starting focus is the window's: Prey foregrounds it at creation, so a window
// that is not in front has already sent its loss to the audio.
DWORD InstallAudio(std::uintptr_t base, bool foreground)
{
    if (gAudioHooked) { return kOk; }
    if (!AudioCodeMatches(base)) { return kCodeMismatch; }
    if (!EnsureMinHook()) { return kHookFailed; }
    gAudioNative.store(foreground ? 1 : 0, std::memory_order_release);
    gAudioApplied.store(foreground ? 1 : 0, std::memory_order_release);
    gAudioManager.store(reinterpret_cast<AudioManagerFn>(base + kAudioManagerRva), std::memory_order_release);
    void* const target = reinterpret_cast<void*>(base + kAudioDispatchRva);
    AudioDispatchFn original = nullptr;
    if (MH_CreateHook(target, reinterpret_cast<void*>(&AnswerAudioRequest), reinterpret_cast<void**>(&original)) != MH_OK) {
        return kHookFailed;
    }
    gAudioOriginal.store(original, std::memory_order_release);
    if (MH_EnableHook(target) != MH_OK) { return kHookFailed; }
    gAudioHooked = true;
    return kOk;
}

bool ReadWord(std::uintptr_t at, std::uintptr_t& out)
{
    __try {
        out = *reinterpret_cast<const std::uintptr_t*>(at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Once per process. The slot keeps pointing here after a release: unpinned
// module code would be the hazard, and the module is pinned before this runs.
DWORD Install(std::uintptr_t base)
{
    if (gInstalled) { return kOk; }
    if (!SpanMatches(base + kThrottleRva, kThrottleCode, sizeof(kThrottleCode)) ||
        !SpanMatches(base + kThrottleSleepRva, kThrottleSleepCode, sizeof(kThrottleSleepCode))) {
        return kCodeMismatch;
    }
    // The loader's binding, unshimmed. Anything else -- another tool's hook, a
    // compatibility shim -- is not ours to replace.
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto expected = user32 != nullptr
        ? reinterpret_cast<std::uintptr_t>(GetProcAddress(user32, "GetActiveWindow")) : 0;
    const std::uintptr_t slotAddress = base + kActiveWindowSlotRva;
    std::uintptr_t current = 0;
    if (expected == 0 || !ReadWord(slotAddress, current) || current != expected) {
        return kSlotMismatch;
    }
    gReal.store(reinterpret_cast<GetActiveWindowFn>(current), std::memory_order_release);
    auto* const slot = reinterpret_cast<void**>(slotAddress);
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) { return kProtectFailed; }
    // One aligned pointer store: the main thread calls through either value.
    InterlockedExchangePointer(slot, reinterpret_cast<void*>(&AnswerGetActiveWindow));
    DWORD unused = 0;
    VirtualProtect(slot, sizeof(void*), protection, &unused);
    gInstalled = true;
    return kOk;
}

HWND ReadRendererWindow(std::uintptr_t base)
{
    __try {
        const auto renderer = *reinterpret_cast<const std::uintptr_t*>(
            base + engine::GlobalEnvironmentLayout::baseRva + engine::GlobalEnvironmentLayout::renderer);
        return renderer == 0 ? nullptr : *reinterpret_cast<const HWND*>(renderer + kRendererWindow);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// The field is read, then confirmed by Windows: a live CryENGINE window of this
// process. A stale or unrelated value never becomes the answer.
bool IsRenderWindow(HWND window)
{
    if (window == nullptr || !IsWindow(window)) { return false; }
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    char name[sizeof(kWindowClass) + 1]{};
    return process == GetCurrentProcessId() && GetClassNameA(window, name, sizeof(name)) > 0 &&
           std::strcmp(name, kWindowClass) == 0;
}

const char* RefusalName(DWORD code)
{
    switch (code) {
    case kCodeMismatch: return "code_mismatch";
    case kSlotMismatch: return "import_not_getactivewindow";
    case kProtectFailed: return "slot_protect_failed";
    default: return "unknown";
    }
}

// Holds when it can. Waiting on the module pin or the render window is not a
// refusal: it is retried every tick.
DWORD Advance()
{
    if (!gWanted || gHeld.load(std::memory_order_acquire)) { return kOk; }
    const std::uintptr_t base = gModuleBase();
    DWORD result = kNoModule;
    HWND window = nullptr;
    if (ModulePinStatus() == 0) {
        result = kNotPinned;
    } else if (base != 0) {
        result = Install(base);
        if (result == kOk) {
            window = ReadRendererWindow(base);
            if (!IsRenderWindow(window)) { result = kNoWindow; }
        }
    }
    if (result == kNotPinned || result == kNoModule || result == kNoWindow) {
        if (!gPendingLogged) {
            gPendingLogged = true;
            Log(std::string("state=pending detail=") +
                (result == kNoWindow ? "no_render_window" : result == kNotPinned ? "module_not_pinned" : "no_preydll"));
        }
        return kOk;
    }
    if (result != kOk) {
        gWanted = false;
        gRefusal = result;
        Log("state=refused code=" + std::to_string(result) + " detail=" + RefusalName(result) +
            " native_focus_behaviour=retained");
        return result;
    }
    // Not a condition of the hold: without it the game runs and is silent while
    // unfocused, which the log and the report say.
    if (!gAudioTried) {
        gAudioTried = true;
        gAudioRefusal = InstallAudio(base, GetForegroundWindow() == window);
        Log(gAudioRefusal == kOk
                ? std::string("audio result=0 detail=hooked dispatcher=0xD5B950 lose_focus=held")
                : "audio result=refused code=" + std::to_string(gAudioRefusal) +
                      (gAudioRefusal == kCodeMismatch ? " detail=code_mismatch" : " detail=hook_failed") +
                      " unfocused_audio=native");
    }
    gWindow.store(window, std::memory_order_release);
    gHeld.store(true, std::memory_order_release);
    gPendingLogged = false;
    gStateKnown = false;
    gPauseWanted = 0;
    gPauseFailed = -1;
    Log("state=held window=" + Hex(window) + " slot=GetActiveWindow@0x1C73A10 throttle=0xDF58F0"
        " pause_on_lose_focus=pending audio=" + (gAudioHooked ? "held" : "native"));
    return kOk;
}

void Release()
{
    gWanted = false;
    gPendingLogged = false;
    // Put back only what the hold changed, or may have.
    if (gPauseApplied == 0 || gPauseSending == 0 || gPauseFailed == 0) { gPauseWanted = 1; }
    else { gPauseWanted = -1; }
    gPauseFailed = -1;
    if (gHeld.exchange(false, std::memory_order_acq_rel)) {
        Log("state=released answered=" + std::to_string(gAnswered.load()) + " pause_on_lose_focus=" +
            (gPauseWanted == 1 ? "restoring" : "native"));
    }
}

// The verified path shares one slot with VR start's renderer settings, and both
// owners run on this thread, so the next completion after a queue is ours.
void ServicePause()
{
    if (gPauseSending >= 0) {
        if (VerifiedRendererSettingCount() == gPauseCount) { return; }
        const bool verified = VerifiedRendererSettingResult() == 0;
        Log("pause_on_lose_focus=" + std::to_string(gPauseSending) +
            (verified ? " result=0 detail=verified_readback" : " result=failed detail=readback_timeout_or_mismatch"));
        if (verified) { gPauseApplied = gPauseSending; }
        else { gPauseFailed = gPauseSending; }
        gPauseSending = -1;
    }
    if (gPauseWanted < 0 || gPauseWanted == gPauseApplied || gPauseWanted == gPauseFailed) { return; }
    // Console commands drain on observed frames; queued before that, this would
    // hold the shared slot for nothing.
    if (FrameObserverStatus() != static_cast<DWORD>(FrameObserverRuntimeStatus::enabled)) { return; }
    const auto count = VerifiedRendererSettingCount();
    const DWORD queued = QueueVerifiedRendererSetting(gPauseWanted == 0 ? "g_pauseOnLoseFocus 0" : "g_pauseOnLoseFocus 1");
    if (queued == 0) {
        gPauseSending = gPauseWanted;
        gPauseCount = count;
    } else if (queued != static_cast<DWORD>(ConsoleBridgeResult::busy)) {
        gPauseFailed = gPauseWanted;
        Log("pause_on_lose_focus=" + std::to_string(gPauseWanted) + " result=" + std::to_string(queued) +
            " detail=queue_refused");
    }
}

// The evidence a headset run needs: when Windows took focus or minimised the
// window, and how often the throttle has been answered since.
void ServiceWindowState()
{
    const HWND window = gWindow.load(std::memory_order_acquire);
    if (!gHeld.load(std::memory_order_acquire) || window == nullptr) {
        gStateKnown = false;
        return;
    }
    if (!IsWindow(window)) {
        // A renderer that recreates its window gets held again from the field.
        gHeld.store(false, std::memory_order_release);
        gWindow.store(nullptr, std::memory_order_release);
        gStateKnown = false;
        gPendingLogged = true;
        Log("state=pending detail=window_gone");
        return;
    }
    const bool foreground = GetForegroundWindow() == window;
    const bool minimized = IsIconic(window) != FALSE;
    if (gStateKnown && foreground == gForeground && minimized == gMinimized) { return; }
    gStateKnown = true;
    gForeground = foreground;
    gMinimized = minimized;
    Log(std::string("window foreground=") + (foreground ? "1" : "0") + " minimized=" + (minimized ? "1" : "0") +
        " calls=" + std::to_string(gCalls.load()) + " answered=" + std::to_string(gAnswered.load()) +
        " audioSwallowed=" + std::to_string(gAudioSwallowed.load()) + " audioIssued=" + std::to_string(gAudioIssued.load()));
}
} // namespace

DWORD SetFocusHold(unsigned int enabled)
{
    if (enabled > 1) { return kBadValue; }
    std::lock_guard lock(gMutex);
    if (enabled == 0) {
        Release();
        return kOk;
    }
    gWanted = true;
    gRefusal = kOk;
    return Advance();
}

void ServiceFocusHold()
{
    std::lock_guard lock(gMutex);
    Advance();
    ServicePause();
    ServiceWindowState();
}

std::string FocusHoldReport()
{
    std::lock_guard lock(gMutex);
    const HWND window = gWindow.load(std::memory_order_acquire);
    const bool held = gHeld.load(std::memory_order_acquire);
    const auto value = [](int v) { return v < 0 ? std::string("none") : std::to_string(v); };
    std::ostringstream out;
    out << " state=" << (held ? "held" : gWanted ? "pending" : gRefusal != kOk ? "refused" : "off")
        << " code=" << gRefusal << " installed=" << (gInstalled ? 1 : 0) << " window=" << Hex(window)
        << " foreground=" << (held && window != nullptr && GetForegroundWindow() == window ? 1 : 0)
        << " minimized=" << (held && window != nullptr && IsIconic(window) ? 1 : 0)
        << " calls=" << gCalls.load() << " answered=" << gAnswered.load()
        << " pauseOnLoseFocus=" << value(gPauseApplied) << " pauseSending=" << value(gPauseSending)
        << " pauseFailed=" << value(gPauseFailed)
        << " audio=" << (gAudioHooked ? "hooked" : gAudioTried ? "refused" : "untried") << " audioCode=" << gAudioRefusal
        << " audioSwallowed=" << gAudioSwallowed.load() << " audioIssued=" << gAudioIssued.load()
        << " audioNative=" << value(gAudioNative.load()) << " audioApplied=" << value(gAudioApplied.load());
    return out.str();
}
} // namespace preyvr::dll
