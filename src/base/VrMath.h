#pragma once

#include <cmath>
#include <cstdint>

namespace preyvr {

inline constexpr float kPi = 3.14159265358979323846f;
// Degrees to radians, and back: the floats nearest pi/180 and 180/pi. Not
// 180.0f / kPi, which rounds to the float one below.
inline constexpr float kRadiansPerDegree = kPi / 180.0f;
inline constexpr float kDegreesPerRadian = 57.29578f;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct Pose {
    Quaternion orientation{};
    Vec3 position{};
};

struct PoseValidity {
    bool orientationValid = false;
    bool positionValid = false;
    bool orientationTracked = false;
    bool positionTracked = false;
    std::uint64_t ageNanoseconds = 0;
};

// Plain vector algebra, one copy for every module. Same operations in the same
// order as each local copy these replaced, so the results are bit-identical.
inline Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 Scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
// The share of the way to its target an exponential filter at `perSecond` moves
// in `dt` seconds: 1 - e^(-dt * perSecond). Not the time-constant form
// 1 - e^(-dt / tau), which rounds differently.
inline float ExpAlpha(float dt, float perSecond) { return 1.0f - std::exp(-dt * perSecond); }
// Smoothstep on [0, 1]: `x` clamped as std::clamp does (NaN stays NaN), then
// 3x^2 - 2x^3, which starts and ends with zero slope: a fade with no visible start or end.
inline float SmoothStep(float x)
{
    x = x < 0.0f ? 0.0f : (1.0f < x ? 1.0f : x);
    return x * x * (3.0f - 2.0f * x);
}

// The inverse of a unit quaternion.
inline Quaternion Conjugate(Quaternion q) { return {-q.x, -q.y, -q.z, q.w}; }

Quaternion Normalize(Quaternion value);
Quaternion Multiply(Quaternion parent, Quaternion child);
Vec3 Rotate(Quaternion rotation, Vec3 value);
Pose Compose(Pose parent, Pose child);
// The rotation whose columns are the orthonormal basis (x, y, z), i.e. the
// images of the unit axes; normalised.
Quaternion QuaternionFromBasis(Vec3 x, Vec3 y, Vec3 z);
bool IsPoseUsable(const Pose& pose, const PoseValidity& validity, std::uint64_t maxAgeNanoseconds);

} // namespace preyvr
