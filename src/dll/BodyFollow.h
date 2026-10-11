#pragma once

#include <windows.h>

#include <string>

// Body following (docs/BODY-YAW-2026-10-11.md; the arithmetic is
// preyvr/BodyFollow.h): **the player's body turns with the player.**
//
// The body mesh, capsule and arms rig face the native player yaw, which
// neither a physical turn nor a snap turn changed: look down after turning
// round and you saw the character's back. Every frame the native player is
// turned to the torso -- the head's yaw behind a deadzone, armpose::TorsoYaw --
// through the engine's own view-rotation setter, and the head-tracking
// reference turns by the same angle, so the view, the aim and the hands do not
// move. A snap turn, a recenter or a smooth turn needs nothing of its own: any
// change of the reference is a gap between it and the torso, which the next
// frame closes by turning the body.
//
// On the game thread, at the entry of ArkPlayer::Update (R-021's function,
// vtable slot 19): before the frame's view. Whether it is also before the
// frame's arm IK is measured, not assumed (body.report ik*): a turn applied
// after the IK draws the hands off by the turn for one frame.
namespace preyvr::dll {

// Installs the hook and verifies the setter's bytes; VR mode calls it. 0 on
// success, 1 no PreyDll, 2 a byte or vtable mismatch, 3 the hook failed.
DWORD SetBodyFollowActive(unsigned active);
// The player's choice (VR options, body.follow). The torso is estimated and
// shared whenever the lane is active, followed or not.
DWORD SetBodyFollow(unsigned enabled);
unsigned BodyFollowEnabled();
// Write a level rotation (no pitch or roll), on by default: native pitch
// bends the body mesh, and nothing in VR reads it.
DWORD SetBodyFollowLevel(unsigned enabled);
// The shared torso filter: deadzone in degrees, relaxation in per mille per
// second (arm.torso sets the same filter).
DWORD SetBodyTorso(int deadzoneDegrees, int relaxPerMille);
// Diagnostic: one native turn of `degrees`, with the reference turned by the
// same angle -- the body turns and the view does not. Next frame, even with
// following off.
DWORD RequestBodyTurn(int degrees);
std::string BodyFollowReport();

// The torso yaw the body is turned to (tracking space, armpose::YawOf
// convention), when the lane estimated one within the last 100 ms.
bool BodyTorsoYaw(float& yaw);

// Frame-order probes: the view seam, the owner rig's IK callback and the
// CSystem::Render hook.
void NoteBodyFollowView(float playSpaceYaw, unsigned long long referenceGeneration);
void NoteBodyFollowIk();
void NoteBodyFollowRender();

} // namespace preyvr::dll
