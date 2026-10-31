#pragma once
#include "base/VrMath.h"

// **The ray a screen is pointed at with, while it is examined.**
//
// A terminal's or keypad's cursor is where the player's cached reticle ray meets
// the screen (aim/WorldUiNative.h). The aim lane gives that ray the right
// hand's direction but leaves its origin at the eye, so at a screen half a metre
// away the cursor lands as far from the controller's line as the hand is from
// the eyes -- about the size of the screen. While a screen is examined the lane
// starts the ray on the hand's own line instead. See
// docs/WORLD-UI-POINTER-2026-10-26.md.
namespace preyvr::examine {

// Where the ray along `direction` (unit) through `hand` starts: on that line,
// level with `eye` (in the plane through the eye across the direction), so a hand
// that has reached past the screen still points at it. A hand that is not ahead
// of the eye starts it.
inline Vec3 ScreenRayOrigin(Vec3 hand, Vec3 eye, Vec3 direction)
{
    const float ahead = Dot(Sub(hand, eye), direction);
    return ahead > 0.0f ? Sub(hand, Scale(direction, ahead)) : hand;
}

} // namespace preyvr::examine
