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
// VR Options (Hands): Free-Swing Wrench (on by default), Minimum Swing Speed and
// Heavy Swing Speed, cm/s at the contact point (100-700 and 300-1500). From the
// heavy speed a strike is Prey's charged hit, its whole charge bonus 3 m/s on; a
// heavy speed at or under the minimum makes every strike heavy.
void SetFreeSwing(unsigned on);
unsigned FreeSwingEnabled();
void SetSwingSpeed(unsigned cms);
unsigned SwingSpeed();
void SetHeavySwingSpeed(unsigned cms);
unsigned HeavySwingSpeed();
// A prop or body the wrench struck in the last 150 ms: physical contact leaves it
// to the hit's impulse.
bool SwingRecentlyStruck(const void* collider);
// The per-event lines (begin, strike, swing), off by default (`swing log 1`).
// Counters, the 10 s report and the once-only lines always run.
void SetSwingEventLog(unsigned on);
unsigned SwingEventLog();
std::string SwingReport();
} // namespace preyvr::dll
