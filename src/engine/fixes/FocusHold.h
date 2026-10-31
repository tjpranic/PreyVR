#pragma once
#include <windows.h>

#include <string>

namespace preyvr::dll {
// **While held, Prey runs as though its window had focus.**
// docs/DESKTOP-WINDOW-2026-10-17.md.
//
// Prey reacts to losing activation in two ways that matter in a headset. While
// GetActiveWindow() is not the render window, CSystem::Update pumps messages and
// sleeps 5 ms up to five times a frame (0xDF58F0): up to 25 ms on every frame.
// And g_pauseOnLoseFocus (registered default 1) pauses the game. A minimised
// window, or any other window clicked, would stop the game and drop the headset
// to a fraction of its rate.
//
// And the audio translation layer runs its "lose_focus" trigger and the
// middleware's OnLoseFocus: silence while minimised.
//
// The hold answers PreyDll's only GetActiveWindow call -- its import slot, gated
// on the throttle loop's bytes -- with the render window, and sets
// g_pauseOnLoseFocus 0 through the verified console path. A detour on the audio
// dispatcher (0xD5B950) marks lose-focus requests done without running them,
// and issues the get-focus or lose-focus the audio is owed when the hold starts
// or ends. Input focus, gamma and Windows' own activation are left as they are.
// The launcher arms it (window.hold 1) and minimises the window only after
// state=held is logged.
//
// window.hold: 1 holds (installing the import on first use), 0 releases and puts
// g_pauseOnLoseFocus back to 1. Returns 0, 1 for a bad value, or a refusal code;
// a hold waiting for the render window returns 0 and logs state=pending.
DWORD SetFocusHold(unsigned int enabled);
// Command worker: completes a pending hold, sets or restores the cvar, and logs
// each change of the window's foreground or minimised state while held.
void ServiceFocusHold();
std::string FocusHoldReport();
} // namespace preyvr::dll
