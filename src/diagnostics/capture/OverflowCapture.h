#pragma once
#include <windows.h>

namespace preyvr::dll {
// **A dump that survives a stack overflow** (docs/ENGINE-INSTABILITY.md).
// The phantom crash of 2026-10-25 left no error.dmp because it was a stack
// overflow (Steam recorded exit code 0xC00000FD): Prey's crash handler runs on
// the thread that overflowed, has no stack left, and dies before it writes one.
//
// ArmOverflowCapture adds a first vectored exception handler and starts a
// thread of its own, with its own stack, that waits. On a stack overflow, and
// nothing else, the handler (on what is left of the faulting thread's stack)
// records only the thread and its stack bounds, wakes that thread and waits for
// it. The capture thread writes, beside PreyVR.log:
//   preyvr-overflow-<pid>.dmp  a minidump at the fault: every thread's stack,
//                              thread and TEB data, memory-region info;
//   preyvr-overflow-<pid>.txt  the faulting thread (main, render, worker, hold
//                              or other, and its name), its stack's size and use,
//                              its frames unwound from the fault, and the return
//                              addresses that repeat most (a recursion's loop);
// and one preyvr_crash line in PreyVR.log. Then the exception goes on to Prey's
// own handler, as before. Once per process.
//
// Armed by the bootstrap, before any hook. PREYVR_OVERFLOW_CAPTURE=0 leaves it out.
void ArmOverflowCapture();

// Which of the mod's own threads this is, so a report can name the faulting
// one. Called on each from its per-frame or per-pass service; after the first
// call per role it is one relaxed load.
enum class CrashThread { main, render, worker, hold };
void NoteCrashThread(CrashThread role);
} // namespace preyvr::dll
