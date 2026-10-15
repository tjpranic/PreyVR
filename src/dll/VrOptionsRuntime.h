#pragma once
#include "preyvr/VrOptions.h"
#include "preyvr/UiPanel.h"
#include "UiPointer.h"
#include "VrOptionsUi.h"
#include "XrInput.h"
#include <string>
namespace preyvr::dll {
// XR frame thread owns the menu state. Worker requests are atomic mailboxes.
bool ProcessVrOptionsInput(const TrackingFrame&);
UiPointerVisual OptionsPointer(const ui::Surface&);
void SetVrOptionsSurface(const ui::Surface* surface,std::uint64_t reference);
// XR frame thread: this frame's controls for the panel (VrOptionsUi), captured
// by ProcessVrOptionsInput, and what the drawn frame did with them.
VrOptionsUiInput VrOptionsUiFrameInput();
void ApplyVrOptionsUi(const VrOptionsUiResult& result);
// XR frame thread: **the panel held by the laser** -- a trigger press on no
// control (VrOptionsUiResult::grabStarted), until the trigger is let go or the
// panel closes. `serial` counts grabs; `push` is the laser hand's stick, -1..1,
// positive pushing the panel away. VrOptionsLayer moves the panel with them;
// meanwhile the trigger and sticks reach no control.
struct VrOptionsGrab { bool active=false; unsigned serial=0; float push=0; };
VrOptionsGrab VrOptionsGrabState();
bool VrOptionsOpen();
bool VrOptionsReady();
bool VrOptionsInputOwned();
void RequestVrOptions(bool open);
void ResetVrOptionsSession(); // XR teardown
// Reads vr-options.ini, once. Nothing is applied until ApplySavedVrOptions.
void LoadVrOptions();
// **Saved menu choices win**: applies every setting the file holds (or the
// menu has set this session), over VR start's own defaults and any command
// sent before it. vr-options.ini is the one source of VR Options settings; the
// launcher passes none. VR start calls it last, on every start -- an F11
// restart included. Settings never chosen in the menu are left as they are.
void ApplySavedVrOptions();
void ServiceVrOptions(); // worker: settings application and disk I/O
options::Values VrOptionsValues();
bool VrOptionsSaveFailed();
std::wstring VrSetupMessage();
bool PsychoscopeGestureEnabled();
bool HolstersEnabled();
bool WristDisplayEnabled();
unsigned WristSizePercent();
bool BeltHintsEnabled();
std::string VrOptionsReport();
}
