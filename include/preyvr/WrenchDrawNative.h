#pragma once
#include <cstddef>
#include <cstdint>
// The wrench comes out without its draw animation. Steam SHA 7d6e322f...11a7.
// See docs/WRENCH-DRAW-ANIMATION-2026-10-20.md. Static contracts, checked by
// tools/re/verify_wrench_draw.py. The rows already hashed elsewhere stay there:
// SelectItem and the weapon lookup are EquipmentNative.h's (pinned by a receipt:
// read, never edited), the wrench's vtable is PhysicalNative.h's.
namespace preyvr::wrenchdraw::native {
// The weapon's PlayAction (RCX the weapon, RDX IAction**, XMM2 a float it writes to
// the action's +0x54). It queues the action on the weapon's action controller
// (vtable +0x90) unless the action's fragment (+0x38) is -1, then drops the
// caller's reference: --refcount (+0x50), and at <= 0 the action's vtable +0xD0.
// Hooked: the wrench's select action is not queued.
inline constexpr std::uintptr_t PlayAction=0x169A5B0;
// The shared OnEquip (weapon vtable +8; the wrench's too). Its tail builds the
// select action and plays it: the fragment Wpn_Select (fragment table +0xC4) with
// the select flag set, or on a first pickup Wpn_Pickup (+0xCC) with it clear. The
// table is weapon+0x470; 0x1637940 fills it by name.
inline constexpr std::uintptr_t OnEquip=0x1699800;
// The select action: 0x90 bytes, built only by OnEquip (SelectActionCtor over the
// action base ActionBaseCtor). Its weapon's item id at +0x80, the select flag at
// +0x88, the refcount at +0x50 and the fragment at +0x38.
inline constexpr std::uintptr_t SelectActionCtor=0x169D6F0,ActionBaseCtor=0x169D420,SelectActionVtable=0x1E6D698;
inline constexpr std::size_t ActionFragment=0x38,ActionRefCount=0x50,ActionItemId=0x80,ActionSelectFlag=0x88,
    ActionReleaseSlot=0xD0;
// Its Enter (vtable +0x10) and Exit (+0x20). Each finds the weapon by the item id
// (WeaponLookup). Enter, a select only: the weapon's +0x100 (SelectBegun: the
// weapon's busy byte +0x48B = 0). Exit, always: the weapon's +0x110
// (SelectFinished: a first pickup's bookkeeping, then +0x108, SelectDone: the
// player's switch-in-progress byte +0x992 = 0 when this is the current weapon,
// +0x48B = 1).
inline constexpr std::uintptr_t SelectEnter=0x169D8D0,SelectExit=0x169DBD0;
inline constexpr std::size_t EnterSlot=0x10,ExitSlot=0x20;
inline constexpr std::uintptr_t SelectBegun=0x1692E60,SelectDone=0x1692E70,SelectFinished=0x1692F50;
inline constexpr std::size_t SelectBegunSlot=0x100,SelectDoneSlot=0x108,SelectFinishedSlot=0x110;
// The weapon's item id, as OnEquip hands it to the select action.
inline constexpr std::size_t WeaponItemId=0x38;
// EquipmentNative.h's rows. SelectItem calls the new weapon's OnEquip BEFORE it
// stores the weapon as current (component +0x58), so SelectDone cannot clear
// +0x992 from inside OnEquip: the finish waits a frame. WeaponLookup(id) returns
// the weapon, or null.
inline constexpr std::uintptr_t SelectItem=0x12748D0,WeaponLookup=0x16A5650;
// The stow. The wrench's OnUnequip (its vtable +0x10, WrenchUnequip; RCX the weapon,
// DL animate) cancels its charge (+0x90), calls the shared OnUnequip with the same
// animate, then clears two fields. The shared OnUnequip, with a fragment table and
// the weapon not lowered (+0x2C9 clear), either plays the stow action (StowActionCtor,
// vtable StowActionVtable, fragment Wpn_Unselect +0xC8) when animating, or stows at
// once: Stow (detach from the hand, the equipment's current id 0, last = this,
// unequip-in-progress cleared) and the HUD's "empty". The stow action's Exit
// (StowExit) does the same Stow and "empty". Lowered (+0x2C9 set) it plays a lower
// then the stow, whose Exit also calls SelectDone; that case is left to Prey.
inline constexpr std::uintptr_t WrenchUnequip=0x17392F0,OnUnequip=0x169A000,Stow=0x16924E0,StowActionCtor=0x169D790,
    StowActionVtable=0x1E6D770,StowExit=0x169DCA0;
inline constexpr std::size_t OnUnequipSlot=0x10,WeaponLowered=0x2C9,WeaponFragments=0x470;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0x169A5B0,117,0x60BB84B985B68E0Aull}, // PlayAction
    {0x1699800,1206,0x08BFA6E85129C5D2ull}, // OnEquip
    {0x169D6F0,59,0xAC34A3E7E16EBE5Bull}, // SelectActionCtor
    {0x169D420,165,0x3178CE3E13703CB5ull}, // ActionBaseCtor
    {0x169D8D0,66,0x11A7DE190E524987ull}, // SelectEnter
    {0x169DBD0,54,0x4E9F593F581F68D2ull}, // SelectExit
    {0x1692E60,8,0x67556257525E534Aull}, // SelectBegun
    {0x1692E70,212,0xE28BC7286CCAFB43ull}, // SelectDone
    {0x1692F50,327,0xEFCF865877E1B7D4ull}, // SelectFinished
    {0x17392F0,61,0xCE18ECF24409270Full}, // WrenchUnequip
    {0x169A000,820,0xE407BBB6E0A05CCAull}, // OnUnequip
    {0x16924E0,1119,0x90A8CB2491B62AD1ull}, // Stow
    {0x169D790,39,0xF10FE024214D38FFull}, // StowActionCtor
    {0x169DCA0,124,0x765681AFC703F9ABull}, // StowExit
};
}
