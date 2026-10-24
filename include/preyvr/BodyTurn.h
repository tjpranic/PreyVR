#pragma once

#include "preyvr/VrMath.h"

#include <optional>

// The pure half of the snap turn's body turn (TorsoLane): **after a snap turn
// the body faces the torso again.**
//
// The body mesh, capsule and arms rig face the native player yaw G. The view
// is the play space's yaw P = G - R plus the head, where R is the reference
// yaw. A snap turn changes R alone, so the view turns and the body is left
// behind. Turning G and R together by the torso yaw T's offset from R faces the
// body P + T, the torso's real direction in the world, and leaves P -- and with
// it the view, the aim and the hands -- where the snap put it.
//
// Engine yaws turn about engine Z, tracking-space yaws about OpenXR +Y; both
// turn left for positive angles (stereo::YawQuaternion, armpose::YawOf), so
// they compare directly.
namespace preyvr::bodyturn {

// The yaw an engine rotation faces: a yaw of t sends +Y to (-sin t, cos t, 0),
// matching stereo::CameraYawOf. Robust looking straight up or down: the up
// axis stands in for the forward one, as in armpose::HeadYaw. Nothing for a
// non-finite or degenerate rotation.
std::optional<float> EngineYawOf(const Quaternion& engineRotation);

// `current` turned by `delta` about engine Z, levelled: pitch and roll are
// dropped, since native pitch bends the body mesh and nothing in VR reads it.
// Nothing when `current` has no yaw or `delta` is not finite.
std::optional<Quaternion> LevelledTurn(const Quaternion& current, float delta);

// How far to turn so the reference yaw equals the torso yaw, the short way.
// Not applied at or below `minimum` (radians), or for non-finite input.
struct Step {
    float delta = 0.0f;
    bool apply = false;
};
Step TorsoStep(float referenceYaw, float torsoYaw, float minimum);

} // namespace preyvr::bodyturn
