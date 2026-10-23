#include "WrenchDraw.h"
#include "EngineAccess.h"
#include "HookInstall.h"
#include "Logger.h"
#include "WrenchSound.h"
#include "preyvr/EquipmentNative.h"
#include "preyvr/NativeContract.h"
#include "preyvr/PhysicalNative.h"
#include "preyvr/TextFormat.h"
#include "preyvr/WrenchDrawNative.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <iterator>

namespace preyvr::dll {
namespace {
namespace wn = wrenchdraw::native;
namespace en = equipment::native;
namespace pn = physical::native;

void Log(const std::string& line) { lifecycle::Log("preyvr_wrench_draw " + line); }

// Every function hooked or called through a named RVA or vtable slot is one a
// gate hashes: this lane's rows, and EquipmentNative.h's SelectItem and lookup.
static_assert(contract::Gated(wn::Contracts, wn::PlayAction) && contract::Gated(wn::Contracts, wn::OnEquip) &&
              contract::Gated(wn::Contracts, wn::SelectActionCtor) && contract::Gated(wn::Contracts, wn::ActionBaseCtor) &&
              contract::Gated(wn::Contracts, wn::SelectEnter) && contract::Gated(wn::Contracts, wn::SelectExit) &&
              contract::Gated(wn::Contracts, wn::SelectBegun) && contract::Gated(wn::Contracts, wn::SelectDone) &&
              contract::Gated(wn::Contracts, wn::SelectFinished) && contract::Gated(en::Contracts, wn::SelectItem) &&
              contract::Gated(en::Contracts, wn::WeaponLookup) && contract::Gated(wn::Contracts, wn::WrenchUnequip) &&
              contract::Gated(wn::Contracts, wn::OnUnequip) && contract::Gated(wn::Contracts, wn::Stow) &&
              contract::Gated(wn::Contracts, wn::StowActionCtor) && contract::Gated(wn::Contracts, wn::StowExit));

std::atomic<bool> enabled{true}, installed{false}, fault{false};
std::atomic<const char*> gate{"unchecked"};
std::atomic<unsigned long> serviceThread{0};
std::atomic<std::uint64_t> frames{0}, selects{0}, pickups{0}, finished{0}, gone{0}, full{0}, elsewhere{0}, sounds{0};
std::atomic<std::uint64_t> stows{0}, stowSounds{0}, stowsLowered{0};
std::atomic<unsigned> waiting{0};

using PlayActionFn = void(__fastcall*)(std::uintptr_t, std::uintptr_t*, float);
using UnequipFn = void(__fastcall*)(std::uintptr_t, bool);
using WeaponFn = void(__fastcall*)(std::uintptr_t);
using ReleaseFn = void(__fastcall*)(std::uintptr_t);   // the action's own release, as PlayAction calls it
using LookupFn = std::uintptr_t(__fastcall*)(std::uint32_t);
std::atomic<PlayActionFn> originalPlayAction{nullptr};
std::atomic<UnequipFn> originalUnequip{nullptr};

// What the hook and the finish use, resolved on the main thread before the hook
// goes live and never changed after. Tests put fakes here.
struct Natives {
    std::uintptr_t base = 0, selectVtable = 0, wrenchVtable = 0;
    LookupFn lookup = nullptr;
};
Natives natives{};

// A virtual function of an engine object, by its vtable slot's byte offset.
template <class Fn> Fn Virtual(std::uintptr_t object, std::size_t slot)
{
    return reinterpret_cast<Fn>((*reinterpret_cast<const std::uintptr_t* const*>(object))[slot / sizeof(void*)]);
}

// --- the gate (main thread) -------------------------------------------------------
std::uintptr_t SlotTarget(std::uintptr_t base, std::uintptr_t table, std::size_t slot)
{
    return *reinterpret_cast<const std::uintptr_t*>(base + table + slot) - base;
}
bool GateBodies(std::uintptr_t base)
{
    __try {
        if (!contract::BodiesMatch(base, wn::Contracts)) { return false; }
        for (const auto& row : en::Contracts) {
            if ((row.rva == wn::SelectItem || row.rva == wn::WeaponLookup) && !contract::BodyMatches(base, row)) { return false; }
        }
        // What is called through vtables: the wrench's begun/done/finished, and
        // the select action's Enter and Exit, whose work the lane splits.
        return SlotTarget(base, pn::WrenchVtable, wn::SelectBegunSlot) == wn::SelectBegun &&
               SlotTarget(base, pn::WrenchVtable, wn::SelectDoneSlot) == wn::SelectDone &&
               SlotTarget(base, pn::WrenchVtable, wn::SelectFinishedSlot) == wn::SelectFinished &&
               SlotTarget(base, wn::SelectActionVtable, wn::EnterSlot) == wn::SelectEnter &&
               SlotTarget(base, wn::SelectActionVtable, wn::ExitSlot) == wn::SelectExit &&
               // The stow: the wrench's own OnUnequip, and the stow action's Exit,
               // which the instant stow stands in for.
               SlotTarget(base, pn::WrenchVtable, wn::OnUnequipSlot) == wn::WrenchUnequip &&
               SlotTarget(base, wn::StowActionVtable, wn::ExitSlot) == wn::StowExit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
DWORD Resolve(Natives& n)
{
    n = {};
    const auto base = PreyBase();
    if (!base) { return ERROR_NOT_READY; }
    if (!GateBodies(base)) { return ERROR_BAD_EXE_FORMAT; }
    n.base = base;
    n.selectVtable = base + wn::SelectActionVtable;
    n.wrenchVtable = base + pn::WrenchVtable;
    n.lookup = reinterpret_cast<LookupFn>(base + wn::WeaponLookup);
    return ERROR_SUCCESS;
}
// Tests replace the engine here.
DWORD (*gResolve)(Natives&) = Resolve;

// --- the skip (the hook) ----------------------------------------------------------
// The selections skipped and not yet finished, with the frame each was skipped
// in. Main thread only: the hook acts on no other.
struct Pending { std::uint32_t id = 0; std::uint64_t frame = 0; bool sounded = false; };
std::array<Pending, 4> pending{};
Pending* FreePending()
{
    for (auto& p : pending) {
        if (!p.id) { return &p; }
    }
    return nullptr;
}
struct Select { std::uintptr_t action = 0; std::uint32_t id = 0; bool select = false; };
// The wrench's select action as OnEquip built it: its vtable, the weapon's, and
// the same item id on both. Plain reads, guarded.
bool ReadSelect(std::uintptr_t weapon, const std::uintptr_t* slot, Select& s)
{
    __try {
        if (!weapon || !slot || !*slot) { return false; }
        s.action = *slot;
        if (*reinterpret_cast<const std::uintptr_t*>(s.action) != natives.selectVtable ||
            *reinterpret_cast<const std::uintptr_t*>(weapon) != natives.wrenchVtable) { return false; }
        s.id = *reinterpret_cast<const std::uint32_t*>(s.action + wn::ActionItemId);
        s.select = *reinterpret_cast<const std::uint8_t*>(s.action + wn::ActionSelectFlag) != 0;
        return s.id != 0 && *reinterpret_cast<const std::uint32_t*>(weapon + wn::WeaponItemId) == s.id;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// What PlayAction does with an action it does not queue (a fragment of -1):
// drop the caller's reference. Then Enter's part: a select marks the weapon busy.
bool Skip(std::uintptr_t weapon, const Select& s)
{
    __try {
        auto* const refs = reinterpret_cast<int*>(s.action + wn::ActionRefCount);
        if (--*refs <= 0) { Virtual<ReleaseFn>(s.action, wn::ActionReleaseSlot)(s.action); }
        if (s.select) { Virtual<WeaponFn>(weapon, wn::SelectBegunSlot)(weapon); }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void __fastcall PlayActionHook(std::uintptr_t weapon, std::uintptr_t* slot, float value)
{
    Select s{};
    if (enabled.load() && !fault.load() && ReadSelect(weapon, slot, s)) {
        if (GetCurrentThreadId() != serviceThread.load()) {
            // The finish runs on the main thread; anywhere else the draw plays.
            ++elsewhere;
        } else if (Pending* const free = FreePending(); !free) {
            ++full;
        } else if (!Skip(weapon, s)) {
            // The reference may be gone already: never hand the action on.
            fault = true;
            Log("fault detail=skip id=" + std::to_string(s.id));
            return;
        } else {
            *free = {s.id, frames.load(), false};
            ++waiting;
            ++(s.select ? selects : pickups);
            Log("skip id=" + std::to_string(s.id) + " kind=" + (s.select ? "select" : "pickup"));
            return;
        }
    }
    originalPlayAction.load()(weapon, slot, value);
}

// --- the stow (the hook) ------------------------------------------------------------
enum class StowKind : std::uint8_t { animate, instant, lowered };
// The wrench with a fragment table to animate with: instant unless lowered (+0x2C9,
// where Prey's animated stow also ends the switch, which its instant path does not).
// Plain reads, guarded.
StowKind ReadStow(std::uintptr_t weapon, std::uint32_t& id)
{
    __try {
        if (!weapon || *reinterpret_cast<const std::uintptr_t*>(weapon) != natives.wrenchVtable ||
            !*reinterpret_cast<const std::uintptr_t*>(weapon + wn::WeaponFragments)) { return StowKind::animate; }
        id = *reinterpret_cast<const std::uint32_t*>(weapon + wn::WeaponItemId);
        return *reinterpret_cast<const std::uint8_t*>(weapon + wn::WeaponLowered) ? StowKind::lowered : StowKind::instant;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return StowKind::animate;
    }
}
// The wrench's OnUnequip. Asked to animate, it stows at once instead: Prey's own
// path for no animation, which ends where the stow action's Exit ends (Stow and
// "empty"). The stow's sound then plays as its clip would have, at its start.
void __fastcall UnequipHook(std::uintptr_t weapon, bool animate)
{
    std::uint32_t id = 0;
    StowKind kind = StowKind::animate;
    if (animate && enabled.load() && !fault.load() &&
        GetCurrentThreadId() == serviceThread.load()) {
        kind = ReadStow(weapon, id);
    }
    if (kind == StowKind::lowered) { ++stowsLowered; }
    originalUnequip.load()(weapon, kind == StowKind::instant ? false : animate);
    if (kind != StowKind::instant) { return; }
    ++stows;
    const bool sound = WrenchSoundPlayStow();
    if (sound) { ++stowSounds; }
    Log("stow id=" + std::to_string(id) + " sound=" + std::to_string(sound));
}

// Both hooks or neither.
bool Install(const Natives& n)
{
    void* originals[2]{};
    const hook::Spec hooks[] = {
        {reinterpret_cast<void*>(n.base + wn::PlayAction), reinterpret_cast<void*>(PlayActionHook), &originals[0]},
        {reinterpret_cast<void*>(n.base + wn::WrenchUnequip), reinterpret_cast<void*>(UnequipHook), &originals[1]}};
    const auto result = hook::InstallAll(hooks, [&] {
        originalPlayAction = reinterpret_cast<PlayActionFn>(originals[0]);
        originalUnequip = reinterpret_cast<UnequipFn>(originals[1]);
    });
    if (!result) {
        Log(std::string("hooks result=") + hook::StepName(result.step) + " status=" + std::to_string(result.status) +
            " index=" + std::to_string(result.index));
        return false;
    }
    Log("hooks result=0 targets=PlayAction,WrenchUnequip");
    return true;
}
// Tests replace the installer here.
bool (*gInstall)(const Natives&) = Install;

// --- the finish (main thread) -----------------------------------------------------
enum class Finish : std::uint8_t { done, gone, fault };
// Exit's part: the weapon by its item id, as Exit finds it, then its +0x110,
// Prey's own select-finished (SelectDone ends the switch and frees the weapon).
Finish FinishSelect(std::uint32_t id)
{
    __try {
        const auto weapon = natives.lookup(id);
        if (!weapon || *reinterpret_cast<const std::uintptr_t*>(weapon) != natives.wrenchVtable) { return Finish::gone; }
        Virtual<WeaponFn>(weapon, wn::SelectFinishedSlot)(weapon);
        return Finish::done;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return Finish::fault;
    }
}
// Until the hook is in, or refused for good.
void Prepare()
{
    Natives n{};
    const DWORD ready = gResolve(n);
    if (ready == ERROR_NOT_READY) { return; }
    if (ready) {
        fault = true;
        gate = "refused";
        Log("gate result=" + std::to_string(ready) + " detail=bytes_mismatch");
        return;
    }
    natives = n;   // before the hook can run
    if (!gInstall(natives)) {
        fault = true;
        gate = "hook_failed";
        return;
    }
    gate = "ok";
    installed = true;
    Log("gate result=0 rows=" + std::to_string(std::size(wn::Contracts)) + "+2");
}
} // namespace

void ServiceWrenchDrawFrame()
{
    serviceThread = GetCurrentThreadId();
    const auto now = ++frames;
    if (!installed.load() && !fault.load()) { Prepare(); }
    for (auto& p : pending) {
        if (!p.id) { continue; }
        // The draw's sound, the next frame, as its clips would have played it
        // 11-13 ms in (WrenchSound.h).
        if (!p.sounded && now - p.frame >= 1) {
            p.sounded = true;
            if (WrenchSoundPlayDraw()) { ++sounds; }
        }
        // A whole frame after the skip: SelectItem has stored the wrench as
        // current, so SelectDone ends the switch.
        if (now - p.frame < 2) { continue; }
        const auto id = p.id;
        const auto waited = now - p.frame;
        p = {};
        --waiting;
        const Finish result = FinishSelect(id);
        if (result == Finish::done) { ++finished; }
        else if (result == Finish::gone) { ++gone; }
        else { fault = true; }
        Log("finish id=" + std::to_string(id) + " result=" +
            (result == Finish::done ? "finished" : result == Finish::gone ? "gone" : "fault") + " frames=" + std::to_string(waited));
    }
}
void SetWrenchDrawSkip(unsigned on)
{
    enabled = on != 0;
    Log("settings enabled=" + std::to_string(enabled.load()));
}
std::string WrenchDrawReport()
{
    return " enabled=" + std::to_string(enabled.load()) + " hooks=" + std::to_string(installed.load()) + " gate=" + gate.load() +
           " fault=" + std::to_string(fault.load()) + " selects=" + std::to_string(selects.load()) + " pickups=" +
           std::to_string(pickups.load()) + " finished=" + std::to_string(finished.load()) + " gone=" + std::to_string(gone.load()) +
           " waiting=" + std::to_string(waiting.load()) + " full=" + std::to_string(full.load()) + " elsewhere=" +
           std::to_string(elsewhere.load()) + " sounds=" + std::to_string(sounds.load()) + " stows=" + std::to_string(stows.load()) +
           " stowSounds=" + std::to_string(stowSounds.load()) + " stowsLowered=" + std::to_string(stowsLowered.load()) +
           " frames=" + std::to_string(frames.load());
}
} // namespace preyvr::dll
