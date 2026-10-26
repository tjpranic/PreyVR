#pragma once
#include "preyvr/VrMath.h"

#include <algorithm>
#include <cmath>

// **Screen Distance: how far Prey's examination camera stays back from a screen.**
//
// Using a keypad, terminal, security station or fabricator glides the camera up
// to it (preyvr/ExamineNative.h). For these world UIs Prey's fit puts the camera
// where the screen and a small margin fill the view camera's FOV, along the
// screen's normal, facing it. Flat, that fills the monitor. In the headset the
// eye buffer is square, so the vertical FOV is the whole FOV slider (~117
// degrees), and a keypad lands about 10 cm from the eyes, at 9/16 of its flat
// distance. See docs/KEYPAD-CAMERA-SCOPE-2026-10-26.md.
//
// The camera is moved back along its own view, so the screen stays centred, by
// Screen Distance -- but never further back than the eye was when the
// examination began. So the glide never carries you backwards, and the camera
// stays on your side of anything facing the screen: Prey closes an examination
// whose camera loses sight of the screen.
namespace preyvr::examine {

inline constexpr unsigned kMinCm = 0, kMaxCm = 100, kDefaultCm = 30;

inline unsigned ClampCm(unsigned cm) { return std::min(cm, kMaxCm); }
inline float Metres(unsigned cm) { return 0.01f * static_cast<float>(ClampCm(cm)); }

// Where a camera facing `q` looks: CryEngine's forward, +Y, rotated by `q`
// (x, y, z, w as Prey stores it). Unit length, or zero for a `q` that is no
// rotation (squared length outside 0.5..2, or not finite).
inline Vec3 Forward(Quaternion q)
{
    const float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!std::isfinite(n) || n < 0.5f || n > 2.0f) { return {}; }
    const float s = 1.0f / std::sqrt(n);
    q = {q.x * s, q.y * s, q.z * s, q.w * s};
    const Vec3 f{2.0f * (q.x * q.y - q.w * q.z), 1.0f - 2.0f * (q.x * q.x + q.z * q.z), 2.0f * (q.y * q.z + q.w * q.x)};
    return Scale(f, 1.0f / Length(f));
}

// How far the eye was behind the fit point, along the camera's view: positive
// when it was further from the screen.
inline float EyeDepth(Vec3 fit, Vec3 forward, Vec3 eye) { return Dot(Sub(fit, eye), forward); }

struct Placement {
    Vec3 position{};
    float pull = 0.0f;   // metres moved back
};

// The camera `cm` behind the fit point, at most `eyeDepth` (metres; not finite
// when the eye is not known, and then not moved), along `facing`'s view.
inline Placement Place(Vec3 fit, Quaternion facing, unsigned cm, float eyeDepth)
{
    const Vec3 forward = Forward(facing);
    if (!Finite(fit) || Length(forward) == 0.0f || !std::isfinite(eyeDepth)) { return {fit, 0.0f}; }
    const float pull = std::clamp(Metres(cm), 0.0f, std::max(eyeDepth, 0.0f));
    return {Sub(fit, Scale(forward, pull)), pull};
}

} // namespace preyvr::examine
