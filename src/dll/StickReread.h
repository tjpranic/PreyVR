#pragma once

#include <string>

#include <windows.h>

// **The engine keeps the stick the lanes hold** (docs/MOVE-STOP-2026-10-24.md).
//
// Prey's player input re-reads the move and yaw axes from the physical pad on an
// action-map or filter event while you play: leaving the weapon wheel, a keypad
// or a menu, casting a psi power, any filter a level script switches. The lanes'
// posts never set the pad's symbols, so the re-read stored 0 and stopped the
// player. The lanes post a stick only when it changes, so a stick held at the
// rim (strafing, a full smooth turn) stayed stopped until it was nudged.
//
//  * The re-read finds what the lanes hold (fix 1). AxisRead (0x158EB90), which
//    only the re-read calls, returns the lane's delivered value for move X, move
//    Y and yaw instead of the idle pad's zero -- not when the action is
//    filtered, and not when the pad itself is pushed. `move.reread 0` keeps the
//    pad's value, and the counting, for A/B.
//  * The listener (0x1590390) is watched, to name the event that re-read and
//    whether it would have stopped the player.
//
// ReadStickAxes is MoveLane's per-frame read-back (fix 2).
namespace preyvr::dll {

// Installs both hooks, once; 0 when they are in. Not fatal: without them the
// read-back still restores a stick within a frame.
DWORD InstallStickReread();
// 1 (default): the re-read finds the lanes' stick. 0: the pad's, as Prey reads it.
DWORD SetStickRereadFix(unsigned enabled);
unsigned StickRereadFix();

// The axes ArkPlayerInput holds now, as its handlers stored them. `gate` is the
// cinematic gate (+0x94); `yawBlocked` says the yaw handler stores nothing
// (+0xB4 set, or the gate 2).
struct StickAxes {
    float moveX = 0.0f;
    float moveY = 0.0f;
    float yaw = 0.0f;
    int gate = 0;
    bool yawBlocked = false;
};
// False with no player, or when the player's input is not ArkPlayerInput.
// Main thread: it asks the game for its player.
bool ReadStickAxes(StickAxes& out);

// " reread={...}" for the status line and move.report.
std::string StickRereadReport();

} // namespace preyvr::dll
