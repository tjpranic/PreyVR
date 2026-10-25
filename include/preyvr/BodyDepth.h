#pragma once
#include "preyvr/BodyDepthNative.h"
#include "preyvr/EngineMap.h"
#include <cstdint>

// **The player's body drawn in the world, so what is in front of it hides it.**
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
// The near pass is chosen for each slot render (BodyDepthNative.h). For the
// player's slot in the main view, the near bits are clear for the call: the
// slot draws at its world transform, at the world's depth, and the eyes' own
// views give it its parallax. Every other pass (the shadows among them) and
// every other slot keeps Prey's near pass, and nothing is left changed after
// the call.
namespace preyvr::bodydepth {

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
