#pragma once
#include <cstddef>
#include <cstdint>
// How Prey places the lights on the player's eye each frame: the flashlight's
// ArkLight entity rides the `flashlight` bone attachment on camera_jnt, the
// psychoscope's the `psychoscope_light` one. Steam SHA 7d6e322f...11a7. See
// docs/FLASHLIGHT-HEAD-SCOPE-2026-10-29.md. Static contracts, checked by
// tools/re/verify_head_lamp.py.
namespace preyvr::headlamp::native {
// CEntityAttachment::ProcessAttachment (RCX the binding, RDX the attachment):
// the attachment's world placement (its vtable +0x70) onto the bound entity, by
// SetPosRotScale (entity vtable +0x138) with the binding's scale times the
// placement's and flags 0x10. Only the binding's vtable (+0x10) holds it, and
// the bone update calls that right after each attachment's model transform.
// Hooked: the player's lights go on the headset's eye.
inline constexpr std::uintptr_t ProcessAttachment=0x335010;
inline constexpr std::uintptr_t BindingVtable=0x1CB13B0;
inline constexpr std::size_t VtableProcessAttachment=0x10;
// Of the binding: the entity id (+0x08) and its scale (+0x0C, a Vec3).
inline constexpr std::size_t BindingEntity=0x08,BindingScale=0x0C;
// The bone attachment. Its world placement (vtable +0x70) is the character's
// location (+0xA90, a QuatTS: q xyzw, t, s) times its model transform (+0x130).
inline constexpr std::uintptr_t BoneVtable=0x1D212B8,BoneWorldAbsolute=0x7A2250;
inline constexpr std::size_t VtableWorldAbsolute=0x70;
// Its name (+0x10, a char*), its manager (+0x28, whose +0x18 is the character),
// its relative default (+0xF8, a QuatT: q xyzw, then t), K (+0x14C, xyzw) and
// its joint (+0x15C).
inline constexpr std::size_t AttachName=0x10,AttachManager=0x28,ManagerCharacter=0x18,AttachRelative=0xF8,
    AttachK=0x14C,AttachJoint=0x15C;
// IEntity::SetPosRotScale(const Vec3*, const Quat*, const Vec3*, int flags).
inline constexpr std::size_t EntitySetPosRotScale=0x138;
inline constexpr int PlacementFlags=0x10;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0x335010,232,0x60972B014C823893ull}, // EntityProcessAttachment
    {0x7A2250,670,0x0C25B8DA509182F5ull}, // BoneWorldAbsolute
    {0x7A3390,1089,0x8C0110153A04A3AEull}, // BoneUpdateExecute
};
}
