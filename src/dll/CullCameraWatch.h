#pragma once
#include <windows.h>

#include <string>

namespace preyvr::dll {
// Read-only watch on the engine's pass-camera pool reset, for the recurring
// job-worker crash at PreyDll+0x27EC40 (read 0x23C). docs/CULL-CAMERA-CRASH-2026-10-17.md.
//
// Every render pass copies its camera into a C3DEngine pool, and the occlusion
// cull queue's items carry a pointer into that pool. Once a frame the pool is
// reset (C3DEngine vtable +0x90), which frees the old storage whenever the pool
// overflowed. A cull item still queued at that moment is the shape of the crash.
// The hook calls the original unchanged and only reads, before and after it:
// both pools, the cull queue's indices, and the camera of every item still queued.
//
// Started by the command worker once the module is pinned, VR or not, so a flat
// run can be compared with a VR one. PREYVR_CULL_WATCH=0 leaves it uninstalled.
void ServiceCullCameraWatch();
// cull.watch: 1 observes, 0 passes straight through (the hook stays installed).
// Returns 0, 1 for a bad value, or the install failure code.
DWORD SetCullCameraWatch(unsigned int enabled);
// The counters, as cull.report and the minute summary print them.
std::string CullCameraWatchReport();
} // namespace preyvr::dll
