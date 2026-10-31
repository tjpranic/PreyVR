#pragma once
#include <cstddef>
#include <cstdint>
// Every weapon comes out without its draw animation. Steam SHA 7d6e322f...11a7.
// See docs/WEAPON-DRAW-SCOPE-2026-10-31.md and docs/WRENCH-DRAW-ANIMATION-2026-10-20.md.
// Static contracts, checked by tools/re/verify_weapon_draw.py. The rows already hashed
// elsewhere stay there: SelectItem, the weapon lookup and the player are
// EquipmentNative.h's.
namespace preyvr::weapondraw::native {
// The weapon's PlayAction (RCX the weapon, RDX IAction**, XMM2 a float it writes to
// the action's +0x54). It queues the action on the weapon's action controller
// (vtable +0x90) unless the action's fragment (+0x38) is -1, then drops the
// caller's reference: --refcount (+0x50), and at <= 0 the action's vtable +0xD0.
// Hooked: a weapon's select action is not queued.
inline constexpr std::uintptr_t PlayAction=0x169A5B0;
// The shared OnEquip. Every class's own vtable +8 runs it first. Its tail builds the
// select action and plays it, for the player's weapons only: the fragment Wpn_Select
// (fragment table +0xC4) with the select flag set, or on a first pickup Wpn_Pickup
// (+0xCC) with it clear. The table is weapon+0x470; 0x1637940 fills it by name.
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
// player's attacking byte +0x992 = 0 when this is the current weapon, +0x48B = 1).
// Other actions' Exits call +0x108 as well: it is "ready", not only the select's end.
inline constexpr std::uintptr_t SelectEnter=0x169D8D0,SelectExit=0x169DBD0;
inline constexpr std::size_t EnterSlot=0x10,ExitSlot=0x20;
inline constexpr std::uintptr_t SelectBegun=0x1692E60,SelectDone=0x1692E70,SelectFinished=0x1692F50;
inline constexpr std::size_t SelectBegunSlot=0x100,SelectDoneSlot=0x108,SelectFinishedSlot=0x110;
// The weapon's item id, as OnEquip hands it to the select action.
inline constexpr std::size_t WeaponItemId=0x38;
// EquipmentNative.h's rows. SelectItem calls the new weapon's OnEquip BEFORE it
// stores the weapon as current (component +0x58), so SelectDone cannot clear
// +0x992 from inside OnEquip: the finish waits a frame. WeaponLookup(id) returns
// the weapon, or null. GetPlayer returns the player, whose equipment component is at
// PlayerEquipment.
inline constexpr std::uintptr_t SelectItem=0x12748D0,WeaponLookup=0x16A5650,GetPlayer=0x157C990;
inline constexpr std::size_t PlayerEquipment=0x14B8,EquipmentCurrent=0x58;
// The stow. Every class's own OnUnequip (vtable +0x10; RCX the weapon, DL animate)
// reaches the shared one with animate as it came. Hooked. The shared OnUnequip does
// nothing unless the weapon is the current one. With a fragment table and no attack
// under way (+0x2C9 clear) it either plays the stow action (StowActionCtor, vtable
// StowActionVtable, fragment Wpn_Unselect +0xC8) when animating, or, given the hand
// attachment (+0x2B0), stows at once: Stow (detach from the hand, the equipment's
// current id 0, last = this, unequip-in-progress cleared) and the HUD's "empty". The
// stow action's Exit (StowExit) does the same Stow and "empty". With an attack under
// way (StartAttack, vtable +0xE8, sets +0x2C9 and StopAttack, +0xF8, clears it) it
// plays a closing fragment, then the stow; that case is left to Prey.
inline constexpr std::uintptr_t OnUnequip=0x169A000,Stow=0x16924E0,StowActionCtor=0x169D790,StowActionVtable=0x1E6D770,
    StowExit=0x169DCA0;
inline constexpr std::size_t OnUnequipSlot=0x10,WeaponAttacking=0x2C9,WeaponHandAttachment=0x2B0,WeaponFragments=0x470;
// What differs between the weapon classes: the slots the skip calls, and each class's
// own OnUnequip. Prey registers them by name in 0x17327F0. The Q-Beam's finish clears
// the +0x584 its OnEquip set, which blocks firing and reloading until then, so a finish
// must go through the slot. The Disruptor never goes busy. The grenade's done shows the
// grenade, or unequips it when none are left.
inline constexpr std::uintptr_t NoOp=0x1706520,QBeamFinished=0x16A07A0,DisruptorDone=0x13B9E80,GrenadeDone=0x16A5300;
struct WeaponClass {std::uintptr_t vtable,unequip,begun,done,finished;};
inline constexpr WeaponClass WeaponClasses[]={
    {0x1E6C640,0x169A000,SelectBegun,SelectDone,SelectFinished}, // ArkWeapon
    {0x1E92F00,0x17392F0,SelectBegun,SelectDone,SelectFinished}, // ArkWeaponWrench
    {0x1E92A08,0x17392D0,SelectBegun,SelectDone,SelectFinished}, // ArkWeaponDoubleWrench
    {0x1E6F780,0x16AAB20,SelectBegun,SelectDone,SelectFinished}, // ArkWeaponPistol, Shotgun, ToyGun
    {0x1E6E390,0x169F6C0,SelectBegun,SelectDone,SelectFinished}, // ArkWeaponGooGun
    {0x1E6E9F0,0x16A3530,SelectBegun,SelectDone,QBeamFinished}, // ArkWeaponInstalaser
    {0x1E31290,0x13BC2B0,NoOp,DisruptorDone,SelectFinished}, // ArkWeaponStunGun
    {0x1E6F148,0x16A5EA0,SelectBegun,GrenadeDone,SelectFinished}, // the four grenades
    {0x1E308D0,0x13B7B60,SelectBegun,SelectDone,SelectFinished}, // ArkWeaponDiscRifle
};
// A weapon's entity (+0x40, an IEntity), and its class name as Prey's own metrics read
// it (0x13B7DC0): IEntity +0x18 GetClass, then IEntityClass +0x10 GetName. Pistol,
// Shotgun and ToyGun share a vtable, so only this tells them apart.
inline constexpr std::size_t WeaponEntity=0x40,EntityClassSlot=0x18,ClassNameSlot=0x10;
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
    {0x169A000,820,0xE407BBB6E0A05CCAull}, // OnUnequip
    {0x16924E0,1119,0x90A8CB2491B62AD1ull}, // Stow
    {0x169D790,39,0xF10FE024214D38FFull}, // StowActionCtor
    {0x169DCA0,124,0x765681AFC703F9ABull}, // StowExit
    {0x1706520,3,0x69BC481C23DE2D4Dull}, // NoOp
    {0x16A07A0,12,0x96922667EAD302B7ull}, // QBeamFinished
    {0x13B9E80,34,0xF83240510F59AC3Aull}, // DisruptorDone
    {0x16A5300,276,0x154624BF33B4A6FBull}, // GrenadeDone
};
}
