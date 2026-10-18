#include "BindingDump.h"
#include "SafeMemory.h"

#include "HudBridge.h"
#include "Logger.h"
#include "preyvr/ActionMapWalk.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace preyvr::dll {
namespace {

// No C++ objects here: __try cannot share a frame with unwinding.
bool GuardedCopy(void* out, std::uintptr_t address, std::size_t size)
{
    return SafeCopy(out, reinterpret_cast<const void*>(address), size);
}

class GuardedMemory final : public input::MemoryView {
public:
    bool Read(std::uintptr_t address, void* out, std::size_t size) const override
    {
        return address != 0 && GuardedCopy(out, address, size);
    }
};

void Log(const std::string& line) { lifecycle::Log("preyvr_bindings " + line); }

// 0 none, 1 the gamepad's keys, 2 every key.
std::atomic<int> gRequest{0};
bool gLoggedInPlay = false;
// Frames in play, menu state known and no menu up: the automatic dump waits two
// seconds of them, so it reads the maps of a loaded game rather than the main
// menu's first frames, where the manager reads disabled (2026-10-17's run).
unsigned gFramesInPlay = 0;
constexpr unsigned kFramesBeforeDump = 180;

// PreyWheelOpen's map, found by a walk; main thread only.
constexpr const char* kWheelMap = "focusmode";
std::uintptr_t gWheelMap = 0;
unsigned long long gNextWheelFind = 0;
constexpr unsigned long long kWheelFindRetryMs = 2000;
int gWheelLogged = -1;

bool FindWheelMap(std::uintptr_t base)
{
    const auto now = GetTickCount64();
    if (now < gNextWheelFind) { return false; }
    gNextWheelFind = now + kWheelFindRetryMs;
    std::vector<input::BoundAction> rows;
    std::string why;
    if (!input::ReadBindings(GuardedMemory{}, base, input::ActionMapLayout{}, rows, why)) { return false; }
    for (const auto& row : rows) {
        if (row.map == kWheelMap) {
            gWheelMap = row.mapAddress;
            std::ostringstream line;
            line << "result=0 detail=wheel_map map=" << kWheelMap << " at=0x" << std::hex << (gWheelMap - base);
            Log(line.str());
            return true;
        }
    }
    return false;
}

} // namespace

bool PreyWheelOpen()
{
    const HMODULE module = GetModuleHandleW(L"PreyDll.dll");
    if (module == nullptr) { return false; }
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    if (gWheelMap == 0 && !FindWheelMap(base)) { return false; }
    bool open = false;
    std::string why;
    if (!input::ReadActionMapEnabled(GuardedMemory{}, base, input::ActionMapLayout{}, gWheelMap, kWheelMap, open, why)) {
        Log("result=failed detail=wheel_map reason=\"" + why + "\" refind=1");
        gWheelMap = 0;
        open = false;
    }
    if (static_cast<int>(open) != gWheelLogged) {
        gWheelLogged = open ? 1 : 0;
        Log(std::string("result=0 detail=wheel open=") + (open ? "1" : "0"));
    }
    return open;
}

void RequestBindingDump(bool allKeys) { gRequest.store(allKeys ? 2 : 1, std::memory_order_release); }

void ServiceBindingDump()
{
    int request = gRequest.exchange(0, std::memory_order_acq_rel);
    const bool inPlay = HudMenuStateKnown() && !HudMenuIsOpen() && HudGameplayInputAllowed();
    gFramesInPlay = inPlay ? gFramesInPlay + 1 : 0;
    if (request == 0 && !gLoggedInPlay && gFramesInPlay >= kFramesBeforeDump) {
        request = 1;
        gLoggedInPlay = true;
    }
    if (request == 0) { return; }
    const HMODULE module = GetModuleHandleW(L"PreyDll.dll");
    if (module == nullptr) {
        Log("result=failed detail=bindings reason=no_preydll");
        return;
    }
    std::vector<input::BoundAction> rows;
    std::string why;
    if (!input::ReadBindings(GuardedMemory{}, reinterpret_cast<std::uintptr_t>(module), input::ActionMapLayout{},
                             rows, why)) {
        Log("result=failed detail=bindings reason=\"" + why + "\"");
        return;
    }
    unsigned logged = 0;
    for (const auto& row : rows) {
        if (request == 1 && row.key.rfind("xi_", 0) != 0) { continue; }
        std::ostringstream line;
        line << "result=0 detail=binding map=" << row.map << " action=" << row.action << " key=" << row.key
             << " enabled=" << (row.enabled ? 1 : 0) << " activation=" << row.activation;
        Log(line.str());
        ++logged;
    }
    std::ostringstream summary;
    summary << "result=0 detail=bindings total=" << rows.size() << " logged=" << logged
            << " keys=" << (request == 1 ? "gamepad" : "all");
    Log(summary.str());
}

} // namespace preyvr::dll
