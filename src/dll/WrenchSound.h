#pragma once
#include <cstdint>
#include <string>

// **The wrench draw's sound** (docs/WRENCH-DRAW-ANIMATION-2026-10-20.md, "The equip
// sound").
//
// The replay: WrenchDraw skips the draw, and with it the two PlaySound clips that
// were its sound (Play_Player_Arm_Wrench and Play_switch_weapon, on the player,
// 11-13 ms in: WrenchSoundNative.h). A frame after each skip WrenchDraw calls
// WrenchSoundPlayDraw, which plays both on the player's audio proxy as the clips
// did. The stow is skipped too, and WrenchSoundPlayStow plays its sound
// (Play_Player_Unequip). On by default (`wrench.sound play 0` turns both off).
//
// The triggers were found with a capture hook on PlaySound clips, trigger lookups
// and proxy executes (`wrench.sound capture`), removed on 2026-10-23; see the doc
// above and git history (ba6599c) to bring it back.
namespace preyvr::dll {
// wrench.sound play [0|1].
void SetWrenchSoundPlay(unsigned on);
unsigned WrenchSoundPlay();
// Prey's main thread, a frame after WrenchDraw skipped a draw: plays the draw's
// sound on the player. False when it is off or could not play (the report says why).
bool WrenchSoundPlayDraw();
// Prey's main thread, right after WrenchDraw stowed the wrench at once: plays the
// stow's sound (Play_Player_Unequip) on the player.
bool WrenchSoundPlayStow();
std::string WrenchSoundReport();
} // namespace preyvr::dll
