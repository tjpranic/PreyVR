#include "body/RoomScaleLane.h"

#include "engine/EngineAccess.h"
#include "aim/GameplayFrame.h"
#include "render/camera/HeadTrackingHook.h"
#include "base/Logger.h"
#include "controls/locomotion/MoveLane.h"
#include "engine/SafeMemory.h"
#include "physics/contact/SceneQueryLane.h"
#include "ui/screens/UiMode.h"
#include "controls/input/XrInput.h"
#include "xr/XrSessionHost.h"
#include "body/ArmPose.h"
#include "physics/contact/ContactNative.h"
#include "base/MonotonicClock.h"
#include "engine/NativeContract.h"
#include "body/RoomScale.h"
#include "body/RoomScaleNative.h"
#include "render/camera/StereoCamera.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>

namespace preyvr::dll {
namespace {

namespace rn = roomscale::native;
namespace cn = contact::native;

void Log(const std::string& line) { lifecycle::Log("preyvr_room " + line); }

// The player entity's position (tools/re/verify_view_rotation.py): the concrete
// CEntity vtable, the one holding SceneQuery's hash-gated GetPhysics (+0x228).
// SetPos(pos, whyFlags, recalcPhysics, force) at +0x108 stores m_vPos and
// dispatches the transform update; GetPos at +0x110 returns &m_vPos. The game's
// own stance change moves the player with this pair.
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

// The living body's calls: the bodies RoomScaleNative.h pins, the living and
// base GetStatus and GetParams rows ContactNative.h pins, and the living
// vtable's SetParams, GetStatus and GetParams slots. Checked before the first.
static_assert(contract::Gated(rn::Contracts, rn::LivingSetParams) &&
              contract::Gated(rn::Contracts, rn::PhysicalSetParams) &&
              contract::Gated(rn::Contracts, rn::LivingStanceOverlap) &&
              contract::Gated(rn::Contracts, rn::PhysicsProxyXForm) &&
              contract::Gated(rn::Contracts, rn::PhysicsChangeRequest));
static_assert(contract::Gated(cn::Contracts, cn::LivingGetStatus) && contract::Gated(cn::Contracts, cn::StaticGetStatus) &&
              contract::Gated(cn::Contracts, cn::LivingGetParams) && contract::Gated(cn::Contracts, cn::StaticGetParams));
static_assert(cn::LivingStatusSize <= 0x100 && rn::LivingRequested + sizeof(Vec3) <= cn::LivingStatusSize);

constexpr int kFitIterations = 6;          // within 1/64 of a move from the first contact
constexpr float kStepSettling = 0.001f;    // m of step-up smoothing: the body is climbing
// A walk's first frames read as rest -- the body's velocity and request are
// last frame's -- so within this long of the stick being pushed it counts as
// moving: the teleport there started the walk airborne (docs/BODY-FOLLOW.md).
constexpr std::uint64_t kWalkStartNs = 150000000ull;
constexpr std::uint64_t kFreshNs = 100000000ull;    // the frame's gameplay sample
constexpr std::uint64_t kActiveNs = 200000000ull;   // RoomScaleActive, BodyHeadOut
// The head-in-wall line starts on the capsule's axis this far below the eyes,
// within these heights above the feet: over a counter, under the head.
constexpr float kLineBelowEyes = 0.45f, kLineLowest = 0.3f, kLineHighest = 1.25f;

enum class Gate { unchecked, open, refused };
std::atomic<Gate> gGate{Gate::unchecked};
std::atomic<bool> gFault{false}, gHeadOut{false};
std::atomic<std::uint64_t> gHeadOutNs{0}, gActiveNs{0};
// Main thread, after the gate opened.
SetPosFn gSetPos = nullptr;
GetPosFn gGetPos = nullptr;
std::uintptr_t gEntityVtable = 0, gLivingVtable = 0;
roomscale::Follower gFollower;
// A recenter or calibration redefines the height upright was learnt against.
unsigned long long gUprightEpoch = ~0ull;
// Lean Zone (VR Options' lean_zone), cm: roomscale::Settings' leanZone.
constexpr unsigned kLeanZoneMaxCm = 50;
std::atomic<unsigned> gLeanZoneCm{25};

using Counter = std::atomic<unsigned long long>;
Counter gServices{0}, gNotGameplay{0}, gNoFrame{0}, gNotPlayer{0}, gNoLiving{0}, gMoved{0}, gMovedMm{0};
Counter gBlocked{0}, gRefused{0}, gStartOverlap{0}, gBusy{0}, gKeptMotion{0}, gStartKept{0}, gRestMoves{0};
Counter gStepSettling{0}, gJumps{0}, gRaced{0}, gReferenceBusy{0}, gHeadOutEvents{0}, gHeadOutFrames{0};
Counter gSettleMoves{0};
std::atomic<int> gLastMoveMm{0}, gResidualMm{0}, gDropMm{0}, gAllowanceMm{0}, gUprightMm{0};

int Mm(float metres) { return std::isfinite(metres) ? static_cast<int>(std::lround(metres * 1000.0f)) : 0; }

template<class T> bool ReadAt(const void* base, std::size_t offset, T& out)
{
    return SafeCopy(&out, static_cast<const std::uint8_t*>(base) + offset, sizeof(out));
}

bool BytesMatch(std::uintptr_t address, std::string_view hex)
{
    std::uint8_t bytes[0x80]{};
    const std::size_t size = hex.size() / 2;
    if (size == 0 || size > sizeof(bytes) || !SafeCopy(bytes, reinterpret_cast<const void*>(address), size)) {
        return false;
    }
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < size; ++i) {
        if (bytes[i] != static_cast<std::uint8_t>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]))) { return false; }
    }
    return true;
}

bool SlotIs(std::uintptr_t base, std::uintptr_t vtableRva, unsigned slot, std::uintptr_t rva)
{
    std::uintptr_t value = 0;
    return SafeRead(base + vtableRva + slot * sizeof(value), value) && value == base + rva;
}

template <class Fn> Fn Slot(void* object, std::size_t offset)
{
    return *reinterpret_cast<const Fn*>(*static_cast<const std::uintptr_t*>(object) + offset);
}

bool PhysicsMatches(std::uintptr_t base)
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

// Once, before the first call: the entity's pair and the living body's calls.
bool OpenGate()
{
    const Gate gate = gGate.load(std::memory_order_acquire);
    if (gate != Gate::unchecked) { return gate == Gate::open; }
    const std::uintptr_t base = PreyBase();
    const char* mismatch = nullptr;
    if (base == 0) { mismatch = "no_preydll"; }
    else if (!BytesMatch(base + kSetPosRva, kSetPosBody)) { mismatch = "set_pos_body"; }
    else if (!BytesMatch(base + kGetPosRva, kGetPosBody)) { mismatch = "get_pos_body"; }
    else if (!SlotIs(base, kEntityVtableRva, kSetPosSlot, kSetPosRva) || !SlotIs(base, kEntityVtableRva, kGetPosSlot, kGetPosRva) ||
             !SlotIs(base, kEntityVtableRva, kGetPhysicsSlot, kGetPhysicsRva)) {
        mismatch = "entity_vtable";
    } else if (!PhysicsMatches(base)) {
        mismatch = "living_body";
    }
    if (mismatch != nullptr) {
        gGate.store(Gate::refused, std::memory_order_release);
        Log(std::string("result=unavailable detail=") + mismatch);
        return false;
    }
    gSetPos = reinterpret_cast<SetPosFn>(base + kSetPosRva);
    gGetPos = reinterpret_cast<GetPosFn>(base + kGetPosRva);
    gEntityVtable = base + kEntityVtableRva;
    gLivingVtable = base + cn::LivingVtable;
    gGate.store(Gate::open, std::memory_order_release);
    Log("result=0 detail=available");
    return true;
}

// A fault in engine code is not assumed recoverable: room-scale stays off for
// the rest of the process, and the capsule where the game puts it.
void Fault(const char* where)
{
    gHeadOut.store(false, std::memory_order_release);
    if (!gFault.exchange(true)) { Log(std::string("result=fault detail=") + where + " room_scale=off_for_process"); }
}

void* PlayerEntity(void* player)
{
    void* entity = nullptr;
    std::uintptr_t table = 0;
    if (!ReadAt(player, kPlayerEntity, entity) || entity == nullptr || !ReadAt(entity, 0, table) ||
        table != gEntityVtable) {
        return nullptr;
    }
    return entity;
}

bool CallGetPos(void* entity, Vec3& out)
{
    __try {
        out = *gGetPos(entity);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallSetPos(void* entity, const Vec3* position)
{
    __try {
        gSetPos(entity, position, 0, false, false);   // as the stance change calls it
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// What the living body's movement is doing: its own velocity, the velocity its
// movement asks for (pe_action_move), and camOffset, non-zero while a step-up
// is being smoothed.
struct Living {
    Vec3 velocity{}, requested{}, stance{};
};

bool ReadLiving(void* physics, Living& out)
{
    alignas(16) std::uint8_t status[0x100]{};
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

// pe_status_check_stance at `position` (null: where the body stands), with the
// body's own rotation and collider: 0 when nothing overlaps, else the overlap's
// depth. False when the call faulted.
bool StanceDepth(void* physics, const Vec3* position, float& depth)
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
    int fits = 0;
    __try {
        using GetStatusFn = int(__fastcall*)(void*, void*);
        fits = Slot<GetStatusFn>(physics, cn::StatusSlot)(physics, check);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (fits != 0) {
        depth = 0.0f;
        return true;
    }
    std::memcpy(&depth, check + rn::StanceDepth, sizeof(depth));
    // An overlap with no depth to speak of is still one.
    if (!std::isfinite(depth) || depth <= 0.0f) { depth = 1.0f; }
    return true;
}

// pe_params_pos with the position alone and bRecalcBounds 0x21: the body moves,
// its bounds with it, and keeps its velocity, ground and airborne state. The
// entity's SetPos that follows sends the same position, which no longer moves
// it, so the engine's teleport is skipped as well.
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
        // bThreadSafe 0, as the entity's own physics proxy calls it.
        using SetParamsFn = int(__fastcall*)(void*, const void*, int);
        Slot<SetParamsFn>(physics, rn::SetParamsSlot)(physics, params, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// Whether a SetParams on the body applies at once rather than queued behind a
// physics step: the change request's own test (RoomScaleNative.h).
bool PhysicsIdle(std::uintptr_t world, void* physics)
{
    std::uint32_t stepLock = 1, stepBusy = 1, worldLock = 1, queued = rn::EntityQueuedUnit;
    const void* const w = reinterpret_cast<const void*>(world);
    return world != 0 && ReadAt(w, rn::WorldStepLock, stepLock) && ReadAt(w, rn::WorldStepBusy, stepBusy) &&
           ReadAt(w, rn::WorldLock, worldLock) && ReadAt(physics, rn::EntityQueued, queued) && stepLock == 0 &&
           stepBusy == 0 && worldLock == 0 && queued < rn::EntityQueuedUnit;
}

// The head in a wall: the line from the capsule's axis, below the eyes, to the
// head's pivot and on to the eyes, through anything. A lean over a desk passes
// above its top; a head pushed through a wall the capsule stopped at does not.
void HeadCheck(const GameplayPoseFrame& pose, void* entity)
{
    bool hit = false;
    Vec3 feet{};
    if (entity != nullptr && CallGetPos(entity, feet)) {
        const Vec3 eye = pose.cameraCentre;
        const Vec3 atlas = Add(eye, stereo::TrackingDirToEngine(Rotate(pose.tracking.head.orientation, armpose::kNeckFromEye),
                                                                pose.yaw));
        const float height = std::clamp(eye.z - feet.z - kLineBelowEyes, kLineLowest, kLineHighest);
        const Vec3 inside{feet.x, feet.y, feet.z + height};
        scene::Hit h{};
        hit = (QuerySegment(pose, inside, atlas, h) && h.distance >= 0.0f) ||
              (QuerySegment(pose, atlas, eye, h) && h.distance >= 0.0f);
    }
    if (hit && !gHeadOut.load(std::memory_order_relaxed)) { gHeadOutEvents.fetch_add(1, std::memory_order_relaxed); }
    if (hit) { gHeadOutFrames.fetch_add(1, std::memory_order_relaxed); }
    gHeadOut.store(hit, std::memory_order_release);
    gHeadOutNs.store(MonotonicNanoseconds(), std::memory_order_release);
}

void Stand()
{
    gFollower.Reset();
    gHeadOut.store(false, std::memory_order_release);
}

} // namespace

void ServiceRoomScale(void* player, float torsoYaw, float dt, bool gameplay)
{
    gServices.fetch_add(1, std::memory_order_relaxed);
    const auto epoch = HeadTrackingRecenterEpoch();
    if (epoch != gUprightEpoch) {
        gUprightEpoch = epoch;
        gFollower.ResetUpright();
    }
    if (gFault.load(std::memory_order_acquire) || player == nullptr) {
        Stand();
        return;
    }
    if (!gameplay || !HudGameplayInputAllowed()) {
        gNotGameplay.fetch_add(1, std::memory_order_relaxed);
        Stand();
        return;
    }
    if (!OpenGate()) { return; }
    const std::uint64_t now = MonotonicNanoseconds();
    GameplayPoseFrame pose{};
    if (!TryGetGameplayPoseFrame(pose, true) || pose.player != reinterpret_cast<std::uintptr_t>(player) ||
        !pose.cameraCentreValid || !FreshSample(now, pose.publishedNs, kFreshNs) || !std::isfinite(pose.yaw) ||
        !stereo::ReferenceCurrent(pose.referenceGeneration, HeadTrackingReferenceGeneration())) {
        gNoFrame.fetch_add(1, std::memory_order_relaxed);
        gFollower.Reset();
        return;
    }
    void* const entity = PlayerEntity(player);
    if (entity == nullptr) {
        gNotPlayer.fetch_add(1, std::memory_order_relaxed);
        Stand();
        return;
    }
    std::uintptr_t world = 0, table = 0;
    void* physics = nullptr;
    Living living{};
    if (SceneQueryWorld(pose, world, physics) != ERROR_SUCCESS || physics == nullptr || !ReadAt(physics, 0, table) ||
        table != gLivingVtable || !ReadLiving(physics, living)) {
        gNoLiving.fetch_add(1, std::memory_order_relaxed);
        gFollower.Reset();
        return;
    }
    // The point followed: the eye as it would be with the head level and
    // facing the torso, so a glance moves nothing; and how far below its
    // calibrated height it is, which tells a lean from a step.
    const Pose& head = pose.tracking.head;
    const Pose follow{head.orientation, armpose::LevelEye(head, torsoYaw)};
    float drop = 0.0f;
    if (!HeadDropBelowCalibrated(follow.position, drop)) { drop = 0.0f; }
    roomscale::Settings settings{};
    settings.leanZone = 0.01f * static_cast<float>(gLeanZoneCm.load(std::memory_order_relaxed));
    gDropMm.store(Mm(drop), std::memory_order_relaxed);
    gAllowanceMm.store(Mm(roomscale::LeanAllowance(drop, settings)), std::memory_order_relaxed);
    Vec3 applied{};
    float residual = std::numeric_limits<float>::quiet_NaN();
    bool blocked = false, settling = false;
    const int result = MoveHeadTrackingOriginWithNative(follow, pose.yaw, [&](const Vec3& offset) -> std::optional<Vec3> {
        residual = std::hypot(offset.x, offset.y);   // until a move takes some of it off
        // Within the lean zone the capsule stays and the body pin bends the
        // torso; past it, or once the head came to rest upright, it moves.
        const Vec3 move = gFollower.Update(offset, drop, dt, settings);
        settling = gFollower.Settling();
        if (gFollower.Jumped()) { gJumps.fetch_add(1, std::memory_order_relaxed); }
        const float length = std::hypot(move.x, move.y);
        if (!(length > 1e-5f)) { return std::nullopt; }
        // Climbing a step, the body's position is still being smoothed up to
        // it: wait the moment that takes rather than test or move it mid-way.
        if (Length(living.stance) > kStepSettling) {
            gStepSettling.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        }
        Vec3 feet{};
        if (!CallGetPos(entity, feet)) {
            Fault("get_pos");
            return std::nullopt;
        }
        // A body that is moving keeps its motion. It is moved from where its
        // physics has it now, and only while a SetParams applies at once: one
        // queued behind a physics step would put it back where it was before
        // that step, undoing the walk's own movement. Otherwise the move waits a
        // frame. At rest the entity's SetPos alone, whose teleport lets the
        // next step settle the body on what is under it.
        const bool bodyMoving = roomscale::BodyMoving(living.velocity, living.requested);
        const bool starting = !bodyMoving && MoveLaneStickStarted(now, kWalkStartNs);
        const bool moving = bodyMoving || starting;
        Vec3 base = feet;
        if (moving) {
            Vec3 body{};
            if (!PhysicsIdle(world, physics) || !ReadAt(physics, rn::BodyPosition, body) || !Finite(body)) {
                gBusy.fetch_add(1, std::memory_order_relaxed);
                return std::nullopt;
            }
            base = body;
        }
        // How much of the move fits, by the living body's own stance test: the
        // capsule stops short of a wall or a desk instead of being put into it
        // and pushed back out. Against something already, it may still step
        // away (roomscale::NoDeeper).
        float here = 0.0f;
        if (!StanceDepth(physics, nullptr, here)) {
            Fault("stance_test");
            return std::nullopt;
        }
        if (here > 0.0f) { gStartOverlap.fetch_add(1, std::memory_order_relaxed); }
        bool faulted = false;
        const float fraction = roomscale::FitFraction([&](float f) {
            const Vec3 at{base.x + move.x * f, base.y + move.y * f, base.z};
            float there = 0.0f;
            if (faulted || !StanceDepth(physics, &at, there)) {
                faulted = true;
                return false;
            }
            return roomscale::NoDeeper(there, here);
        }, kFitIterations);
        if (faulted) {
            Fault("stance_test");
            return std::nullopt;
        }
        if (fraction < 1.0f) { blocked = true; }
        if (fraction * length < 1e-4f) {
            gRefused.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        }
        const Vec3 target{base.x + move.x * fraction, base.y + move.y * fraction, base.z};
        if (moving) {
            if (!SetPositionKeepingMotion(physics, target)) {
                Fault("set_params");
                return std::nullopt;
            }
            gKeptMotion.fetch_add(1, std::memory_order_relaxed);
            if (starting) { gStartKept.fetch_add(1, std::memory_order_relaxed); }
        } else {
            gRestMoves.fetch_add(1, std::memory_order_relaxed);
        }
        if (!CallSetPos(entity, &target)) {
            Fault("set_pos");
            return std::nullopt;
        }
        // A physics step that began between the two calls queued the entity's
        // own SetParams behind it; replayed afterwards with this position, it
        // puts the body back by that step's movement. Counted, should it happen.
        std::uint32_t queued = 0;
        if (moving && ReadAt(physics, rn::EntityQueued, queued) && queued >= rn::EntityQueuedUnit) {
            gRaced.fetch_add(1, std::memory_order_relaxed);
        }
        Vec3 movedTo{};
        if (!CallGetPos(entity, movedTo)) {
            Fault("get_pos");
            return std::nullopt;
        }
        // Level only: height stays the view's and the engine's business. From
        // `base`: if the entity was behind its physics, catching that up is
        // the walk's own movement, which the view shows rather than cancels.
        applied = {movedTo.x - base.x, movedTo.y - base.y, 0.0f};
        residual = std::hypot(offset.x - applied.x, offset.y - applied.y);
        return applied;
    });
    if (result == 0) {
        gFollower.Applied(applied);
        const int mm = Mm(std::hypot(applied.x, applied.y));
        gMoved.fetch_add(1, std::memory_order_relaxed);
        gMovedMm.fetch_add(static_cast<unsigned long long>(mm), std::memory_order_relaxed);
        gLastMoveMm.store(mm, std::memory_order_relaxed);
        if (settling) { gSettleMoves.fetch_add(1, std::memory_order_relaxed); }
    } else if (result == 2) {
        gReferenceBusy.fetch_add(1, std::memory_order_relaxed);
    }
    if (blocked) { gBlocked.fetch_add(1, std::memory_order_relaxed); }
    if (std::isfinite(residual)) { gResidualMm.store(Mm(residual), std::memory_order_relaxed); }
    gUprightMm.store(Mm(gFollower.Upright()), std::memory_order_relaxed);
    gActiveNs.store(now, std::memory_order_release);
    if (!gFault.load(std::memory_order_acquire)) { HeadCheck(pose, entity); }
}

bool BodyHeadOut()
{
    return gHeadOut.load(std::memory_order_acquire) &&
           FreshSample(MonotonicNanoseconds(), gHeadOutNs.load(std::memory_order_acquire), kActiveNs);
}

bool RoomScaleActive()
{
    return !gFault.load(std::memory_order_acquire) && gGate.load(std::memory_order_acquire) == Gate::open &&
           FreshSample(MonotonicNanoseconds(), gActiveNs.load(std::memory_order_acquire), kActiveNs);
}

std::string RoomScaleReport()
{
    const Gate gate = gGate.load(std::memory_order_acquire);
    std::ostringstream out;
    out << " roomAvailable=" << (gate == Gate::open ? "1" : gate == Gate::refused ? "0" : "unchecked")
        << " roomFault=" << gFault.load() << " roomActive=" << RoomScaleActive() << " roomServices=" << gServices.load()
        << " notGameplay=" << gNotGameplay.load() << " noFrame=" << gNoFrame.load()
        << " roomNotPlayer=" << gNotPlayer.load() << " noLiving=" << gNoLiving.load() << " moved=" << gMoved.load()
        << " movedMm=" << gMovedMm.load() << " lastMoveMm=" << gLastMoveMm.load() << " residualMm=" << gResidualMm.load()
        << " leanZoneCm=" << gLeanZoneCm.load() << " headDropMm=" << gDropMm.load() << " allowanceMm=" << gAllowanceMm.load() << " uprightMm=" << gUprightMm.load()
        << " settleMoves=" << gSettleMoves.load() << " blocked=" << gBlocked.load()
        << " fitRefused=" << gRefused.load()
        << " startOverlap=" << gStartOverlap.load() << " busy=" << gBusy.load() << " referenceBusy=" << gReferenceBusy.load()
        << " keptMotion=" << gKeptMotion.load() << " startKept=" << gStartKept.load() << " restMoves=" << gRestMoves.load()
        << " stepSettling=" << gStepSettling.load() << " jumps=" << gJumps.load() << " raced=" << gRaced.load()
        << " headOut=" << BodyHeadOut() << " headOutEvents=" << gHeadOutEvents.load()
        << " headOutFrames=" << gHeadOutFrames.load() << " fadeFrames=" << XrFadeFrames();
    return out.str();
}

void SetRoomScaleLeanZoneCm(unsigned cm) { gLeanZoneCm.store(std::min(cm, kLeanZoneMaxCm), std::memory_order_relaxed); }

unsigned RoomScaleLeanZoneCm() { return gLeanZoneCm.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
