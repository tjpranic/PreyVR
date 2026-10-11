#pragma once

#include "preyvr/VrMath.h"

#include <array>

// The pure half of room-scale (docs/ROOMSCALE-2026-10-11.md): **the body
// follows the player across the room.**
//
// The view places the eye at the native eye plus Rz(P) * ToEngine((head -
// origin) * scale). Walking moves only the head, so the offset grew without
// limit and the capsule, the body and the native eye stayed behind. Each
// frame the capsule is moved towards the head -- swept for walls first -- and
// the origin by exactly the move that was applied, so the view does not move.
// Only this move shifts the origin: stick walking, falls, lifts and scripted
// moves move the capsule natively and the view goes with them.
//
// Engine axes are Z up; tracking space is OpenXR (Y up). Metres throughout.
namespace preyvr::roomscale {

struct Settings {
    float startRadius = 0.12f;   // idle until the head is this far from the body
    float stopRadius = 0.03f;    // then follow until it is this close
    float maxSpeed = 3.0f;       // m/s: a tracking glitch never teleports the body
    float maxJump = 0.5f;        // m in one frame: a tracking-space change, not a step
    float radius = 0.35f;        // the capsule, for the sweep
    float skin = 0.02f;          // kept between the capsule and what it meets
};

// Where the head is relative to the native eye, in the world (engine axes):
// Rz(playSpaceYaw) * ToEngine((head - origin) * scale).
Vec3 HeadOffsetWorld(const Vec3& headXr, const Vec3& originXr, float playSpaceYaw, float scale);

// The tracking-space shift of the origin that cancels a native move of
// `appliedWorld`: the head offset drops by exactly that move.
Vec3 OriginShift(const Vec3& appliedWorld, float playSpaceYaw, float scale);

// The horizontal move wanted this frame (engine axes, z = 0), with hysteresis:
// idle until the head is startRadius away, then towards the head at up to
// maxSpeed until it is within stopRadius.
class Follower {
public:
    Vec3 Update(const Vec3& headOffsetWorld, float dtSeconds, const Settings& settings);
    bool Following() const { return following_; }
    bool Jumped() const { return jumped_; }
    void Reset();

private:
    bool following_ = false;
    bool jumped_ = false;
    bool havePrevious_ = false;
    Vec3 previous_{};
};

// The sweep for a horizontal move of the capsule standing at `feet`: rays at
// three heights from the capsule's centre line and both sides. Each ray starts
// at the capsule's surface's distance behind its front (`lead`) and reaches
// the move's length plus the skin beyond it.
struct Ray {
    Vec3 from{}, to{};
    float lead = 0.0f;
};
inline constexpr std::size_t kRays = 9;
using Rays = std::array<Ray, kRays>;
// False for a move too short or not horizontal to sweep.
bool SweepRays(const Vec3& feet, const Vec3& move, const Settings& settings, Rays& out);

// How far the capsule may move along `move`, given each ray's hit distance
// from its start (negative: clear). Never more than the move, never negative.
float AllowedLength(float moveLength, const Rays& rays, const std::array<float, kRays>& hitDistances,
                    const Settings& settings);

} // namespace preyvr::roomscale
