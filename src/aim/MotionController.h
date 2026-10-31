#pragma once

#include "render/camera/StereoCamera.h"
#include "base/VrMath.h"

#include <cstdint>
#include <optional>

// Motion controllers: turning a tracked hand into an aim ray and a weapon
// mount rotation, and snap turning.
//
// Pure, so the geometry can be settled before any of it is wired to Prey.
//
// The fail-closed rule throughout: when a pose is not usable, return nothing and
// let the caller keep the game's own behaviour. A stale or untracked controller
// must never silently produce a plausible-looking ray, because a plausible wrong
// aim is worse than no aim -- it fires.
namespace preyvr::controller {

// A ray in engine world space.
struct AimRay {
    Vec3 origin{};
    Vec3 direction{}; // unit length
};

// Controller forward. OpenXR's grip and aim poses both point along -Z, which
// becomes engine +Y under the basis change -- the same "forward is column 1"
// convention CCamera uses, so the controller's forward and the camera's forward
// are directly comparable without a second correction.
Vec3 ForwardOf(const Pose& engineSpacePose);

// Converts a raw OpenXR controller pose into engine world space using the same
// reference frame the eyes use, so the hand and the view cannot drift apart.
Pose ControllerPoseInWorld(
    const stereo::ReferenceFrame& reference,
    const Pose& openXrControllerPose);

// The aim ray, gated on tracking validity. Returns nothing when the pose is
// unusable; the caller then leaves Prey's own reticle ray (R-012) alone.
std::optional<AimRay> AimFromController(
    const stereo::ReferenceFrame& reference,
    const Pose& openXrControllerPose,
    const PoseValidity& validity,
    std::uint64_t maxAgeNanoseconds);

// ---------------------------------------------------------------------------
// Weapon
// ---------------------------------------------------------------------------

// The weapon mount, recomposed from how far the controller has rotated since
// calibration.
//
// **This exists as a pure function because its failure mode is silent.** The
// authored mount is a *basis change* and must be composed on the right --
// `trackedDelta * authoredMount`. Composed the other way it becomes a meaningless
// residual, and the symptom looks exactly like a sign error: BioshockVR loaded a
// correct +90 degree grip offset, applied it on the wrong side, and then tried
// +90000 and -90000 millidegrees without either being closer.
//
// **The diagnostic that identifies the class:** if both extremes of a signed
// parameter fail symmetrically, the model is wrong rather than the sign.
//
// The delta form needs only a calibration reference, not an absolute model-space
// controller pose.
//
// Returns nothing when any input is not a usable rotation, so a caller keeps the
// engine's own mount rather than writing a degenerate one.
std::optional<Quaternion> MountRotationFromControllerDelta(
    const Quaternion& authoredMount,
    const Quaternion& calibrationAim,
    const Quaternion& currentAim);

// ---------------------------------------------------------------------------
// Locomotion
// ---------------------------------------------------------------------------

// Snap turn. Returns the new reference yaw.
//
// Snap rather than smooth is the default for a reason that is not stylistic:
// smooth rotation the player's inner ear does not predict is the single largest
// contributor to sim sickness, and a mod that makes people ill does not get
// played. Smooth turning is offered for those who prefer it, and the choice
// belongs to the player.
float SnapTurn(float currentYawRadians, int steps, float stepRadians);

// Wraps to (-pi, pi] so the stored yaw cannot grow without bound over a long
// session and lose float precision.
float WrapAngle(float radians);

} // namespace preyvr::controller
