#include "CullPrepareGuard.h"
#include "Bootstrap.h"
#include "EnvSwitch.h"
#include "FrameProbe.h"
#include "HookInstall.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "SafeMemory.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>

namespace preyvr::dll {
namespace {
// Steam PreyDll SHA256 7d6e322f...05311a7. Every offset below is fixed by the
// exact code gate (kCode); docs/CULL-CAMERA-CRASH-2026-10-17.md, "The handshake",
// has the disassembly they were read from.
//
// C3DEngine::PrepareOcclusion(const CCamera&), vtable +0x40, the whole function:
//   if (cvars+0x1A4 && !gEnv+0x2AB && (|m03| > eps || |m13| > eps || |m23| > eps))
//       CCullThread::PrepareCullbufferAsync(objManager + 0x200, camera);
using PrepareOcclusionFn = void(__fastcall*)(void* engine, const void* camera);
constexpr std::uintptr_t kPrepareRva = 0x20DA10;
constexpr std::uintptr_t kEngineVtableRva = 0x1C912A0, kPrepareSlot = 0x40;
constexpr std::uintptr_t kThreeDEngineRva = 0x224D988; // gEnv->p3DEngine
constexpr std::uintptr_t kCVarsRva = 0x243A688;        // the C3DEngine cvars (R-063)
constexpr std::uintptr_t kCVarRenderTasks = 0x1A4;     // must be nonzero
constexpr std::uintptr_t kNoWorldFlagRva = 0x224DC2B;  // gEnv+0x2AB, must be 0; CSystem::Render tests it too
constexpr std::uintptr_t kEpsilonRva = 0x1C7F77C;      // 0.05f
constexpr std::uintptr_t kObjManagerRva = 0x243A698;
// CCullThread is CObjManager+0x200 (both thunks add it); the handshake state at +4.
constexpr std::uintptr_t kCullThread = 0x200, kState = 0x04;
// CCamera's translation, matrix column 3: what PrepareOcclusion and CSystem::Render test.
constexpr std::uintptr_t kCameraX = 0x0C, kCameraY = 0x1C, kCameraZ = 0x2C;

// The handshake. PrepareCullbufferAsync sets 1 and kicks the z-buffer jobs; the
// last (RasterizeZBuffer) sets 2, or launches the consumer from 3. CullStart sets
// 3 and stores the pass, or launches from 2. EndOcclusion waits for the jobs and,
// when the state is neither 0 nor 4, only drops the producer count.
constexpr std::uint32_t kStatePreparing = 1, kStatePrepared = 2, kStateWaiting = 3;

// PrepareOcclusion's gates, as bits: all three set means it prepares.
constexpr std::uint32_t kGateRenderTasks = 1, kGateWorld = 2, kGateCamera = 4, kGateUnread = 8;
constexpr std::uint32_t kGatesOpen = kGateRenderTasks | kGateWorld | kGateCamera;

// Exact build gate, not a signature for other builds.
// PrepareOcclusion, whole: the cvar, the gEnv flag, the translation test, and the
// tail jump into the CCullThread thunk.
constexpr std::uint8_t kPrepareCode[] = {
    0x48,0x8B,0x05,0x71,0xCC,0x22,0x02,0x83,0xB8,0xA4,0x01,0x00,0x00,0x00,0x74,0x4C,
    0x80,0x3D,0x04,0x02,0x04,0x02,0x00,0x75,0x43,0xF3,0x0F,0x10,0x42,0x0C,0xF3,0x0F,
    0x10,0x15,0x2A,0x11,0xA7,0x01,0xF3,0x0F,0x10,0x1D,0x3E,0x1D,0xA7,0x01,0x0F,0x54,
    0xC2,0x0F,0x2F,0xC3,0xF3,0x0F,0x10,0x62,0x1C,0xF3,0x0F,0x10,0x4A,0x2C,0x77,0x10,
    0x0F,0x54,0xE2,0x0F,0x2F,0xE3,0x77,0x08,0x0F,0x54,0xCA,0x0F,0x2F,0xCB,0x76,0x0C,
    0x48,0x8B,0x0D,0x31,0xCC,0x22,0x02,0xE9,0xD4,0x43,0x07,0x00,0xC3};
// CObjManager+0x200, then PrepareCullbufferAsync (0x23FFB0).
constexpr std::uint8_t kPrepareThunkCode[] = {
    0x48,0x81,0xC1,0x00,0x02,0x00,0x00,0xE9,0x64,0xE1,0xFB,0xFF};
// CObjManager+0x200, then CullStart (0x23F020); RenderWorld's call at 0x21E6F0.
constexpr std::uint8_t kCullStartThunkCode[] = {
    0x48,0x81,0xC1,0x00,0x02,0x00,0x00,0xE9,0xF4,0xD1,0xFB,0xFF};
// PrepareCullbufferAsync: state = 1, before its first job.
constexpr std::uint8_t kPrepareStateCode[] = {
    0x48,0x8B,0x0D,0x0D,0x9F,0x1F,0x02,0xE8,0x70,0x16,0x04,0x00,0xC7,0x43,0x04,0x01,
    0x00,0x00,0x00,0x44,0x88,0x3B,0x44,0x89,0x7B,0x78};
// CullStart: 2 -> 4 and launch, else 3 and store the pass.
constexpr std::uint8_t kCullStartStateCode[] = {
    0x83,0x7F,0x04,0x02,0x75,0x0C,0xC7,0x47,0x04,0x04,0x00,0x00,0x00,0x41,0xB6,0x01,
    0xEB,0x26,0xC7,0x47,0x04,0x03,0x00,0x00,0x00};
// RasterizeZBuffer: r14d = 2, then 3 -> 4 and launch, else r14d.
constexpr std::uint8_t kRasterizeTwoCode[] = {0x41,0xBE,0x02,0x00,0x00,0x00};
constexpr std::uint8_t kRasterizeStateCode[] = {
    0x83,0x7F,0x04,0x03,0x75,0x0C,0xC7,0x47,0x04,0x04,0x00,0x00,0x00,0x40,0xB6,0x01,
    0xEB,0x04,0x44,0x89,0x77,0x04};
// EndOcclusion (CCullThread): wait for the jobs; a state other than 0 or 4 only
// drops the producer count (0x27E5C0). Nothing discards the queued batch.
constexpr std::uint8_t kEndOcclusionCode[] = {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8B,0x0D,0xD0,0xEA,0x00,0x02,
    0x48,0x8B,0x01,0x48,0x8D,0x93,0xB0,0x00,0x00,0x00,0xFF,0x50,0x20,0xF7,0x43,0x04,
    0xFB,0xFF,0xFF,0xFF,0x74,0x11,0x48,0x8B,0x0D,0x8B,0xB6,0x1F,0x02,0x48,0x83,0xC4,
    0x20,0x5B,0xE9,0xA9,0xF5,0x03,0x00,0x48,0x83,0xC4,0x20,0x5B,0xC3};
struct CodeSpan {
    std::uintptr_t rva;
    const std::uint8_t* bytes;
    std::size_t size;
};
constexpr std::array<CodeSpan, 8> kCode{{
    {kPrepareRva, kPrepareCode, sizeof(kPrepareCode)},
    {0x281E40, kPrepareThunkCode, sizeof(kPrepareThunkCode)},
    {0x281E20, kCullStartThunkCode, sizeof(kCullStartThunkCode)},
    {0x240784, kPrepareStateCode, sizeof(kPrepareStateCode)},
    {0x23F061, kCullStartStateCode, sizeof(kCullStartStateCode)},
    {0x240CE5, kRasterizeTwoCode, sizeof(kRasterizeTwoCode)},
    {0x240D47, kRasterizeStateCode, sizeof(kRasterizeStateCode)},
    {0x23EFE0, kEndOcclusionCode, sizeof(kEndOcclusionCode)},
}};

// Bounded telemetry: the first in full, then one in kStride.
constexpr unsigned long long kLogged = 32, kStride = 256;
constexpr unsigned long long kSummaryMs = 60000;

// What PrepareOcclusion did since the last render: 0 none, else kSeen | gates |
// the state it left << 4.
constexpr std::uint32_t kSeen = 0x100;

std::atomic<bool> gEnabled{false}, gInstalled{false}, gHooked{false}, gStartAttempted{false};
std::mutex gInstallMutex;
bool gHookTried = false;
std::atomic<PrepareOcclusionFn> gPrepareOriginal{nullptr}, gPrepareTarget{nullptr};
// Absolute addresses, published by Install (a test points them at its own memory).
std::atomic<std::uintptr_t> gObjManagerSlot{0}, gEngineSlot{0}, gEngineVtable{0};
std::atomic<std::uintptr_t> gCVarsSlot{0}, gNoWorldFlag{0};
std::atomic<float> gEpsilon{0.05f};

std::atomic<unsigned long long> gPrepares{0}, gPrepareRan{0}, gSkippedOrigin{0}, gSkippedRenderTasks{0};
std::atomic<unsigned long long> gSkippedNoWorld{0}, gGateUnread{0}, gRenders{0}, gUnprepared{0};
std::atomic<unsigned long long> gKicks{0}, gKickFailed{0}, gOrphans{0}, gWrongReceiver{0}, gUnreadable{0};
std::atomic<std::uint32_t> gLastPrepare{0}, gFrameLast{0};
std::atomic<bool> gLoggedFirstOrigin{false};
std::atomic<ULONGLONG> gNextSummary{0};

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_cull_prepare " + line);
}

bool CodeMatches(std::uintptr_t base)
{
    for (const auto& span : kCode) {
        if (!SafeEqual(reinterpret_cast<const void*>(base + span.rva), span.bytes, span.size)) {
            return false;
        }
    }
    std::uintptr_t slot = 0;
    return SafeRead(base + kEngineVtableRva + kPrepareSlot, slot) && slot == base + kPrepareRva;
}

bool ReadState(std::uint32_t& out)
{
    const std::uintptr_t slot = gObjManagerSlot.load(std::memory_order_acquire);
    std::uintptr_t manager = 0;
    return slot != 0 && SafeRead(slot, manager) && manager != 0 && SafeRead(manager + kCullThread + kState, out);
}

bool ReadTranslation(const void* camera, float (&out)[3])
{
    const auto at = reinterpret_cast<std::uintptr_t>(camera);
    return camera != nullptr && SafeRead(at + kCameraX, out[0]) && SafeRead(at + kCameraY, out[1]) &&
           SafeRead(at + kCameraZ, out[2]);
}

// PrepareOcclusion's own three tests, read the way it reads them.
std::uint32_t Gates(const void* camera)
{
    std::uintptr_t cvars = 0;
    int renderTasks = 0;
    std::uint8_t noWorld = 0;
    float t[3]{};
    const std::uintptr_t cvarsSlot = gCVarsSlot.load(std::memory_order_acquire);
    const std::uintptr_t noWorldFlag = gNoWorldFlag.load(std::memory_order_acquire);
    if (cvarsSlot == 0 || !SafeRead(cvarsSlot, cvars) || cvars == 0 ||
        !SafeRead(cvars + kCVarRenderTasks, renderTasks) || noWorldFlag == 0 || !SafeRead(noWorldFlag, noWorld) ||
        !ReadTranslation(camera, t)) {
        return kGateUnread;
    }
    // |v| > eps, false for a NaN: the engine's ANDPS/COMISS/JA.
    const float eps = gEpsilon.load(std::memory_order_relaxed);
    std::uint32_t gates = 0;
    if (renderTasks != 0) gates |= kGateRenderTasks;
    if (noWorld == 0) gates |= kGateWorld;
    if (std::fabs(t[0]) > eps || std::fabs(t[1]) > eps || std::fabs(t[2]) > eps) gates |= kGateCamera;
    return gates;
}

const char* PrepareName(std::uint32_t last)
{
    if ((last & kSeen) == 0) return "none";
    const std::uint32_t gates = last & 0xF;
    if ((gates & kGateUnread) != 0) return "unread";
    if ((gates & kGateRenderTasks) == 0) return "skipped_render_tasks";
    if ((gates & kGateWorld) == 0) return "skipped_no_world";
    if ((gates & kGateCamera) == 0) return "skipped_origin";
    return "ran";
}

void AppendState(std::ostringstream& line, std::uint32_t state)
{
    if (state == kCullStateUnknown) line << '?';
    else line << state;
}

void AppendCamera(std::ostringstream& line, const void* camera)
{
    float t[3]{};
    if (!ReadTranslation(camera, t)) {
        line << " camera=?";
        return;
    }
    line.setf(std::ios::fixed);
    line.precision(3);
    line << " camera=" << t[0] << ',' << t[1] << ',' << t[2];
}

bool Due(unsigned long long n)
{
    return n <= kLogged || n % kStride == 0;
}

// Read-only: what the engine's own call decided, then the original unchanged.
void __fastcall ObservePrepare(void* engine, const void* camera)
{
    const auto original = gPrepareOriginal.load(std::memory_order_acquire);
    const std::uint32_t before = CullThreadState();
    const std::uint32_t gates = Gates(camera);
    if (original != nullptr) {
        original(engine, camera);
    }
    const std::uint32_t after = CullThreadState();
    gPrepares.fetch_add(1, std::memory_order_relaxed);
    if ((gates & kGateUnread) != 0) gGateUnread.fetch_add(1, std::memory_order_relaxed);
    else if ((gates & kGateRenderTasks) == 0) gSkippedRenderTasks.fetch_add(1, std::memory_order_relaxed);
    else if ((gates & kGateWorld) == 0) gSkippedNoWorld.fetch_add(1, std::memory_order_relaxed);
    else if ((gates & kGateCamera) == 0) {
        gSkippedOrigin.fetch_add(1, std::memory_order_relaxed);
        if (!gLoggedFirstOrigin.exchange(true, std::memory_order_relaxed)) {
            std::ostringstream line;
            line << "detail=first_origin_skip state=";
            AppendState(line, after);
            AppendCamera(line, camera);
            Log(line.str());
        }
    } else gPrepareRan.fetch_add(1, std::memory_order_relaxed);
    gLastPrepare.store(kSeen | gates | (after & 0xF) << 4, std::memory_order_release);
    ProbeRecord(ProbeStep::prepare, before, after, gates);
}

DWORD Install()
{
    // Caller holds gInstallMutex. The observation hook stays until process exit;
    // cull.prepare 0 only turns the guard off.
    if (gInstalled.load(std::memory_order_acquire)) {
        return 0;
    }
    if (ModulePinStatus() == 0) {
        return 2;
    }
    const HMODULE module = GetModuleHandleW(L"PreyDll.dll");
    if (module == nullptr) {
        Log("result=unavailable detail=preydll_not_loaded");
        return 3;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    if (!CodeMatches(base)) {
        Log("result=refused detail=code_mismatch");
        return 4;
    }
    float eps = 0.0f;
    if (!SafeRead(base + kEpsilonRva, eps) || !(eps > 0.0f)) {
        Log("result=refused detail=epsilon_unreadable");
        return 4;
    }
    gEpsilon.store(eps, std::memory_order_relaxed);
    // Published before the hook can run.
    gObjManagerSlot.store(base + kObjManagerRva, std::memory_order_release);
    gEngineSlot.store(base + kThreeDEngineRva, std::memory_order_release);
    gEngineVtable.store(base + kEngineVtableRva, std::memory_order_release);
    gCVarsSlot.store(base + kCVarsRva, std::memory_order_release);
    gNoWorldFlag.store(base + kNoWorldFlagRva, std::memory_order_release);
    gPrepareTarget.store(reinterpret_cast<PrepareOcclusionFn>(base + kPrepareRva), std::memory_order_release);
    // The observation is optional: the guard calls the gated function directly
    // without it. MinHook relocates the RIP-relative load it displaces.
    if (!gHookTried) {
        gHookTried = true;
        if (EnsureMinHook() &&
            hook::HookAt(reinterpret_cast<void*>(base + kPrepareRva), kPrepareCode,
                         reinterpret_cast<void*>(&ObservePrepare), gPrepareOriginal, "prepare_occlusion", &Log)) {
            gHooked.store(true, std::memory_order_release);
        }
    }
    gNextSummary.store(GetTickCount64() + kSummaryMs, std::memory_order_relaxed);
    gInstalled.store(true, std::memory_order_release);
    std::ostringstream line;
    line << "result=0 detail=installed seam=CSystem::Render observe=PrepareOcclusion rva=0x20DA10 hooked="
         << (gHooked.load() ? 1 : 0) << " epsilon=" << eps;
    Log(line.str());
    return 0;
}
} // namespace

std::uint32_t CullThreadState()
{
    std::uint32_t state = 0;
    return ReadState(state) ? state : kCullStateUnknown;
}

void CullPrepareBeforeRender(const void* viewCamera)
{
    if (!gInstalled.load(std::memory_order_acquire) || viewCamera == nullptr) {
        return;
    }
    gRenders.fetch_add(1, std::memory_order_relaxed);
    const std::uint32_t last = gLastPrepare.exchange(0, std::memory_order_acq_rel);
    gFrameLast.store(last, std::memory_order_relaxed);
    const std::uint32_t state = CullThreadState();
    if (state == kCullStateUnknown) {
        gUnreadable.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // The normal frame: this frame's prepare is in flight or done.
    if (state == kStatePreparing || state == kStatePrepared) {
        return;
    }
    // Where PrepareOcclusion would skip, so does CSystem::Render's world (the
    // camera and the gEnv flag; the cvar only gates the queue). Nothing to pair.
    const std::uint32_t gates = Gates(viewCamera);
    if (gates != kGatesOpen) {
        return;
    }
    const auto unprepared = gUnprepared.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!gEnabled.load(std::memory_order_acquire)) {
        // Observe only: CullPrepareAfterRender counts the orphan this leaves.
        return;
    }
    std::uintptr_t engine = 0, vtable = 0;
    if (!SafeRead(gEngineSlot.load(std::memory_order_acquire), engine) || engine == 0 ||
        !SafeRead(engine, vtable) || vtable != gEngineVtable.load(std::memory_order_acquire)) {
        gWrongReceiver.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    PrepareOcclusionFn prepare = gPrepareOriginal.load(std::memory_order_acquire);
    if (prepare == nullptr) prepare = gPrepareTarget.load(std::memory_order_acquire);
    if (prepare == nullptr) {
        return;
    }
    prepare(reinterpret_cast<void*>(engine), viewCamera);
    const std::uint32_t after = CullThreadState();
    const bool kicked = after == kStatePreparing || after == kStatePrepared;
    const auto n = (kicked ? gKicks : gKickFailed).fetch_add(1, std::memory_order_relaxed) + 1;
    ProbeRecord(ProbeStep::prepareKick, state, after, gates, last);
    if (!kicked || Due(n)) {
        std::ostringstream line;
        line << "detail=" << (kicked ? "kick" : "kick_failed") << " n=" << n << " state=";
        AppendState(line, state);
        line << "->";
        AppendState(line, after);
        line << " lastPrepare=" << PrepareName(last) << " unprepared=" << unprepared;
        AppendCamera(line, viewCamera);
        Log(line.str());
    }
}

void CullPrepareAfterRender(std::uint32_t before, std::uint32_t after)
{
    if (!gInstalled.load(std::memory_order_acquire)) {
        return;
    }
    // CullStart parked (3) with no prepare in flight to launch the consumer: the
    // frame's batch will stay queued. A 3 after a 1 or 2 is the normal race,
    // which RasterizeZBuffer resolves.
    if (before == kStatePreparing || before == kStatePrepared || before == kCullStateUnknown ||
        after != kStateWaiting) {
        return;
    }
    const auto n = gOrphans.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::uint32_t last = gFrameLast.load(std::memory_order_relaxed);
    const bool guarding = gEnabled.load(std::memory_order_relaxed);
    ProbeRecord(ProbeStep::orphan, before, after, guarding ? 1 : 0, last);
    if (Due(n)) {
        std::ostringstream line;
        line << "detail=orphan n=" << n << " state=";
        AppendState(line, before);
        line << "->";
        AppendState(line, after);
        line << " guard=" << (guarding ? 1 : 0) << " lastPrepare=" << PrepareName(last);
        Log(line.str());
    }
}

DWORD SetCullPrepareGuard(unsigned int enabled)
{
    if (enabled > 1) {
        return 1;
    }
    {
        std::lock_guard lock(gInstallMutex);
        const DWORD installed = Install();
        if (installed != 0) {
            gEnabled.store(false, std::memory_order_release);
            Log("result=unavailable code=" + std::to_string(installed));
            return installed;
        }
    }
    gEnabled.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=guard value=") + (enabled != 0 ? "1" : "0"));
    return 0;
}

void ServiceCullPrepareGuard()
{
    if (!gStartAttempted.load(std::memory_order_acquire)) {
        if (ModulePinStatus() == 0) {
            return;
        }
        gStartAttempted.store(true, std::memory_order_release);
        const bool off = EnvSwitchOff(L"PREYVR_CULL_PREPARE");
        if (off) Log("result=0 detail=guard_off reason=PREYVR_CULL_PREPARE");
        SetCullPrepareGuard(off ? 0 : 1);
        return;
    }
    if (!gInstalled.load(std::memory_order_acquire)) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    ULONGLONG next = gNextSummary.load(std::memory_order_relaxed);
    if (now >= next && gNextSummary.compare_exchange_strong(next, now + kSummaryMs, std::memory_order_relaxed)) {
        Log("summary" + CullPrepareReport());
    }
}

std::string CullPrepareReport()
{
    const auto n = [](const std::atomic<unsigned long long>& value) { return value.load(std::memory_order_relaxed); };
    std::ostringstream out;
    out << " installed=" << (gInstalled.load() ? 1 : 0) << " guard=" << (gEnabled.load() ? 1 : 0)
        << " hooked=" << (gHooked.load() ? 1 : 0) << " prepares=" << n(gPrepares) << " ran=" << n(gPrepareRan)
        << " skippedOrigin=" << n(gSkippedOrigin) << " skippedNoWorld=" << n(gSkippedNoWorld)
        << " skippedRenderTasks=" << n(gSkippedRenderTasks) << " gateUnread=" << n(gGateUnread)
        << " renders=" << n(gRenders) << " unprepared=" << n(gUnprepared) << " kicks=" << n(gKicks)
        << " kickFailed=" << n(gKickFailed) << " orphans=" << n(gOrphans) << " wrongReceiver=" << n(gWrongReceiver)
        << " unreadable=" << n(gUnreadable) << " state=";
    AppendState(out, CullThreadState());
    return out.str();
}
} // namespace preyvr::dll
