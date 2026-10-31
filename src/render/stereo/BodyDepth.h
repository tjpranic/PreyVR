#pragma once
#include "render/stereo/BodyDepthNative.h"
#include "engine/EngineMap.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// **The player's body at the world's depth, so what is in front of it hides it.**
//
// Prey draws its first-person character (arms, body, legs and the weapon on
// them) in CryEngine's near pass. That pass draws into the front of the depth
// buffer: viewport depth [0, r_DrawNearZRange] (0x2B1C718, 0.1), [0.9, 1] in
// reverse depth (the viewport setter 0xF90390), with its own projection, near
// plane 0.03 m and far r_DrawNearFarPlane (UpdateNearestChange 0xF43D70). With
// the world's near plane at 0.1 m, only a surface within about 11 cm of the
// eyes is ever in front of it, so in VR a hand reached behind a crate is drawn
// over the crate.
//
// Two ways out (Mode):
//
// **The near pass at the world's depth (nearWorldDepth, the default).** The body
// stays in the near pass, with everything that comes with it: the weapon's
// "draw near" effects (the stun gun's charge sparks) drawn with it, and the
// weapon's Flash display (the stun gun's charge meter), which the near-pass
// draw keeps refreshing and a world draw does not (A/B, 2026-10-28: both broke
// with `world` and came back with `native`). Only its depth changes, in the
// main view:
// the near pass's depth slice becomes the viewport's own (SetViewport, and
// UpdateNearestChange in the legacy pipeline), and its projection's depth the
// world's (DepthMap, in NearViewStereo's per-view packer). A near pixel then
// holds the depth a world pixel at that distance would, and the depth test does
// the rest.
//
// **The body drawn in the world (world, PreyVR-91).** The near pass is chosen for
// each slot render (BodyDepthNative.h). For the player's slot in the main view,
// the near bits are clear for the call: the slot draws at its world transform,
// at the world's depth, and the eyes' own views give it its parallax. It leaves
// behind what is drawn near for it: the weapon's effects stay in the near pass,
// in front of the weapon, and its display is never refreshed.
//
// Either way every other pass (the shadows among them) and every other slot
// keeps Prey's near pass, and nothing is left changed after the call.
namespace preyvr::bodydepth {

// body.depth's values.
enum class Mode : unsigned {
    native = 0,          // Prey's near pass, over everything (A/B)
    nearWorldDepth = 1,  // the near pass at the world's depth
    world = 2,           // the player's slot drawn in the world
};
inline const char* ModeName(Mode mode)
{
    switch (mode) {
    case Mode::native: return "native";
    case Mode::nearWorldDepth: return "near";
    case Mode::world: return "world";
    }
    return "?";
}

// **A view-projection's depth is two numbers.** In Prey's row-vector layout
// (translation in row 3, as NearViewStereo reads it) a perspective projection
// gives clip z = a * clip w + b: in its depth column, rows 0..2 are `a` times the
// w column (the view's forward) and row 3 is `a` times the w translation plus
// `b`. So the near pass's depth is the world's once its depth column is rebuilt
// from the world's (a, b) against its own w column, with x, y and w -- where the
// pass draws, and its parallax -- untouched. (a, b) is (-n/(f-n), nf/(f-n)) with
// reverse depth, (f/(f-n), -nf/(f-n)) without.
struct DepthMap {
    float a = 0.0f, b = 0.0f;
};

// False for a matrix whose depth column is not that shape (not a perspective
// view-projection, or not finite): nothing is read from it.
inline bool ReadDepthMap(const float* m, DepthMap& out)
{
    float ww = 0.0f, zw = 0.0f;
    for (int i = 0; i < 3; ++i) {
        ww += m[4 * i + 3] * m[4 * i + 3];
        zw += m[4 * i + 2] * m[4 * i + 3];
    }
    if (!std::isfinite(ww) || !std::isfinite(zw) || ww < 0.25f || ww > 4.0f) { return false; }
    const float a = zw / ww;
    const float b = m[14] - a * m[15];
    if (!std::isfinite(a) || !std::isfinite(b) || b == 0.0f) { return false; }
    const float tolerance = 1e-3f * std::fabs(a) + 1e-7f;
    for (int i = 0; i < 3; ++i) {
        if (!(std::fabs(m[4 * i + 2] - a * m[4 * i + 3]) <= tolerance)) { return false; }
    }
    out = {a, b};
    return true;
}

// The depth column rebuilt from `map` against `m`'s own w column.
inline void WriteDepthMap(float* m, DepthMap map)
{
    for (int i = 0; i < 3; ++i) { m[4 * i + 2] = map.a * m[4 * i + 3]; }
    m[14] = map.a * m[15] + map.b;
}

// The same projection depth, to float precision: the near pass's own (0.03 to
// 40 m) and the world's (its far plane hundreds of metres) differ in `a` tenfold.
inline bool SameDepthMap(DepthMap x, DepthMap y)
{
    return std::fabs(x.a - y.a) <= 1e-3f * std::max(std::fabs(x.a), std::fabs(y.a)) + 1e-9f &&
           std::fabs(x.b - y.b) <= 1e-4f * std::max(std::fabs(x.b), std::fabs(y.b)) + 1e-9f;
}

// The two planes `map` puts at depth 0 and 1, in metres, the nearer first; the
// farther is infinite for an infinite projection (a = 0).
inline void DepthPlanes(DepthMap map, float& nearPlane, float& farPlane)
{
    const float one = map.b / (1.0f - map.a);
    const float other = map.a != 0.0f ? -map.b / map.a : std::numeric_limits<float>::infinity();
    nearPlane = std::min(one, other);
    farPlane = std::max(one, other);
}

// What a slot render reads to decide it is a near draw.
struct NearState {
    std::uint16_t slotFlags = 0;          // slot +0xAC
    std::uint32_t slotObjectFlags = 0;    // slot +0xA8
    std::uint64_t paramsObjectFlags = 0;  // SRendParams +0x80
    std::uint32_t characterFlags = 0;     // character +0xAC8
};

inline bool NearSlot(std::uint16_t slotFlags) { return (slotFlags & native::SlotRenderNearest) != 0; }

enum class Decision {
    native,      // body.depth 0
    notPlayer,   // another character's slot, or no player character known
    otherPass,   // the player's slot, in a pass other than the main view's
    world,       // the player's slot in the main view: drawn in the world
};

// For a near slot: what its render does. The main view is the pass CSystem::Render
// builds (CreateGeneralPassRenderingInfo with its rendering flags).
inline Decision Decide(bool enabled, std::uintptr_t slotCharacter, std::uintptr_t playerCharacter,
                       std::uint32_t passFlags)
{
    if (!enabled) { return Decision::native; }
    if (playerCharacter == 0 || slotCharacter != playerCharacter) { return Decision::notPlayer; }
    if (passFlags != engine::PassInfoLayout::generalPassFlags) { return Decision::otherPass; }
    return Decision::world;
}

// `state` with every near bit clear: what the slot render reads to draw in the
// world. (Not `near`: windows.h defines it away.)
inline NearState InWorld(const NearState& state)
{
    NearState out = state;
    out.slotFlags = static_cast<std::uint16_t>(out.slotFlags & ~native::SlotRenderNearest);
    out.slotObjectFlags &= ~native::ObjectNearest;
    out.paramsObjectFlags &= ~static_cast<std::uint64_t>(native::ObjectNearest);
    out.characterFlags &= ~engine::CharacterInstanceLayout::drawNear;
    return out;
}

// `now` with the near bits `before` had put back. Nothing else is changed, so
// whatever the render wrote in the meantime stays.
inline NearState Restored(const NearState& now, const NearState& before)
{
    NearState out = now;
    out.slotFlags = static_cast<std::uint16_t>(out.slotFlags | (before.slotFlags & native::SlotRenderNearest));
    out.slotObjectFlags |= before.slotObjectFlags & native::ObjectNearest;
    out.paramsObjectFlags |= before.paramsObjectFlags & native::ObjectNearest;
    out.characterFlags |= before.characterFlags & engine::CharacterInstanceLayout::drawNear;
    return out;
}

} // namespace preyvr::bodydepth
