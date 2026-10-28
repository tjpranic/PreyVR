#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

// `body.depth`: the player's body (arms, body, legs and the weapon on them) at
// the world's depth, so what is in front of it hides it, instead of in front of
// everything, as Prey's near pass draws it (preyvr/BodyDepth.h).
//
// 1 (default), the near pass at the world's depth, in the main view: render
// thread, a near render pass's depth slice is its viewport's own (SetViewport),
// the legacy pipeline's switch into the near pass for an eye's camera keeps the
// camera's own max depth (UpdateNearestChange), and NearViewStereo's packer
// gives the near projection the world's depth. What Prey draws near for the
// weapon -- its effects, its display -- stays with it.
//
// 2, the body drawn in the world (PreyVR-91): main thread, every slot render,
// the player's slot, in the main view only, is drawn with its near bits clear
// for that one call, and they are put back after it.
//
// Either way the shadow and other passes, and every other slot, are Prey's.
namespace preyvr::dll {

// 1 (default) the near pass at the world's depth; 2 the body drawn in the world;
// 0 Prey's near pass, for A/B. 1 and 2 install the hooks if they are not in yet:
// ERROR_NOT_SUPPORTED when they cannot be.
DWORD SetBodyDepth(unsigned mode);
// The mode, 0 to 2.
unsigned BodyDepthEnabled();
// Installs the hooks, gated on the three functions' whole bodies. Called when VR
// starts; a failure leaves the body as Prey draws it and is reported, never
// fatal. PREYVR_BODY_DEPTH=0: not hooked.
void EnsureBodyDepth();
// The gameplay lanes (FrameSchedule.cpp), main thread, before the frame's
// render: the player's first-person character, the one whose slot is drawn in
// the world. Only compared with each slot's character, never followed.
void PublishBodyDepthPlayer(std::uintptr_t player);

std::string BodyDepthReport();

} // namespace preyvr::dll
