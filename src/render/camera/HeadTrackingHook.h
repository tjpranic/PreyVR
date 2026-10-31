#pragma once

#include "base/VrMath.h"
#include <functional>
#include <optional>
#include <string>

#include <windows.h>

#include <cstddef>
#include <cstdint>

// M1 head tracking: the world responds to head rotation.
//
// This file owns the head pose and the recenter reference every lane composes
// against, and drives the view at ArkPlayerCamera::UpdateView (R-009, the view
// seam below), where the game computes it. The first seam, a hook on
// CRenderView::SetCamera (R-030), was removed on 2026-10-18: it sits downstream
// of culling, so it could never carry rotation, and by then it only counted.
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
// Views composed on zero-G's tilted body (the play space turned by the body's
// whole rotation, TorsoLane.h PlayerInZeroG).
unsigned long long ViewHookZeroGCount();

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

// **Near Clip Distance** (VR Options, Setup): how close to the eyes the world is
// still drawn, cm (1..10, default 3; out of range is taken as the nearer end).
// While VR drives the view it is the view's near plane (SViewParams +0x2C),
// which CView::Update builds the camera with in place of the view system's
// 10 cm default; afterwards the view is given its default back. Prey's near
// pass drew the body at 3 cm, and once the body is drawn in the world
// (BodyDepthHook.h) the view's near plane is what cuts it. A smaller one costs
// depth precision far away.
void SetViewNearClipCm(unsigned cm);
unsigned ViewNearClipCm();
// "nearClip={cm= written= kept= cleared=}": frames written, frames where Prey's
// own near plane was kept, and times the default was given back.
std::string ViewNearClipReport();

// Non-finite or implausibly large offsets, refused rather than written. Non-zero
// means the pose or the reference frame is wrong -- a different problem from a
// lane that never ran, which is why it is not folded into one counter.
unsigned long long ViewPositionRefusedCount();

// Current head offset from the engine eye point, in millimetres. This is the
// number that says positional tracking is doing something: it should move as you
// lean and sit near zero when you do not.
DWORD ViewPositionOffsetMillimetres();
// The same offset as a vector, engine axes, metres: the view less the game's own
// eye point, as the last applied view wrote it. False before one was applied, or
// when the last was more than 200 ms ago.
bool ViewPositionOffset(Vec3& out);

// **The view last applied**, for what sits on the headset's eye in the world
// (HeadLampLane.h, HelmetLane.h): its rotation (world), and how far the headset's eye
// sits from Prey's own eye point (world axes, metres; zero while positional
// tracking is off or the position was refused). `sequence` counts the views
// applied, 0 before the first. `prey` is the view as Prey composed it, before
// it was replaced: its rotation is the world's, but in first person its
// position is relative to the player entity (CView::Update, 0x3D91C0, adds the
// entity's world position after UpdateView), so it is no world point. Main
// thread.
struct AppliedView {
    Quaternion rotation{};
    Vec3 offset{};
    unsigned long long sequence = 0;
    Pose prey{};
    // The play space's rotation into the world the view was composed on: the
    // yaw's alone, or zero-G's tilted body (`tilted`).
    Quaternion basis{};
    bool tilted = false;
    // When it was applied (MonotonicNanoseconds), 0 before the first: a view
    // that stopped coming says nothing of the play space now.
    std::uint64_t publishedNs = 0;
};
AppliedView LastAppliedView();

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
// (TorsoLane) and, failing that, the UpdateView hook -- pass `late` false and
// requeue a busy one. The turn lane passes `late` true for one neither seam took
// in a whole frame: applied there, a frame late and with the old frame drops,
// and dropped if busy. Main thread.
void ApplyQueuedSnapTurn(bool late);
// " snapApplied= snapLate= snapDropped=" for the report.
std::string SnapTurnReport();
// Snap turns applied so far, from any seam: TorsoLane turns the body after each.
unsigned long long HeadTrackingSnapCount();
// Set while TorsoLane turns the body with each snap. A snap then turns the view
// about the body, as a smooth turn does, so the camera keeps its place on the
// model; otherwise about the head, so the camera stays where it is.
void SetSnapTurnsAboutBody(bool aboutBody);

// Turns the reference yaw with a native turn of the player (TorsoLane, after a
// snap turn), so the play space P = G - R does not move. `turnNative` runs
// under the recenter lock, is given the reference yaw, and returns the angle it
// actually turned the native player by -- nothing if it did not turn it.
//
// **No generation bump**, unlike a snap or a recenter. The generation announces
// a change of P, and some 25 consumers reset on it (IK calibration, the two-hand
// hold, gestures, panel fits); this changes R and G together and never P.
// 0 turned, 1 no reference, 2 busy (lock or a reference change in flight),
// 3 the native turn was not made.
int TurnHeadTrackingReferenceWithNative(const std::function<std::optional<float>(float referenceYaw)>& turnNative);

// The positional twin (room-scale, RoomScaleLane.h): moves the native player
// and the play-space origin together, so the view does not move. `moveNative`
// runs under the recenter lock, is given the offset of `head` from the native
// eye in the world (engine axes, metres) at `playSpaceYaw`, and returns the
// world move it applied -- nothing if it applied none. The origin shifts by
// exactly that move. No generation bump, for the same reason as the turn.
// 0 moved, 1 no reference, 2 busy, 3 nothing applied, 4 positional tracking off.
int MoveHeadTrackingOriginWithNative(const Pose& head, float playSpaceYaw,
                                     const std::function<std::optional<Vec3>(const Vec3& headOffsetWorld)>& moveNative);
// How far `head` is below the height calibrated (metres, tracking space); false
// without a reference.
bool HeadDropBelowCalibrated(const Vec3& head, float& drop);

// **For this frame's view, Prey's look yaw comes from its camera's own rotation**
// (m_rotation, the +0x13F0 the slot-52 setter writes) rather than from the view
// Prey composes. TorsoLane calls it when a body turn moved the entity off where
// Prey's animation had left it: after a stick turn Prey lets the body lag the
// view and turns the head (the `#camera` attachment) to make up the difference,
// and that frame's animation was computed before the turn -- so the composed
// view would carry the old lag on top of the turned body, the one bad frame of
// docs/BODY-FOLLOW.md. Consumed by the next view; not while Prey's focus
// camera (player +0x9B0) supplies the view.
void UseNativeViewYawOnce();

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

// 1 once a recenter reference exists.
DWORD HeadTrackingHasReference();

} // namespace preyvr::dll

namespace preyvr::dll {
// Consumes only the pose whose world transform matches this camera.
bool ConsumeHeadRenderPose(const unsigned char* camera, Pose& pose, long long& displayTime, unsigned long long& reference,
                           unsigned long long& spaceEpoch);
}
