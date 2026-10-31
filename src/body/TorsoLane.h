#pragma once

#include "controls/locomotion/ZeroGControls.h"

#include <windows.h>

#include <string>

// **The player's torso**, estimated once a frame and shared: the head's yaw
// behind a deadzone, relaxing toward it (armpose::TorsoYaw). The arm solver's
// shoulders and the holster and medkit slots all read this one copy.
//
// On the main thread, at the entry of ArkPlayer::Update (R-021's function,
// vtable slot 19), where a snap turn queued last frame also lands
// (HeadTrackingHook.h ApplyQueuedSnapTurn): before the frame's view and arm IK.
//
// **The body is turned to the torso**, through the engine's own view-rotation
// setter, with the reference turned by the same angle (body/BodyTurn.h): the
// body faces the player and the view stays where it was.
//  - After each snap turn, at once. While it can, the snap turns the view about
//    the body rather than the head (SetSnapTurnsAboutBody), so the two together
//    are one turn about the capsule, as a smooth turn's is, and the camera keeps
//    its place on the model.
//  - As the player turns round in the room, every frame, no faster than
//    360 degrees a second -- but never while the game is turning the player
//    itself (smooth turning), nor for 150 ms after. Decided at the entry but
//    made after the frame's render (ServiceBodyFollow): the frame's animation
//    had already started, posed for the body as it was, so a body turned before
//    the render was drawn with a pose meant for the old facing
//    (docs/BODY-FOLLOW.md).
// Body Follows You followed every frame regardless and also moved the capsule
// under the head; both were removed on 2026-10-24, because with either on,
// smooth turning jittered -- the turn, it was found. The capsule follows again
// (RoomScaleLane.h), after the frame's render with this lane's turn.
namespace preyvr::dll {

// Installs the hook; VR mode calls it. 0 on success, 1 no PreyDll, 2 a byte or
// vtable mismatch, 3 the hook failed. 0 stops the estimate and clears it.
DWORD SetTorsoLaneActive(unsigned active);
// The torso filter's settings, the one copy: deadzone in degrees, relaxation in
// per mille per second. body.torso and arm.torso both set them, and the arms'
// own filter (AnimIk, only while this lane has no fresh torso) reads them.
DWORD SetBodyTorso(int deadzoneDegrees, int relaxPerMille);
// The same settings as the filters use them: radians, and a fraction per second.
void BodyTorsoSettings(float& deadzoneRadians, float& relax);
// After the frame's world render, on the main thread (FrameSchedule): makes the
// follow's turn the entry decided, then room-scale's move (RoomScaleLane.h),
// before the next frame's animation starts.
void ServiceBodyFollow();
// Zero-G (docs/ZERO-G-SCOPE-2026-10-30.md): the player's movement state is
// Prey's zero-G one, as of this frame's ArkPlayer::Update. The body's turns
// above are then about its own up axis, keeping its pitch and roll, and
// room-scale stands.
bool PlayerInZeroG();
// A snap step of zero-G roll or pitch, signed degrees about the body's own axis
// (controls/locomotion/ZeroGControls.h), made at the next ArkPlayer::Update entry. Refused
// outside zero-G; a later one replaces one not yet made.
DWORD QueueZeroGStep(zerog::Axis axis, int degrees);
// The torso yaw (tracking space, armpose::YawOf convention), when the lane
// estimated one within the last 100 ms.
bool BodyTorsoYaw(float& yaw);

std::string TorsoLaneReport();

} // namespace preyvr::dll
