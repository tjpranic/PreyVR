// Exercise the production controller-prompts lane and its handshake with the
// shared verified console slot, without loading Prey. Including this TU exposes
// its internals to this test only.
#include "../src/dll/ControllerPrompts.cpp"
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
void Reset()
{
    gWanted = gApplied = gSending = gFailed = -1;
    gCount = 0;
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
    std::cout << "controller prompts: refusal, frames, readback, busy slot, restore and failures passed\n";
    return 0;
}
