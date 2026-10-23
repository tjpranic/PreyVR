#pragma once
#include <string>

// **The wrench comes out without its draw animation**
// (docs/WRENCH-DRAW-ANIMATION-2026-10-20.md).
//
// Prey's draw (the Mannequin fragment Wpn_Select, or Wpn_Pickup on a first
// pickup) turns the wrench inside the hand. The IK keeps the wrench on the
// controller, so the hand spun around it instead. The select action OnEquip
// builds for the wrench is not queued: what its Enter does is done at once
// (a select marks the weapon busy), and what its Exit does -- Prey's own
// select-finished, which ends the weapon switch -- a frame later on the main
// thread. Mannequin falls through to the wrench's idle, already queued. The draw's
// sound went with it; the frame after a skip WrenchSound plays it on the player.
//
// The stow too: the wrench's own OnUnequip, asked to animate, stows at once
// instead (Prey's own no-animation path, which ends as the stow action's Exit
// does), and its sound (Play_Player_Unequip) plays at once. A lowered wrench
// (EMP, friendly NPCs) keeps Prey's animated stow.
namespace preyvr::dll {
// Prey's main thread, once a frame (MainRenderHook's CSystem::Render detour):
// gates and installs the hook the first time, then finishes the selections it
// skipped, each once a whole frame has passed. The hook acts only on this thread.
void ServiceWrenchDrawFrame();
// wrench.draw: 1 (default) skips the wrench's draw and stow animations, 0 plays
// them again. Skipped selections already waiting are finished either way.
void SetWrenchDrawSkip(unsigned on);
std::string WrenchDrawReport();
} // namespace preyvr::dll
