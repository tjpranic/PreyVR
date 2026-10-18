#pragma once
#include <cstddef>
#include <cstdint>
// The free-swing wrench: Prey's own wrench hit, called without its attack. Steam
// SHA 7d6e322f...11a7. See docs/FREE-SWING-WRENCH-SCOPE-2026-10-18.md ("Phase 0").
// Static contracts, checked by tools/re/verify_free_swing.py. The rows already
// hashed elsewhere stay there: the wrench's attack, WeaponHit, the component hit,
// GetHits, the hit vector and the impulse wrapper are PhysicalNative.h's; the
// player singleton is SceneQueryNative.h's; the broadphase and the per-entity
// collision are ContactNative.h's.
namespace preyvr::swing::native {
// The wrench component's hit (PhysicalNative.h ComponentHit), called directly:
//   RCX the component (weapon + ComponentOffset), XMM1 the swing angle in degrees,
//   R8 the weapon, XMM3 the damage scale, [rsp+0x20] the charged flag (a byte).
// It returns 0 (dodged, nothing hit, or a breakable surface), 1, or 2 (a living
// hostile). It reads no attack state: of the weapon only its entity (+0x40), its
// stats (+0x1A8) and one flag (+0x1A0, the critical test).
inline constexpr std::uintptr_t ComponentHit=0x13BF730;
inline constexpr std::size_t ComponentOffset=0x4C8;
// The component's fields the hit reads: the damage package per kind of hit, the
// fatigue a hit costs (times the damage scale), and the dodge flag. Prey's
// SetParamClip animation event sets the flag from the target (0x13C0160); the hit
// clears it and returns 0, hitting nothing.
inline constexpr std::size_t PackageNormal=0x00,PackageCritical=0x08,PackageCharged=0x10,PackageChargedCritical=0x18,
    FatiguePerHit=0x38,DodgeFlag=0x3F;
// The weapon's charge, as WeaponHit turns it into the hit's damage scale:
// 1 + bonus * (1 - (full - min(time, full)) / full), with the charged flag passed on.
inline constexpr std::size_t ChargeTime=0x524,ChargeFull=0x528,ChargeBonus=0x52C,ChargedFlag=0x531;
// The artificial collision each hit sends. The builder fills a CryEngine
// EventPhysCollision and, when `dispatch` is set, hands it to the physics world's
// AddDeferredEvent (world slot +0x2E8) as a collision (type 2). That queues it
// under pef_log_collisions (0x2000000) for the game's collision listeners: the
// hit's sound, particles and decal, and breaking glass. Its arguments:
//   (EventPhysCollision* event, material* hitMaterial, const Vec3* direction,
//    IEntity* weapon, IPhysicalEntity* player, float weaponMass, float radius,
//    bool dispatch, short weaponSurface)
// `direction` times CollisionSpeed is the event's velocity (vloc[0], +0x54); the
// impulse wrapper ends up along the same direction (it negates what it is given,
// and the hit gives the builder that negation). The free swing gives it the
// wrench's own motion.
inline constexpr std::uintptr_t CollisionEventBuilder=0x16AEF20,AddDeferredEvent=0xB9F370;
inline constexpr std::size_t DeferredEventSlot=0x2E8,EventVelocity=0x54;
inline constexpr int EventCollision=2;
inline constexpr unsigned LogCollisions=0x2000000;
inline constexpr float CollisionSpeed=100.f;
// Damage: the hit sends its package through the signal system, scaled by the
// damage scale times the stealth multiplier.
inline constexpr std::uintptr_t SendSignalPackage=0x1608860;
// Stamina: the player (SceneQueryNative.h's singleton, the frame's player)
// + PlayerStamina, then + StaminaPointer (the accessor 0x1587C20). Two floats at
// StaminaPair: equal means drained. StaminaAvailable is the check Prey's attack
// makes first; it returns false when drained and sends the HUD "staminaDrained".
// The hit charges fatigue through StaminaCharge.
inline constexpr std::size_t PlayerStamina=0x678,StaminaPointer=0x88,StaminaPair=0x10;
inline constexpr std::uintptr_t StaminaAvailable=0x12618E0,StaminaCharge=0x1260D10;
// Surface types, for the log: what each struck surface is called and whether it
// breaks. C3DEngine::GetMaterialManager (0x20A540) returns the global
// MaterialManager; its CMatMan (vtable MatManVtable) keeps the surface type
// manager at +MatManSurfaceTypes (vtable SurfaceTypeManagerVtable), whose
// GetSurfaceType(id) answers ids 1..254 (anything else: mat_default). A
// CMaterialSurfaceType (SurfaceTypeVtable) holds its name's characters at +0x08,
// its flags at +0x1C and its breakability (> 0: glass and the like) at +0x68.
inline constexpr std::uintptr_t MaterialManager=0x243A6D0,MatManVtable=0x1C9D0B8,SurfaceTypeManagerVtable=0x1C9D878,
    SurfaceTypeVtable=0x1C9D8B8,GetSurfaceType=0x24E270;
inline constexpr std::size_t MatManSurfaceTypes=0xE0,SurfaceTypeName=0x08,SurfaceTypeFlags=0x1C,SurfaceTypeBreakability=0x68;
// The broadphase classes Prey's own melee asks for: static, sleeping and awake
// rigid, independent and terrain (ContactNative.h's movable classes are 0x6).
inline constexpr int StrikeObjects=0x117;
// What a struck collider is, for the log. Its IPhysicalEntity's foreign-data kind
// (+0x70: 1 scenery, 2 an entity, as the hit's material lookup and the collision
// builder read it). The entity behind it: the entity system (the global the hit
// reads) +0xE0 GetEntityFromPhysics; an IEntity's id +0x08, class +0x18 (the
// class's name +0x10) and name +0x78, as two native messages print them
// ("[LoadGame] Entity class mismatch ID=%d %s ..." and "... on entity %s (class %s)").
inline constexpr std::uintptr_t EntitySystem=0x224DA38;
// The player's own entity id: every native hit names its shooter so, and the
// player singleton (SceneQueryNative.h) fetches actor 0x7777. Its character's
// skeleton is an articulated body with a host, in the independent class (the
// PreyVR-36 run met it every frame as class ArkPlayer): the wrench skips it.
inline constexpr unsigned PlayerEntityId=0x7777;
inline constexpr std::size_t ForeignKindSlot=0x70,EntityFromPhysicsSlot=0xE0,EntityIdSlot=0x08,EntityClassSlot=0x18,
    ClassNameSlot=0x10,EntityNameSlot=0x78;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0x16AEF20,415,0x65A76EE8C172DA9Eull}, // CollisionEventBuilder
    {0xB9F370,2216,0x1C5E175225C4C054ull}, // AddDeferredEvent
    {0x1608860,609,0xA3429539B0416F24ull}, // SendSignalPackage
    {0x12618E0,67,0x831BC3C524B6DFE1ull}, // StaminaAvailable
    {0x1260D10,883,0xAF8F9157D603D559ull}, // StaminaCharge
    {0x24E270,115,0xBD3404C9870A68B5ull}, // GetSurfaceType
};
}
