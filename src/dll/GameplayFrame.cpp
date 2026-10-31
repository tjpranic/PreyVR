#include "GameplayFrame.h"
#include "AimTakeover.h"
#include "EngineAccess.h"
#include "HeadTrackingHook.h"
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
std::atomic<int> gCamYawMilli{0}, gHeadYawMilli{0}, gPlaySpaceYawMilli{0};
std::atomic<int> gNativeEyeMilli[3]{0,0,0};
std::atomic<int> gTrackedHeadMilli[3]{0,0,0};
// The last head yaw that was defined, held near-vertical, where the head's yaw is
// genuinely undefined. Only a frame with no view to read goes by it (see
// BuildGameplayFrame): the camera yaw it is taken from goes on following the head.
std::atomic<float> gLastHeadYaw{0.0f};
std::atomic<bool> gHaveHeadYaw{false};
std::atomic<unsigned long long> gHeadYawHeld{0}, gHeadYawUnavailable{0};
// Frames built on zero-G's tilted view.
std::atomic<unsigned long long> gTiltedFrames{0};

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
    // so elements 3, 7 and 11. This is the gameplay camera on the main thread,
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
    frame.recenterEpoch = HeadTrackingRecenterEpoch();
    frame.referenceYaw = HeadTrackingReferenceYaw();
    frame.cameraYaw = GameCameraYaw();
    // Worked back from the camera, for a frame with no view to read (below).
    // Camera yaw is body + (head - reference) because this mod writes head
    // tracking into that camera. Subtracting the head's own yaw leaves the
    // body, which is what a head-relative offset must be rotated by; using the
    // camera directly applies the head twice and the hand follows the headset.
    // The camera-relative yaw stands only on a frame no head yaw is defined
    // for, which the consumers refuse (headYawUsable).
    frame.yaw = frame.cameraYaw - frame.referenceYaw;
    const auto headYaw = haveTracking
        ? stereo::RecenterYawFromHeadPose(frame.tracking.head)
        : std::optional<float>{};
    if (headYaw) {
        gHeadYawMilli.store(static_cast<int>(*headYaw * 57295.78f), std::memory_order_relaxed);
        gLastHeadYaw.store(*headYaw, std::memory_order_release);
        gHaveHeadYaw.store(true, std::memory_order_release);
        frame.headYaw = *headYaw;
        frame.headYawUsable = true;
        frame.yaw = frame.cameraYaw - *headYaw;
    } else if (gHaveHeadYaw.load(std::memory_order_acquire)) {
        // Looking near-vertical. **Never fall through to the camera yaw
        // here**: that is precisely the defect this lane exists to fix, and
        // silently restoring it would make the hands swing again whenever a
        // player looked up. Hold the last defined head yaw instead, the lesser
        // wrong: the camera yaw goes on following the head, so this is off by
        // how far the head's heading has moved since. A view corrects it below.
        const float last = gLastHeadYaw.load(std::memory_order_acquire);
        frame.headYaw = last;
        frame.headYawUsable = true;
        frame.yaw = frame.cameraYaw - last;
        gHeadYawHeld.fetch_add(1, std::memory_order_relaxed);
    } else {
        // No head yaw has ever been defined. Reject rather than invent:
        // consumers below refuse the sample instead of driving from a frame
        // whose orientation is unknown.
        frame.headYawUsable = false;
        gHeadYawUnavailable.fetch_add(1, std::memory_order_relaxed);
    }
    // **The play space this frame's view was composed on**, read rather than
    // worked back: the view hook has it, and it needs no head yaw. The camera's
    // yaw less the head's is the same number only while the head's forward has
    // a heading. Bent over looking straight down it has hardly any: a degree of
    // head movement swings the camera's yaw by tens of them, the hold above
    // does not follow, and the hands, the aim and the chest turned about the
    // head (docs/BODY-FOLLOW.md, the four yaws). In zero-G the view is composed
    // on the tilted body (HeadTrackingHook.cpp), so that difference is no
    // heading at all. The basis splits into the heading and the tilt
    // (GameplayFrame.h PlaySpaceRotation), which is the identity on a level body.
    frame.tilt = Quaternion{};
    const AppliedView view = LastAppliedView();
    if (FreshSample(MonotonicNanoseconds(), view.publishedNs)) {
        const stereo::SplitBasis split = stereo::SplitHeading(view.basis);
        frame.yaw = split.yaw;
        frame.tilt = split.tilt;
        frame.headYawUsable = true;
        if (view.tilted) { gTiltedFrames.fetch_add(1, std::memory_order_relaxed); }
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

unsigned long long AimHeadYawHeldCount() { return gHeadYawHeld.load(std::memory_order_relaxed); }
unsigned long long AimTiltedFrameCount() { return gTiltedFrames.load(std::memory_order_relaxed); }
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
