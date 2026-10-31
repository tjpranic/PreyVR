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
UiPointerVisual OptionsPointer(const ui::Panel&);
void SetVrOptionsSurface(const ui::Panel* surface,std::uint64_t reference);
// XR frame thread: this frame's controls for the panel (VrOptionsUi), captured
// by ProcessVrOptionsInput, and what the drawn frame did with them.
VrOptionsUiInput VrOptionsUiFrameInput();
// **The Controls tab's rows** (VrOptionsUi::SetControls) from what each action
// is bound to now (XrInputBoundControls): play, menus and any time, each action
// beside its buttons, with the gestures that are not one button.
std::vector<VrOptionsControlRow> VrOptionsControlRows(const std::vector<XrBoundControl>& bound);
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
// **The saved menu panel settings, before the first menu frame**: size, fit,
// curvature, guide card, distance, place, text panel and laser hand. VR start
// calls it right after LoadVrOptions. ApplySavedVrOptions comes only after the
// headset is tracking and every lane is installed -- 5 to 7 s after the first
// menu frame on 2026-10-17's runs -- and until then the menu showed at the
// defaults, then snapped to the saved ones. ApplySavedVrOptions applies them
// again; the same values refit the menu from the same anchor, unchanged.
void ApplySavedPanelOptions();
// **The saved anti-aliasing choice, before VR start's renderer settings**, which
// apply the AA mode it picks (VrMode.cpp). VR start calls it right after
// ApplySavedPanelOptions; ApplySavedVrOptions applies it again, unchanged.
void ApplySavedRendererOptions();
void ServiceVrOptions(); // worker: settings application and disk I/O
options::Values VrOptionsValues();
bool VrOptionsSaveFailed();
std::wstring VrSetupMessage();
bool PsychoscopeGestureEnabled();
bool HolstersEnabled();
bool WristDisplayEnabled();
unsigned WristSizePercent();
// The wrist card's width, metres: 18 cm at WRIST SIZE 100. Its height keeps the
// native wrist texture's aspect.
inline float WristCardWidthMetres() { return .18f * static_cast<float>(WristSizePercent()) * .01f; }
bool BeltHintsEnabled();
std::string VrOptionsReport();
}
