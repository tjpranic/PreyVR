#pragma once

// **Globals that are never destroyed** (docs/CRASHES-2026-10-24.md, section 2).
//
// At process exit Windows ends every other thread, then unloads this DLL, and
// the C runtime runs its global destructors right after DllMain's
// DLL_PROCESS_DETACH, whether or not the process is ending (vcruntime
// dll_dllmain.cpp), under the loader lock. A destructor there that calls OpenXR
// or D3D calls into runtimes whose threads are already gone, and a std::thread
// still joinable calls std::terminate. Every quit on 2026-10-24 ended in one of
// those two fail-fasts. The globals that own XR handles, D3D objects or a thread
// are made here instead, on the heap, and never freed: their own teardown
// (StopXrSession, a lane's release) still runs while the process lives, and at
// exit the OS takes back what is left.
namespace preyvr::dll {

// A value-initialised T that lives until the process ends. An array type gives a
// reference to the array, so indexing, range-for and sizeof read as before.
template <class T>
T& NeverDestroyed()
{
    return *new T[1]();
}

} // namespace preyvr::dll
