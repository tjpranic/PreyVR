#pragma once

// Rotations the geometry tests build their fixtures from. Kept separate from
// the production builders (e.g. handpose::AxisAngle) so a test never derives
// its expected values from the code it checks.
#include "base/VrMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

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

// The rotation of `degrees` about `axis`, which need not be unit length.
inline Quaternion About(Vec3 axis, float degrees)
{
    const float h = degrees * 3.14159265f / 360.0f;
    const float n = Length(axis);
    return {axis.x / n * std::sin(h), axis.y / n * std::sin(h), axis.z / n * std::sin(h), std::cos(h)};
}

// A 4x4 matrix as Prey's: row-major, row vectors, translation in row 3.
using Matrix = std::array<float, 16>;

inline Matrix Multiply(const Matrix& a, const Matrix& b)
{
    Matrix m{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            for (int k = 0; k < 4; ++k) { m[4 * i + j] += a[4 * i + k] * b[4 * k + j]; }
        }
    }
    return m;
}

// Where the segment from `a` along `d` enters the box [lo, hi], as a fraction of
// `d`, and the axis it enters on. False when it starts inside or misses.
inline bool EntersBox(Vec3 boxLo, Vec3 boxHi, Vec3 a, Vec3 d, float& enter, int& axis)
{
    const float lo[3] = {boxLo.x, boxLo.y, boxLo.z}, hi[3] = {boxHi.x, boxHi.y, boxHi.z};
    const float o[3] = {a.x, a.y, a.z}, v[3] = {d.x, d.y, d.z};
    bool inside = true;
    for (int i = 0; i < 3; ++i) { inside &= o[i] > lo[i] && o[i] < hi[i]; }
    if (inside) { return false; }
    float leave = 1e9f;
    enter = -1e9f;
    axis = -1;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(v[i]) < 1e-9f) {
            if (o[i] < lo[i] || o[i] > hi[i]) { return false; }
            continue;
        }
        float t0 = (lo[i] - o[i]) / v[i], t1 = (hi[i] - o[i]) / v[i];
        if (t0 > t1) { std::swap(t0, t1); }
        if (t0 > enter) { enter = t0; axis = i; }
        leave = std::min(leave, t1);
    }
    return axis >= 0 && enter <= leave && enter >= 0 && enter <= 1;
}

} // namespace preyvr::test
