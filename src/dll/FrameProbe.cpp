#include "FrameProbe.h"
#include "EnvSwitch.h"
#include "Logger.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace preyvr::dll {
namespace {
// A normal frame records about a dozen events across the three threads, so the
// ring holds several seconds: the command worker has time to write a window
// before it is overwritten, and a stall, which records little, fits whole.
constexpr std::uint64_t kRingSize = 8192;
constexpr std::uint64_t kTickHistory = 64;
// Main frames kept before the trigger and written after it. A window also stays
// open until its stall has ended, then kAfterEndTicks more; never past
// kMaxWindowTicks.
constexpr std::uint64_t kBeforeTicks = 8, kAfterTicks = 12, kAfterEndTicks = 4, kMaxWindowTicks = 120;
constexpr unsigned long long kMaxWindows = 16;
// A window still open this long after its trigger (the stall never ended, or
// the game stopped ticking) is written with what it has.
constexpr std::int64_t kWindowTimeoutUs = 5000000;

struct Slot {
    std::atomic<std::uint64_t> seq{0}; // index + 1 once written, 0 while being written
    std::atomic<std::int64_t> qpc{0};
    std::atomic<std::uint32_t> thread{0}, step{0}, a{0}, b{0}, c{0}, d{0};
};
struct Event {
    std::uint64_t index = 0;
    std::int64_t qpc = 0;
    std::uint32_t thread = 0, step = 0, a = 0, b = 0, c = 0, d = 0;
};
struct Window {
    std::uint64_t start = 0, trigger = 0, end = 0; // event indices, end exclusive
    std::int64_t triggerQpc = 0;
    std::uint64_t firstTick = 0, endTick = 0;
    unsigned stalls = 0;
    std::uint32_t gapMs = 0, stallMs = 0, held = 0;
    bool ended = false, load = false;
};

std::array<Slot, kRingSize> gRing;
std::atomic<std::uint64_t> gNext{0};
std::array<std::atomic<std::uint64_t>, kTickHistory> gTickIndex{};
std::atomic<std::uint64_t> gTicks{0};
std::atomic<DWORD> gMainThread{0}, gRenderThread{0}, gHoldThread{0};

std::mutex gWindowMutex;
Window gOpen{};                       // guarded by gWindowMutex
std::atomic<bool> gWindowOpen{false};
constexpr std::size_t kReadySlots = 4;
std::array<Window, kReadySlots> gReady{}; // guarded by gWindowMutex
unsigned gReadyCount = 0;             // guarded by gWindowMutex

std::atomic<bool> gRecording{false}, gStartAttempted{false};
std::atomic<unsigned long long> gStalls{0}, gLoads{0}, gWritten{0}, gSuppressed{0}, gLost{0}, gEmitted{0};

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_frame_probe " + line);
}

std::int64_t Qpc()
{
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

std::int64_t QpcMicros(std::int64_t ticks)
{
    static const std::int64_t frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? f.QuadPart : 1;
    }();
    return ticks / frequency * 1000000 + ticks % frequency * 1000000 / frequency;
}

// Seqlock read: the slot must still hold `index` after the copy.
bool ReadEvent(std::uint64_t index, Event& out)
{
    const Slot& slot = gRing[index % kRingSize];
    if (slot.seq.load(std::memory_order_acquire) != index + 1) {
        return false;
    }
    out.index = index;
    out.qpc = slot.qpc.load(std::memory_order_relaxed);
    out.thread = slot.thread.load(std::memory_order_relaxed);
    out.step = slot.step.load(std::memory_order_relaxed);
    out.a = slot.a.load(std::memory_order_relaxed);
    out.b = slot.b.load(std::memory_order_relaxed);
    out.c = slot.c.load(std::memory_order_relaxed);
    out.d = slot.d.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    return slot.seq.load(std::memory_order_relaxed) == index + 1;
}

// Caller holds gWindowMutex. Queues the open window, drops it as a load, or
// counts it suppressed.
void Finish(std::uint64_t end)
{
    gOpen.end = end;
    if (gOpen.load) {
        gLoads.fetch_add(1, std::memory_order_relaxed);
    } else if (gWritten.load(std::memory_order_relaxed) < kMaxWindows && gReadyCount < kReadySlots) {
        gWritten.fetch_add(1, std::memory_order_relaxed);
        gReady[gReadyCount++] = gOpen;
    } else {
        gSuppressed.fetch_add(1, std::memory_order_relaxed);
    }
    gOpen = {};
    gWindowOpen.store(false, std::memory_order_release);
}

void BeginStall(std::uint64_t index, std::int64_t qpc, std::uint32_t gapMs, bool loading)
{
    std::lock_guard lock(gWindowMutex);
    gStalls.fetch_add(1, std::memory_order_relaxed);
    if (gWindowOpen.load(std::memory_order_relaxed)) {
        // Another stall soon after joins the window, which waits for it too.
        ++gOpen.stalls;
        gOpen.ended = false;
        gOpen.load = gOpen.load || loading;
        return;
    }
    if (loading) {
        gLoads.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const std::uint64_t ticks = gTicks.load(std::memory_order_acquire);
    gOpen = {};
    std::uint64_t start = ticks > kBeforeTicks
        ? gTickIndex[(ticks - kBeforeTicks) % kTickHistory].load(std::memory_order_relaxed) : 0;
    // Never older than the ring still holds once the window has run its course.
    const std::uint64_t reach = kRingSize / 2;
    if (index > reach && start < index - reach) start = index - reach;
    gOpen.start = (std::min)(start, index);
    gOpen.trigger = index;
    gOpen.triggerQpc = qpc;
    gOpen.firstTick = ticks;
    gOpen.endTick = ticks + kAfterTicks;
    gOpen.stalls = 1;
    gOpen.gapMs = gapMs;
    gWindowOpen.store(true, std::memory_order_release);
}

void EndStall(std::uint32_t stallMs, std::uint32_t held)
{
    std::lock_guard lock(gWindowMutex);
    if (!gWindowOpen.load(std::memory_order_relaxed)) {
        return;
    }
    const std::uint64_t ticks = gTicks.load(std::memory_order_acquire);
    gOpen.ended = true;
    gOpen.stallMs = (std::max)(gOpen.stallMs, stallMs);
    gOpen.held += held;
    gOpen.endTick = (std::min)((std::max)(gOpen.endTick, ticks + kAfterEndTicks), gOpen.firstTick + kMaxWindowTicks);
}

void MarkLoad()
{
    std::lock_guard lock(gWindowMutex);
    if (gWindowOpen.load(std::memory_order_relaxed)) gOpen.load = true;
}

void CloseIfDue(std::uint64_t ticks, std::uint64_t index)
{
    std::lock_guard lock(gWindowMutex);
    if (gWindowOpen.load(std::memory_order_relaxed) &&
        ((gOpen.ended && ticks >= gOpen.endTick) || ticks >= gOpen.firstTick + kMaxWindowTicks)) {
        Finish(index + 1);
    }
}

constexpr const char* kStepNames[] = {"tick", "gameplay", "render_enter", "render_exit", "scene_enter",
    "scene_exit", "end_frame", "xr_wait", "xr_acquire", "xr_end", "present", "panel_open", "panel_close",
    "load", "hold_begin", "hold_end"};

const char* StepName(std::uint32_t step)
{
    return step < std::size(kStepNames) ? kStepNames[step] : "unknown";
}

const char* SceneName(std::uint32_t outcome)
{
    static constexpr const char* names[] = {"single", "doubled", "build_miss", "refused", "no_teardown"};
    return outcome < std::size(names) ? names[outcome] : "unknown";
}

// The steps that carry their own duration in a, in microseconds.
bool Timed(std::uint32_t step)
{
    switch (static_cast<ProbeStep>(step)) {
    case ProbeStep::xrWait:
    case ProbeStep::xrAcquire:
    case ProbeStep::xrEnd:
    case ProbeStep::present:
        return true;
    default:
        return false;
    }
}

struct Threads {
    DWORD main = 0, render = 0, hold = 0;
};

void AppendThread(std::ostringstream& line, DWORD thread, const Threads& threads)
{
    if (thread == threads.main) line << 'M';
    else if (thread == threads.render) line << 'R';
    else if (thread == threads.hold) line << 'H';
    else line << 'J' << thread;
}

std::string FormatEvent(unsigned long long window, const Event& e, std::int64_t triggerQpc, const Threads& threads)
{
    std::ostringstream line;
    line << "w=" << window << ' ' << (e.qpc >= triggerQpc ? "+" : "-")
         << QpcMicros(e.qpc >= triggerQpc ? e.qpc - triggerQpc : triggerQpc - e.qpc) << "us ";
    AppendThread(line, e.thread, threads);
    line << ' ' << StepName(e.step);
    switch (static_cast<ProbeStep>(e.step)) {
    case ProbeStep::sceneEnter:
        line << " type=" << e.a;
        break;
    case ProbeStep::sceneExit:
        line << " outcome=" << SceneName(e.a) << " type=" << e.b;
        break;
    case ProbeStep::endFrame:
        line << " frame=" << e.a;
        break;
    case ProbeStep::xrWait:
        line << " us=" << e.a << " render=" << e.b;
        break;
    case ProbeStep::xrAcquire:
    case ProbeStep::present:
        line << " us=" << e.a;
        break;
    case ProbeStep::xrEnd:
        line << " us=" << e.a << " layers=" << e.b;
        break;
    case ProbeStep::holdBegin:
        line << " gap_ms=" << e.a << " loading=" << e.b;
        break;
    case ProbeStep::holdEnd:
        line << " stall_ms=" << e.a << " held=" << e.b;
        break;
    default:
        break;
    }
    return line.str();
}

// The verdict a reader wants first: on the main and render threads, the
// longest time between two of their own steps, and which two; and the longest
// single call that times itself.
struct Gap {
    std::int64_t us = -1;
    std::uint32_t from = 0, to = 0;
};
struct Summary {
    Gap main, render;
    std::uint32_t callStep = 0, callUs = 0;
    bool haveCall = false;
};

void Observe(Summary& summary, const Event& e, const Threads& threads, const Event*& lastMain,
             const Event*& lastRender)
{
    const auto widen = [&](Gap& gap, const Event*& last) {
        if (last != nullptr) {
            const std::int64_t us = QpcMicros(e.qpc >= last->qpc ? e.qpc - last->qpc : 0);
            if (us > gap.us) gap = {us, last->step, e.step};
        }
        last = &e;
    };
    if (e.thread == threads.main) widen(summary.main, lastMain);
    else if (e.thread == threads.render) widen(summary.render, lastRender);
    if (Timed(e.step) && (!summary.haveCall || e.a > summary.callUs)) {
        summary.haveCall = true;
        summary.callStep = e.step;
        summary.callUs = e.a;
    }
}

void AppendGap(std::ostringstream& line, const char* name, const Gap& gap)
{
    line << ' ' << name << '=';
    if (gap.us < 0) {
        line << '?';
        return;
    }
    line << gap.us / 1000 << "ms(" << StepName(gap.from) << "->" << StepName(gap.to) << ')';
}

// Command worker: one finished window to the log.
void Emit(const Window& window)
{
    const auto n = gEmitted.fetch_add(1, std::memory_order_relaxed) + 1;
    const Threads threads{gMainThread.load(std::memory_order_relaxed), gRenderThread.load(std::memory_order_relaxed),
                          gHoldThread.load(std::memory_order_relaxed)};
    std::uint64_t start = window.start;
    if (window.end > start + kRingSize) start = window.end - kRingSize;
    std::ostringstream header;
    header << "window=" << n << " kinds=stall stalls=" << window.stalls << " stall_ms=";
    if (window.ended) header << window.stallMs;
    else header << '?';
    header << " held=" << window.held << " gap_ms=" << window.gapMs << " events=" << window.end - start
           << " before=" << kBeforeTicks << " after=" << kAfterTicks << " main=" << threads.main
           << " render=" << threads.render << " hold=" << threads.hold << " J=job/other thread";
    Log(header.str());
    unsigned long long lost = 0;
    // Kept so the summary's gaps can name both ends; a window is a few hundred events.
    std::vector<Event> events;
    events.reserve(static_cast<std::size_t>(window.end - start));
    for (std::uint64_t index = start; index < window.end; ++index) {
        Event e;
        if (!ReadEvent(index, e)) {
            ++lost;
            continue;
        }
        events.push_back(e);
        Log(FormatEvent(n, e, window.triggerQpc, threads) + (index == window.trigger ? " <trigger" : ""));
    }
    Summary summary;
    const Event* lastMain = nullptr;
    const Event* lastRender = nullptr;
    for (const auto& e : events) Observe(summary, e, threads, lastMain, lastRender);
    std::ostringstream verdict;
    verdict << "window=" << n << " summary";
    AppendGap(verdict, "main", summary.main);
    AppendGap(verdict, "render", summary.render);
    verdict << " call=";
    if (summary.haveCall) verdict << StepName(summary.callStep) << ':' << summary.callUs / 1000 << "ms";
    else verdict << '?';
    Log(verdict.str());
    gLost.fetch_add(lost, std::memory_order_relaxed);
    Log("window=" + std::to_string(n) + " end lost=" + std::to_string(lost));
}
} // namespace

void ProbeRecord(ProbeStep step, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d)
{
    if (!gRecording.load(std::memory_order_relaxed)) {
        return;
    }
    const std::int64_t qpc = Qpc();
    const DWORD thread = GetCurrentThreadId();
    const std::uint64_t index = gNext.fetch_add(1, std::memory_order_relaxed);
    Slot& slot = gRing[index % kRingSize];
    slot.seq.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    slot.qpc.store(qpc, std::memory_order_relaxed);
    slot.thread.store(thread, std::memory_order_relaxed);
    slot.step.store(static_cast<std::uint32_t>(step), std::memory_order_relaxed);
    slot.a.store(a, std::memory_order_relaxed);
    slot.b.store(b, std::memory_order_relaxed);
    slot.c.store(c, std::memory_order_relaxed);
    slot.d.store(d, std::memory_order_relaxed);
    slot.seq.store(index + 1, std::memory_order_release);

    const auto name = [thread](std::atomic<DWORD>& role) {
        DWORD none = 0;
        role.compare_exchange_strong(none, thread, std::memory_order_relaxed);
    };
    switch (step) {
    case ProbeStep::tick: {
        name(gMainThread);
        const std::uint64_t ticks = gTicks.load(std::memory_order_relaxed) + 1;
        gTickIndex[ticks % kTickHistory].store(index, std::memory_order_relaxed);
        gTicks.store(ticks, std::memory_order_release);
        if (gWindowOpen.load(std::memory_order_acquire)) CloseIfDue(ticks, index);
        break;
    }
    case ProbeStep::endFrame: name(gRenderThread); break;
    case ProbeStep::load: MarkLoad(); break;
    case ProbeStep::holdBegin:
        name(gHoldThread);
        BeginStall(index, qpc, a, b != 0);
        break;
    case ProbeStep::holdEnd:
        name(gHoldThread);
        EndStall(a, b);
        break;
    default: break;
    }
}

std::int64_t ProbeClock()
{
    return gRecording.load(std::memory_order_relaxed) ? Qpc() : 0;
}

std::uint32_t ProbeMicrosSince(std::int64_t start)
{
    if (start == 0) return 0;
    const std::int64_t us = QpcMicros((std::max)(Qpc() - start, std::int64_t{0}));
    return static_cast<std::uint32_t>((std::min)(us, std::int64_t{(std::numeric_limits<std::uint32_t>::max)()}));
}

DWORD SetFrameProbe(unsigned int enabled)
{
    if (enabled > 1) {
        return 1;
    }
    gRecording.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=recording value=") + (enabled != 0 ? "1" : "0") + " trigger=hold_stall mutation=none");
    return 0;
}

void ServiceFrameProbe()
{
    if (!gStartAttempted.exchange(true, std::memory_order_acq_rel)) {
        if (EnvSwitchOff(L"PREYVR_FRAME_PROBE")) {
            Log("result=0 detail=off reason=PREYVR_FRAME_PROBE");
            return;
        }
        SetFrameProbe(1);
    }
    std::array<Window, kReadySlots> ready{};
    unsigned count = 0;
    {
        std::lock_guard lock(gWindowMutex);
        if (gWindowOpen.load(std::memory_order_relaxed) &&
            QpcMicros(Qpc() - gOpen.triggerQpc) > kWindowTimeoutUs) {
            Finish(gNext.load(std::memory_order_relaxed));
        }
        for (; count < gReadyCount; ++count) ready[count] = gReady[count];
        gReadyCount = 0;
    }
    for (unsigned i = 0; i < count; ++i) Emit(ready[i]);
}

std::string FrameProbeReport()
{
    const auto n = [](const std::atomic<unsigned long long>& value) { return value.load(std::memory_order_relaxed); };
    std::ostringstream out;
    out << " recording=" << (gRecording.load() ? 1 : 0) << " events=" << gNext.load() << " ticks=" << gTicks.load()
        << " stalls=" << n(gStalls) << " loads=" << n(gLoads) << " windows=" << n(gWritten)
        << " suppressed=" << n(gSuppressed) << " emitted=" << n(gEmitted) << " lost=" << n(gLost)
        << " open=" << (gWindowOpen.load() ? 1 : 0) << " main=" << gMainThread.load()
        << " render=" << gRenderThread.load() << " hold=" << gHoldThread.load();
    return out.str();
}
} // namespace preyvr::dll
