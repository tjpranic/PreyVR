#pragma once
#include <cstddef>
#include <cstdint>
// Prey's zoom: how the view's FOV comes from the player's zoom manager.
// Steam SHA 7d6e322f...11a7. See docs/PSYCHOSCOPE-ZOOM-SCOPE-2026-10-31.md. Static
// contracts, checked by tools/re/verify_view_zoom.py.
namespace preyvr::viewzoom::native {
// The view's hfov (RCX the zoom manager, XMM0 degrees): the manager's blended
// value, taken as an offset from Prey's 85.57-degree base and added to cl_hfov, at
// least 1. Called once, by ArkPlayerCamera::UpdateView, for the view's FOV. Reads
// memory and calls one getter, so it is called here for that same value.
inline constexpr std::uintptr_t FinalHfov=0x1490BE0;
// ArkPlayer +0x1898, the zoom manager; the camera UpdateView is given is ArkPlayer +0x12A0.
inline constexpr std::size_t PlayerZoomManager=0x1898,PlayerCamera=0x12A0;
// The pointer to the game's cvars, and cl_hfov in them: the FOV slider, degrees.
inline constexpr std::uintptr_t GameCVars=0x2C16830;
inline constexpr std::size_t CVarsHfov=0x08;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0x1490BE0,56,0xA335BDD74506BFA8ull}, // ZoomFinalHfov
};
}
