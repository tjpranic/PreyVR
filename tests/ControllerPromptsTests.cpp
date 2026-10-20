// Exercise the production controller-prompts lane, its handshake with the shared
// verified console slot and its frame check of Prey's scheme, without loading
// Prey. Including this TU exposes its internals to this test only.
#include "../src/dll/ControllerPrompts.cpp"
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "TestSupport.h"

namespace preyvr::lifecycle {
std::vector<std::string> gLines;
void Log(std::string_view message) { gLines.emplace_back(message); }
} // namespace preyvr::lifecycle
namespace preyvr::dll {
DWORD gObserver = static_cast<DWORD>(FrameObserverRuntimeStatus::enabled);
DWORD FrameObserverStatus() { return gObserver; }
std::vector<std::string> gQueued;
DWORD gQueueResult = 0;
unsigned long long gVerifiedCount = 0;
DWORD gVerifiedResult = 0;
DWORD QueueVerifiedRendererSetting(const char* command)
{
    if (gQueueResult == 0) { gQueued.emplace_back(command); }
    return gQueueResult;
}
unsigned long long VerifiedRendererSettingCount() { return gVerifiedCount; }
DWORD VerifiedRendererSettingResult() { return gVerifiedResult; }
} // namespace preyvr::dll

namespace {
using namespace preyvr::dll;
using preyvr::lifecycle::gLines;
using preyvr::test::Require;

bool Logged(const char* text)
{
    for (const auto& line : gLines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}
bool Reports(const char* text) { return ControllerPromptsReport().find(text) != std::string::npos; }
std::vector<int> gSetCalls;
void Reset()
{
    gWanted = gApplied = gSending = gFailed = -1;
    gCount = 0;
    gNextReapply = gCorrectedLogged = gNextCorrectionLog = gNextCorrection = 0;
    gGuardLogged = false;
    gEnforce = false;
    gGuard = kGuardUnchecked;
    gForceSeen = -1;
    gLastStray = -1;
    gCorrected = 0;
    gSetCalls.clear();
    gQueued.clear();
    gLines.clear();
    gQueueResult = 0;
    gVerifiedCount = 0;
    gVerifiedResult = 0;
    gObserver = static_cast<DWORD>(FrameObserverRuntimeStatus::enabled);
}
// The slot drains on the render thread: the count moves, with this result.
void Complete(DWORD result)
{
    gVerifiedResult = result;
    ++gVerifiedCount;
}

void TestBadValueIsRefused()
{
    Reset();
    Require(SetControllerPrompts(2) == 1, "a value other than 0 or 1 is refused");
    Require(ControllerPromptsEnabled() == 0 && Reports("wanted=none"), "and changes nothing");
}

void TestOffWithNothingSetLeavesPreyAlone()
{
    Reset();
    Require(SetControllerPrompts(0) == 0, "off is accepted");
    ServiceControllerPrompts();
    Require(gQueued.empty(), "Prey's own value is not touched when this lane never set it");
}

void TestOnWaitsForFramesThenVerifies()
{
    Reset();
    gObserver = static_cast<DWORD>(FrameObserverRuntimeStatus::ready);
    Require(SetControllerPrompts(1) == 0 && ControllerPromptsEnabled() == 1, "on is accepted and reads back");
    ServiceControllerPrompts();
    Require(gQueued.empty(), "nothing is queued before the console drains on observed frames");
    gObserver = static_cast<DWORD>(FrameObserverRuntimeStatus::enabled);
    ServiceControllerPrompts();
    Require(gQueued.size() == 1 && gQueued[0] == "hud_forceControllerGlyphs 1", "Prey's cvar is set");
    ServiceControllerPrompts();
    Require(gQueued.size() == 1, "and not queued twice while it is in the slot");
    Complete(0);
    ServiceControllerPrompts();
    Require(Logged("force_controller_glyphs=1 result=0 detail=verified_readback"), "the readback is logged");
    Require(Reports("applied=1") && Reports("sending=none"), "the report shows it applied");
    ServiceControllerPrompts();
    Require(gQueued.size() == 1, "an applied value is not sent again");
}

void TestBusySlotIsRetried()
{
    Reset();
    gQueueResult = static_cast<DWORD>(ConsoleBridgeResult::busy);
    SetControllerPrompts(1);
    ServiceControllerPrompts();
    Require(gQueued.empty() && Reports("failed=none"), "a busy slot (the focus hold, VR start) is a wait");
    gQueueResult = 0;
    ServiceControllerPrompts();
    Require(gQueued.size() == 1 && gQueued[0] == "hud_forceControllerGlyphs 1", "and is tried again");
}

void TestOffPutsPreysDefaultBack()
{
    Reset();
    SetControllerPrompts(1);
    ServiceControllerPrompts();
    Complete(0);
    ServiceControllerPrompts();
    Require(SetControllerPrompts(0) == 0 && ControllerPromptsEnabled() == 0, "off reads back as off");
    ServiceControllerPrompts();
    Require(gQueued.size() == 2 && gQueued[1] == "hud_forceControllerGlyphs 0", "Prey's default 0 is put back");
    Complete(0);
    ServiceControllerPrompts();
    Require(Logged("force_controller_glyphs=0 result=0") && Reports("applied=0"), "and verified");
}

void TestChangeOfMindWhileSending()
{
    Reset();
    SetControllerPrompts(1);
    ServiceControllerPrompts();
    SetControllerPrompts(0); // VR stopped with the 1 still in the slot
    ServiceControllerPrompts();
    Require(gQueued.size() == 1, "the slot's command finishes first");
    Complete(0);
    ServiceControllerPrompts();
    Require(gQueued.size() == 2 && gQueued[1] == "hud_forceControllerGlyphs 0",
        "then the 1 that landed is put back");
}

void TestFailedReadbackIsNotRetried()
{
    Reset();
    SetControllerPrompts(1);
    ServiceControllerPrompts();
    Complete(2);
    ServiceControllerPrompts();
    Require(Logged("force_controller_glyphs=1 result=failed"), "a failed readback is logged");
    Require(Reports("failed=1"), "the report shows the failure");
    ServiceControllerPrompts();
    Require(gQueued.size() == 1, "and it is not retried every tick");
    SetControllerPrompts(0);
    ServiceControllerPrompts();
    Require(gQueued.size() == 2 && gQueued[1] == "hud_forceControllerGlyphs 0", "a value that may have landed is put back");
}

void TestRefusedQueueIsLogged()
{
    Reset();
    gQueueResult = static_cast<DWORD>(ConsoleBridgeResult::denied);
    SetControllerPrompts(1);
    ServiceControllerPrompts();
    Require(Logged("force_controller_glyphs=1 result=1 detail=queue_refused") && Reports("failed=1"),
        "a refusal other than busy is logged once");
    gLines.clear();
    ServiceControllerPrompts();
    Require(gLines.empty(), "and not repeated");
    gQueueResult = 0;
    SetControllerPrompts(1);
    ServiceControllerPrompts();
    Require(gQueued.size() == 1, "a new request tries again");
}

// Prey's setter as far as the frame check sees it: it stores the scheme.
void __fastcall FakeSetScheme(void* ui, int scheme)
{
    gSetCalls.push_back(scheme);
    std::memcpy(static_cast<std::uint8_t*>(ui) + kUiScheme, &scheme, sizeof(scheme));
}
void __fastcall FaultingSetScheme(void*, int) { RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr); }

// A PreyDll-sized reservation with only the touched pages committed: the
// setter, callback and tracker spans, and the two globals, which point at this
// test's CGame, CUIManager and game cvars.
struct Image {
    static constexpr std::size_t kSize = 0x2D00000;
    std::uintptr_t base = 0;
    std::vector<std::uint8_t> game = std::vector<std::uint8_t>(kGameUiManager + 8, 0);
    std::vector<std::uint8_t> ui = std::vector<std::uint8_t>(kUiScheme + 4, 0);
    std::vector<std::uint8_t> cvars = std::vector<std::uint8_t>(kForceGlyphs + 4, 0);

    Image()
    {
        base = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, kSize, MEM_RESERVE, PAGE_NOACCESS));
        Require(base != 0, "the fake image is reserved");
        for (const std::uintptr_t rva : {kSetSchemeRva, kGlyphCallbackRva, kTrackerForceRva, kGameRva}) {
            Require(VirtualAlloc(reinterpret_cast<void*>((base + rva) & ~std::uintptr_t{0xFFF}), 0x1000, MEM_COMMIT,
                                 PAGE_READWRITE) != nullptr, "a fake image page is committed");
        }
        Put(kSetSchemeRva, kSetSchemeCode, sizeof(kSetSchemeCode));
        Put(kSetSchemeStoreRva, kSetSchemeStoreCode, sizeof(kSetSchemeStoreCode));
        Put(kGlyphCallbackRva, kGlyphCallbackCode, sizeof(kGlyphCallbackCode));
        Put(kTrackerForceRva, kTrackerForceCode, sizeof(kTrackerForceCode));
        const auto pointer = [this](std::uintptr_t at, const void* to) {
            const auto value = reinterpret_cast<std::uintptr_t>(to);
            std::memcpy(reinterpret_cast<void*>(at), &value, sizeof(value));
        };
        pointer(base + kGameRva, game.data());
        pointer(base + kGameCVarsRva, cvars.data());
        pointer(reinterpret_cast<std::uintptr_t>(game.data()) + kGameUiManager, ui.data());
        Scheme(2);
        Force(1);
    }
    ~Image() { VirtualFree(reinterpret_cast<void*>(base), 0, MEM_RELEASE); }
    void Put(std::uintptr_t rva, const std::uint8_t* bytes, std::size_t size)
    {
        std::memcpy(reinterpret_cast<void*>(base + rva), bytes, size);
    }
    void Scheme(int value) { std::memcpy(ui.data() + kUiScheme, &value, sizeof(value)); }
    int Scheme() const
    {
        int value = 0;
        std::memcpy(&value, ui.data() + kUiScheme, sizeof(value));
        return value;
    }
    void Force(int value) { std::memcpy(cvars.data() + kForceGlyphs, &value, sizeof(value)); }
    void Frame(unsigned long long now, SetSchemeFn setScheme = FakeSetScheme) { ServiceFrame(base, setScheme, now); }
};

// On, queued and read back: the state VR start leaves.
void Enforce(unsigned long long now)
{
    SetControllerPrompts(1);
    ServiceLane(now);
    Complete(0);
    ServiceLane(now);
}

void TestGuardWaitsForTheAppliedCvar()
{
    Reset();
    Image image;
    image.Scheme(5); // the callback's pick: Prey's Steam device always answers
    image.Frame(1000);
    Require(gSetCalls.empty(), "nothing is touched while the lane has not set the cvar");
    SetControllerPrompts(1);
    ServiceLane(1000);
    image.Frame(1000);
    Require(gSetCalls.empty(), "nor while the 1 is still in the console slot");
    Complete(0);
    ServiceLane(1000);
    image.Frame(1000);
    Require(gSetCalls.size() == 1 && gSetCalls[0] == 3 && image.Scheme() == 3,
        "once it is read back, the Steam Controller's scheme goes to the pad's");
    image.Frame(2000);
    Require(gSetCalls.size() == 1, "and a pad scheme is left alone");
    ServiceLane(2000);
    Require(Logged("scheme_guard corrected=1 from=5 to=3"), "the correction is logged");
    Require(Reports("guard=on") && Reports("corrected=1") && Reports("lastFrom=5"), "and reported");
}

void TestOnlyKeyboardMouseAndSteamSchemesMove()
{
    Reset();
    Image image;
    Enforce(0);
    unsigned long long now = 1000;
    for (const int scheme : {1, 2, 5}) {
        image.Scheme(scheme);
        image.Frame(now += 1000);
        Require(image.Scheme() == 3, "a keyboard, mouse or Steam Controller scheme goes to the pad's");
    }
    for (const int scheme : {3, 4, 0, 6}) {
        image.Scheme(scheme);
        image.Frame(now += 1000);
        Require(image.Scheme() == scheme, "the pads' schemes, and values this check does not know, stay");
    }
    Require(gSetCalls.size() == 3, "three corrections, no more");
}

void TestCorrectionsAreSpaced()
{
    Reset();
    Image image;
    Enforce(0);
    image.Scheme(5);
    image.Frame(1000);
    image.Scheme(5); // a device that keeps choosing its own scheme
    image.Frame(1050);
    Require(gSetCalls.size() == 1, "a scheme put back is not put back again within the interval");
    image.Frame(1100);
    Require(gSetCalls.size() == 2 && image.Scheme() == 3, "and is after it");
}

void TestClearedCvarIsSetAgain()
{
    Reset();
    Image image;
    Enforce(0);
    image.Force(0); // something other than this lane cleared it
    image.Scheme(2);
    image.Frame(1000);
    Require(gSetCalls.empty(), "the scheme is not moved while the cvar is clear: the laser would move it back");
    ServiceLane(1000);
    Require(Logged("force_controller_glyphs=0 detail=cleared_outside_lane reapplying=1"), "the clearing is logged");
    Require(gQueued.size() == 2 && gQueued[1] == "hud_forceControllerGlyphs 1", "and the cvar is set again");
    image.Frame(1000);
    Require(gSetCalls.empty(), "the check waits for the readback");
    image.Force(1); // the console set it
    Complete(0);
    ServiceLane(1000);
    image.Frame(1000);
    Require(gSetCalls.size() == 1 && image.Scheme() == 3, "then puts the scheme back");
    image.Force(0);
    image.Frame(1100);
    ServiceLane(1100);
    Require(gQueued.size() == 2, "a clearing again within the second waits");
    ServiceLane(2000);
    Require(gQueued.size() == 3, "then the cvar is set again");
}

void TestOffStopsTheGuardAtOnce()
{
    Reset();
    Image image;
    Enforce(0);
    SetControllerPrompts(0);
    image.Scheme(2);
    image.Frame(1000);
    Require(gSetCalls.empty(), "with the option off the laser's mouse scheme stays, before the next tick");
}

void TestMismatchedCodeRefuses()
{
    Reset();
    Image image;
    const std::uint8_t other = 0xCC;
    image.Put(kGlyphCallbackRva + 0x1C, &other, 1); // another build's callback
    Enforce(0);
    image.Scheme(5);
    image.Frame(1000);
    Require(gSetCalls.empty(), "nothing is called when any span differs");
    ServiceLane(1000);
    ServiceLane(2000);
    Require(Logged("scheme_guard result=refused detail=code_mismatch") && Reports("guard=refused"),
        "the refusal is logged and reported");
    Require(gLines.size() == 2, "once, beside the readback");
}

void TestSetterFaultDisarms()
{
    Reset();
    Image image;
    Enforce(0);
    image.Scheme(5);
    image.Frame(1000, FaultingSetScheme);
    image.Frame(2000);
    Require(gSetCalls.empty(), "a fault in the setter disarms the check");
    ServiceLane(2000);
    Require(Logged("scheme_guard result=failed detail=setter_fault disarmed=1") && Reports("guard=faulted"),
        "and is logged");
}
} // namespace

int main()
{
    TestBadValueIsRefused();
    TestOffWithNothingSetLeavesPreyAlone();
    TestOnWaitsForFramesThenVerifies();
    TestBusySlotIsRetried();
    TestOffPutsPreysDefaultBack();
    TestChangeOfMindWhileSending();
    TestFailedReadbackIsNotRetried();
    TestRefusedQueueIsLogged();
    TestGuardWaitsForTheAppliedCvar();
    TestOnlyKeyboardMouseAndSteamSchemesMove();
    TestCorrectionsAreSpaced();
    TestClearedCvarIsSetAgain();
    TestOffStopsTheGuardAtOnce();
    TestMismatchedCodeRefuses();
    TestSetterFaultDisarms();
    std::cout << "controller prompts: refusal, frames, readback, busy slot, restore, failures and scheme guard passed\n";
    return 0;
}
