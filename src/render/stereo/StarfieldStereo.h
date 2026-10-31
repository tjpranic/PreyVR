#pragma once

#include <windows.h>

#include <string>

// `stars.depth`: the starfield at the sky's depth in both eyes, instead of 1 m
// from the head (render/stereo/StarfieldDepth.h).
//
// Render thread, once per eye submission: Prey's ArkStars draw is handed a copy
// of the space sky's render element whose star block draws the stars 10 km
// out. Prey's own element and block are only read, never written. The Milky
// Way, Earth, Moon and sun are left as Prey draws them; they were already far
// enough out, or drawn as directions.
namespace preyvr::dll {

// 1 (default) the stars far out; 0 where Prey puts them, for A/B. 1 installs
// the hook if it is not in yet: ERROR_NOT_SUPPORTED when it cannot be.
DWORD SetStarfieldDepth(unsigned enabled);
unsigned StarfieldDepthEnabled();
// Installs the hook, gated on the ArkStars draw's whole body. Called when VR
// starts; a failure leaves the stars as Prey draws them and is reported, never
// fatal. PREYVR_STAR_DEPTH=0: not hooked.
void EnsureStarfieldDepth();

std::string StarfieldDepthReport();

} // namespace preyvr::dll
