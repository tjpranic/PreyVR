#pragma once

// The scaffolding the physical lanes (ContactLane, SwingLane, SurfaceStopLane)
// share: the re-entry guard, the event-line budget, the 10 s report's throttle,
// the gate chain each asks first, the performance-counter timing and the window
// a speed is trusted in. Each lane keeps its own state, thresholds and log
// lines; these are only the mechanics. Header-only so the lane tests, which
// compile a lane's .cpp directly, need nothing more to link.
#include "aim/GameplayFrame.h"
#include "render/camera/HeadTrackingHook.h"
#include "body/RoomScaleLane.h"
#include "ui/screens/UiMode.h"
#include "base/MonotonicClock.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace preyvr::dll::lane {

// Holds a lane's thread-local busy flag for one update, so the update never
// runs inside itself.
struct ReentryGuard {
    bool& busy;
    explicit ReentryGuard(bool& flag) : busy(flag) { busy = true; }
    ~ReentryGuard() { busy = false; }
};

// At most `perSecond` event lines a second, counted in the lane's own window
// (`secondNs`, `lines`). True spends one; false counts the line in `dropped`.
inline bool TakeLine(std::uint64_t& secondNs, unsigned& lines, unsigned perSecond, std::atomic<std::uint64_t>& dropped)
{
    const auto now = MonotonicNanoseconds();
    if (now - secondNs >= 1000000000ull) { secondNs = now; lines = 0; }
    if (lines >= perSecond) { ++dropped; return false; }
    ++lines;
    return true;
}

// The 10 s report: due at most every 10 s, and then only when the sum of the
// counters that matter (`counters()`) or the gate has moved since the last one.
struct ReportThrottle {
    std::uint64_t ns = 0, sum = 0;
    const char* gate = nullptr;
    template <class Counters> bool Due(Counters&& counters, const std::atomic<const char*>& lastGate)
    {
        const auto now = MonotonicNanoseconds();
        if (now - ns < 10000000000ull) { return false; }
        ns = now;
        const std::uint64_t total = counters();
        const char* const g = lastGate.load();
        if (total == sum && g == gate) { return false; }
        sum = total;
        gate = g;
        return true;
    }
};

// Why a lane stays shut this frame, asked in this order: the option off, a
// latched fault, tracking not allowed, a menu, the head out of the body, no
// usable camera, a stale pose or another reference. Null when none is.
inline const char* GateReason(const GameplayPoseFrame& frame, bool allowed, bool on, bool faulted, std::uint64_t now,
                              std::uint64_t freshNs)
{
    if (!on) { return "off"; }
    if (faulted) { return "fault"; }
    if (!allowed) { return "tracking"; }
    if (!HudGameplayInputAllowed()) { return "menu"; }
    if (BodyHeadOut()) { return "head_out"; }
    if (!frame.cameraCentreValid || !frame.headYawUsable || !std::isfinite(frame.yaw)) { return "camera"; }
    if (!FreshSample(now, frame.tracking.publishedNs, freshNs) ||
        frame.referenceGeneration != HeadTrackingReferenceGeneration()) { return "pose"; }
    return nullptr;
}

// Microseconds on the performance counter since construction.
struct Stopwatch {
    LARGE_INTEGER start{};
    Stopwatch() { QueryPerformanceCounter(&start); }
    std::uint64_t Micros() const
    {
        LARGE_INTEGER end{}, frequency{};
        QueryPerformanceCounter(&end);
        QueryPerformanceFrequency(&frequency);
        return static_cast<std::uint64_t>((end.QuadPart - start.QuadPart) * 1000000 / std::max<LONGLONG>(frequency.QuadPart, 1));
    }
};
// One timed stretch into a lane's running total and maximum.
inline void AddMicros(std::uint64_t us, std::atomic<std::uint64_t>& total, std::atomic<std::uint64_t>& peak)
{
    total += us;
    if (us > peak.load()) { peak = us; }
}

// The seconds between two pose stamps (ns) when a speed can be trusted from
// them, 1/240..1/20 s apart; else 0.
inline float TrustedDt(std::uint64_t stamp, std::uint64_t before)
{
    const float d = stamp > before ? static_cast<float>(stamp - before) * 1e-9f : 0.0f;
    return d >= 1.0f / 240.0f && d <= 1.0f / 20.0f ? d : 0.0f;
}

} // namespace preyvr::dll::lane
