#pragma once
#include <cstddef>
#include <cstdint>
// Room-scale's physics calls on the player's living body. Steam SHA
// 7d6e322f...11a7. See docs/BODY-FOLLOW.md (the capsule's physics). Static
// contracts, checked by tools/re/verify_room_scale_physics.py and gated by
// RoomScaleLane.cpp. The living vtable, its GetStatus and GetParams and the
// base ones are ContactNative.h's.
namespace preyvr::roomscale::native {
// IPhysicalEntity::SetParams(params, bThreadSafe) is slot +0x20. The living
// class's (0xB49610) queues the call while the body is being stepped, runs the
// base SetParams (0xB88AA0), then -- for pe_params_pos whose position moved
// more than 1% of the collider's half-height, unless bRecalcBounds & 0x20 --
// releases the ground, sets the airborne flag and zeroes the body's velocity
// (+0x168): the teleport that stalled a walk. The base SetParams takes pos when
// +0x04 is not the "unused" float, q when its w (+0x1C) is not, the sim class
// when +0x38 is not 0x80000000, and recomputes the bounds when bRecalcBounds is
// non-zero. So bRecalcBounds 0x21 moves the body, bounds included, and keeps
// its motion.
//
// The entity's own SetPos then reaches the same SetParams through the physics
// proxy's XForm handler (0x96BE00): pe_params_pos with that position only and
// bRecalcBounds 1. The position is already there, so nothing moved and the
// teleport is skipped.
inline constexpr std::size_t SetParamsSlot=0x20;
inline constexpr int ParamsPos=0;
// pe_params_pos: pos +0x04, q +0x10 (unused by w at +0x1C), scale +0x20,
// pMtx3x4 +0x28, pMtx3x3 +0x30, iSimClass +0x38, bRecalcBounds +0x3C,
// bEntGridUseOBB +0x40 -- as the proxy builds it.
inline constexpr std::size_t PosPosition=0x04,PosRotation=0x10,PosRotationW=0x1C,PosScale=0x20,
    PosMatrix34=0x28,PosMatrix33=0x30,PosSimClass=0x38,PosRecalcBounds=0x3C,PosGridObb=0x40,PosBytes=0x48;
inline constexpr int RecalcBoundsKeepMotion=0x21;
// The engine's "unused" markers: a float and an int.
inline constexpr std::uint32_t UnusedFloat=0xFFBFFFFF,UnusedInt=0x80000000;
// pe_status_living (type 2), as the living GetStatus (0xB475D0) copies it:
// the body's own velocity +0x30 (from +0x168) and the velocity the movement
// asks for +0x3C (from +0x174, pe_action_move). camOffset +0x0C is ContactNative's.
inline constexpr std::size_t LivingVelocity=0x30,LivingRequested=0x3C;
// pe_status_check_stance (type 0x15): the base GetStatus leaves it to the
// living one, which tests the collider at pos +0x04 (the body's own position
// +0xA8 when unused), q +0x10, sizeCollider +0x20, heightCollider +0x2C and
// bUseCapsule +0x40 (each its own when unused) through the stance test
// (0xB56900): every entity around it but itself, its own parts, and what
// ignores collisions with it, against the body's collision classes. It returns
// 1 when nothing overlaps; otherwise the depth goes to +0x3C and, when
// dirUnproj (+0x30) is zero, the contact's normal to it.
inline constexpr int StatusCheckStance=0x15;
inline constexpr std::size_t StancePosition=0x04,StanceRotationW=0x1C,StanceSize=0x20,StanceHeight=0x2C,
    StanceDirection=0x30,StanceDepth=0x3C,StanceCapsule=0x40,StanceBytes=0x48;
// When a SetParams applies (the change request, 0xB1BCD0, which both
// SetParams run first): at once, from a thread other than the physics thread,
// only while the world's step lock (+0x576C), its +0x57B0 and its lock
// (+0x5784) are all clear and the entity has nothing queued (+0x40 below
// 0x20000). Otherwise it is queued and replayed after the step -- an absolute
// position computed before the step would then undo the step's own movement.
// So a move that keeps a walk's motion is made only while all four are clear,
// from the body's own position (+0xA8), and otherwise waits a frame.
inline constexpr std::size_t WorldStepLock=0x576C,WorldStepBusy=0x57B0,WorldLock=0x5784,EntityQueued=0x40;
inline constexpr std::uint32_t EntityQueuedUnit=0x20000;
inline constexpr std::size_t BodyPosition=0xA8;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr std::uintptr_t LivingSetParams=0xB49610,PhysicalSetParams=0xB88AA0,LivingStanceOverlap=0xB56900,
    PhysicsProxyXForm=0x96BE00,PhysicsChangeRequest=0xB1BCD0;
inline constexpr Contract Contracts[]={
    {0xB49610,4565,0xC738299B51BD2807ull}, // LivingSetParams
    {0xB88AA0,13094,0x804ED4D859A251D3ull}, // PhysicalSetParams
    {0xB56900,3935,0xC07C3476F44E2E17ull}, // LivingStanceOverlap
    {0x96BE00,519,0xAACCB6B7C4267F75ull}, // PhysicsProxyXForm
    {0xB1BCD0,1498,0xDE140578B20449F1ull}, // PhysicsChangeRequest
};
}
