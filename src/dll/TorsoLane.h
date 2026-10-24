#pragma once

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
// **After each snap turn the body is turned to the torso**, through the
// engine's own view-rotation setter, with the reference turned by the same
// angle (preyvr/BodyTurn.h): the body faces the player again and the view stays
// where the snap put it. One turn per snap. While it can, the snap turns the
// view about the body rather than the head (SetSnapTurnsAboutBody), so the two
// together are one turn about the capsule, as a smooth turn's is, and the camera
// keeps its place on the model. Body Follows You did it every frame
// and also moved the capsule under the head (docs/BODY-YAW-2026-10-11.md,
// docs/ROOMSCALE-2026-10-11.md); both were removed on 2026-10-24, because with
// either on, smooth turning jittered.
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
// The torso yaw (tracking space, armpose::YawOf convention), when the lane
// estimated one within the last 100 ms.
bool BodyTorsoYaw(float& yaw);

std::string TorsoLaneReport();

} // namespace preyvr::dll
