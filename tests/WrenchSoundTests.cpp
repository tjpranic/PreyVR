// The wrench draw's sound: the production lane with Prey replaced at its ABI
// boundary -- the PlaySound clip's OnEnter, the trigger lookup and the entity audio
// proxy's ExecuteTrigger (their originals, for the capture), and the entity system,
// the player's entity, its audio proxy and the audio system (for the replay).
#include "../src/dll/WrenchSound.cpp"
#include "TestSupport.h"
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

std::vector<std::string> lines;
namespace preyvr::lifecycle { void Log(std::string_view line) { lines.emplace_back(line); } }
namespace preyvr::dll { bool EnsureMinHook() { return false; } }
namespace {
using namespace preyvr::dll;
using preyvr::test::Check;
bool Logged(const char* a, const char* b = "", const char* c = "")
{
    for (const auto& l : lines) {
        if (l.find(a) != std::string::npos && l.find(b) != std::string::npos && l.find(c) != std::string::npos) { return true; }
    }
    return false;
}
unsigned Count(const char* a)
{
    unsigned n = 0;
    for (const auto& l : lines) { n += l.find(a) != std::string::npos; }
    return n;
}
bool Reports(const char* field) { return (WrenchSoundReport() + " ").find(std::string(" ") + field + " ") != std::string::npos; }
void Put(void* base, std::size_t offset, const void* value, std::size_t size)
{
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, value, size);
}
std::uint32_t Crc(const char* name)   // the ATL's id: ~CRC32 of the lowercased name
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (; *name; ++name) {
        crc ^= static_cast<std::uint8_t>(*name >= 'A' && *name <= 'Z' ? *name + 32 : *name);
        for (int k = 0; k < 8; ++k) { crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1; }
    }
    return ~crc;
}

using Slot = void*;
struct Object { Slot* table; unsigned char bytes[0x100]; };
// Entities: GetId at +0x08, GetProxy at +0x200.
Slot playerTable[0x50]{}, wrenchTable[4]{};
Object player{playerTable, {}}, wrenchEntity{wrenchTable, {}};
// The player's audio proxy: ExecuteTrigger at +0xD8; its entity at +0x40.
Slot proxyTable[0x20]{}, otherProxyTable[0x20]{};
Object audioProxy{proxyTable, {}};
// The entity system (GetEntity at +0x70) and the audio system (GetAudioTriggerID at +0x40).
Slot entitySystemTable[0x10]{}, audioTable[0x10]{};
Object entitySystem{entitySystemTable, {}}, audio{audioTable, {}};
std::uintptr_t entitySystemGlobal = 0, audioGlobal = 0;
bool havePlayer = true, haveProxy = true, knownTriggers = true;
std::uint32_t __fastcall PlayerId(std::uintptr_t) { return 0x7777; }
std::uint32_t __fastcall WrenchId(std::uintptr_t) { return 7; }
std::uintptr_t __fastcall GetEntity(std::uintptr_t, std::uint32_t id)
{
    return havePlayer && id == 0x7777 ? reinterpret_cast<std::uintptr_t>(&player) : 0;
}
std::uintptr_t __fastcall GetProxy(std::uintptr_t, int type)
{
    return haveProxy && type == 3 ? reinterpret_cast<std::uintptr_t>(&audioProxy) : 0;
}
struct Played { std::uint32_t id; int lipSync; std::uint32_t local; std::uint32_t flags; };
std::vector<Played> replayed;
unsigned lookups = 0;
bool __fastcall ProxyExecute(std::uintptr_t, std::uint32_t id, int lipSync, std::uint32_t local, const void* callbacks)
{
    std::uint32_t flags = 0;
    std::memcpy(&flags, static_cast<const std::uint8_t*>(callbacks) + 0x18, 4);
    replayed.push_back({id, lipSync, local, flags});
    return true;
}
bool __fastcall AudioLookup(std::uintptr_t, const char* name, std::uint32_t* id)
{
    ++lookups;
    *id = knownTriggers ? Crc(name) : 0;
    return *id != 0;
}

// The capture: a PlaySound clip (its entity at +0x08) and its params; the hooks' originals.
Object clip{nullptr, {}}, params{nullptr, {}};
const char* kStart = "Play_Wrench_Draw";
unsigned clipCalls = 0, lookupCalls = 0, executeCalls = 0;
float lastBlend = 0, lastDuration = 0;
void __fastcall ClipOriginal(std::uintptr_t, float blend, float duration, std::uintptr_t)
{
    ++clipCalls;
    lastBlend = blend;
    lastDuration = duration;
}
bool __fastcall LookupOriginal(std::uintptr_t, const char* name, std::uint32_t* id)
{
    ++lookupCalls;
    *id = std::strcmp(name, kStart) == 0 ? 0xC0FFEEu : 0u;
    return *id != 0;
}
bool __fastcall ExecuteOriginal(std::uintptr_t, std::uint32_t, int, std::uint32_t, const void*)
{
    ++executeCalls;
    return true;
}
unsigned installs = 0;
DWORD readiness = ERROR_SUCCESS;
DWORD FakeResolve(Natives& n)
{
    n = {};
    if (readiness) { return readiness; }
    n.base = 1;
    n.entitySystem = reinterpret_cast<std::uintptr_t>(&entitySystemGlobal);
    n.audioSystem = reinterpret_cast<std::uintptr_t>(&audioGlobal);
    n.audioSystemVtable = reinterpret_cast<std::uintptr_t>(audioTable);
    n.proxyVtable = reinterpret_cast<std::uintptr_t>(proxyTable);
    return ERROR_SUCCESS;
}
bool FakeInstall(const Natives&)
{
    ++installs;
    originalClipEnter = ClipOriginal;
    originalTriggerId = LookupOriginal;
    originalExecute = ExecuteOriginal;
    return true;
}
void Sounds()
{
    ClipEnterHook(reinterpret_cast<std::uintptr_t>(&clip), 0.1f, 0.75f, reinterpret_cast<std::uintptr_t>(&params));
    std::uint32_t id = 0;
    TriggerIdHook(0x1234, kStart, &id);
    ExecuteHook(reinterpret_cast<std::uintptr_t>(&audioProxy), id, 0, 1, nullptr);
}
} // namespace

int main()
{
    playerTable[1] = reinterpret_cast<Slot>(PlayerId);
    playerTable[sn::GetProxySlot / 8] = reinterpret_cast<Slot>(GetProxy);
    wrenchTable[1] = reinterpret_cast<Slot>(WrenchId);
    proxyTable[sn::ExecuteTriggerSlot / 8] = reinterpret_cast<Slot>(ProxyExecute);
    otherProxyTable[sn::ExecuteTriggerSlot / 8] = reinterpret_cast<Slot>(ProxyExecute);
    entitySystemTable[sn::GetEntitySlot / 8] = reinterpret_cast<Slot>(GetEntity);
    audioTable[sn::TriggerIdSlot / 8] = reinterpret_cast<Slot>(AudioLookup);
    entitySystemGlobal = reinterpret_cast<std::uintptr_t>(&entitySystem);
    audioGlobal = reinterpret_cast<std::uintptr_t>(&audio);
    const auto playerAt = reinterpret_cast<std::uintptr_t>(&player);
    const auto wrenchAt = reinterpret_cast<std::uintptr_t>(&wrenchEntity);
    Put(&clip, sn::ClipEntity, &playerAt, 8);
    Put(&params, sn::StartTrigger, &kStart, 8);
    const std::uint32_t joint = 0xABCD, flags = 1;
    const std::uint8_t fire = 1;
    Put(&params, sn::AttachmentJoint, &joint, 4);
    Put(&params, sn::SoundFlags, &flags, 4);
    Put(&params, sn::FireAndForget, &fire, 1);
    Put(&audioProxy, sn::ProxyEntity, &wrenchAt, 8);
    gResolve = FakeResolve;
    gInstall = FakeInstall;
    for (std::size_t i = 0; i < std::size(sn::DrawTriggers); ++i) {
        Check(Crc(sn::DrawTriggers[i]) == sn::DrawTriggerIds[i], "each draw trigger's id is the CRC32 of its lowercased name");
    }

    // Off by default: no hooks, no window, no gate yet.
    ServiceWrenchSoundFrame();
    WrenchSoundSelectSeen(7, true);
    Check(installs == 0 && !WrenchSoundCapturing() && Reports("capture=0") && Reports("windows=0") &&
          Reports("gate=unchecked") && WrenchSoundPlay() == 1, "capture off, play on, nothing checked yet");

    // Refused bytes: no hooks and no replay.
    gResolve = [](Natives& n) -> DWORD { n = {}; return ERROR_BAD_EXE_FORMAT; };
    SetWrenchSoundCapture(1);
    ServiceWrenchSoundFrame();
    Check(installs == 0 && Reports("gate=refused") && Logged("gate result=193"), "mismatched bytes refuse the capture");
    Check(!WrenchSoundPlayDraw() && replayed.empty() && Reports("playFailed=1") && Logged("play what=draw result=fault triggers=0 gate=refused"),
          "and the replay");
    fault = false;
    gate = "unchecked";
    gResolve = FakeResolve;
    SetWrenchSoundCapture(0);

    // The replay: the player's audio proxy plays both draw triggers, as the clips did.
    Check(WrenchSoundPlayDraw(), "the draw's sound plays");
    Check(Logged("gate result=0 rows=4 audioSystem=caudiosystem") && Reports("gate=ok"), "the gate holds");
    Check(replayed.size() == 2 && replayed[0].id == 0x446C5BFDu && replayed[1].id == 0x364DA6FEu,
          "Play_Player_Arm_Wrench, then Play_switch_weapon");
    Check(replayed[0].lipSync == 0 && replayed[0].local == 1 && replayed[0].flags == 1, "default proxy, no lip sync, normal priority");
    Check(lookups == 2 && Reports("played=1") && Reports("lastPlay=played") && Logged("play what=draw result=played triggers=2"),
          "looked up once each, counted");
    WrenchSoundPlayDraw();
    Check(replayed.size() == 4 && lookups == 2, "the ids are kept");
    replayed.clear();

    // What stops it: no player, a proxy that is not the entity audio proxy, no proxy.
    havePlayer = false;
    Check(!WrenchSoundPlayDraw() && Reports("lastPlay=no_entity"), "no player entity, no sound");
    havePlayer = true;
    audioProxy.table = otherProxyTable;
    Check(!WrenchSoundPlayDraw() && Reports("lastPlay=no_proxy"), "an unknown proxy class is not called");
    audioProxy.table = proxyTable;
    haveProxy = false;
    Check(!WrenchSoundPlayDraw() && Reports("lastPlay=no_proxy") && replayed.empty(), "no audio proxy, no sound");
    haveProxy = true;
    // An unknown trigger (a later lookup, the ids not yet kept): nothing plays.
    drawIds[0] = drawIds[1] = 0;
    knownTriggers = false;
    Check(!WrenchSoundPlayDraw() && Reports("lastPlay=unknown_trigger") && replayed.empty(), "an unknown trigger plays nothing");
    knownTriggers = true;
    SetWrenchSoundPlay(0);
    Check(!WrenchSoundPlayDraw() && replayed.empty() && Reports("play=0") && Logged("settings play=0"), "off plays nothing");
    SetWrenchSoundPlay(1);
    Check(WrenchSoundPlayDraw() && replayed.size() == 2, "on again");
    replayed.clear();

    // The stow's sound: one trigger, the same way.
    for (std::size_t i = 0; i < std::size(sn::StowTriggers); ++i) {
        Check(Crc(sn::StowTriggers[i]) == sn::StowTriggerIds[i], "each stow trigger's id is the CRC32 of its lowercased name");
    }
    Check(WrenchSoundPlayStow() && replayed.size() == 1 && replayed[0].id == 0xE06723D1u && replayed[0].local == 1,
          "Play_Player_Unequip on the player");
    Check(Logged("play what=stow result=played triggers=1") && Logged("play what=draw result=played triggers=2"),
          "each play says what it was");
    SetWrenchSoundPlay(0);
    Check(!WrenchSoundPlayStow() && replayed.size() == 1, "off plays no stow sound either");
    SetWrenchSoundPlay(1);
    replayed.clear();

    // The capture: hooked once the gate holds.
    readiness = ERROR_NOT_READY;   // the gate already holds: readiness no longer matters
    SetWrenchSoundCapture(1);
    ServiceWrenchSoundFrame();
    ServiceWrenchSoundFrame();
    Check(installs == 1 && WrenchSoundCapturing() && Reports("hooks=1"), "the capture's hooks go in once");
    readiness = ERROR_SUCCESS;

    // Before a draw: everything passes through unlogged.
    lines.clear();
    Sounds();
    Check(clipCalls == 1 && lookupCalls == 1 && executeCalls == 1 && lines.empty(), "no window, no lines, Prey's calls made");

    // A draw opens a window: the clip, the lookup and the execute are each logged.
    WrenchSoundSelectSeen(7, true);
    Check(Logged("select item=7 kind=select window=1"), "the draw opens a window");
    Sounds();
    Check(clipCalls == 2 && lastBlend == 0.1f && lastDuration == 0.75f, "the clip's original gets its arguments unchanged");
    Check(Logged("clip tMs=", "entity=player start=Play_Wrench_Draw stop=- fireAndForget=1 soundFlags=1 joint=0xabcd",
                 "blendMs=100 durationMs=750"),
          "the clip line names its triggers, flags and entity");
    Check(Logged("resolve tMs=", "name=Play_Wrench_Draw id=0xc0ffee found=1"), "the lookup line pairs the name and id");
    Check(Logged("execute tMs=", "id=0xc0ffee entity=wrench proxy=1 lipSync=0"), "the execute line names the entity");
    Check(Reports("clips=1") && Reports("resolves=1") && Reports("executes=1"), "counted");

    // An unknown name: logged as not found.
    std::uint32_t none = 5;
    TriggerIdHook(0x1234, "Play_Nothing", &none);
    Check(Logged("name=Play_Nothing id=0x0 found=0"), "an unknown trigger is logged with id 0");

    // A window holds 200 lines; the rest are dropped and counted.
    for (unsigned i = 0; i < 80; ++i) { Sounds(); }
    Check(Count("clip tMs=") + Count("resolve tMs=") + Count("execute tMs=") == 200 && !Reports("dropped=0"),
          "a window logs at most 200 lines");
    const unsigned calls = clipCalls;
    Sounds();
    Check(clipCalls == calls + 1, "a full window still calls Prey");

    // The window ends: one line, and nothing more is logged.
    windowEnd = 1;
    ServiceWrenchSoundFrame();
    Check(Logged("window end item=7 lines=200"), "the window's end is logged");
    const auto before = lines.size();
    Sounds();
    Check(lines.size() == before, "nothing is logged after the window");

    // A pickup opens a new window, its lines counted afresh.
    WrenchSoundSelectSeen(7, false);
    Sounds();
    Check(Logged("kind=pickup window=2") && lines.size() == before + 4, "a first pickup opens its own window");

    // Turned off: no new windows.
    SetWrenchSoundCapture(0);
    WrenchSoundSelectSeen(7, true);
    Check(!WrenchSoundCapturing() && Reports("windows=2") && Logged("settings capture=0"), "off opens no window");
    std::cout << "wrench_sound: the draw's sound replays on the player; the capture names what plays\n";
}
