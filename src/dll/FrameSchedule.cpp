#include "FrameSchedule.h"

#include "ArmsLane.h"
#include "ActionMaps.h"
#include "BodyEquipment.h"
#include "BodyFollow.h"
#include "CarryLane.h"
#include "ConsoleBridgeWin32.h"
#include "ContactLane.h"
#include "ControllerPrompts.h"
#include "CullCameraWatch.h"
#include "CullPrepareGuard.h"
#include "ResourceSetWatch.h"
#include "DebugOverlay.h"
#include "FocusHold.h"
#include "FrameCaptureWin32.h"
#include "FrameProbe.h"
#include "GrenadeLane.h"
#include "HudBridge.h"
#include "InputPost.h"
#include "InteractionLane.h"
#include "MoveLane.h"
#include "PsiMedkit.h"
#include "Psychoscope.h"
#include "SurfaceStopLane.h"
#include "SwingLane.h"
#include "UiPointer.h"
#include "VrMode.h"
#include "VrOptionsRuntime.h"
#include "WristLane.h"
#include "WrenchDraw.h"
#include "XrSessionHost.h"
#include "preyvr/EngineMap.h"

namespace preyvr::dll::schedule {

void PrepareGameplayFrame(GameplayPoseFrame& frame, bool allowed)
{
    // Before publishing: the takeover draws each hand where this puts it.
    UpdateSurfaceStop(frame, allowed);
}

void TickGameplayLanes(const GameplayPoseFrame& frame, bool allowed, bool tracking)
{
    UpdatePsiMedkit(frame, allowed);
    UpdatePsychoscopeGesture(frame, allowed);
    UpdateBodyEquipment(frame, allowed);
    // Before contact: a strike tells contact what not to push.
    UpdateSwingLane(frame, allowed);
    UpdateContactLane(frame, allowed);
    UpdateInteractionLane(frame, allowed);
    UpdateCarryLane(frame, allowed);
    UpdateGrenadeLane(frame, allowed);
    UpdateArmsLane(frame, allowed);
    UpdateWristLane(frame, allowed);
    DebugOverlayGameFrame(frame, tracking);
}

void TickMainRender()
{
    // **Queued input is drained here, not on the thread that produced it.**
    // `CSystem::Render` is the engine's main thread and, unlike the gameplay
    // camera callback, it still runs while a menu is up -- which is exactly the
    // case menu navigation needs. Called ahead of the hook's stereo-armed check:
    // driving a menu must not require stereo or head tracking to be enabled
    // first, and the early return would otherwise skip it.
    NoteBodyFollowRender();
    DrainQueuedInput();
    DrainQueuedHudCalls();
    DrainUiPointer();
    // Same thread and same frame as the drain: the lane produces at most two
    // axis events per frame and the drain consumes one, so producing anywhere
    // else would race the queue it feeds.
    UpdateMoveLane();
    UpdateTurnAndFireLanes();
    // After every lane that posts this frame, so it checks the scheme this
    // frame's events left; on the main thread, as the scheme's listeners need.
    ServiceControllerPromptsFrame();
    ServiceBindingDump();
    // The wrench's skipped draws finish here, a frame on (WrenchDraw.h).
    ServiceWrenchDrawFrame();
}

EngineRenderBracket BeforeEngineRender(void* system)
{
    // After every drain and the camera edit, so it tests the camera Render
    // is about to: if this frame's occlusion prepare was skipped though the
    // world is about to be drawn, make it now, or the frame's cull batch is
    // orphaned and the culler stays a frame behind (CullPrepareGuard.h).
    if (system != nullptr) {
        CullPrepareBeforeRender(static_cast<const std::uint8_t*>(system) + engine::SystemLayout::viewCamera);
    }
    // The frame probe's main-thread bracket around the world render
    // (FrameProbe.h): whether the cull queue drained inside it, and the cull
    // handshake on each side (CullPrepareGuard.h), which shows an orphaned batch.
    std::uint32_t write = 0, read = 0;
    CullQueueIndices(write, read);
    EngineRenderBracket bracket{};
    bracket.handshake = CullThreadState();
    ProbeRecord(ProbeStep::renderEnter, bracket.handshake, 0, write, read);
    return bracket;
}

void AfterEngineRender(const EngineRenderBracket& bracket)
{
    std::uint32_t write = 0, read = 0;
    CullQueueIndices(write, read);
    const std::uint32_t handshakeAfter = CullThreadState();
    ProbeRecord(ProbeStep::renderExit, handshakeAfter, 0, write, read);
    CullPrepareAfterRender(bracket.handshake, handshakeAfter);
}

void TickRenderThread(void* renderer, unsigned long long frame)
{
    // Both return on a single atomic load when nothing is armed, so the
    // observer stays as close to free as it was. They run before the original
    // so a console command lands at a frame boundary, and so a capture reads the
    // backbuffer this callback was invoked for.
    ServiceConsoleQueue();
    ObserveXrBackbuffer(renderer);
    ServiceFrameCapture(renderer, frame);
    // The XR frame runs here because this is the render thread and the only
    // place Prey's backbuffer is valid. Returns on one atomic load when no
    // session is running.
    ServiceXrFrame(renderer);
}

void TickWorker()
{
    // Once, as soon as the module is pinned: VR or not (CullCameraWatch.h).
    // The probe also writes its finished windows here, off the game threads.
    ServiceCullCameraWatch();
    ServiceFrameProbe();
    ServiceCullPrepareGuard();
    ServiceResourceSetWatch();
    TickVrMode();
    ServiceVrOptions();
    ServiceFocusHold();
    ServiceControllerPrompts();
}

} // namespace preyvr::dll::schedule
