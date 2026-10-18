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
// keyboard/mouse scheme to the gamepad one as it is set.
//
// ui.prompts / VR Options' Controller Prompts: 1 sets it through the verified
// console path; 0 puts Prey's 0 back, only if this lane may have changed it. On
// at VR start, put back when VR stops. Returns 0, or 1 for a bad value.
DWORD SetControllerPrompts(unsigned enabled);
unsigned ControllerPromptsEnabled();
// Command worker, beside the focus hold: they share the verified console slot,
// so the next completion after a queue is ours. Queues the wanted value and
// logs its readback.
void ServiceControllerPrompts();
std::string ControllerPromptsReport();
} // namespace preyvr::dll
