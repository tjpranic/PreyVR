#pragma once

// Zero-G's roll, pitch and brake on the controllers (docs/ZERO-G-SCOPE-2026-10-30.md).
//
// Prey's own controls: its lean buttons roll the body (left, right) and, both
// held, brake; its look stick's Y pitches the whole body. Smooth (the default)
// hands them to Prey. Snap makes each roll or pitch one step of fixed degrees,
// turned by the mod about the body's own axis; both bumpers stay Prey's brake.
#include "base/VrMath.h"

#include <cmath>

namespace preyvr::zerog {

// How long a bumper pressed alone waits for the other before it is a roll step
// rather than half of a brake, in seconds.
inline constexpr float kBrakeWindow = 0.12f;

// The lean buttons to hold this frame, and a roll step to make: -1 left, +1 right.
struct BumperOutput {
    bool left = false, right = false;
    int step = 0;
};

// The bumpers, as Prey's lean buttons (smooth) or as roll steps and a brake (snap).
class Bumpers {
public:
    BumperOutput Update(bool left, bool right, bool snap, float dt)
    {
        if (!snap) {
            state_ = State::idle;
            return {left, right, 0};
        }
        const bool both = left && right;
        switch (state_) {
        case State::idle:
            if (both) {
                state_ = State::brake;
                return {true, true, 0};
            }
            if (left || right) {
                state_ = State::pending;
                side_ = right ? 1 : -1;
                waited_ = 0.0f;
            }
            return {};
        case State::pending: {
            if (both) {
                state_ = State::brake;
                return {true, true, 0};
            }
            const bool held = side_ > 0 ? right : left;
            waited_ += std::isfinite(dt) && dt > 0.0f ? dt : 0.0f;
            if (held && waited_ < kBrakeWindow) { return {}; }
            // Let go within the window, or held past it: one step.
            state_ = left || right ? State::spent : State::idle;
            return {false, false, side_};
        }
        case State::brake:
            if (both) { return {true, true, 0}; }
            state_ = left || right ? State::spent : State::idle;
            return {};
        case State::spent:
            if (both) {
                state_ = State::brake;
                return {true, true, 0};
            }
            if (!left && !right) { state_ = State::idle; }
            return {};
        }
        return {};
    }
    void Reset() { state_ = State::idle; }

private:
    enum class State { idle, pending, brake, spent };
    State state_ = State::idle;
    int side_ = 0;
    float waited_ = 0.0f;
};

// A snap pitch from the right stick: a push past 0.65 that is more up or down
// than sideways, once per push (comfort::SnapLatch's mirror, which takes the
// sideways ones). +1 up, -1 down.
class PitchLatch {
public:
    int Update(float x, float y, bool allowed)
    {
        if (!allowed || !std::isfinite(x) || !std::isfinite(y)) {
            ready_ = false;
            return 0;
        }
        if (std::fabs(x) < 0.3f && std::fabs(y) < 0.3f) {
            ready_ = true;
            return 0;
        }
        if (ready_ && std::fabs(y) > 0.65f && std::fabs(y) > std::fabs(x)) {
            ready_ = false;
            return y > 0.0f ? 1 : -1;
        }
        return 0;
    }

private:
    bool ready_ = false;
};

// A step about the body's own axis (engine: x right, y forward, z up). Pitch
// turns about x, + raising the nose; roll about y, + lowering the right side.
enum class Axis { pitch, roll };
inline Quaternion LocalStep(Axis axis, float degrees)
{
    const float half = degrees * kRadiansPerDegree * 0.5f;
    const float s = std::sin(half), c = std::cos(half);
    return axis == Axis::pitch ? Quaternion{s, 0.0f, 0.0f, c} : Quaternion{0.0f, s, 0.0f, c};
}

} // namespace preyvr::zerog
