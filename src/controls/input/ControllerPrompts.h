#pragma once
#include <windows.h>

#include <string>

namespace preyvr::dll {
// **Prey's menus keep the controller's prompts while the laser points at them.**
// docs/CONTROLLER-PROMPTS-2026-10-18.md.
//
// The laser posts a zero mouse axis before each move (UiPointer.cpp), and Prey's
// last-input-device listener 0x182D3A0 answers any keyboard or mouse event by
// switching every menu to its keyboard/mouse scheme: PC prompts, and the
// inventory's item menu -- Y on a gamepad -- closing on the laser's next move.
// Prey's own cvar hud_forceControllerGlyphs (registered default 0, no flags, so
// never saved) skips that switch, and its change callback 0x1726630 moves a
// keyboard/mouse scheme to a controller one as it is set.
//
// **The cvar alone is not enough.** That callback picks the Steam Controller's
// scheme (5) whenever an input device answers "Steam Controller", and Prey's
// Steam device always does -- it is created whenever Steamworks starts, Steam
// Input on or off, and Prey then creates no XInput devices. Scheme 5 asks Steam
// for each prompt's button and, given none, prints a text label: no controller
// prompts until the first posted pad event moves the scheme to the Xbox pad (3).
// So while the cvar is in force, a check on the main thread puts any keyboard,
// mouse or Steam Controller scheme back on the pad through Prey's own setter
// (ServiceControllerPromptsFrame).
//
// 1 sets it through the verified console path; 0 puts Prey's 0 back, only if this
// lane may have changed it. On at VR start, put back when VR stops. Returns 0, or
// 1 for a bad value.
DWORD SetControllerPrompts(unsigned enabled);
// Command worker, beside the focus hold: they share the verified console slot,
// so the next completion after a queue is ours. Queues the wanted value, logs
// its readback, sets it again if anything else clears it, and logs the
// frame check's corrections.
void ServiceControllerPrompts();
// Prey's main thread, once a frame after the input drains (MainRenderHook's
// CSystem::Render detour). While this lane's 1 is in force: keeps the menus on
// the pad's scheme. Gated on the setter's, callback's and tracker's bytes; does
// nothing otherwise.
void ServiceControllerPromptsFrame();
std::string ControllerPromptsReport();
} // namespace preyvr::dll
