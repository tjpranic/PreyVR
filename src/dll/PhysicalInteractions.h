#pragma once
#include "preyvr/AnimIk.h"
#include "preyvr/WeaponRigAlignment.h"
#include <array>
#include <cstdint>
#include <string>
namespace preyvr::dll {
struct GameplayPoseFrame;
void PublishPhysicalWeapon(const GameplayPoseFrame&,const RigIdentity&,const animik::Location&,
                           Pose wristModel,const weaponrig::ContactGeometry&);
void UpdatePhysicalInteractions(const GameplayPoseFrame&,bool allowed);
bool PhysicalInteractionsEnabled();
// The weapon box is wanted: by the wrench, or by physical contact (ContactLane.h).
bool PhysicalGeometryWanted();
// The latest published weapon box, any weapon, as world-space points (centre,
// then corners; physical::Bounds) where it was at its own tracking sample.
struct WeaponBox {
    std::array<Vec3,9> points{};
    std::uint64_t rig=0,sequence=0,reference=0,epoch=0,publishedNs=0;
    std::uint64_t stamp=0;    // the tracking sample's display time, ns
    float yaw=0;    // the play-space yaw the sample was mapped with
    bool wrench=false;
};
bool TryGetWeaponBox(WeaponBox&);
void SetPhysicalMelee(unsigned);unsigned PhysicalMeleeEnabled();
void SetSwingSpeed(unsigned);unsigned SwingSpeed(); // centimetres per second
std::string PhysicalInteractionsReport();
}
