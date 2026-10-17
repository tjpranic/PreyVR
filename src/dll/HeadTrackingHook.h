#pragma once

#include "preyvr/VrMath.h"
#include <functional>
#include <optional>
#include <string>

#include <windows.h>

#include <cstddef>
#include <cstdint>

// M1 head tracking: the world responds to head rotation.
//
// **On a different seam from the stereo camera edit, deliberately.** The stereo
// path writes `CSystem::m_ViewCamera`, the global view camera. H-008 found 84+
// sites that read that global per frame, spanning rendering, gameplay, UI and
// 3D-engine code, and only one of them -- the cached aim ray -- was ever named
// and instrumented. Under stereo the exposure is one IPD, which measured as
// harmless on 2026-09-03. Under head tracking the camera carries the player's
// entire look rotation, so any of those unenumerated readers would see a
// different world orientation mid-render.
//
// This hooks `CRenderView::SetCamera` (R-030) instead, which is **structurally**
// immune rather than correctly ordered: R-049 shows it copies the camera by
// value into `CRenderView::m_camera` at `+0x11A0`, so a camera passed here
// provably cannot alias the global, and nothing that reads the global can see
// it. There is also no restore to perform, because nothing global was written --
// the detour edits a copy of its own argument and hands that to the original.
//
// R-030 was blocked from hooking until 2026-09-03 on the grounds that it had
// never been observed executing. It has now: 230 entries in 5 seconds on a
// single thread, prologue verified immediately before attaching.
//
// **Orientation only.** No positional tracking, which is what keeps M1 free of
// both `unitsPerMetre` -- that constant scales position, not rotation, and is
// still unmeasured -- and of the head-versus-world collision problem, since a
// head that cannot translate cannot lean through a wall.
namespace preyvr::dll {

// Publishes the head pose for the camera hook to consume. Called from wherever
// the runtime pose is located; safe from any thread.
//
// **Latest-wins rather than queued**, unlike the eye handoff. Eye identity had to
// be exact, so it needed a queue that preserved order. A pose does not: an older
// pose is simply a worse answer to the same question, so the newest available is
// always the right one to use and a backlog would be pure latency.
void PublishHeadPose(const Pose& openXrHeadPose, long long displayTime = 0);

// Reads the latest published head pose, and how old it is in microseconds.
//
// Exposed because **rotation belongs upstream**, on the camera the engine culls
// from, while this file owns the pose slot. Measured 2026-09-03: driving rotation
// from CRenderView::SetCamera renders through a rotated camera that the engine
// already culled against an unrotated one, so geometry outside the original
// frustum is gone before the rewrite happens. The playbook's rule is to keep the
// RenderView override for stereo and projection and never for CPU culling.
bool TryReadHeadPose(Pose& out, unsigned long long& ageMicroseconds);

// Composes the recentered reference with a live head pose, orientation only,
// keeping whatever position the camera already has.
//
// Shared rather than duplicated, so the upstream rotation and anything later
// cannot drift apart in how they build the same camera.
bool ApplyHeadRotation(std::uint8_t* camera, std::size_t size);

// ---------------------------------------------------------------------------
// The view seam: ArkPlayerCamera::UpdateView (R-009)
// ---------------------------------------------------------------------------
//
// **The camera is set here, at the source, so everything downstream inherits it.**
// Writing further down the frame reaches some readers and not others: an edit at
// CRenderView::SetCamera missed culling entirely, and an edit at CSystem::Render
// reached the pass camera exactly -- 903 samples, zero disagreements -- while
// culling still followed the aim, because the occlusion job is spawned earlier
// from GetViewCamera(). Setting the view where the game computes it puts us
// upstream of every one of those reads at once.
//
// **Orientation is yaw plus head, not the game's rotation plus a head delta.**
// The target architecture has no mouse pitch at all: the headset owns the whole
// view in 6DoF, a stick moves the player capsule, and a motion controller owns
// aim. So the only thing the view needs from outside the headset is a *yaw* for
// the play space -- taken from the game's camera today, so mouse turning still
// works while building, and replaced by an owned turn value later.
//
// Off by default, and observe-first: the SViewParams layout below is from the
// CryGame CE3 tree and Prey is an Arkane fork, so it is checked against a value
// measured independently -- the live camera's 1.5447 rad FOV -- before anything
// is written.

// Frames the view hook saw, and frames it actually rewrote.
unsigned long long ViewHookObservedCount();
unsigned long long ViewHookAppliedCount();

// The last SViewParams the hook saw, for confirming the struct layout before
// trusting it. FOV in radians: a reading near 1.5447 confirms the offsets, since
// that value was measured from the live camera by a different route.
float ViewHookLastFov();
float ViewHookLastNearPlane();

// Installs the hook in observe-only mode. Nothing is written; the counters and
// the readings above start moving.
DWORD SetViewHookObserving(unsigned int enabled);

// Starts rewriting the view orientation. Requires observe mode to have confirmed
// the layout, and a recenter reference.
DWORD SetViewHookApplying(unsigned int enabled);

// Positional 6DoF: lean, crouch and room-scale translation, written into
// SViewParams position alongside the rotation.
//
// **Gated separately from rotation, because they fail differently.** A wrong
// rotation reads as the world spinning and is recoverable by looking away; a
// wrong position reads as sliding, or as standing inside geometry, and is not.
// One switch for both would make an in-headset A/B unable to say which half is
// wrong -- and this project has already paid once for a lane whose counters were
// all green while the result was unusable.
//
// The engine keeps its own eye point as the origin, so walking, collision and
// scripted movement stay its business; only the offset from it is ours.
DWORD SetViewPositionApplying(unsigned int enabled);

// Scale in thousandths; 1000 == 1.0 == the measured unitsPerMetre. The lever
// exists because a wrong world scale is judged in a headset, not here -- SS2VR's
// symptom triad leads with "HMD movement feels too small". Needing a value far
// from 1000 means the measurement was wrong, which is worth surfacing.
DWORD SetViewPositionScaleMilli(unsigned int scaleMilli);

unsigned long long ViewPositionAppliedCount();

// Non-finite or implausibly large offsets, refused rather than written. Non-zero
// means the pose or the reference frame is wrong -- a different problem from a
// lane that never ran, which is why it is not folded into one counter.
unsigned long long ViewPositionRefusedCount();

// Current head offset from the engine eye point, in millimetres. This is the
// number that says positional tracking is doing something: it should move as you
// lean and sit near zero when you do not.
DWORD ViewPositionOffsetMillimetres();

// The recenter yaw, shared with every lane that composes against the play space.
//
// CAM-003, "one recenter event, all lanes": the view and the aim must resolve the
// same reference, or the hand and the eye drift apart and a recenter fixes only
// one of them.
// Frames a reader reused its own last pose because the publisher held the
// snapshot lock. Non-zero is fine; climbing quickly means the publisher and a
// reader are on different threads and contending every frame.
unsigned long long HeadTrackingPoseReadFallbacks();
unsigned long long HeadTrackingReferenceGeneration();
float HeadTrackingReferenceYaw();

// Arms the camera edit. Off by default.
//
// Refused unless a recenter reference has been captured, because without one the
// composition has no idea which way the play space faces and would snap the
// world to engine north on the first frame.
DWORD SetHeadTrackingEnabled(unsigned int enabled);

// Captures the current head pose as the yaw-only reference.
//
// Refused when the head is near-vertical, per RecenterYawFromHeadPose: looking
// straight up or down carries no usable yaw, and inventing one would face the
// player somewhere arbitrary. A refusal leaves the previous reference in place.
DWORD RecenterHeadTracking(bool recalibrateHeight = false);
DWORD SnapTurnHeadTracking(int steps, unsigned degrees);

// **A snap turn is queued where it is decided and applied at the start of the
// next frame**, before that frame's view and arm IK (comfort::PendingSnap). The
// turn lane decides at the CSystem::Render hook, after the frame's view was
// built; applying it there refused that frame and the one in flight.
// 0 queued, 1 out of range, 4 one already queued.
DWORD QueueSnapTurn(int steps, unsigned degrees);
// Applies a queued snap. The frame-start seams -- ArkPlayer::Update's entry
// (BodyFollow) and, failing that, the UpdateView hook -- pass `late` false and
// requeue a busy one. The turn lane passes `late` true for one neither seam took
// in a whole frame: applied there, a frame late and with the old frame drops,
// and dropped if busy. Main thread.
void ApplyQueuedSnapTurn(bool late);
// " snapApplied= snapLate= snapDropped=" for the report.
std::string SnapTurnReport();

// Turns the reference yaw with a native turn of the player (BodyFollow), so the
// play space P = G - R does not move. `turnNative` runs under the recenter lock,
// is given the reference yaw, and returns the angle it actually turned the
// native player by -- nothing if it did not turn it.
//
// **No generation bump**, unlike a snap or a recenter. The generation announces
// a change of P, and some 25 consumers reset on it (IK calibration, the two-hand
// hold, gestures, panel fits); this changes R and G together and never P.
// 0 turned, 1 no reference, 2 busy (lock or a reference change in flight),
// 3 the native turn was refused.
int TurnHeadTrackingReferenceWithNative(const std::function<std::optional<float>(float referenceYaw)>& turnNative);

// The positional twin (room-scale, docs/ROOMSCALE-2026-10-11.md): moves the
// native player and the play-space origin together, so the view does not move.
// `moveNative` runs under the recenter lock, is given the head's offset from the
// native eye in the world (engine axes, metres) at `playSpaceYaw`, and returns
// the world move it applied -- nothing if it applied none. The origin shifts by
// exactly that move. No generation bump, for the same reason as the turn.
// 0 moved, 1 no reference, 2 busy, 3 nothing applied, 4 positional tracking off.
int MoveHeadTrackingOriginWithNative(const Pose& head, float playSpaceYaw,
                                     const std::function<std::optional<Vec3>(const Vec3& headOffsetWorld)>& moveNative);

// Bumped by recenter, runtime rebase and reset: the events that redefine which
// way the tracking space faces, after which a torso estimate is stale. Not by a
// snap turn, which turns the world and leaves the player's torso where it was.
unsigned long long HeadTrackingRecenterEpoch();

// The tracking space head poses are published in: bumped by a runtime rebase
// and a reset, never by a snap or a recenter. A render contract carries the one
// its poses came from (SubmittableRenderContract).
unsigned long long HeadTrackingSpaceEpoch();

// View frames that reused the last reference because its publisher held the
// lock. Non-zero is fine; the alternative is a frame without head tracking.
unsigned long long HeadTrackingRoomReadFallbacks();
void ResetHeadTrackingReference();
bool RebaseHeadTrackingSpace(const Pose& previousFromNew, bool valid);
std::string HeadHeightReport();

// Frames the detour ran with the edit armed and applied.
unsigned long long HeadTrackingAppliedCount();

// Frames the detour refused to edit -- no pose yet, an unusable pose, or a
// matrix that failed the orthonormality gate. A high count against a high
// applied count means something is intermittently wrong.
unsigned long long HeadTrackingRefusedCount();

// Microseconds between a pose being published and the camera edit that consumed
// it. **This is the latency number the scope calls for measuring in M1.** It is
// only the handoff segment -- it excludes the runtime's own prediction and the
// display pipeline -- so it is a floor on end-to-end latency, not the whole of it.
unsigned long long HeadTrackingLastPoseAgeMicroseconds();

unsigned long long HeadTrackingMaxPoseAgeMicroseconds();

// 1 once a recenter reference exists.
DWORD HeadTrackingHasReference();

} // namespace preyvr::dll

namespace preyvr::dll {
// Consumes only the pose whose world transform matches this camera.
bool ConsumeHeadRenderPose(const unsigned char* camera, Pose& pose, long long& displayTime, unsigned long long& reference,
                           unsigned long long& spaceEpoch);
}
