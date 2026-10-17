#pragma once

// Rotations the geometry tests build their fixtures from. Kept separate from
// the production builders (e.g. handpose::AxisAngle) so a test never derives
// its expected values from the code it checks.
#include "preyvr/VrMath.h"

#include <cmath>

namespace preyvr::test {

inline constexpr float kDeg = kPi / 180.0f;
inline Quaternion AboutX(float radians) { return {std::sin(radians / 2), 0, 0, std::cos(radians / 2)}; }
inline Quaternion AboutY(float radians) { return {0, std::sin(radians / 2), 0, std::cos(radians / 2)}; }

// The rotation of `radians` about `axis`, which need not be unit length.
inline Quaternion AxisAngle(Vec3 axis, float radians)
{
    const float s = std::sin(radians * 0.5f) / Length(axis);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(radians * 0.5f)};
}

} // namespace preyvr::test
