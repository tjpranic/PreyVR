#include "GameplayFrame.h"
#include "AimTakeover.h"
#include "EngineAccess.h"
#include "HeadTrackingHook.h"
#include "Logger.h"
#include "XrInput.h"
#include "preyvr/EngineMap.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/StereoCamera.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>
#include <string>

// The gameplay frame's producer: the aim hook (AimTakeover.cpp) calls
// BuildGameplayFrame after the engine's own UpdateCachedReticleViewPosAndDir, and
// PublishGameplayFrame once the frame's stages have run (FrameSchedule.h).
namespace preyvr::dll {
namespace {

LatestSnapshot<GameplayPoseFrame> gGameplayFrame;
std::atomic<unsigned int> gNativeMagnitude{0};
std::atomic<bool> gBodyYaw{true};
std::atomic<int> gCamYawMilli{0}, gHeadYawMilli{0}, gPlaySpaceYawMilli{0};
std::atomic<int> gNativeEyeMilli[3]{0,0,0};
std::atomic<int> gTrackedHeadMilli[3]{0,0,0};
// The last head yaw that was defined. Near-vertical, the head's yaw is genuinely
// undefined but the BODY's has not changed, so holding this keeps the frame
// correct -- and camera yaw still carries body turning, so the player can look
// at the ceiling and keep walking in a circle without the hands drifting.
std::atomic<float> gLastHeadYaw{0.0f};
std::atomic<bool> gHaveHeadYaw{false};
std::atomic<unsigned long long> gHeadYawHeld{0}, gHeadYawUnavailable{0};

// The yaw the engine's own view camera is facing. Read live rather than cached,
// because the whole point is to follow the player's facing as it changes.
float GameCameraYaw()
{
    const std::uint8_t* const camera = LiveViewCamera();
    if (camera == nullptr) {
        return 0.0f;
    }
    const stereo::Matrix34 matrix =
        stereo::ReadMatrix(std::span<const std::uint8_t>(camera, engine::CameraLayout::size));
    return stereo::CameraYawOf(matrix);
}

} // namespace

FrameBuild BuildGameplayFrame(std::uintptr_t player, GameplayPoseFrame& frame, bool& haveTracking)
{
    const auto playerAddress = player;
    frame.player = playerAddress;
    std::memcpy(&frame.reticleOrigin, reinterpret_cast<const void*>(
        playerAddress + engine::ArkPlayerLayout::cachedReticleOrigin), sizeof(Vec3));
    // The view camera's own translation -- Matrix34, translation in column 3,
    // so elements 3, 7 and 11. This is the gameplay camera on the game thread,
    // before the render seam installs a per-eye offset, so it carries no half
    // IPD (the trap H-005C / FAIL-HAND-037 records for the live render camera).
    frame.cameraCentreValid = false;
    if (const std::uint8_t* const view = LiveViewCamera()) {
        const auto* const camera = reinterpret_cast<const float*>(view + engine::CameraLayout::matrix);
        const Vec3 centre{camera[3], camera[7], camera[11]};
        if (std::isfinite(centre.x) && std::isfinite(centre.y) && std::isfinite(centre.z)) {
            frame.cameraCentre = centre;
            frame.cameraCentreValid = true;
        }
    }
    Vec3 nativeDirection{};
    std::memcpy(&nativeDirection, reinterpret_cast<const void*>(
        playerAddress + engine::ArkPlayerLayout::cachedReticleDirection), sizeof(Vec3));
    const float nativeLength = Length(nativeDirection);
    if (!std::isfinite(nativeLength) || nativeLength < 0.9f || nativeLength > 1.1f ||
        !std::isfinite(frame.reticleOrigin.x) || !std::isfinite(frame.reticleOrigin.y) ||
        !std::isfinite(frame.reticleOrigin.z)) {
        gGameplayFrame.Clear();
        return FrameBuild::badNativeRay;
    }
    gNativeMagnitude.store(static_cast<unsigned int>(nativeLength * 1000.0f + 0.5f));
    haveTracking = TryGetTrackingFrame(frame.tracking) &&
        IsPoseUsable(frame.tracking.head, frame.tracking.headValidity, kMaxPoseAgeNs);
    frame.referenceGeneration = HeadTrackingReferenceGeneration();
    frame.referenceYaw = HeadTrackingReferenceYaw();
    frame.cameraYaw = GameCameraYaw();
    // Camera yaw is body + (head - reference) because this mod writes head
    // tracking into that camera. Subtracting the head's own yaw leaves the
    // body, which is what a head-relative offset must be rotated by; using the
    // camera directly applies the head twice and the hand follows the headset.
    frame.yaw = frame.cameraYaw - frame.referenceYaw;   // aim.bodyyaw 0 behaviour
    // Measured unconditionally. Recording it only on the body-yaw path made the
    // instrument unable to describe the mode it was meant to be compared
    // against: an A/B on 2026-09-08 reported a 0-degree head sweep for
    // `aim.bodyyaw 0` purely because nothing sampled the head there.
    const auto measuredHeadYaw = haveTracking
        ? stereo::RecenterYawFromHeadPose(frame.tracking.head)
        : std::optional<float>{};
    if (measuredHeadYaw) {
        gHeadYawMilli.store(static_cast<int>(*measuredHeadYaw * 57295.78f),
                            std::memory_order_relaxed);
    }
    if (gBodyYaw.load(std::memory_order_acquire)) {
        const auto headYaw = measuredHeadYaw;
        if (headYaw) {
            gLastHeadYaw.store(*headYaw, std::memory_order_release);
            gHaveHeadYaw.store(true, std::memory_order_release);
            frame.headYaw = *headYaw;
            frame.headYawUsable = true;
            frame.yaw = frame.cameraYaw - *headYaw;
        } else if (gHaveHeadYaw.load(std::memory_order_acquire)) {
            // Looking near-vertical. **Never fall through to the camera yaw
            // here**: that is precisely the defect this lane exists to fix, and
            // silently restoring it would make the hands swing again whenever a
            // player looked up. Hold the last defined head yaw instead -- the
            // body has not turned just because the head tilted, and body turning
            // still arrives through the camera yaw.
            const float held = gLastHeadYaw.load(std::memory_order_acquire);
            frame.headYaw = held;
            frame.headYawUsable = true;
            frame.yaw = frame.cameraYaw - held;
            gHeadYawHeld.fetch_add(1, std::memory_order_relaxed);
        } else {
            // No head yaw has ever been defined. Reject rather than invent:
            // consumers below refuse the sample instead of driving from a frame
            // whose orientation is unknown.
            frame.headYawUsable = false;
            gHeadYawUnavailable.fetch_add(1, std::memory_order_relaxed);
        }
    }
    gCamYawMilli.store(static_cast<int>(frame.cameraYaw * 57295.78f), std::memory_order_relaxed);
    gPlaySpaceYawMilli.store(static_cast<int>(frame.yaw * 57295.78f), std::memory_order_relaxed);
    // The anchor the hand is placed against, and the tracked head it is measured
    // from. A 2026-09-08 measurement found the hand still moving 0.74 mm per
    // degree of head yaw at r = -0.92 with the controller resting motionless,
    // *after* the play-space yaw was proved decoupled. The remaining suspect is
    // these two disagreeing: the composition assumes the native eye translates
    // by the same amount as the tracked head, and if it translates by less, the
    // difference lands in the hand. Reporting both makes that one measurement.
    for (unsigned int i = 0; i < 3; ++i) {
        const float eye = (&frame.reticleOrigin.x)[i];
        const float head = (&frame.tracking.head.position.x)[i];
        gNativeEyeMilli[i].store(static_cast<int>(eye * 1000.0f), std::memory_order_relaxed);
        gTrackedHeadMilli[i].store(static_cast<int>(head * 1000.0f), std::memory_order_relaxed);
    }
    if (!stereo::ReferenceCurrent(frame.referenceGeneration, HeadTrackingReferenceGeneration()) ||
        !std::isfinite(frame.yaw)) { gGameplayFrame.Clear(); return FrameBuild::notPlaceable; }
    return FrameBuild::built;
}

void PublishGameplayFrame(GameplayPoseFrame& frame)
{
    frame.publishedNs = MonotonicNanoseconds();
    gGameplayFrame.Publish(frame);
}

void ClearGameplayFrame() { gGameplayFrame.Clear(); }

bool TryGetGameplayPoseFrame(GameplayPoseFrame& out, bool requireTracking)
{
    if (!gGameplayFrame.TryRead(out)) { return false; }
    if (!FreshSample(MonotonicNanoseconds(), out.publishedNs)) { return false; }
    if (!requireTracking) { return true; }
    TrackingFrame current{};
    // Also require a currently focused publication. A cleared XR slot must
    // invalidate an otherwise young cached gameplay frame immediately.
    if (!TryGetTrackingFrame(current) || out.tracking.epoch != current.epoch ||
        out.referenceGeneration != HeadTrackingReferenceGeneration()) { return false; }
    const auto now = MonotonicNanoseconds();
    if (!FreshSample(now, out.tracking.publishedNs)) { return false; }
    const auto age = now - out.tracking.publishedNs;
    out.tracking.headValidity.ageNanoseconds = age;
    for (auto& hand : out.tracking.hands) {
        hand.gripValidity.ageNanoseconds = age;
        hand.aimValidity.ageNanoseconds = age;
    }
    return IsPoseUsable(out.tracking.head, out.tracking.headValidity, kMaxPoseAgeNs);
}

DWORD SetAimBodyYaw(unsigned int enabled)
{
    if (enabled > 1) { return 1; }
    gBodyYaw.store(enabled != 0u, std::memory_order_release);
    lifecycle::Log(std::string("preyvr_aim_takeover result=0 detail=body_yaw value=") + (enabled ? "1" : "0"));
    return 0;
}

unsigned int AimBodyYawEnabled() { return gBodyYaw.load(std::memory_order_relaxed) ? 1u : 0u; }
unsigned long long AimHeadYawHeldCount() { return gHeadYawHeld.load(std::memory_order_relaxed); }
unsigned long long AimHeadYawUnavailableCount() { return gHeadYawUnavailable.load(std::memory_order_relaxed); }
int AimCameraYawMilliDegrees() { return gCamYawMilli.load(std::memory_order_relaxed); }
int AimHeadYawMilliDegrees() { return gHeadYawMilli.load(std::memory_order_relaxed); }
int AimPlaySpaceYawMilliDegrees() { return gPlaySpaceYawMilli.load(std::memory_order_relaxed); }
int AimNativeEyeMillimetres(unsigned int axis) { return axis < 3 ? gNativeEyeMilli[axis].load(std::memory_order_relaxed) : 0; }
int AimTrackedHeadMillimetres(unsigned int axis) { return axis < 3 ? gTrackedHeadMilli[axis].load(std::memory_order_relaxed) : 0; }

DWORD AimTakeoverNativeDirectionMagnitude()
{
    return gNativeMagnitude.load(std::memory_order_relaxed);
}

} // namespace preyvr::dll
