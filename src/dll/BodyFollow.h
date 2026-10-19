#pragma once

#include <windows.h>

#include <string>

// Body following (docs/BODY-YAW-2026-10-11.md; the arithmetic is
// preyvr/BodyFollow.h): **the player's body turns with the player** -- and,
// with room-scale below, moves with them (preyvr/RoomScale.h).
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
// same angle -- the body turns and the view does not. Even with following off.
// Runs `delaySeconds` after the request, in gameplay (typed in the tuner, the
// game is paused until refocused); dropped if not run within a minute of that.
DWORD RequestBodyTurn(int degrees, int delaySeconds = 5);

// Room-scale (docs/ROOMSCALE-2026-10-11.md): the capsule follows the head
// across the room -- swept for walls, through the entity's own SetPos -- and
// the play-space origin moves by exactly the move applied, so the view does
// not. Only this move shifts the origin: stick walking, lifts and scripted
// moves move the capsule natively and the view with it. Needs positional head
// tracking in the view; refused, and counted, without it.
DWORD SetRoomScale(unsigned enabled);
unsigned RoomScaleEnabled();
// Idle until the head is `startMm` from the body, then follow to `stopMm`,
// closing the gap with time constant `followMs` (negative: unchanged).
DWORD SetRoomScaleZone(int startMm, int stopMm, int followMs = -1);
int RoomScaleStartMm();
int RoomScaleStopMm();
int RoomScaleFollowMs();
// The capsule radius the sweep assumes (not read from the engine).
DWORD SetRoomScaleRadius(int radiusMm);
// The living body's own physics (docs/TURN-LEAN-ARMS-2026-10-19.md, fixes 1
// and 2; on by default): its stance test decides where the capsule fits before
// it moves, and a move while it walks keeps its motion. 0: the 9 rays and the
// entity's plain SetPos, as before.
DWORD SetRoomScalePhysics(unsigned enabled);
// 1: no room-scale move while the engine moves the capsule faster than
// 0.25 m/s (stick walking, falling). 0 (default since 2026-10-19): the body
// follows you then too, its moves keeping the walk's motion.
DWORD SetRoomScaleStandAside(unsigned enabled);
// Diagnostic: one swept, compensated move of the capsule relative to the
// body's facing -- the body moves, the view must not. Deferred like the turn.
DWORD RequestBodyNudge(int rightCm, int forwardCm, int delaySeconds = 5);

// The head-in-wall layer: true while the head is through geometry, or far from
// a body the sweep stopped. The XR fade blacks the view out; fire and use are
// held off. Fresh within 100 ms, and only with room-scale on.
bool BodyHeadOut();

// The VR option: 0 off, 1 turn only, 2 turn and move.
DWORD SetBodyFollowMode(unsigned mode);
unsigned BodyFollowMode();

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
