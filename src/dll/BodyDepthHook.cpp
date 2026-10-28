#include "BodyDepthHook.h"
#include "EngineAccess.h"
#include "EnvSwitch.h"
#include "EyeRecords.h"
#include "HookInstall.h"
#include "Logger.h"
#include "NearViewStereo.h"
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
using bodydepth::Mode;
using bodydepth::NearState;

void Log(const std::string& line) { lifecycle::Log("preyvr_body_depth " + line); }

static_assert(contract::Gated(bn::Contracts, bn::SlotRender));
static_assert(contract::Gated(bn::Contracts, bn::SetViewport));
static_assert(contract::Gated(bn::Contracts, bn::NearestChange));

// CEntityObject::Render(slot, ?, SRendParams*, ?, render proxy, SRenderingPassInfo*).
// The second and fourth arguments are forwarded as they came.
using SlotRenderFn = std::uintptr_t(__fastcall*)(void* slot, void* entity, void* params, std::uintptr_t lod,
                                                 void* proxy, const void* pass);
// CSceneRenderPass::SetViewport(pass, viewport).
using SetViewportFn = void(__fastcall*)(void* pass, const void* viewport);
// UpdateNearestChange(renderer, the drawn object's FOB_NEAREST as NearestEntering).
using NearestChangeFn = void(__fastcall*)(void* renderer, std::uint32_t nearest);
std::atomic<SlotRenderFn> gOriginal{nullptr};
std::atomic<SetViewportFn> gViewportOriginal{nullptr};
std::atomic<NearestChangeFn> gNearestOriginal{nullptr};
std::atomic<unsigned> gMode{static_cast<unsigned>(Mode::nearWorldDepth)};
std::mutex gInstallMutex;
bool gInstallTried = false;
std::atomic<bool> gInstalled{false};
std::atomic<const char*> gGate{"unchecked"};
// The player's first-person character, from the gameplay lanes.
std::atomic<std::uintptr_t> gCharacter{0};
std::atomic<std::uint64_t> gNearSlots{0}, gWorld{0}, gKept{0}, gOthers{0}, gNative{0}, gUnread{0};
std::atomic<std::uint64_t> gCharacterNear{0}, gParamsNear{0};
std::atomic<std::uint32_t> gKeptFlags{0};
// The near pass at the world's depth: near passes and legacy near switches
// given the world's depth range, and legacy switches for a camera no eye's.
std::atomic<std::uint64_t> gViewports{0}, gNearest{0}, gNearestOther{0};
std::atomic<bool> gLoggedFirst{false}, gLoggedFirstViewport{false}, gLoggedFirstNearest{false};

Mode CurrentMode() { return static_cast<Mode>(gMode.load(std::memory_order_acquire)); }

// The near pass at the world's depth is drawing: chosen, and the packer giving
// the near projection the world's depth (VR is on).
bool NearWorldDepth() { return CurrentMode() == Mode::nearWorldDepth && NearViewWorldDepthActive(); }

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
    switch (bodydepth::Decide(CurrentMode() == Mode::world, character,
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

// --- the near pass's depth slice (render thread) ----------------------------------
// A near pass given the depth range of its viewport, the one the pass was set
// with, as a pass without PassNearDepth keeps it (0xF903A6). False for any other
// pass.
bool WidenPassDepth(std::uint8_t* pass, const std::uint8_t* viewport)
{
    __try {
        if ((*reinterpret_cast<const std::uint32_t*>(pass + bn::PassFlags) & bn::PassNearDepth) == 0) { return false; }
        const float minDepth = *reinterpret_cast<const float*>(viewport + bn::ViewportDepth);
        const float maxDepth = *reinterpret_cast<const float*>(viewport + bn::ViewportDepth + 4);
        *reinterpret_cast<float*>(pass + bn::PassViewportDepth) = minDepth;
        *reinterpret_cast<float*>(pass + bn::PassViewportDepth + 4) = maxDepth;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gUnread.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

void __fastcall SetViewportDetour(void* pass, const void* viewport)
{
    gViewportOriginal.load(std::memory_order_acquire)(pass, viewport);
    if (pass == nullptr || viewport == nullptr || !NearWorldDepth()) { return; }
    if (WidenPassDepth(static_cast<std::uint8_t*>(pass), static_cast<const std::uint8_t*>(viewport))) {
        const auto widened = gViewports.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!gLoggedFirstViewport.exchange(true)) {
            Log("result=0 detail=first_near_viewport passes=" + std::to_string(widened));
        }
    }
}

enum class Switch { notNear, read, unread };

// Entering the legacy near pass: whether the renderer is now near, and the
// camera it saved (the one it is drawn with outside the near pass): its position
// and its own max depth.
Switch ReadNearestSwitch(const std::uint8_t* renderer, float position[3], float& maxDepth)
{
    __try {
        if ((*reinterpret_cast<const std::uint32_t*>(renderer + bn::RendererNearestFlags) & bn::NearestEntering) == 0) {
            return Switch::notNear;
        }
        const auto* camera = reinterpret_cast<const float*>(renderer + bn::RendererSavedCamera +
                                                            engine::CameraLayout::matrix);
        position[0] = camera[3];
        position[1] = camera[7];
        position[2] = camera[11];
        maxDepth = *reinterpret_cast<const float*>(renderer + bn::RendererSavedCamera + engine::CameraLayout::zRangeMax);
        return Switch::read;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return Switch::unread;
    }
}

bool WriteNearestDepth(std::uint8_t* renderer, float maxDepth)
{
    __try {
        *reinterpret_cast<float*>(renderer + bn::RendererViewportMaxDepth) = maxDepth;
        renderer[bn::RendererViewportDirty] = 1;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void __fastcall NearestChangeDetour(void* renderer, std::uint32_t nearest)
{
    gNearestOriginal.load(std::memory_order_acquire)(renderer, nearest);
    if (renderer == nullptr || (nearest & bn::NearestEntering) == 0 || !NearWorldDepth()) { return; }
    auto* const r = static_cast<std::uint8_t*>(renderer);
    float position[3]{};
    float maxDepth = 0.0f;
    switch (ReadNearestSwitch(r, position, maxDepth)) {
    case Switch::notNear: return;   // Prey did not switch (no near field of view)
    case Switch::unread: gUnread.fetch_add(1, std::memory_order_relaxed); return;
    case Switch::read: break;
    }
    // Only an eye's camera: a shadow or any other view keeps the near slice.
    BuiltEye eye{};
    if (!(maxDepth > 0.0f && maxDepth <= 1.0f) || !FindBuiltEyeByPosition(position, eye)) {
        gNearestOther.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!WriteNearestDepth(r, maxDepth)) {
        gUnread.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto widened = gNearest.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!gLoggedFirstNearest.exchange(true)) {
        Log("result=0 detail=first_near_switch switches=" + std::to_string(widened) + " eye=" + std::to_string(eye.eye));
    }
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

// --- the gate and the hooks (main thread) -------------------------------------------
struct Targets {
    std::uintptr_t slotRender = 0, setViewport = 0, nearestChange = 0;
};

bool GateBody(std::uintptr_t base)
{
    __try {
        return contract::BodiesMatch(base, bn::Contracts);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// The three functions' addresses, once PreyDll is loaded and every body is the
// one read: ERROR_NOT_READY before, ERROR_BAD_EXE_FORMAT for other bytes.
DWORD Resolve(Targets& targets)
{
    targets = {};
    const auto base = PreyBase();
    if (base == 0) { return ERROR_NOT_READY; }
    if (!GateBody(base)) { return ERROR_BAD_EXE_FORMAT; }
    targets = {base + bn::SlotRender, base + bn::SetViewport, base + bn::NearestChange};
    return ERROR_SUCCESS;
}
// Tests replace the engine here.
DWORD (*gResolve)(Targets&) = Resolve;

template <class Fn>
bool InstallOne(std::uintptr_t target, void* detour, std::atomic<Fn>& original, const char* name)
{
    const hook::Result result = hook::Install(reinterpret_cast<void*>(target), detour, original);
    if (!result) {
        Log(std::string("result=failed detail=") + hook::StepName(result.step) + " status=" +
            std::to_string(result.status) + " target=" + name);
        return false;
    }
    return true;
}
bool Install(const Targets& targets)
{
    return InstallOne(targets.slotRender, reinterpret_cast<void*>(&SlotRenderDetour), gOriginal, "SlotRender") &&
           InstallOne(targets.setViewport, reinterpret_cast<void*>(&SetViewportDetour), gViewportOriginal,
                      "SetViewport") &&
           InstallOne(targets.nearestChange, reinterpret_cast<void*>(&NearestChangeDetour), gNearestOriginal,
                      "NearestChange");
}
// Tests replace the installer here.
bool (*gInstall)(const Targets&) = Install;

// Once: a refused gate or a failed hook stays refused. Before PreyDll is loaded
// it is not yet tried.
bool InstallHook()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstallTried) { return gInstalled.load(std::memory_order_acquire); }
    Targets targets{};
    const DWORD ready = gResolve(targets);
    if (ready == ERROR_NOT_READY) {
        Log("result=unavailable detail=no_preydll");
        return false;
    }
    gInstallTried = true;
    if (ready != ERROR_SUCCESS) {
        gGate = "refused";
        Log("result=unavailable detail=bytes_mismatch target=SlotRender,SetViewport,NearestChange");
        return false;
    }
    if (!gInstall(targets)) {
        gGate = "hook_failed";
        return false;
    }
    gGate = "ok";
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=hook_installed target=SlotRender,SetViewport,NearestChange rows=" +
        std::to_string(std::size(bn::Contracts)));
    return true;
}

// The packer's half of the near pass at the world's depth goes with the hooks.
void ApplyMode(Mode mode, bool hooked)
{
    SetNearViewWorldDepth(hooked && mode == Mode::nearWorldDepth);
}

} // namespace

DWORD SetBodyDepth(unsigned mode)
{
    if (mode > static_cast<unsigned>(Mode::world)) { return ERROR_INVALID_PARAMETER; }
    gMode.store(mode, std::memory_order_release);
    const bool hooked = mode != static_cast<unsigned>(Mode::native) && InstallHook();
    ApplyMode(static_cast<Mode>(mode), hooked);
    if (mode != static_cast<unsigned>(Mode::native) && !hooked) { return ERROR_NOT_SUPPORTED; }
    Log(std::string("result=0 detail=mode value=") + bodydepth::ModeName(static_cast<Mode>(mode)));
    return 0;
}

unsigned BodyDepthEnabled() { return gMode.load(std::memory_order_acquire); }

void EnsureBodyDepth()
{
    // PREYVR_BODY_DEPTH=0: leave the body in Prey's near pass, not even hooked.
    if (EnvSwitchOff(L"PREYVR_BODY_DEPTH")) {
        gMode.store(static_cast<unsigned>(Mode::native), std::memory_order_release);
        ApplyMode(Mode::native, false);
        Log("result=0 detail=disabled_by_environment hooks=0");
        return;
    }
    const Mode mode = CurrentMode();
    if (mode != Mode::native) { ApplyMode(mode, InstallHook()); }
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
    s << "bodyDepth={depth=" << BodyDepthEnabled() << " mode=" << bodydepth::ModeName(CurrentMode())
      << " hooks=" << (gInstalled.load() ? 1 : 0) << " gate=" << gGate.load()
      << " character=" << (gCharacter.load() != 0 ? 1 : 0) << " nearSlots=" << gNearSlots.load()
      << " world=" << gWorld.load() << " kept=" << gKept.load() << " keptFlags=0x" << std::hex << gKeptFlags.load()
      << std::dec << " others=" << gOthers.load() << " native=" << gNative.load()
      << " characterNear=" << gCharacterNear.load() << " paramsNear=" << gParamsNear.load()
      << " viewports=" << gViewports.load() << " nearest=" << gNearest.load() << " nearestOther=" << gNearestOther.load()
      << " unread=" << gUnread.load() << "} " << NearViewWorldDepthReport();
    return s.str();
}

} // namespace preyvr::dll
