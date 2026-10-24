#pragma once

// BodyFollow's private state, shared by its three files:
//  - BodyFollow.cpp: the ArkPlayer::Update hook and its gates, the torso, turning
//    and the per-frame tick;
//  - RoomScaleMove.cpp: room-scale moves, the living body's own calls and the
//    head-in-wall check;
//  - BodyFollowDiagnostics.cpp: the frame-order probes and body.report.
// The constants, the state and the small engine-call helpers, moved from
// BodyFollow.cpp as they were; one instance of each (`inline`). Include it from
// those three files only.
#include "GameplayFrame.h"
#include "Logger.h"
#include "SafeMemory.h"
#include "preyvr/ArmPose.h"
#include "preyvr/ContactNative.h"
#include "preyvr/EngineMap.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/RoomScale.h"
#include "preyvr/RoomScaleNative.h"
#include "preyvr/VrMath.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace preyvr::dll::body {

inline void Log(const std::string& line) { lifecycle::Log("preyvr_body " + line); }

// ArkPlayer::Update(SEntityUpdateContext&, int), primary vtable slot 19; R-021
// returns into it. RCX the player, RDX the context (frame time at +0x14), R8 the
// update slot. No float arguments, so the three registers are all it forwards.
using PlayerUpdateFn = void(__fastcall*)(void* player, void* context, int slot);
constexpr std::uintptr_t kPlayerUpdateRva = 0x1584FE0;
constexpr std::string_view kPlayerUpdatePrologue =
    "488bc455574881ecf800000048895808488bf90fb699851800004c8978e84c8b";

// The view-rotation pair, pinned offline by tools/re/verify_view_rotation.py:
// slots 52 and 53 of the primary vtable. The setter copies q to +0x13F0
// (ArkPlayerCamera::m_rotation), calls the entity's SetRotation slot with
// (q, 0) and hands q to the component at +0x1468, which keeps its own copy --
// so it is called, never imitated with a field write. The complete bodies are
// compared before the first call.
using SetViewRotationFn = void(__fastcall*)(void* player, const Quaternion* rotation);
using GetViewRotationFn = Quaternion*(__fastcall*)(void* player, Quaternion* out);
constexpr std::uintptr_t kPlayerVtableRva = 0x1E56B30;
constexpr unsigned kUpdateSlot = 19, kSetSlot = 52, kGetSlot = 53;
constexpr std::uintptr_t kSetViewRotationRva = 0x1584CC0, kGetViewRotationRva = 0x157CE80;
constexpr std::uintptr_t kComponentCopyRva = 0x1327050;
constexpr std::string_view kSetViewRotationBody =
    "48895c2408574883ec208b02488bd98981f01300004533c08b4204488bfa8981f41300008b42088981f8130000"
    "8b420c8981fc130000488b4938488b01ff9018010000488d8b68140000488bd7488b5c24304883c4205fe93423daff";
constexpr std::string_view kGetViewRotationBody =
    "8b81fc13000089420c8b81f013000089028b81f41300008942048b81f8130000894208488bc2c3";
constexpr std::string_view kComponentCopyBody = "8b028941088b420489410c8b42088941108b420c894114c3";

// Room-scale (docs/ROOMSCALE-2026-10-11.md): the player entity's position. The
// concrete CEntity vtable is the one holding SceneQuery's hash-gated GetPhysics
// (+0x228). SetPos(pos, whyFlags, recalcPhysics, force) at +0x108 stores
// m_vPos and dispatches the transform update; GetPos at +0x110 returns
// &m_vPos. The game's own stance change moves the player with this pair.
using SetPosFn = void(__fastcall*)(void* entity, const Vec3* position, int whyFlags, bool recalcPhysics, bool force);
using GetPosFn = const Vec3*(__fastcall*)(void* entity);
constexpr std::size_t kPlayerEntity = 0x38;
constexpr std::uintptr_t kEntityVtableRva = 0x1D30BD0;
constexpr unsigned kSetPosSlot = 0x108 / 8, kGetPosSlot = 0x110 / 8, kGetPhysicsSlot = 0x228 / 8;
constexpr std::uintptr_t kSetPosRva = 0x90AF40, kGetPosRva = 0x10EA250, kGetPhysicsRva = 0x9042B0;
constexpr std::string_view kSetPosBody =
    "f30f1002458bd00f2e41387516f30f1042040f2e413c750bf30f1042080f2e41407424f20f10024183ca02f20f114138"
    "450fb6c18b4208418bd2894140488b0148ffa050030000c3";
constexpr std::string_view kGetPosBody = "488d4138c3";

// The player's living body (docs/TURN-LEAN-ARMS-2026-10-19.md, fixes 1 and 2),
// through the physical entity's own slots and the bodies RoomScaleNative.h and
// ContactNative.h pin:
//  * before a move, its own stance test -- the 9 rays missed desk tops and
//    corners, SetPos put the capsule inside them, and the engine's next step
//    pushed it, and the view, back out;
//  * a move while it is moving keeps its motion -- the entity's SetPos alone is
//    the engine's teleport, which zeroed a walk's velocity. At rest that
//    teleport is kept: it lets the next step settle the body on what is under it.
// body.physics 0 restores the rays and the plain SetPos.
namespace rn = roomscale::native;
namespace cn = contact::native;
constexpr int kFitIterations = 6;          // within 1/64 of a move from the first contact
constexpr float kStepSettling = 0.001f;    // m of step-up smoothing: the body is climbing
constexpr float kPushOut = 0.005f;         // m the engine moved a body nothing moved

// Gameplay only. ArkPlayerCamera (+0x12A0): the mode UpdateView switches on
// (+0x1A4: 0 is the ordinary camera; modes 1-3 write m_rotation themselves,
// 0x181489010) and the custom-view callable (+0x148, R-009). ArkPlayerInput
// (+0x8E0): the cinematic gate (+0x94, R-089).
constexpr std::size_t kCameraMode = 0x12A0 + 0x1A4;
constexpr std::size_t kCustomView = 0x12A0 + 0x148;
constexpr std::size_t kCinematicGate = engine::ArkPlayerLayout::input + engine::ArkPlayerInputLayout::cinematic;

// Closer than this the body is where the torso is.
constexpr float kMinimumStep = 0.01f * kRadiansPerDegree;
constexpr std::uint64_t kTorsoFreshNs = 100000000ull;

inline std::mutex gInstallMutex;
inline std::atomic<bool> gInstalled{false};
inline std::atomic<PlayerUpdateFn> gOriginal{nullptr};
inline std::atomic<SetViewRotationFn> gSet{nullptr};
inline std::atomic<GetViewRotationFn> gGet{nullptr};
inline std::atomic<std::uintptr_t> gVtable{0};

inline std::atomic<bool> gActive{false}, gEnabled{true}, gLevel{true}, gFault{false}, gResetTorso{false};
inline std::atomic<bool> gRoomAvailable{false}, gRoomScale{true}, gNudgePending{false};
inline std::atomic<SetPosFn> gSetPos{nullptr};
inline std::atomic<GetPosFn> gGetPos{nullptr};
inline std::atomic<std::uintptr_t> gEntityVtable{0};
inline std::atomic<int> gStartMm{30}, gStopMm{5}, gFollowMs{50}, gRadiusMm{350};
inline std::atomic<int> gNudgeRightMm{0}, gNudgeForwardMm{0};
inline std::atomic<int> gDeadzoneMilliDeg{35000};
inline std::atomic<int> gRelaxPerMille{600};
inline std::atomic<int> gTurnRequestMdeg{0};
// Diagnostic requests wait for gameplay -- typed in the tuner, the game is
// unfocused and paused -- and for their delay, so the wearer can be back in
// the headset and looking down. Dropped a minute after they fell due.
inline std::atomic<std::uint64_t> gTurnDueNs{0}, gNudgeDueNs{0};
constexpr std::uint64_t kRequestLifeNs = 60000000000ull;
// The head-in-wall layer: out while the head is through geometry, or far
// from a body the sweep stopped. Published for the XR fade and the inputs.
inline std::atomic<bool> gHeadOut{false};
inline std::atomic<std::uint64_t> gHeadOutNs{0};
constexpr float kOutResidual = 0.30f, kInResidual = 0.25f;

// The game thread's own.
inline armpose::TorsoYaw gTorso;
inline std::uint64_t gTorsoNs = 0;
inline unsigned long long gTorsoEpoch = ~0ull;
inline roomscale::Follower gRoomFollower;
// Where the capsule was after the last tick, to tell a capsule the engine is
// moving -- stick walking, a jump, a fall, a lift -- from one at rest.
inline Vec3 gLastFeet{};
inline std::uint64_t gLastFeetNs = 0;
constexpr float kNativeMovingSpeed = 0.25f;   // m/s

struct TorsoSample {
    float yaw = 0.0f;
    std::uint64_t ns = 0;
};
inline LatestSnapshot<TorsoSample> gTorsoSample;

using Counter = std::atomic<unsigned long long>;
inline Counter gTicks{0}, gApplied{0}, gTurns{0}, gNotPlayer{0}, gNoHead{0}, gNotGameplay{0};
inline Counter gNoReference{0}, gBusy{0}, gNativeRefused{0}, gFaults{0}, gTurnedMdeg{0};
inline std::atomic<int> gCameraMode{-1}, gHeadMdeg{0}, gTorsoMdeg{0}, gLastDeltaMdeg{0}, gReadbackMaxMdeg{0};

// Frame order: one sequence over the follower, the view and the render hook.
inline Counter gSequence{0}, gFollowSeq{0}, gViewSeq{0}, gRenderSeq{0};
inline std::atomic<unsigned long> gFollowThread{0};
inline std::atomic<bool> gTurnedSinceView{false};
inline Counter gViewAfterFollow{0}, gViewWithoutFollow{0}, gThreadMismatch{0};
inline Counter gIkBetween{0}, gIkAfterView{0}, gIkAfterRender{0};
// How far the play space moved between consecutive views of one reference
// generation, on frames the body was turned and on frames it was not. Equal
// maxima mean the turn left the view alone.
inline std::mutex gViewMutex;
inline bool gHavePrevious = false;
inline float gPreviousP = 0.0f;
inline unsigned long long gPreviousGeneration = 0;
inline Counter gStepFollowed{0}, gStepIdle{0};
inline std::atomic<int> gStepFollowedMaxMdeg{0}, gStepIdleMaxMdeg{0};

// Room-scale. The play-space yaw the last view composed with: the move is
// expressed in its frame.
inline std::atomic<float> gViewP{0.0f};
inline std::atomic<unsigned long long> gViewPGeneration{~0ull};
inline std::atomic<std::uint64_t> gViewPNs{0};
inline Counter gMoved{0}, gMovedMm{0}, gBlocked{0}, gSweepRefused{0}, gRoomBusy{0}, gPositionOff{0}, gNoView{0};
inline Counter gNoPose{0}, gJumps{0}, gNudges{0}, gViewDrift{0}, gHeadOutEvents{0}, gHeadOutFrames{0}, gHeadHits{0};
inline Counter gNativeMoving{0};
inline std::atomic<int> gLastMoveMm{0}, gResidualMm{0}, gViewDriftMaxMm{0}, gNudgeDriftMaxMm{0};
// The living body's physics (fixes 1 and 2). Standing aside while the engine
// moves the capsule (body.standaside) is off by default: a move keeps the
// walk's motion, so the body follows physical turns and steps mid-walk too.
inline std::atomic<bool> gPhysicsAvailable{false}, gPhysics{true}, gStandAside{false}, gDimensionsLogged{false};
inline std::atomic<std::uintptr_t> gLivingVtable{0};
inline Counter gLivingUnread{0}, gFitChecks{0}, gFitShortened{0}, gFitVertical{0}, gFitStartBlocked{0};
inline Counter gKeptMotion{0}, gRestMoves{0}, gStepSettling{0}, gPushOut{0}, gDeferredBusy{0};
inline std::atomic<int> gPushOutMaxMm{0}, gBodyLagMaxMm{0};
// Where SetPos left the entity, for the view of the same frame to read back;
// and after a nudge, for the next views. Under gViewMutex.
inline bool gMovedPending = false;
inline Vec3 gMovedPos{};
inline void* gMovedEntity = nullptr;
inline int gNudgeWatch = 0;
inline Vec3 gNudgePos{};

inline int Mdeg(float radians)
{
    return std::isfinite(radians) ? static_cast<int>(std::lround(radians * kDegreesPerRadian * 1000.0f)) : 0;
}

inline bool ReadMemory(const void* at, void* out, std::size_t size)
{
    return SafeCopy(out, at, size);
}

template<class T> bool ReadAt(void* base, std::size_t offset, T& out)
{
    return ReadMemory(static_cast<const std::uint8_t*>(base) + offset, &out, sizeof(out));
}

inline bool CallGet(GetViewRotationFn get, void* player, Quaternion* out)
{
    __try {
        get(player, out);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline bool CallSet(SetViewRotationFn set, void* player, const Quaternion* rotation)
{
    __try {
        set(player, rotation);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline bool BytesMatch(std::uintptr_t address, std::string_view hex)
{
    std::uint8_t bytes[0x80]{};
    const std::size_t size = hex.size() / 2;
    if (size == 0 || size > sizeof(bytes) || !ReadMemory(reinterpret_cast<const void*>(address), bytes, size)) {
        return false;
    }
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < size; ++i) {
        if (bytes[i] != static_cast<std::uint8_t>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]))) {
            return false;
        }
    }
    return true;
}

inline bool SlotIs(std::uintptr_t base, std::uintptr_t vtableRva, unsigned slot, std::uintptr_t rva)
{
    std::uintptr_t value = 0;
    return ReadMemory(reinterpret_cast<const void*>(base + vtableRva + slot * sizeof(value)), &value,
                      sizeof(value)) &&
           value == base + rva;
}

inline bool CallGetPos(GetPosFn get, void* entity, Vec3& out)
{
    __try {
        out = *get(entity);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline bool CallSetPos(SetPosFn set, void* entity, const Vec3* position)
{
    __try {
        set(entity, position, 0, false, false);   // as the stance change calls it
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline int Mm(float metres) { return std::isfinite(metres) ? static_cast<int>(std::lround(metres * 1000.0f)) : 0; }

// --- the living body (fixes 1 and 2) -----------------------------------------

// What the living body's movement is doing: its own velocity, the velocity
// its movement asks for (pe_action_move), and camOffset, non-zero while a
// step-up is being smoothed.
struct Living {
    Vec3 velocity{}, requested{}, stance{};
};

// Called across the three files.
// RoomScaleMove.cpp:
bool ReadLiving(void* physics, Living& out);
void LogDimensions(void* physics);
void* PlayerEntity(void* player);
bool ViewPlaySpaceYaw(float& yaw);
int Move(void* player, const GameplayPoseFrame& pose, float playSpaceYaw, const Pose& head, float dt,
         const Vec3* nudge, std::uintptr_t world, void* physics, const Living& living, float& residual);
void HeadCheck(void* player, const GameplayPoseFrame& pose, float residual);
// BodyFollow.cpp:
void Fault(const char* where);
void ResetStats();

} // namespace preyvr::dll::body
