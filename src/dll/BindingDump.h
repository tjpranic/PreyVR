#pragma once

// **Prey's live gamepad bindings, into the log** (preyvr/ActionMapWalk.h): which
// game action each `xi_*` key drives, read from the running CActionMapManager.
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
// Main thread, once per rendered frame (CameraEditHook, beside the lanes).
void ServiceBindingDump();

} // namespace preyvr::dll
