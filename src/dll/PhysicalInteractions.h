#pragma once
#include "preyvr/AnimIk.h"
#include "preyvr/WeaponRigAlignment.h"
#include <string>
namespace preyvr::dll {
struct GameplayPoseFrame;
void PublishPhysicalWeapon(const GameplayPoseFrame&,const RigIdentity&,const animik::Location&,
                           Pose wristModel,const weaponrig::ContactGeometry&);
void UpdatePhysicalInteractions(const GameplayPoseFrame&,bool allowed);
bool PhysicalInteractionsEnabled();
void SetPhysicalMelee(unsigned);unsigned PhysicalMeleeEnabled();
void SetSwingSpeed(unsigned);unsigned SwingSpeed(); // centimetres per second
std::string PhysicalInteractionsReport();
}
