#pragma once

#include "preyvr/VrMath.h"

#include <cstdint>
#include <optional>

// The pure half of the body's turn (TorsoLane): **the body faces the torso** --
// after a snap turn, and as the player turns round in the room.
//
// The body mesh, capsule and arms rig face the native player yaw G. The view
// is the play space's yaw P = G - R plus the head, where R is the reference
// yaw. A snap turn changes R alone, and a physical turn the head alone, so the
// view turns and the body is left behind. Turning G and R together by the
// torso yaw T's offset from R faces the body P + T, the torso's real direction
// in the world, and leaves P -- and with it the view, the aim and the hands --
// where it was.
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

// Zero-G: `current` turned by `delta` about its own up axis (engine Z), so its
// pitch and roll stay. Nothing for an unusable rotation or a non-finite delta.
std::optional<Quaternion> LocalTurn(const Quaternion& current, float delta);
// How far `to` is turned from `from` about `from`'s own up axis: the twist
// about Z of from^-1 * to. Nothing for unusable rotations.
std::optional<float> LocalYawBetween(const Quaternion& from, const Quaternion& to);

// How far to turn so the reference yaw equals the torso yaw, the short way.
// Not applied at or below `minimum` (radians), or for non-finite input.
struct Step {
    float delta = 0.0f;
    bool apply = false;
};
Step TorsoStep(float referenceYaw, float torsoYaw, float minimum);

// `step` no further than `maxStep` (radians) either way; 0 leaves it whole.
Step Limited(Step step, float maxStep);

// **Whether the game is turning the player itself** -- smooth turning, aim
// assist, a script: its yaw has moved since this lane last left it. The body
// follows physical turns only while it is not, and for `holdNs` after: turning
// the body every frame while a smooth turn ran made the view jitter
// (2026-10-24).
class NativeTurnWatch {
public:
    // The native yaw at the start of a frame; true while the game is turning
    // it, or was within `holdNs`. A yaw that is not finite counts as turning.
    bool Update(float yaw, std::uint64_t nowNs, float epsilon, std::uint64_t holdNs);
    // The yaw this lane left the player at after turning it.
    void Left(float yaw)
    {
        left_ = yaw;
        valid_ = true;
    }
    void Reset()
    {
        valid_ = false;
        untilNs_ = 0;
    }

private:
    float left_ = 0.0f;
    bool valid_ = false;
    std::uint64_t untilNs_ = 0;
};

// NativeTurnWatch for zero-G, where the game turns the player about any axis
// (its stick's yaw and pitch, its roll, a landing): the whole rotation moved
// more than `epsilon` radians since this lane last left it.
class NativeRotationWatch {
public:
    bool Update(const Quaternion& rotation, std::uint64_t nowNs, float epsilon, std::uint64_t holdNs);
    void Left(const Quaternion& rotation)
    {
        left_ = rotation;
        valid_ = true;
    }
    void Reset()
    {
        valid_ = false;
        untilNs_ = 0;
    }

private:
    Quaternion left_{};
    bool valid_ = false;
    std::uint64_t untilNs_ = 0;
};

} // namespace preyvr::bodyturn
