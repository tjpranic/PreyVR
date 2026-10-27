#pragma once
#include <windows.h>

#include <cstdint>
#include <string>

namespace preyvr::dll {
// A flight recorder for the render freeze (docs/ENGINE-INSTABILITY.md):
// the rare gameplay stall of 100-400 ms where the headset shows held frames.
// The hold thread knows a stall has begun once Prey's render thread has not
// serviced an XR frame for the hold threshold; by then the frame that stalled is
// already in flight, so every frame step of the main thread, the render thread
// and the hold thread goes into a lock-free ring, and the stall's start opens a
// window from kBeforeTicks main frames before it to kAfterTicks after (and at
// least until the stall ends). The command worker then writes that window to
// the log, off the game threads, with one summary line: the longest gap on each
// thread, between which two steps, and the longest single call.
//
// Only records from seams other lanes already own; it has no hook of its own.
// A stall during a loading screen is a load, not a freeze, and opens nothing.
//
// Started by the command worker. PREYVR_FRAME_PROBE=0 leaves it off;
// frame.probe 0|1 switches recording.
enum class ProbeStep : std::uint16_t {
    tick,        // main: CSystem::Render's detour starts, the frame clock (TickMainRender)
    gameplay,    // main: the gameplay seam, the player's frame published (TickGameplayLanes)
    renderEnter, // main: Prey's own Render is called
    renderExit,  // main: it returned
    sceneEnter,  // render: a top-level RT_RenderScene. a=view type
    sceneExit,   // render: it returned. a=ProbeScene outcome b=view type
    endFrame,    // render: RT_EndFrame, before the XR frame. a=frame count
    xrWait,      // render: xrWaitFrame returned. a=microseconds in it b=shouldRender
    xrAcquire,   // render: the eye image acquired and waited for. a=microseconds
    xrEnd,       // render: xrEndFrame returned. a=microseconds in it b=layers
    present,     // render: Prey's RT_EndFrame, its present among it, returned. a=microseconds
    panelOpen,   // render (XR): the VR panel is shown
    panelClose,  // render (XR): the VR panel is hidden
    load,        // render: a loading screen's first capture; a window with one is a load
    holdBegin,   // hold: no frame for the threshold, so held frames start -- the trigger.
                 //   a=milliseconds since the last frame b=1 while a loading screen waits
    holdEnd,     // hold: a newer frame ended the stall. a=stall milliseconds b=frames held
};
// sceneExit outcomes, from the dual lane's own counters.
enum class ProbeScene : std::uint32_t { single = 0, doubled = 1, buildMiss = 2, refused = 3, noTeardown = 4 };

// Cheap and lock-free on every thread; a no-op while recording is off.
void ProbeRecord(ProbeStep step, std::uint32_t a = 0, std::uint32_t b = 0, std::uint32_t c = 0,
                 std::uint32_t d = 0);
// Microseconds from a QueryPerformanceCounter reading to now, for the steps that
// carry a duration; 0 when not recording, so a caller can skip its own reads.
std::int64_t ProbeClock();
std::uint32_t ProbeMicrosSince(std::int64_t start);
// Command worker, every poll: starts the probe once, writes finished windows.
void ServiceFrameProbe();
// frame.probe: 1 records, 0 stops. Returns 0 or 1 for a bad value.
DWORD SetFrameProbe(unsigned int enabled);
std::string FrameProbeReport();
} // namespace preyvr::dll
