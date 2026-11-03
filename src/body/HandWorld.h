#pragma once

// Where a controller is in engine world this frame: its tracked pose turned by
// the frame's play-space yaw about an anchor (animik::ControllerWorldFromHead).
// Every hand placement goes through here, so the anchor it is measured from, and
// whether it is the drawn hand, are named at the call site.
//
// Header-only, so the tests that compile a lane's .cpp directly need nothing
// extra.
#include "aim/GameplayFrame.h"
#include "base/MonotonicClock.h"
#include "body/ik/AnimIk.h"

#include <cmath>

namespace preyvr::dll {

// The point a head-relative offset is rotated about.
enum class Anchor {
    // The view camera's centre: where the hands are drawn from. Refused when
    // the frame could not read the camera -- never replaced by the reticle
    // origin, which would bring back the IK crosstalk (GameplayFrame.h).
    camera,
    // The camera centre, else the reticle origin.
    cameraElseReticle,
    // The engine's cached reticle-ray origin: where the aim ray starts.
    reticle,
};

// False only for Anchor::camera on a frame without a camera.
inline bool AnchorPoint(const GameplayPoseFrame& frame, Anchor anchor, Vec3& out)
{
    switch (anchor) {
    case Anchor::camera:
        if (!frame.cameraCentreValid) { return false; }
        out = frame.cameraCentre;
        return true;
    case Anchor::cameraElseReticle:
        out = frame.cameraCentreValid ? frame.cameraCentre : frame.reticleOrigin;
        return true;
    case Anchor::reticle:
        out = frame.reticleOrigin;
        return true;
    }
    return false;
}

enum class ControllerPose {
    grip,
    aim,
    // The index finger a hand with no weapon points with (FingerPose, GameplayFrame.h).
    finger,
};

enum class Placement {
    // Where the controller is.
    controller,
    // Where the hand is drawn: the controller plus the surface stop's offset
    // (SurfaceStopLane.h), held back by what it would go through.
    drawn,
};

// `hand`'s controller pose in engine world. False only when the anchor is
// unavailable (AnchorPoint), and then `out` is untouched. The pose's own
// freshness and finiteness stay with the caller: the sites check different
// validity fields and ages, and refuse with their own reasons (ControllerGrip
// below is the one check two lanes share).
inline bool HandWorld(const GameplayPoseFrame& frame, Hand hand, ControllerPose pose, Anchor anchor,
                      Placement placement, Pose& out)
{
    Vec3 at{};
    if (!AnchorPoint(frame, anchor, at)) { return false; }
    const auto& controller = frame.tracking.hands[static_cast<unsigned>(hand)];
    const Pose tracked = pose == ControllerPose::finger ? FingerPose(frame, hand)
                       : pose == ControllerPose::aim    ? controller.aimPose
                                                        : controller.gripPose;
    out = animik::ControllerWorldFromHead(PlaySpaceRotation(frame), at, frame.tracking.head, tracked);
    if (placement == Placement::drawn) { out.position = Add(out.position, SurfaceOffset(frame, hand)); }
    return true;
}

// `hand`'s grip at the controller, from the camera centre: what the contact box
// and the surface stop's probes are built on, checked as both refuse it. Null,
// or the refusal: "hand_pose" for a stale or non-finite pose, "camera" for a
// frame without the camera.
inline const char* ControllerGrip(const GameplayPoseFrame& frame, Hand hand, Pose& out)
{
    const auto& h = frame.tracking.hands[static_cast<unsigned>(hand)];
    if (!IsPoseUsable(h.gripPose, h.gripValidity, kMaxPoseAgeNs)) { return "hand_pose"; }
    if (!HandWorld(frame, hand, ControllerPose::grip, Anchor::camera, Placement::controller, out)) { return "camera"; }
    const auto& q = out.orientation;
    if (!Finite(out.position) || !std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) {
        return "hand_pose";
    }
    return nullptr;
}

} // namespace preyvr::dll
