#pragma once

#include <Windows.h>

#include <cstdint>

// Prey's CSystem::Render, hooked: the one per-frame seam on the engine's main
// thread, which keeps running while a menu is up. Every frame it runs the
// main-render schedule (FrameSchedule.h): the input and HUD drains and the move,
// turn and fire lanes. With stereo armed it also edits the view camera for that
// render (the eye itself is built by StereoEyeCamera.h), restores it byte for
// byte, and publishes the frame's render contract for submission (EyeRecords.h).
//
// It began as the first, bounded write to the render path, and held the A1-A7
// render experiments (double render, second pass, interpose, frame shape)
// until 2026-10-18; docs/STEREO_ROUTE.md and docs/FAILURE_REGISTRY.md keep what
// they found.
//
// **Why CSystem::Render and not CSystem::SetViewCamera.** The game rewrites
// m_ViewCamera every frame from ArkPlayerCamera, so a write issued at
// RT_EndFrame is simply overwritten before the next render reads it. The edit
// has to happen inside the window between the camera being set and the render
// consuming it. CSystem::Render (R-058) *is* that window: it reads m_ViewCamera
// and hands it to both consumers, so wrapping it gives
//
//     save -> modify -> render -> restore
//
// which is also the shape that generalises to stereo (call the original twice
// with two cameras). SetViewCamera itself is a 12-byte function ending in a tail
// jump -- a poor hook target for no benefit.
//
// **Why the frustum is rebuilt on our own copy.** CCamera caches derived state:
// eight frustum corners, six planes at +0x10C, plane sign tables, and the
// position at +0x230. Writing the matrix alone leaves all of it stale. So the
// edit is applied to a local copy, CCamera::UpdateFrustum (RVA 0x121D70) is
// called *on that copy*, and only then are the 0x240 bytes blitted into the
// game. The engine function therefore only ever touches our stack, and the sole
// write to game memory is one bounded memcpy with the original bytes held.
//
// **The hook stays installed and becomes a pass-through when disarmed**, rather
// than being removed. F-009 recorded that our observer's disable path did not
// restore the target prologue on a live host, so a design that depends on
// unhooking would be depending on something we have never observed working.
namespace preyvr::dll {


enum class CameraEditStatus : DWORD {
    unavailable = 0,  // signatures did not match, or MinHook refused
    ready = 1,        // hook installed, currently a pass-through
    armed = 2,        // an edit is being applied every frame
    failed = 3,
};

// Installs the `CSystem::Render` hook without arming any camera edit. Needed by
// anything that relies on that per-frame seam -- notably the input drain, which
// otherwise queues events that are never delivered.
DWORD EnsureRenderHookInstalled();

// Alternating-eye stereo, without any double-render.
//
// **This is the point of the design.** Validating per-eye camera construction
// and validating that Prey can render twice in one frame are separate problems,
// and only the second one is risky. With the scene frozen (`t_Scale 0`), a
// left-eye frame followed by a right-eye frame *is* a stereo pair -- so the whole
// eye-construction path can be proven correct, and looked at through
// New-StereoView, before anyone calls the render function twice.
//
// The eye offset is composed onto the engine's *current* camera rather than
// replacing it, so this exercises the real path including the game's own pitch,
// roll and position. `ipdMetres` of 0 disarms.
//
// The FOV is synthetic and deliberately asymmetric per eye, mirrored the way a
// real headset reports, so the asymmetry-shift path is exercised too rather than
// being left untested until a headset is attached.
DWORD SetSyntheticStereo(float ipdMetres, float halfFovDegrees);

// **Rung 1b: both eyes in one frame.** With this set and stereo armed, the
// camera hook stops alternating eyes: the frame keeps the head camera (so the
// engine culls and prepares it once, from the head) and its render contract is
// a both-eyes record (kRenderContractBothEyes) carrying the two eye poses for
// one display time. The render thread then submits the frame twice
// (DualSceneStereo mode 3), building each eye with BuildDualEyeCamera.
DWORD SetDualStereoContract(unsigned int enabled);
bool DualStereoContractEnabled();

// The eye separation the stereo path is armed with; 0 when stereo is off.
float SyntheticStereoIpdMetres();
// The separation of the eyes last built: less while Prey zooms (preyvr/ViewZoom.h).
// For what is drawn with those eyes.
float RenderedEyeIpdMetres();

// Render-thread half of rung 1b. Turns `camera` (a CCamera, 0x240 bytes, the
// frame's head camera) into eye `eye` by the same construction as the
// alternate-eye path, rebuilds its frustum, and publishes the eye record before
// returning, so the record holds the exact bits the render view will be given.
// False when stereo is not armed or the result is unsafe; `camera` is then
// unchanged.
bool BuildDualEyeCamera(std::uint8_t* camera, int eye);

// Locks synthetic stereo to one eye instead of alternating.
//
// **This replaces the per-frame tagging, which did not work.** The camera hook
// runs on the main thread and the capture runs on the render thread, and the two
// are offset by the engine's MT/RT double buffer -- so a tag written by one is
// not reliably read by the other for the same frame. Observed live 2026-08-31:
// two captures reported as eye 1 then eye 0 both landed in files tagged 1, which
// is exactly the silently-swapped pair the tagging existed to prevent.
//
// Compensating for the offset would mean guessing at it. Locking the eye removes
// it: hold one eye, let a few frames pass, capture, then hold the other. The
// signal is now far longer than the uncertainty, so no ordering assumption is
// needed at all.
//
// 0 locks left, 1 locks right, any other value returns to alternating.
DWORD SetStereoEyeLock(unsigned int eye);

// Sets the eye lock from inside the render loop, without taking the control
// mutex. For the stereo submission path only; everything else should use
// SetStereoEyeLock, which also keeps the capture tag correct.
void SetStereoEyeLockFromRenderThread(int eye);

// Which eye the main thread last built a camera for: 0 or 1, kRenderContractBothEyes
// for a both-eyes frame, or -1. (Called
// LastRenderedEye until 2026-10-24; the PreyVR_GetLastRenderedEye export keeps
// that name.)
//
// **Reported for observation only -- do not identify a capture by it.** It is
// written on the main thread and any capture reads it on the render thread, and
// the two are a frame or so apart. Use SetStereoEyeLock instead; that is the
// whole reason it exists.
int LastBuiltEye();

DWORD CameraEditStatusValue();

// Counts frames on which the edit was actually applied and the restore verified
// byte for byte. A frame that applied but failed to restore increments the
// failure counter instead, and disarms -- one unverified restore is enough to
// stop, because every later measurement in the session would be suspect.
unsigned long long CameraEditAppliedCount();
unsigned long long CameraEditRestoreFailureCount();

} // namespace preyvr::dll
