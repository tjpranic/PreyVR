#pragma once
#include <cstddef>
#include <cstdint>
// Physical contact: pushing objects with the hands and weapons. Steam SHA
// 7d6e322f...11a7. See docs/PHYSICAL-CONTACT-SCOPE-2026-10-18.md ("Phase 0").
// Static contracts, checked by tools/re/verify_physical_contact.py and gated by
// ContactLane.cpp. The world pointer, its vtable and the player skip are
// SceneQueryNative.h's; the rigid Action and GetType rows are PhysicalNative.h's.
namespace preyvr::contact::native {
// IPhysicalWorld (vtable 0x1D82E70) slots and the bodies they hold.
//   GetEntitiesInBox(world, Vec3* min, Vec3* max, IPhysicalEntity*** list, int objtypes,
//     int prealloc) -> count. Takes the world lock, caller index 4. Copies into *list
//     when prealloc >= count; otherwise points *list at a shared per-caller buffer.
//   CollideEntityWithPrimitive(world, entity, int itype, primitive*, Vec3* sweep,
//     ray_hit* out, intersection_params* ip or null) -> 1 when out->dist < 1e9.
//     Null ip: a zero sweep is a static overlap test. Writes the primitive's centre
//     back after zeroing it: the primitive must be the caller's own, per call.
//   GetSurfaceParameters(world, int surface, float* bounciness, float* friction,
//     unsigned* flags) -> 0 for surface >= 512.
inline constexpr std::uintptr_t EntitiesInBoxSlot=0x110,CollidePrimitiveSlot=0x1D0,SurfaceParametersSlot=0xD0;
inline constexpr std::uintptr_t GetEntitiesInBox=0xBA8560,CollideEntityWithPrimitive=0xBA1D80,GetSurfaceParameters=0xBA9810;
// World surface tables, float[512] each, by ray_hit surface index (+0x18).
// The contact solver's friction is max(0, a + b) * 0.5 (0xBE5FD0); dynamic
// friction is friction * 2/3 (SetSurfaceParameters).
inline constexpr std::size_t BouncinessTable=0x10F0,FrictionTable=0x18F0,DynamicFrictionTable=0x20F0;
inline constexpr unsigned SurfaceCount=512;
// IPhysicalEntity slots, and the two classes contact may move.
inline constexpr std::size_t TypeSlot=0x08,StatusSlot=0x30,ActionSlot=0x38;
inline constexpr std::uintptr_t RigidVtable=0x1D83870,RigidGetStatus=0xBE2BB0;
inline constexpr std::uintptr_t ArticulatedVtable=0x1D803E8,ArticulatedGetType=0xB2D060,
    ArticulatedGetStatus=0xB2C660,ArticulatedAction=0xB1CF30;
// CRigidEntity fields the rigid Action reads for an impulse (PhysicalNative.h
// witnesses): inverse mass, and the world inverse inertia (3x3, row-major).
inline constexpr std::size_t RigidInverseMass=0x2E8,RigidInverseInertia=0x328;
// A rigid body as both classes lay it out: the rigid entity's at +0x298, each
// articulated joint's at joint +0x1B0. Centre of mass, w, v, mass, inverse mass,
// world inverse inertia.
inline constexpr std::size_t RigidBody=0x298,BodyCentre=0x00,BodyW=0x34,BodyV=0x40,BodyMass=0x4C,
    BodyInverseMass=0x50,BodyInverseInertia=0x90;
static_assert(RigidBody+BodyInverseMass==RigidInverseMass&&RigidBody+BodyInverseInertia==RigidInverseInertia);
// Phase 4, corpses: CArticulatedEntity's parts, as its Action (set velocity per
// part), GetStatus (dynamics per part) and GetParams (articulated body) read them.
//   parts  +0x120, count +0x128, stride 0x88: part id +0x48 (ray_hit partid)
//   infos  +0x458, one per part, stride 0x94: the part's joint +0x54
//   joints +0x460, count +0x468, stride 0x330: the joint's body +0x1B0
//   host   +0x4F8: what a live character's skeleton rides on; null for a ragdoll
// Set velocity by partid writes that joint body's v, w and momenta and wakes the
// entity; the step (+0x100) starts from them (its joint pass copies body v to the
// joint's +0x88, which StepBack restores and per-part GetStatus reports).
inline constexpr std::size_t ArticulatedParts=0x120,ArticulatedPartCount=0x128,PartStride=0x88,PartId=0x48;
inline constexpr std::size_t ArticulatedInfos=0x458,InfoStride=0x94,InfoJoint=0x54;
inline constexpr std::size_t ArticulatedJoints=0x460,ArticulatedJointCount=0x468,JointStride=0x330,JointBody=0x1B0;
inline constexpr std::size_t ArticulatedHost=0x4F8;
inline constexpr std::uintptr_t ArticulatedGetParams=0xB2AA00,ArticulatedStep=0xB37960,
    ArticulatedStepJoints=0xB3C000,ArticulatedStepBack=0xB398A0;
inline constexpr std::size_t StepSlot=0x100,StepBackSlot=0xD8,ParamsSlot=0x28;
// Phase 5, what the player stands on: CLivingEntity (vtable 0x1D81580, its Action
// names "CLivingEntity:Action(action_move)"). Its GetStatus asks the base GetStatus
// first, then for pe_status_living (type 2) copies out, under its read lock
// (+0x348): bFlying +0x04 (flags +0x2C0 bits 0 and 7; not used: standing on a
// light object it flickers on) and the ground collider +0x70 (from +0x2C8, the
// same pointer that adds the ground's velocity to vel). Prey's struct is not the
// SDK's; it ends by +0xA0.
// The ground collider is not kept while the player stands still (the PreyVR-28
// run: 5 frames, then null, on the same object). So the lane also tests each
// object it would push against the feet: the living body's position (+0xA8, the
// entity's m_pos) less the pivot height, plus camOffset (+0x0C), the ground
// point pe_status_pos itself reports once active.
// camOffset is q * (0, 0, -(+0x1D0)), a stance-smoothing offset (0 at rest):
// the pivot is pe_player_dimensions' heightPivot. Its GetParams (type 1, after the
// base GetParams) copies heightPivot +0x04 (from +0x1C0), sizeCollider +0x0C
// (+0x1C4: x is the radius), heightCollider +0x18 and bUseCapsule +0x34. So the
// feet are +0xA8 - heightPivot + camOffset, and the samples reach the collider's radius.
inline constexpr std::uintptr_t LivingVtable=0x1D81580,LivingGetStatus=0xB475D0,StaticGetStatus=0xB83040,
    LivingGetParams=0xB46AA0,StaticGetParams=0xB81A20;
inline constexpr int StatusLiving=2,ParamsPlayerDimensions=1;
inline constexpr std::size_t LivingFlying=0x04,LivingCamOffset=0x0C,LivingGroundCollider=0x70,LivingStatusSize=0xA0;
inline constexpr std::size_t DimensionsPivot=0x04,DimensionsSize=0x0C,DimensionsBytes=0x40;
inline constexpr std::size_t EntityPosition=0xA8;
// pe_type values from GetType, and the broadphase classes contact asks for:
// ent_sleeping_rigid | ent_rigid (no static, terrain or living).
inline constexpr int TypeStatic=1,TypeRigid=2,TypeLiving=4,TypeArticulated=6;
inline constexpr int MovableObjects=0x6;
// primitives itype values CollideEntityWithPrimitive accepts. The box is
// Matrix33 Basis +0x00, int bOriented +0x24, centre +0x28, half-sizes +0x34.
inline constexpr int PrimitiveBox=0,PrimitiveSphere=4,PrimitiveCylinder=5,PrimitiveCapsule=6;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0xBA8560,106,0x9AF37E93B2FCEC90ull}, // GetEntitiesInBox
    {0xBA6830,7470,0x8F7C61BDDC1DF353ull}, // EntitiesInBoxGrid
    {0xBA1D80,4002,0xC557C354AD9C05BCull}, // CollideEntityWithPrimitive
    {0xC5F080,78,0x0998817E8DC33946ull}, // BoxGeometryFromPrimitive
    {0xBA9810,60,0xCD55A4284760D733ull}, // GetSurfaceParameters
    {0xBB2A10,118,0x0C98C985C25CA910ull}, // SetSurfaceParameters
    {0xBE5FD0,1371,0x1A6B58A0A8DB46B8ull}, // RigidContactFriction
    {0xBE2BB0,2698,0x75DED2772C70D546ull}, // RigidGetStatus
    {0xB2D060,6,0xF24AE2DAC3164C16ull}, // ArticulatedGetType
    {0xB2C660,2549,0xE0892703B0B4A82Eull}, // ArticulatedGetStatus
    {0xB1CF30,11812,0x5200C0DF7636F70Full}, // ArticulatedAction
    {0xB2AA00,2071,0x9CB31380F5DE393Aull}, // ArticulatedGetParams
    {0xB37960,7999,0xBC06FCF0ADBEDD9Cull}, // ArticulatedStep
    {0xB3C000,11555,0x07D88D23F14A7FCDull}, // ArticulatedStepJoints
    {0xB398A0,3412,0xBF523F2E56DEAA04ull}, // ArticulatedStepBack
    {0xB475D0,2231,0x98C63299E7FFE8DAull}, // LivingGetStatus
    {0xB83040,2829,0x21595B9C643FBC02ull}, // StaticGetStatus
    {0xB46AA0,752,0x5DB5708AFB281271ull}, // LivingGetParams
    {0xB81A20,2712,0x8CDBD2B6578FBC46ull}, // StaticGetParams
};
}
