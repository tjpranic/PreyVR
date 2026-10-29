#include "FrameSchedule.h"

#include "ArmsLane.h"
#include "ActionMaps.h"
#include "BodyDepthHook.h"
#include "BodyEquipment.h"
#include "CarryLane.h"
#include "ConsoleBridgeWin32.h"
#include "ContactLane.h"
#include "ControllerPrompts.h"
#include "CullPrepareGuard.h"
#include "DebugOverlay.h"
#include "FocusHold.h"
#include "FrameCaptureWin32.h"
#include "FrameProbe.h"
#include "GrenadeLane.h"
#include "HeadLamp.h"
#include "HudBridge.h"
#include "InputPost.h"
#include "InteractionLane.h"
#include "MoveLane.h"
#include "OverflowCapture.h"
#include "PsiMedkit.h"
#include "Psychoscope.h"
#include "SurfaceStopLane.h"
#include "SwingLane.h"
#include "TorsoLane.h"
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
    // The frame probe's gameplay step (FrameProbe.h): splits the main thread's
    // update at the player's frame.
    ProbeRecord(ProbeStep::gameplay);
    // Before this frame's render: which slot is the player's, drawn in the world.
    PublishBodyDepthPlayer(frame.player);
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
    // The frame probe's clock, and the main thread named for a crash report.
    NoteCrashThread(CrashThread::main);
    ProbeRecord(ProbeStep::tick);
    // **Queued input is drained here, not on the thread that produced it.**
    // `CSystem::Render` is the engine's main thread and, unlike the gameplay
    // camera callback, it still runs while a menu is up -- which is exactly the
    // case menu navigation needs. Called ahead of the hook's stereo-armed check:
    // driving a menu must not require stereo or head tracking to be enabled
    // first, and the early return would otherwise skip it.
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

void BeforeEngineRender(void* system)
{
    // After every drain and the camera edit, so it tests the camera Render
    // is about to: if this frame's occlusion prepare was skipped though the
    // world is about to be drawn, make it now, or the frame's cull batch is
    // orphaned and the culler stays a frame behind (CullPrepareGuard.h).
    if (system != nullptr) {
        CullPrepareBeforeRender(static_cast<const std::uint8_t*>(system) + engine::SystemLayout::viewCamera);
    }
    // After this frame's view, before the render takes the lights: the ones Prey
    // placed before the view was known, on the headset's eye (HeadLamp.h).
    PlaceHeadLampsBeforeRender();
    // The frame probe's main-thread bracket around Prey's own render (FrameProbe.h).
    ProbeRecord(ProbeStep::renderEnter);
}

void AfterEngineRender()
{
    ProbeRecord(ProbeStep::renderExit);
    // The view this frame was rendered with is spent: a light the next frame's
    // attachment pass meets before its view waits for that one (HeadLamp.h).
    EndHeadLampFrame();
    // After the world render took the body's transform, before the next frame's
    // animation starts: the body follow's turn and room-scale's move (TorsoLane.h).
    ServiceBodyFollow();
}

void TickRenderThread(void* renderer, unsigned long long frame)
{
    NoteCrashThread(CrashThread::render);
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
    NoteCrashThread(CrashThread::worker);
    // The probe starts here and writes its finished windows here, off the game
    // threads; the cull guard installs once the module is pinned, VR or not.
    ServiceFrameProbe();
    ServiceCullPrepareGuard();
    TickVrMode();
    ServiceVrOptions();
    ServiceFocusHold();
    ServiceControllerPrompts();
}

} // namespace preyvr::dll::schedule
