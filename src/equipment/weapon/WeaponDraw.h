#pragma once
#include <string>

// **Weapons come out without their draw animation**
// (docs/WEAPON-DRAW-SCOPE-2026-10-31.md, docs/WRENCH-DRAW-ANIMATION-2026-10-20.md).
//
// Prey's draw (the Mannequin fragment Wpn_Select, or Wpn_Pickup on a first
// pickup) turns the weapon inside the hand. The IK keeps the weapon on the
// controller, so the hand spun around it instead. The select action OnEquip
// builds for every weapon is not queued: what its Enter does is done at once
// (a select marks the weapon busy), and what its Exit does -- Prey's own
// select-finished, through the weapon's vtable since the classes differ there -- a
// frame later on the main thread. Mannequin falls through to the weapon's idle,
// already queued. The draw's sound went with it; the frame after a skip WeaponSound
// plays it on the player.
//
// The stow too: the shared OnUnequip, asked to animate, stows at once instead
// (Prey's own no-animation path, which ends as the stow action's Exit does), and
// the stow's sound plays at once. A weapon stowed with an attack under way keeps
// Prey's animated stow.
namespace preyvr::dll {
// Prey's main thread, once a frame (MainRenderHook's CSystem::Render detour):
// gates and installs the hooks the first time, then finishes the selections it
// skipped, each once a whole frame has passed. The hooks act only on this thread.
void ServiceWeaponDrawFrame();
// VR Options' Weapon Draw Animations: 0 (default) skips every weapon's draw and stow
// animations, 1 plays them. Skipped selections already waiting are finished either way.
void SetWeaponDrawAnimations(unsigned play);
unsigned WeaponDrawAnimations();
std::string WeaponDrawReport();
} // namespace preyvr::dll
