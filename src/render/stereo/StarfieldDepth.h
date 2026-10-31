#pragma once
#include "render/stereo/StarfieldNative.h"
#include <array>
#include <cstdint>
#include <cstring>

// **The stars at the sky's depth, not 1 m from the head**
// (docs/STARFIELD-DEPTH-2026-10-25.md).
//
// Prey's stars are unit vectors (stars.dat; each vertex's w is 1) drawn through
// the space sky's render object, which the main thread places at the camera it
// culls from. In the both-eyes frame that camera is the head, and each eye is
// drawn from half the IPD beside it (DualSceneStereo.h). ArkStarsVS's
// _pos_Common takes the eye from the object's position, so every star sat 1 m
// from the head -- about IPD / 1 m apart between the eyes, 4 degrees -- while
// its depth is the far plane: double vision.
//
// The shader scales nothing but the rotation (pos.xyz = mul((float3x3)
// StarRotation, pos.xyz)), so a scaled StarRotation draws the stars that far
// out and leaves the head-to-eye offset nothing to shift. A star's size is set
// in clip space (its quad offset times w) and its depth is forced to the far
// plane (xyww), so a flat frame is drawn the same.
namespace preyvr::starfield {

// How far out the stars are drawn. What is left of the error is the eye's
// offset over this: 35 um at 10 km for half a 69 mm IPD, 0.0002 degrees.
inline constexpr float kStarDistanceMetres = 10000.0f;

static_assert(native::BlockRotation + native::BlockRotationFloats * sizeof(float) <= native::BlockSize);

using StarBlock = std::array<std::uint8_t, native::BlockSize>;

// The block as the draw is to read it: StarRotation scaled to `distance`, every
// other byte the block's own.
inline StarBlock FarStarBlock(const StarBlock& block, float distance = kStarDistanceMetres)
{
    // Not `far`: windows.h defines it away.
    StarBlock out = block;
    for (std::size_t i = 0; i < native::BlockRotationFloats; ++i) {
        std::uint8_t* const at = out.data() + native::BlockRotation + i * sizeof(float);
        float value = 0;
        std::memcpy(&value, at, sizeof(value));
        value *= distance;
        std::memcpy(at, &value, sizeof(value));
    }
    return out;
}

} // namespace preyvr::starfield
