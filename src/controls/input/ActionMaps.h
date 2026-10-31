#pragma once

// **Prey's live action maps** (controls/input/ActionMapWalk.h), read from the running
// CActionMapManager for two things: the bindings, into the log, and whether the
// weapon wheel's map is enabled (PreyWheelOpen, which MoveLane reads every
// frame). Called BindingDump until 2026-10-24.
//
// **The bindings, into the log:** which game action each `xi_*` key drives.
// A valid key that nothing is bound to reads exactly like a working one in the
// mod's own counters (R-124), so a key the mod posts is proven by this list,
// not by being accepted.
//
// Read-only: guarded reads of the manager's own structures, validated against
// this build's vtables, on the main thread. Logged once per session after two
// seconds of play (a loaded game, not the main menu), and again on
// `input.binds [all]`.
namespace preyvr::dll {

void RequestBindingDump(bool allKeys);
// Main thread, once per rendered frame (FrameSchedule.cpp, beside the lanes).
void ServiceBindingDump();

// **Prey's own weapon wheel is open**: its `focusmode` action map is enabled
// (ReadActionMapEnabled). Main thread, every frame: the map is found once by a
// binding walk -- retried every two seconds until Prey has its bindings -- and
// after that each call is a handful of guarded, re-checked reads. False until
// the map is found, and whenever it fails to check out (then found again).
// Each change is logged, so a headset run shows the wheel opening and closing.
bool PreyWheelOpen();

} // namespace preyvr::dll
