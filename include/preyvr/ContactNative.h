#pragma once
#include <cstddef>
#include <cstdint>
// Physical contact: pushing objects with the hands and weapons. Steam SHA
// 7d6e322f...11a7. See docs/PHYSICAL-CONTACT-SCOPE-2026-10-18.md ("Phase 0").
// Static contracts only, checked by tools/re/verify_physical_contact.py:
// nothing calls these yet. The world pointer, its vtable and the player skip are
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
};
}
