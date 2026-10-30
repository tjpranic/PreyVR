#pragma once

#include <windows.h>
#include <array>
#include <string>
#include "GameplayFrame.h"
#include "preyvr/WeaponAim.h"

// M2: the weapon points where the controller points, not where the head looks.
//
// **This is the gate that stopped the earlier prototype.** H-004 records it by
// name -- *"the critical detached-aim gate that blocked the earlier fholger
// prototype"* -- and it is already reproduced here: a fixed-camera reticle probe
// changed the cached ray, and the A0b wrench test moved a native wall contact
// `0.127165` units laterally on the same collider. What was missing was a
// controller to drive it with, which now exists.
//
// **Where it writes, and why exactly there.** `ArkPlayer::UpdateCachedReticleViewPosAndDir`
// (R-011) rebuilds the cached ray each frame by unprojecting through the global
// view camera. Its consumers -- weapon firing (R-014), target selection (R-022),
// wrench hits (R-020) -- all read the cached fields afterwards. So the takeover
// goes **after the producer returns and before the consumers read**, which is the
// same seam FarCry2-vr used for Dunia: *"overwriting outB after the provider
// returns and before the caller consumes it is the aim takeover."*
//
// Writing there rather than at each consumer means one write serves all of them,
// and no consumer can be missed.
//
// **Composition follows the fleet's one rule**, because there is only one right
// answer and four projects converged on it: **yaw composes onto the body; pitch
// and roll pass through absolutely.** The same recenter event feeds the view and
// the aim, so the hand and the eye cannot drift apart -- CAM-003, one recenter
// event for all lanes.
//
// Off by default. The A/B that matters is *aim, look away, toggle, aim again*, so
// it is togglable live rather than fixed at startup.
namespace preyvr::dll {

// Published only after a successful owned right-wrist target write. Geometry
// comes from the native pre-IK wrists expressed in the authored barrel basis.
// **A snap turn keeps it, and the hold.** It turns both hands and the weapon
// together, so the region is keyed by the recenter epoch, not the reference
// generation a snap bumps, and the drawn wrist's offset from the controller is
// kept in play space (the frame's yaw taken out): the aim hook reads it a frame
// later, and a snap in between must not turn it.
struct SupportGripGeometry {
    twohand::Region region{};
    Vec3 primaryOffsetPlay{};
    std::uint64_t owner=0, recenter=0, epoch=0, publishedNs=0;
};
void PublishSupportGripGeometry(const SupportGripGeometry& geometry);
DWORD SetTwoHandedAim(unsigned int enabled);
unsigned int TwoHandedAimEnabled();
DWORD SetTwoHandGripToggle(unsigned enabled);
DWORD SetTwoHandSupportSnap(unsigned enabled);
bool TwoHandGripToggle();
bool TwoHandSupportSnap();
bool TwoHandedAimHeld();
// The left hand is inside the equipped weapon's foregrip region this frame.
bool SupportGripNear();
std::string TwoHandedAimStatus();
// The aim hook's two-hand step (TwoHandHold.cpp), before the frame is published:
// the foregrip hold, into frame.twoHand.
void SolveTwoHandedFrame(GameplayPoseFrame& frame, bool tracking);

// **The body yaw rotates a head-relative controller offset into the world**
// (GameplayPoseFrame::yaw). The engine's view camera CARRIES head tracking --
// this mod writes it -- so its yaw is `body + (head - reference)`. Rotating an
// offset that is already measured from the head by that angle applies the head
// twice, and the hand and weapon swing with the headset. A wearer reported
// exactly that on 2026-09-07: "the hand and gun yaw WITH the hmd yaw."
// Subtracting the head's own yaw cancels it: `playSpace = camera - head`.
// This is the composition the old hand lane called BodyYaw(), quoting the
// fleet playbook -- parenting the shoulders to the HMD is the obvious
// implementation and it is wrong. Near-vertical the last defined head yaw is
// held; with none ever defined the frame is refused, never composed from the
// camera yaw.
// Frames that held the last defined head yaw because the wearer was looking
// near-vertical, and frames refused because no head yaw has ever been defined.
// Held frames are correct, not degraded: the body has not turned.
unsigned long long AimHeadYawHeldCount();
unsigned long long AimHeadYawUnavailableCount();
// Frames built on zero-G's tilted view (GameplayFrame.h PlaySpaceRotation).
unsigned long long AimTiltedFrameCount();
int AimCameraYawMilliDegrees();
int AimHeadYawMilliDegrees();
int AimPlaySpaceYawMilliDegrees();
// The world anchor the hand is placed against (Prey's cached eye) and the
// tracked head it is measured from, both in millimetres. The composition
// assumes these translate together; where they do not, the difference appears
// as hand movement driven by head movement. Correlating each against head yaw
// with a motionless controller localises that in one measurement.
int AimNativeEyeMillimetres(unsigned int axis);
int AimTrackedHeadMillimetres(unsigned int axis);

// The one per-frame answer to where the weapon aims, for every lane that needs
// it. Published by the aim hook because that is where a coherent controller
// sample, the engine's own eye anchor and the reference frame already meet.
//
// Consumers must call `preyvr::aim::Usable` with the CURRENT generations rather
// than trusting the sample they were handed: it is a snapshot, and a weapon
// change or a recentre invalidates it without anything notifying them.
bool TryGetAimSample(preyvr::aim::Sample& out);
// Main render seam only, after installing the camera for the eye being drawn.
void UpdateAimReticleForRender();
unsigned long long AimSamplePublishedCount();

// The published world aim direction, and the raw controller orientation behind
// it, in thousandths. Under a pure head rotation both must hold still: the
// controller pose is located in LOCAL space and the reference yaw is the body
// yaw. A headset measurement says otherwise, so these exist to name which.
int AimDirectionMilli(unsigned int axis);
int AimRawOrientationMilli(unsigned int axis);

DWORD SetAimTakeoverEnabled(unsigned int enabled);
bool AimTakeoverEnabled();
// 0 preserves native origin; 1 uses the tracked aim point (diagnostic).
// The controller point is NOT the authored muzzle; per-weapon barrel alignment
// and native wall-clip fallback require independent validation.
// 0 keeps Prey's native eye origin, 1 uses the tracked hand, 2 uses the
// CALIBRATED MUZZLE and falls back to the hand when none is available.
//
// Mode 2 is the reconciliation: Prey's projectile already leaves the weapon's
// authored muzzle helper while the reticle ray starts at the eye, so the two
// agree at exactly one distance. Sharing an origin removes that.
DWORD SetAimOriginFromHand(unsigned int mode);
unsigned int AimOriginMode();
unsigned long long AimMuzzleOriginAppliedCount();
// Frames that asked for the muzzle and did not get one -- uncalibrated, or a
// weapon change since. Counted, because falling back silently to a less
// accurate origin is how a lane looks fine and aims wrong.
unsigned long long AimMuzzleUnavailableCount();
unsigned long long AimOriginAppliedCount();

// **A screen highlights what the crosshair sits on.** A terminal's or keypad's
// cursor -- what it highlights and presses -- is cast from the player's cached
// ray (preyvr/WorldUiNative.h), whose origin stays the eye, while the crosshair
// is drawn from the aim sample: from the muzzle with a gun drawn. Half a metre
// from a screen the two land as far apart as the muzzle is from the eyes.
//
// VR start: the world UI's read of the ray (its function's body checked first)
// is handed the sample's ray instead, through the getter's filter. A refusal is
// reported, never fatal: the screen keeps Prey's ray.
//
// While the player examines a screen (ExaminingScreen) the ray, and the sample,
// start on the right hand's line level with the eye (preyvr/ExamineRay.h),
// whatever aim.origin says; the edit is undone before the next rebuild, as
// aim.origin's is. docs/WORLD-UI-POINTER-2026-10-26.md.
void EnsureWorldUiRay();
// screenAim={frames refused worldUi={gate steered}}: frames pointed from the
// hand at an examined screen (and refused, with no camera), whether the world
// UI's read is steered, and the reads handed the sample's ray.
std::string ScreenAimReport();

// Frames the cached ray was overwritten.
unsigned long long AimTakeoverAppliedCount();

// **Rejections are split, because they call for opposite responses.** A sleeping
// controller is a tracking problem the player can fix by picking it up; a refused
// composition is a bug in this code; a missing player pointer is a state problem.
// A single "refused" counter would make the three indistinguishable, which is the
// mistake this project already made with the head-rotation counter.
unsigned long long AimTakeoverRejectedNoPose();
unsigned long long AimTakeoverRejectedNoPlayer();
unsigned long long AimTakeoverRejectedCompose();

// The magnitude of the direction the engine produced, in thousandths, sampled
// before any overwrite.
//
// **This exists so the data names itself.** Prey's own field is documented as a
// unit vector; reading it back as 1000 confirms both the offset and the
// convention, exactly as FarCry2-vr identified Dunia's shot direction by printing
// magnitudes rather than trusting a decompiled stack-slot name. A reading far from
// 1000 means the offset is wrong and nothing should be written.
DWORD AimTakeoverNativeDirectionMagnitude();

} // namespace preyvr::dll
