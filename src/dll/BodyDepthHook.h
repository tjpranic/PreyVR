#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

// `body.depth`: the player's body (arms, body, legs and the weapon on them)
// drawn in the world, so what is in front of it hides it, instead of in Prey's
// near pass, which is drawn over everything (preyvr/BodyDepth.h).
//
// Main thread, every slot render: the player's slot, in the main view only, is
// drawn with its near bits clear for that one call, and they are put back after
// it. The shadow and other passes, and every other slot, are Prey's.
namespace preyvr::dll {

// 1 (default) the body in the world; 0 Prey's near pass, for A/B. 1 installs
// the hook if it is not in yet: ERROR_NOT_SUPPORTED when it cannot be.
DWORD SetBodyDepth(unsigned enabled);
unsigned BodyDepthEnabled();
// Installs the hook, gated on the slot render's whole body. Called when VR
// starts; a failure leaves the body as Prey draws it and is reported, never
// fatal. PREYVR_BODY_DEPTH=0: not hooked.
void EnsureBodyDepth();
// The gameplay lanes (FrameSchedule.cpp), main thread, before the frame's
// render: the player's first-person character, the one whose slot is drawn in
// the world. Only compared with each slot's character, never followed.
void PublishBodyDepthPlayer(std::uintptr_t player);

std::string BodyDepthReport();

} // namespace preyvr::dll
