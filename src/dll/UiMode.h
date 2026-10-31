#pragma once

// **Is a game menu open, and may gameplay input act?** What every input lane,
// capture and layer gates on. HudBridge samples it on the main thread
// (HudRefreshMenuOpenState, from the HUD queue's drain); these read it as plain
// values from any thread. They moved here from HudBridge.h on 2026-10-24, so
// the files that only gate on the menu need not include the HUD's dispatch.
//
// **Why it exists.** The controller menu navigator posts D-pad taps, and in
// gameplay Prey's D-pad is the weapon quick-select -- so an unguarded navigator
// changes weapons every time the right stick moves, which is exactly what a
// wearer reported: stick forward gave the torch, back the GLOO gun, left and
// right the wrench and pistol. R-116 saw the navigator was a second right-stick
// producer; this is the routing predicate it said was missing.
//
// A poll where nothing resolved leaves the previous value alone rather than
// reporting "no menu": during a load, every element is absent, and that must not
// read as gameplay and re-arm the very taps this suppresses.
namespace preyvr::dll {

bool HudMenuIsOpen();
bool HudMenuStateKnown();
unsigned long long HudMenuEpoch();
// The game is showing gameplay: the menu state is known and no native menu is
// up. What the HUD, text and note captures gate on (HudLayer). Unlike the input
// gate below it ignores VR Options, which takes the controllers but leaves the
// game running: the movies must stay captured under it, or they fall back into
// the game's own image -- both eyes, fixed to the head, behind the panel.
bool HudGameplayShown();
// HudGameplayShown, and VR Options does not own the controllers: what every
// gameplay input lane waits for.
bool HudGameplayInputAllowed();
unsigned long long HudMenuStateSampleCount();

} // namespace preyvr::dll
