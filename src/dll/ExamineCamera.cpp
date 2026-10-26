#include "ExamineCamera.h"
#include "EngineAccess.h"
#include "EnvSwitch.h"
#include "HookInstall.h"
#include "Logger.h"
#include "SafeMemory.h"
#include "preyvr/EngineMap.h"
#include "preyvr/ExamineNative.h"
#include "preyvr/ExaminePullback.h"
#include "preyvr/NativeContract.h"

#include <intrin.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <mutex>
#include <sstream>

#pragma intrinsic(_ReturnAddress)

namespace preyvr::dll {
namespace {
namespace en = examine::native;

void Log(const std::string& line) { lifecycle::Log("preyvr_examine " + line); }

static_assert(contract::Gated(en::Contracts, en::FitView));

// RAX whole: a world UI's fit sets AL to 1, and the callers test AL alone.
using FitFn = std::uintptr_t(__fastcall*)(void* examination, float* target, float* facing, void* entity);
std::atomic<FitFn> gOriginal{nullptr};
std::uintptr_t gBase = 0;   // PreyDll's, once hooked: the callers' return addresses are from it
std::atomic<unsigned> gPullbackCm{examine::kDefaultCm};
std::atomic<bool> gActive{false};
std::mutex gInstallMutex;
bool gInstallTried = false;
std::atomic<bool> gInstalled{false};
// The three bodies were found the ones read, before anything hooked the fit:
// the examination's layout ExaminingScreen reads is known.
std::atomic<bool> gLayout{false};
std::atomic<const char*> gGate{"unchecked"};
std::atomic<std::uint64_t> gFits{0}, gBegins{0}, gMoved{0}, gCapped{0}, gNoEye{0}, gOtherCaller{0}, gUnread{0},
    gUnwritten{0};
// The eye's depth behind the fit point when the examination of this entity
// began (examine::EyeDepth). Main thread only writes them.
std::atomic<std::uint32_t> gEyeEntity{0};
std::atomic<float> gEyeDepth{std::numeric_limits<float>::quiet_NaN()};

// --- reads and the one write (main thread) ----------------------------------------
// Tests replace the engine here.
const std::uint8_t* (*gViewCamera)() = LiveViewCamera;

bool WriteTarget(float* target, const Vec3& position)
{
    __try {
        std::memcpy(target, &position, sizeof(position));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The examination begins: the view camera is still the eye. Latch how far
// behind the fit point it is.
void Begin(std::uint32_t entityId, Vec3 fit, Vec3 forward)
{
    gBegins.fetch_add(1, std::memory_order_relaxed);
    float eyeDepth = std::numeric_limits<float>::quiet_NaN();
    float matrix[12]{};
    const std::uint8_t* camera = gViewCamera();
    if (camera != nullptr && SafeCopy(matrix, camera + engine::CameraLayout::matrix, sizeof(matrix))) {
        eyeDepth = examine::EyeDepth(fit, forward, Vec3{matrix[3], matrix[7], matrix[11]});
    }
    gEyeDepth.store(eyeDepth, std::memory_order_relaxed);
    gEyeEntity.store(entityId, std::memory_order_relaxed);
}

// After Prey's fit, which `from` (an RVA) called. Only a world UI's fit, from
// one of the two known callers, is moved; the begin's latches the eye first.
void AfterFit(std::uint8_t* examination, float* target, const float* facing, bool fitted, std::uintptr_t from)
{
    gFits.fetch_add(1, std::memory_order_relaxed);
    int type = 0;
    std::uint32_t entityId = 0;
    if (!fitted || !SafeRead(examination + en::ExamType, type) || type != en::TypeWorldUi) { return; }
    if (from != en::BeginReturn && from != en::FrameReturn) {
        gOtherCaller.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    Vec3 fit{};
    Quaternion facingNow{};
    if (!SafeRead(examination + en::ExamEntity, entityId) || !SafeCopy(&fit, target, sizeof(fit)) ||
        !SafeCopy(&facingNow, facing, sizeof(facingNow))) {
        gUnread.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (from == en::BeginReturn) { Begin(entityId, fit, examine::Forward(facingNow)); }
    const unsigned cm = gPullbackCm.load(std::memory_order_relaxed);
    if (!gActive.load(std::memory_order_acquire) || cm == 0) { return; }
    const float eyeDepth = gEyeEntity.load(std::memory_order_relaxed) == entityId
                               ? gEyeDepth.load(std::memory_order_relaxed)
                               : std::numeric_limits<float>::quiet_NaN();
    if (!std::isfinite(eyeDepth)) {
        gNoEye.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const examine::Placement placed = examine::Place(fit, facingNow, cm, eyeDepth);
    if (placed.pull > 0.0f) {
        if (!WriteTarget(target, placed.position)) {
            gUnwritten.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        gMoved.fetch_add(1, std::memory_order_relaxed);
    }
    if (placed.pull < examine::Metres(cm)) { gCapped.fetch_add(1, std::memory_order_relaxed); }
}

std::uintptr_t __fastcall FitViewDetour(void* examination, float* target, float* facing, void* entity)
{
    const std::uintptr_t result = gOriginal.load(std::memory_order_acquire)(examination, target, facing, entity);
    const auto from = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - gBase;
    AfterFit(static_cast<std::uint8_t*>(examination), target, facing, (result & 0xFF) != 0, from);
    return result;
}

// --- the gate and the hook (main thread) ----------------------------------------
bool GateBodies(std::uintptr_t base)
{
    __try {
        return contract::BodiesMatch(base, en::Contracts);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// PreyDll's base, once it is loaded and the three bodies are the ones read:
// ERROR_NOT_READY before, ERROR_BAD_EXE_FORMAT for other bytes.
DWORD Resolve(std::uintptr_t& base)
{
    base = PreyBase();
    if (base == 0) { return ERROR_NOT_READY; }
    if (!GateBodies(base)) {
        base = 0;
        return ERROR_BAD_EXE_FORMAT;
    }
    return ERROR_SUCCESS;
}
// Tests replace the engine here.
DWORD (*gResolve)(std::uintptr_t&) = Resolve;

bool Install(std::uintptr_t target)
{
    const hook::Result result =
        hook::Install(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&FitViewDetour), gOriginal);
    if (!result) {
        Log(std::string("result=failed detail=") + hook::StepName(result.step) +
            " status=" + std::to_string(result.status) + " target=ExamineFitView");
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
    std::uintptr_t base = 0;
    const DWORD ready = gResolve(base);
    if (ready == ERROR_NOT_READY) {
        Log("result=unavailable detail=no_preydll");
        return false;
    }
    gInstallTried = true;
    if (ready != ERROR_SUCCESS) {
        gGate = "refused";
        Log("result=unavailable detail=bytes_mismatch target=ExamineFitView");
        return false;
    }
    gBase = base;
    gLayout.store(true, std::memory_order_release);
    if (!gInstall(base + en::FitView)) {
        gGate = "hook_failed";
        return false;
    }
    gGate = "ok";
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=hook_installed target=ExamineFitView rows=" + std::to_string(std::size(en::Contracts)));
    return true;
}

// The layout without the hook (PREYVR_EXAMINE_CAMERA=0): nothing hooks the fit,
// so its bytes can be read whenever this runs.
void LatchLayout()
{
    std::lock_guard lock(gInstallMutex);
    if (gLayout.load(std::memory_order_acquire)) { return; }
    std::uintptr_t base = 0;
    if (gResolve(base) == ERROR_SUCCESS) { gLayout.store(true, std::memory_order_release); }
}

} // namespace

DWORD SetExaminePullbackCm(unsigned cm)
{
    if (cm > examine::kMaxCm) { return ERROR_INVALID_PARAMETER; }
    gPullbackCm.store(cm, std::memory_order_relaxed);
    return 0;
}

unsigned ExaminePullbackCm() { return gPullbackCm.load(std::memory_order_relaxed); }

void EnsureExamineCamera()
{
    // PREYVR_EXAMINE_CAMERA=0: Prey's camera, not even hooked. Screens are still
    // pointed at from the hand (ExaminingScreen).
    if (EnvSwitchOff(L"PREYVR_EXAMINE_CAMERA")) {
        gActive.store(false, std::memory_order_release);
        LatchLayout();
        Log("result=0 detail=disabled_by_environment hooks=0");
        return;
    }
    gActive.store(InstallHook(), std::memory_order_release);
}

void StopExamineCamera() { gActive.store(false, std::memory_order_release); }

bool ExaminingScreen(std::uintptr_t player)
{
    if (player == 0 || !gLayout.load(std::memory_order_acquire)) { return false; }
    const auto* examination = reinterpret_cast<const std::uint8_t*>(player + en::PlayerExamination);
    int state = 0, type = 0;
    return SafeRead(examination + en::ExamState, state) && state == en::StateOn &&
           SafeRead(examination + en::ExamType, type) && type == en::TypeWorldUi;
}

std::string ExamineCameraReport()
{
    std::ostringstream s;
    s << "examine={pullbackCm=" << ExaminePullbackCm() << " active=" << (gActive.load() ? 1 : 0)
      << " hooks=" << (gInstalled.load() ? 1 : 0) << " gate=" << gGate.load() << " fits=" << gFits.load()
      << " begins=" << gBegins.load() << " moved=" << gMoved.load() << " capped=" << gCapped.load()
      << " noEye=" << gNoEye.load() << " otherCaller=" << gOtherCaller.load() << " unread=" << gUnread.load()
      << " unwritten=" << gUnwritten.load() << "}";
    return s.str();
}

} // namespace preyvr::dll
