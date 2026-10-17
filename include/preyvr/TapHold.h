#pragma once

#include <cmath>

namespace preyvr::input {

// **One button, two meanings by how long it is held**, as Prey's own gamepad Y:
// let go within `holdSeconds` and it is a tap (one frame, on the release); held
// past it, a hold until it is let go. The mod's Y: a tap swaps weapon, a hold
// opens the weapon wheel.
//
// After Reset (a menu opening, input lost), a button still down is ignored until
// it has been let go: a Y held through a menu must not swap weapon on release.
class TapHold {
public:
    struct Out { bool tap = false, hold = false; };
    explicit TapHold(float holdSeconds = 0.3f) : holdSeconds_(holdSeconds) {}

    Out Update(bool down, float seconds)
    {
        Out out{};
        if (!down) {
            out.tap = armed_ && down_ && !holding_;
            armed_ = true;
            down_ = holding_ = false;
            heldFor_ = 0.0f;
            return out;
        }
        if (!armed_) { return out; }
        if (!down_) {
            down_ = true;
            heldFor_ = 0.0f;
        } else if (std::isfinite(seconds) && seconds > 0.0f) {
            heldFor_ += seconds;
        }
        holding_ = holding_ || heldFor_ >= holdSeconds_;
        out.hold = holding_;
        return out;
    }
    void Reset()
    {
        armed_ = down_ = holding_ = false;
        heldFor_ = 0.0f;
    }

private:
    float holdSeconds_;
    float heldFor_ = 0.0f;
    bool armed_ = true, down_ = false, holding_ = false;
};

} // namespace preyvr::input
