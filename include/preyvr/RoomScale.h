#pragma once

#include "preyvr/VrMath.h"

// The pure half of room-scale (docs/BODY-FOLLOW.md, room-scale): **the capsule
// follows the player across the room.**
//
// The view places the eye at the native eye plus Rz(P) * ToEngine((head -
// origin) * scale). Walking moves only the head, so without room-scale that
// offset grows without limit: the capsule, the body and the native eye stay
// behind. After each frame's render the capsule is moved under the player --
// fitted first by the living body's own stance test -- and the origin by
// exactly the move that was applied, so the view does not move. Only this move
// shifts the origin: stick walking, falls, lifts and scripted moves move the
// capsule natively and the view goes with them.
//
// **What is followed.** The point followed is the eye as it would be with the
// head level and facing the torso (armpose::LevelEye): turning or tipping the
// head leaves it where it is.
//
// **The lean zone.** The head may lead the capsule by up to leanZone (25 cm,
// VR Options' Lean Zone) before it follows; over that the body pin bends the torso to the head, as a
// lean from the hips does. Bent well down -- the head more than dropBand below
// its calibrated height -- it may lead by as much as a hinge that drops it that
// far would take it, if that is more (LeanAllowance): bending to the floor bends
// the body. Past the allowance the capsule follows at once.
//
// **Settling.** While the head moves, it cannot tell a lean from a step: a lean
// of 20 cm about the hips drops it less than 4 cm, as much as a walk's bob. At
// rest it can: a lean that far out holds the head lower, a step leaves it
// upright. So a head that stays still for settleDelay, lowered by less than half
// what a lean that far out would lower it (LeanDrop), was a step, and the
// capsule glides under it. Upright is learnt while the head stands still over
// the body, so a stance more relaxed than the calibrated one is not a lean.
//
// Where the capsule cannot follow (a desk, a wall), the body pin bends the
// torso the rest of the way.
//
// Engine axes are Z up; tracking space is OpenXR (Y up). Metres throughout.
namespace preyvr::roomscale {

struct Settings {
    // Standing sway: a settled capsule is within this of under the head.
    float deadzone = 0.02f;
    // How far, level, the head may lead the capsule before it follows: the
    // body pin bends the torso over it.
    float leanZone = 0.25f;
    // The head this far below its calibrated height is still upright: a gait's
    // bob, a relaxed stance.
    float dropBand = 0.04f;
    // The lean a drop below the band explains is a hinge of this length.
    float trunk = 0.6f;
    // The most a lean is taken to explain.
    float maxLean = 0.35f;
    // Settling: the head within stillRadius of where it rests for settleDelay
    // seconds, lowered by less than half the drop a lean that far out makes
    // (plus uprightSlack), was a step: the capsule glides under it at
    // settleSpeed. Where it rests trails the head by restSeconds, so a head
    // slower than stillRadius / restSeconds is at rest. Upright is learnt over
    // uprightSeconds standing over the body.
    float stillRadius = 0.02f;
    float restSeconds = 0.2f;
    float settleDelay = 0.5f;
    float settleSpeed = 0.4f;
    float uprightSlack = 0.003f;
    float uprightSeconds = 1.0f;
    // Moves shorter than this are not made.
    float jitter = 0.001f;
    // A tracking glitch never moves the capsule faster than this (m/s), and an
    // offset that jumps this far between frames is a tracking-space change, not
    // a step (m).
    float maxSpeed = 3.0f;
    float maxJump = 0.5f;
};

// Where the head is relative to the native eye, in the world (engine axes):
// Rz(playSpaceYaw) * ToEngine((head - origin) * scale).
Vec3 HeadOffsetWorld(const Vec3& headXr, const Vec3& originXr, float playSpaceYaw, float scale);

// The tracking-space shift of the origin that cancels a native move of
// `appliedWorld`: the head offset drops by exactly that move.
Vec3 OriginShift(const Vec3& appliedWorld, float playSpaceYaw, float scale);

// How far, level, the followed point may be from over the capsule before it
// follows: the lean zone, or -- if more -- the deadzone plus the lean that a
// head `drop` below its calibrated height explains (a hinge of the trunk's
// length, past the drop band, at most maxLean).
float LeanAllowance(float drop, const Settings& settings);

// How far a hinge of the trunk's length lowers the head to put it `lead` ahead.
float LeanDrop(float lead, const Settings& settings);

// The move wanted this frame (engine axes, level): the followed point's level
// offset beyond the allowance, at once; or, settling, a glide towards it. Never
// faster than maxSpeed. A frame whose offset jumped from the last one -- less
// the move applied since (Applied) -- by more than maxJump is refused. `drop` is
// how far the head is below its calibrated height.
class Follower {
public:
    Vec3 Update(const Vec3& offsetWorld, float drop, float dtSeconds, const Settings& settings);
    // The move the capsule actually made: the next offset is less by it.
    void Applied(const Vec3& applied);
    bool Jumped() const { return jumped_; }
    // Gliding under a head that came to rest upright.
    bool Settling() const { return settling_; }
    // The drop learnt for standing upright over the body (m).
    float Upright() const { return upright_; }
    // Forgets the frames before: the next is a first. What upright is stays.
    void Reset();
    // Upright back to the calibrated height: a recenter or a calibration.
    void ResetUpright() { upright_ = 0.0f; }

private:
    bool jumped_ = false;
    bool havePrevious_ = false;
    Vec3 previous_{};
    // Where the head rests (as an offset, the capsule's moves taken off) and
    // how low, both trailing it; and for how long it has stayed within
    // stillRadius of it.
    Vec3 rest_{};
    float restDrop_ = 0.0f;
    float stillSeconds_ = 0.0f;
    float upright_ = 0.0f;
    bool settling_ = false;
};

// **How much of a move fits** (docs/BODY-FOLLOW.md, the capsule's physics): the
// largest fraction of it for which `fits(fraction)` -- the living body's own
// stance test -- holds. The whole move when its end fits; otherwise bisected
// `iterations` times, so within 2^-iterations of the first contact. The body
// is never put where it overlaps something, so the engine never pushes it back
// out.
template <class Fits> float FitFraction(Fits&& fits, int iterations)
{
    if (fits(1.0f)) { return 1.0f; }
    float clear = 0.0f, blocked = 1.0f;
    for (int i = 0; i < iterations; ++i) {
        const float mid = 0.5f * (clear + blocked);
        if (fits(mid)) { clear = mid; } else { blocked = mid; }
    }
    return clear;
}

// The stance test's verdict for a move, from its overlap depth there and where
// the body stands (0: clear): clear there, or -- for a body already against
// something -- no deeper in it than now, so it may still step away from a wall.
inline constexpr float kDepthSlack = 0.001f;   // m
bool NoDeeper(float depthThere, float depthHere);

// **Whether the body is moving** -- walking, falling or pushed: its own
// velocity or the velocity its movement asks for is more than a crawl. A move
// must then keep that motion. At rest the engine's teleport costs nothing and
// lets the next physics step settle the body on what is under it, which a move
// that keeps its motion would not.
inline constexpr float kMovingSpeed = 0.05f;   // m/s
bool BodyMoving(const Vec3& velocity, const Vec3& requested);

} // namespace preyvr::roomscale
