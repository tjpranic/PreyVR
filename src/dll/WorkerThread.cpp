#include "WorkerThread.h"

#include "CommandChannel.h"
#include "FrameSchedule.h"
#include "Logger.h"

#include <atomic>

namespace preyvr::dll {
namespace {

std::atomic<bool> gRunning{false};

DWORD WINAPI WorkerLoop(LPVOID)
{
    LogCommandChannelStarted();
    unsigned poll = 0;
    while (gRunning.load(std::memory_order_acquire)) {
        schedule::TickWorker();
        // Keys and startup progress are serviced at 50 Hz; disk polling stays 5 Hz.
        if (++poll%10!=0) { Sleep(20); continue; }
        ServiceCommandFile();
        Sleep(20);
    }
    return 0;
}

} // namespace

DWORD StartWorkerThread()
{
    if (gRunning.exchange(true, std::memory_order_acq_rel)) {
        return 0;   // already running
    }
    const HANDLE thread = CreateThread(nullptr, 0, &WorkerLoop, nullptr, 0, nullptr);
    if (thread == nullptr) {
        gRunning.store(false, std::memory_order_release);
        lifecycle::Log("preyvr_channel result=failed detail=create_thread");
        return 1;
    }
    CloseHandle(thread);
    return 0;
}

} // namespace preyvr::dll
