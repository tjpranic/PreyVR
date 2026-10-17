#pragma once

#include <cmath>
#include <cstdint>

namespace preyvr {

inline constexpr float kPi = 3.14159265358979323846f;

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

// The inverse of a unit quaternion.
inline Quaternion Conjugate(Quaternion q) { return {-q.x, -q.y, -q.z, q.w}; }

Quaternion Normalize(Quaternion value);
Quaternion Multiply(Quaternion parent, Quaternion child);
Vec3 Rotate(Quaternion rotation, Vec3 value);
Pose Compose(Pose parent, Pose child);
// The rotation whose columns are the orthonormal basis (x, y, z), i.e. the
// images of the unit axes; normalised.
Quaternion QuaternionFromBasis(Vec3 x, Vec3 y, Vec3 z);
Vec2 ApplyRadialDeadzone(Vec2 input, float deadzone);
bool IsPoseUsable(const Pose& pose, const PoseValidity& validity, std::uint64_t maxAgeNanoseconds);

} // namespace preyvr
