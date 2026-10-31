#include "equipment/weapon/WeaponDraw.h"
#include "engine/EngineAccess.h"
#include "engine/hooks/HookInstall.h"
#include "base/Logger.h"
#include "equipment/weapon/WeaponSound.h"
#include "equipment/body/EquipmentNative.h"
#include "engine/NativeContract.h"
#include "base/TextFormat.h"
#include "equipment/weapon/WeaponDrawNative.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <iterator>

namespace preyvr::dll {
namespace {
namespace wn = weapondraw::native;
namespace en = equipment::native;

void Log(const std::string& line) { lifecycle::Log("preyvr_weapon_draw " + line); }

// Every function hooked or called through a named RVA or vtable slot is one a
// gate hashes: this lane's rows, and EquipmentNative.h's SelectItem, lookup and player.
constexpr bool ClassesGated()
{
    for (const auto& c : wn::WeaponClasses) {
        if (!contract::Gated(wn::Contracts, c.begun) || !contract::Gated(wn::Contracts, c.done) ||
            !contract::Gated(wn::Contracts, c.finished)) { return false; }
    }
    return true;
}
static_assert(ClassesGated());
static_assert(contract::Gated(wn::Contracts, wn::PlayAction) && contract::Gated(wn::Contracts, wn::OnEquip) &&
              contract::Gated(wn::Contracts, wn::SelectActionCtor) && contract::Gated(wn::Contracts, wn::ActionBaseCtor) &&
              contract::Gated(wn::Contracts, wn::SelectEnter) && contract::Gated(wn::Contracts, wn::SelectExit) &&
              contract::Gated(en::Contracts, wn::SelectItem) && contract::Gated(en::Contracts, wn::WeaponLookup) &&
              contract::Gated(en::Contracts, wn::GetPlayer) && contract::Gated(wn::Contracts, wn::OnUnequip) &&
              contract::Gated(wn::Contracts, wn::Stow) && contract::Gated(wn::Contracts, wn::StowActionCtor) &&
              contract::Gated(wn::Contracts, wn::StowExit));

std::atomic<bool> skip{true}, installed{false}, fault{false};
std::atomic<const char*> gate{"unchecked"};
std::atomic<unsigned long> serviceThread{0};
std::atomic<std::uint64_t> frames{0}, selects{0}, pickups{0}, finished{0}, gone{0}, full{0}, elsewhere{0}, sounds{0};
std::atomic<std::uint64_t> stows{0}, stowSounds{0}, stowsAttacking{0};
std::atomic<unsigned> waiting{0};

using PlayActionFn = void(__fastcall*)(std::uintptr_t, std::uintptr_t*, float);
using UnequipFn = void(__fastcall*)(std::uintptr_t, bool);
using WeaponFn = void(__fastcall*)(std::uintptr_t);
using ReleaseFn = void(__fastcall*)(std::uintptr_t);   // the action's own release, as PlayAction calls it
using LookupFn = std::uintptr_t(__fastcall*)(std::uint32_t);
using PlayerFn = std::uintptr_t(__fastcall*)();
using ObjectFn = std::uintptr_t(__fastcall*)(std::uintptr_t);
using NameFn = const char*(__fastcall*)(std::uintptr_t);
std::atomic<PlayActionFn> originalPlayAction{nullptr};
std::atomic<UnequipFn> originalUnequip{nullptr};

// What the hook and the finish use, resolved on the main thread before the hook
// goes live and never changed after. Tests put fakes here.
struct Natives {
    std::uintptr_t base = 0, selectVtable = 0;
    std::array<std::uintptr_t, std::size(wn::WeaponClasses)> weaponVtables{};
    LookupFn lookup = nullptr;
    PlayerFn player = nullptr;
};
Natives natives{};

// A virtual function of an engine object, by its vtable slot's byte offset.
template <class Fn> Fn Virtual(std::uintptr_t object, std::size_t slot)
{
    return reinterpret_cast<Fn>((*reinterpret_cast<const std::uintptr_t* const*>(object))[slot / sizeof(void*)]);
}
bool WeaponVtable(std::uintptr_t table)
{
    for (const auto known : natives.weaponVtables) {
        if (known == table) { return true; }
    }
    return false;
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
            if ((row.rva == wn::SelectItem || row.rva == wn::WeaponLookup || row.rva == wn::GetPlayer) &&
                !contract::BodyMatches(base, row)) { return false; }
        }
        // What is called through vtables: each class's begun, done and finished, and
        // the select action's Enter and Exit, whose work the lane splits. The stow:
        // each class's own OnUnequip, which reaches the hooked one, and the stow
        // action's Exit, which the instant stow stands in for.
        for (const auto& c : wn::WeaponClasses) {
            if (SlotTarget(base, c.vtable, wn::SelectBegunSlot) != c.begun ||
                SlotTarget(base, c.vtable, wn::SelectDoneSlot) != c.done ||
                SlotTarget(base, c.vtable, wn::SelectFinishedSlot) != c.finished ||
                SlotTarget(base, c.vtable, wn::OnUnequipSlot) != c.unequip) { return false; }
        }
        return SlotTarget(base, wn::SelectActionVtable, wn::EnterSlot) == wn::SelectEnter &&
               SlotTarget(base, wn::SelectActionVtable, wn::ExitSlot) == wn::SelectExit &&
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
    for (std::size_t i = 0; i < n.weaponVtables.size(); ++i) { n.weaponVtables[i] = base + wn::WeaponClasses[i].vtable; }
    n.lookup = reinterpret_cast<LookupFn>(base + wn::WeaponLookup);
    n.player = reinterpret_cast<PlayerFn>(base + wn::GetPlayer);
    return ERROR_SUCCESS;
}
// Tests replace the engine here.
DWORD (*gResolve)(Natives&) = Resolve;

// The weapon's entity class name, for its sound and the log: "?" when it can't be read.
struct ClassName { char text[48]; };
void CopyClassName(std::uintptr_t weapon, ClassName& out)
{
    __try {
        const auto entity = *reinterpret_cast<const std::uintptr_t*>(weapon + wn::WeaponEntity);
        const auto cls = entity ? Virtual<ObjectFn>(entity, wn::EntityClassSlot)(entity) : 0;
        const char* const name = cls ? Virtual<NameFn>(cls, wn::ClassNameSlot)(cls) : nullptr;
        std::size_t i = 0;
        for (; name && i + 1 < sizeof(out.text) && name[i]; ++i) { out.text[i] = name[i]; }
        out.text[i] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out.text[0] = 0;
    }
    if (!out.text[0]) {
        out.text[0] = '?';
        out.text[1] = 0;
    }
}

// --- the skip (the hook) ----------------------------------------------------------
// The selections skipped and not yet finished, with the frame each was skipped
// in and the sound it plays. Main thread only: the hook acts on no other.
struct Pending { std::uint32_t id = 0; std::uint64_t frame = 0; bool sounded = false; unsigned sound = 0; };
std::array<Pending, 4> pending{};
Pending* FreePending()
{
    for (auto& p : pending) {
        if (!p.id) { return &p; }
    }
    return nullptr;
}
struct Select { std::uintptr_t action = 0; std::uint32_t id = 0; bool select = false; };
// A weapon's select action as OnEquip built it: its vtable, a known weapon's,
// and the same item id on both. Plain reads, guarded.
bool ReadSelect(std::uintptr_t weapon, const std::uintptr_t* slot, Select& s)
{
    __try {
        if (!weapon || !slot || !*slot) { return false; }
        s.action = *slot;
        if (*reinterpret_cast<const std::uintptr_t*>(s.action) != natives.selectVtable ||
            !WeaponVtable(*reinterpret_cast<const std::uintptr_t*>(weapon))) { return false; }
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
    if (skip.load() && !fault.load() && ReadSelect(weapon, slot, s)) {
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
            ClassName name{};
            CopyClassName(weapon, name);
            *free = {s.id, frames.load(), false, WeaponSoundFor(name.text)};
            ++waiting;
            ++(s.select ? selects : pickups);
            Log("skip id=" + std::to_string(s.id) + " kind=" + (s.select ? "select" : "pickup") + " class=" + name.text);
            return;
        }
    }
    originalPlayAction.load()(weapon, slot, value);
}

// --- the stow (the hook) ------------------------------------------------------------
enum class StowKind : std::uint8_t { animate, instant, attacking };
// The player's current weapon, in the hand (which the instant path needs) with a
// fragment table to animate with: instant unless an attack is under way (+0x2C9, where
// Prey plays a closing fragment first and then the stow). Anything else the shared
// OnUnequip passes over or is Prey's. Plain reads, guarded.
StowKind ReadStow(std::uintptr_t weapon, std::uint32_t& id)
{
    __try {
        if (!weapon || !WeaponVtable(*reinterpret_cast<const std::uintptr_t*>(weapon)) ||
            !*reinterpret_cast<const std::uintptr_t*>(weapon + wn::WeaponFragments) ||
            !*reinterpret_cast<const std::uintptr_t*>(weapon + wn::WeaponHandAttachment)) { return StowKind::animate; }
        const auto player = natives.player();
        id = *reinterpret_cast<const std::uint32_t*>(weapon + wn::WeaponItemId);
        if (!player || !id ||
            *reinterpret_cast<const std::uint32_t*>(player + wn::PlayerEquipment + wn::EquipmentCurrent) != id) {
            return StowKind::animate;
        }
        return *reinterpret_cast<const std::uint8_t*>(weapon + wn::WeaponAttacking) ? StowKind::attacking : StowKind::instant;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return StowKind::animate;
    }
}
// The shared OnUnequip. Asked to animate, it stows at once instead: Prey's own
// path for no animation, which ends where the stow action's Exit ends (Stow and
// "empty"). The stow's sound then plays as its clip would have, at its start.
void __fastcall UnequipHook(std::uintptr_t weapon, bool animate)
{
    std::uint32_t id = 0;
    StowKind kind = StowKind::animate;
    if (animate && skip.load() && !fault.load() && GetCurrentThreadId() == serviceThread.load()) {
        kind = ReadStow(weapon, id);
    }
    if (kind == StowKind::attacking) { ++stowsAttacking; }
    // Read while it is still in the hand.
    ClassName name{};
    if (kind == StowKind::instant) { CopyClassName(weapon, name); }
    originalUnequip.load()(weapon, kind == StowKind::instant ? false : animate);
    if (kind != StowKind::instant) { return; }
    ++stows;
    const bool sound = WeaponSoundPlayStow(WeaponSoundFor(name.text));
    if (sound) { ++stowSounds; }
    Log("stow id=" + std::to_string(id) + " class=" + name.text + " sound=" + std::to_string(sound));
}

// Both hooks or neither.
bool Install(const Natives& n)
{
    void* originals[2]{};
    const hook::Spec hooks[] = {
        {reinterpret_cast<void*>(n.base + wn::PlayAction), reinterpret_cast<void*>(PlayActionHook), &originals[0]},
        {reinterpret_cast<void*>(n.base + wn::OnUnequip), reinterpret_cast<void*>(UnequipHook), &originals[1]}};
    const auto result = hook::InstallAll(hooks, [&] {
        originalPlayAction = reinterpret_cast<PlayActionFn>(originals[0]);
        originalUnequip = reinterpret_cast<UnequipFn>(originals[1]);
    });
    if (!result) {
        Log(std::string("hooks result=") + hook::StepName(result.step) + " status=" + std::to_string(result.status) +
            " index=" + std::to_string(result.index));
        return false;
    }
    Log("hooks result=0 targets=PlayAction,OnUnequip");
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
        if (!weapon || !WeaponVtable(*reinterpret_cast<const std::uintptr_t*>(weapon))) { return Finish::gone; }
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
    Log("gate result=0 rows=" + std::to_string(std::size(wn::Contracts)) + "+3 classes=" +
        std::to_string(std::size(wn::WeaponClasses)));
}
} // namespace

void ServiceWeaponDrawFrame()
{
    serviceThread = GetCurrentThreadId();
    const auto now = ++frames;
    if (!installed.load() && !fault.load()) { Prepare(); }
    for (auto& p : pending) {
        if (!p.id) { continue; }
        // The draw's sound, the next frame, as its clips would have played it
        // 10-16 ms in (WeaponSound.h).
        if (!p.sounded && now - p.frame >= 1) {
            p.sounded = true;
            if (WeaponSoundPlayDraw(p.sound)) { ++sounds; }
        }
        // A whole frame after the skip: SelectItem has stored the weapon as
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
void SetWeaponDrawAnimations(unsigned play)
{
    skip = play == 0;
    Log("settings animations=" + std::to_string(WeaponDrawAnimations()));
}
unsigned WeaponDrawAnimations() { return skip.load() ? 0u : 1u; }
std::string WeaponDrawReport()
{
    return " animations=" + std::to_string(WeaponDrawAnimations()) + " hooks=" + std::to_string(installed.load()) + " gate=" +
           gate.load() + " fault=" + std::to_string(fault.load()) + " selects=" + std::to_string(selects.load()) + " pickups=" +
           std::to_string(pickups.load()) + " finished=" + std::to_string(finished.load()) + " gone=" + std::to_string(gone.load()) +
           " waiting=" + std::to_string(waiting.load()) + " full=" + std::to_string(full.load()) + " elsewhere=" +
           std::to_string(elsewhere.load()) + " sounds=" + std::to_string(sounds.load()) + " stows=" + std::to_string(stows.load()) +
           " stowSounds=" + std::to_string(stowSounds.load()) + " stowsAttacking=" + std::to_string(stowsAttacking.load()) +
           " frames=" + std::to_string(frames.load());
}
} // namespace preyvr::dll
