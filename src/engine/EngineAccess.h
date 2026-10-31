#pragma once

// The engine reads nearly every lane starts from: PreyDll.dll's base, CSystem's
// live view camera and an entity by id. Header-only so tests that compile a lane's .cpp
// directly need nothing extra; a test that redefines GetModuleHandleW before
// including the lane still stubs this.
#include "engine/EngineMap.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <span>
#include <string_view>

namespace preyvr::dll {

// The gated bytes of landmark `id`, or an empty span when it is missing or its
// RVA is not `rva`. hook::InstallVerified refuses an empty prologue, so a hook
// gated on this fails closed.
inline std::span<const std::uint8_t> LandmarkBytes(std::string_view id, std::uintptr_t rva)
{
    const engine::Landmark* landmark = engine::FindLandmark(id);
    return landmark != nullptr && landmark->rva == rva ? landmark->expected : std::span<const std::uint8_t>{};
}

// PreyDll.dll's base address, or 0 before it is loaded. Cached once non-zero:
// the game's own module stays loaded for the life of the process, so this
// replaces a loader lookup on every call, several of them per frame.
inline std::uintptr_t PreyBase()
{
    static std::atomic<std::uintptr_t> cached{0};
    std::uintptr_t base = cached.load(std::memory_order_acquire);
    if (base == 0) {
        base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
        cached.store(base, std::memory_order_release);
    }
    return base;
}

// CSystem's view camera, a CCamera by value at SystemLayout::viewCamera, or
// nullptr until PreyDll and gEnv->pSystem exist. Plain reads: the system
// pointer is a global in the loaded module.
inline const std::uint8_t* LiveViewCamera()
{
    const std::uintptr_t base = PreyBase();
    if (base == 0) {
        return nullptr;
    }
    const auto* const system = *reinterpret_cast<const std::uint8_t* const*>(base + engine::SystemLayout::pointerRva);
    return system != nullptr ? system + engine::SystemLayout::viewCamera : nullptr;
}

// The entity with `id` (gEnv->pEntitySystem's GetEntity), or 0: no such entity,
// no entity system yet, or a fault. `base` is PreyDll's.
inline std::uintptr_t EntityById(std::uintptr_t base, std::uint32_t id)
{
    using engine::GlobalEnvironmentLayout;
    using GetEntityFn = std::uintptr_t(__fastcall*)(std::uintptr_t system, std::uint32_t id);
    __try {
        const auto system = *reinterpret_cast<const std::uintptr_t*>(base + GlobalEnvironmentLayout::baseRva +
                                                                     GlobalEnvironmentLayout::entitySystem);
        if (system == 0 || id == 0) { return 0; }
        const auto vtable = *reinterpret_cast<const std::uintptr_t*>(system);
        const auto getEntity =
            *reinterpret_cast<const GetEntityFn*>(vtable + engine::EntitySystemLayout::vtableGetEntity);
        return getEntity(system, id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

} // namespace preyvr::dll
