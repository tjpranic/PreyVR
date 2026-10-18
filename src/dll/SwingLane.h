#pragma once
#include <string>

// The free-swing wrench (docs/FREE-SWING-WRENCH-SCOPE-2026-10-18.md). Phase 1,
// diagnostic only: once a gameplay frame, on the input-drain thread, the drawn
// wrench's box is tested against everything Prey's own melee asks for (scenery,
// glass, props, bodies), and each contact gets the decision a strike would:
// fast enough, one hit per pass, a clear line from the head, stamina to spare.
// Nothing in Prey is called that acts: the decisions, what was touched (class,
// surface name, breakability) and the wrench's own fields go to the log
// (`preyvr_swing`), the `swing` report and the debug overlay's physical layer.
namespace preyvr::dll {
struct GameplayPoseFrame;
void UpdateSwingLane(const GameplayPoseFrame&, bool allowed);
// The weapon box is wanted (PhysicalInteractions.h PhysicalGeometryWanted).
bool SwingLaneWanted();
// The per-event lines (begin, strike, swing); on by default while this is a
// diagnostic. Counters, the 10 s report and the once-only lines always run.
void SetSwingEventLog(unsigned on);
unsigned SwingEventLog();
std::string SwingReport();
} // namespace preyvr::dll
