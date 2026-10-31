#pragma once
#include <cstdint>
#include <string>

// **A weapon draw's sound** (docs/WEAPON-DRAW-SCOPE-2026-10-31.md,
// docs/WRENCH-DRAW-ANIMATION-2026-10-20.md, "The equip sound").
//
// WeaponDraw skips the draw, and with it the PlaySound clips that were its sound: the
// weapon's own trigger and Play_switch_weapon, on the player, 10-16 ms in
// (WeaponSoundNative.h). A frame after each skip WeaponDraw calls WeaponSoundPlayDraw,
// which plays them on the player's audio proxy as the clips did. The stow is skipped
// too, and WeaponSoundPlayStow plays its sound. On by default (`weapon.sound play 0`
// turns both off).
//
// The triggers were found with a capture hook on PlaySound clips, trigger lookups and
// proxy executes (`weapon.sound capture`), removed again on 2026-10-31: the version that
// covers every weapon is in docs/evidence/weapon-draw-2026-10-31/.
namespace preyvr::dll {
// weapon.sound play [0|1].
void SetWeaponSoundPlay(unsigned on);
unsigned WeaponSoundPlay();
// Which sounds a weapon of this entity class plays, for the two below.
unsigned WeaponSoundFor(const char* className);
// Prey's main thread, a frame after WeaponDraw skipped a draw: plays the draw's
// sound on the player. False when it is off or could not play (the report says why).
bool WeaponSoundPlayDraw(unsigned sound);
// Prey's main thread, right after WeaponDraw stowed a weapon at once: plays the
// stow's sound on the player.
bool WeaponSoundPlayStow(unsigned sound);
std::string WeaponSoundReport();
} // namespace preyvr::dll
