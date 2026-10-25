#include "StarfieldStereo.h"
#include "EngineAccess.h"
#include "EnvSwitch.h"
#include "HookInstall.h"
#include "Logger.h"
#include "SafeMemory.h"
#include "preyvr/NativeContract.h"
#include "preyvr/StarfieldDepth.h"
#include "preyvr/StarfieldNative.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>
#include <sstream>

namespace preyvr::dll {
namespace {
namespace sn = starfield::native;

void Log(const std::string& line) { lifecycle::Log("preyvr_starfield " + line); }

static_assert(contract::Gated(sn::Contracts, sn::ArkStarsDraw));

using DrawFn = void(__fastcall*)(void* element);
std::atomic<DrawFn> gOriginal{nullptr};
std::atomic<bool> gEnabled{true};
std::mutex gInstallMutex;
bool gInstallTried = false;
std::atomic<bool> gInstalled{false};
std::atomic<const char*> gGate{"unchecked"};
std::atomic<std::uint64_t> gDraws{0}, gFar{0}, gNative{0}, gUnread{0};
std::atomic<bool> gLoggedFirst{false};

// --- the draw (render thread) -------------------------------------------------------
// The element's first bytes, which hold every field the draw reads, and the star
// block they point to. False when either cannot be read or there is no block.
bool ReadElement(const void* element, std::uint8_t (&copy)[sn::ElementSize], starfield::StarBlock& block)
{
    if (!SafeCopy(copy, element, sizeof(copy))) { return false; }
    const void* at = nullptr;
    std::memcpy(&at, copy + sn::ElementStarBlock, sizeof(at));
    return at != nullptr && SafeCopy(block.data(), at, block.size());
}

// The draw reads four fields of its element and hands the element to nothing
// (StarfieldNative.h), so a copy whose block is the far one draws what Prey's
// would, 10 km out instead of 1 m. The copies live on this stack for the call.
void __fastcall ArkStarsDrawDetour(void* element)
{
    const DrawFn original = gOriginal.load(std::memory_order_acquire);
    gDraws.fetch_add(1, std::memory_order_relaxed);
    if (!gEnabled.load(std::memory_order_acquire)) {
        gNative.fetch_add(1, std::memory_order_relaxed);
        original(element);
        return;
    }
    alignas(16) std::uint8_t copy[sn::ElementSize];
    starfield::StarBlock block{};
    if (!ReadElement(element, copy, block)) {
        gUnread.fetch_add(1, std::memory_order_relaxed);
        original(element);
        return;
    }
    const starfield::StarBlock distant = starfield::FarStarBlock(block);
    const std::uint8_t* const farAt = distant.data();
    std::memcpy(copy + sn::ElementStarBlock, &farAt, sizeof(farAt));
    const auto drawn = gFar.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!gLoggedFirst.exchange(true)) {
        Log("result=0 detail=first_far distanceM=" + std::to_string(static_cast<int>(starfield::kStarDistanceMetres)) +
            " draws=" + std::to_string(drawn));
    }
    original(copy);
}

// --- the gate and the hook (main thread) ----------------------------------------
bool GateBody(std::uintptr_t base)
{
    __try {
        return contract::BodiesMatch(base, sn::Contracts);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// The draw's address, once PreyDll is loaded and the draw's body is the one
// read: ERROR_NOT_READY before, ERROR_BAD_EXE_FORMAT for other bytes.
DWORD Resolve(std::uintptr_t& target)
{
    target = 0;
    const auto base = PreyBase();
    if (base == 0) { return ERROR_NOT_READY; }
    if (!GateBody(base)) { return ERROR_BAD_EXE_FORMAT; }
    target = base + sn::ArkStarsDraw;
    return ERROR_SUCCESS;
}
// Tests replace the engine here.
DWORD (*gResolve)(std::uintptr_t&) = Resolve;

bool Install(std::uintptr_t target)
{
    const hook::Result result =
        hook::Install(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&ArkStarsDrawDetour), gOriginal);
    if (!result) {
        Log(std::string("result=failed detail=") + hook::StepName(result.step) +
            " status=" + std::to_string(result.status) + " target=ArkStarsDraw");
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
        Log("result=unavailable detail=bytes_mismatch target=ArkStarsDraw");
        return false;
    }
    if (!gInstall(target)) {
        gGate = "hook_failed";
        return false;
    }
    gGate = "ok";
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=hook_installed target=ArkStarsDraw rows=" + std::to_string(std::size(sn::Contracts)));
    return true;
}

} // namespace

DWORD SetStarfieldDepth(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gEnabled.store(enabled != 0, std::memory_order_release);
    if (enabled != 0 && !InstallHook()) { return ERROR_NOT_SUPPORTED; }
    Log(std::string("result=0 detail=enabled value=") + (enabled ? "1" : "0"));
    return 0;
}

unsigned StarfieldDepthEnabled() { return gEnabled.load(std::memory_order_acquire) ? 1u : 0u; }

void EnsureStarfieldDepth()
{
    // PREYVR_STAR_DEPTH=0: leave the stars untouched, not even hooked.
    if (EnvSwitchOff(L"PREYVR_STAR_DEPTH")) {
        gEnabled.store(false, std::memory_order_release);
        Log("result=0 detail=disabled_by_environment hooks=0");
        return;
    }
    if (gEnabled.load(std::memory_order_acquire)) { InstallHook(); }
}

std::string StarfieldDepthReport()
{
    std::ostringstream s;
    s << "stars={depth=" << StarfieldDepthEnabled() << " hooks=" << (gInstalled.load() ? 1 : 0)
      << " gate=" << gGate.load() << " draws=" << gDraws.load() << " far=" << gFar.load()
      << " native=" << gNative.load() << " unread=" << gUnread.load()
      << " distanceM=" << static_cast<int>(starfield::kStarDistanceMetres) << '}';
    return s.str();
}

} // namespace preyvr::dll
