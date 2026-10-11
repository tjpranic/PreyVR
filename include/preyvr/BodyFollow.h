#pragma once

#include "preyvr/VrMath.h"

#include <optional>

// The pure half of body following (docs/BODY-YAW-2026-10-11.md): **the body
// faces where the player's torso does.**
//
// The body mesh, capsule and arms rig face the native player yaw G. The view
// is the play space's yaw P = G - R plus the head, where R is the reference
// yaw. Keeping R equal to the torso yaw T (tracking space) makes the body face
// P + T, the torso's real direction in the world. Each step turns G and R by
// the same angle, so P -- and with it the view, the aim and the hands -- does
// not move.
//
// Engine yaws turn about engine Z, tracking-space yaws about OpenXR +Y; both
// turn left for positive angles (stereo::YawQuaternion, armpose::YawOf), so
// they compare directly.
namespace preyvr::bodyfollow {

// An angle in (-pi, pi].
float Wrap(float radians);

// The yaw an engine rotation faces: a yaw of t sends +Y to (-sin t, cos t, 0),
// matching stereo::CameraYawOf. Robust looking straight up or down: the up
// axis stands in for the forward one, as in armpose::HeadYaw. Nothing for a
// non-finite or degenerate rotation.
std::optional<float> EngineYawOf(const Quaternion& engineRotation);

// `current` turned by `delta` about engine Z. Levelled -- pitch and roll
// dropped -- when `level` is set: native pitch bends the body mesh, and
// nothing in VR reads it. Nothing when `current` has no yaw or `delta` is not
// finite.
std::optional<Quaternion> TurnedViewRotation(const Quaternion& current, float delta, bool level);

// How far to turn so the reference yaw equals the torso yaw. Not applied at
// or below `minimum` (radians), or for non-finite input.
struct Step {
    float delta = 0.0f;
    bool apply = false;
};
Step FollowStep(float referenceYaw, float torsoYaw, float minimum);

} // namespace preyvr::bodyfollow
