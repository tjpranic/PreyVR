#pragma once
#include <cstdint>
#include <cstddef>
// The weapon component's Equip and CanEquip, which the grip draw (ArmsLane) and
// the grenade lane call after checking their prologue bytes, and the bodies the
// weapon draw skip hashes for SelectItem, the weapon lookup and the player
// (WeaponDraw.cpp, WeaponDrawNative.h). Steam PreyDll 7D6E322F...B05311A7;
// sizes are complete bodies.
namespace preyvr::equipment::native {
inline constexpr std::uintptr_t Equip=0x1274820,CanEquip=0x1273EB0;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
 {0x12748D0,444,0x03CD03E55C5F1CEBull}, // EquipWeapon
 {0x157C990,48,0xB0D3EB821FBC9AB5ull}, // Player
 {0x16A5650,72,0xAAA04A7A0DEBB0F5ull}, // WeaponLookup
};
}
