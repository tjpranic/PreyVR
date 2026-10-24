#include "StickReread.h"

#include "EngineAccess.h"
#include "HookInstall.h"
#include "Logger.h"
#include "MoveLane.h"
#include "SafeMemory.h"
#include "preyvr/Locomotion.h"
#include "preyvr/NativeContract.h"
#include "preyvr/StickRereadNative.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>

namespace preyvr::dll {
namespace {
namespace sn = stick::native;

static_assert(contract::Gated(sn::Contracts, sn::AxisRead) && contract::Gated(sn::Contracts, sn::AxisReread) &&
              contract::Gated(sn::Contracts, sn::ActionMapEvent) &&
              contract::Gated(sn::Contracts, sn::ActionFiltered) && contract::Gated(sn::Contracts, sn::YawHandler) &&
              contract::Gated(sn::Contracts, sn::PlayerSingleton));

void Log(const std::string& line) { lifecycle::Log("preyvr_stick " + line); }

using AxisReadFn = float(__fastcall*)(void* context, const void* action, const char* map);
using ListenerFn = void(__fastcall*)(void* listener, const void* event);
using FilteredFn = bool(__fastcall*)(void* manager, const void* action);
using PlayerFn = std::uintptr_t(__fastcall*)();
using Counter = std::atomic<unsigned long long>;

std::mutex gInstallMutex;
std::atomic<bool> gInstalled{false};
std::atomic<AxisReadFn> gAxisRead{nullptr};
std::atomic<ListenerFn> gListener{nullptr};
std::atomic<std::uintptr_t> gBase{0};
std::atomic<bool> gFix{true};
// ReadStickAxes' own gate: 0 unchecked, 1 matched, 2 refused.
std::atomic<int> gReadGate{0};

// Re-reads (listener calls that read a stick axis), by event. Of those, the ones
// that would have stopped the player -- an axis a lane holds read as 0 -- and
// the ones the lanes' stick was kept for. Axis reads that kept the pad's value
// because the action was filtered, the pad was pushed or the manager could not
// be asked; and reads outside any listener call, which the contract says the
// re-read never makes.
Counter gMapEvents{0}, gFilterEvents{0}, gWouldStop{0}, gKept{0}, gFiltered{0}, gPadPushed{0}, gUnasked{0},
    gUnattributed{0};
// The stops are logged, this many a session; past that they are counted.
constexpr unsigned long long kLogBudget = 60;
Counter gLogged{0}, gUnlogged{0};

// One listener call on this thread: the event, and what its re-read did.
struct EventRead {
    int type = 0;
    std::uint64_t first = 0, second = 0;
    bool read = false;
    unsigned wouldStop = 0;   // axis bits: 1 move X, 2 move Y, 4 yaw
    unsigned kept = 0;
    int held[3] = {};
};
thread_local EventRead* tEvent = nullptr;

// Which stick axis the re-read asks for: 0 move X, 1 move Y, 2 yaw; -1 another.
int AxisOf(std::uintptr_t base, const void* action)
{
    std::uintptr_t table = 0;
    if (!SafeRead(base + sn::ActionTable, table) || table == 0) { return -1; }
    const auto at = reinterpret_cast<std::uintptr_t>(action);
    if (at == table + sn::MoveXAction) { return 0; }
    if (at == table + sn::MoveYAction) { return 1; }
    if (at == table + sn::YawAction) { return 2; }
    return -1;
}

// ActionFiltered for `action`, asked of the manager the read itself uses
// (**context). False when it cannot be asked.
bool AskFiltered(std::uintptr_t base, void* context, const void* action, bool& filtered)
{
    __try {
        void* const manager = **static_cast<void***>(context);
        const auto vtable = *static_cast<const std::uintptr_t*>(manager);
        if (vtable != base + sn::ManagerVtable) { return false; }
        const auto fn = *reinterpret_cast<const FilteredFn*>(vtable + sn::FilteredSlot);
        filtered = fn(manager, action);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

float __fastcall AxisReadDetour(void* context, const void* action, const char* map)
{
    const AxisReadFn original = gAxisRead.load(std::memory_order_acquire);
    const float native = original != nullptr ? original(context, action, map) : 0.0f;
    const std::uintptr_t base = gBase.load(std::memory_order_relaxed);
    const int axis = AxisOf(base, action);
    if (axis < 0) { return native; }
    EventRead* const event = tEvent;
    if (event != nullptr) { event->read = true; }
    else { gUnattributed.fetch_add(1, std::memory_order_relaxed); }
    const int held = MoveLaneHeldMilli(static_cast<unsigned>(axis));
    if (held == 0) { return native; }
    if (native != 0.0f) {
        gPadPushed.fetch_add(1, std::memory_order_relaxed);
        return native;
    }
    bool filtered = false;
    if (!AskFiltered(base, context, action, filtered)) {
        gUnasked.fetch_add(1, std::memory_order_relaxed);
        return native;
    }
    if (filtered) {
        gFiltered.fetch_add(1, std::memory_order_relaxed);
        return native;
    }
    // Stored as read, this zero would stop the player.
    const bool keep = gFix.load(std::memory_order_relaxed);
    if (event != nullptr) {
        event->wouldStop |= 1u << axis;
        event->held[axis] = held;
        if (keep) { event->kept |= 1u << axis; }
    } else {
        gWouldStop.fetch_add(1, std::memory_order_relaxed);
        if (keep) { gKept.fetch_add(1, std::memory_order_relaxed); }
    }
    return keep ? locomotion::RereadValue(native, held, false) : native;
}

// The event's type and its two words; zeros when it cannot be read.
void ReadEvent(const void* event, EventRead& out)
{
    __try {
        const auto* bytes = static_cast<const std::uint8_t*>(event);
        out.type = *reinterpret_cast<const int*>(bytes);
        out.first = *reinterpret_cast<const std::uint64_t*>(bytes + sn::EventFirst);
        out.second = *reinterpret_cast<const std::uint64_t*>(bytes + sn::EventSecond);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out.type = 0;
        out.first = out.second = 0;
    }
}

void LogStop(const EventRead& event)
{
    if (gLogged.fetch_add(1, std::memory_order_relaxed) >= kLogBudget) {
        gUnlogged.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::ostringstream line;
    line << "result=0 detail=reread";
    if (event.type == sn::EventFilter) {
        // The name and the state come in either order (StickRereadNative.h).
        const bool nameFirst = event.first > 1;
        line << " event=filter name=" << SafeText(nameFirst ? event.first : event.second).substr(0, 48)
             << " enable=" << (nameFirst ? event.second : event.first);
    } else if (event.type == sn::EventMap) {
        line << " event=map name=player enable=" << event.second;
    } else {
        line << " event=" << event.type;
    }
    line << " heldMilli=" << event.held[0] << ',' << event.held[1] << ',' << event.held[2]
         << " kept=" << (event.kept != 0 ? 1 : 0);
    Log(line.str());
}

void __fastcall ListenerDetour(void* listener, const void* event)
{
    EventRead read;
    ReadEvent(event, read);
    EventRead* const outer = tEvent;
    tEvent = &read;
    const ListenerFn original = gListener.load(std::memory_order_acquire);
    if (original != nullptr) { original(listener, event); }
    tEvent = outer;
    if (!read.read) { return; }
    (read.type == sn::EventMap ? gMapEvents : gFilterEvents).fetch_add(1, std::memory_order_relaxed);
    if (read.wouldStop == 0) { return; }
    gWouldStop.fetch_add(1, std::memory_order_relaxed);
    if (read.kept != 0) { gKept.fetch_add(1, std::memory_order_relaxed); }
    LogStop(read);
}

bool SlotHolds(std::uintptr_t base, std::uintptr_t vtable, std::size_t slot, std::uintptr_t rva)
{
    std::uintptr_t target = 0;
    return SafeRead(base + vtable + slot, target) && target == base + rva;
}

bool HooksGateMatches(std::uintptr_t base)
{
    __try {
        if (!contract::BodiesMatch(base, sn::Contracts)) { return false; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return SlotHolds(base, sn::ListenerVtable, sn::ListenerSlot, sn::ActionMapEvent) &&
           SlotHolds(base, sn::ManagerVtable, sn::FilteredSlot, sn::ActionFiltered);
}

// What the read-back calls and relies on: the player singleton and the yaw
// handler's store.
bool ReadGateMatches(std::uintptr_t base)
{
    __try {
        for (const auto& row : sn::Contracts) {
            if ((row.rva == sn::PlayerSingleton || row.rva == sn::YawHandler) && !contract::BodyMatches(base, row)) {
                return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadAxes(std::uintptr_t base, StickAxes& out)
{
    __try {
        const std::uintptr_t player = reinterpret_cast<PlayerFn>(base + sn::PlayerSingleton)();
        if (player == 0) { return false; }
        const std::uintptr_t input = player + sn::PlayerInput;
        if (*reinterpret_cast<const std::uintptr_t*>(input) != base + sn::InputVtable) { return false; }
        out.moveX = *reinterpret_cast<const float*>(input + sn::InputMoveX);
        out.moveY = *reinterpret_cast<const float*>(input + sn::InputMoveY);
        out.yaw = *reinterpret_cast<const float*>(input + sn::InputYaw);
        out.gate = *reinterpret_cast<const int*>(input + sn::InputGate);
        out.yawBlocked = *reinterpret_cast<const std::uint8_t*>(input + sn::InputYawBlock) != 0 || out.gate == 2;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

DWORD InstallStickReread()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstalled.load(std::memory_order_acquire)) { return 0; }
    const std::uintptr_t base = PreyBase();
    if (base == 0) { return ERROR_MOD_NOT_FOUND; }
    if (!HooksGateMatches(base)) {
        Log("result=unavailable detail=contract_mismatch fallback=read_back");
        return ERROR_BAD_EXE_FORMAT;
    }
    void* axisRead = nullptr;
    void* listener = nullptr;
    // Both or neither: the listener only names what the read does.
    const hook::Spec hooks[] = {
        {reinterpret_cast<void*>(base + sn::AxisRead), reinterpret_cast<void*>(&AxisReadDetour), &axisRead},
        {reinterpret_cast<void*>(base + sn::ActionMapEvent), reinterpret_cast<void*>(&ListenerDetour), &listener},
    };
    const hook::Result installed = hook::InstallAll(hooks, [&] {
        gBase.store(base, std::memory_order_release);
        gAxisRead.store(reinterpret_cast<AxisReadFn>(axisRead), std::memory_order_release);
        gListener.store(reinterpret_cast<ListenerFn>(listener), std::memory_order_release);
    });
    if (!installed) {
        Log(std::string("result=failed detail=") + hook::StepName(installed.step) +
            " index=" + std::to_string(installed.index) + " fallback=read_back");
        return ERROR_GEN_FAILURE;
    }
    gInstalled.store(true, std::memory_order_release);
    Log(std::string("result=0 detail=hooks_installed axis_read=0x158EB90 listener=0x1590390 fix=") +
        (gFix.load() ? "1" : "0"));
    return 0;
}

DWORD SetStickRereadFix(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gFix.store(enabled != 0, std::memory_order_relaxed);
    Log(std::string("result=0 detail=fix value=") + (enabled != 0 ? "1" : "0"));
    return 0;
}

unsigned StickRereadFix() { return gFix.load(std::memory_order_relaxed) ? 1u : 0u; }

bool ReadStickAxes(StickAxes& out)
{
    const std::uintptr_t base = PreyBase();
    if (base == 0) { return false; }
    int gate = gReadGate.load(std::memory_order_acquire);
    if (gate == 0) {
        gate = ReadGateMatches(base) ? 1 : 2;
        gReadGate.store(gate, std::memory_order_release);
        if (gate == 2) { Log("result=unavailable detail=read_back_contract_mismatch"); }
    }
    return gate == 1 && ReadAxes(base, out);
}

std::string StickRereadReport()
{
    std::ostringstream out;
    out << " reread={hooked=" << (gInstalled.load() ? 1 : 0) << " fix=" << StickRereadFix()
        << " mapEvents=" << gMapEvents.load() << " filterEvents=" << gFilterEvents.load()
        << " wouldStop=" << gWouldStop.load() << " kept=" << gKept.load() << " filtered=" << gFiltered.load()
        << " padPushed=" << gPadPushed.load() << " unasked=" << gUnasked.load()
        << " unattributed=" << gUnattributed.load() << " unlogged=" << gUnlogged.load() << " }";
    return out.str();
}

} // namespace preyvr::dll
