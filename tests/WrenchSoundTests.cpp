// The wrench draw's sound: the production lane with Prey replaced at its ABI
// boundary -- the entity system, the player's entity, its audio proxy and the
// audio system.
#include "../src/dll/WrenchSound.cpp"
#include "DllTestSupport.h"
#include "TestSupport.h"
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

PREYVR_TEST_CAPTURE_LOG()
namespace {
using namespace preyvr::dll;
using preyvr::test::Check;
using preyvr::test::Logged;
bool Reports(const char* field) { return (WrenchSoundReport() + " ").find(std::string(" ") + field + " ") != std::string::npos; }
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
// The player's entity: GetProxy at +0x200.
Slot playerTable[0x50]{};
Object player{playerTable, {}};
// The player's audio proxy: ExecuteTrigger at +0xD8.
Slot proxyTable[0x20]{}, otherProxyTable[0x20]{};
Object audioProxy{proxyTable, {}};
// The entity system (GetEntity at +0x70) and the audio system (GetAudioTriggerID at +0x40).
Slot entitySystemTable[0x10]{}, audioTable[0x10]{};
Object entitySystem{entitySystemTable, {}}, audio{audioTable, {}};
std::uintptr_t entitySystemGlobal = 0, audioGlobal = 0;
bool havePlayer = true, haveProxy = true, knownTriggers = true;
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
} // namespace

int main()
{
    playerTable[sn::GetProxySlot / 8] = reinterpret_cast<Slot>(GetProxy);
    proxyTable[sn::ExecuteTriggerSlot / 8] = reinterpret_cast<Slot>(ProxyExecute);
    otherProxyTable[sn::ExecuteTriggerSlot / 8] = reinterpret_cast<Slot>(ProxyExecute);
    entitySystemTable[sn::GetEntitySlot / 8] = reinterpret_cast<Slot>(GetEntity);
    audioTable[sn::TriggerIdSlot / 8] = reinterpret_cast<Slot>(AudioLookup);
    entitySystemGlobal = reinterpret_cast<std::uintptr_t>(&entitySystem);
    audioGlobal = reinterpret_cast<std::uintptr_t>(&audio);
    gResolve = FakeResolve;
    for (std::size_t i = 0; i < std::size(sn::DrawTriggers); ++i) {
        Check(Crc(sn::DrawTriggers[i]) == sn::DrawTriggerIds[i], "each draw trigger's id is the CRC32 of its lowercased name");
    }

    // On by default, nothing checked yet.
    Check(Reports("gate=unchecked") && WrenchSoundPlay() == 1, "play on, nothing checked yet");

    // Refused bytes: no replay.
    gResolve = [](Natives& n) -> DWORD { n = {}; return ERROR_BAD_EXE_FORMAT; };
    Check(!WrenchSoundPlayDraw() && replayed.empty() && Reports("playFailed=1") && Reports("gate=refused") &&
              Logged("gate result=193") && Logged("play what=draw result=fault triggers=0 gate=refused"),
          "mismatched bytes refuse the replay");
    fault = false;
    gate = "unchecked";
    gResolve = FakeResolve;

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

    std::cout << "wrench_sound: the draw's and the stow's sounds replay on the player\n";
}
