#include "engine/fixes/TimeStopAnimation.h"
#include "body/ik/AnimIkTakeover.h"
#include "base/Logger.h"
#include "engine/EngineAccess.h"
#include "engine/EngineMap.h"
#include "engine/NativeContract.h"
#include "engine/NativeGate.h"
#include "engine/SafeMemory.h"
#include "engine/hooks/HookInstall.h"
#include "engine/fixes/TimeStopAnimationNative.h"

#include <atomic>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>

namespace preyvr::dll {
namespace {
namespace tn = timestop::native;

void Log(const std::string& line) { lifecycle::Log("preyvr_time_stop " + line); }

static_assert(contract::Gated(tn::Contracts, tn::PrePhysicsUpdate));
static_assert(contract::Gated(tn::Contracts, tn::StartAnimation));
static_assert(contract::Gated(tn::Contracts, engine::ArkPlayerLayout::getInstanceRva));

using PrePhysicsFn = void(__fastcall*)(void* animated);
std::atomic<PrePhysicsFn> gOriginal{nullptr};
std::uintptr_t gBase = 0;   // PreyDll's, once the gate passed
std::atomic<bool> gActive{false};
std::mutex gInstallMutex;
bool gInstallTried = false;
std::atomic<bool> gInstalled{false};
std::atomic<const char*> gGate{"unchecked"};

std::atomic<std::uint64_t> gStops{0}, gStoppedFrames{0}, gStarted{0}, gDeclined{0}, gOnTransform{0}, gOthers{0},
    gUnread{0}, gFaulted{0};

// The stop in progress. Main thread only: PrePhysicsUpdate runs there.
struct Stop {
    void* animated = nullptr;
    std::uint64_t frames = 0, started = 0, declined = 0, onTransform = 0;
    unsigned long long matchedBefore = 0, beganMs = 0;
};
Stop gStop;

// --- the engine (tests replace it here) ----------------------------------------
enum class Start { started, declined, faulted };

// Prey's own start. Declined: already started this frame, no character, or
// Prey's own flag (+0x847) off.
Start StartAnimation(void* animated, const void* location)
{
    using StartFn = bool(__fastcall*)(void* animated, const void* location);
    __try {
        return reinterpret_cast<StartFn>(gBase + tn::StartAnimation)(animated, location) ? Start::started
                                                                                         : Start::declined;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return Start::faulted;
    }
}

std::uintptr_t (*gPlayer)(std::uintptr_t) = PlayerInstance;
Start (*gStart)(void*, const void*) = StartAnimation;

// --- the stop ------------------------------------------------------------------------
void EndStop(const char* reason)
{
    std::ostringstream line;
    line << "detail=stop_end reason=" << reason << " ms=" << GetTickCount64() - gStop.beganMs
         << " frames=" << gStop.frames << " started=" << gStop.started << " declined=" << gStop.declined
         << " onTransform=" << gStop.onTransform << " ikMatched=" << AnimIkMatched() - gStop.matchedBefore;
    Log(line.str());
    gStop = {};
}

void BeginStop(void* animated)
{
    if (gStop.animated != nullptr) { EndStop("new_character"); }
    gStop.animated = animated;
    gStop.matchedBefore = AnimIkMatched();
    gStop.beganMs = GetTickCount64();
    gStops.fetch_add(1, std::memory_order_relaxed);
    Log("detail=stop_begin");
}

// A frame PrePhysicsUpdate returned before starting the animation of: the
// player's is started here, as the frame would have started it.
void StartStopped(void* animated)
{
    const auto at = reinterpret_cast<std::uintptr_t>(animated);
    const std::uintptr_t player = gPlayer(gBase);
    std::uintptr_t own = 0;
    if (player == 0 || !SafeRead(player + engine::ArkPlayerLayout::animatedCharacter, own) || own != at) {
        gOthers.fetch_add(1, std::memory_order_relaxed);   // not the player's: Prey's behaviour
        return;
    }
    std::uint8_t onTransform = 0;
    if (!SafeRead(at + tn::StartsOnTransform, onTransform)) {
        gUnread.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (gStop.animated != animated) { BeginStop(animated); }
    ++gStop.frames;
    gStoppedFrames.fetch_add(1, std::memory_order_relaxed);
    // Started from the transform event instead: not PrePhysicsUpdate's to start.
    if (onTransform != 0) {
        ++gStop.onTransform;
        gOnTransform.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    switch (gStart(animated, reinterpret_cast<const void*>(at + tn::Location))) {
    case Start::started:
        ++gStop.started;
        gStarted.fetch_add(1, std::memory_order_relaxed);
        break;
    case Start::declined:
        ++gStop.declined;
        gDeclined.fetch_add(1, std::memory_order_relaxed);
        break;
    case Start::faulted:
        gFaulted.fetch_add(1, std::memory_order_relaxed);
        break;
    }
}

void __fastcall PrePhysicsUpdateDetour(void* animated)
{
    // The original's own test, before it runs (comisd 0,[+0x1D0]; jnc): 0 or
    // less skips the start; a NaN does not.
    double frameTime = 0.0;
    const bool skipped =
        SafeRead(reinterpret_cast<std::uintptr_t>(animated) + tn::FrameTime, frameTime) && frameTime <= 0.0;
    gOriginal.load(std::memory_order_acquire)(animated);
    const bool active = gActive.load(std::memory_order_acquire);
    if (animated == gStop.animated && (!skipped || !active)) { EndStop(active ? "clock" : "vr_off"); }
    if (skipped && active) { StartStopped(animated); }
}

// --- the gate and the hook (main thread) ----------------------------------------
bool GateBodies(std::uintptr_t base) { return BodiesMatchGuarded(base, tn::Contracts); }
// Tests replace the engine here.
DWORD (*gResolve)(std::uintptr_t&) = ResolveBase<GateBodies>;

bool Install(std::uintptr_t target)
{
    return hook::InstallLogged(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&PrePhysicsUpdateDetour),
                               gOriginal, "AnimatedCharacterPrePhysicsUpdate", &Log);
}
// Tests replace the installer here.
bool (*gInstall)(std::uintptr_t) = Install;

// Once: a refused gate or a failed hook stays refused. Before PreyDll is loaded
// it is not yet tried.
bool InstallHook()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstallTried) { return gInstalled.load(std::memory_order_acquire); }
    std::uintptr_t base = 0;
    const DWORD ready = gResolve(base);
    if (ready == ERROR_NOT_READY) {
        Log("result=unavailable detail=no_preydll");
        return false;
    }
    gInstallTried = true;
    if (ready != ERROR_SUCCESS) {
        gGate = "refused";
        Log("result=unavailable detail=bytes_mismatch target=AnimatedCharacterPrePhysicsUpdate");
        return false;
    }
    gBase = base;
    if (!gInstall(base + tn::PrePhysicsUpdate)) {
        gGate = "hook_failed";
        return false;
    }
    gGate = "ok";
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=hook_installed target=AnimatedCharacterPrePhysicsUpdate rows=" +
        std::to_string(std::size(tn::Contracts)));
    return true;
}

} // namespace

void EnsureTimeStopAnimation() { gActive.store(InstallHook(), std::memory_order_release); }

void StopTimeStopAnimation() { gActive.store(false, std::memory_order_release); }

std::string TimeStopAnimationReport()
{
    std::ostringstream s;
    s << "timeStop={active=" << (gActive.load() ? 1 : 0) << " hooks=" << (gInstalled.load() ? 1 : 0)
      << " gate=" << gGate.load() << " stops=" << gStops.load() << " stoppedFrames=" << gStoppedFrames.load()
      << " started=" << gStarted.load() << " declined=" << gDeclined.load() << " onTransform=" << gOnTransform.load()
      << " others=" << gOthers.load() << " unread=" << gUnread.load() << " faulted=" << gFaulted.load() << "}";
    return s.str();
}

} // namespace preyvr::dll
