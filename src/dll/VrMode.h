#pragma once
#include <string>
namespace preyvr::dll {
// Owned by the command worker; the worker polls hotkeys and advances startup.
void EnableVrMode();
void DisableVrMode();
void TickVrMode();
std::string VrModeReport();
// While active, r_DrawNearFoV follows the vertical FOV the world camera rendered
// (default on). 0 keeps the value set at activation (120-degree assumption).
void FollowNearFov();
unsigned long SetNearFovFollow(unsigned enabled);
// **Anti-aliasing** (VR Options > Setup): 1 temporal -- the per-eye TSAA lane
// (TemporalAaLane.h) with r_AntialiasingMode 4 -- or 0 SMAA 1X (mode 1). VR
// start applies the mode with its renderer settings; a change while VR runs is
// queued to the console, again while the console is busy.
unsigned long SetTemporalAntialiasing(unsigned enabled);
unsigned TemporalAntialiasingEnabled();
bool NearFovFollowing();
unsigned NearFovFollowed();
}
