// The wrench draw: the production lane with Prey replaced at its ABI boundary --
// PlayAction's original, the wrench's and the select action's vtables, the
// weapon lookup, and the installer.
#include "../src/dll/WrenchDraw.cpp"
#include "TestSupport.h"
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

std::vector<std::string> lines;
namespace preyvr::lifecycle { void Log(std::string_view line) { lines.emplace_back(line); } }
// The sound capture (WrenchSound.cpp, its own test): what the draw lane asks of it.
bool capturing = false, soundOk = true;
std::vector<std::uint32_t> seen;
unsigned soundFrames = 0, soundPlays = 0, stowPlays = 0;
namespace preyvr::dll {
bool EnsureMinHook() { return false; }
bool WrenchSoundCapturing() { return capturing; }
void WrenchSoundSelectSeen(std::uint32_t item, bool) { seen.push_back(item); }
void ServiceWrenchSoundFrame() { ++soundFrames; }
bool WrenchSoundPlayDraw() { ++soundPlays; return soundOk; }
bool WrenchSoundPlayStow() { ++stowPlays; return soundOk; }
}
namespace {
using namespace preyvr::dll;
using preyvr::test::Check;
bool Logged(const char* a, const char* b = "")
{
    for (const auto& l : lines) {
        if (l.find(a) != std::string::npos && l.find(b) != std::string::npos) { return true; }
    }
    return false;
}
std::string Report() { return WrenchDrawReport() + " "; }
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
struct Object { Slot* table; unsigned char bytes[0x600]; };
Slot wrenchTable[64]{}, gunTable[64]{}, selectTable[32]{}, otherTable[32]{};
Object wrench{wrenchTable, {}}, gun{gunTable, {}};
constexpr std::uint32_t kWrenchId = 7, kGunId = 9;
std::vector<std::uintptr_t> begun, done, released, played;
bool wrenchGone = false;
void __fastcall Begun(std::uintptr_t weapon) { begun.push_back(weapon); }
void __fastcall Finished(std::uintptr_t weapon) { done.push_back(weapon); }
void __fastcall Release(std::uintptr_t action) { released.push_back(action); }
std::uintptr_t __fastcall Lookup(std::uint32_t id)
{
    if (id == kWrenchId && !wrenchGone) { return reinterpret_cast<std::uintptr_t>(&wrench); }
    return id == kGunId ? reinterpret_cast<std::uintptr_t>(&gun) : 0;
}
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
    n.wrenchVtable = reinterpret_cast<std::uintptr_t>(wrenchTable);
    n.lookup = Lookup;
    return ERROR_SUCCESS;
}
// The wrench's OnUnequip: what it was asked.
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
} // namespace

int main()
{
    wrenchTable[wn::SelectBegunSlot / 8] = reinterpret_cast<Slot>(Begun);
    wrenchTable[wn::SelectFinishedSlot / 8] = reinterpret_cast<Slot>(Finished);
    gunTable[wn::SelectBegunSlot / 8] = reinterpret_cast<Slot>(Begun);
    gunTable[wn::SelectFinishedSlot / 8] = reinterpret_cast<Slot>(Finished);
    selectTable[wn::ActionReleaseSlot / 8] = reinterpret_cast<Slot>(Release);
    otherTable[wn::ActionReleaseSlot / 8] = reinterpret_cast<Slot>(Release);
    Put(&wrench, wn::WeaponItemId, &kWrenchId, 4);
    Put(&gun, wn::WeaponItemId, &kGunId, 4);
    gInstall = FakeInstall;

    // A refused gate: the hook is never installed, and the lane says why.
    gResolve = [](Natives&) -> DWORD { return ERROR_BAD_EXE_FORMAT; };
    ServiceWrenchDrawFrame();
    Check(installs == 0 && fault.load() && Reports("gate=refused") && Logged("gate result=193", "bytes_mismatch"),
          "mismatched bytes refuse the hook for good");
    ServiceWrenchDrawFrame();
    Check(installs == 0, "a refusal is not retried");
    fault = false;
    gate = "unchecked";

    // Not ready (no PreyDll yet): tried again next frame.
    gResolve = FakeResolve;
    readiness = ERROR_NOT_READY;
    ServiceWrenchDrawFrame();
    Check(installs == 0 && !fault.load() && Reports("gate=unchecked"), "not ready waits");
    readiness = ERROR_SUCCESS;
    ServiceWrenchDrawFrame();
    Check(installs == 1 && installed.load() && Reports("gate=ok") && Reports("hooks=1") && Logged("gate result=0 rows=14+2"),
          "the hook goes in once the gate holds");
    ServiceWrenchDrawFrame();
    Check(installs == 1, "installed once");

    // The wrench's select: not queued, the caller's reference dropped (the last
    // one: released), Enter's part done at once, Exit's a whole frame later.
    auto select = MakeAction(selectTable, kWrenchId, true);
    Play(wrench, select);
    Check(played.empty(), "the wrench's draw is not queued");
    Check(released.size() == 1 && released[0] == reinterpret_cast<std::uintptr_t>(&select), "the last reference is released");
    Check(begun.size() == 1 && begun[0] == reinterpret_cast<std::uintptr_t>(&wrench), "a select marks the wrench busy");
    Check(done.empty() && Reports("selects=1") && Reports("waiting=1") && Logged("skip id=7 kind=select"), "skipped, waiting");
    Check(soundPlays == 0, "no sound inside the skip");
    ServiceWrenchDrawFrame();
    Check(done.empty(), "not finished in the frame it was skipped in");
    Check(soundPlays == 1 && Reports("sounds=1"), "the draw's sound the next frame");
    ServiceWrenchDrawFrame();
    Check(done.size() == 1 && done[0] == reinterpret_cast<std::uintptr_t>(&wrench), "finished a whole frame later");
    Check(soundPlays == 1, "the sound once per draw");
    Check(Reports("finished=1") && Reports("waiting=0") && Logged("finish id=7 result=finished frames=2"), "the finish is counted");
    ServiceWrenchDrawFrame();
    ServiceWrenchDrawFrame();
    Check(done.size() == 1, "finished once");
    Clear();

    // A first pickup (Wpn_Pickup, the flag clear): Enter does not mark it busy.
    auto pickup = MakeAction(selectTable, kWrenchId, false);
    Play(wrench, pickup);
    Check(played.empty() && begun.empty() && released.size() == 1 && Reports("pickups=1") && Logged("kind=pickup"),
          "a first pickup is skipped without Enter's part");
    soundOk = false;
    ServiceWrenchDrawFrame();
    ServiceWrenchDrawFrame();
    Check(done.size() == 1 && Reports("finished=2"), "and finished");
    Check(soundPlays == 2 && Reports("sounds=1"), "a sound that could not play is not counted, and the finish still comes");
    soundOk = true;
    Clear();

    // Someone else still holds a reference: dropped, not released.
    auto held = MakeAction(selectTable, kWrenchId, true, 2);
    Play(wrench, held);
    Check(released.empty() && Get<int>(&held, wn::ActionRefCount) == 1, "only the caller's reference is dropped");
    ServiceWrenchDrawFrame();
    ServiceWrenchDrawFrame();
    Clear();

    // Everything else is Prey's: another weapon, another action, a mismatched id.
    auto gunSelect = MakeAction(selectTable, kGunId, true);
    Play(gun, gunSelect);
    auto fire = MakeAction(otherTable, kWrenchId, true);
    Play(wrench, fire);
    auto stray = MakeAction(selectTable, kGunId, true);
    Play(wrench, stray);
    Check(played.size() == 3 && begun.empty() && released.empty(), "a gun's draw, other actions and a stray id play");
    Clear();

    // Off: the draw plays.
    SetWrenchDrawSkip(0);
    auto off = MakeAction(selectTable, kWrenchId, true);
    Play(wrench, off);
    Check(played.size() == 1 && begun.empty() && Reports("enabled=0") && Logged("settings enabled=0"), "off plays the draw");
    SetWrenchDrawSkip(1);
    Clear();

    // Another thread: the finish could not run there, so the draw plays.
    auto elsewhereAction = MakeAction(selectTable, kWrenchId, true);
    std::thread([&] { Play(wrench, elsewhereAction); }).join();
    Check(played.size() == 1 && begun.empty() && Reports("elsewhere=1"), "off the main thread the draw plays");
    Clear();

    // The wrench is gone before its finish: nothing is called.
    auto lost = MakeAction(selectTable, kWrenchId, true);
    Play(wrench, lost);
    wrenchGone = true;
    ServiceWrenchDrawFrame();
    ServiceWrenchDrawFrame();
    Check(done.empty() && Reports("gone=1") && Logged("result=gone"), "a vanished wrench is not finished");
    wrenchGone = false;
    Clear();

    // A wrench found under the id that is not the wrench any more: not finished either.
    auto changed = MakeAction(selectTable, kWrenchId, true);
    Play(wrench, changed);
    wrench.table = gunTable;
    ServiceWrenchDrawFrame();
    ServiceWrenchDrawFrame();
    Check(done.empty() && Reports("gone=2"), "only the wrench is finished");
    wrench.table = wrenchTable;
    Clear();

    // Four waiting at most; a fifth plays.
    Object queued[5] = {MakeAction(selectTable, kWrenchId, true), MakeAction(selectTable, kWrenchId, true),
                        MakeAction(selectTable, kWrenchId, true), MakeAction(selectTable, kWrenchId, true),
                        MakeAction(selectTable, kWrenchId, true)};
    for (auto& a : queued) { Play(wrench, a); }
    Check(played.size() == 1 && Reports("waiting=4") && Reports("full=1"), "a fifth waiting select plays");
    ServiceWrenchDrawFrame();
    ServiceWrenchDrawFrame();
    Check(done.size() == 4 && Reports("waiting=0"), "every waiting select is finished");
    Clear();

    // Turned off while one waits: it is still finished.
    auto late = MakeAction(selectTable, kWrenchId, true);
    Play(wrench, late);
    SetWrenchDrawSkip(0);
    ServiceWrenchDrawFrame();
    ServiceWrenchDrawFrame();
    Check(done.size() == 1, "a skipped select is finished even after turning the skip off");
    SetWrenchDrawSkip(1);

    Clear();

    // The sound capture: the draw plays and the capture is told, even with the skip off.
    capturing = true;
    auto heard = MakeAction(selectTable, kWrenchId, true);
    Play(wrench, heard);
    SetWrenchDrawSkip(0);
    auto heardOff = MakeAction(selectTable, kWrenchId, false);
    Play(wrench, heardOff);
    SetWrenchDrawSkip(1);
    Check(played.size() == 2 && begun.empty() && seen.size() == 2 && seen[0] == kWrenchId && Reports("captured=2"),
          "while capturing the draw plays and is reported to the capture");
    const unsigned playsBefore = soundPlays;
    ServiceWrenchDrawFrame();
    ServiceWrenchDrawFrame();
    Check(soundPlays == playsBefore, "a draw that played makes its own sound: no replay");
    auto gunHeard = MakeAction(selectTable, kGunId, true);
    Play(gun, gunHeard);
    Check(seen.size() == 2, "only the wrench's draw opens a capture");
    capturing = false;
    Check(soundFrames > 0, "the capture is serviced every frame");

    // The stow: asked to animate, the wrench stows at once and its sound plays.
    const std::uintptr_t fragments = 0x1234;
    Put(&wrench, wn::WeaponFragments, &fragments, 8);
    const auto wrenchAt = reinterpret_cast<std::uintptr_t>(&wrench);
    lines.clear();
    UnequipHook(wrenchAt, true);
    Check(unequips.size() == 1 && unequips[0].weapon == wrenchAt && !unequips[0].animate, "the stow is asked not to animate");
    Check(stowPlays == 1 && Reports("stows=1") && Reports("stowSounds=1") && Logged("stow id=7 sound=1"),
          "and its sound plays, at once");
    // Not asked to animate (an instant stow of Prey's own): passed on as it is, no sound.
    UnequipHook(wrenchAt, false);
    Check(unequips.size() == 2 && !unequips[1].animate && stowPlays == 1 && Reports("stows=1"), "Prey's instant stow is its own");
    // Lowered (EMP, friendly NPCs): Prey's animated stow, which also ends the switch.
    const std::uint8_t lowered = 1, raised = 0;
    Put(&wrench, wn::WeaponLowered, &lowered, 1);
    UnequipHook(wrenchAt, true);
    Check(unequips.size() == 3 && unequips[2].animate && stowPlays == 1 && Reports("stowsLowered=1"),
          "a lowered wrench keeps the animated stow");
    Put(&wrench, wn::WeaponLowered, &raised, 1);
    // No fragment table: nothing to animate, passed on.
    const std::uintptr_t noFragments = 0;
    Put(&wrench, wn::WeaponFragments, &noFragments, 8);
    UnequipHook(wrenchAt, true);
    Check(unequips.size() == 4 && unequips[3].animate && stowPlays == 1, "without fragments it is Prey's");
    Put(&wrench, wn::WeaponFragments, &fragments, 8);
    // Off, capturing, or another thread: Prey's animated stow.
    SetWrenchDrawSkip(0);
    UnequipHook(wrenchAt, true);
    SetWrenchDrawSkip(1);
    capturing = true;
    UnequipHook(wrenchAt, true);
    capturing = false;
    std::thread([&] { UnequipHook(wrenchAt, true); }).join();
    Check(unequips.size() == 7 && unequips[4].animate && unequips[5].animate && unequips[6].animate && stowPlays == 1,
          "off, capturing or off the main thread the stow animates");
    // Another weapon's object under this hook: passed on.
    UnequipHook(reinterpret_cast<std::uintptr_t>(&gun), true);
    Check(unequips.size() == 8 && unequips[7].animate && Reports("stows=1"), "only the wrench is stowed at once");
    // A sound that could not play: the stow still instant, counted without a sound.
    soundOk = false;
    UnequipHook(wrenchAt, true);
    Check(unequips.size() == 9 && !unequips[8].animate && Reports("stows=2") && Reports("stowSounds=1") && Logged("sound=0"),
          "the stow does not wait on its sound");
    soundOk = true;

    Check(!fault.load() && Reports("fault=0"), "no fault");
    std::cout << "wrench_draw: skipped, finished a frame later, everything else Prey's\n";
}
