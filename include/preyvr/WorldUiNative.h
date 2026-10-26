#pragma once
#include <cstddef>
#include <cstdint>
// A world UI's cursor: the screen of a terminal, keypad or kiosk (CryEngine
// FlashUI elements with Arkane's WorldUI block). Steam SHA 7d6e322f...11a7. See
// docs/WORLD-UI-POINTER-2026-10-26.md. Static contracts, checked by
// tools/re/verify_world_ui_cursor.py.
namespace preyvr::worldui::native {
// The element's cursor raycast (RCX the UI element). It asks gEnv->pGame
// (+0xA28, vtable +0x198) for the IArkPlayer and reads the player's cached
// reticle ray through its slot 0, the getter 0x157CBB0 (aim.get_cached_ray).
// It casts that ray `distance` (element +0x1E8) long, as RayWorldIntersection
// (types 7, flags 0x40F). A hit on the element's screen entity becomes the
// cursor: 0x301A60 maps the hit point through the screen's local bounds to
// pixels at element +0x200/+0x204. While an examination is on (ArkPlayer
// +0xA48 == 1) Prey builds that ray from its examination cursor (0x1585320).
inline constexpr std::uintptr_t CursorRay=0x3017C0;
// Where its call of the getter (0x30190F, `call [rax]`) returns.
inline constexpr std::uintptr_t RayReadReturn=0x301911;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0x3017C0,666,0x0F4F513F594BFADFull}, // WorldUiCursorRay
};
}
