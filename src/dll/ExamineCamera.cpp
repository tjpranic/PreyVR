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
std::atomic<const char*> gGate{"unchecked"};
std::atomic<std::uint64_t> gFits{0}, gBegins{0}, gMoved{0}, gCapped{0}, gNoEye{0}, gOtherCaller{0}, gUnread{0},
    gUnwritten{0};
// The eye's depth behind the fit point when the examination of this entity
// began (examine::EyeDepth). Main thread only writes them.
std::atomic<std::uint32_t> gEyeEntity{0};
std::atomic<float> gEyeDepth{std::numeric_limits<float>::quiet_NaN()};
// For the report, from the last begin: the fit's distance from the screen, the
// eye's behind it, the view camera's vertical FOV and Prey's viewingBuffer; and
// the last pull applied. Millimetres and tenths of a degree; -1 unknown.
std::atomic<int> gLastScreenMm{-1}, gLastEyeMm{-1}, gLastFovDeci{-1}, gLastBufferMm{-1}, gLastPullMm{0};

int Millimetres(float metres) { return std::isfinite(metres) ? static_cast<int>(std::lround(metres * 1000.0f)) : -1; }

// --- reads and the one write (main thread) ----------------------------------------
// The examined entity's position: IEntity::GetWorldTM (vtable +0xE8), which the
// fit has just called on it. Its translation is the screen's corner.
bool EntityOrigin(void* entity, Vec3& out)
{
    __try {
        const auto table = *reinterpret_cast<const std::uintptr_t* const*>(entity);
        using WorldTm = const float*(__fastcall*)(void*);
        const float* m = reinterpret_cast<WorldTm>(table[0xE8 / sizeof(std::uintptr_t)])(entity);
        if (m == nullptr) { return false; }
        out = {m[3], m[7], m[11]};
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// Tests replace the engine here.
bool (*gEntityOrigin)(void*, Vec3&) = EntityOrigin;
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
// behind the fit point it is, and what the report shows.
void Begin(const std::uint8_t* examination, std::uint32_t entityId, Vec3 fit, Vec3 forward, void* entity)
{
    gBegins.fetch_add(1, std::memory_order_relaxed);
    float eyeDepth = std::numeric_limits<float>::quiet_NaN(), fov = std::numeric_limits<float>::quiet_NaN();
    float matrix[12]{};
    const std::uint8_t* camera = gViewCamera();
    if (camera != nullptr && SafeCopy(matrix, camera + engine::CameraLayout::matrix, sizeof(matrix)) &&
        SafeRead(camera + engine::CameraLayout::fov, fov)) {
        eyeDepth = examine::EyeDepth(fit, forward, Vec3{matrix[3], matrix[7], matrix[11]});
    }
    gEyeDepth.store(eyeDepth, std::memory_order_relaxed);
    gEyeEntity.store(entityId, std::memory_order_relaxed);
    Vec3 origin{};
    float buffer = std::numeric_limits<float>::quiet_NaN();
    gLastScreenMm.store(gEntityOrigin(entity, origin) ? Millimetres(Dot(Sub(origin, fit), forward)) : -1,
                        std::memory_order_relaxed);
    gLastEyeMm.store(Millimetres(eyeDepth), std::memory_order_relaxed);
    gLastFovDeci.store(std::isfinite(fov) ? static_cast<int>(std::lround(fov * 1800.0f / kPi)) : -1,
                       std::memory_order_relaxed);
    gLastBufferMm.store(SafeRead(examination + en::ExamViewingBuffer, buffer) ? Millimetres(buffer) : -1,
                        std::memory_order_relaxed);
}

// After Prey's fit, which `from` (an RVA) called. Only a world UI's fit, from
// one of the two known callers, is moved; the begin's latches the eye first.
void AfterFit(std::uint8_t* examination, float* target, const float* facing, void* entity, bool fitted,
              std::uintptr_t from)
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
    if (from == en::BeginReturn) { Begin(examination, entityId, fit, examine::Forward(facingNow), entity); }
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
    gLastPullMm.store(Millimetres(placed.pull), std::memory_order_relaxed);
}

std::uintptr_t __fastcall FitViewDetour(void* examination, float* target, float* facing, void* entity)
{
    const std::uintptr_t result = gOriginal.load(std::memory_order_acquire)(examination, target, facing, entity);
    const auto from = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - gBase;
    AfterFit(static_cast<std::uint8_t*>(examination), target, facing, entity, (result & 0xFF) != 0, from);
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
    if (!gInstall(base + en::FitView)) {
        gGate = "hook_failed";
        return false;
    }
    gGate = "ok";
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=hook_installed target=ExamineFitView rows=" + std::to_string(std::size(en::Contracts)));
    return true;
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
    // PREYVR_EXAMINE_CAMERA=0: Prey's camera, not even hooked.
    if (EnvSwitchOff(L"PREYVR_EXAMINE_CAMERA")) {
        gActive.store(false, std::memory_order_release);
        Log("result=0 detail=disabled_by_environment hooks=0");
        return;
    }
    gActive.store(InstallHook(), std::memory_order_release);
}

void StopExamineCamera() { gActive.store(false, std::memory_order_release); }

std::string ExamineCameraReport()
{
    std::ostringstream s;
    s << "examine={pullbackCm=" << ExaminePullbackCm() << " active=" << (gActive.load() ? 1 : 0)
      << " hooks=" << (gInstalled.load() ? 1 : 0) << " gate=" << gGate.load() << " fits=" << gFits.load()
      << " begins=" << gBegins.load() << " moved=" << gMoved.load() << " capped=" << gCapped.load()
      << " noEye=" << gNoEye.load() << " otherCaller=" << gOtherCaller.load() << " unread=" << gUnread.load()
      << " unwritten=" << gUnwritten.load() << " last={screenMm=" << gLastScreenMm.load()
      << " eyeMm=" << gLastEyeMm.load() << " pullMm=" << gLastPullMm.load() << " fovDeci=" << gLastFovDeci.load()
      << " bufferMm=" << gLastBufferMm.load() << "}}";
    return s.str();
}

} // namespace preyvr::dll
