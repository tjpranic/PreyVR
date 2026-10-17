#pragma once

// The two engine reads nearly every lane starts from: PreyDll.dll's base, and
// CSystem's live view camera. Header-only so tests that compile a lane's .cpp
// directly need nothing extra; a test that redefines GetModuleHandleW before
// including the lane still stubs this.
#include "preyvr/EngineMap.h"

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace preyvr::dll {

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

} // namespace preyvr::dll
