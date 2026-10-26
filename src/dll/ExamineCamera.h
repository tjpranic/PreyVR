#pragma once

#include <windows.h>

#include <string>

// `examine.pullback`: Screen Distance (VR Options, Interface). How far the
// camera stays back from a keypad, terminal or other screen you use
// (preyvr/ExaminePullback.h, docs/KEYPAD-CAMERA-SCOPE-2026-10-26.md).
//
// Main thread, inside ArkPlayerCamera::UpdateView: each time Prey's fit places
// the examination camera for a world UI -- once as the examination begins, then
// every frame of it -- the camera it wrote is moved back along its view. Prey's
// own glide in and out carries it, so neither jumps. Telescopes and other
// examinations are left as Prey places them.
namespace preyvr::dll {

// 0..100 cm (default 30); 0 is Prey's own camera. Out of range is refused
// (ERROR_INVALID_PARAMETER). Only stores the value: VR start hooks.
DWORD SetExaminePullbackCm(unsigned cm);
unsigned ExaminePullbackCm();
// VR start: installs the hook, gated on the fit, the state setter and the
// examination's update, and moves the camera from then on. A failure leaves
// Prey's camera and is reported, never fatal. PREYVR_EXAMINE_CAMERA=0: not
// hooked.
void EnsureExamineCamera();
// VR stop: Prey's own camera again (the hook stays and passes through).
void StopExamineCamera();

std::string ExamineCameraReport();

} // namespace preyvr::dll
