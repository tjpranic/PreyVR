#pragma once
#include <cstddef>
#include <cstdint>
// The zero-G helmet: the `helmet` bone attachment on the player's eye joint.
// Steam SHA 7d6e322f...11a7. See docs/ZERO-G-SCOPE-2026-10-30.md ("The zero-G
// helmet"). Static contracts, checked by tools/re/verify_helmet.py and gated by
// HelmetLane.cpp. The bone attachment's vtable, name, owner, relative default, K
// and joint are HeadLampNative.h's.
namespace preyvr::helmet::native {
// GetAttModelRelative (vtable +0x68) returns the model transform at +0x130, a
// QuatT (q xyzw, then t): the joint times the relative default, the
// orientation times K (the bone update, 0x7A3390). The render draws it where
// the character is.
inline constexpr std::size_t VtableModelRelative=0x68,AttachModel=0x130;
// SetAttRelativeDefault (vtable +0x58) stores its QuatT at +0xF8. The HUD
// helmet's update (0x1327070, ArkPlayer +0x1468) writes the helmet's sway
// there every frame: a rotation, no offset.
inline constexpr std::size_t VtableRelativeDefault=0x58;
// HideAttachment (vtable +0x88) sets or clears these bits of the flags (+0x08):
// the main pass, the shadow pass and the attached object. Prey's helmet deploy
// (0x1326690) clears them; the main-pass bit alone says whether it is drawn.
inline constexpr std::size_t VtableHide=0x88,AttachFlags=0x08;
inline constexpr std::uint32_t HideBits=0x70000,HideMainPass=0x10000;
// The player's entity (ArkPlayer +0x38) is a CEntity (vtable 0x1D30BD0), whose
// GetWorldTM (+0xE8) returns its world matrix at +0x60: a Matrix34, row-major,
// the translation in the last column. Its character is drawn there.
inline constexpr std::size_t PlayerEntity=0x38,EntityWorldTmSlot=0xE8,EntityWorldTm=0x60;
inline constexpr std::uintptr_t EntityVtable=0x1D30BD0;
// The helmet is a character attached to the bone attachment: its object
// (+0x20, which the attachment draw 0x821FD0 calls at vtable +0x18) is a
// CCHRAttachment (vtable 0x1CB1328) holding the character at +0x08. Its
// RenderAttachment (0x334DF0) calls the character's Render (CCharInstance
// vtable 0x1D22200 +0xC0, 0x81BCB0) with an identity QuatTS offset (q xyzw, t,
// s: eight floats), and Render scales the character by that s. Hooked: the
// helmet's character is drawn with the Helmet Size scale.
inline constexpr std::size_t AttachObject=0x20,ObjectCharacter=0x08,VtableRenderAttachment=0x18,VtableRender=0xC0,
    OffsetFloats=8,OffsetScale=7;
inline constexpr std::uintptr_t ChrAttachmentVtable=0x1CB1328,CharacterVtable=0x1D22200;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr std::uintptr_t ModelRelative=0x822C80,RelativeDefault=0x828DC0,Hide=0x7B2B40,
    EntityGetWorldTm=0x10EA260,CharacterRender=0x81BCB0,RenderAttachment=0x334DF0;
inline constexpr Contract Contracts[]={
    {0x822C80,8,0x7F88A32F2C8797C3ull}, // GetAttModelRelative
    {0x828DC0,58,0x6E46B83CD1E333A7ull}, // SetAttRelativeDefault
    {0x7B2B40,20,0xD8E753775F70610Eull}, // HideAttachment
    {0x10EA260,5,0x86A7F411C046A30Cull}, // EntityGetWorldTM
    {0x81BCB0,2676,0x4F14A20688506B3Full}, // CharacterRender
    {0x334DF0,202,0x3A9FF4E1344F809Bull}, // ChrAttachmentRender
};
}
