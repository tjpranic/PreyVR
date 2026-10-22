#pragma once

#include "preyvr/VrMath.h"
#include "preyvr/RenderContract.h"

#include <Windows.h>

#include <cstdint>
#include <string>

// Prey's CSystem::Render, hooked: the one per-frame seam on the engine's main
// thread, which keeps running while a menu is up. Every frame it drains queued
// input and HUD calls and runs the move, turn and fire lanes. With stereo armed
// it also edits the view camera for that render, restores it byte for byte, and
// publishes the frame's render contract for submission.
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
// One immutable record of a built eye: where that eye's camera ended up, the
// right axis it was offset along, and which eye it is.
//
// **Why content and not a tag.** `LastRenderedEye()` is a mutable game-thread
// global, and the render thread is about a frame behind it, so a consumer that
// reads it may be packing a view built for the *other* eye while every counter
// stays valid (H-022). Matching a view-info's own camera position against these
// records identifies the eye from the data being rendered, which cannot go stale
// because it IS the thing being rendered.
struct BuiltEye {
    float position[3];
    float right[3];
    // The camera this eye was offset FROM: the cyclops the game built the frame
    // around. Camera-space content (the near pass) is relative to it.
    float centre[3];
    int eye;
    unsigned long long serial;
};
// Finds the record whose camera position matches `position`. False when no eye
// matches, which a caller must treat as "refuse", never as "assume the latest".
bool FindBuiltEyeByPosition(const float position[3], BuiltEye& out);
unsigned long long BuiltEyeLookupMissCount();
// True when `position` is the camera of a recent both-eyes frame: the head
// camera the engine prepares it from, left on the frame before either eye is
// built. Content built from it then (DrawNear particle geometry) is relative to
// the cyclops already.
bool IsDualFrameCentre(const float position[3]);

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

// Render-thread half of rung 1b. Turns `camera` (a CCamera, 0x240 bytes, the
// frame's head camera) into eye `eye` by the same construction as the
// alternate-eye path, rebuilds its frustum, and publishes the eye record before
// returning, so the record holds the exact bits the render view will be given.
// False when stereo is not armed or the result is unsafe; `camera` is then
// unchanged.
bool BuildDualEyeCamera(std::uint8_t* camera, int eye);

// **Render the frustum the headset asked for, instead of Prey's.** Measured on
// this machine: 41.25% of the submitted pixel rectangle lands inside the
// runtime's requested frustum, and the request is fully covered -- so the other
// 59% is rasterised and thrown away by the compositor (R-121).
//
// **This alone does not make anything faster.** At an unchanged target size it
// spends the reclaimed area on detail: the same pixels now cover a narrower
// field, so angular density rises. The speed comes from ALSO shrinking the
// target -- 1719x1857 carries the saved session's density against 2688x2880 --
// and the target is latched at session creation, so that is a launch argument,
// not a runtime one.
//
// **Off by default, and it should stay that way until the near pass follows.**
// The weapon model is drawn with its own FOV (`r_DrawNearFoV`), tuned against
// the old frustum; changing the scene without it leaves the weapon at the wrong
// scale. Culling and weapon scale need checking together, which is why this is
// a prototype behind an explicit opt-in rather than a new default.
//
// Takes precedence over `xr.native`. When the runtime frustum is not known --
// before `xrLocateViews` has answered, or if it answered something degenerate --
// this falls through to the previous behaviour and counts the miss, rather than
// inventing a frustum that would then be submitted as if it were the headset's.
DWORD SetRuntimeFrustum(unsigned int enabled);
unsigned long long RuntimeFrustumUsedCount();
unsigned long long RuntimeFrustumMissingCount();

DWORD SetSyntheticStereo(float ipdMetres, float halfFovDegrees);

// Scales how asymmetric the synthetic per-eye frustum is. 1.0 makes both eyes
// symmetric, so the *only* difference between them is the eye offset.
//
// Needed because the first A2 run could not be judged. With the default 1.1 the
// two eyes' frusta differ by ten degrees, which at 2560 px across a 100-degree
// field is about 256 px of uniform sideways shear -- and that completely masked
// the 64 mm eye separation it was supposed to be measuring. Two effects in one
// image is one effect too many.
//
// Set 1.0 to measure the eye offset alone; restore 1.1 to exercise the asymmetry
// path separately.
DWORD SetStereoAsymmetry(float outerScale);

// Locks synthetic stereo to one eye instead of alternating.
//
// **This replaces the per-frame tagging, which did not work.** The camera hook
// runs on the game thread and the capture runs on the render thread, and the two
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

// The render contract of the frame the render thread has just finished -- which
// eye it is, and its pose -- or false if the game thread has not published one.
//
// The camera hook publishes the eye it built alongside each frame, and this pops
// them in order. The engine's MT/RT double buffer delays work without reordering
// it, so the eye arrives with its frame and submission never has to wait to find
// out which one it has -- which is what let the dwell go, and with it the 11 Hz
// per-eye refresh that made the first stereo test judder.
//
// **Not always 1:1.** Through a level-transition load Prey's main loop keeps
// rendering while the render thread ends the frame once, so a both-eyes frame
// whose camera was noted (NoteDualFrameCamera) takes the record carrying that
// camera and drops the older ones (RenderContractQueue). Any other frame takes
// the oldest.
//
// Call exactly once per finished frame. Consuming twice would advance past a
// frame and swap the eyes from then on.
bool ConsumeRenderedContract(RenderContract& out);

// The head camera (CCamera bytes, Matrix34 first) of the both-eyes frame the
// render thread is drawing, noted from its own frame info; the next
// ConsumeRenderedContract identifies the frame's record by it. Render thread.
void NoteDualFrameCamera(const std::uint8_t* camera);

// Clears the queue. Call when arming, so a stale backlog from a previous run
// cannot decide the first few frames' eyes.
void ResetEyeHandoff();

// Records queued -- the pipeline depth in frames. It should sit at a small
// constant; a load can raise it until the next identified frame drops the backlog.
unsigned long long EyeHandoffLag();

// Frames finished with nothing queued -- the render thread ran ahead.
unsigned long long EyeHandoffStarvedCount();

// Records dropped, oldest first, because the queue was full.
unsigned long long EyeHandoffDroppedCount();

// " handoff=" queued/pushed/matched/resyncs/skipped/unmatched/starved/dropped.
std::string EyeHandoffReport();

// Sets the eye lock from inside the render loop, without taking the control
// mutex. For the stereo submission path only; everything else should use
// SetStereoEyeLock, which also keeps the capture tag correct.
void SetStereoEyeLockFromRenderThread(int eye);

// The forward axis the camera edit last wrote, for the pass-camera probe.
//
// Exists so the probe can answer whether the camera the engine culls with is the
// one we wrote, rather than the two of us reasoning about it from the call graph
// -- which said it should be, while the cull frustum visibly followed the mouse.
bool LastWrittenCameraForward(Vec3& out);

// Builds the per-eye camera by translation alone, inheriting Prey's own
// projection rather than overwriting it with a synthetic one.
//
// **This is what makes the declared frustum honest.** PreyVR cannot change what
// Prey renders, so the frustum it declares to OpenXR has to be the one Prey drew
// with -- measured 2026-09-02 as 120 degrees horizontal by 88.507 vertical, zero
// asymmetry, confirmed against the player's FOV slider. Overwriting the eye
// camera's projection with a synthetic half-FOV breaks that correspondence, and
// breaks it invisibly: on a monitor the image merely looks a little wide, and it
// is only in a headset that it reads as wrong depth.
//
// Both eyes therefore share one frustum and differ by translation, which is also
// the only difference A2b ever measured.
//
// Off by default, so every measurement taken before this existed still means what
// it meant when it was taken.
DWORD SetNativeProjection(unsigned int enabled);
unsigned int NativeProjectionEnabled();

// Which eye the most recent render used, or -1.
//
// **Reported for observation only -- do not identify a capture by it.** It is
// written on the game thread and any capture reads it on the render thread, and
// the two are a frame or so apart. Use SetStereoEyeLock instead; that is the
// whole reason it exists.
int LastRenderedEye();

DWORD CameraEditStatusValue();

// Counts frames on which the edit was actually applied and the restore verified
// byte for byte. A frame that applied but failed to restore increments the
// failure counter instead, and disarms -- one unverified restore is enough to
// stop, because every later measurement in the session would be suspect.
unsigned long long CameraEditAppliedCount();
unsigned long long CameraEditRestoreFailureCount();


// The frustum the eye camera we last handed the renderer actually projects with,
// as tangent half-extents. False before any eye has been built.
//
// **The third quantity.** xr-tape can see what the runtime located and what we
// declared; it explicitly cannot see this, because the engine's projection never
// crosses the OpenXR boundary. The mod is the only place all three coexist, which
// is why the declared-vs-rendered assert has to live in-process.
bool RenderedEyeTangents(float& tanLeft, float& tanRight, float& tanUp, float& tanDown);

} // namespace preyvr::dll
