#include "ControllerPrompts.h"
#include "ConsoleBridgeWin32.h"
#include "FrameObserverHook.h"
#include "Logger.h"
#include <mutex>
#include <sstream>
#include <string>

namespace preyvr::dll {
namespace {
std::mutex gMutex;
// -1 is none. `wanted` is what the lane asks for, `applied` the last value read
// back, `failed` the last one that was not, `sending` the one in the console slot.
int gWanted = -1, gApplied = -1, gSending = -1, gFailed = -1;
unsigned long long gCount = 0;

void Log(const std::string& line) { lifecycle::Log("preyvr_prompts " + line); }
const char* Command(int value) { return value ? "hud_forceControllerGlyphs 1" : "hud_forceControllerGlyphs 0"; }
} // namespace

DWORD SetControllerPrompts(unsigned enabled)
{
    if (enabled > 1) { return 1; }
    std::lock_guard lock(gMutex);
    // Off puts Prey's default back only where this lane set it, or may have.
    if (enabled) { gWanted = 1; }
    else { gWanted = gApplied == 1 || gSending == 1 || gFailed == 1 ? 0 : -1; }
    gFailed = -1; // a new request is tried again
    return 0;
}

unsigned ControllerPromptsEnabled()
{
    std::lock_guard lock(gMutex);
    return gWanted == 1 ? 1u : 0u;
}

void ServiceControllerPrompts()
{
    std::lock_guard lock(gMutex);
    if (gSending >= 0) {
        if (VerifiedRendererSettingCount() == gCount) { return; }
        const bool verified = VerifiedRendererSettingResult() == 0;
        Log("force_controller_glyphs=" + std::to_string(gSending) +
            (verified ? " result=0 detail=verified_readback" : " result=failed detail=readback_timeout_or_mismatch"));
        if (verified) { gApplied = gSending; }
        else { gFailed = gSending; }
        gSending = -1;
    }
    if (gWanted < 0 || gWanted == gApplied || gWanted == gFailed) { return; }
    // Console commands drain on observed frames; queued before that, this would
    // hold the shared slot for nothing.
    if (FrameObserverStatus() != static_cast<DWORD>(FrameObserverRuntimeStatus::enabled)) { return; }
    const auto count = VerifiedRendererSettingCount();
    const DWORD queued = QueueVerifiedRendererSetting(Command(gWanted));
    if (queued == 0) {
        gSending = gWanted;
        gCount = count;
    } else if (queued != static_cast<DWORD>(ConsoleBridgeResult::busy)) {
        gFailed = gWanted;
        Log("force_controller_glyphs=" + std::to_string(gWanted) + " result=" + std::to_string(queued) +
            " detail=queue_refused");
    }
}

std::string ControllerPromptsReport()
{
    std::lock_guard lock(gMutex);
    const auto value = [](int v) { return v < 0 ? std::string("none") : std::to_string(v); };
    std::ostringstream out;
    out << " wanted=" << value(gWanted) << " applied=" << value(gApplied) << " sending=" << value(gSending)
        << " failed=" << value(gFailed);
    return out.str();
}
} // namespace preyvr::dll
