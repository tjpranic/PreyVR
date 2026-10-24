#include "BodyFollow.h"
#include "BodyFollowState.h"
#include "AtomicMax.h"

#include "GameplayFrame.h"
#include "HeadTrackingHook.h"
#include "MoveLane.h"
#include "SceneQuery.h"
#include "XrInput.h"
#include "preyvr/RoomScale.h"
#include "preyvr/RoomScaleNative.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <string>

// Room-scale (docs/ROOMSCALE-2026-10-11.md; BodyFollow.h): the capsule follows the
// head across the room, swept for walls by the living body's own stance test (or
// the rays without it), and the head-in-wall check. BodyFollow.cpp's tick calls it.
namespace preyvr::dll {
using namespace body;

namespace {

template <class Fn> Fn Slot(void* physics, std::size_t slot)
{
    return *reinterpret_cast<const Fn*>(*static_cast<const std::uintptr_t*>(physics) + slot);
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
// main thread -- the world's step lock, +0x57B0 and its lock all clear, and
// nothing queued for the body.
bool PhysicsIdle(std::uintptr_t world, void* physics)
{
    std::uint32_t stepLock = 1, stepBusy = 1, worldLock = 1, queued = rn::EntityQueuedUnit;
    void* const w = reinterpret_cast<void*>(world);
    return world != 0 && ReadAt(w, rn::WorldStepLock, stepLock) && ReadAt(w, rn::WorldStepBusy, stepBusy) &&
           ReadAt(w, rn::WorldLock, worldLock) && ReadAt(physics, rn::EntityQueued, queued) && stepLock == 0 &&
           stepBusy == 0 && worldLock == 0 && queued < rn::EntityQueuedUnit;
}

// A fault in a living-body call: room-scale drops back to the rays and the
// plain SetPos for the rest of the process.
void PhysicsFault(const char* where)
{
    if (gPhysicsAvailable.exchange(false)) {
        Log(std::string("result=fault detail=room_scale_physics_") + where + " fallback=rays_and_setpos");
    }
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

} // namespace

namespace body {

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
        //
        // A walk's first frames read as rest (the velocity and the request are
        // last frame's), so a stick pushed within kWalkStartNs counts as moving:
        // the teleport there started the walk airborne.
        const bool bodyMoving = roomscale::BodyMoving(living.velocity, living.requested);
        const bool starting = !bodyMoving && MoveLaneStickStarted(MonotonicNanoseconds(), kWalkStartNs);
        const bool moving = physics != nullptr && (bodyMoving || starting);
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
                if (starting) { gStartKept.fetch_add(1, std::memory_order_relaxed); }
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
        // A physics step that began between the two calls queued the entity's
        // own SetParams behind it (RoomScaleNative.h). Replayed afterwards with
        // this position, it puts the body back by that step's movement, and
        // takes the teleport. Counted, to decide whether it needs a cure.
        std::uint32_t queued = 0;
        if (moving && ReadAt(physics, rn::EntityQueued, queued) && queued >= rn::EntityQueuedUnit) {
            gRaced.fetch_add(1, std::memory_order_relaxed);
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

} // namespace body

DWORD SetRoomScale(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    if ((enabled != 0) != gRoomScale.load()) { ResetStats(); }
    gRoomScale.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=room_scale value=") + (enabled ? "1" : "0"));
    return 0;
}

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

bool BodyHeadOut()
{
    return gActive.load(std::memory_order_acquire) && gHeadOut.load(std::memory_order_acquire) &&
           FreshSample(MonotonicNanoseconds(), gHeadOutNs.load(std::memory_order_acquire), kTorsoFreshNs);
}

} // namespace preyvr::dll
