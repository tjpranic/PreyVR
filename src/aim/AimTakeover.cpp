#include "aim/AimTakeover.h"
#include "engine/hooks/HookInstall.h"
#include "engine/EngineAccess.h"
#include "ui/screens/ExamineCamera.h"
#include "app/FrameSchedule.h"
#include "body/HandWorld.h"
#include "aim/ReticleRayHook.h"
#include "engine/SafeMemory.h"
#include "physics/contact/SceneQueryLane.h"
#include "aim/ShotRayLane.h"
#include "aim/WeaponAttachment.h"
#include "diagnostics/overlay/DebugOverlay.h"
#include "body/ik/AnimIk.h"
#include "engine/hooks/MinHookInit.h"
#include "base/LatestSnapshot.h"

#include "render/camera/HeadTrackingHook.h"
#include "base/Logger.h"
#include "ui/hud/ReticleFollow.h"
#include "controls/input/XrInput.h"
#include "engine/EngineMap.h"
#include "aim/ExamineRay.h"
#include "aim/MotionController.h"
#include "engine/NativeContract.h"
#include "render/camera/StereoCamera.h"
#include "aim/WorldUiNative.h"

#include <MinHook.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string_view>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_aim_takeover " + line);
}

// R-011 ArkPlayer::UpdateCachedReticleViewPosAndDir(ArkPlayer*).
using UpdateCachedRayFn = void(__fastcall*)(void* player);
using GetArkPlayerInstanceFn = void*(__fastcall*)();

void* gTarget = nullptr;
std::atomic<UpdateCachedRayFn> gOriginal{nullptr};
bool gInstalled = false;
LatestSnapshot<preyvr::aim::Sample> gAimSample;
std::atomic<unsigned long long> gAimSamplePublished{0};
// The world direction actually published, and the raw controller orientation it
// came from. Reported so a head sweep says which one moves.
std::atomic<int> gAimDirMilli[3]{0, 0, 0};
std::atomic<int> gAimRawDirMilli[3]{0, 0, 0};
// The native producer and this edit run on the gameplay thread. A TLS record
// prevents a failed native unprojection from recycling our hand origin.
thread_local animik::RayOriginEdit gLastOriginEdit;

std::atomic<bool> gEnabled{false};
std::atomic<unsigned long long> gApplied{0};
std::atomic<unsigned long long> gRejNoPose{0};
std::atomic<unsigned long long> gRejNoPlayer{0};
std::atomic<unsigned long long> gRejCompose{0};
// 0 native eye, 1 the tracked hand, 2 the calibrated muzzle.
std::atomic<unsigned int> gOriginMode{0};
std::atomic<unsigned long long> gMuzzleOriginApplied{0}, gMuzzleUnavailable{0};
std::atomic<unsigned long long> gOriginApplied{0};

using engine::FindLandmark;

// Undoes the origin the last frame's aim override wrote, before the engine
// rebuilds its own ray from it.
void UndoOriginEdit(void* player)
{
    if (player && gLastOriginEdit.owner == reinterpret_cast<std::uintptr_t>(player)) {
        auto* originAt = static_cast<std::uint8_t*>(player) + engine::ArkPlayerLayout::cachedReticleOrigin;
        Vec3 current{};
        std::memcpy(&current, originAt, sizeof(current));
        if (gLastOriginEdit.Restore(reinterpret_cast<std::uintptr_t>(player), current)) {
            std::memcpy(originAt, &current, sizeof(current));
        }
        gLastOriginEdit = {};
    }
}

// --- screens (AimTakeover.h, aim/ExamineRay.h) ---------------------------------
std::atomic<unsigned long long> gScreenFrames{0}, gScreenRefused{0};
// The world UI's read of the ray: its return address once its body was checked.
namespace wn = worldui::native;
static_assert(contract::Gated(wn::Contracts, wn::CursorRay) && wn::RayReadReturn > wn::CursorRay &&
              wn::RayReadReturn < wn::CursorRay + wn::Contracts[0].size);
std::atomic<std::uintptr_t> gWorldUiReturn{0};
std::atomic<const char*> gWorldUiGate{"unchecked"};
std::atomic<unsigned long long> gWorldUiSteered{0};

// While a screen is examined: the cached ray, which Prey casts the screen's
// cursor from, and the sample start on the right hand's line level with the
// eye. The hand is placed against the camera centre the world is drawn from,
// as the drawn hands are, not against Prey's cached origin: that is a near-plane
// point that moves with Prey's own examination cursor. False leaves the ray as
// it was (no camera this frame).
bool AimAtScreen(const GameplayPoseFrame& frame, const Vec3& engineOrigin, const Vec3& direction,
                 preyvr::aim::Sample& sample)
{
    Pose hand{};
    if (!HandWorld(frame, Hand::right, ControllerPose::aim, Anchor::camera, Placement::controller, hand)) {
        gScreenRefused.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const Vec3 origin = examine::ScreenRayOrigin(hand.position, frame.cameraCentre, direction);
    if (!Finite(origin)) {
        gScreenRefused.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(frame.player + engine::ArkPlayerLayout::cachedReticleOrigin), &origin,
                sizeof(origin));
    gLastOriginEdit = {frame.player, engineOrigin, origin};
    sample.origin = origin;
    sample.cameraAnchored = true;
    gScreenFrames.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// The getter's filter (ReticleRayHook.h), last in line. The world UI's cursor
// raycast is handed the ray the crosshair is drawn from -- the published sample:
// from the muzzle with a gun drawn, from the hand's line at an examined screen,
// else from the eye -- instead of the cached one, whose origin stays the eye. So
// what a screen highlights is what the crosshair sits on. With no usable sample
// Prey's own ray is left.
void WorldUiRay(std::uintptr_t, void* out, void* result, std::uintptr_t caller)
{
    const std::uintptr_t expected = gWorldUiReturn.load(std::memory_order_acquire);
    if (expected == 0 || caller != expected || result != out) { return; }
    preyvr::aim::Sample sample{};
    if (!gAimSample.TryRead(sample) ||
        !preyvr::aim::Usable(sample, WeaponEquipGeneration(), HeadTrackingReferenceGeneration(),
                             MonotonicNanoseconds())) { return; }
    const float ray[6] = {sample.origin.x, sample.origin.y, sample.origin.z,
                          sample.direction.x, sample.direction.y, sample.direction.z};
    if (SafeCopy(out, ray, sizeof(ray))) { gWorldUiSteered.fetch_add(1, std::memory_order_relaxed); }
}

bool WorldUiBodyMatches(std::uintptr_t base)
{
    __try {
        return contract::BodiesMatch(base, wn::Contracts);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The aim override: the cached ray (and with aim.origin its origin) from the
// right controller, and the published aim sample.
void OverrideAimRay(const GameplayPoseFrame& frame, bool haveTracking)
{
    if (!haveTracking) { gRejNoPose.fetch_add(1); return; }
    // A frame whose play-space yaw is unknown cannot aim, and must not silently
    // aim with the camera-relative yaw the body yaw exists to replace.
    if (!frame.headYawUsable) {
        gRejNoPose.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!gEnabled.load(std::memory_order_acquire)) { return; }
    const auto playerAddress = frame.player;
    const auto& controller = frame.tracking.hands[static_cast<unsigned int>(Hand::right)];
    const Vec3 engineOrigin = frame.reticleOrigin;
    stereo::ReferenceFrame reference{};
    reference.yawRadians = frame.yaw;
    // The hands' own basis (HandWorld.h): in zero-G the tilted body's.
    reference.tilted = true;
    reference.basis = PlaySpaceRotation(frame);
    reference.worldPosition = engineOrigin;

    auto ray = controller::AimFromController(
        reference, controller.aimPose, controller.aimValidity,
        /*maxAgeNanoseconds=*/200ull * 1000ull * 1000ull);
    if (!ray) {
        // AimFromController refuses an untracked or stale pose rather than
        // producing a plausible ray from a dead controller -- the caller then
        // leaves Prey's own ray alone, which is what this does.
        gRejNoPose.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if(frame.twoHand.blend>0) {
        ray->direction=Rotate(frame.twoHand.orientation,Vec3{0,1,0});
    }
    const float length = Length(ray->direction);
    if (!std::isfinite(length) || length < 0.99f || length > 1.01f) {
        gRejCompose.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Describe the ray actually installed below, not the best origin available
    // regardless of aim.origin. A positional muzzle calibration does not prove
    // a barrel rotation: this direction is still the runtime's aiming axis.
    preyvr::aim::Sample sample{};
    sample.origin = engineOrigin;
    sample.direction = ray->direction;
    Pose hand{};
    HandWorld(frame, Hand::right, ControllerPose::aim, Anchor::reticle, Placement::controller, hand);
    if(frame.twoHand.blend>0) hand.orientation=frame.twoHand.orientation;
    sample.orientation = hand.orientation; // world space, like the direction
    sample.confidence = preyvr::aim::Confidence::origin;
    sample.equipGeneration = WeaponEquipGeneration();
    sample.referenceGeneration = frame.referenceGeneration;
    sample.trackingSequence = frame.tracking.sequence;

    // This selects a pointing direction. Native firing may converge from its
    // authored muzzle toward this ray; a controller point is not that muzzle.
    std::memcpy(reinterpret_cast<void*>(
                    playerAddress + engine::ArkPlayerLayout::cachedReticleDirection),
                &ray->direction, sizeof(ray->direction));
    gApplied.fetch_add(1, std::memory_order_relaxed);

    const unsigned int originMode = gOriginMode.load(std::memory_order_acquire);
    // A screen being examined is pointed at from the hand, whatever aim.origin
    // says (AimAtScreen).
    const bool screen = ExaminingScreen(playerAddress) && AimAtScreen(frame, engineOrigin, ray->direction, sample);
    if (!screen && originMode != 0) {
        Vec3 chosen = hand.position;
        bool haveMuzzle = false;
        if (originMode == 2) {
            // **The reconciliation.** Prey's projectile already leaves the
            // weapon's authored muzzle helper while the reticle ray starts at
            // the eye, so a shot flies from the muzzle toward wherever an eye
            // ray landed and the two agree at exactly one distance. Starting the
            // ray at that same muzzle removes the convergence entirely.
            //
            // The GRIP pose is used, not the aim pose, because the calibration
            // was measured against the grip: an offset is only meaningful in
            // the frame it was captured in.
            Pose grip{};
            HandWorld(frame, Hand::right, ControllerPose::grip, Anchor::reticle, Placement::controller, grip);
            if(frame.twoHand.blend>0) grip.orientation=Normalize(Multiply(frame.twoHand.correction,grip.orientation));
            Vec3 muzzle{};
            if (IsPoseUsable(controller.gripPose, controller.gripValidity, kMaxPoseAgeNs) &&
                TryGetMuzzleFromGrip(grip, sample.equipGeneration, muzzle)) {
                chosen = muzzle;
                haveMuzzle = true;
            } else {
                // Uncalibrated, or a different weapon since. Fall back to the
                // hand and COUNT it: silently reverting to a less accurate
                // origin is how a lane looks fine and aims wrong.
                gMuzzleUnavailable.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (std::isfinite(chosen.x) && std::isfinite(chosen.y) && std::isfinite(chosen.z)) {
            std::memcpy(reinterpret_cast<void*>(
                            playerAddress + engine::ArkPlayerLayout::cachedReticleOrigin),
                        &chosen, sizeof(chosen));
            gLastOriginEdit = {playerAddress, engineOrigin, chosen};
            sample.origin = chosen;
            gOriginApplied.fetch_add(1, std::memory_order_relaxed);
            if (haveMuzzle) { gMuzzleOriginApplied.fetch_add(1, std::memory_order_relaxed); }
        }
    }

    // Legacy latest-value telemetry; use reticleProjection's coherent render
    // record to compare a ray with its actual projection camera and raw pose.
    for (unsigned int i = 0; i < 3; ++i) {
        gAimDirMilli[i].store(static_cast<int>((&sample.direction.x)[i] * 1000.0f),
                              std::memory_order_relaxed);
        gAimRawDirMilli[i].store(
            static_cast<int>((&controller.aimPose.orientation.x)[i] * 1000.0f),
            std::memory_order_relaxed);
    }
    // aim.shot: the published ray starts at the muzzle the shot leaves from, so
    // the scene query, the reticles and the overlay all describe the shot. The
    // engine's cached origin stays the eye (see ShotRayLane.h). Not at a screen: its
    // ray is the hand's.
    if (originMode == 0 && !screen) { ShotRayFrame(frame, sample); }
    QueryAimScene(frame,sample);
    sample.publishedNs = MonotonicNanoseconds();
    gAimSample.Publish(sample);
    gAimSamplePublished.fetch_add(1, std::memory_order_relaxed);
    DebugOverlayAim(frame, sample);
    // Where the world reticle goes: this sample's hit, in tracking space.
    PublishReticleTarget(frame, sample);

    // Screen projection is deferred until CSystem::Render has installed the
    // actual eye camera. Here the global camera still has gameplay projection;
    // the per-eye projection is transient and restored after each render.
}

void __fastcall UpdateCachedRayWithTakeover(void* player)
{
    // Every refusal below invalidates the previous publication immediately.
    gAimSample.Clear();
    const UpdateCachedRayFn original = gOriginal.load(std::memory_order_acquire);
    UndoOriginEdit(player);
    if (original != nullptr) {
        original(player);   // let the engine build its own ray first
    }
    if (player == nullptr || original == nullptr) {
        ClearGameplayFrame();
        gRejNoPlayer.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    GameplayPoseFrame frame{};
    bool haveTracking = false;
    switch (BuildGameplayFrame(reinterpret_cast<std::uintptr_t>(player), frame, haveTracking)) {
    case FrameBuild::badNativeRay: gRejCompose.fetch_add(1, std::memory_order_relaxed); return;
    case FrameBuild::notPlaceable: return;
    case FrameBuild::built: break;
    }
    // IK must never reread the mutable cached origin below.
    SolveTwoHandedFrame(frame,haveTracking);
    // The frame's stages and then its lanes, in order (FrameSchedule.cpp).
    schedule::PrepareGameplayFrame(frame,haveTracking&&gEnabled.load());
    PublishGameplayFrame(frame);
    schedule::TickGameplayLanes(frame,haveTracking&&gEnabled.load(),haveTracking);
    OverrideAimRay(frame, haveTracking);
}

bool Install()
{
    if (gInstalled) {
        return true;
    }
    const engine::Landmark* const landmark = FindLandmark("aim.update_cached_ray");
    if (landmark == nullptr) {
        Log("result=unavailable detail=landmark_missing");
        return false;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return false;
    }
    const auto target = reinterpret_cast<std::uintptr_t>(preyDll) + landmark->rva;
    if (std::memcmp(reinterpret_cast<const void*>(target),
                    landmark->expected.data(), landmark->expected.size()) != 0) {
        Log("result=unavailable detail=prologue_mismatch");
        return false;
    }
    gTarget = reinterpret_cast<void*>(target);
    const auto installed = hook::Install(gTarget, reinterpret_cast<void*>(&UpdateCachedRayWithTakeover), gOriginal);
    if (!installed) {
        Log(std::string("result=failed detail=") + hook::StepName(installed.step));
        return false;
    }
    gInstalled = true;
    Log("result=0 detail=hook_installed target=ArkPlayer::UpdateCachedReticleViewPosAndDir");
    return true;
}

} // namespace

bool TryGetAimSample(preyvr::aim::Sample& out) { return gAimSample.TryRead(out); }

void UpdateAimReticleForRender()
{
    if (!gEnabled.load(std::memory_order_acquire)) { return; }
    preyvr::aim::Sample sample{};
    GameplayPoseFrame frame{};
    if (!TryGetAimSample(sample) || !TryGetGameplayPoseFrame(frame) ||
        sample.trackingSequence != frame.tracking.sequence ||
        !preyvr::aim::Usable(sample, WeaponEquipGeneration(),
                            HeadTrackingReferenceGeneration(), MonotonicNanoseconds())) {
        return;
    }
    const ReticleAimContext context{frame.tracking.sequence, frame.tracking.epoch,
        frame.referenceGeneration, frame.tracking.displayTime, frame.yaw,
        frame.tracking.head, frame.tracking.hands[static_cast<int>(Hand::right)].aimPose,sample.sceneDistance};
    WriteReticleScreenPosition(reinterpret_cast<void*>(frame.player),
                               sample.origin, sample.direction, &context);
}
int AimDirectionMilli(unsigned int axis)
{
    return axis < 3 ? gAimDirMilli[axis].load(std::memory_order_relaxed) : 0;
}
int AimRawOrientationMilli(unsigned int axis)
{
    return axis < 3 ? gAimRawDirMilli[axis].load(std::memory_order_relaxed) : 0;
}
unsigned long long AimSamplePublishedCount()
{
    return gAimSamplePublished.load(std::memory_order_relaxed);
}

bool EnsureGameplayPoseObservation() { return Install(); }
bool AimTakeoverEnabled() { return gEnabled.load(); }

DWORD SetAimOriginFromHand(unsigned int mode)
{
    if (mode > 2u) { return 1; }
    gOriginMode.store(mode, std::memory_order_release);
    Log("result=0 detail=origin_mode value=" + std::to_string(mode));
    return 0;
}

unsigned int AimOriginMode() { return gOriginMode.load(std::memory_order_relaxed); }

void EnsureWorldUiRay()
{
    if (gWorldUiReturn.load(std::memory_order_acquire) != 0) { return; }
    const std::uintptr_t base = PreyBase();
    if (base == 0) {
        gWorldUiGate = "no_preydll";
        return;
    }
    if (!WorldUiBodyMatches(base)) {
        gWorldUiGate = "refused";
        Log("result=unavailable detail=bytes_mismatch target=WorldUiCursorRay");
        return;
    }
    if (!EnsureReticleRayHook()) {
        gWorldUiGate = "no_getter_hook";
        Log("result=unavailable detail=no_getter_hook target=WorldUiCursorRay");
        return;
    }
    SetReticleRayFilter(ReticleRayClient::worldUi, &WorldUiRay);
    gWorldUiReturn.store(base + wn::RayReadReturn, std::memory_order_release);
    gWorldUiGate = "ok";
    Log("result=0 detail=steering target=WorldUiCursorRay");
}

std::string ScreenAimReport()
{
    std::ostringstream s;
    s << "screenAim={frames=" << gScreenFrames.load() << " refused=" << gScreenRefused.load()
      << " worldUi={gate=" << gWorldUiGate.load() << " steered=" << gWorldUiSteered.load() << "}}";
    return s.str();
}

unsigned long long AimMuzzleOriginAppliedCount()
{
    return gMuzzleOriginApplied.load(std::memory_order_relaxed);
}
unsigned long long AimMuzzleUnavailableCount()
{
    return gMuzzleUnavailable.load(std::memory_order_relaxed);
}

unsigned long long AimOriginAppliedCount() { return gOriginApplied.load(std::memory_order_relaxed); }

DWORD SetAimTakeoverEnabled(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on) {
        if (!Install()) {
            return 1;
        }
        gApplied.store(0, std::memory_order_relaxed);
        gRejNoPose.store(0, std::memory_order_relaxed);
        gRejNoPlayer.store(0, std::memory_order_relaxed);
        gRejCompose.store(0, std::memory_order_relaxed);
    }
    gEnabled.store(on, std::memory_order_release);
    if (!on) { gAimSample.Clear(); }
    Log(std::string("result=0 detail=enabled value=") + (on ? "1" : "0"));
    return 0;
}

unsigned long long AimTakeoverAppliedCount() { return gApplied.load(std::memory_order_relaxed); }
unsigned long long AimTakeoverRejectedNoPose() { return gRejNoPose.load(std::memory_order_relaxed); }
unsigned long long AimTakeoverRejectedNoPlayer() { return gRejNoPlayer.load(std::memory_order_relaxed); }
unsigned long long AimTakeoverRejectedCompose() { return gRejCompose.load(std::memory_order_relaxed); }
} // namespace preyvr::dll
