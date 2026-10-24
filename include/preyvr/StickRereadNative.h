#pragma once
#include <cstddef>
#include <cstdint>
// Prey's re-read of the player's stick axes, which stopped the player
// (docs/MOVE-STOP-2026-10-24.md). Steam SHA 7d6e322f...11a7. Static contracts,
// checked by tools/re/verify_stick_reread.py and gated by StickReread.cpp.
namespace preyvr::stick::native {
// ArkPlayerInput (ArkPlayer +0x8E0, vtable 0x1E580E0) listens to the action map
// manager: OnActionMapEvent (0x1590390) is slot 1 of its vtable at +8
// (0x1E580F8). On event 3 (a map enabled) for `player`, and on event 2 (a
// filter changed) while the top of its input-mode stack (+0x10..+0x18, 8-byte
// entries, mode first) is mode 0 (`player`), it runs the re-read (0x158E780).
//
// The re-read stores each axis from AxisRead(context, action, map) (0x158EB90,
// called only there): move X +0x5C (xi_movex, action table +0x2F0), move Y
// +0x60 (+0x2F8), yaw +0x4C negated (xi_rotateyaw, +0x308) and pitch +0x44
// (+0x310). Afterwards the move axes are zeroed while the cinematic gate (+0x94)
// is set, and yaw while +0xB4 is set or the gate is 2.
//
// AxisRead returns 0 when the manager (**context) reports the action filtered
// (slot +0x1A8, ActionFiltered 0x3CDFE0), otherwise the physical pad's symbol:
// IInput LookupSymbol(gamepad 3, CUIManager+0x3C, key)->value (+0x1C). The
// mod's posts carry no symbol, so with motion controllers it found 0.
inline constexpr std::uintptr_t ActionTable=0x2C16838;   // qword: the action names' struct
inline constexpr std::size_t MoveXAction=0x2F0,MoveYAction=0x2F8,YawAction=0x308;
inline constexpr std::uintptr_t InputVtable=0x1E580E0,ListenerVtable=0x1E580F8;
inline constexpr std::size_t ListenerSlot=0x8,PlayerInput=0x8E0;
inline constexpr std::size_t InputYaw=0x4C,InputMoveX=0x5C,InputMoveY=0x60,InputGate=0x94,InputYawBlock=0xB4;
inline constexpr std::uintptr_t ManagerVtable=0x1CBFC48;
inline constexpr std::size_t FilteredSlot=0x1A8;
// The yaw handler (0x158FE50) the turn lane's xi_thumbrx reaches stores -value at
// +0x4C, and stores nothing while +0xB4 is set or the gate is 2.
//
// The event: type +0x00; event 3 carries the map at +0x08 and enable at +0x10;
// event 2 the filter's name and its state in either order --
// CActionFilter::Enable (0x3C3770, which sends it on every call, changed or not)
// puts enable first, the manager's EnableFilter (0x3CF1C0) the name first, after
// calling the filter's Enable.
inline constexpr int EventFilter=2,EventMap=3;
inline constexpr std::size_t EventFirst=0x08,EventSecond=0x10;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr std::uintptr_t AxisRead=0x158EB90,AxisReread=0x158E780,ActionMapEvent=0x1590390,
    ActionFiltered=0x3CDFE0,YawHandler=0x158FE50,PlayerSingleton=0x157C990;
inline constexpr Contract Contracts[]={
    {0x158EB90,156,0x839FF8F2B903A36Dull}, // AxisRead
    {0x158E780,1029,0xA020E7C8A9E8C647ull}, // AxisReread
    {0x1590390,342,0x0F80B91D3FA9D615ull}, // ActionMapEvent
    {0x3CDFE0,168,0x81C105DC760EE087ull}, // ActionFiltered
    {0x158FE50,39,0x39D47CA81721D5CAull}, // YawHandler
    {0x157C990,48,0xB0D3EB821FBC9AB5ull}, // PlayerSingleton
};
}
