#pragma once
#include <string>

// The free-swing wrench (docs/FREE-SWING-WRENCH-SCOPE-2026-10-18.md). Once a
// gameplay frame, on the input-drain thread, the drawn wrench's box is tested
// against everything Prey's own melee asks for (scenery, glass, props, bodies,
// live characters). A contact strikes when it is fast enough, the first of its
// pass, in clear sight of the head and with stamina to spare: Prey's own hit is
// called with it, without Prey's attack (phase 2). Damage, knockback, sounds,
// decals, glass and stamina are Prey's. Prey's attack, with the wrench out, does
// nothing. The log (`preyvr_swing`), the `swing` report and the debug overlay's
// physical layer show each decision.
namespace preyvr::dll {
struct GameplayPoseFrame;
void UpdateSwingLane(const GameplayPoseFrame&, bool allowed);
// The weapon box is wanted (PhysicalInteractions.h PhysicalGeometryWanted).
bool SwingLaneWanted();
// VR Options: Free-Swing Wrench (on by default) and Minimum Swing Speed, cm/s at
// the contact point (80-250).
void SetFreeSwing(unsigned on);
unsigned FreeSwingEnabled();
void SetSwingSpeed(unsigned cms);
unsigned SwingSpeed();
// A prop or body the wrench struck in the last 150 ms: physical contact leaves it
// to the hit's impulse.
bool SwingRecentlyStruck(const void* collider);
// The per-event lines (begin, strike, swing); on by default while the feature is
// being proven. Counters, the 10 s report and the once-only lines always run.
void SetSwingEventLog(unsigned on);
unsigned SwingEventLog();
std::string SwingReport();
} // namespace preyvr::dll
