#include "ResourceSetWatch.h"
#include "Bootstrap.h"
#include "DualSceneStereo.h"
#include "EnvSwitch.h"
#include "HookInstall.h"
#include "InputPost.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "SafeMemory.h"
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>

namespace preyvr::dll {
namespace {
// Steam PreyDll SHA256 7d6e322f...05311a7 (docs/CRASHES-2026-10-24.md, section 1).
// The resource set's rebuild, bool(set): its entries at +0x78, 16 bytes each,
// their count at +0x80.
using BuildFn = bool(__fastcall*)(void* set);
constexpr std::uintptr_t kBuildRva = 0xF63F10;
constexpr std::uintptr_t kVtableSlotRva = 0x1DD1D48;  // vtable 0x1DD1D38 + 0x10: its one reference
constexpr std::uintptr_t kEntries = 0x78, kCount = 0x80;

// Exact build gate, not a signature for other builds: the prologue up to the
// count reset -- the saves and frame, r12 = 0, r13 = set + 0x98,
// [set + 0x80] = 0, rsi = set.
constexpr std::uint8_t kBuildCode[] = {
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,
    0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x40,0x45,0x33,0xE4,0x4C,
    0x8D,0xA9,0x98,0x00,0x00,0x00,0x44,0x89,0xA1,0x80,0x00,0x00,0x00,0x48,0x8B,0xF1};

// Bounded telemetry: the first in full, then one in kStride.
constexpr unsigned long long kLogged = 32, kStride = 256;
constexpr unsigned long long kSummaryMs = 60000;
constexpr std::size_t kCallers = 16;
constexpr unsigned long kStackFrames = 6;

// Which thread rebuilt: the one running the scene submissions, the main
// thread, or any other.
constexpr unsigned kRender = 0, kMain = 1, kOther = 2, kThreads = 3;
constexpr const char* kThreadName[kThreads] = {"render", "main", "other"};

std::atomic<bool> gEnabled{false}, gInstalled{false}, gStartAttempted{false};
std::mutex gInstallMutex;
bool gHookTried = false;
std::atomic<BuildFn> gOriginal{nullptr};
// The module callers are read against, published by Install (a test points it
// at its own image).
std::atomic<std::uintptr_t> gModuleBase{0}, gModuleSize{0};

// Rebuilds by thread, outside and overlapping a top-level submission.
std::array<std::atomic<unsigned long long>, kThreads> gIdle{}, gInPass{};
std::atomic<unsigned long long> gRaces{0}, gGrown{0}, gGrownInPass{0};
std::atomic<ULONGLONG> gNextSummary{0};

// The first kCallers distinct callers, by return address, with their rebuilds
// and races.
struct Caller {
    std::uintptr_t address = 0;
    unsigned long long builds = 0, races = 0;
};
std::mutex gCallerMutex;
std::array<Caller, kCallers> gCallers{};
std::size_t gCallerCount = 0;
unsigned long long gCallersDropped = 0;

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_resource_set " + line);
}

bool Due(unsigned long long n)
{
    return n <= kLogged || n % kStride == 0;
}

// An address as an RVA of the module when it is inside it, else as itself.
void AppendAddress(std::ostringstream& out, std::uintptr_t address)
{
    const std::uintptr_t base = gModuleBase.load(std::memory_order_relaxed);
    const std::uintptr_t size = gModuleSize.load(std::memory_order_relaxed);
    out << std::hex << std::uppercase;
    if (base != 0 && address >= base && address - base < size) {
        out << "0x" << address - base;
    } else {
        out << "ext:0x" << address;
    }
    out << std::nouppercase << std::dec;
}

// Counts one rebuild against its caller. True when the caller is new; the table
// keeps the first kCallers.
bool NoteCaller(std::uintptr_t address, bool race)
{
    std::lock_guard lock(gCallerMutex);
    for (std::size_t i = 0; i < gCallerCount; ++i) {
        if (gCallers[i].address == address) {
            ++gCallers[i].builds;
            if (race) ++gCallers[i].races;
            return false;
        }
    }
    if (gCallerCount == kCallers) {
        ++gCallersDropped;
        return false;
    }
    gCallers[gCallerCount++] = {address, 1, race ? 1u : 0u};
    return true;
}

// Read-only: who rebuilt, during what, and whether the array moved; the
// original, unchanged.
bool __fastcall ObserveBuild(void* set)
{
    const auto original = gOriginal.load(std::memory_order_acquire);
    if (!gEnabled.load(std::memory_order_acquire)) {
        return original != nullptr && original(set);
    }
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const SceneSubmission atStart = CurrentSceneSubmission();
    const unsigned long thread = GetCurrentThreadId();
    const unsigned who = atStart.thread != 0 && thread == atStart.thread ? kRender
                         : OnEngineMainThread()                           ? kMain
                                                                          : kOther;
    const auto at = reinterpret_cast<std::uintptr_t>(set);
    std::uintptr_t before = 0;
    SafeRead(at + kEntries, before);
    const bool result = original != nullptr && original(set);
    std::uintptr_t after = 0;
    int count = 0;
    SafeRead(at + kEntries, after);
    SafeRead(at + kCount, count);
    // Overlapping a submission: in flight when it started or when it ended.
    const SceneSubmission atEnd = CurrentSceneSubmission();
    const bool inPass = atStart.inFlight || atEnd.inFlight;
    const bool grown = before != 0 && after != before;
    const bool race = inPass && who != kRender;
    (inPass ? gInPass : gIdle)[who].fetch_add(1, std::memory_order_relaxed);
    if (grown) {
        gGrown.fetch_add(1, std::memory_order_relaxed);
        if (inPass) gGrownInPass.fetch_add(1, std::memory_order_relaxed);
    }
    if (NoteCaller(caller, race)) {
        std::ostringstream line;
        line << "detail=new_caller caller=";
        AppendAddress(line, caller);
        line << " who=" << kThreadName[who] << " thread=" << thread << " inPass=" << (inPass ? 1 : 0);
        Log(line.str());
    }
    if (race) {
        const auto n = gRaces.fetch_add(1, std::memory_order_relaxed) + 1;
        if (Due(n)) {
            // The caller's own callers name the subsystem; only for logged races.
            void* frames[kStackFrames]{};
            const USHORT got = RtlCaptureStackBackTrace(1, kStackFrames, frames, nullptr);
            std::ostringstream line;
            line << "detail=race n=" << n << " who=" << kThreadName[who] << " thread=" << thread
                 << " step=" << (atStart.inFlight ? atStart.step : atEnd.step) << " sceneThread=" << atStart.thread
                 << " entries=" << count << " grown=" << (grown ? 1 : 0) << " caller=";
            AppendAddress(line, caller);
            line << " stack=";
            for (USHORT i = 0; i < got; ++i) {
                if (i != 0) line << ',';
                AppendAddress(line, reinterpret_cast<std::uintptr_t>(frames[i]));
            }
            Log(line.str());
        }
    }
    return result;
}

DWORD Install()
{
    // Caller holds gInstallMutex. The hook stays until process exit; rset.watch 0
    // only pauses the counting.
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
    std::uintptr_t slot = 0;
    if (!SafeEqual(reinterpret_cast<const void*>(base + kBuildRva), kBuildCode, sizeof(kBuildCode)) ||
        !SafeRead(base + kVtableSlotRva, slot) || slot != base + kBuildRva) {
        Log("result=refused detail=code_mismatch");
        return 4;
    }
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    const bool sized = SafeRead(base, dos) && SafeRead(base + static_cast<std::uintptr_t>(dos.e_lfanew), nt);
    gModuleSize.store(sized ? nt.OptionalHeader.SizeOfImage : 0, std::memory_order_relaxed);
    gModuleBase.store(base, std::memory_order_relaxed);
    if (!gHookTried) {
        gHookTried = true;
        if (!EnsureMinHook() ||
            !hook::HookAt(reinterpret_cast<void*>(base + kBuildRva), kBuildCode,
                          reinterpret_cast<void*>(&ObserveBuild), gOriginal, "resource_set_build", &Log)) {
            return 5;
        }
    } else if (gOriginal.load(std::memory_order_acquire) == nullptr) {
        return 5;
    }
    gNextSummary.store(GetTickCount64() + kSummaryMs, std::memory_order_relaxed);
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=installed observe=resource_set_build rva=0xF63F10");
    return 0;
}
} // namespace

DWORD SetResourceSetWatch(unsigned int enabled)
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
    Log(std::string("result=0 detail=watch value=") + (enabled != 0 ? "1" : "0"));
    return 0;
}

void ServiceResourceSetWatch()
{
    if (!gStartAttempted.load(std::memory_order_acquire)) {
        if (ModulePinStatus() == 0) {
            return;
        }
        gStartAttempted.store(true, std::memory_order_release);
        if (EnvSwitchOff(L"PREYVR_RSET_WATCH")) {
            Log("result=0 detail=off reason=PREYVR_RSET_WATCH");
            return;
        }
        SetResourceSetWatch(1);
        return;
    }
    if (!gInstalled.load(std::memory_order_acquire)) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    ULONGLONG next = gNextSummary.load(std::memory_order_relaxed);
    if (now >= next && gNextSummary.compare_exchange_strong(next, now + kSummaryMs, std::memory_order_relaxed)) {
        Log("summary" + ResourceSetWatchReport());
    }
}

std::string ResourceSetWatchReport()
{
    const auto n = [](const std::atomic<unsigned long long>& value) { return value.load(std::memory_order_relaxed); };
    unsigned long long builds = 0;
    for (unsigned i = 0; i < kThreads; ++i) builds += n(gIdle[i]) + n(gInPass[i]);
    std::ostringstream out;
    out << " installed=" << (gInstalled.load() ? 1 : 0) << " enabled=" << (gEnabled.load() ? 1 : 0)
        << " builds=" << builds;
    for (unsigned i = 0; i < kThreads; ++i) {
        out << ' ' << kThreadName[i] << "Idle=" << n(gIdle[i]) << ' ' << kThreadName[i] << "InPass=" << n(gInPass[i]);
    }
    out << " races=" << n(gRaces) << " grown=" << n(gGrown) << " grownInPass=" << n(gGrownInPass)
        << " sceneThread=" << CurrentSceneSubmission().thread << " callers=";
    std::lock_guard lock(gCallerMutex);
    for (std::size_t i = 0; i < gCallerCount; ++i) {
        if (i != 0) out << ',';
        AppendAddress(out, gCallers[i].address);
        out << ':' << gCallers[i].builds << '/' << gCallers[i].races;
    }
    out << " callersDropped=" << gCallersDropped;
    return out.str();
}
} // namespace preyvr::dll
