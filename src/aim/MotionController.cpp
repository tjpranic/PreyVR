#include "aim/MotionController.h"

#include <cmath>

namespace preyvr::controller {
namespace {


std::optional<Vec3> TryNormalize(const Vec3& v, float minimumLength = 1e-6f)
{
    const float length = Length(v);
    if (!std::isfinite(length) || length < minimumLength) {
        return std::nullopt;
    }
    return Vec3{v.x / length, v.y / length, v.z / length};
}

} // namespace

Vec3 ForwardOf(const Pose& engineSpacePose)
{
    return Rotate(Normalize(engineSpacePose.orientation), Vec3{0.0f, 1.0f, 0.0f});
}

Pose ControllerPoseInWorld(
    const stereo::ReferenceFrame& reference,
    const Pose& openXrControllerPose)
{
    // Deliberately the same composition the eyes use. If the hand and the view
    // were built from different reference frames they would drift apart the
    // moment the player snap-turned, which reads as the weapon lagging the world.
    return stereo::EyePoseInWorld(reference, openXrControllerPose);
}

std::optional<AimRay> AimFromController(
    const stereo::ReferenceFrame& reference,
    const Pose& openXrControllerPose,
    const PoseValidity& validity,
    std::uint64_t maxAgeNanoseconds)
{
    if (!IsPoseUsable(openXrControllerPose, validity, maxAgeNanoseconds)) {
        return std::nullopt;
    }
    const Pose world = ControllerPoseInWorld(reference, openXrControllerPose);
    const auto direction = TryNormalize(ForwardOf(world));
    if (!direction) {
        return std::nullopt;
    }
    return AimRay{world.position, *direction};
}

namespace {

bool IsUsableRotation(const Quaternion& q)
{
    const float lengthSquared = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    return std::isfinite(lengthSquared) && lengthSquared > 0.25f && lengthSquared < 4.0f;
}


} // namespace

std::optional<Quaternion> MountRotationFromControllerDelta(
    const Quaternion& authoredMount,
    const Quaternion& calibrationAim,
    const Quaternion& currentAim)
{
    if (!IsUsableRotation(authoredMount) || !IsUsableRotation(calibrationAim) ||
        !IsUsableRotation(currentAim)) {
        return std::nullopt;
    }
    const Quaternion delta = Normalize(Multiply(currentAim, Conjugate(Normalize(calibrationAim))));
    // Authored mount on the RIGHT: it is the basis being changed.
    const Quaternion composed = Normalize(Multiply(delta, Normalize(authoredMount)));
    if (!IsUsableRotation(composed)) {
        return std::nullopt;
    }
    return composed;
}

float WrapAngle(float radians)
{
    if (!std::isfinite(radians)) {
        return 0.0f;
    }
    while (radians > kPi) {
        radians -= 2.0f * kPi;
    }
    while (radians <= -kPi) {
        radians += 2.0f * kPi;
    }
    return radians;
}

float SnapTurn(float currentYawRadians, int steps, float stepRadians)
{
    if (!std::isfinite(currentYawRadians) || !std::isfinite(stepRadians)) {
        return 0.0f;
    }
    return WrapAngle(currentYawRadians + static_cast<float>(steps) * stepRadians);
}

} // namespace preyvr::controller
