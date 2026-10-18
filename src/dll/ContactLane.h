#pragma once
#include <string>
// Physical contact (docs/PHYSICAL-CONTACT-SCOPE-2026-10-18.md): each gameplay
// frame, which movable objects the held weapon's box touches, with the engine's
// own broadphase and per-entity box test (ContactNative.h), and -- phase 2 --
// pushing the rigid ones so their surface keeps up with the weapon, as hard as
// a hand pushes (contact::Solve). Logged (`preyvr_contact`) and drawn in the debug
// overlay's physical layer. Off by default (VR Options, Experimental).
namespace preyvr::dll {
struct GameplayPoseFrame;
// Input-drain thread, once per gameplay frame, after the physical lane.
void UpdateContactLane(const GameplayPoseFrame&,bool allowed);
void SetPhysicalContact(unsigned enabled);
unsigned PhysicalContactEnabled();
// Percent, 25..200, of the force a push may use (contact::PushSettings::force).
void SetPushStrength(unsigned percent);
unsigned PushStrength();
std::string ContactReport();
}
