#pragma once

#include <windows.h>

#include <string>

// Part of `dual.overlay` (DualOverlay.h): Prey's fullscreen videos -- the first
// neuromod install, the ApexVision clips, the endings -- in both eyes
// (preyvr/VideoLetterbox.h).
//
// Render thread, once a frame while one plays: Prey's draw of the video is
// left as it is. Inside a both-eyes frame's overlay window, when that draw
// cleared the target and letterboxed, the backbuffer -- black and the video,
// nothing of the world -- is then copied into the held first eye
// (XrSessionHost CopyBackBufferIntoHeldEye). Not drawn twice: the draw's clear
// goes to the engine's own view of the backbuffer, which a redirected second
// draw (DrawRedirected) would clear after the video was drawn into it.
// Screen-space, like the rest of the overlays.
namespace preyvr::dll {

// Installs the hook, gated on the draw's and GetViewport's whole bodies.
// SetDualOverlayRedraw(1) calls it; without it the rest of dual.overlay still
// runs and the video stays in the second eye. Tried once a PreyDll is loaded: a
// refused gate or a failed hook stays refused. 0; ERROR_NOT_READY before PreyDll
// is loaded; ERROR_NOT_SUPPORTED when it cannot be hooked.
DWORD EnsureVideoOverlay();

// " video={hook=1 gate=ok draws=0 copied=0 refused=0 lastRefusal=none}"
std::string VideoOverlayReport();

} // namespace preyvr::dll
