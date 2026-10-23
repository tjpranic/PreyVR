// Exercise the frame probe's ring, windows, detours and report without loading
// Prey. Including this TU exposes internal seams only inside this test executable.
#include "../src/dll/FrameProbe.cpp"
#include <string>
#include <thread>
#include <vector>
#include "TestSupport.h"

namespace preyvr::lifecycle {
std::vector<std::string> gLines;
void Log(std::string_view message) { gLines.emplace_back(message); }
} // namespace preyvr::lifecycle
namespace preyvr::dll {
DWORD gPinned = 0;
DWORD ModulePinStatus() { return gPinned; }
bool EnsureMinHook() { return false; }
// PassCreationHook's shared R-071 hook: unavailable without Prey.
bool EnsurePassCreationHook() { return false; }
// The cull watch's queue indices: a scripted sequence.
std::vector<std::pair<std::uint32_t, std::uint32_t>> gIndices;
std::size_t gIndexCall = 0;
bool CullQueueIndices(std::uint32_t& write, std::uint32_t& read)
{
    if (gIndices.empty()) {
        write = read = 0;
        return false;
    }
    const auto& next = gIndices[(std::min)(gIndexCall++, gIndices.size() - 1)];
    write = next.first;
    read = next.second;
    return true;
}
} // namespace preyvr::dll

namespace {
using namespace preyvr::dll;
using preyvr::lifecycle::gLines;
using preyvr::test::Require;

void ResetProbe()
{
    for (auto& slot : gRing) slot.seq.store(0);
    gNext.store(0);
    gTicks.store(0);
    for (auto& index : gTickIndex) index.store(0);
    gMainThread.store(0);
    gRenderThread.store(0);
    {
        std::lock_guard lock(gWindowMutex);
        gOpen = {};
        gReadyCount = 0;
    }
    gWindowOpen.store(false);
    for (auto* counter : {&gTriggers, &gWritten, &gControls, &gSuppressed, &gLost, &gEmitted}) counter->store(0);
    gRecording.store(true);
    gStartAttempted.store(true);
    gIndices.clear();
    gIndexCall = 0;
    gLines.clear();
}

// One main frame: its start, then the world render around the cull queue.
void Frame(unsigned frames = 1)
{
    for (unsigned i = 0; i < frames; ++i) {
        ProbeRecord(ProbeStep::tick, 1, 0, 10, 10);
        ProbeRecord(ProbeStep::renderEnter, 0, 0, 10, 10);
        ProbeRecord(ProbeStep::renderExit, 0, 0, 12, 10);
    }
}

std::vector<std::string> Lines(const char* prefix)
{
    std::vector<std::string> out;
    for (const auto& line : gLines) {
        if (line.rfind(prefix, 0) == 0) out.push_back(line);
    }
    return out;
}
bool Logged(const char* text)
{
    for (const auto& line : gLines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

void TestOff()
{
    ResetProbe();
    gRecording.store(false);
    Frame(3);
    ProbeRecord(ProbeStep::panelClose);
    Require(gNext == 0 && gTicks == 0 && !gWindowOpen, "nothing is recorded while off");
}

void TestControlWindow()
{
    ResetProbe();
    Frame(10);
    ProbeRecord(ProbeStep::panelClose);
    Require(gWindowOpen && gTriggers == 1, "a panel close opens a window");
    Frame(11);
    Require(gWindowOpen, "still open 11 frames after");
    Frame(1);
    Require(!gWindowOpen && gReadyCount == 1 && gControls == 1, "closed at the 12th frame start, as a control");
    ServiceFrameProbe();
    const auto headers = Lines("preyvr_frame_probe window=1 kinds=panel_close control=1");
    Require(headers.size() == 1, "one header");
    const auto events = Lines("preyvr_frame_probe w=1 ");
    // From the tick 8 frames before the trigger to the tick that closed it:
    // frames 2..10 (9 frames of 3), the trigger, frames 11..21 (11 of 3), tick 22.
    Require(events.size() == 9 * 3 + 1 + 11 * 3 + 1, "the window spans 8 frames before to 12 after");
    Require(events.front().find(" M tick depth=1") != std::string::npos && events.front().find(" -") != std::string::npos,
            "it starts at a frame start, before the trigger");
    Require(events.back().find(" M tick ") != std::string::npos, "and ends at the closing one");
    std::size_t triggers = 0;
    for (const auto& line : events) triggers += line.find("<trigger") != std::string::npos ? 1 : 0;
    Require(triggers == 1 && Logged("panel_close <trigger"), "the trigger is marked");
    Require(Logged("render_exit state=0 q=12/10") && Logged("window=1 end lost=0"), "args and the footer");
    Require(gReadyCount == 0 && gEmitted == 1, "written once");
    ServiceFrameProbe();
    Require(Lines("preyvr_frame_probe window=").size() == 2, "and not again");
}

void TestQuotas()
{
    ResetProbe();
    for (int i = 0; i < 4; ++i) {
        Frame(1);
        ProbeRecord(ProbeStep::panelClose);
        Frame(12);
        ServiceFrameProbe();
    }
    Require(gControls == 3 && gSuppressed == 1, "three plain closes are kept as the control");
    for (unsigned i = 0; i < kMaxWindows + 1; ++i) {
        Frame(1);
        ProbeRecord(ProbeStep::firstEyeRefused, i + 1);
        Frame(12);
        ServiceFrameProbe();
    }
    Require(gWritten == kMaxWindows && gSuppressed == 2, "refusal windows up to their own cap");
    Require(Lines("preyvr_frame_probe window=").size() == 2 * (3 + kMaxWindows), "header and footer each");
}

void TestMergedTriggers()
{
    ResetProbe();
    Frame(20);
    ProbeRecord(ProbeStep::panelClose);
    ProbeRecord(ProbeStep::firstEyeRefused, 1);
    Frame(1);
    ProbeRecord(ProbeStep::lagChange, 1, 0, 300);
    Require(gTriggers == 3, "three triggers");
    Frame(11);  // tick 32: the close's own window would end here
    Require(gWindowOpen, "the lag change extended the window");
    Frame(1);
    Require(!gWindowOpen && gWritten == 1 && gControls == 0, "one window, not a control");
    ServiceFrameProbe();
    Require(Logged("kinds=panel_close+first_eye_refused+lag_change control=0"), "carrying every kind");
    Require(Logged("first_eye_refused refused=1") && Logged("lag_change depth=1 was=0 pending=300"),
            "with their args");

    ResetProbe();
    Frame(10);
    ProbeRecord(ProbeStep::lagChange, 2, 1, 5);
    for (int i = 0; i < 100; ++i) {
        Frame(1);
        ProbeRecord(ProbeStep::lagChange, 1 + i % 2, 2 - i % 2, 5);
    }
    Require(gWritten == 1, "a flickering lag cannot hold a window open past its cap");
}

void TestOverwriteAndTimeout()
{
    ResetProbe();
    Frame(10);
    ProbeRecord(ProbeStep::panelClose);
    // Flood well past the ring before the window closes.
    for (std::uint64_t i = 0; i < kRingSize + 100; ++i) ProbeRecord(ProbeStep::passCreated, 1, 1, 0x1000, 0);
    Frame(12);
    ServiceFrameProbe();
    const auto events = Lines("preyvr_frame_probe w=1 ");
    Require(!events.empty() && events.size() <= kRingSize, "a window is bounded by the ring");
    Require(Logged("frame=1/1 caller=0x1000 flags=0x0"), "pass args");

    ResetProbe();
    Frame(10);
    ProbeRecord(ProbeStep::panelClose);
    ServiceFrameProbe();
    Require(gWindowOpen && gEmitted == 0, "no frame starts yet: still open");
    {
        std::lock_guard lock(gWindowMutex);
        gOpen.triggerQpc -= 1000000000000LL; // far in the past, whatever the frequency
    }
    ServiceFrameProbe();
    Require(!gWindowOpen && gEmitted == 1 && Logged("window=1 kinds=panel_close"),
            "a window without frame starts is written after the timeout");
}

void TestThreadsAndFormat()
{
    ResetProbe();
    // The render thread stays alive while the job thread runs: Windows reuses
    // the id of a thread that has exited.
    std::atomic<bool> release{false};
    std::thread render([&] {
        ProbeRecord(ProbeStep::endFrame, 7); // the first end frame names the render thread
        while (!release.load()) Sleep(1);
        ProbeRecord(ProbeStep::sceneEnter, 0);
        ProbeRecord(ProbeStep::sceneExit, static_cast<std::uint32_t>(ProbeScene::doubled), 0);
    });
    while (gRenderThread.load() == 0) Sleep(1);
    Frame(9);
    std::thread([] { ProbeRecord(ProbeStep::consumerEnter, 1500, 1499, 640, 320); }).join();
    release.store(true);
    render.join();
    ProbeRecord(ProbeStep::tick, kLagUnknown, 0, 0, 0);
    ProbeRecord(ProbeStep::panelClose);
    Frame(12);
    ServiceFrameProbe();
    Require(Logged(" M tick depth=? pending=0"), "an unobserved tick prints its depth as unknown");
    Require(Logged(" R scene_exit outcome=doubled type=0"), "the render thread is tagged R");
    Require(Logged(" J") && Logged("consumer_enter frame=1500/1499 q=640/320"),
            "other threads are tagged J with their id");
    Require(Logged(" main=") && Logged(" render="), "the header names the main and render threads");
}

void TestGuardSteps()
{
    ResetProbe();
    Frame(10);
    ProbeRecord(ProbeStep::prepare, 4, 4, 3);                // PostUpdate's: skipped at the origin
    ProbeRecord(ProbeStep::prepareKick, 4, 1, 7, 0x100 | 3); // the guard's, before Render
    Frame(12);
    ServiceFrameProbe();
    Require(gWritten == 1 && gControls == 0 && Logged("kinds=prepare_kick control=0"),
            "a kick opens a window of its own, not a control");
    Require(Logged(" M prepare state=4->4 gates=rw-"), "the engine's prepare with the gate that closed");
    Require(Logged(" M prepare_kick state=4->1 gates=rwc last=skipped <trigger"), "the kick and what preceded it");

    ResetProbe();
    Frame(10);
    ProbeRecord(ProbeStep::renderEnter, kStateUnknown, 0, 1, 1);
    ProbeRecord(ProbeStep::prepare, kStateUnknown, kStateUnknown, 8);
    ProbeRecord(ProbeStep::orphan, 4, 3, 0, 0);
    Frame(12);
    ServiceFrameProbe();
    Require(gWritten == 1 && Logged("kinds=orphan control=0"), "an orphan opens a window");
    Require(Logged(" M orphan state=4->3 guard=0 last=none <trigger"), "with the guard and the prepare");
    Require(Logged(" M render_enter state=? q=1/1") && Logged(" M prepare state=?->? gates=u"),
            "unread states and gates print as such");
}

unsigned gConsumerCalls = 0;
void* gConsumerParams = nullptr;
void __fastcall FakeConsumer(void* params)
{
    ++gConsumerCalls;
    gConsumerParams = params;
}
void TestDetours()
{
    ResetProbe();
    std::vector<std::uint8_t> params(0x48, 0);
    const std::uint32_t frame = 900, frame2 = 899;
    std::memcpy(params.data() + kJobFrameId, &frame, sizeof(frame));
    std::memcpy(params.data() + kJobFrameId2, &frame2, sizeof(frame2));
    gConsumerOriginal.store(&FakeConsumer);
    gIndices = {{700, 400}, {700, 700}};
    ConsumerJobDetour(params.data());
    Require(gConsumerCalls == 1 && gConsumerParams == params.data(), "the consumer runs once, unchanged");
    Require(gNext == 2, "entry and exit recorded");
    Event in{}, out{};
    Require(ReadEvent(0, in) && ReadEvent(1, out), "both readable");
    Require(in.step == static_cast<std::uint32_t>(ProbeStep::consumerEnter) && in.a == 900 && in.b == 899 &&
                in.c == 700 && in.d == 400,
            "entry: its frame and the queue it starts on");
    Require(out.step == static_cast<std::uint32_t>(ProbeStep::consumerExit) && out.c == 700 && out.d == 700,
            "exit: the queue it left");
    gRecording.store(false);
    ConsumerJobDetour(params.data());
    Require(gConsumerCalls == 2 && gNext == 2, "off: the consumer still runs, nothing recorded");

    ResetProbe();
    // A pass the original has filled: its two frame ids where 0x1E5B30 stores them.
    std::vector<std::uint8_t> pass(0x40, 0);
    const std::uint32_t passFrame = 4242, passFrame2 = 4241;
    std::memcpy(pass.data() + kPassFrameId, &passFrame, sizeof(passFrame));
    std::memcpy(pass.data() + kPassFrameId2, &passFrame2, sizeof(passFrame2));
    const auto image = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    gBase.store(0);
    ProbePassCreated(pass.data(), 0x2E5DF, reinterpret_cast<const void*>(image + 0x1234));
    Event created{};
    Require(ReadEvent(0, created) && created.a == 4242 && created.b == 4241 && created.d == 0x2E5DF &&
                created.c == kCallerOutsidePreyDll,
            "frame ids and flags, a caller with no PreyDll base marked as outside");
    gBase.store(image);
    ProbePassCreated(pass.data(), 0x2E5DF, reinterpret_cast<const void*>(image + 0x1234));
    Require(ReadEvent(1, created) && created.c == 0x1234, "a caller inside PreyDll is its RVA");
    ProbePassCreated(pass.data(), 0x2E5DF, reinterpret_cast<const void*>(image - 0x10));
    Require(ReadEvent(2, created) && created.c == kCallerOutsidePreyDll, "a caller below it is outside");
    ProbePassCreated(nullptr, 1, nullptr);
    gRecording.store(false);
    ProbePassCreated(pass.data(), 1, nullptr);
    Require(gNext == 3, "no pass or not recording: nothing recorded");
}

void TestGateAndCommands()
{
    std::vector<std::uint8_t> code(std::begin(kConsumerJobCode), std::end(kConsumerJobCode));
    code[48] ^= 1; // the call into the consumer loop
    ConsumerJobFn ignored = nullptr;
    Require(hook::InstallVerified(code.data(), kConsumerJobCode, reinterpret_cast<void*>(&ConsumerJobDetour), ignored)
                    .step == hook::Step::prologue,
            "a different consumer job is refused");

    ResetProbe();
    gRecording.store(false);
    Require(SetFrameProbe(2) == 1 && !gRecording, "bad value refused");
    Require(SetFrameProbe(1) == 0 && gRecording, "recording without the hooks");
    Require(Logged("detail=preydll_not_loaded hooks=none") && Logged("consumerHook=0 passHook=0 mutation=none"),
            "the hooks' absence is logged");
    Require(FrameProbeReport().find(" recording=1 consumerHook=0 passHook=0 events=") == 0, "report leads with state");
    Require(SetFrameProbe(0) == 0 && !gRecording, "off");

    gStartAttempted.store(false);
    gPinned = 0;
    ServiceFrameProbe();
    Require(!gStartAttempted, "start waits for the pin");
    gPinned = 1;
    SetEnvironmentVariableW(L"PREYVR_FRAME_PROBE", L"0");
    ServiceFrameProbe();
    Require(gStartAttempted && !gRecording && Logged("detail=off reason=PREYVR_FRAME_PROBE"), "opt-out honoured");
    SetEnvironmentVariableW(L"PREYVR_FRAME_PROBE", nullptr);
}
} // namespace

int main()
{
    TestOff();
    TestControlWindow();
    TestQuotas();
    TestMergedTriggers();
    TestOverwriteAndTimeout();
    TestThreadsAndFormat();
    TestGuardSteps();
    TestDetours();
    TestGateAndCommands();
    std::cout << "frame probe tests passed\n";
    return 0;
}
