#include "BodyFollow.h"
#include "BodyFollowState.h"
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
using namespace body;

namespace body {

// A fault in engine code is not assumed recoverable: following stays off for
// the rest of the process.
void Fault(const char* where)
{
    gFaults.fetch_add(1, std::memory_order_relaxed);
    if (!gFault.exchange(true)) {
        Log(std::string("result=fault detail=") + where + " following=disarmed_for_process");
    }
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

} // namespace body

namespace {

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
    float deadzone = 0.0f, relax = 0.0f;
    BodyTorsoSettings(deadzone, relax);
    const float torso = gTorso.Update(head, dt, deadzone, relax);
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
        const int result = Turn(player, torso, requestMdeg * 1e-3f * kRadiansPerDegree, false);
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

void BodyTorsoSettings(float& deadzoneRadians, float& relax)
{
    deadzoneRadians = gDeadzoneMilliDeg.load(std::memory_order_relaxed) * 1e-3f * kRadiansPerDegree;
    relax = gRelaxPerMille.load(std::memory_order_relaxed) * 1e-3f;
}

DWORD SetTorsoDeadzone(int degrees)
{
    return SetBodyTorso(degrees, gRelaxPerMille.load(std::memory_order_relaxed));
}

int TorsoDeadzoneDegrees() { return gDeadzoneMilliDeg.load(std::memory_order_relaxed) / 1000; }

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

} // namespace preyvr::dll
