#pragma once
#include <windows.h>

#include <cstdint>
#include <string>

namespace preyvr::dll {
// A flight recorder for the cull lag behind the PreyDll+0x27EC40 crash
// (docs/CULL-CAMERA-CRASH-2026-10-17.md). The lag starts on a VR-panel close
// where the main and render threads disagree about one frame, so every frame
// step of both threads, and of the cull consumer job between them, goes into a
// lock-free ring. A panel close, a first-eye refusal, a cull-lag change, a
// prepare the guard had to make or an orphaned cull batch opens a window from
// kBeforeTicks main frames before it to kAfterTicks after; the
// command worker then writes that window to the log, off the game threads.
// Only read-only observation: one hook of its own (the consumer job) that calls
// the original unchanged, plus records from seams other lanes already own --
// CreateGeneralPassRenderingInfo through PassCreationHook's hook among them.
//
// Started with the cull watch once the module is pinned, VR or not.
// PREYVR_FRAME_PROBE=0 leaves it off; cull.probe 0|1 switches recording.
enum class ProbeStep : std::uint16_t {
    tick,            // main: C3DEngine pool reset, the frame start. a=lag depth b=pending c/d=queue write/read
    renderEnter,     // main: CSystem::Render calls the original. a=cull handshake state c/d=queue write/read
    renderExit,      // main: the original returned. a=cull handshake state c/d=queue write/read
    passCreated,     // any thread: CreateGeneralPassRenderingInfo. a/b=frame ids c=caller RVA d=flags
    consumerEnter,   // job: the cull consumer (0x23F1B0). a/b=its frame ids c/d=queue write/read
    consumerExit,    // job: it returned. a/b=frame ids c/d=queue write/read
    sceneEnter,      // render: a top-level RT_RenderScene. a=view type
    sceneExit,       // render: it returned. a=ProbeScene outcome b=view type
    endFrame,        // render: RT_EndFrame. a=frame count
    panelOpen,       // render (XR): the VR panel is shown
    panelClose,      // render (XR): the VR panel is hidden -- a trigger
    firstEyeRefused, // render (XR): a both-eyes contract with no held first eye -- a trigger. a=refusals
    lagChange,       // main: the cull lag depth changed -- a trigger. a=depth b=was c=pending
    // CullPrepareGuard.h. States are CCullThread's handshake, kCullStateUnknown unread.
    prepare,         // main: C3DEngine::PrepareOcclusion. a/b=state before/after c=its gates
    prepareKick,     // main: the guard called it before Render -- a trigger. a/b=state c=gates d=last prepare
    orphan,          // main: CullStart parked with no prepare in flight -- a trigger. a/b=state c=guard d=last prepare
};
// sceneExit outcomes, from the dual lane's own counters.
enum class ProbeScene : std::uint32_t { single = 0, doubled = 1, buildMiss = 2, refused = 3, noTeardown = 4 };
// a=kLagUnknown in a tick: the cull watch did not observe that reset.
constexpr std::uint32_t kLagUnknown = 0xFF;

// Cheap and lock-free on every thread; a no-op while recording is off.
void ProbeRecord(ProbeStep step, std::uint32_t a = 0, std::uint32_t b = 0, std::uint32_t c = 0,
                 std::uint32_t d = 0);
// From the CreateGeneralPassRenderingInfo hook PassCreationHook owns, after the
// original has filled `out`: records its frame ids, its caller and its flags.
void ProbePassCreated(const void* out, std::uint32_t flags, const void* returnAddress);
// Command worker, every poll: starts the probe once pinned, writes finished windows.
void ServiceFrameProbe();
// cull.probe: 1 records, 0 stops (the hooks stay). Returns 0 or 1 for a bad value.
DWORD SetFrameProbe(unsigned int enabled);
std::string FrameProbeReport();
} // namespace preyvr::dll
