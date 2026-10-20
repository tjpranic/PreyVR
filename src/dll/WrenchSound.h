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
// The capture, off by default, is how they were found. While it is on, the wrench's
// draw plays again (WrenchDraw does not skip it), and for 3 s after each draw starts
// the log gets, timed from that start (`preyvr_wrench_sound`):
//  - `clip`: each Mannequin PlaySound clip that enters (its start and stop
//    triggers, fire-and-forget, sound flags, joint, entity);
//  - `resolve`: each audio trigger looked up by name (name, id, found);
//  - `execute`: each trigger played on an entity's audio proxy (id, entity).
namespace preyvr::dll {
// wrench.sound capture [0|1].
void SetWrenchSoundCapture(unsigned on);
bool WrenchSoundCapturing();
// wrench.sound play [0|1].
void SetWrenchSoundPlay(unsigned on);
unsigned WrenchSoundPlay();
// WrenchDraw's PlayAction hook, on the wrench's select action while capturing:
// opens a window. `item` is the wrench's entity id.
void WrenchSoundSelectSeen(std::uint32_t item, bool select);
// Prey's main thread, a frame after WrenchDraw skipped a draw: plays the draw's
// sound on the player. False when it is off or could not play (the report says why).
bool WrenchSoundPlayDraw();
// Prey's main thread, right after WrenchDraw stowed the wrench at once: plays the
// stow's sound (Play_Player_Unequip) on the player.
bool WrenchSoundPlayStow();
// Prey's main thread, once a frame (ServiceWrenchDrawFrame): installs the capture's
// hooks the first time it is wanted, and ends windows.
void ServiceWrenchSoundFrame();
std::string WrenchSoundReport();
} // namespace preyvr::dll
