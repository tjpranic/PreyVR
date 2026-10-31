// Exercise the frame probe's ring, stall windows, summary and report without
// loading Prey. Including this TU exposes internal seams only inside this test
// executable.
#include "diagnostics/probes/FrameProbe.cpp"
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "DllTestSupport.h"
#include "TestSupport.h"

PREYVR_TEST_CAPTURE_LOG()

namespace {
using namespace preyvr::dll;
using preyvr::test::Count;
using preyvr::test::gLines;
using preyvr::test::Logged;
using preyvr::test::Require;

void ResetProbe()
{
    for (auto& slot : gRing) slot.seq.store(0);
    gNext.store(0);
    gTicks.store(0);
    for (auto& index : gTickIndex) index.store(0);
    gMainThread.store(0);
    gRenderThread.store(0);
    gHoldThread.store(0);
    {
        std::lock_guard lock(gWindowMutex);
        gOpen = {};
        gReadyCount = 0;
    }
    gWindowOpen.store(false);
    for (auto* counter : {&gStalls, &gLoads, &gWritten, &gSuppressed, &gLost, &gEmitted}) counter->store(0);
    gRecording.store(true);
    gStartAttempted.store(true);
    gLines.clear();
}

// One main frame, as the schedule records it.
void Frame(unsigned frames = 1)
{
    for (unsigned i = 0; i < frames; ++i) {
        ProbeRecord(ProbeStep::tick);
        ProbeRecord(ProbeStep::gameplay);
        ProbeRecord(ProbeStep::renderEnter);
        ProbeRecord(ProbeStep::renderExit);
    }
}

// The hold thread's steps, all from one thread of its own, as in the game.
class HoldThread {
public:
    HoldThread() : worker_([this] { Run(); }) {}
    ~HoldThread()
    {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        worker_.join();
    }
    void Record(ProbeStep step, std::uint32_t a, std::uint32_t b)
    {
        std::unique_lock lock(mutex_);
        step_ = step;
        a_ = a;
        b_ = b;
        pending_ = true;
        wake_.notify_all();
        wake_.wait(lock, [this] { return !pending_; });
    }

private:
    void Run()
    {
        std::unique_lock lock(mutex_);
        for (;;) {
            wake_.wait(lock, [this] { return pending_ || stop_; });
            if (pending_) {
                ProbeRecord(step_, a_, b_);
                pending_ = false;
                wake_.notify_all();
            } else if (stop_) {
                return;
            }
        }
    }
    std::mutex mutex_;
    std::condition_variable wake_;
    ProbeStep step_ = ProbeStep::holdBegin;
    std::uint32_t a_ = 0, b_ = 0;
    bool pending_ = false, stop_ = false;
    std::thread worker_;
};
HoldThread* gHold = nullptr;

void Hold(ProbeStep step, std::uint32_t a, std::uint32_t b)
{
    gHold->Record(step, a, b);
}

void TestOff()
{
    ResetProbe();
    gRecording.store(false);
    Frame(3);
    ProbeRecord(ProbeStep::holdBegin, 100, 0);
    Require(gNext == 0 && gTicks == 0 && !gWindowOpen && gStalls == 0, "off: nothing recorded, nothing opened");
    Require(ProbeClock() == 0 && ProbeMicrosSince(0) == 0, "off: the clock reads 0, so callers skip their reads");
}

void TestStallWindow()
{
    ResetProbe();
    Frame(10);
    Hold(ProbeStep::holdBegin, 104, 0);
    Require(gWindowOpen && gStalls == 1, "a stall opens a window");
    Frame(3);
    Hold(ProbeStep::holdEnd, 207, 1);
    Frame(kAfterTicks - 3 - 1);
    Require(gWindowOpen, "open until kAfterTicks after the trigger");
    Frame(1);
    Require(!gWindowOpen && gReadyCount == 1, "closed, queued for the worker");
    ServiceFrameProbe();
    Require(Logged("window=1 kinds=stall stalls=1 stall_ms=207 held=1 gap_ms=104"), "the header carries the stall");
    Require(Logged(" H hold_begin gap_ms=104 loading=0 <trigger") && Logged(" H hold_end stall_ms=207 held=1"),
            "the hold thread's steps, the trigger marked");
    Require(Count("w=1 -") > 0 && Logged(" M tick"), "frames from before the trigger");
    Require(Logged("window=1 summary main=") && Logged("window=1 end lost=0"), "a summary and an end line");
    Require(gWritten == 1 && gEmitted == 1, "one window written");
}

void TestLoadsOpenNothing()
{
    ResetProbe();
    Frame(10);
    Hold(ProbeStep::holdBegin, 101, 1);
    Require(!gWindowOpen && gLoads == 1 && gStalls == 1, "a stall with a loading screen waiting is a load");

    // Captured only after the stall began: the window is dropped as a load.
    Hold(ProbeStep::holdEnd, 9600, 480);
    Hold(ProbeStep::holdBegin, 100, 0);
    Require(gWindowOpen, "not known to be a load yet");
    ProbeRecord(ProbeStep::load);
    Hold(ProbeStep::holdEnd, 9500, 470);
    Frame(kAfterTicks + 1);
    ServiceFrameProbe();
    Require(gLoads == 2 && gWritten == 0 && !Logged("kinds=stall"), "a window with a load in it is dropped");
}

void TestWaitsForTheStallToEnd()
{
    ResetProbe();
    Frame(10);
    Hold(ProbeStep::holdBegin, 100, 0);
    // The main thread can go on ticking while the render thread is the one stuck.
    Frame(kAfterTicks + 5);
    Require(gWindowOpen, "not before the stall has ended");
    Hold(ProbeStep::holdEnd, 390, 22);
    Frame(kAfterEndTicks - 1);
    Require(gWindowOpen, "and a few frames after it");
    Frame(1);
    Require(!gWindowOpen, "then closed");
    ServiceFrameProbe();
    Require(Logged("stall_ms=390 held=22"), "with the stall's length");

    ResetProbe();
    Frame(10);
    Hold(ProbeStep::holdBegin, 100, 0);
    Frame(kMaxWindowTicks);
    Require(!gWindowOpen, "never past kMaxWindowTicks");
    ServiceFrameProbe();
    Require(Logged("window=1 kinds=stall stalls=1 stall_ms=? held=0"), "an unended stall says so");
}

void TestMergedStalls()
{
    ResetProbe();
    Frame(10);
    Hold(ProbeStep::holdBegin, 100, 0);
    Hold(ProbeStep::holdEnd, 210, 1);
    Frame(2);
    Hold(ProbeStep::holdBegin, 100, 0);
    Frame(3);
    Hold(ProbeStep::holdEnd, 164, 1);
    Frame(kAfterTicks);
    ServiceFrameProbe();
    Require(gStalls == 2 && gWritten == 1 && Logged("window=1 kinds=stall stalls=2 stall_ms=210 held=2"),
            "a second stall joins the window: the longest and the frames held by both");
}

void TestQuota()
{
    ResetProbe();
    for (unsigned i = 0; i < kMaxWindows + 4; ++i) {
        Frame(10);
        Hold(ProbeStep::holdBegin, 100, 0);
        Hold(ProbeStep::holdEnd, 150, 1);
        Frame(kAfterTicks);
        ServiceFrameProbe();
    }
    Require(gWritten == kMaxWindows && gSuppressed == 4 && gEmitted == kMaxWindows, "kMaxWindows a session");

    // More finished than the worker has collected: the extra are suppressed.
    ResetProbe();
    for (unsigned i = 0; i < kReadySlots + 2; ++i) {
        Frame(10);
        Hold(ProbeStep::holdBegin, 100, 0);
        Hold(ProbeStep::holdEnd, 150, 1);
        Frame(kAfterTicks);
    }
    Require(gReadyCount == kReadySlots && gSuppressed == 2, "the ready queue is bounded");
}

void TestOverwriteAndTimeout()
{
    // A window finished but not yet written, while the ring turns over it.
    ResetProbe();
    Frame(10);
    Hold(ProbeStep::holdBegin, 100, 0);
    Hold(ProbeStep::holdEnd, 150, 1);
    Frame(kAfterTicks);
    Require(gReadyCount == 1, "queued");
    for (std::uint64_t i = 0; i < kRingSize + 100; ++i) ProbeRecord(ProbeStep::sceneEnter, 0);
    ServiceFrameProbe();
    Require(gEmitted == 1 && gLost > 0 && Logged("window=1 end lost="), "overwritten events counted as lost");

    // A stall that never ends, and a game that stopped ticking.
    ResetProbe();
    Frame(10);
    Hold(ProbeStep::holdBegin, 100, 0);
    {
        std::lock_guard lock(gWindowMutex);
        gOpen.triggerQpc -= 10 * Qpc(); // long ago
    }
    ServiceFrameProbe();
    Require(!gWindowOpen && gEmitted == 1 && Logged("window=1 kinds=stall stalls=1 stall_ms=? held=0"),
            "a window past the timeout is written with what it has");
}

// The render thread's frame, ending stuck in the runtime's wait for `stuckMs`.
void RenderFrameStuck(DWORD stuckMs)
{
    std::thread([stuckMs] {
        ProbeRecord(ProbeStep::endFrame, 7); // the first end frame names the render thread
        ProbeRecord(ProbeStep::sceneEnter, 0);
        ProbeRecord(ProbeStep::sceneExit, static_cast<std::uint32_t>(ProbeScene::doubled), 0);
        ProbeRecord(ProbeStep::xrAcquire, 900);
        ProbeRecord(ProbeStep::xrEnd, 1500, 3);
        ProbeRecord(ProbeStep::present, 2100);
        ProbeRecord(ProbeStep::endFrame, 8);
        Sleep(stuckMs);
        ProbeRecord(ProbeStep::xrWait, stuckMs * 1000 + 1000, 1);
    }).join();
}

void TestThreadsFormatAndSummary()
{
    ResetProbe();
    Frame(2);
    RenderFrameStuck(60);
    Hold(ProbeStep::holdBegin, 102, 0);
    ProbeRecord(ProbeStep::panelOpen);
    std::thread([] { ProbeRecord(ProbeStep::gameplay); }).join(); // a thread with no role
    Hold(ProbeStep::holdEnd, 160, 4);
    Frame(1);
    Sleep(150); // the main thread's longest gap: between its render and the next frame
    Frame(kAfterTicks);
    ServiceFrameProbe();
    Require(Logged(" R end_frame frame=7") && Logged(" R scene_enter type=0") &&
                Logged(" R scene_exit outcome=doubled type=0") && Logged(" R xr_acquire us=900") &&
                Logged(" R xr_end us=1500 layers=3") && Logged(" R present us=2100") &&
                Logged(" R xr_wait us=61000 render=1"),
            "the render thread's steps");
    Require(Logged(" M panel_open") && Logged(" J", " gameplay"), "main, and another thread by its id");
    Require(Logged("window=1 summary main=", "ms(render_exit->tick) render=", "ms(end_frame->xr_wait)"),
            "each thread's longest gap, between which two steps");
    Require(Logged("window=1 summary", " call=xr_wait:61ms"), "and the longest timed call");

    // The same, with the loading screen captured inside it: a load, not written.
    ResetProbe();
    Frame(2);
    Hold(ProbeStep::holdBegin, 102, 0);
    std::thread([] { ProbeRecord(ProbeStep::load); }).join();
    Hold(ProbeStep::holdEnd, 9000, 450);
    Frame(kAfterTicks);
    ServiceFrameProbe();
    Require(gLoads == 1 && gWritten == 0 && gEmitted == 0, "the load in it dropped the window");
}

void TestClock()
{
    ResetProbe();
    const auto start = ProbeClock();
    Require(start != 0, "recording: a real reading");
    Sleep(5);
    const auto us = ProbeMicrosSince(start);
    Require(us >= 4000 && us < 1000000, "microseconds since it");
}

void TestCommands()
{
    ResetProbe();
    gRecording.store(false);
    Require(SetFrameProbe(2) == 1 && !gRecording, "bad value refused");
    Require(SetFrameProbe(1) == 0 && gRecording && Logged("detail=recording value=1 trigger=hold_stall mutation=none"),
            "recording");
    Require(FrameProbeReport().find(" recording=1 events=") == 0 && FrameProbeReport().find(" stalls=0 loads=0") !=
                std::string::npos,
            "report leads with state");
    Require(SetFrameProbe(0) == 0 && !gRecording, "off");

    gStartAttempted.store(false);
    SetEnvironmentVariableW(L"PREYVR_FRAME_PROBE", L"0");
    ServiceFrameProbe();
    Require(gStartAttempted && !gRecording && Logged("detail=off reason=PREYVR_FRAME_PROBE"), "opt-out honoured");
    SetEnvironmentVariableW(L"PREYVR_FRAME_PROBE", nullptr);
    gStartAttempted.store(false);
    ServiceFrameProbe();
    Require(gRecording, "otherwise it starts on the first poll");
}
} // namespace

int main()
{
    HoldThread hold;
    gHold = &hold;
    TestOff();
    TestStallWindow();
    TestLoadsOpenNothing();
    TestWaitsForTheStallToEnd();
    TestMergedStalls();
    TestQuota();
    TestOverwriteAndTimeout();
    TestThreadsFormatAndSummary();
    TestClock();
    TestCommands();
    std::cout << "frame probe tests passed\n";
    return 0;
}
