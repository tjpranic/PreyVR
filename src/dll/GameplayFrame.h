#pragma once

// The per-frame input every gameplay lane is handed: one XR tracking sample
// plus the engine's own camera and play-space yaw for the same frame. The aim
// hook (AimTakeover.cpp) builds it (GameplayFrame.cpp) and publishes it, then
// hands it to the gameplay schedule (FrameSchedule.h), which runs the lanes on it.
#include "XrInput.h"
#include "preyvr/SurfaceStop.h"
#include "preyvr/TwoHandedAim.h"

#include <array>
#include <cstdint>

namespace preyvr::dll {

// Native producer output plus one XR publication, captured before aim writes.
struct GameplayPoseFrame {
    TrackingFrame tracking{};
    // The engine's cachedReticleOrigin: an unprojected reticle-ray point, NOT
    // the camera centre. A fresh pre-write snapshot does not make it independent
    // of aim.reticle. The hand anchor is `cameraCentre` below (Anchor::camera,
    // HandWorld.h); see docs/RE-IK-CROSSTALK-2026-09-09.md before treating this
    // as one. Called nativeEye until 2026-10-24.
    Vec3 reticleOrigin{};
    // **The camera centre, which is what a head-relative offset must be rotated
    // about.** `reticleOrigin` above is the native cached RETICLE ray origin -- an
    // unprojected screen point that moves when the reticle moves, and our own
    // reticle lane writes that reticle. Using it as the shared hand anchor feeds
    // the right controller's aim back into BOTH hands' world positions, which is
    // the reported left-hand drag (RE-IK-CROSSTALK-2026-09-09).
    //
    // Read from the same view camera as the declared frustum, so the anchor and
    // the projection cannot come from two different frames. Invalid means the
    // camera could not be read: consumers must refuse rather than fall back to
    // the reticle point, because falling back silently restores the defect.
    Vec3 cameraCentre{};
    bool cameraCentreValid = false;
    // The play-space -> engine-world yaw, the one every lane must rotate a
    // head-relative offset by. See AimTakeover.h (the body yaw) for why it is
    // not simply cameraYaw - referenceYaw.
    float yaw = 0.0f;
    float cameraYaw = 0.0f;
    float headYaw = 0.0f;
    bool headYawUsable = false;
    float referenceYaw = 0.0f;
    std::uint64_t referenceGeneration = 0;
    // HeadTrackingRecenterEpoch, read with the generation above: a recenter
    // changes both, a snap turn only the generation.
    std::uint64_t recenterEpoch = 0;
    std::uintptr_t player = 0;
    std::uint64_t publishedNs = 0;
    std::uint64_t weaponGeneration = 0;
    twohand::Output twoHand{};
    // Surface stop (SurfaceStopLane.h), by Hand: how far from where its
    // controller puts it each hand is drawn, held back by fixed scenery.
    std::array<surface::Correction, 2> surface{};
};

// The world offset a hand is drawn at this frame (SurfaceStopLane.h): added to a
// controller position, it is where the drawn hand is.
inline Vec3 SurfaceOffset(const GameplayPoseFrame& frame, Hand hand)
{
    return frame.surface[static_cast<unsigned>(hand)].offset;
}

bool EnsureGameplayPoseObservation();
bool TryGetGameplayPoseFrame(GameplayPoseFrame& out, bool requireTracking = true);

// The aim hook's frame build (GameplayFrame.cpp), after the engine's own
// UpdateCachedReticleViewPosAndDir: the cached ray, the view camera, the latest
// tracking sample and the play-space yaw. Anything but `built` has cleared the
// published frame: a native ray out of range, or a frame that cannot be placed
// (a recenter under way, or a yaw that is not finite).
enum class FrameBuild { built, badNativeRay, notPlaceable };
FrameBuild BuildGameplayFrame(std::uintptr_t player, GameplayPoseFrame& frame, bool& haveTracking);
// Stamps and publishes the frame the lanes and the IK read, and clears it.
void PublishGameplayFrame(GameplayPoseFrame& frame);
void ClearGameplayFrame();

} // namespace preyvr::dll
