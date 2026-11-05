#pragma once

#include <string>

// The player's animation keeps running while Prey stops the player's clock
// (docs/TIME-STOP-SCOPE-2026-11-05.md).
//
// The Favorites Wheel and aiming a psi power stop the player's clock. Prey's
// AnimatedCharacter then gives every other character a tiny frame time, so its
// animation is still started, but leaves the player's at 0, and its
// PrePhysicsUpdate returns before starting the player's. With no animation job
// there is no ProcessAnimationDrivenIK: the hands, the body and the wrist card
// stop following the headset and the controllers.
//
// While VR is on, the hook starts the player's animation on such a frame itself,
// with Prey's own call, where Prey makes it. The job runs on the character's own
// clock, so its delta is 0: Prey's pose holds still and the IK moves the arms.
// The movement and physics work that PrePhysicsUpdate also skips stays skipped.
//
// Each stop logs where it began and ended, with the animations started and the
// IK frames matched in between.
namespace preyvr::dll {

// VR start: installs the hook, gated on the bodies it rests on. A failure leaves
// Prey's behaviour and is reported, never fatal.
void EnsureTimeStopAnimation();
// VR stop: Prey's behaviour again (the hook stays and passes through).
void StopTimeStopAnimation();

std::string TimeStopAnimationReport();

} // namespace preyvr::dll
