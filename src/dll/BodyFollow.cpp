#include "BodyFollow.h"
#include "SafeMemory.h"
#include "HookInstall.h"
#include "EngineAccess.h"
#include "AtomicMax.h"

#include "GameplayFrame.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "SceneQuery.h"
#include "XrSessionHost.h"
#include "XrInput.h"
#include "preyvr/EngineMap.h"
#include "preyvr/ArmPose.h"
#include "preyvr/BodyFollow.h"
#include "preyvr/ContactNative.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/NativeContract.h"
#include "preyvr/RoomScale.h"
#include "preyvr/RoomScaleNative.h"

#include <MinHook.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <sstream>
#include <string_view>

namespace preyvr::dll {
namespace {

void Log(const std::string& line) { lifecycle::Log("preyvr_body " + line); }

constexpr float kDegrees = 57.2957795f;
constexpr float kRadians = 0.0174532925f;

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
constexpr float kMinimumStep = 0.01f * kRadians;
constexpr std::uint64_t kTorsoFreshNs = 100000000ull;

std::mutex gInstallMutex;
std::atomic<bool> gInstalled{false};
std::atomic<PlayerUpdateFn> gOriginal{nullptr};
std::atomic<SetViewRotationFn> gSet{nullptr};
std::atomic<GetViewRotationFn> gGet{nullptr};
std::atomic<std::uintptr_t> gVtable{0};

std::atomic<bool> gActive{false}, gEnabled{true}, gLevel{true}, gFault{false}, gResetTorso{false};
std::atomic<bool> gRoomAvailable{false}, gRoomScale{true}, gNudgePending{false};
std::atomic<SetPosFn> gSetPos{nullptr};
std::atomic<GetPosFn> gGetPos{nullptr};
std::atomic<std::uintptr_t> gEntityVtable{0};
std::atomic<int> gStartMm{30}, gStopMm{5}, gFollowMs{50}, gRadiusMm{350};
std::atomic<int> gNudgeRightMm{0}, gNudgeForwardMm{0};
std::atomic<int> gDeadzoneMilliDeg{35000};
std::atomic<int> gRelaxPerMille{600};
std::atomic<int> gTurnRequestMdeg{0};
// Diagnostic requests wait for gameplay -- typed in the tuner, the game is
// unfocused and paused -- and for their delay, so the wearer can be back in
// the headset and looking down. Dropped a minute after they fell due.
std::atomic<std::uint64_t> gTurnDueNs{0}, gNudgeDueNs{0};
constexpr std::uint64_t kRequestLifeNs = 60000000000ull;
// The head-in-wall layer: out while the head is through geometry, or far
// from a body the sweep stopped. Published for the XR fade and the inputs.
std::atomic<bool> gHeadOut{false};
std::atomic<std::uint64_t> gHeadOutNs{0};
constexpr float kOutResidual = 0.30f, kInResidual = 0.25f;

// The game thread's own.
armpose::TorsoYaw gTorso;
std::uint64_t gTorsoNs = 0;
unsigned long long gTorsoEpoch = ~0ull;
roomscale::Follower gRoomFollower;
// Where the capsule was after the last tick, to tell a capsule the engine is
// moving -- stick walking, a jump, a fall, a lift -- from one at rest.
Vec3 gLastFeet{};
std::uint64_t gLastFeetNs = 0;
constexpr float kNativeMovingSpeed = 0.25f;   // m/s

struct TorsoSample {
    float yaw = 0.0f;
    std::uint64_t ns = 0;
};
LatestSnapshot<TorsoSample> gTorsoSample;

using Counter = std::atomic<unsigned long long>;
Counter gTicks{0}, gApplied{0}, gTurns{0}, gNotPlayer{0}, gNoHead{0}, gNotGameplay{0};
Counter gNoReference{0}, gBusy{0}, gNativeRefused{0}, gFaults{0}, gTurnedMdeg{0};
std::atomic<int> gCameraMode{-1}, gHeadMdeg{0}, gTorsoMdeg{0}, gLastDeltaMdeg{0}, gReadbackMaxMdeg{0};

// Frame order: one sequence over the follower, the view and the render hook.
Counter gSequence{0}, gFollowSeq{0}, gViewSeq{0}, gRenderSeq{0};
std::atomic<unsigned long> gFollowThread{0};
std::atomic<bool> gTurnedSinceView{false};
Counter gViewAfterFollow{0}, gViewWithoutFollow{0}, gThreadMismatch{0};
Counter gIkBetween{0}, gIkAfterView{0}, gIkAfterRender{0};
// How far the play space moved between consecutive views of one reference
// generation, on frames the body was turned and on frames it was not. Equal
// maxima mean the turn left the view alone.
std::mutex gViewMutex;
bool gHavePrevious = false;
float gPreviousP = 0.0f;
unsigned long long gPreviousGeneration = 0;
Counter gStepFollowed{0}, gStepIdle{0};
std::atomic<int> gStepFollowedMaxMdeg{0}, gStepIdleMaxMdeg{0};

// Room-scale. The play-space yaw the last view composed with: the move is
// expressed in its frame.
std::atomic<float> gViewP{0.0f};
std::atomic<unsigned long long> gViewPGeneration{~0ull};
std::atomic<std::uint64_t> gViewPNs{0};
Counter gMoved{0}, gMovedMm{0}, gBlocked{0}, gSweepRefused{0}, gRoomBusy{0}, gPositionOff{0}, gNoView{0};
Counter gNoPose{0}, gJumps{0}, gNudges{0}, gViewDrift{0}, gHeadOutEvents{0}, gHeadOutFrames{0}, gHeadHits{0};
Counter gNativeMoving{0};
std::atomic<int> gLastMoveMm{0}, gResidualMm{0}, gViewDriftMaxMm{0}, gNudgeDriftMaxMm{0};
// The living body's physics (fixes 1 and 2). Standing aside while the engine
// moves the capsule (body.standaside) is off by default: a move keeps the
// walk's motion, so the body follows physical turns and steps mid-walk too.
std::atomic<bool> gPhysicsAvailable{false}, gPhysics{true}, gStandAside{false}, gDimensionsLogged{false};
std::atomic<std::uintptr_t> gLivingVtable{0};
Counter gLivingUnread{0}, gFitChecks{0}, gFitShortened{0}, gFitVertical{0}, gFitStartBlocked{0};
Counter gKeptMotion{0}, gRestMoves{0}, gStepSettling{0}, gPushOut{0}, gDeferredBusy{0};
std::atomic<int> gPushOutMaxMm{0}, gBodyLagMaxMm{0};
// Where SetPos left the entity, for the view of the same frame to read back;
// and after a nudge, for the next views. Under gViewMutex.
bool gMovedPending = false;
Vec3 gMovedPos{};
void* gMovedEntity = nullptr;
int gNudgeWatch = 0;
Vec3 gNudgePos{};

int Mdeg(float radians)
{
    return std::isfinite(radians) ? static_cast<int>(std::lround(radians * kDegrees * 1000.0f)) : 0;
}

bool ReadMemory(const void* at, void* out, std::size_t size)
{
    return SafeCopy(out, at, size);
}

template<class T> bool ReadAt(void* base, std::size_t offset, T& out)
{
    return ReadMemory(static_cast<const std::uint8_t*>(base) + offset, &out, sizeof(out));
}

bool CallGet(GetViewRotationFn get, void* player, Quaternion* out)
{
    __try {
        get(player, out);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallSet(SetViewRotationFn set, void* player, const Quaternion* rotation)
{
    __try {
        set(player, rotation);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool BytesMatch(std::uintptr_t address, std::string_view hex)
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

bool SlotIs(std::uintptr_t base, std::uintptr_t vtableRva, unsigned slot, std::uintptr_t rva)
{
    std::uintptr_t value = 0;
    return ReadMemory(reinterpret_cast<const void*>(base + vtableRva + slot * sizeof(value)), &value,
                      sizeof(value)) &&
           value == base + rva;
}

bool CallGetPos(GetPosFn get, void* entity, Vec3& out)
{
    __try {
        out = *get(entity);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallSetPos(SetPosFn set, void* entity, const Vec3* position)
{
    __try {
        set(entity, position, 0, false, false);   // as the stance change calls it
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int Mm(float metres) { return std::isfinite(metres) ? static_cast<int>(std::lround(metres * 1000.0f)) : 0; }

// --- the living body (fixes 1 and 2) -----------------------------------------

// What the living body's movement is doing: its own velocity, the velocity
// its movement asks for (pe_action_move), and camOffset, non-zero while a
// step-up is being smoothed.
struct Living {
    Vec3 velocity{}, requested{}, stance{};
};

template <class Fn> Fn Slot(void* physics, std::size_t slot)
{
    return *reinterpret_cast<const Fn*>(*static_cast<const std::uintptr_t*>(physics) + slot);
}

bool ReadLiving(void* physics, Living& out)
{
    alignas(16) std::uint8_t status[0x100]{};
    static_assert(cn::LivingStatusSize <= sizeof(status) && rn::LivingRequested + sizeof(Vec3) <= cn::LivingStatusSize);
    const int type = cn::StatusLiving;
    std::memcpy(status, &type, sizeof(type));
    __try {
        using GetStatusFn = int(__fastcall*)(void*, void*);
        if (!Slot<GetStatusFn>(physics, cn::StatusSlot)(physics, status)) { return false; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    std::memcpy(&out.velocity, status + rn::LivingVelocity, sizeof(Vec3));
    std::memcpy(&out.requested, status + rn::LivingRequested, sizeof(Vec3));
    std::memcpy(&out.stance, status + cn::LivingCamOffset, sizeof(Vec3));
    return true;
}

// pe_status_check_stance at `position` (null: where the body stands), with
// the body's own rotation and collider. False when the call faulted; `fits` is
// the verdict and `normal` the contact's normal when it does not.
bool StanceFits(void* physics, const Vec3* position, bool& fits, Vec3& normal)
{
    alignas(16) std::uint8_t check[rn::StanceBytes]{};
    const int type = rn::StatusCheckStance;
    const std::uint32_t unusedFloat = rn::UnusedFloat, unusedInt = rn::UnusedInt;
    std::memcpy(check, &type, sizeof(type));
    if (position != nullptr) {
        std::memcpy(check + rn::StancePosition, position, sizeof(Vec3));
    } else {
        std::memcpy(check + rn::StancePosition, &unusedFloat, sizeof(unusedFloat));
    }
    std::memcpy(check + rn::StanceRotationW, &unusedFloat, sizeof(unusedFloat));
    std::memcpy(check + rn::StanceSize, &unusedFloat, sizeof(unusedFloat));
    std::memcpy(check + rn::StanceHeight, &unusedFloat, sizeof(unusedFloat));
    std::memcpy(check + rn::StanceCapsule, &unusedInt, sizeof(unusedInt));
    // dirUnproj (+0x30) stays zero, so the test writes the contact's normal there.
    __try {
        using GetStatusFn = int(__fastcall*)(void*, void*);
        fits = Slot<GetStatusFn>(physics, cn::StatusSlot)(physics, check) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    std::memcpy(&normal, check + rn::StanceDirection, sizeof(Vec3));
    return true;
}

// pe_params_pos with the position alone and bRecalcBounds 0x21: the body
// moves, its bounds with it, and keeps its velocity, ground and airborne state.
// The entity's SetPos that follows sends the same position, which no longer
// moves it, so the engine's teleport is skipped as well.
bool SetPositionKeepingMotion(void* physics, const Vec3& position)
{
    alignas(16) std::uint8_t params[rn::PosBytes]{};   // zero: no matrices, no grid OBB
    const int type = rn::ParamsPos, recalcBounds = rn::RecalcBoundsKeepMotion;
    const std::uint32_t unusedFloat = rn::UnusedFloat, unusedInt = rn::UnusedInt;
    std::memcpy(params, &type, sizeof(type));
    std::memcpy(params + rn::PosPosition, &position, sizeof(position));
    std::memcpy(params + rn::PosRotationW, &unusedFloat, sizeof(unusedFloat));
    std::memcpy(params + rn::PosScale, &unusedFloat, sizeof(unusedFloat));
    std::memcpy(params + rn::PosSimClass, &unusedInt, sizeof(unusedInt));
    std::memcpy(params + rn::PosRecalcBounds, &recalcBounds, sizeof(recalcBounds));
    __try {
        // bThreadSafe 0, as the entity's own physics proxy calls it: queued
        // while the body is being stepped, in order with that call.
        using SetParamsFn = int(__fastcall*)(void*, const void*, int);
        Slot<SetParamsFn>(physics, rn::SetParamsSlot)(physics, params, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// pe_player_dimensions (type 1) into `dims` (ContactNative.h's layout).
bool ReadDimensions(void* physics, std::uint8_t* dims)
{
    const int type = cn::ParamsPlayerDimensions;
    std::memcpy(dims, &type, sizeof(type));
    __try {
        using GetParamsFn = int(__fastcall*)(void*, void*);
        return Slot<GetParamsFn>(physics, cn::ParamsSlot)(physics, dims) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Whether a SetParams on the body applies at once rather than queued behind a
// physics step: the change request's own test (RoomScaleNative.h), from the
// game thread -- the world's step lock, +0x57B0 and its lock all clear, and
// nothing queued for the body.
bool PhysicsIdle(std::uintptr_t world, void* physics)
{
    std::uint32_t stepLock = 1, stepBusy = 1, worldLock = 1, queued = rn::EntityQueuedUnit;
    void* const w = reinterpret_cast<void*>(world);
    return world != 0 && ReadAt(w, rn::WorldStepLock, stepLock) && ReadAt(w, rn::WorldStepBusy, stepBusy) &&
           ReadAt(w, rn::WorldLock, worldLock) && ReadAt(physics, rn::EntityQueued, queued) && stepLock == 0 &&
           stepBusy == 0 && worldLock == 0 && queued < rn::EntityQueuedUnit;
}

// The living body's collider, once: where its bottom is decides which steps a
// physical step can take without the stance test refusing them.
void LogDimensions(void* physics)
{
    if (gDimensionsLogged.exchange(true)) { return; }
    alignas(16) std::uint8_t dims[0x80]{};
    static_assert(cn::DimensionsBytes <= sizeof(dims));
    if (!ReadDimensions(physics, dims)) {
        Log("result=unread detail=room_scale_dimensions");
        return;
    }
    // heightPivot +0x04, sizeCollider +0x0C (radius, -, half-height),
    // heightCollider +0x18, bUseCapsule +0x34 (ContactNative.h).
    float pivot = 0, size[3]{}, height = 0;
    int capsule = 0;
    std::memcpy(&pivot, dims + cn::DimensionsPivot, sizeof(pivot));
    std::memcpy(size, dims + cn::DimensionsSize, sizeof(size));
    std::memcpy(&height, dims + 0x18, sizeof(height));
    std::memcpy(&capsule, dims + 0x34, sizeof(capsule));
    const float bottom = height - pivot - size[2] - (capsule ? size[0] : 0.0f);
    std::ostringstream line;
    line << "result=0 detail=room_scale_dimensions radiusMm=" << Mm(size[0]) << " halfHeightMm=" << Mm(size[2])
         << " heightColliderMm=" << Mm(height) << " pivotMm=" << Mm(pivot) << " capsule=" << capsule
         << " bottomMm=" << Mm(bottom);
    Log(line.str());
}

// A fault in a living-body call: room-scale drops back to the rays and the
// plain SetPos for the rest of the process.
void PhysicsFault(const char* where)
{
    if (gPhysicsAvailable.exchange(false)) {
        Log(std::string("result=fault detail=room_scale_physics_") + where + " fallback=rays_and_setpos");
    }
}

// The player's entity, when it is the concrete CEntity the pair was verified on.
void* PlayerEntity(void* player)
{
    void* entity = nullptr;
    std::uintptr_t table = 0;
    if (!ReadAt(player, kPlayerEntity, entity) || entity == nullptr || !ReadAt(entity, 0, table) ||
        table != gEntityVtable.load(std::memory_order_relaxed)) {
        return nullptr;
    }
    return entity;
}

roomscale::Settings RoomSettings()
{
    roomscale::Settings settings;
    settings.startRadius = gStartMm.load(std::memory_order_relaxed) / 1000.0f;
    settings.stopRadius = gStopMm.load(std::memory_order_relaxed) / 1000.0f;
    settings.followTime = gFollowMs.load(std::memory_order_relaxed) / 1000.0f;
    settings.radius = gRadiusMm.load(std::memory_order_relaxed) / 1000.0f;
    return settings;
}

// The play-space yaw the latest view of this reference composed with.
bool ViewPlaySpaceYaw(float& yaw)
{
    if (gViewPGeneration.load(std::memory_order_acquire) != HeadTrackingReferenceGeneration() ||
        !FreshSample(MonotonicNanoseconds(), gViewPNs.load(std::memory_order_acquire), kTorsoFreshNs)) {
        return false;
    }
    yaw = gViewP.load(std::memory_order_acquire);
    return std::isfinite(yaw);
}

// A fault in engine code is not assumed recoverable: following stays off for
// the rest of the process.
void Fault(const char* where)
{
    gFaults.fetch_add(1, std::memory_order_relaxed);
    if (!gFault.exchange(true)) {
        Log(std::string("result=fault detail=") + where + " following=disarmed_for_process");
    }
}

bool InGameplay(void* player)
{
    int mode = -1;
    int cinematic = -1;
    std::uintptr_t customView = 1;
    if (!ReadAt(player, kCameraMode, mode) || !ReadAt(player, kCustomView, customView) ||
        !ReadAt(player, kCinematicGate, cinematic)) {
        return false;
    }
    gCameraMode.store(mode, std::memory_order_relaxed);
    return mode == 0 && customView == 0 && cinematic == 0;
}

// Turns the native player -- by `requested`, or to the torso when `toTorso` --
// and the reference with it, by what the setter actually applied.
int Turn(void* player, float torso, float requested, bool toTorso)
{
    const SetViewRotationFn set = gSet.load(std::memory_order_acquire);
    const GetViewRotationFn get = gGet.load(std::memory_order_acquire);
    float applied = 0.0f;
    const int result = TurnHeadTrackingReferenceWithNative([&](float reference) -> std::optional<float> {
        float delta = requested;
        if (toTorso) {
            const auto step = bodyfollow::FollowStep(reference, torso, kMinimumStep);
            if (!step.apply) { return std::nullopt; }   // already facing the torso
            delta = step.delta;
        }
        Quaternion before{};
        if (!CallGet(get, player, &before)) {
            Fault("get_view_rotation");
            return std::nullopt;
        }
        const auto beforeYaw = bodyfollow::EngineYawOf(before);
        const auto target = bodyfollow::TurnedViewRotation(before, delta, gLevel.load(std::memory_order_relaxed));
        if (!beforeYaw || !target) {
            gNativeRefused.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        }
        if (!CallSet(set, player, &*target)) {
            Fault("set_view_rotation");
            return std::nullopt;
        }
        Quaternion after{};
        if (!CallGet(get, player, &after)) {
            Fault("get_view_rotation");
            return std::nullopt;
        }
        const auto afterYaw = bodyfollow::EngineYawOf(after);
        applied = afterYaw ? bodyfollow::Wrap(*afterYaw - *beforeYaw) : delta;
        StoreMax(gReadbackMaxMdeg, std::abs(Mdeg(bodyfollow::Wrap(applied - delta))));
        return applied;
    });
    if (result == 0) {
        (toTorso ? gApplied : gTurns).fetch_add(1, std::memory_order_relaxed);
        gLastDeltaMdeg.store(Mdeg(applied), std::memory_order_relaxed);
        gTurnedMdeg.fetch_add(static_cast<unsigned long long>(std::abs(Mdeg(applied))), std::memory_order_relaxed);
        gTurnedSinceView.store(true, std::memory_order_release);
    } else if (result == 1) {
        gNoReference.fetch_add(1, std::memory_order_relaxed);
    } else if (result == 2) {
        gBusy.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

// How far along `move` the capsule standing at `feet` may go, by the living
// body's own stance test: the whole move, the largest part of it that fits,
// or nothing. False when the test cannot decide -- it faulted, or the body
// already overlaps something where it stands (what the test counts and the
// engine's step ignores, perhaps) -- and the caller sweeps the rays instead.
bool FitAlong(void* physics, const Vec3& feet, const Vec3& move, float& fraction, bool& vertical)
{
    bool startFits = false;
    Vec3 normal{};
    if (!StanceFits(physics, nullptr, startFits, normal)) {
        PhysicsFault("stance_test");
        return false;
    }
    if (!startFits) {
        gFitStartBlocked.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    bool faulted = false;
    vertical = false;
    fraction = roomscale::FitFraction([&](float f) {
        const Vec3 at{feet.x + move.x * f, feet.y + move.y * f, feet.z};
        bool fits = false;
        Vec3 contact{};
        if (faulted || !StanceFits(physics, &at, fits, contact)) {
            faulted = true;
            return false;
        }
        // A contact above or below -- a step under the capsule's bottom, or a
        // low ceiling -- rather than a wall: counted, so a refused staircase
        // shows in body.report (fitVertical).
        if (!fits && std::fabs(contact.z) > 0.7f) { vertical = true; }
        return fits;
    }, kFitIterations);
    if (faulted) {
        PhysicsFault("stance_test");
        return false;
    }
    gFitChecks.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// Moves the player's capsule -- towards the head, or by `nudge` -- where it
// fits (the living body's stance test, or the rays without it), and the origin
// with it by exactly what SetPos applied. `physics` is the living body, or null.
int Move(void* player, const GameplayPoseFrame& pose, float playSpaceYaw, const Pose& head, float dt,
         const Vec3* nudge, std::uintptr_t world, void* physics, const Living& living, float& residual)
{
    residual = std::numeric_limits<float>::quiet_NaN();
    void* const entity = PlayerEntity(player);
    if (entity == nullptr) {
        gNotPlayer.fetch_add(1, std::memory_order_relaxed);
        return -1;
    }
    const SetPosFn set = gSetPos.load(std::memory_order_acquire);
    const GetPosFn get = gGetPos.load(std::memory_order_acquire);
    const roomscale::Settings settings = RoomSettings();
    bool blocked = false;
    bool sweepRefused = false;
    Vec3 applied{};
    Vec3 movedTo{};
    const int result = MoveHeadTrackingOriginWithNative(head, playSpaceYaw, [&](const Vec3& offset) -> std::optional<Vec3> {
        residual = std::hypot(offset.x, offset.y);   // until a move takes some of it off
        const Vec3 move = nudge != nullptr ? *nudge : gRoomFollower.Update(offset, dt, settings);
        if (nudge == nullptr && gRoomFollower.Jumped()) { gJumps.fetch_add(1, std::memory_order_relaxed); }
        const float length = std::hypot(move.x, move.y);
        if (!(length > 1e-5f)) { return std::nullopt; }
        // Climbing a step, the body's position is still being smoothed up to
        // it: wait the moment that takes rather than test or move it mid-way.
        if (physics != nullptr && Length(living.stance) > kStepSettling) {
            gStepSettling.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        }
        Vec3 feet{};
        if (!CallGetPos(get, entity, feet)) {
            Fault("get_pos");
            return std::nullopt;
        }
        // A body that is moving keeps its motion (fix 1). It is moved from
        // where its physics has it now, and only while a SetParams applies at
        // once: one queued behind a physics step would put it back where it was
        // before that step, undoing the walk's own movement. Otherwise the move
        // waits a frame. At rest the entity's SetPos alone, whose teleport lets
        // the next step settle the body on what is under it.
        const bool moving = physics != nullptr && roomscale::BodyMoving(living.velocity, living.requested);
        Vec3 base = feet;
        if (moving) {
            Vec3 body{};
            if (!PhysicsIdle(world, physics) || !ReadAt(physics, rn::BodyPosition, body) || !Finite(body)) {
                gDeferredBusy.fetch_add(1, std::memory_order_relaxed);
                return std::nullopt;
            }
            StoreMax(gBodyLagMaxMm, Mm(std::hypot(body.x - feet.x, body.y - feet.y)));
            base = body;
        }
        float allowed = length;
        float fraction = 1.0f;
        bool vertical = false;
        if (physics != nullptr && FitAlong(physics, base, move, fraction, vertical)) {
            allowed = fraction * length;
            if (fraction < 1.0f) {
                blocked = true;
                gFitShortened.fetch_add(1, std::memory_order_relaxed);
                if (vertical) { gFitVertical.fetch_add(1, std::memory_order_relaxed); }
            }
        } else {
            roomscale::Rays rays{};
            if (!roomscale::SweepRays(base, move, settings, rays)) { return std::nullopt; }
            std::array<float, roomscale::kRays> hits{};
            for (std::size_t i = 0; i < rays.size(); ++i) {
                scene::Hit hit{};
                if (!QuerySegment(pose, rays[i].from, rays[i].to, hit)) {
                    sweepRefused = true;   // no move without a sweep
                    return std::nullopt;
                }
                hits[i] = hit.distance;
            }
            allowed = roomscale::AllowedLength(length, rays, hits, settings);
            if (allowed < length) { blocked = true; }
        }
        if (allowed < 1e-4f) { return std::nullopt; }
        const float k = allowed / length;
        const Vec3 target{base.x + move.x * k, base.y + move.y * k, base.z};
        if (moving) {
            if (SetPositionKeepingMotion(physics, target)) {
                gKeptMotion.fetch_add(1, std::memory_order_relaxed);
            } else {
                PhysicsFault("set_params");
            }
        } else if (physics != nullptr) {
            gRestMoves.fetch_add(1, std::memory_order_relaxed);
        }
        if (!CallSetPos(set, entity, &target)) {
            Fault("set_pos");
            return std::nullopt;
        }
        if (!CallGetPos(get, entity, movedTo)) {
            Fault("get_pos");
            return std::nullopt;
        }
        // Horizontal only: height stays the view's and the engine's business.
        // From `base`: if the entity was behind its physics, catching that up is
        // the walk's own movement, which the view shows rather than cancels.
        applied = {movedTo.x - base.x, movedTo.y - base.y, 0.0f};
        gLastFeet = movedTo;   // our move is not the engine's
        residual = std::hypot(offset.x - applied.x, offset.y - applied.y);
        return applied;
    });
    if (std::isfinite(residual)) { gResidualMm.store(Mm(residual), std::memory_order_relaxed); }
    if (blocked) { gBlocked.fetch_add(1, std::memory_order_relaxed); }
    if (sweepRefused) { gSweepRefused.fetch_add(1, std::memory_order_relaxed); }
    if (result == 0) {
        const int mm = Mm(std::hypot(applied.x, applied.y));
        gMoved.fetch_add(1, std::memory_order_relaxed);
        gMovedMm.fetch_add(static_cast<unsigned long long>(mm), std::memory_order_relaxed);
        gLastMoveMm.store(mm, std::memory_order_relaxed);
        if (nudge != nullptr) { gNudges.fetch_add(1, std::memory_order_relaxed); }
        std::lock_guard lock(gViewMutex);
        gMovedPending = true;
        gMovedPos = movedTo;
        gMovedEntity = entity;
        if (nudge != nullptr) {
            gNudgeWatch = 300;
            gNudgePos = movedTo;
        }
    } else if (result == 1) {
        gNoReference.fetch_add(1, std::memory_order_relaxed);
    } else if (result == 2) {
        gRoomBusy.fetch_add(1, std::memory_order_relaxed);
    } else if (result == 4) {
        gPositionOff.fetch_add(1, std::memory_order_relaxed);
    }
    if (result == 0 && nudge != nullptr) {
        Log("result=0 detail=nudge_done movedMm=" + std::to_string(Mm(std::hypot(applied.x, applied.y))));
    } else if (nudge != nullptr) {
        Log("result=" + std::to_string(result) + " detail=nudge_refused blocked=" + std::to_string(blocked) +
            " sweepRefused=" + std::to_string(sweepRefused));
    }
    return result;
}

// The head-in-wall layer (docs/ROOMSCALE-2026-10-11.md): is the head through
// geometry? The segment from the capsule's axis at head height to the head --
// inside the capsule at one end, so a wall between them is one the head went
// through -- or a head far from a body the sweep stopped.
void HeadCheck(void* player, const GameplayPoseFrame& pose, float residual)
{
    bool hit = false;
    void* const entity = PlayerEntity(player);
    Vec3 feet{};
    if (entity != nullptr && pose.cameraCentreValid && CallGetPos(gGetPos.load(std::memory_order_acquire), entity, feet)) {
        const Vec3 head = pose.cameraCentre;
        const Vec3 axis{feet.x, feet.y, head.z};
        const float span = std::hypot(head.x - axis.x, head.y - axis.y);
        if (span > 2.0f) {
            hit = true;
        } else if (span >= 0.01f) {
            scene::Hit h{};
            hit = QuerySegment(pose, axis, head, h) && h.distance >= 0.0f;
        }
    }
    if (hit) { gHeadHits.fetch_add(1, std::memory_order_relaxed); }
    const float away = std::isfinite(residual) ? residual : 0.0f;
    const bool wasOut = gHeadOut.load(std::memory_order_relaxed);
    const bool out = hit || away > (wasOut ? kInResidual : kOutResidual);
    if (out && !wasOut) { gHeadOutEvents.fetch_add(1, std::memory_order_relaxed); }
    if (out) { gHeadOutFrames.fetch_add(1, std::memory_order_relaxed); }
    gHeadOut.store(out, std::memory_order_release);
    gHeadOutNs.store(MonotonicNanoseconds(), std::memory_order_release);
}

void Tick(void* player)
{
    gFollowSeq.store(gSequence.fetch_add(1) + 1);
    gFollowThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    gTicks.fetch_add(1, std::memory_order_relaxed);
    if (!gActive.load(std::memory_order_acquire) || gFault.load(std::memory_order_acquire) || player == nullptr) {
        return;
    }
    std::uintptr_t vtable = 0;
    if (!ReadAt(player, 0, vtable) || vtable != gVtable.load(std::memory_order_relaxed)) {
        gNotPlayer.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // The torso, every frame the head is tracked -- followed or not, the arm
    // solver and the body slots share it.
    TrackingFrame frame{};
    const std::uint64_t now = MonotonicNanoseconds();
    if (!TryGetTrackingFrame(frame) || !FreshSample(now, frame.publishedNs) ||
        !IsPoseUsable(frame.head, frame.headValidity, kMaxPoseAgeNs)) {
        gNoHead.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // A recenter faces the body the new forward; a rebase redefines the space.
    const auto epoch = HeadTrackingRecenterEpoch();
    if (epoch != gTorsoEpoch || gResetTorso.exchange(false)) {
        gTorso.Reset();
        gRoomFollower.Reset();
        gTorsoEpoch = epoch;
    }
    const float dt = gTorsoNs != 0 && now > gTorsoNs ? static_cast<float>(now - gTorsoNs) * 1e-9f : 0.0f;
    gTorsoNs = now;
    const float head = armpose::HeadYaw(frame.head.orientation);
    const float torso = gTorso.Update(head, dt, gDeadzoneMilliDeg.load(std::memory_order_relaxed) * 1e-3f * kRadians,
                                      gRelaxPerMille.load(std::memory_order_relaxed) * 1e-3f);
    gTorsoSample.Publish(TorsoSample{torso, now});
    gHeadMdeg.store(Mdeg(head), std::memory_order_relaxed);
    gTorsoMdeg.store(Mdeg(torso), std::memory_order_relaxed);

    // Diagnostics run when due, in gameplay; until then they stay pending.
    int requestMdeg = gTurnRequestMdeg.load(std::memory_order_acquire);
    if (requestMdeg != 0 && now > gTurnDueNs.load() + kRequestLifeNs) {
        gTurnRequestMdeg.store(0, std::memory_order_release);
        Log("result=expired detail=turn");
        requestMdeg = 0;
    }
    const bool turnDue = requestMdeg != 0 && now >= gTurnDueNs.load();
    bool nudge = gNudgePending.load(std::memory_order_acquire);
    if (nudge && now > gNudgeDueNs.load() + kRequestLifeNs) {
        gNudgePending.store(false, std::memory_order_release);
        Log("result=expired detail=nudge");
        nudge = false;
    }
    const bool nudgeDue = nudge && now >= gNudgeDueNs.load();
    const bool following = gEnabled.load(std::memory_order_acquire);
    const bool roomScale = gRoomScale.load(std::memory_order_acquire) && gRoomAvailable.load(std::memory_order_acquire);
    if (!roomScale) { gHeadOut.store(false, std::memory_order_release); }
    if (!turnDue && !nudgeDue && !following && !roomScale) { return; }
    if (!InGameplay(player)) {
        gNotGameplay.fetch_add(1, std::memory_order_relaxed);
        gRoomFollower.Reset();
        gHeadOut.store(false, std::memory_order_release);
        return;
    }
    // The turn first: it changes G and R together and leaves the play space,
    // and so the head's offset, alone.
    if (turnDue) {
        gTurnRequestMdeg.store(0, std::memory_order_release);
        const int result = Turn(player, torso, requestMdeg * 1e-3f * kRadians, false);
        Log("result=" + std::to_string(result) + " detail=turn_done degrees=" + std::to_string(requestMdeg / 1000) +
            " appliedMdeg=" + std::to_string(gLastDeltaMdeg.load()));
    }
    if (following && !gFault.load(std::memory_order_acquire)) { Turn(player, torso, 0.0f, true); }

    if ((!roomScale && !nudgeDue) || !gRoomAvailable.load(std::memory_order_acquire) ||
        gFault.load(std::memory_order_acquire) || !HudGameplayInputAllowed()) {
        gRoomFollower.Reset();
        gHeadOut.store(false, std::memory_order_release);
        return;
    }
    float playSpaceYaw = 0.0f;
    if (!ViewPlaySpaceYaw(playSpaceYaw)) {
        gNoView.fetch_add(1, std::memory_order_relaxed);
        gRoomFollower.Reset();
        return;
    }
    GameplayPoseFrame pose{};
    if (!TryGetGameplayPoseFrame(pose, true) || pose.player != reinterpret_cast<std::uintptr_t>(player)) {
        gNoPose.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    float residual = std::numeric_limits<float>::quiet_NaN();
    // The player's living body, for the moves below (fixes 1 and 2).
    void* physics = nullptr;
    std::uintptr_t world = 0;
    Living living{};
    if (gPhysicsAvailable.load(std::memory_order_acquire) && gPhysics.load(std::memory_order_acquire)) {
        std::uintptr_t table = 0;
        void* candidate = nullptr;
        if (SceneQueryWorld(pose, world, candidate) == ERROR_SUCCESS && candidate != nullptr &&
            ReadAt(candidate, 0, table) && table == gLivingVtable.load(std::memory_order_relaxed) &&
            ReadLiving(candidate, living)) {
            physics = candidate;
            LogDimensions(physics);
        } else {
            gLivingUnread.fetch_add(1, std::memory_order_relaxed);
        }
    }
    // The engine is moving the capsule: leave it be (body.standaside). A
    // SetPos while the stick walked the player stalled the walk (second
    // headset run); with the living body's moves keeping its motion, standing
    // aside is a choice rather than a cure.
    bool nativeMoving = false;
    if (void* const entity = PlayerEntity(player)) {
        Vec3 feet{};
        if (CallGetPos(gGetPos.load(std::memory_order_acquire), entity, feet)) {
            if (gLastFeetNs != 0 && now > gLastFeetNs && now - gLastFeetNs < kTorsoFreshNs) {
                const float seconds = static_cast<float>(now - gLastFeetNs) * 1e-9f;
                const float travelled = std::sqrt((feet.x - gLastFeet.x) * (feet.x - gLastFeet.x) +
                                                  (feet.y - gLastFeet.y) * (feet.y - gLastFeet.y) +
                                                  (feet.z - gLastFeet.z) * (feet.z - gLastFeet.z));
                nativeMoving = travelled > kNativeMovingSpeed * seconds;
                // Moved sideways with nothing moving it -- at rest, no walk
                // asked for, and our own moves taken out (gLastFeet is where
                // SetPos left it): the engine pushing it out of something.
                const float sideways = std::hypot(feet.x - gLastFeet.x, feet.y - gLastFeet.y);
                if (physics != nullptr && sideways > kPushOut &&
                    !roomscale::BodyMoving(living.velocity, living.requested)) {
                    gPushOut.fetch_add(1, std::memory_order_relaxed);
                    StoreMax(gPushOutMaxMm, Mm(sideways));
                }
            }
            gLastFeet = feet;
            gLastFeetNs = now;
        }
    }
    const bool standAside = nativeMoving && gStandAside.load(std::memory_order_relaxed);
    if (nativeMoving) { gNativeMoving.fetch_add(1, std::memory_order_relaxed); }
    if (standAside) { gRoomFollower.Reset(); }
    // The body follows the neck, not the eyes.
    const Pose followHead{frame.head.orientation, roomscale::FollowPoint(frame.head, torso)};
    if (nudgeDue) {
        gNudgePending.store(false, std::memory_order_release);
        // Right and forward of the body's facing.
        Quaternion facing{};
        const auto yaw = CallGet(gGet.load(std::memory_order_acquire), player, &facing)
                             ? bodyfollow::EngineYawOf(facing) : std::nullopt;
        if (yaw) {
            const float right = gNudgeRightMm.load() / 1000.0f;
            const float forward = gNudgeForwardMm.load() / 1000.0f;
            const float c = std::cos(*yaw), sn = std::sin(*yaw);
            // A yaw of t sends +X to (cos t, sin t) and +Y to (-sin t, cos t).
            const Vec3 move{right * c - forward * sn, right * sn + forward * c, 0.0f};
            Move(player, pose, playSpaceYaw, followHead, dt, &move, world, physics, living, residual);
        }
    }
    if (roomScale) {
        if (!standAside) { Move(player, pose, playSpaceYaw, followHead, dt, nullptr, world, physics, living, residual); }
        HeadCheck(player, pose, residual);
    } else {
        gRoomFollower.Reset();
    }
}

void __fastcall PlayerUpdateDetour(void* player, void* context, int slot)
{
    // Before the engine's own update: the frame's view, and if the order holds
    // its arm IK, see the turned body.
    //
    // A snap queued by last frame's render hook lands first, so the follower
    // below turns the body by it in this same frame, and the view and the IK
    // after it see one reference (HeadTrackingHook.h QueueSnapTurn).
    ApplyQueuedSnapTurn(false);
    Tick(player);
    const PlayerUpdateFn original = gOriginal.load(std::memory_order_acquire);
    if (original != nullptr) { original(player, context, slot); }
}

// The living body's calls (fixes 1 and 2): the bodies RoomScaleNative.h pins,
// the living and base GetStatus and GetParams rows ContactNative.h pins, and
// the living vtable's SetParams, GetStatus and GetParams slots.
static_assert(contract::Gated(rn::Contracts, rn::LivingSetParams) &&
              contract::Gated(rn::Contracts, rn::PhysicalSetParams) &&
              contract::Gated(rn::Contracts, rn::LivingStanceOverlap) &&
              contract::Gated(rn::Contracts, rn::PhysicsProxyXForm) &&
              contract::Gated(rn::Contracts, rn::PhysicsChangeRequest));
static_assert(contract::Gated(cn::Contracts, cn::LivingGetStatus) && contract::Gated(cn::Contracts, cn::StaticGetStatus) &&
              contract::Gated(cn::Contracts, cn::LivingGetParams) && contract::Gated(cn::Contracts, cn::StaticGetParams));
bool GatePhysics(std::uintptr_t base)
{
    __try {
        if (!contract::BodiesMatch(base, rn::Contracts)) { return false; }
        for (const auto& row : cn::Contracts) {
            const bool living = row.rva == cn::LivingGetStatus || row.rva == cn::StaticGetStatus ||
                                row.rva == cn::LivingGetParams || row.rva == cn::StaticGetParams;
            if (living && !contract::BodyMatches(base, row)) { return false; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return SlotIs(base, cn::LivingVtable, rn::SetParamsSlot / 8, rn::LivingSetParams) &&
           SlotIs(base, cn::LivingVtable, cn::StatusSlot / 8, cn::LivingGetStatus) &&
           SlotIs(base, cn::LivingVtable, cn::ParamsSlot / 8, cn::LivingGetParams);
}

DWORD Install()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstalled.load(std::memory_order_acquire)) { return 0; }
    const auto base = PreyBase();
    if (base == 0) { return 1; }
    const char* mismatch = nullptr;
    if (!BytesMatch(base + kPlayerUpdateRva, kPlayerUpdatePrologue)) { mismatch = "update_prologue"; }
    else if (!BytesMatch(base + kSetViewRotationRva, kSetViewRotationBody)) { mismatch = "set_view_rotation_body"; }
    else if (!BytesMatch(base + kGetViewRotationRva, kGetViewRotationBody)) { mismatch = "get_view_rotation_body"; }
    else if (!BytesMatch(base + kComponentCopyRva, kComponentCopyBody)) { mismatch = "component_copy_body"; }
    else if (!SlotIs(base, kPlayerVtableRva, kUpdateSlot, kPlayerUpdateRva) ||
             !SlotIs(base, kPlayerVtableRva, kSetSlot, kSetViewRotationRva) ||
             !SlotIs(base, kPlayerVtableRva, kGetSlot, kGetViewRotationRva)) {
        mismatch = "player_vtable";
    }
    if (mismatch != nullptr) {
        Log(std::string("result=unavailable detail=") + mismatch);
        return 2;
    }
    const char* roomMismatch = nullptr;
    if (!BytesMatch(base + kSetPosRva, kSetPosBody)) { roomMismatch = "set_pos_body"; }
    else if (!BytesMatch(base + kGetPosRva, kGetPosBody)) { roomMismatch = "get_pos_body"; }
    else if (!SlotIs(base, kEntityVtableRva, kSetPosSlot, kSetPosRva) ||
             !SlotIs(base, kEntityVtableRva, kGetPosSlot, kGetPosRva) ||
             !SlotIs(base, kEntityVtableRva, kGetPhysicsSlot, kGetPhysicsRva)) {
        roomMismatch = "entity_vtable";
    }
    if (roomMismatch != nullptr) {
        Log(std::string("result=unavailable detail=room_scale_") + roomMismatch + " turning=available");
    } else {
        gSetPos.store(reinterpret_cast<SetPosFn>(base + kSetPosRva), std::memory_order_release);
        gGetPos.store(reinterpret_cast<GetPosFn>(base + kGetPosRva), std::memory_order_release);
        gEntityVtable.store(base + kEntityVtableRva, std::memory_order_release);
        gRoomAvailable.store(true, std::memory_order_release);
        if (GatePhysics(base)) {
            gLivingVtable.store(base + cn::LivingVtable, std::memory_order_release);
            gPhysicsAvailable.store(true, std::memory_order_release);
        } else {
            Log("result=unavailable detail=room_scale_physics fallback=rays_and_setpos");
        }
    }
    gSet.store(reinterpret_cast<SetViewRotationFn>(base + kSetViewRotationRva), std::memory_order_release);
    gGet.store(reinterpret_cast<GetViewRotationFn>(base + kGetViewRotationRva), std::memory_order_release);
    gVtable.store(base + kPlayerVtableRva, std::memory_order_release);
    void* const target = reinterpret_cast<void*>(base + kPlayerUpdateRva);
    const auto installed = hook::Install(target, reinterpret_cast<void*>(&PlayerUpdateDetour), gOriginal);
    if (!installed) {
        Log(std::string("result=failed detail=") + hook::StepName(installed.step) + " target=ArkPlayer::Update");
        return 3;
    }
    gInstalled.store(true, std::memory_order_release);
    Log(std::string("result=0 detail=hook_installed target=ArkPlayer::Update setter=0x1584CC0 roomScale=") +
        (gRoomAvailable.load() ? "available" : "unavailable") +
        " physics=" + (gPhysicsAvailable.load() ? "available" : "unavailable"));
    return 0;
}

void ResetStats()
{
    for (Counter* c : {&gApplied, &gTurns, &gNotPlayer, &gNoHead, &gNotGameplay, &gNoReference, &gBusy,
                       &gNativeRefused, &gTurnedMdeg, &gViewAfterFollow, &gViewWithoutFollow, &gThreadMismatch,
                       &gIkBetween, &gIkAfterView, &gIkAfterRender, &gStepFollowed, &gStepIdle, &gMoved,
                       &gMovedMm, &gBlocked, &gSweepRefused, &gRoomBusy, &gPositionOff, &gNoView, &gNoPose,
                       &gJumps, &gNudges, &gViewDrift, &gHeadOutEvents, &gHeadOutFrames, &gHeadHits,
                       &gNativeMoving, &gLivingUnread, &gFitChecks, &gFitShortened, &gFitVertical,
                       &gFitStartBlocked, &gKeptMotion, &gRestMoves, &gStepSettling, &gPushOut,
                       &gDeferredBusy}) {
        c->store(0, std::memory_order_relaxed);
    }
    gPushOutMaxMm.store(0, std::memory_order_relaxed);
    gBodyLagMaxMm.store(0, std::memory_order_relaxed);
    gReadbackMaxMdeg.store(0, std::memory_order_relaxed);
    gStepFollowedMaxMdeg.store(0, std::memory_order_relaxed);
    gStepIdleMaxMdeg.store(0, std::memory_order_relaxed);
    gViewDriftMaxMm.store(0, std::memory_order_relaxed);
    gNudgeDriftMaxMm.store(0, std::memory_order_relaxed);
}

} // namespace

DWORD SetBodyFollowActive(unsigned active)
{
    if (active > 1) { return ERROR_INVALID_PARAMETER; }
    if (active != 0) {
        const DWORD installed = Install();
        if (installed != 0) { return installed; }
        ResetStats();
        gResetTorso.store(true, std::memory_order_release);
    } else {
        gTorsoSample.Clear();
    }
    gActive.store(active != 0, std::memory_order_release);
    Log(std::string("result=0 detail=active value=") + (active ? "1" : "0"));
    return 0;
}

DWORD SetBodyFollow(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    if ((enabled != 0) != gEnabled.load()) { ResetStats(); }
    gEnabled.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=follow value=") + (enabled ? "1" : "0"));
    return 0;
}

unsigned BodyFollowEnabled() { return gEnabled.load(std::memory_order_relaxed) ? 1u : 0u; }

DWORD SetBodyFollowLevel(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gLevel.store(enabled != 0, std::memory_order_release);
    return 0;
}

DWORD SetBodyTorso(int deadzoneDegrees, int relaxPerMille)
{
    if (deadzoneDegrees < 0 || deadzoneDegrees > 180 || relaxPerMille < 0 || relaxPerMille > 10000) {
        return ERROR_INVALID_PARAMETER;
    }
    gDeadzoneMilliDeg.store(deadzoneDegrees * 1000, std::memory_order_relaxed);
    gRelaxPerMille.store(relaxPerMille, std::memory_order_relaxed);
    return 0;
}

DWORD SetRoomScale(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    if ((enabled != 0) != gRoomScale.load()) { ResetStats(); }
    gRoomScale.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=room_scale value=") + (enabled ? "1" : "0"));
    return 0;
}

unsigned RoomScaleEnabled() { return gRoomScale.load(std::memory_order_relaxed) ? 1u : 0u; }

DWORD SetRoomScaleZone(int startMm, int stopMm, int followMs)
{
    if (startMm < 5 || startMm > 500 || stopMm < 0 || stopMm >= startMm || followMs > 1000) {
        return ERROR_INVALID_PARAMETER;
    }
    gStartMm.store(startMm, std::memory_order_relaxed);
    gStopMm.store(stopMm, std::memory_order_relaxed);
    if (followMs >= 0) { gFollowMs.store(followMs, std::memory_order_relaxed); }
    return 0;
}

int RoomScaleStartMm() { return gStartMm.load(std::memory_order_relaxed); }
int RoomScaleStopMm() { return gStopMm.load(std::memory_order_relaxed); }
int RoomScaleFollowMs() { return gFollowMs.load(std::memory_order_relaxed); }

DWORD SetRoomScalePhysics(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    if ((enabled != 0) != gPhysics.load()) { ResetStats(); }
    gPhysics.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=room_scale_physics value=") + (enabled ? "1" : "0") +
        " available=" + (gPhysicsAvailable.load() ? "1" : "0"));
    return 0;
}

DWORD SetRoomScaleStandAside(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gStandAside.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=stand_aside value=") + (enabled ? "1" : "0"));
    return 0;
}

DWORD SetRoomScaleRadius(int radiusMm)
{
    if (radiusMm < 100 || radiusMm > 600) { return ERROR_INVALID_PARAMETER; }
    gRadiusMm.store(radiusMm, std::memory_order_relaxed);
    return 0;
}

DWORD RequestBodyNudge(int rightCm, int forwardCm, int delaySeconds)
{
    if ((rightCm == 0 && forwardCm == 0) || std::abs(rightCm) > 100 || std::abs(forwardCm) > 100 ||
        delaySeconds < 0 || delaySeconds > 60) {
        return ERROR_INVALID_PARAMETER;
    }
    if (!gActive.load(std::memory_order_acquire) || !gRoomAvailable.load(std::memory_order_acquire)) {
        return ERROR_NOT_READY;
    }
    gNudgeRightMm.store(rightCm * 10, std::memory_order_relaxed);
    gNudgeForwardMm.store(forwardCm * 10, std::memory_order_relaxed);
    gNudgeDueNs.store(MonotonicNanoseconds() + static_cast<std::uint64_t>(delaySeconds) * 1000000000ull);
    gNudgePending.store(true, std::memory_order_release);
    return 0;
}

DWORD SetBodyFollowMode(unsigned mode)
{
    if (mode > 2) { return ERROR_INVALID_PARAMETER; }
    SetBodyFollow(mode != 0 ? 1u : 0u);
    SetRoomScale(mode == 2 ? 1u : 0u);
    return 0;
}

unsigned BodyFollowMode()
{
    if (!gEnabled.load(std::memory_order_relaxed)) { return 0; }
    return gRoomScale.load(std::memory_order_relaxed) ? 2u : 1u;
}

DWORD RequestBodyTurn(int degrees, int delaySeconds)
{
    if (degrees == 0 || degrees < -180 || degrees > 180 || delaySeconds < 0 || delaySeconds > 60) {
        return ERROR_INVALID_PARAMETER;
    }
    if (!gActive.load(std::memory_order_acquire)) { return ERROR_NOT_READY; }
    gTurnDueNs.store(MonotonicNanoseconds() + static_cast<std::uint64_t>(delaySeconds) * 1000000000ull);
    gTurnRequestMdeg.store(degrees * 1000, std::memory_order_release);
    return 0;
}

bool BodyHeadOut()
{
    return gActive.load(std::memory_order_acquire) && gHeadOut.load(std::memory_order_acquire) &&
           FreshSample(MonotonicNanoseconds(), gHeadOutNs.load(std::memory_order_acquire), kTorsoFreshNs);
}

bool BodyTorsoYaw(float& yaw)
{
    if (!gActive.load(std::memory_order_acquire)) { return false; }
    TorsoSample sample{};
    if (!gTorsoSample.TryRead(sample) || !FreshSample(MonotonicNanoseconds(), sample.ns, kTorsoFreshNs)) {
        return false;
    }
    yaw = sample.yaw;
    return true;
}

void NoteBodyFollowView(float playSpaceYaw, unsigned long long referenceGeneration)
{
    const auto sequence = gSequence.fetch_add(1) + 1;
    const auto previousView = gViewSeq.exchange(sequence);
    if (!gActive.load(std::memory_order_acquire)) { return; }
    (gFollowSeq.load() > previousView ? gViewAfterFollow : gViewWithoutFollow).fetch_add(1, std::memory_order_relaxed);
    const auto follower = gFollowThread.load(std::memory_order_relaxed);
    if (follower != 0 && follower != GetCurrentThreadId()) { gThreadMismatch.fetch_add(1, std::memory_order_relaxed); }
    if (std::isfinite(playSpaceYaw)) {
        gViewP.store(playSpaceYaw, std::memory_order_release);
        gViewPGeneration.store(referenceGeneration, std::memory_order_release);
        gViewPNs.store(MonotonicNanoseconds(), std::memory_order_release);
    }
    const bool turned = gTurnedSinceView.exchange(false, std::memory_order_acq_rel);
    std::lock_guard lock(gViewMutex);
    // Where the entity is now against where SetPos left it this frame, and
    // after a nudge for the next 300 views: a physics step computed from the
    // old position would put it back (docs/ROOMSCALE-2026-10-11.md, risk 1).
    const GetPosFn getPos = gGetPos.load(std::memory_order_acquire);
    if ((gMovedPending || gNudgeWatch > 0) && getPos != nullptr && gMovedEntity != nullptr) {
        Vec3 now{};
        if (CallGetPos(getPos, gMovedEntity, now)) {
            if (gMovedPending) {
                const int drift = Mm(std::hypot(now.x - gMovedPos.x, now.y - gMovedPos.y));
                StoreMax(gViewDriftMaxMm, drift);
                if (drift > 5) { gViewDrift.fetch_add(1, std::memory_order_relaxed); }
            }
            if (gNudgeWatch > 0) {
                StoreMax(gNudgeDriftMaxMm, Mm(std::hypot(now.x - gNudgePos.x, now.y - gNudgePos.y)));
                --gNudgeWatch;
            }
        }
    }
    gMovedPending = false;
    if (gHavePrevious && referenceGeneration == gPreviousGeneration && std::isfinite(playSpaceYaw)) {
        const int step = std::abs(Mdeg(bodyfollow::Wrap(playSpaceYaw - gPreviousP)));
        if (turned) {
            StoreMax(gStepFollowedMaxMdeg, step);
            gStepFollowed.fetch_add(1, std::memory_order_relaxed);
        } else {
            StoreMax(gStepIdleMaxMdeg, step);
            gStepIdle.fetch_add(1, std::memory_order_relaxed);
        }
    }
    gHavePrevious = std::isfinite(playSpaceYaw);
    gPreviousP = playSpaceYaw;
    gPreviousGeneration = referenceGeneration;
}

void NoteBodyFollowIk()
{
    if (!gActive.load(std::memory_order_acquire)) { return; }
    const auto follow = gFollowSeq.load(), view = gViewSeq.load(), render = gRenderSeq.load();
    // After the follower and before the view: this frame's IK saw the turned
    // body. After the view: still this frame's turned body. After the render
    // hook: either this frame's IK running late or the next frame's running
    // before its follower -- the case a snap turn shows as a hand flash.
    if (follow > view && follow > render) { gIkBetween.fetch_add(1, std::memory_order_relaxed); }
    else if (view > follow && view > render) { gIkAfterView.fetch_add(1, std::memory_order_relaxed); }
    else { gIkAfterRender.fetch_add(1, std::memory_order_relaxed); }
}

void NoteBodyFollowRender() { gRenderSeq.store(gSequence.fetch_add(1) + 1); }

std::string BodyFollowReport()
{
    std::ostringstream out;
    out << " active=" << gActive.load() << " follow=" << gEnabled.load() << " level=" << gLevel.load()
        << " hooked=" << gInstalled.load() << " fault=" << gFault.load()
        << " torsoDeadzoneDeg=" << gDeadzoneMilliDeg.load() / 1000.0f
        << " torsoRelax=" << gRelaxPerMille.load() / 1000.0f
        << " ticks=" << gTicks.load() << " applied=" << gApplied.load() << " turns=" << gTurns.load()
        << " turnedDeg=" << gTurnedMdeg.load() / 1000 << " lastDeltaMdeg=" << gLastDeltaMdeg.load()
        << " readbackMaxMdeg=" << gReadbackMaxMdeg.load()
        << " headDeg=" << gHeadMdeg.load() / 1000.0f << " torsoDeg=" << gTorsoMdeg.load() / 1000.0f
        << " referenceDeg=" << HeadTrackingReferenceYaw() * kDegrees
        << " notPlayer=" << gNotPlayer.load() << " noHead=" << gNoHead.load()
        << " notGameplay=" << gNotGameplay.load() << " cameraMode=" << gCameraMode.load()
        << " noReference=" << gNoReference.load() << " busy=" << gBusy.load()
        << " nativeRefused=" << gNativeRefused.load() << " faults=" << gFaults.load()
        << " viewAfterFollow=" << gViewAfterFollow.load() << " viewWithoutFollow=" << gViewWithoutFollow.load()
        << " threadMismatch=" << gThreadMismatch.load()
        << " stepFollowed=" << gStepFollowed.load() << " stepFollowedMaxMdeg=" << gStepFollowedMaxMdeg.load()
        << " stepIdle=" << gStepIdle.load() << " stepIdleMaxMdeg=" << gStepIdleMaxMdeg.load()
        << " ikBetween=" << gIkBetween.load() << " ikAfterView=" << gIkAfterView.load()
        << " ikAfterRender=" << gIkAfterRender.load()
        << " roomReadFallbacks=" << HeadTrackingRoomReadFallbacks()
        << " roomScale=" << gRoomScale.load() << " roomAvailable=" << gRoomAvailable.load()
        << " zoneMm=" << gStartMm.load() << '/' << gStopMm.load() << " followMs=" << gFollowMs.load()
        << " radiusMm=" << gRadiusMm.load()
        << " moved=" << gMoved.load() << " movedM=" << gMovedMm.load() / 1000.0
        << " lastMoveMm=" << gLastMoveMm.load() << " residualMm=" << gResidualMm.load()
        << " blocked=" << gBlocked.load() << " sweepRefused=" << gSweepRefused.load()
        << " roomBusy=" << gRoomBusy.load() << " positionOff=" << gPositionOff.load()
        << " noView=" << gNoView.load() << " noPose=" << gNoPose.load() << " jumps=" << gJumps.load()
        << " viewDrift=" << gViewDrift.load() << " viewDriftMaxMm=" << gViewDriftMaxMm.load()
        << " nudges=" << gNudges.load() << " nudgeDriftMaxMm=" << gNudgeDriftMaxMm.load()
        << " pendingTurnDeg=" << gTurnRequestMdeg.load() / 1000 << " pendingNudge=" << gNudgePending.load()
        << " headOut=" << BodyHeadOut() << " headOutEvents=" << gHeadOutEvents.load()
        << " headOutFrames=" << gHeadOutFrames.load() << " headHits=" << gHeadHits.load()
        << " fadeFrames=" << XrFadeFrames() << " nativeMoving=" << gNativeMoving.load()
        << " standAside=" << gStandAside.load() << " physics=" << gPhysics.load()
        << " physicsAvailable=" << gPhysicsAvailable.load() << " livingUnread=" << gLivingUnread.load()
        << " fitChecks=" << gFitChecks.load() << " fitShortened=" << gFitShortened.load()
        << " fitVertical=" << gFitVertical.load() << " fitStartBlocked=" << gFitStartBlocked.load()
        << " keptMotion=" << gKeptMotion.load() << " restMoves=" << gRestMoves.load()
        << " stepSettling=" << gStepSettling.load() << " pushOut=" << gPushOut.load()
        << " pushOutMaxMm=" << gPushOutMaxMm.load() << " deferredBusy=" << gDeferredBusy.load()
        << " bodyLagMaxMm=" << gBodyLagMaxMm.load();
    return out.str();
}

} // namespace preyvr::dll
