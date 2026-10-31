#pragma once
#include <cstddef>
#include <cstdint>
// The starfield at the sky's depth in both eyes. Steam SHA 7d6e322f...11a7.
// See docs/STARFIELD-DEPTH-2026-10-25.md. Static contracts, checked by
// tools/re/verify_starfield.py.
namespace preyvr::starfield::native {
// Arkane's ArkStars draw (RCX the space sky's render element). Its only caller is
// the element's draw 0xEFBD90, with the batch flag 8 clear (then the sun, 0xEFBA40).
// Of the element it reads, and only reads, four fields: the star mesh (+0x48;
// nothing is drawn when it is null), the star block (+0x40), the falloff texture's
// id (+0x50) and +0x54, handed to the texture's sampler. `this` is passed to no
// callee. Hooked: it is handed a copy of the element whose block is the far one.
inline constexpr std::uintptr_t ArkStarsDraw=0xEFB4B0;
inline constexpr std::size_t ElementStarBlock=0x40,ElementSize=0x58;
// The star block: the space sky's +0x170 (0xBAC90 stores that address at the
// element's +0x40 every frame; 0xBB720 rebuilds the block on the main thread).
// StarRotation's 3x3 at +0x08, nine floats in rows, which the draw sets as the
// shader's StarRotation; then the intensity +0x2C, the size +0x30 and the share
// of the stars drawn +0x34. The draw reads +0x08..+0x37.
inline constexpr std::size_t BlockRotation=0x08,BlockRotationFloats=9,BlockSize=0x38;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0xEFB4B0,1423,0x8122BDBDFBDA2C9Eull}, // ArkStarsDraw
};
}
