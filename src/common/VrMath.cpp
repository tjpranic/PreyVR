#include "preyvr/VrMath.h"

#include <cmath>

namespace preyvr {
namespace {

bool IsFinite(float value)
{
    return std::isfinite(value);
}

bool IsFinite(const Vec3& value)
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

bool IsFinite(const Quaternion& value)
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

} // namespace

Quaternion Normalize(Quaternion value)
{
    const float lengthSquared =
        value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    if (!IsFinite(value) || !IsFinite(lengthSquared) || lengthSquared <= 1.0e-12f) {
        return {};
    }

    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    return {
        value.x * inverseLength,
        value.y * inverseLength,
        value.z * inverseLength,
        value.w * inverseLength,
    };
}

Quaternion Multiply(Quaternion parent, Quaternion child)
{
    parent = Normalize(parent);
    child = Normalize(child);
    return Normalize({
        parent.w * child.x + parent.x * child.w + parent.y * child.z - parent.z * child.y,
        parent.w * child.y - parent.x * child.z + parent.y * child.w + parent.z * child.x,
        parent.w * child.z + parent.x * child.y - parent.y * child.x + parent.z * child.w,
        parent.w * child.w - parent.x * child.x - parent.y * child.y - parent.z * child.z,
    });
}

Vec3 Rotate(Quaternion rotation, Vec3 value)
{
    rotation = Normalize(rotation);
    const Vec3 axis{rotation.x, rotation.y, rotation.z};
    const Vec3 doubledCross = [&] {
        const Vec3 cross = Cross(axis, value);
        return Vec3{2.0f * cross.x, 2.0f * cross.y, 2.0f * cross.z};
    }();
    const Vec3 secondCross = Cross(axis, doubledCross);
    return {
        value.x + rotation.w * doubledCross.x + secondCross.x,
        value.y + rotation.w * doubledCross.y + secondCross.y,
        value.z + rotation.w * doubledCross.z + secondCross.z,
    };
}

Pose Compose(Pose parent, Pose child)
{
    const Vec3 rotatedChild = Rotate(parent.orientation, child.position);
    return {
        Multiply(parent.orientation, child.orientation),
        {
            parent.position.x + rotatedChild.x,
            parent.position.y + rotatedChild.y,
            parent.position.z + rotatedChild.z,
        },
    };
}

Quaternion QuaternionFromBasis(Vec3 x, Vec3 y, Vec3 z)
{
    const float m00 = x.x, m01 = y.x, m02 = z.x;
    const float m10 = x.y, m11 = y.y, m12 = z.y;
    const float m20 = x.z, m21 = y.z, m22 = z.z;
    Quaternion q{};
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return Normalize(q);
}

bool IsPoseUsable(const Pose& pose, const PoseValidity& validity, std::uint64_t maxAgeNanoseconds)
{
    const float orientationLengthSquared =
        pose.orientation.x * pose.orientation.x +
        pose.orientation.y * pose.orientation.y +
        pose.orientation.z * pose.orientation.z +
        pose.orientation.w * pose.orientation.w;
    return validity.orientationValid && validity.positionValid &&
        validity.orientationTracked && validity.positionTracked &&
        validity.ageNanoseconds <= maxAgeNanoseconds &&
        IsFinite(pose.orientation) && IsFinite(pose.position) &&
        IsFinite(orientationLengthSquared) && orientationLengthSquared > 1.0e-12f;
}

} // namespace preyvr
