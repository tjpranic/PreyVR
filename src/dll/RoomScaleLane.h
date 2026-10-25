#pragma once

#include <string>

// **Room-scale** (docs/BODY-FOLLOW.md, room-scale): the capsule follows the
// player across the room, so the body stays under them.
//
// After each frame's render, on the main thread, with the frame's body turn
// (TorsoLane's ServiceBodyFollow): the frame's render has taken the body's
// transform and the next frame's animation has not started, so the move is
// posed for at once; and physics is idle there (99.97% of frames, the research
// build), so the move applies at once rather than queued behind a step.
//  - **What it follows:** the player's eye as it would be with the head level
//    and facing the torso (armpose::LevelEye), once it leads the capsule by
//    more than the 25 cm lean zone, or bent well down more than the lean that
//    drop explains; within it the body pin bends the torso. A head that comes
//    to rest upright, not lowered as a lean, was a step: the capsule glides
//    under it (preyvr/RoomScale.h).
//  - **How it moves:** the living body's own stance test says how much of the
//    move fits, so the capsule stops at walls and furniture instead of being
//    pushed back out; a moving body keeps its motion (pe_params_pos with
//    bRecalcBounds 0x21), never the engine's teleport, and waits a frame while
//    physics is busy; the origin moves by exactly what was applied, so the view
//    does not move (HeadTrackingHook.h MoveHeadTrackingOriginWithNative).
//  - **The head in a wall:** when the line from inside the capsule to the head's
//    pivot and on to the eyes passes through something, the view fades out and
//    firing, using and psi wait (BodyHeadOut).
namespace preyvr::dll {

// After the frame's render, main thread: `player` is the ArkPlayer TorsoLane
// checked at the frame's start, `torsoYaw` its torso (tracking space), `dt` the
// frame's time, and `gameplay` whether the ordinary camera has the view.
void ServiceRoomScale(void* player, float torsoYaw, float dt, bool gameplay);
// The head is through geometry: the view fades out, and firing, using and psi wait.
bool BodyHeadOut();
// Room-scale moved or held the capsule under the player within the last 200 ms:
// the body pin then leans as far as the spine does, rather than giving up on a
// head far from a capsule the room-scale would have followed.
bool RoomScaleActive();
// Frames moved and why not, as body.report reports it.
std::string RoomScaleReport();

} // namespace preyvr::dll
