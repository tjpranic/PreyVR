#pragma once
#include <windows.h>

namespace preyvr::dll {
// **Prey's loading screen in the headset** (docs/LOADING-SCREEN-2026-10-20.md).
// While a level loads, Prey's render thread draws the loading screen in a loop of
// its own and presents it without RT_EndFrame, where every XR frame is made, so
// the headset showed the frame from before the load: black. This detours that
// loop's present so the XR session can copy the screen first
// (CaptureXrLoadingScreen). Code-gated; installed once and kept for the life of
// the process, and a no-op without a running session. 0 when installed.
DWORD EnsureLoadingScreenHook();
// The loading loop's presents seen since install.
unsigned long long LoadingScreenPresentCount();
} // namespace preyvr::dll
