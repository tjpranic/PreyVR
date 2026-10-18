#pragma once
#include "preyvr/AnimIk.h"
#include "preyvr/WeaponRigAlignment.h"
#include <array>
#include <cstdint>
#include <string>
// The weapon box (preyvr/PhysicalInteractions.h): the drawn weapon's bounds where
// the hand holds it, for the contact lane (ContactLane.h) and the free-swing
// wrench (SwingLane.h).
namespace preyvr::dll {
struct GameplayPoseFrame;
// The animation takeover publishes the right hand's weapon each frame, while a
// lane wants it.
void PublishPhysicalWeapon(const GameplayPoseFrame&,const RigIdentity&,const animik::Location&,
                           Pose wristModel,const weaponrig::ContactGeometry&);
// The weapon box is wanted: by physical contact (ContactLane.h), or by the
// free-swing wrench (SwingLane.h).
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
}
