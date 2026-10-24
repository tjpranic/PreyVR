#pragma once
#include <string>
// Surface stop (docs/SURFACE-STOP-SCOPE-2026-10-21.md): the drawn hands and weapon
// stop against fixed scenery (walls, floors, desks, glass, doors) and against
// heavy or immovable loose objects, while the tracked hands go on. Each gameplay
// frame, before the frame is published, each hand's probes (its palm box, and the
// right hand's weapon) travel along the scene query's rays (surface::Solve), and
// the correction they need goes into `GameplayPoseFrame::surface`. The animation
// takeover draws the hand there, and the lanes that act where the hand is (shots,
// interaction, pushing, grenades, psi) read it; strikes take the real swing.
// A heavy prop holding a hand back is handed to the contact lane, which pushes it
// as hard as the real hand presses (surface::Press). Light props, corpses and
// characters never hold a hand back. Counted in `preyvr_surface` reports
// (per-event lines on request) and drawn in the debug overlay's physical layer.
// On by default (VR Options, Hands).
namespace preyvr::dll {
struct GameplayPoseFrame;
// Main thread, in the aim hook, after the two-handed solve and before the
// frame is published.
void UpdateSurfaceStop(GameplayPoseFrame& frame, bool allowed);
void SetSurfaceStop(unsigned enabled);
unsigned SurfaceStopEnabled();
// Heavy Objects (VR Options, Hands): loose props heavier than this many kg
// (1..100, default 10) hold a hand back; lighter ones are pushed through. A prop
// the game gave no mass always holds it.
void SetSurfaceHeavyMass(unsigned kg);
unsigned SurfaceHeavyMass();
// The weapon's box among the right hand's probes (`surface weapon 0|1`, default 1):
// off, only the hand itself stops.
void SetSurfaceStopWeapon(unsigned on);
unsigned SurfaceStopWeapon();
// The per-event hold and let-go lines (`surface log 1`); off by default.
void SetSurfaceStopEventLog(unsigned on);
unsigned SurfaceStopEventLog();
std::string SurfaceStopReport();
}
