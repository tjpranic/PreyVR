#pragma once
#include <windows.h>

#include <string>

namespace preyvr::dll {
// **Who rebuilds a render item's resource set while a scene pass draws it**
// (docs/CRASHES-2026-10-24.md, section 1).
//
// A compiled render item binds its textures through resource sets: arrays of
// {type, stages, slot, raw view} entries at set+0x78, which the draw jobs read
// on the job workers (FUN_180F66720, then FUN_180F63C70 binds each view). The
// rebuild, FUN_180F63F10 (the set's vtable 0x1DD1D38, +0x10), zeroes the count
// at +0x80 and rewrites the same array in place, reallocating it when it grows.
// On 10-24 a draw job bound a view its texture had already destroyed: the entry
// had been rewritten under it, while the render thread waited for the jobs
// inside RT_RenderScene and the main thread waited for the render thread.
//
// Read-only. A hook on the rebuild records, for each one, the thread (the
// render thread, the main thread, or another), whether a top-level scene
// submission was in flight (CurrentSceneSubmission), the caller, and whether the
// array moved. A rebuild on a thread other than the render thread during a
// submission is the race: the first ones are logged with their callers' stacks.
//
// Installed by the command worker once the module is pinned, VR or not;
// PREYVR_RSET_WATCH=0 before launch leaves it out. rset.watch 0|1 pauses and
// resumes the counting (the hook stays); rset.report reads it. Every 60 s, and
// for each event, a preyvr_resource_set line.
void ServiceResourceSetWatch();
// rset.watch: 0 pauses, 1 counts. Returns 0, 1 for a bad value, or the install
// failure code (2 not pinned, 3 PreyDll not loaded, 4 code mismatch, 5 hook).
DWORD SetResourceSetWatch(unsigned int enabled);
std::string ResourceSetWatchReport();
} // namespace preyvr::dll
