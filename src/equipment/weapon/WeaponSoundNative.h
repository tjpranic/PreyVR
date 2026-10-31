#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
// A weapon draw's sound: what plays it, so the skip can play it again. Steam SHA 7d6e322f...11a7. See docs/WEAPON-DRAW-SCOPE-2026-10-31.md and
// docs/WRENCH-DRAW-ANIMATION-2026-10-20.md ("The equip sound"). Static contracts,
// checked by tools/re/verify_weapon_draw.py.
namespace preyvr::weaponsound::native {
// Mannequin's PlaySound procedural clip, OnEnter (RCX the clip, XMM1 blend time,
// XMM2 duration, R9 its params). It resolves the start trigger by name
// (TriggerIdByName, through the audio system) and executes it on its context's
// entity audio proxy (ProxyExecuteTrigger's slot). The clip's entity (IEntity*) is
// at +0x08. Params: start trigger name +0x08 and stop trigger name +0x10 (char*,
// may be null or empty), attachment joint CRC +0x18, fire-and-forget +0x20 (else
// OnExit stops the start trigger), sound flags +0x28 (1 first person only, 2 third).
inline constexpr std::uintptr_t PlaySoundEnter=0x3AE460;
inline constexpr std::size_t ClipEntity=0x08,StartTrigger=0x08,StopTrigger=0x10,AttachmentJoint=0x18,FireAndForget=0x20,
    SoundFlags=0x28;
// gEnv->pAudioSystem (gEnv 0x224D980 + 0xD0), a CAudioSystem (vtable AudioSystemVtable).
// Its GetAudioTriggerID (+0x40, GetAudioTriggerId: this+0x2E8, then TriggerIdByName)
// is bool(name, unsigned* id). The id is ~CRC32 (the standard table CrcTable) of
// the lowercased name, written only when the trigger exists ("Unknown trigger name"
// otherwise, id 0).
inline constexpr std::uintptr_t AudioSystemGlobal=0x224DA50,AudioSystemVtable=0x1D8EE30,GetAudioTriggerId=0xD72300,
    TriggerIdByName=0xD59F40,CrcTable=0x1C8AE40;
inline constexpr std::size_t TriggerIdSlot=0x40;
// CEntityAudioProxy (vtable EntityAudioProxyVtable): ExecuteTrigger (+0xD8)
// bool(id, lipSync, proxyLocalId, callbackInfo*), StopTrigger (+0xE0). Its entity
// (IEntity*) at +0x40; an IEntity's id is its vtable +0x08. Prey's own Lua
// ExecuteAudioTrigger (0x940B90) takes an entity's audio proxy with IEntity +0x210
// (type 3, created if missing) and calls +0xD8.
inline constexpr std::uintptr_t ProxyExecuteTrigger=0x911F40,EntityAudioProxyVtable=0x1D31A30;
inline constexpr std::size_t ExecuteTriggerSlot=0xD8,StopTriggerSlot=0xE0,ProxyEntity=0x40,EntityIdSlot=0x08;
// The replay's way to the player's audio proxy, as Prey's own code goes: the entity
// system (gEnv+0xB8) +0x70 GetEntity(id) (OnEquip's attachment lookup, 0x169B473),
// then IEntity +0x200 GetProxy(type) with the audio type 3 (the Lua StopAudioTrigger,
// 0x960FC9). The trigger plays on the proxy's default local id 1 with empty callback
// info ({0, 0, 0, flags 1}), as the PlaySound clip plays it (0x3AEBE0).
inline constexpr std::uintptr_t EntitySystem=0x224DA38;
inline constexpr std::size_t GetEntitySlot=0x70,GetProxySlot=0x200;
inline constexpr int AudioProxyType=3;
inline constexpr std::uint32_t PlayerEntityId=0x7777,DefaultProxyId=1,CallbackFlags=1;
// A trigger by name, and the id the audio system must give it (~CRC32 of the
// lowercased name).
struct Trigger {const char* name;std::uint32_t id;};
// What each weapon's draw and stow play (the capture, run player-20261031-211309, 78
// windows over eleven classes). A draw's fragment plays two PlaySound clips on the
// player 10-16 ms in, no joint, sound flags 0, open-ended: the weapon's own trigger
// and Play_switch_weapon (the Disruptor has no trigger of its own). A stow's plays
// Play_Player_Unequip as it starts (300 ms blend). Their stop triggers are none or
// "null" (0x25CBFC4F, which stops nothing): fire and forget in effect.
inline constexpr Trigger SwitchWeapon{"Play_switch_weapon",0x364DA6FE},Unequip{"Play_Player_Unequip",0xE06723D1};
inline constexpr Trigger ArmWrench{"Play_Player_Arm_Wrench",0x446C5BFD},ArmInstalaser{"Play_Player_Arm_Instalaser",0x4C554451},
    EquipPistol{"Play_WPN_Pistol_Equip",0xEAA21D0A},EquipToyGun{"Play_WPN_NarfGun_Equip",0x2FE7D01F},
    PickupShotgun{"Play_Pickup_Shotgun",0x258CF3AD},PickupGooGun{"Play_Pickup_GooGun",0x2ADD4571},
    SelectEmp{"Play_Grenade_EMP_Select",0xC29E38B3},SelectNullwave{"Play_Nullwave_Select",0xA5C9FDBE},
    EquipRecycler{"Play_Recycler_Grenade_Equip",0x66DC62FD},UnholsterLure{"Play_WPN_Grenade_Lure_Unholster",0xB1D6200F};
inline constexpr Trigger WrenchDraw[]={ArmWrench,SwitchWeapon},QBeamDraw[]={ArmInstalaser,SwitchWeapon},
    ToyGunDraw[]={EquipToyGun,SwitchWeapon},ShotgunDraw[]={PickupShotgun,SwitchWeapon},GlooDraw[]={PickupGooGun,SwitchWeapon},
    EmpDraw[]={SelectEmp,SwitchWeapon},NullwaveDraw[]={SelectNullwave,SwitchWeapon},RecyclerDraw[]={EquipRecycler,SwitchWeapon},
    LureDraw[]={UnholsterLure,SwitchWeapon};
// Five animations also play Play_switch_weapon with no clip behind it, in every window
// of that weapon, most likely as an animation event: the Disruptor's and the pistol's
// draws, 12-29 ms after the clips, and the Q-Beam's, shotgun's and Boltcaster's stows,
// mostly 13-60 ms after. A frame apart at most is no difference here, so it plays with
// the rest.
inline constexpr Trigger StunGunDraw[]={SwitchWeapon,SwitchWeapon},PistolDraw[]={EquipPistol,SwitchWeapon,SwitchWeapon},
    StowSwitch[]={Unequip,SwitchWeapon};
// Any other class: it was not seen in play.
inline constexpr Trigger DefaultDraw[]={SwitchWeapon},DefaultStow[]={Unequip};
// Each weapon's, by its entity class name (WeaponDrawNative.h).
struct ClassSound {const char* className;std::span<const Trigger> draw,stow;};
inline constexpr ClassSound ClassSounds[]={
    {"ArkWeaponWrench",WrenchDraw,DefaultStow},
    {"ArkWeaponPistol",PistolDraw,DefaultStow},
    {"ArkWeaponShotgun",ShotgunDraw,StowSwitch},
    {"ArkWeaponToyGun",ToyGunDraw,StowSwitch},
    {"ArkWeaponGooGun",GlooDraw,DefaultStow},
    {"ArkWeaponInstalaser",QBeamDraw,StowSwitch},
    {"ArkWeaponStunGun",StunGunDraw,DefaultStow},
    {"ArkWeaponEMPGrenade",EmpDraw,DefaultStow},
    {"ArkWeaponLureGrenade",LureDraw,DefaultStow},
    {"ArkWeaponNullwaveTransmitter",NullwaveDraw,DefaultStow},
    {"ArkWeaponRecyclerGrenade",RecyclerDraw,DefaultStow},
};
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0x3AE460,333,0xE9B0FE0C319AFC54ull}, // PlaySoundEnter
    {0xD72300,12,0x9A30972C9DF339EDull}, // GetAudioTriggerId
    {0xD59F40,235,0x8B90C393F1172E37ull}, // TriggerIdByName
    {0x911F40,453,0xDF19342103BD36D9ull}, // ProxyExecuteTrigger
};
}
