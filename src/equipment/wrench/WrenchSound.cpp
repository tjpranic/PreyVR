#include "equipment/wrench/WrenchSound.h"
#include "engine/EngineAccess.h"
#include "base/Logger.h"
#include "engine/NativeContract.h"
#include "equipment/wrench/WrenchSoundNative.h"
#include <atomic>
#include <cstdint>
#include <iterator>

namespace preyvr::dll {
namespace {
namespace sn = wrenchsound::native;

void Log(const std::string& line) { lifecycle::Log("preyvr_wrench_sound " + line); }

// Every function the replay calls through a slot is one the gate checks.
static_assert(contract::Gated(sn::Contracts, sn::GetAudioTriggerId) && contract::Gated(sn::Contracts, sn::ProxyExecuteTrigger));

std::atomic<bool> fault{false};
std::atomic<const char*> gate{"unchecked"}, audioSystem{"unread"};

using TriggerIdFn = bool(__fastcall*)(std::uintptr_t, const char*, std::uint32_t*);
using ExecuteFn = bool(__fastcall*)(std::uintptr_t, std::uint32_t, int, std::uint32_t, const void*);

// --- the gate (main thread) --------------------------------------------------------
std::uintptr_t SlotTarget(std::uintptr_t base, std::uintptr_t table, std::size_t slot)
{
    return *reinterpret_cast<const std::uintptr_t*>(base + table + slot) - base;
}
bool GateBodies(std::uintptr_t base)
{
    __try {
        return contract::BodiesMatch(base, sn::Contracts) &&
               SlotTarget(base, sn::AudioSystemVtable, sn::TriggerIdSlot) == sn::GetAudioTriggerId &&
               SlotTarget(base, sn::EntityAudioProxyVtable, sn::ExecuteTriggerSlot) == sn::ProxyExecuteTrigger;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// Whether the live audio system is the CAudioSystem whose lookup the gate checked.
const char* AudioSystemKind(std::uintptr_t global, std::uintptr_t vtable)
{
    __try {
        const auto system = *reinterpret_cast<const std::uintptr_t*>(global);
        if (!system) { return "none"; }
        return *reinterpret_cast<const std::uintptr_t*>(system) == vtable ? "caudiosystem" : "other";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "fault";
    }
}
// Where the replay reads and what it compares, resolved once the gate holds. Tests
// put fakes here.
struct Natives {
    std::uintptr_t base = 0;
    std::uintptr_t entitySystem = 0, audioSystem = 0;   // the globals holding them
    std::uintptr_t audioSystemVtable = 0, proxyVtable = 0;
};
Natives natives{};
DWORD Resolve(Natives& n)
{
    n = {};
    const auto base = PreyBase();
    if (!base) { return ERROR_NOT_READY; }
    if (!GateBodies(base)) { return ERROR_BAD_EXE_FORMAT; }
    n.base = base;
    n.entitySystem = base + sn::EntitySystem;
    n.audioSystem = base + sn::AudioSystemGlobal;
    n.audioSystemVtable = base + sn::AudioSystemVtable;
    n.proxyVtable = base + sn::EntityAudioProxyVtable;
    return ERROR_SUCCESS;
}
// Tests replace the engine here.
DWORD (*gResolve)(Natives&) = Resolve;
std::atomic<bool> gated{false};
// True once the gate holds; refused for good on a mismatch.
bool Gate()
{
    if (gated.load()) { return true; }
    if (fault.load()) { return false; }
    Natives n{};
    const DWORD ready = gResolve(n);
    if (ready == ERROR_NOT_READY) { return false; }
    if (ready) {
        fault = true;
        gate = "refused";
        Log("gate result=" + std::to_string(ready) + " detail=bytes_mismatch");
        return false;
    }
    natives = n;
    audioSystem = AudioSystemKind(n.audioSystem, n.audioSystemVtable);
    gate = "ok";
    gated = true;
    Log("gate result=0 rows=" + std::to_string(std::size(sn::Contracts)) + " audioSystem=" + audioSystem.load());
    return true;
}

// --- the replay (main thread) -------------------------------------------------------
std::atomic<bool> playOn{true};
std::atomic<std::uint64_t> played{0}, playFailed{0};
std::atomic<const char*> lastPlay{"none"};
using GetEntityFn = std::uintptr_t(__fastcall*)(std::uintptr_t, std::uint32_t);
using GetProxyFn = std::uintptr_t(__fastcall*)(std::uintptr_t, int);
// SAudioCallBackInfos as the PlaySound clip passes it: nothing to notify, normal priority.
struct CallbackInfo { void* object; void* userData; void* owner; std::uint32_t flags; };
const CallbackInfo kNoCallback{nullptr, nullptr, nullptr, sn::CallbackFlags};
// A set of triggers to play, and each one's id once the audio system has confirmed it
// (the trigger exists and its id is the captured one).
struct Sound {
    const char* what;
    const char* const* names;
    const std::uint32_t* ids;
    std::uint32_t* cache;
    std::size_t count;
};
std::uint32_t drawIds[std::size(sn::DrawTriggers)]{}, stowIds[std::size(sn::StowTriggers)]{};
const Sound kDraw{"draw", sn::DrawTriggers, sn::DrawTriggerIds, drawIds, std::size(sn::DrawTriggers)};
const Sound kStow{"stow", sn::StowTriggers, sn::StowTriggerIds, stowIds, std::size(sn::StowTriggers)};
enum class Play : std::uint8_t { played, noEntity, noProxy, unknownTrigger, fault };
const char* PlayName(Play p)
{
    switch (p) {
    case Play::played: return "played";
    case Play::noEntity: return "no_entity";
    case Play::noProxy: return "no_proxy";
    case Play::unknownTrigger: return "unknown_trigger";
    case Play::fault: return "fault";
    }
    return "none";
}
template <class Fn> Fn Virtual(std::uintptr_t object, std::size_t slot)
{
    return reinterpret_cast<Fn>((*reinterpret_cast<const std::uintptr_t* const*>(object))[slot / sizeof(void*)]);
}
// The player's entity (the entity system's GetEntity), its audio proxy (GetProxy,
// which must be a CEntityAudioProxy: its ExecuteTrigger is the gated body), then
// each trigger on the proxy's default local id, as the clips played them.
Play PlayTriggers(const Natives& n, const Sound& s, unsigned& count)
{
    __try {
        const auto system = *reinterpret_cast<const std::uintptr_t*>(n.entitySystem);
        const auto entity = system ? Virtual<GetEntityFn>(system, sn::GetEntitySlot)(system, sn::PlayerEntityId) : 0;
        if (!entity) { return Play::noEntity; }
        const auto proxy = Virtual<GetProxyFn>(entity, sn::GetProxySlot)(entity, sn::AudioProxyType);
        if (!proxy || *reinterpret_cast<const std::uintptr_t*>(proxy) != n.proxyVtable) { return Play::noProxy; }
        const auto audio = *reinterpret_cast<const std::uintptr_t*>(n.audioSystem);
        for (std::size_t i = 0; i < s.count; ++i) {
            if (!s.cache[i]) {
                std::uint32_t id = 0;
                if (!audio || *reinterpret_cast<const std::uintptr_t*>(audio) != n.audioSystemVtable ||
                    !Virtual<TriggerIdFn>(audio, sn::TriggerIdSlot)(audio, s.names[i], &id) || id != s.ids[i]) {
                    return Play::unknownTrigger;
                }
                s.cache[i] = id;
            }
            Virtual<ExecuteFn>(proxy, sn::ExecuteTriggerSlot)(proxy, s.cache[i], 0, sn::DefaultProxyId, &kNoCallback);
            ++count;
        }
        return Play::played;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return Play::fault;
    }
}
bool PlayTriggerSet(const Sound& s)
{
    if (!playOn.load()) { return false; }
    unsigned count = 0;
    const Play result = Gate() ? PlayTriggers(natives, s, count) : Play::fault;
    lastPlay = PlayName(result);
    (result == Play::played ? played : playFailed).fetch_add(1);
    Log(std::string("play what=") + s.what + " result=" + PlayName(result) + " triggers=" + std::to_string(count) +
        (gated.load() ? std::string() : " gate=" + std::string(gate.load())));
    return result == Play::played;
}
} // namespace

void SetWrenchSoundPlay(unsigned on)
{
    playOn = on != 0;
    Log("settings play=" + std::to_string(playOn.load()));
}
unsigned WrenchSoundPlay() { return playOn.load() ? 1u : 0u; }
bool WrenchSoundPlayDraw() { return PlayTriggerSet(kDraw); }
bool WrenchSoundPlayStow() { return PlayTriggerSet(kStow); }
std::string WrenchSoundReport()
{
    return " play=" + std::to_string(playOn.load()) + " played=" + std::to_string(played.load()) + " playFailed=" +
           std::to_string(playFailed.load()) + " lastPlay=" + lastPlay.load() + " gate=" + gate.load() + " fault=" +
           std::to_string(fault.load()) + " audioSystem=" + audioSystem.load();
}
} // namespace preyvr::dll
