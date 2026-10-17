#include "ReticleRayHook.h"

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

const engine::Landmark* FindGetterLandmark()
{
    for (const auto& landmark : engine::Landmarks()) {
        if (landmark.id == "aim.get_cached_ray") { return &landmark; }
    }
    return nullptr;
}

bool BodyMatches(std::uintptr_t address, const engine::Landmark& landmark)
{
    __try {
        return std::memcmp(reinterpret_cast<const void*>(address), landmark.expected.data(),
                           landmark.expected.size()) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if (!base) { return false; }
    const auto* landmark = FindGetterLandmark();
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
    GetterFn trampoline = nullptr;
    MH_STATUS status = MH_CreateHook(target, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&trampoline));
    if (status != MH_OK) {
        Log(std::string("result=failed detail=create_hook minhook=") + MH_StatusToString(status));
        return false;
    }
    // Published before enabling: the detour can run the moment it is live.
    gOriginal.store(trampoline, std::memory_order_release);
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        MH_RemoveHook(target);
        gOriginal.store(nullptr, std::memory_order_release);
        Log(std::string("result=failed detail=enable_hook minhook=") + MH_StatusToString(status));
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
