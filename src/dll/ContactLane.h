#pragma once
#include <string>
// Physical contact (docs/PHYSICAL-CONTACT-SCOPE-2026-10-18.md): each gameplay
// frame, which movable objects the held weapon's box touches, with the engine's
// own broadphase and per-entity box test (ContactNative.h), and pushing the rigid
// ones so their surface keeps up with it, as hard as a hand pushes (contact::Solve).
// Phase 3: the hands too -- the right while no weapon is drawn, the left while it
// is free -- each with a palm-sized box. Phase 4: corpses, one body part at a time
// (a live character's skeleton, which has a host, never). Phase 5: never what the
// player stands on. Counted in `preyvr_contact` reports (per-event lines on
// request) and drawn in the debug overlay's physical layer. On by default (VR
// Options, Hands).
namespace preyvr::dll {
struct GameplayPoseFrame;
// Input-drain thread, once per gameplay frame, after the physical lane.
void UpdateContactLane(const GameplayPoseFrame&,bool allowed);
void SetPhysicalContact(unsigned enabled);
unsigned PhysicalContactEnabled();
// Percent, 25..200, of the force a push may use (contact::PushSettings::force).
void SetPushStrength(unsigned percent);
unsigned PushStrength();
// The per-event begin, end and push lines (`contact log 1`); off by default.
void SetContactEventLog(unsigned on);
unsigned ContactEventLog();
std::string ContactReport();
}
