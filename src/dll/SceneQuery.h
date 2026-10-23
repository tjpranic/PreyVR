#pragma once
#include "preyvr/WeaponAim.h"
#include "preyvr/SceneQuery.h"
#include <string>
#include <windows.h>
namespace preyvr::dll {
struct GameplayPoseFrame;
void QueryAimScene(const GameplayPoseFrame&,aim::Sample&);
void SetSceneReticle(unsigned enabled);
unsigned SceneReticleEnabled();
std::string SceneQueryReport();
// Input-drain thread only: one full-range ray for the debug overlay. Shares the
// query's gates and its process-wide fault latch; never changes the reticle.
bool QueryDebugRay(const GameplayPoseFrame&,Vec3 origin,Vec3 direction,scene::Hit&);
// Input-drain thread only: is the segment a->b clear? True when the query ran;
// then hit.distance<0 means clear. Shares the query's gates and fault latch.
bool QuerySegment(const GameplayPoseFrame&,Vec3 a,Vec3 b,scene::Hit&);
// Input-drain thread only: fixed scenery alone (scene::SceneryObjects) between a
// and b, skipping the player and `skip` (may be null). True when the query ran;
// then hit.distance<0 means clear. Shares the query's gates and fault latch.
bool QuerySceneryBetween(const GameplayPoseFrame&,Vec3 a,Vec3 b,void* skip,scene::Hit&);
// The same, asking for `objectTypes` (scene::SceneryAndLooseObjects, say).
bool QueryObjectsBetween(const GameplayPoseFrame&,Vec3 a,Vec3 b,void* skip,int objectTypes,scene::Hit&);
bool SceneQueryFaulted();
// Input-drain thread only: the physical world and the player's own physics,
// after the scene query's thread, player and vtable checks. ERROR_SUCCESS or why not.
DWORD SceneQueryWorld(const GameplayPoseFrame&,std::uintptr_t& world,void*& player);
}
