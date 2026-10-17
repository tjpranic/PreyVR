#include "BindingDump.h"

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
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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

} // namespace

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
             << " enabled=" << (row.enabled ? 1 : 0);
        Log(line.str());
        ++logged;
    }
    std::ostringstream summary;
    summary << "result=0 detail=bindings total=" << rows.size() << " logged=" << logged
            << " keys=" << (request == 1 ? "gamepad" : "all");
    Log(summary.str());
}

} // namespace preyvr::dll
