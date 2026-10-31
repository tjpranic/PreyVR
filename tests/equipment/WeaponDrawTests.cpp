// The weapon draw: the production lane with Prey replaced at its ABI boundary --
// PlayAction's original, the shared OnUnequip's, the weapons' and the select action's
// vtables, the weapon lookup, the player, and the installer.
#include "equipment/weapon/WeaponDraw.cpp"
#include "DllTestSupport.h"
#include "TestSupport.h"
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

PREYVR_TEST_CAPTURE_LOG()
PREYVR_TEST_STUB_MINHOOK()
// The sound (WeaponSound.cpp, its own test): what the draw lane asks of it.
bool soundOk = true;
unsigned soundPlays = 0, stowPlays = 0, lastDraw = 0, lastStow = 0;
namespace preyvr::dll {
unsigned WeaponSoundFor(const char* className) { return std::strcmp(className, "ArkWeaponShotgun") == 0 ? 1 : 0; }
bool WeaponSoundPlayDraw(unsigned sound) { ++soundPlays; lastDraw = sound; return soundOk; }
bool WeaponSoundPlayStow(unsigned sound) { ++stowPlays; lastStow = sound; return soundOk; }
}
namespace {
using namespace preyvr::dll;
using preyvr::test::Check;
using preyvr::test::gLines;
using preyvr::test::Logged;
std::string Report() { return WeaponDrawReport() + " "; }
bool Reports(const char* field) { return Report().find(std::string(" ") + field + " ") != std::string::npos; }
void Put(void* base, std::size_t offset, const void* value, std::size_t size)
{
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, value, size);
}
template <class T> T Get(const void* base, std::size_t offset)
{
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

// Fake engine objects, each starting with its vtable.
using Slot = void*;
struct Object { Slot* table; unsigned char bytes[0x2000]; };
Slot wrenchTable[64]{}, gunTable[64]{}, unknownTable[64]{}, selectTable[32]{}, otherTable[32]{};
Slot entityTable[8]{}, classTable[4]{};
Object wrench{wrenchTable, {}}, gun{gunTable, {}}, unknown{unknownTable, {}}, player{nullptr, {}};
Object gunEntity{entityTable, {}}, gunClass{classTable, {}};
constexpr std::uint32_t kWrenchId = 7, kGunId = 9, kUnknownId = 11;
std::vector<std::uintptr_t> begun, done, released, played;
bool wrenchGone = false, havePlayer = true;
void __fastcall Begun(std::uintptr_t weapon) { begun.push_back(weapon); }
void __fastcall Finished(std::uintptr_t weapon) { done.push_back(weapon); }
void __fastcall Release(std::uintptr_t action) { released.push_back(action); }
std::uintptr_t __fastcall Lookup(std::uint32_t id)
{
    if (id == kWrenchId && !wrenchGone) { return reinterpret_cast<std::uintptr_t>(&wrench); }
    return id == kGunId ? reinterpret_cast<std::uintptr_t>(&gun) : 0;
}
std::uintptr_t __fastcall Player() { return havePlayer ? reinterpret_cast<std::uintptr_t>(&player) : 0; }
void Current(std::uint32_t id) { Put(&player, wn::PlayerEquipment + wn::EquipmentCurrent, &id, 4); }
const char* __fastcall GunClassName(std::uintptr_t) { return "ArkWeaponShotgun"; }
std::uintptr_t __fastcall GunEntityClass(std::uintptr_t) { return reinterpret_cast<std::uintptr_t>(&gunClass); }
// PlayAction's original: queues (not modelled) and drops the caller's reference.
void __fastcall OriginalPlay(std::uintptr_t, std::uintptr_t* slot, float)
{
    played.push_back(*slot);
    auto* const action = reinterpret_cast<std::uint8_t*>(*slot);
    const int refs = Get<int>(action, wn::ActionRefCount) - 1;
    Put(action, wn::ActionRefCount, &refs, 4);
}
unsigned installs = 0;
DWORD readiness = ERROR_SUCCESS;
DWORD FakeResolve(Natives& n)
{
    n = {};
    if (readiness) { return readiness; }
    n.base = 1;
    n.selectVtable = reinterpret_cast<std::uintptr_t>(selectTable);
    n.weaponVtables[0] = reinterpret_cast<std::uintptr_t>(wrenchTable);
    n.weaponVtables[1] = reinterpret_cast<std::uintptr_t>(gunTable);
    n.lookup = Lookup;
    n.player = Player;
    return ERROR_SUCCESS;
}
// The shared OnUnequip: what it was asked.
struct Unequip { std::uintptr_t weapon; bool animate; };
std::vector<Unequip> unequips;
void __fastcall OriginalUnequip(std::uintptr_t weapon, bool animate) { unequips.push_back({weapon, animate}); }
bool FakeInstall(const Natives&)
{
    ++installs;
    originalPlayAction = OriginalPlay;
    originalUnequip = OriginalUnequip;
    return true;
}
// A select action as OnEquip builds it: one reference (the caller's).
Object MakeAction(Slot* table, std::uint32_t id, bool select, int refs = 1)
{
    Object a{table, {}};
    Put(&a, wn::ActionItemId, &id, 4);
    const std::uint8_t flag = select ? 1 : 0;
    Put(&a, wn::ActionSelectFlag, &flag, 1);
    Put(&a, wn::ActionRefCount, &refs, 4);
    return a;
}
void Play(Object& weapon, Object& action)
{
    auto slot = reinterpret_cast<std::uintptr_t>(&action);
    PlayActionHook(reinterpret_cast<std::uintptr_t>(&weapon), &slot, 1.0f);
}
void Clear() { begun.clear(); done.clear(); released.clear(); played.clear(); }
void Hand(Object& weapon)
{
    const std::uintptr_t fragments = 0x1234, attachment = 0x5678;
    Put(&weapon, wn::WeaponFragments, &fragments, 8);
    Put(&weapon, wn::WeaponHandAttachment, &attachment, 8);
}
} // namespace

int main()
{
    wrenchTable[wn::SelectBegunSlot / 8] = reinterpret_cast<Slot>(Begun);
    wrenchTable[wn::SelectFinishedSlot / 8] = reinterpret_cast<Slot>(Finished);
    gunTable[wn::SelectBegunSlot / 8] = reinterpret_cast<Slot>(Begun);
    gunTable[wn::SelectFinishedSlot / 8] = reinterpret_cast<Slot>(Finished);
    unknownTable[wn::SelectBegunSlot / 8] = reinterpret_cast<Slot>(Begun);
    unknownTable[wn::SelectFinishedSlot / 8] = reinterpret_cast<Slot>(Finished);
    selectTable[wn::ActionReleaseSlot / 8] = reinterpret_cast<Slot>(Release);
    otherTable[wn::ActionReleaseSlot / 8] = reinterpret_cast<Slot>(Release);
    entityTable[wn::EntityClassSlot / 8] = reinterpret_cast<Slot>(GunEntityClass);
    classTable[wn::ClassNameSlot / 8] = reinterpret_cast<Slot>(GunClassName);
    Put(&wrench, wn::WeaponItemId, &kWrenchId, 4);
    Put(&gun, wn::WeaponItemId, &kGunId, 4);
    Put(&unknown, wn::WeaponItemId, &kUnknownId, 4);
    const auto gunEntityAt = reinterpret_cast<std::uintptr_t>(&gunEntity);
    Put(&gun, wn::WeaponEntity, &gunEntityAt, 8);
    gInstall = FakeInstall;

    // A refused gate: the hook is never installed, and the lane says why.
    gResolve = [](Natives&) -> DWORD { return ERROR_BAD_EXE_FORMAT; };
    ServiceWeaponDrawFrame();
    Check(installs == 0 && fault.load() && Reports("gate=refused") && Logged("gate result=193", "bytes_mismatch"),
          "mismatched bytes refuse the hook for good");
    ServiceWeaponDrawFrame();
    Check(installs == 0, "a refusal is not retried");
    fault = false;
    gate = "unchecked";

    // Not ready (no PreyDll yet): tried again next frame.
    gResolve = FakeResolve;
    readiness = ERROR_NOT_READY;
    ServiceWeaponDrawFrame();
    Check(installs == 0 && !fault.load() && Reports("gate=unchecked"), "not ready waits");
    readiness = ERROR_SUCCESS;
    ServiceWeaponDrawFrame();
    Check(installs == 1 && installed.load() && Reports("gate=ok") && Reports("hooks=1") &&
              Logged("gate result=0 rows=17+3 classes=9"),
          "the hook goes in once the gate holds");
    ServiceWeaponDrawFrame();
    Check(installs == 1, "installed once");

    // The wrench's select: not queued, the caller's reference dropped (the last
    // one: released), Enter's part done at once, Exit's a whole frame later.
    auto select = MakeAction(selectTable, kWrenchId, true);
    Play(wrench, select);
    Check(played.empty(), "the wrench's draw is not queued");
    Check(released.size() == 1 && released[0] == reinterpret_cast<std::uintptr_t>(&select), "the last reference is released");
    Check(begun.size() == 1 && begun[0] == reinterpret_cast<std::uintptr_t>(&wrench), "a select marks the wrench busy");
    Check(done.empty() && Reports("selects=1") && Reports("waiting=1") && Logged("skip id=7 kind=select class=?"),
          "skipped, waiting; a class that can't be read is ?");
    Check(soundPlays == 0, "no sound inside the skip");
    ServiceWeaponDrawFrame();
    Check(done.empty(), "not finished in the frame it was skipped in");
    Check(soundPlays == 1 && Reports("sounds=1"), "the draw's sound the next frame");
    ServiceWeaponDrawFrame();
    Check(done.size() == 1 && done[0] == reinterpret_cast<std::uintptr_t>(&wrench), "finished a whole frame later");
    Check(soundPlays == 1, "the sound once per draw");
    Check(Reports("finished=1") && Reports("waiting=0") && Logged("finish id=7 result=finished frames=2"), "the finish is counted");
    ServiceWeaponDrawFrame();
    ServiceWeaponDrawFrame();
    Check(done.size() == 1, "finished once");
    Clear();

    // Any known weapon: skipped the same way, through its own vtable, with its own sound.
    auto gunSelect = MakeAction(selectTable, kGunId, true);
    gLines.clear();
    Play(gun, gunSelect);
    Check(played.empty() && begun.size() == 1 && begun[0] == reinterpret_cast<std::uintptr_t>(&gun) &&
              Logged("skip id=9 kind=select class=ArkWeaponShotgun"),
          "a gun's draw is skipped too, named by its class");
    ServiceWeaponDrawFrame();
    Check(lastDraw == 1, "with the sound for its class");
    ServiceWeaponDrawFrame();
    Check(done.size() == 1 && done[0] == reinterpret_cast<std::uintptr_t>(&gun), "and finished through the gun's own vtable");
    lastDraw = 0;
    Clear();

    // A first pickup (Wpn_Pickup, the flag clear): Enter does not mark it busy.
    auto pickup = MakeAction(selectTable, kWrenchId, false);
    Play(wrench, pickup);
    Check(played.empty() && begun.empty() && released.size() == 1 && Reports("pickups=1") && Logged("kind=pickup"),
          "a first pickup is skipped without Enter's part");
    soundOk = false;
    ServiceWeaponDrawFrame();
    ServiceWeaponDrawFrame();
    Check(done.size() == 1 && Reports("finished=3"), "and finished");
    Check(soundPlays == 3 && Reports("sounds=2"), "a sound that could not play is not counted, and the finish still comes");
    soundOk = true;
    Clear();

    // Someone else still holds a reference: dropped, not released.
    auto held = MakeAction(selectTable, kWrenchId, true, 2);
    Play(wrench, held);
    Check(released.empty() && Get<int>(&held, wn::ActionRefCount) == 1, "only the caller's reference is dropped");
    ServiceWeaponDrawFrame();
    ServiceWeaponDrawFrame();
    Clear();

    // Everything else is Prey's: a class the gate doesn't know, another action, a mismatched id.
    auto unknownSelect = MakeAction(selectTable, kUnknownId, true);
    Play(unknown, unknownSelect);
    auto fire = MakeAction(otherTable, kWrenchId, true);
    Play(wrench, fire);
    auto stray = MakeAction(selectTable, kGunId, true);
    Play(wrench, stray);
    Check(played.size() == 3 && begun.empty() && released.empty(), "an unknown class's draw, other actions and a stray id play");
    Clear();

    // Animations on: the draw plays.
    SetWeaponDrawAnimations(1);
    auto off = MakeAction(selectTable, kWrenchId, true);
    Play(wrench, off);
    Check(played.size() == 1 && begun.empty() && Reports("animations=1") && Logged("settings animations=1") &&
              WeaponDrawAnimations() == 1, "animations on plays the draw");
    SetWeaponDrawAnimations(0);
    Check(WeaponDrawAnimations() == 0, "off again");
    Clear();

    // Another thread: the finish could not run there, so the draw plays.
    auto elsewhereAction = MakeAction(selectTable, kWrenchId, true);
    std::thread([&] { Play(wrench, elsewhereAction); }).join();
    Check(played.size() == 1 && begun.empty() && Reports("elsewhere=1"), "off the main thread the draw plays");
    Clear();

    // The weapon is gone before its finish: nothing is called.
    auto lost = MakeAction(selectTable, kWrenchId, true);
    Play(wrench, lost);
    wrenchGone = true;
    ServiceWeaponDrawFrame();
    ServiceWeaponDrawFrame();
    Check(done.empty() && Reports("gone=1") && Logged("result=gone"), "a vanished weapon is not finished");
    wrenchGone = false;
    Clear();

    // Found under the id, but no longer a weapon the gate knows: not finished either.
    auto changed = MakeAction(selectTable, kWrenchId, true);
    Play(wrench, changed);
    wrench.table = unknownTable;
    ServiceWeaponDrawFrame();
    ServiceWeaponDrawFrame();
    Check(done.empty() && Reports("gone=2"), "only a known weapon is finished");
    wrench.table = wrenchTable;
    Clear();

    // Four waiting at most; a fifth plays.
    Object queued[5] = {MakeAction(selectTable, kWrenchId, true), MakeAction(selectTable, kWrenchId, true),
                        MakeAction(selectTable, kWrenchId, true), MakeAction(selectTable, kWrenchId, true),
                        MakeAction(selectTable, kWrenchId, true)};
    for (auto& a : queued) { Play(wrench, a); }
    Check(played.size() == 1 && Reports("waiting=4") && Reports("full=1"), "a fifth waiting select plays");
    ServiceWeaponDrawFrame();
    ServiceWeaponDrawFrame();
    Check(done.size() == 4 && Reports("waiting=0"), "every waiting select is finished");
    Clear();

    // Turned on while one waits: it is still finished.
    auto late = MakeAction(selectTable, kWrenchId, true);
    Play(wrench, late);
    SetWeaponDrawAnimations(1);
    ServiceWeaponDrawFrame();
    ServiceWeaponDrawFrame();
    Check(done.size() == 1, "a skipped select is finished even after turning the animations on");
    SetWeaponDrawAnimations(0);
    Clear();

    // The stow: asked to animate, the current weapon stows at once and its sound plays.
    Hand(wrench);
    Hand(gun);
    Current(kWrenchId);
    const auto wrenchAt = reinterpret_cast<std::uintptr_t>(&wrench);
    const auto gunAt = reinterpret_cast<std::uintptr_t>(&gun);
    gLines.clear();
    UnequipHook(wrenchAt, true);
    Check(unequips.size() == 1 && unequips[0].weapon == wrenchAt && !unequips[0].animate, "the stow is asked not to animate");
    Check(stowPlays == 1 && lastStow == 0 && Reports("stows=1") && Reports("stowSounds=1") && Logged("stow id=7 class=? sound=1"),
          "and its sound plays, at once");
    // Any known weapon, the same way.
    Current(kGunId);
    UnequipHook(gunAt, true);
    Check(unequips.size() == 2 && unequips[1].weapon == gunAt && !unequips[1].animate && stowPlays == 2 && lastStow == 1 &&
              Reports("stows=2") && Logged("stow id=9 class=ArkWeaponShotgun sound=1"),
          "a gun stows at once too, with the sound for its class");
    // Not asked to animate (an instant stow of Prey's own): passed on as it is, no sound.
    UnequipHook(gunAt, false);
    Check(unequips.size() == 3 && !unequips[2].animate && stowPlays == 2 && Reports("stows=2"), "Prey's instant stow is its own");
    // An attack under way: Prey's animated stow, whose closing fragment comes first.
    const std::uint8_t attacking = 1, idle = 0;
    Put(&gun, wn::WeaponAttacking, &attacking, 1);
    UnequipHook(gunAt, true);
    Check(unequips.size() == 4 && unequips[3].animate && stowPlays == 2 && Reports("stowsAttacking=1"),
          "stowed mid-attack keeps the animated stow");
    Put(&gun, wn::WeaponAttacking, &idle, 1);
    // Not the current weapon: the shared OnUnequip does nothing with it, so no stow and no sound.
    Current(kWrenchId);
    UnequipHook(gunAt, true);
    Check(unequips.size() == 5 && unequips[4].animate && stowPlays == 2 && Reports("stows=2"), "only the current weapon stows");
    Current(kGunId);
    // No player to ask: passed on.
    havePlayer = false;
    UnequipHook(gunAt, true);
    Check(unequips.size() == 6 && unequips[5].animate && stowPlays == 2, "without a player it is Prey's");
    havePlayer = true;
    // No fragment table: nothing to animate, passed on.
    const std::uintptr_t nothing = 0;
    Put(&gun, wn::WeaponFragments, &nothing, 8);
    UnequipHook(gunAt, true);
    Check(unequips.size() == 7 && unequips[6].animate && stowPlays == 2, "without fragments it is Prey's");
    Hand(gun);
    // Not in the hand: Prey's instant path would have nothing to stow, so its animated one runs.
    Put(&gun, wn::WeaponHandAttachment, &nothing, 8);
    UnequipHook(gunAt, true);
    Check(unequips.size() == 8 && unequips[7].animate && stowPlays == 2, "not in the hand it is Prey's");
    Hand(gun);
    // Animations on, or another thread: Prey's animated stow.
    SetWeaponDrawAnimations(1);
    UnequipHook(gunAt, true);
    SetWeaponDrawAnimations(0);
    std::thread([&] { UnequipHook(gunAt, true); }).join();
    Check(unequips.size() == 10 && unequips[8].animate && unequips[9].animate && stowPlays == 2,
          "animations on, or off the main thread, the stow animates");
    // A class the gate doesn't know under this hook: passed on.
    UnequipHook(reinterpret_cast<std::uintptr_t>(&unknown), true);
    Check(unequips.size() == 11 && unequips[10].animate && Reports("stows=2"), "only a known weapon is stowed at once");
    // A sound that could not play: the stow still instant, counted without a sound.
    soundOk = false;
    UnequipHook(gunAt, true);
    Check(unequips.size() == 12 && !unequips[11].animate && Reports("stows=3") && Reports("stowSounds=2") && Logged("sound=0"),
          "the stow does not wait on its sound");
    soundOk = true;

    Check(!fault.load() && Reports("fault=0"), "no fault");
    std::cout << "weapon_draw: skipped, finished a frame later, everything else Prey's\n";
}
