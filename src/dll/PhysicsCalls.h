#pragma once

// The generic IPhysicalEntity calls the lanes share, through the slots every
// physics class has: dynamics status (type 8) and set velocity (type 10). Worn in
// the carry lane's throws; ContactNative.h hashes the rigid and articulated
// GetStatus bodies and PhysicalNative.h the rigid Action and QueueAction.
// Header-only so tests that compile a lane's .cpp directly need nothing extra.
#include "preyvr/VrMath.h"

#include <windows.h>

#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace preyvr::dll::phys {

inline constexpr std::size_t kTypeSlot = 0x08, kStatusSlot = 0x30, kActionSlot = 0x38;

// pe_status_dynamics: type 8; ipart/partid unused; v +0x0C, w +0x18, centre of
// mass +0x3C, mass +0x4C (rigid GetStatus 0xBE2BB0, docs/PHYSICAL-CONTACT-SCOPE).
// A sleeping body reports no velocity.
struct Dynamics {
    Vec3 v{}, w{}, centre{};
    float mass = 0;
};
inline bool QueryDynamics(std::uintptr_t physics, Dynamics& out)
{
    alignas(16) std::uint8_t status[128]{};
    const int type = 8, unused = INT_MIN;
    std::memcpy(status, &type, 4);
    std::memcpy(status + 4, &unused, 4);
    std::memcpy(status + 8, &unused, 4);
    __try {
        if (!physics) { return false; }
        const auto table = *reinterpret_cast<const std::uintptr_t*>(physics);
        const auto getStatus = *reinterpret_cast<int(__fastcall* const*)(std::uintptr_t, void*)>(table + kStatusSlot);
        if (!getStatus(physics, status)) { return false; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    std::memcpy(&out.v, status + 0x0C, sizeof(Vec3));
    std::memcpy(&out.w, status + 0x18, sizeof(Vec3));
    std::memcpy(&out.centre, status + 0x3C, sizeof(Vec3));
    std::memcpy(&out.mass, status + 0x4C, sizeof(float));
    return Finite(out.v) && Finite(out.w) && Finite(out.centre) && std::isfinite(out.mass);
}

// pe_action_set_velocity: type 10, ipart/partid unused (the whole entity),
// v, w (x = the engine's "unused" NaN keeps the current spin),
// bRotationAroundPivot 0 (about the centre of mass). It wakes a sleeping rigid body.
struct SetVelocityAction {
    int type = 10;
    int ipart = INT_MIN;
    int partid = INT_MIN;
    Vec3 v{}, w{};
    int aroundPivot = 0;
};
static_assert(sizeof(SetVelocityAction) == 40, "pe_action_set_velocity is 40 bytes in Prey");
inline bool SetVelocity(std::uintptr_t physics, const SetVelocityAction& action)
{
    __try {
        if (!physics) { return false; }
        const auto table = *reinterpret_cast<const std::uintptr_t*>(physics);
        const auto act = *reinterpret_cast<int(__fastcall* const*)(std::uintptr_t, const void*, int)>(table + kActionSlot);
        // bThreadSafe = 1: queued while the body is being stepped, else applied now.
        return act(physics, &action, 1) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// QueueAction copies a queued action by its type's entry in two runtime tables:
// set velocity must be 40 bytes with no pointed-to payload, or it cannot be queued.
inline constexpr std::uintptr_t kActionSizeTableRva = 0x2982CA0, kActionPayloadTableRva = 0x2982DE0;
inline bool VelocityQueueReady(std::uintptr_t base, std::uint32_t* sizeOut = nullptr, std::uintptr_t* payloadOut = nullptr)
{
    std::uint32_t size = 0;
    std::uintptr_t payload = 1;
    __try {
        size = *reinterpret_cast<const std::uint32_t*>(base + kActionSizeTableRva + 10 * 4);
        payload = *reinterpret_cast<const std::uintptr_t*>(base + kActionPayloadTableRva + 10 * 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (sizeOut) { *sizeOut = size; }
    if (payloadOut) { *payloadOut = payload; }
    return size == sizeof(SetVelocityAction) && payload == 0;
}

} // namespace preyvr::dll::phys
