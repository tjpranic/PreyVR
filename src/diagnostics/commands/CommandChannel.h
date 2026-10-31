#pragma once

#include <windows.h>

// Drives the mod from a text file, so routine testing needs no injector.
//
// **Why this exists.** Every live protocol so far has been driven through Frida,
// and `frida-agent.dll` crashed the host **four times in one session** with four
// different symptoms -- a wedged session, a null dereference, a wild-pointer read
// and another null. Both ways of calling into the DLL cost a run: a
// `CreateThread`-per-call helper destabilised the agent, and calling exports
// directly hit the F-009 abort that leaves a control mutex held. Trading one for
// the other is not progress, so this removes the injector from the loop entirely.
//
// It also makes a protocol **data rather than code**. A new test is a few lines in
// a file; it does not need a rebuild, and it does not need a relaunch to pick up,
// which is otherwise forced by the module pinning itself.
//
// **Fail-closed, like the console allowlist.** The channel does not invoke exports
// by name -- that would be a general call-anything primitive with a text file as
// its argument. It accepts a fixed set of verbs, each wired to one operation, and
// ignores everything else. Console commands still pass through the existing
// console allowlist, so this adds no reach there.
//
// **Nothing happens without a file.** The worker thread that services it
// (WorkerThread.h) starts after the landmark gate has passed, so an unsupported
// build never gets a channel, and it does nothing at all until someone writes a
// command file.
namespace preyvr::dll {

// The worker thread's two calls. The first, once as it starts, logs
// `preyvr_channel result=0 detail=started` with both paths; the launcher waits
// for that line. The second, at 5 Hz, reads, truncates and executes the command
// file and writes the result file.
void LogCommandChannelStarted();
void ServiceCommandFile();

// Commands are read from `<log directory>/commands.txt` and results written to
// `<log directory>/results.txt`. The command file is **truncated once read**, so a
// command runs once rather than every poll.
//
// One verb per line, `#` comments and blank lines ignored, e.g. `vr.enable`,
// `ui.scale 110`, `console <command>` (through the fail-closed allowlist) or
// `report` (every counter to the result file). docs/TUNING-COMMANDS.md lists
// the verbs, grouped by who they are for.
//
// A verb the channel does not know is reported as rejected rather than ignored
// silently, because a typo that does nothing is indistinguishable from a
// mechanism that does not work -- a confusion this project has paid for.

} // namespace preyvr::dll
