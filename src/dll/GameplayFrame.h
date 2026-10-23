#pragma once

// The per-frame input every gameplay lane is handed: one XR tracking sample
// plus the engine's own camera and play-space yaw for the same frame. The aim
// hook (AimTakeover.cpp) produces and publishes it, then runs the lanes on it.
#include "XrInput.h"
#include "preyvr/SurfaceStop.h"
#include "preyvr/TwoHandedAim.h"

#include <array>
#include <cstdint>

namespace preyvr::dll {

// Native producer output plus one XR publication, captured before aim writes.
struct GameplayPoseFrame {
    TrackingFrame tracking{};
    // Historical name: this holds cachedReticleOrigin, an unprojected
    // reticle-ray point, NOT the camera centre. A fresh pre-write snapshot does
    // not make it independent of aim.reticle. The hand anchor is `cameraCentre`
    // below; see docs/RE-IK-CROSSTALK-2026-09-09.md before treating this as one.
    Vec3 nativeEye{};
    // **The camera centre, which is what a head-relative offset must be rotated
    // about.** `nativeEye` above is the native cached RETICLE ray origin -- an
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
    // head-relative offset by. See SetAimBodyYaw (AimTakeover.h) for why it is
    // not simply cameraYaw - referenceYaw.
    float yaw = 0.0f;
    float cameraYaw = 0.0f;
    float headYaw = 0.0f;
    bool headYawUsable = false;
    float referenceYaw = 0.0f;
    std::uint64_t referenceGeneration = 0;
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

} // namespace preyvr::dll
