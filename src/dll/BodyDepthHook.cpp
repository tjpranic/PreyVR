#include "BodyDepthHook.h"
#include "EngineAccess.h"
#include "EnvSwitch.h"
#include "HookInstall.h"
#include "Logger.h"
#include "preyvr/BodyDepth.h"
#include "preyvr/BodyDepthNative.h"
#include "preyvr/EngineMap.h"
#include "preyvr/NativeContract.h"

#include <atomic>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <sstream>

namespace preyvr::dll {
namespace {
namespace bn = bodydepth::native;
using bodydepth::NearState;

void Log(const std::string& line) { lifecycle::Log("preyvr_body_depth " + line); }

static_assert(contract::Gated(bn::Contracts, bn::SlotRender));

// CEntityObject::Render(slot, ?, SRendParams*, ?, render proxy, SRenderingPassInfo*).
// The second and fourth arguments are forwarded as they came.
using SlotRenderFn = std::uintptr_t(__fastcall*)(void* slot, void* entity, void* params, std::uintptr_t lod,
                                                 void* proxy, const void* pass);
std::atomic<SlotRenderFn> gOriginal{nullptr};
std::atomic<bool> gEnabled{true};
std::mutex gInstallMutex;
bool gInstallTried = false;
std::atomic<bool> gInstalled{false};
std::atomic<const char*> gGate{"unchecked"};
// The player's first-person character, from the gameplay lanes.
std::atomic<std::uintptr_t> gCharacter{0};
std::atomic<std::uint64_t> gNearSlots{0}, gWorld{0}, gKept{0}, gOthers{0}, gNative{0}, gUnread{0};
std::atomic<std::uint64_t> gCharacterNear{0}, gParamsNear{0};
std::atomic<std::uint32_t> gKeptFlags{0};
std::atomic<bool> gLoggedFirst{false};

// --- the slot render (main thread) ------------------------------------------------
// Whether the slot is a near one (almost none are), with its character and the
// pass's rendering flags. False for any other slot, and when it cannot be read.
bool ReadNearSlot(const std::uint8_t* slot, const std::uint8_t* pass, std::uintptr_t& character,
                  std::uint32_t& passFlags)
{
    __try {
        if (!bodydepth::NearSlot(*reinterpret_cast<const std::uint16_t*>(slot + bn::SlotFlags))) { return false; }
        character = *reinterpret_cast<const std::uintptr_t*>(slot + bn::SlotCharacter);
        passFlags = pass != nullptr ? *reinterpret_cast<const std::uint32_t*>(pass + engine::PassInfoLayout::flags) : 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gUnread.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

bool ReadState(const std::uint8_t* slot, const std::uint8_t* params, const std::uint8_t* character, NearState& out)
{
    __try {
        out.slotFlags = *reinterpret_cast<const std::uint16_t*>(slot + bn::SlotFlags);
        out.slotObjectFlags = *reinterpret_cast<const std::uint32_t*>(slot + bn::SlotObjectFlags);
        out.paramsObjectFlags = *reinterpret_cast<const std::uint64_t*>(params + bn::ParamsObjectFlags);
        out.characterFlags = *reinterpret_cast<const std::uint32_t*>(character + engine::CharacterInstanceLayout::flags);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Writes the words of `to` that differ from `from`.
bool WriteState(std::uint8_t* slot, std::uint8_t* params, std::uint8_t* character, const NearState& from,
                const NearState& to)
{
    __try {
        if (to.slotFlags != from.slotFlags) { *reinterpret_cast<std::uint16_t*>(slot + bn::SlotFlags) = to.slotFlags; }
        if (to.slotObjectFlags != from.slotObjectFlags) {
            *reinterpret_cast<std::uint32_t*>(slot + bn::SlotObjectFlags) = to.slotObjectFlags;
        }
        if (to.paramsObjectFlags != from.paramsObjectFlags) {
            *reinterpret_cast<std::uint64_t*>(params + bn::ParamsObjectFlags) = to.paramsObjectFlags;
        }
        if (to.characterFlags != from.characterFlags) {
            *reinterpret_cast<std::uint32_t*>(character + engine::CharacterInstanceLayout::flags) = to.characterFlags;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// What a render drawn in the world changed: the words as they were, and the
// slot's character, whose flags are one of them.
struct Taken {
    NearState before{};
    std::uint8_t* character = nullptr;
};

// Clears the near bits for the player's slot in the main view, and keeps what
// they were in `taken`. False for every other render, which is Prey's.
bool TakeIntoWorld(void* slot, void* params, const void* pass, Taken& taken)
{
    std::uintptr_t character = 0;
    std::uint32_t passFlags = 0;
    if (!ReadNearSlot(static_cast<const std::uint8_t*>(slot), static_cast<const std::uint8_t*>(pass), character,
                      passFlags)) {
        return false;
    }
    gNearSlots.fetch_add(1, std::memory_order_relaxed);
    switch (bodydepth::Decide(gEnabled.load(std::memory_order_acquire), character,
                              gCharacter.load(std::memory_order_acquire), passFlags)) {
    case bodydepth::Decision::native:
        gNative.fetch_add(1, std::memory_order_relaxed);
        return false;
    case bodydepth::Decision::notPlayer:
        gOthers.fetch_add(1, std::memory_order_relaxed);
        return false;
    case bodydepth::Decision::otherPass:
        gKept.fetch_add(1, std::memory_order_relaxed);
        gKeptFlags.store(passFlags, std::memory_order_relaxed);
        return false;
    case bodydepth::Decision::world:
        break;
    }
    auto* const s = static_cast<std::uint8_t*>(slot);
    auto* const p = static_cast<std::uint8_t*>(params);
    auto* const c = reinterpret_cast<std::uint8_t*>(character);
    NearState& before = taken.before;
    if (p == nullptr || !ReadState(s, p, c, before)) {
        gUnread.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const NearState cleared = bodydepth::InWorld(before);
    if (!WriteState(s, p, c, before, cleared)) {
        // A word that reads but does not write: put back whatever was written.
        WriteState(s, p, c, cleared, before);
        gUnread.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if ((before.characterFlags & engine::CharacterInstanceLayout::drawNear) != 0) {
        gCharacterNear.fetch_add(1, std::memory_order_relaxed);
    }
    if ((before.paramsObjectFlags & bn::ObjectNearest) != 0) { gParamsNear.fetch_add(1, std::memory_order_relaxed); }
    taken.character = c;
    const auto drawn = gWorld.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!gLoggedFirst.exchange(true)) { Log("result=0 detail=first_world draws=" + std::to_string(drawn)); }
    return true;
}

// After the render: the near bits it was drawn without, put back.
void PutBack(void* slot, void* params, const Taken& taken)
{
    auto* const s = static_cast<std::uint8_t*>(slot);
    auto* const p = static_cast<std::uint8_t*>(params);
    NearState now{};
    if (!ReadState(s, p, taken.character, now) ||
        !WriteState(s, p, taken.character, now, bodydepth::Restored(now, taken.before))) {
        gUnread.fetch_add(1, std::memory_order_relaxed);
    }
}

std::uintptr_t __fastcall SlotRenderDetour(void* slot, void* entity, void* params, std::uintptr_t lod, void* proxy,
                                           const void* pass)
{
    const SlotRenderFn original = gOriginal.load(std::memory_order_acquire);
    Taken taken{};
    if (!TakeIntoWorld(slot, params, pass, taken)) { return original(slot, entity, params, lod, proxy, pass); }
    const std::uintptr_t result = original(slot, entity, params, lod, proxy, pass);
    PutBack(slot, params, taken);
    return result;
}

// --- the player's character (main thread) -------------------------------------------
// Tests replace the module base here.
std::uintptr_t (*gBase)() = PreyBase;

// ArkPlayer -> its animated character -> the CCharInstance, checked by vtable.
bool ReadPlayerCharacter(std::uintptr_t player, std::uintptr_t base, std::uintptr_t& out)
{
    __try {
        const auto animated = *reinterpret_cast<const std::uintptr_t*>(player + engine::ArkPlayerLayout::animatedCharacter);
        if (animated == 0) { return false; }
        const auto character =
            *reinterpret_cast<const std::uintptr_t*>(animated + engine::AnimatedCharacterLayout::characterInstance);
        if (character == 0 ||
            *reinterpret_cast<const std::uintptr_t*>(character) != base + engine::CharacterInstanceLayout::vtableRva) {
            return false;
        }
        out = character;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// --- the gate and the hook (main thread) ----------------------------------------
bool GateBody(std::uintptr_t base)
{
    __try {
        return contract::BodiesMatch(base, bn::Contracts);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// The slot render's address, once PreyDll is loaded and its body is the one
// read: ERROR_NOT_READY before, ERROR_BAD_EXE_FORMAT for other bytes.
DWORD Resolve(std::uintptr_t& target)
{
    target = 0;
    const auto base = PreyBase();
    if (base == 0) { return ERROR_NOT_READY; }
    if (!GateBody(base)) { return ERROR_BAD_EXE_FORMAT; }
    target = base + bn::SlotRender;
    return ERROR_SUCCESS;
}
// Tests replace the engine here.
DWORD (*gResolve)(std::uintptr_t&) = Resolve;

bool Install(std::uintptr_t target)
{
    const hook::Result result =
        hook::Install(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&SlotRenderDetour), gOriginal);
    if (!result) {
        Log(std::string("result=failed detail=") + hook::StepName(result.step) +
            " status=" + std::to_string(result.status) + " target=SlotRender");
        return false;
    }
    return true;
}
// Tests replace the installer here.
bool (*gInstall)(std::uintptr_t) = Install;

// Once: a refused gate or a failed hook stays refused. Before PreyDll is loaded
// it is not yet tried.
bool InstallHook()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstallTried) { return gInstalled.load(std::memory_order_acquire); }
    std::uintptr_t target = 0;
    const DWORD ready = gResolve(target);
    if (ready == ERROR_NOT_READY) {
        Log("result=unavailable detail=no_preydll");
        return false;
    }
    gInstallTried = true;
    if (ready != ERROR_SUCCESS) {
        gGate = "refused";
        Log("result=unavailable detail=bytes_mismatch target=SlotRender");
        return false;
    }
    if (!gInstall(target)) {
        gGate = "hook_failed";
        return false;
    }
    gGate = "ok";
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=hook_installed target=SlotRender rows=" + std::to_string(std::size(bn::Contracts)));
    return true;
}

} // namespace

DWORD SetBodyDepth(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gEnabled.store(enabled != 0, std::memory_order_release);
    if (enabled != 0 && !InstallHook()) { return ERROR_NOT_SUPPORTED; }
    Log(std::string("result=0 detail=enabled value=") + (enabled ? "1" : "0"));
    return 0;
}

unsigned BodyDepthEnabled() { return gEnabled.load(std::memory_order_acquire) ? 1u : 0u; }

void EnsureBodyDepth()
{
    // PREYVR_BODY_DEPTH=0: leave the body in Prey's near pass, not even hooked.
    if (EnvSwitchOff(L"PREYVR_BODY_DEPTH")) {
        gEnabled.store(false, std::memory_order_release);
        Log("result=0 detail=disabled_by_environment hooks=0");
        return;
    }
    if (gEnabled.load(std::memory_order_acquire)) { InstallHook(); }
}

void PublishBodyDepthPlayer(std::uintptr_t player)
{
    if (!gInstalled.load(std::memory_order_acquire)) { return; }
    const auto base = gBase();
    std::uintptr_t character = 0;
    if (base == 0 || player == 0 || !ReadPlayerCharacter(player, base, character)) { character = 0; }
    gCharacter.store(character, std::memory_order_release);
}

std::string BodyDepthReport()
{
    std::ostringstream s;
    s << "bodyDepth={depth=" << BodyDepthEnabled() << " hooks=" << (gInstalled.load() ? 1 : 0)
      << " gate=" << gGate.load() << " character=" << (gCharacter.load() != 0 ? 1 : 0)
      << " nearSlots=" << gNearSlots.load() << " world=" << gWorld.load() << " kept=" << gKept.load()
      << " keptFlags=0x" << std::hex << gKeptFlags.load() << std::dec << " others=" << gOthers.load()
      << " native=" << gNative.load() << " characterNear=" << gCharacterNear.load()
      << " paramsNear=" << gParamsNear.load() << " unread=" << gUnread.load() << '}';
    return s.str();
}

} // namespace preyvr::dll
