#include "CullPrepareGuard.h"
#include "Bootstrap.h"
#include "EnvSwitch.h"
#include "Logger.h"
#include "SafeMemory.h"
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
// exact code gate (kCode); docs/ENGINE-INSTABILITY.md, "The trigger", names the
// functions, and tools/re/verify_cull_prepare_guard.py checks the gate and the
// wiring offline.
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

// A kick that did not start a prepare: the first few are logged.
constexpr unsigned long long kFailuresLogged = 8;

std::atomic<bool> gEnabled{false}, gInstalled{false}, gStartAttempted{false};
std::mutex gInstallMutex;
std::atomic<PrepareOcclusionFn> gPrepareTarget{nullptr};
// Absolute addresses, published by Install (a test points them at its own memory).
std::atomic<std::uintptr_t> gObjManagerSlot{0}, gEngineSlot{0}, gEngineVtable{0};
std::atomic<std::uintptr_t> gCVarsSlot{0}, gNoWorldFlag{0};
std::atomic<float> gEpsilon{0.05f};

// What cull.prepare reports: the guard's health, not a record of the frames.
std::atomic<unsigned long long> gKicks{0}, gKickFailed{0}, gWrongReceiver{0}, gUnreadable{0};

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

DWORD Install()
{
    // Caller holds gInstallMutex. Installed once; cull.prepare 0 only turns the
    // guard off.
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
    // Published before the guard can run.
    gObjManagerSlot.store(base + kObjManagerRva, std::memory_order_release);
    gEngineSlot.store(base + kThreeDEngineRva, std::memory_order_release);
    gEngineVtable.store(base + kEngineVtableRva, std::memory_order_release);
    gCVarsSlot.store(base + kCVarsRva, std::memory_order_release);
    gNoWorldFlag.store(base + kNoWorldFlagRva, std::memory_order_release);
    gPrepareTarget.store(reinterpret_cast<PrepareOcclusionFn>(base + kPrepareRva), std::memory_order_release);
    gInstalled.store(true, std::memory_order_release);
    std::ostringstream line;
    line << "result=0 detail=installed seam=CSystem::Render prepare=PrepareOcclusion rva=0x20DA10 epsilon=" << eps;
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
    if (!gEnabled.load(std::memory_order_acquire)) {
        return;
    }
    std::uintptr_t engine = 0, vtable = 0;
    if (!SafeRead(gEngineSlot.load(std::memory_order_acquire), engine) || engine == 0 ||
        !SafeRead(engine, vtable) || vtable != gEngineVtable.load(std::memory_order_acquire)) {
        gWrongReceiver.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const PrepareOcclusionFn prepare = gPrepareTarget.load(std::memory_order_acquire);
    if (prepare == nullptr) {
        return;
    }
    prepare(reinterpret_cast<void*>(engine), viewCamera);
    const std::uint32_t after = CullThreadState();
    const bool kicked = after == kStatePreparing || after == kStatePrepared;
    if (kicked) {
        gKicks.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto n = gKickFailed.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= kFailuresLogged) {
        std::ostringstream line;
        line << "detail=kick_failed n=" << n << " state=";
        AppendState(line, state);
        line << "->";
        AppendState(line, after);
        AppendCamera(line, viewCamera);
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
    }
}

std::string CullPrepareReport()
{
    const auto n = [](const std::atomic<unsigned long long>& value) { return value.load(std::memory_order_relaxed); };
    std::ostringstream out;
    out << " installed=" << (gInstalled.load() ? 1 : 0) << " guard=" << (gEnabled.load() ? 1 : 0)
        << " kicks=" << n(gKicks) << " kickFailed=" << n(gKickFailed) << " wrongReceiver=" << n(gWrongReceiver)
        << " unreadable=" << n(gUnreadable) << " state=";
    AppendState(out, CullThreadState());
    return out.str();
}
} // namespace preyvr::dll
