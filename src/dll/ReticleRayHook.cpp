#include "ReticleRayHook.h"
#include "SafeMemory.h"
#include "HookInstall.h"
#include "EngineAccess.h"

#include "Logger.h"
#include "MinHookInit.h"
#include "preyvr/EngineMap.h"

#include <windows.h>
#include <MinHook.h>
#include <intrin.h>

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

namespace preyvr::dll {
namespace {

void Log(const std::string& line) { lifecycle::Log("preyvr_reticle_ray " + line); }

using GetterFn = void*(__fastcall*)(std::uintptr_t receiver, void* out);

std::mutex gInstallMutex;
std::atomic<bool> gInstalled{false};
std::atomic<GetterFn> gOriginal{nullptr};
std::array<std::atomic<ReticleRayFilter>, 2> gFilters{};

bool BodyMatches(std::uintptr_t address, const engine::Landmark& landmark)
{
    return SafeEqual(reinterpret_cast<const void*>(address), landmark.expected.data(), landmark.expected.size());
}

void* __fastcall Detour(std::uintptr_t receiver, void* out)
{
    void* const result = gOriginal.load(std::memory_order_acquire)(receiver, out);
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    for (auto& slot : gFilters) {
        if (const auto filter = slot.load(std::memory_order_acquire)) { filter(receiver, out, result, caller); }
    }
    return result;
}

} // namespace

bool EnsureReticleRayHook()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstalled.load(std::memory_order_acquire)) { return true; }
    const auto base = PreyBase();
    if (!base) { return false; }
    const auto* landmark = engine::FindLandmark("aim.get_cached_ray");
    if (!landmark) {
        Log("result=unavailable detail=landmark_missing");
        return false;
    }
    if (!BodyMatches(base + landmark->rva, *landmark)) {
        Log("result=unavailable detail=body_mismatch");
        return false;
    }
    if (!EnsureMinHook()) { return false; }
    auto* const target = reinterpret_cast<void*>(base + landmark->rva);
    const auto installed = hook::Install(target, reinterpret_cast<void*>(&Detour), gOriginal);
    if (!installed) {
        Log(std::string("result=failed detail=") + hook::StepName(installed.step) + " minhook=" +
            MH_StatusToString(installed.status));
        return false;
    }
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=installed");
    return true;
}

bool ReticleRayHookInstalled() { return gInstalled.load(std::memory_order_acquire); }

void SetReticleRayFilter(ReticleRayClient client, ReticleRayFilter filter)
{
    gFilters[static_cast<unsigned>(client)].store(filter, std::memory_order_release);
}

} // namespace preyvr::dll
