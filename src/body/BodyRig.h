#pragma once

#include "body/ArmPose.h"
#include "base/VrMath.h"

// The pure half of the player's body as the engine has it (the body overhaul,
// 2026-10-25): which joints hang from which, where the living body's collider
// sits, and the body pin -- the drawn body posed under the player's head.
namespace preyvr::bodyrig {

// Which joints hang from `root`: `inSubtree[j]` is set for `root` and for every
// joint whose chain of parents (`parent`, -1 for none) reaches it. A joint at or
// under `exclude` (-1: none) is left out, and so is one whose chain does not end
// within `count` steps (a cyclic table). False, with nothing set, for a root
// outside the rig or the excluded joint itself.
bool Subtree(const int* parent, int count, int root, int exclude, bool* inSubtree);

// --- the body pin ------------------------------------------------------------------
//
// **The drawn body hangs from the player's neck.** The game draws its body where
// its animation puts it, standing at the height the view was calibrated at; the
// headset says where the player's head is. After the native pass the body is
// posed again (docs/BODY-FOLLOW.md, the body pin):
//
//  * the neck under the player's atlas -- the head's pivot, kNeckFromEye from the
//    eyes -- which turning or tipping the head leaves where it is. The neck bends
//    forward with part of the head's pitch, and the throat is kept behind or
//    below the eyes: looking down put them inside it (the research build, 15 cm
//    down onto the throat joint);
//  * the spine from the pelvis to the neck, bent along its length, not stretched,
//    and turned towards the torso the arms face;
//  * the pelvis and the feet hipsBack behind where the game stands them: its
//    standing idle has the hips under the eyes, the neck behind them and the
//    back leaning back; a person's hips are under the head's pivot;
//  * the pelvis down and back as the neck drops, leaning the spine further
//    forward where the legs would fold too far;
//  * each leg from the hip to the foot the game planted, moved back with the
//    pelvis, the knee bending the way the game's does, or forward.
//
// All in the rig's model space, the space ik.skel dumps: x right, y forward,
// z up, in the model's units (the caller divides metres by the character's
// scale).

// A joint's absolute transform, as the pose stores it.
struct Joint {
    Quaternion q{};
    Vec3 t{};
};

// `child` as it moves with its parent from `parentOld` to `parentNew`: the same
// transform relative to it.
Joint Carry(const Joint& parentOld, const Joint& parentNew, const Joint& child);

// The joints the pin places itself; everything else hangs from them.
enum Pinned : int {
    kPelvis, kSpine1, kSpine2, kSpine3, kSpine4, kNeck,
    kUpperLegR, kLowerLegR, kFootR, kUpperLegL, kLowerLegL, kFootL,
    kPinnedCount
};

struct PinInput {
    // The pinned joints as the native pass left them, the waist straightened
    // (StraightenShift).
    Joint native[kPinnedCount]{};
    // On the rig, also as left: the point that is the player's atlas (the
    // game's eye less kNeckFromEye, in the eye's frame) and the throat joint.
    Vec3 atlas{}, throat{};
    // The player's atlas and centre eye, the head's facing (level, unit) and
    // its pitch (radians, positive looking down).
    Vec3 realAtlas{}, eye{};
    Vec3 facing{0.0f, 1.0f, 0.0f};
    float pitch = 0.0f;
    // How far the chest turns from the game's, about the vertical (radians,
    // positive left): onto the torso the arms face, a long weapon's stance.
    float twist = 0.0f;
};

struct PinSettings {
    // The neck's share of the head's pitch below level, and above it.
    float neckFlex = 0.5f;
    float neckExtend = 0.25f;
    // How far behind where the game stands them the pelvis and the feet stand,
    // together (the Hips and Legs Back option; none: the game's).
    float hipsBack = 0.0f;
    // How far the pelvis goes back for each unit the neck drops.
    float pelvisBack = 0.45f;
    // The throat at least `throatBack` behind the eye, along the head's facing,
    // less as it lies lower: none once it is `throatDown` below it.
    float throatBack = 0.08f;
    float throatDown = 0.12f;
    // The hip no nearer its ankle than `legFold`, nor further than `legReach`
    // of the leg's length.
    float legFold = 0.30f;
    float legReach = 1.0f;
    // How far the spine may lean from upright, degrees.
    float leanForwardDeg = 80.0f;
    float leanSideDeg = 40.0f;
    float leanBackDeg = 25.0f;
    // The atlas followed level up to `followFull` from the rig's, then less, and
    // not at all from `followNone`: beyond a lean the player has stepped away.
    float followFull = 0.35f;
    float followNone = 0.70f;
    // At most this far the feet sink, when the body is lower than its legs fold.
    float sinkMax = 0.5f;
};

struct PinResult {
    bool valid = false;
    Joint pose[kPinnedCount]{};
    // For the report: the neck's drop, the spine's lean (degrees), how far the
    // throat pushed the neck back, how far the feet sank, how far the neck fell
    // short of its goal, and whether the legs' fold set the lean.
    float drop = 0.0f;
    float leanDeg = 0.0f;
    float throatPush = 0.0f;
    float sink = 0.0f;
    float shortfall = 0.0f;
    bool folded = false;
};

PinResult SolvePin(const PinInput& in, const PinSettings& settings);

// **The waist the game kinks.** Its crouch lowers the upper body by moving
// spine2 off spine1 (its local offset (-0.160, 0.147) against a rest of
// (0.1255, 0, 0)): spine2 lands below spine1 and the hips show through the
// belly. The move, in the model, that puts spine2 and all above it back within
// `limit` of the rest: `spine2Offset` is spine2's local translation, `spine1`
// spine1's absolute rotation. Small offsets (the standing idle's, 4 cm) stay.
Vec3 StraightenShift(const Quaternion& spine1, const Vec3& spine2Offset, const Vec3& rest, float limit);

// The head's facing, level and unit, from its forward and up (z up): its up
// stands in for its forward looking straight down or up (armpose::HeadYaw).
Vec3 FacingOf(const Vec3& forward, const Vec3& up);
// Its pitch, radians, positive looking down.
float PitchOf(const Vec3& forward);
// The turn about +z from `from` to `to`, both levelled (radians, positive left).
float TurnAbout(const Vec3& from, const Vec3& to);

// The arm's shoulder from the player's atlas, x right, y forward, z up (m): where
// armpose::ShoulderFromHead puts it with the head level and facing the torso,
// which hangs it from the eye. `side` +1 right, -1 left.
Vec3 ShoulderFromAtlas(const armpose::Body& body, armpose::Side side);

} // namespace preyvr::bodyrig
