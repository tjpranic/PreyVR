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
// The weapon box is wanted: by the wrench, by physical contact (ContactLane.h), or
// by the free-swing wrench (SwingLane.h).
bool PhysicalGeometryWanted();
// The latest published weapon box, any weapon, as world-space points (centre,
// then corners; physical::Bounds) where it was at its own tracking sample.
struct WeaponBox {
    std::array<Vec3,9> points{};
    std::uint64_t rig=0,sequence=0,reference=0,epoch=0,publishedNs=0;
    std::uint64_t stamp=0;    // the tracking sample's display time, ns
    float yaw=0;    // the play-space yaw the sample was mapped with
    bool wrench=false;
    // The same points in tracking reference space (engine axes), the map that put
    // them in the world, and the right grip there: a pose's motion without the
    // player's own (walking, snap turns) is the reference points' motion.
    std::array<Vec3,9> local{};
    Pose worldFromReference{};
    Vec3 grip{};
    std::uintptr_t weapon=0;  // the weapon object the box is of
};
bool TryGetWeaponBox(WeaponBox&);
void SetPhysicalMelee(unsigned);unsigned PhysicalMeleeEnabled();
void SetSwingSpeed(unsigned);unsigned SwingSpeed(); // centimetres per second
std::string PhysicalInteractionsReport();
}
