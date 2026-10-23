#pragma once

#include <windows.h>

// The mod's own thread. Every pass (50 Hz) it runs schedule::TickWorker
// (FrameSchedule.cpp): the services that must stay off the game threads, and VR
// start. Every 10th pass (5 Hz) it services the command file (CommandChannel.h).
//
// It was the command channel's poll thread until 2026-10-24. VR start runs here,
// so leaving the command channel out of a build must not stop this thread.
namespace preyvr::dll {

// Starts the worker. Called from the bootstrap once the build is verified, so an
// unsupported build never gets one. Safe to call twice.
//
// Its log lines are still the command channel's, `preyvr_channel result=0
// detail=started ...`: the launcher waits for that line before it writes its
// startup commands.
DWORD StartWorkerThread();

} // namespace preyvr::dll
