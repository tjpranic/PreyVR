#include "FrameProbe.h"
#include "Bootstrap.h"
#include "CullCameraWatch.h"
#include "HookInstall.h"
#include "Logger.h"
#include "PassCameraProbe.h"
#include "SafeMemory.h"
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>

namespace preyvr::dll {
namespace {
// Steam PreyDll SHA256 7d6e322f...05311a7; docs/CULL-CAMERA-CRASH-2026-10-17.md.
// The cull consumer's job entry: params {void* owner; SRenderingPassInfo copy},
// handed to 0x23EBD0, which pops occlusion items until the type-0 end marker --
// one run per batch. The copy's frame ids sit at params +0x14/+0x18.
using ConsumerJobFn = void(__fastcall*)(void* params);
constexpr std::uintptr_t kConsumerJobRva = 0x23F1B0;
constexpr std::uintptr_t kJobFrameId = 0x14, kJobFrameId2 = 0x18;
// The whole function, its call into 0x23EBD0 included: exact build gate.
constexpr std::uint8_t kConsumerJobCode[] = {
    0x48,0x83,0xEC,0x68,0x0F,0x10,0x41,0x08,0x48,0x8D,0x54,0x24,0x20,0x0F,0x10,0x49,
    0x18,0x0F,0x29,0x44,0x24,0x20,0x0F,0x10,0x41,0x28,0x0F,0x29,0x4C,0x24,0x30,0x0F,
    0x10,0x49,0x38,0x48,0x8B,0x09,0x0F,0x29,0x44,0x24,0x40,0x0F,0x29,0x4C,0x24,0x50,
    0xE8,0xEB,0xF9,0xFF,0xFF,0x48,0x83,0xC4,0x68,0xC3};
// CreateGeneralPassRenderingInfo (0x1E5B30, landmark pass.create_general) stores
// the two renderer frame ids at out+0x0C/+0x10.
constexpr std::uintptr_t kPassFrameId = 0x0C, kPassFrameId2 = 0x10;
constexpr std::uint32_t kCallerOutsidePreyDll = 0xFFFFFFFF;

// A window is about 20 main frames of ~12 events; the ring holds 16 times that,
// so the command worker has seconds to write it before it is overwritten.
constexpr std::uint64_t kRingSize = 4096;
constexpr std::uint64_t kTickHistory = 64;
constexpr std::uint64_t kBeforeTicks = 8, kAfterTicks = 12, kMaxWindowTicks = 60;
// Windows with a refusal or a lag change are what this is for; a few plain
// panel closes are kept as the control.
constexpr unsigned long long kMaxWindows = 16, kControlWindows = 3;
// A window still open this long after its trigger (no frame starts: the cull
// watch is off, or the game stopped ticking) is written with what it has.
constexpr std::int64_t kWindowTimeoutUs = 2000000;
constexpr unsigned kPanelClose = 1, kFirstEyeRefused = 2, kLagChange = 4, kPrepareKick = 8, kOrphan = 16;
constexpr unsigned kInteresting = kFirstEyeRefused | kLagChange | kPrepareKick | kOrphan;
constexpr std::uint32_t kStateUnknown = 0xFF; // CullPrepareGuard.h's kCullStateUnknown

struct Slot {
    std::atomic<std::uint64_t> seq{0}; // index + 1 once written, 0 while being written
    std::atomic<std::int64_t> qpc{0};
    std::atomic<std::uint32_t> thread{0}, step{0}, a{0}, b{0}, c{0}, d{0};
};
struct Event {
    std::uint64_t index = 0;
    std::int64_t qpc = 0;
    std::uint32_t thread = 0, step = 0, a = 0, b = 0, c = 0, d = 0;
};
struct Window {
    std::uint64_t start = 0, trigger = 0, end = 0; // event indices, end exclusive
    std::int64_t triggerQpc = 0;
    std::uint64_t firstTick = 0, endTick = 0;
    unsigned kinds = 0;
    bool control = false;
};

std::array<Slot, kRingSize> gRing;
std::atomic<std::uint64_t> gNext{0};
std::array<std::atomic<std::uint64_t>, kTickHistory> gTickIndex{};
std::atomic<std::uint64_t> gTicks{0};
std::atomic<DWORD> gMainThread{0}, gRenderThread{0};

std::mutex gWindowMutex;
Window gOpen{};                       // guarded by gWindowMutex
std::atomic<bool> gWindowOpen{false};
constexpr std::size_t kReadySlots = 4;
std::array<Window, kReadySlots> gReady{}; // guarded by gWindowMutex
unsigned gReadyCount = 0;             // guarded by gWindowMutex

std::atomic<bool> gRecording{false}, gStartAttempted{false};
std::atomic<unsigned long long> gTriggers{0}, gWritten{0}, gControls{0}, gSuppressed{0}, gLost{0};
std::atomic<unsigned long long> gEmitted{0};

std::mutex gInstallMutex;
bool gHooksTried = false;
std::atomic<bool> gConsumerHooked{false}, gPassHooked{false};
std::atomic<ConsumerJobFn> gConsumerOriginal{nullptr};
std::atomic<std::uintptr_t> gBase{0};

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_frame_probe " + line);
}

std::int64_t Qpc()
{
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

std::int64_t QpcMicros(std::int64_t ticks)
{
    static const std::int64_t frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? f.QuadPart : 1;
    }();
    return ticks / frequency * 1000000 + ticks % frequency * 1000000 / frequency;
}

// Seqlock read: the slot must still hold `index` after the copy.
bool ReadEvent(std::uint64_t index, Event& out)
{
    const Slot& slot = gRing[index % kRingSize];
    if (slot.seq.load(std::memory_order_acquire) != index + 1) {
        return false;
    }
    out.index = index;
    out.qpc = slot.qpc.load(std::memory_order_relaxed);
    out.thread = slot.thread.load(std::memory_order_relaxed);
    out.step = slot.step.load(std::memory_order_relaxed);
    out.a = slot.a.load(std::memory_order_relaxed);
    out.b = slot.b.load(std::memory_order_relaxed);
    out.c = slot.c.load(std::memory_order_relaxed);
    out.d = slot.d.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    return slot.seq.load(std::memory_order_relaxed) == index + 1;
}

// Caller holds gWindowMutex. Queues the open window, or counts it suppressed.
void Finish(std::uint64_t end)
{
    gOpen.end = end;
    const bool interesting = (gOpen.kinds & kInteresting) != 0;
    gOpen.control = !interesting;
    auto& quota = interesting ? gWritten : gControls;
    const auto limit = interesting ? kMaxWindows : kControlWindows;
    if (quota.load(std::memory_order_relaxed) < limit && gReadyCount < kReadySlots) {
        quota.fetch_add(1, std::memory_order_relaxed);
        gReady[gReadyCount++] = gOpen;
    } else {
        gSuppressed.fetch_add(1, std::memory_order_relaxed);
    }
    gOpen = {};
    gWindowOpen.store(false, std::memory_order_release);
}

void Open(unsigned kind, std::uint64_t index, std::int64_t qpc)
{
    std::lock_guard lock(gWindowMutex);
    gTriggers.fetch_add(1, std::memory_order_relaxed);
    const std::uint64_t ticks = gTicks.load(std::memory_order_acquire);
    if (gWindowOpen.load(std::memory_order_relaxed)) {
        // Triggers that follow (the refusal 1 ms after the close, the lag 2 ms
        // after that) join the window, which stays bounded.
        gOpen.kinds |= kind;
        gOpen.endTick = (std::min)((std::max)(gOpen.endTick, ticks + kAfterTicks), gOpen.firstTick + kMaxWindowTicks);
        return;
    }
    gOpen = {};
    std::uint64_t start = ticks > kBeforeTicks
        ? gTickIndex[(ticks - kBeforeTicks) % kTickHistory].load(std::memory_order_relaxed) : 0;
    // Never older than the ring still holds once the window has run its course.
    const std::uint64_t reach = kRingSize / 2;
    if (index > reach && start < index - reach) start = index - reach;
    gOpen.start = (std::min)(start, index);
    gOpen.trigger = index;
    gOpen.triggerQpc = qpc;
    gOpen.firstTick = ticks;
    gOpen.endTick = ticks + kAfterTicks;
    gOpen.kinds = kind;
    gWindowOpen.store(true, std::memory_order_release);
}

void CloseIfDue(std::uint64_t ticks, std::uint64_t index)
{
    std::lock_guard lock(gWindowMutex);
    if (gWindowOpen.load(std::memory_order_relaxed) && ticks >= gOpen.endTick) {
        Finish(index + 1);
    }
}

const char* StepName(std::uint32_t step)
{
    static constexpr const char* names[] = {"tick", "render_enter", "render_exit", "pass_created",
        "consumer_enter", "consumer_exit", "scene_enter", "scene_exit", "end_frame", "panel_open",
        "panel_close", "first_eye_refused", "lag_change", "prepare", "prepare_kick", "orphan"};
    return step < std::size(names) ? names[step] : "unknown";
}

void AppendState(std::ostringstream& line, std::uint32_t state)
{
    if (state == kStateUnknown) line << '?';
    else line << state;
}

// PrepareOcclusion's gates (CullPrepareGuard.cpp): r render tasks, w world, c camera; u unread.
std::string Gates(std::uint32_t gates)
{
    if ((gates & 8) != 0) return "u";
    std::string out;
    out += (gates & 1) != 0 ? 'r' : '-';
    out += (gates & 2) != 0 ? 'w' : '-';
    out += (gates & 4) != 0 ? 'c' : '-';
    return out;
}

// The guard's record of what PrepareOcclusion did before this render.
const char* LastPrepare(std::uint32_t last)
{
    if ((last & 0x100) == 0) return "none";
    const std::uint32_t gates = last & 0xF;
    if ((gates & 8) != 0) return "unread";
    return (gates & 7) == 7 ? "ran" : "skipped";
}

const char* SceneName(std::uint32_t outcome)
{
    static constexpr const char* names[] = {"single", "doubled", "build_miss", "refused", "no_teardown"};
    return outcome < std::size(names) ? names[outcome] : "unknown";
}

std::string Kinds(unsigned kinds)
{
    std::string out;
    const auto add = [&](unsigned bit, const char* name) {
        if ((kinds & bit) == 0) return;
        if (!out.empty()) out += '+';
        out += name;
    };
    add(kPanelClose, "panel_close");
    add(kFirstEyeRefused, "first_eye_refused");
    add(kLagChange, "lag_change");
    add(kPrepareKick, "prepare_kick");
    add(kOrphan, "orphan");
    return out.empty() ? "none" : out;
}

std::string FormatEvent(unsigned long long window, const Event& e, std::int64_t triggerQpc, DWORD main, DWORD render)
{
    std::ostringstream line;
    line << "w=" << window << ' ' << (e.qpc >= triggerQpc ? "+" : "-")
         << QpcMicros(e.qpc >= triggerQpc ? e.qpc - triggerQpc : triggerQpc - e.qpc) << "us ";
    if (e.thread == main) line << 'M';
    else if (e.thread == render) line << 'R';
    else line << 'J' << e.thread;
    line << ' ' << StepName(e.step);
    switch (static_cast<ProbeStep>(e.step)) {
    case ProbeStep::tick:
        line << " depth=";
        if (e.a == kLagUnknown) line << '?';
        else line << e.a;
        line << " pending=" << e.b << " q=" << e.c << '/' << e.d;
        break;
    case ProbeStep::renderEnter:
    case ProbeStep::renderExit:
        line << " state=";
        AppendState(line, e.a);
        line << " q=" << e.c << '/' << e.d;
        break;
    case ProbeStep::passCreated:
        line << " frame=" << e.a << '/' << e.b << " caller=0x" << std::hex << e.c << " flags=0x" << e.d << std::dec;
        break;
    case ProbeStep::consumerEnter:
    case ProbeStep::consumerExit:
        line << " frame=" << e.a << '/' << e.b << " q=" << e.c << '/' << e.d;
        break;
    case ProbeStep::sceneEnter:
        line << " type=" << e.a;
        break;
    case ProbeStep::sceneExit:
        line << " outcome=" << SceneName(e.a) << " type=" << e.b;
        break;
    case ProbeStep::endFrame:
        line << " frame=" << e.a;
        break;
    case ProbeStep::firstEyeRefused:
        line << " refused=" << e.a;
        break;
    case ProbeStep::lagChange:
        line << " depth=" << e.a << " was=" << e.b << " pending=" << e.c;
        break;
    case ProbeStep::prepare:
    case ProbeStep::prepareKick:
    case ProbeStep::orphan:
        line << " state=";
        AppendState(line, e.a);
        line << "->";
        AppendState(line, e.b);
        if (static_cast<ProbeStep>(e.step) == ProbeStep::orphan) line << " guard=" << e.c;
        else line << " gates=" << Gates(e.c);
        if (static_cast<ProbeStep>(e.step) != ProbeStep::prepare) line << " last=" << LastPrepare(e.d);
        break;
    default:
        break;
    }
    return line.str();
}

// Command worker: one finished window to the log.
void Emit(const Window& window)
{
    const auto n = gEmitted.fetch_add(1, std::memory_order_relaxed) + 1;
    const DWORD main = gMainThread.load(std::memory_order_relaxed);
    const DWORD render = gRenderThread.load(std::memory_order_relaxed);
    std::uint64_t start = window.start;
    if (window.end > start + kRingSize) start = window.end - kRingSize;
    std::ostringstream header;
    header << "window=" << n << " kinds=" << Kinds(window.kinds) << " control=" << (window.control ? 1 : 0)
           << " events=" << window.end - start << " before=" << kBeforeTicks << " after=" << kAfterTicks
           << " main=" << main << " render=" << render << " J=job/other thread";
    Log(header.str());
    unsigned long long lost = 0;
    for (std::uint64_t index = start; index < window.end; ++index) {
        Event e;
        if (!ReadEvent(index, e)) {
            ++lost;
            continue;
        }
        Log(FormatEvent(n, e, window.triggerQpc, main, render) + (index == window.trigger ? " <trigger" : ""));
    }
    gLost.fetch_add(lost, std::memory_order_relaxed);
    Log("window=" + std::to_string(n) + " end lost=" + std::to_string(lost));
}

void __fastcall ConsumerJobDetour(void* params)
{
    const auto original = gConsumerOriginal.load(std::memory_order_acquire);
    const bool recording = gRecording.load(std::memory_order_relaxed);
    std::uint32_t frame = 0, frame2 = 0, write = 0, read = 0;
    if (recording) {
        const auto* bytes = static_cast<const std::uint8_t*>(params);
        SafeRead(bytes + kJobFrameId, frame);
        SafeRead(bytes + kJobFrameId2, frame2);
        CullQueueIndices(write, read);
        ProbeRecord(ProbeStep::consumerEnter, frame, frame2, write, read);
    }
    if (original != nullptr) {
        original(params);
    }
    if (recording) {
        CullQueueIndices(write, read);
        ProbeRecord(ProbeStep::consumerExit, frame, frame2, write, read);
    }
}

// Caller holds gInstallMutex. Tried once; recording does not depend on these,
// the other seams record either way.
void InstallHooks()
{
    if (gHooksTried) {
        return;
    }
    gHooksTried = true;
    const HMODULE module = GetModuleHandleW(L"PreyDll.dll");
    if (module == nullptr) {
        Log("result=unavailable detail=preydll_not_loaded hooks=none");
        return;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    gBase.store(base, std::memory_order_relaxed);
    if (hook::HookAt(reinterpret_cast<void*>(base + kConsumerJobRva), kConsumerJobCode,
                     reinterpret_cast<void*>(&ConsumerJobDetour), gConsumerOriginal, "cull_consumer_job", &Log)) {
        gConsumerHooked.store(true, std::memory_order_release);
    }
    // Shared with PassCameraProbe, which owns it and logs its own failure.
    if (EnsurePassCreationHook()) {
        gPassHooked.store(true, std::memory_order_release);
    }
}
} // namespace

void ProbePassCreated(const void* out, std::uint32_t flags, const void* returnAddress)
{
    if (!gRecording.load(std::memory_order_relaxed) || out == nullptr) {
        return;
    }
    std::uint32_t frame = 0, frame2 = 0;
    SafeRead(static_cast<const std::uint8_t*>(out) + kPassFrameId, frame);
    SafeRead(static_cast<const std::uint8_t*>(out) + kPassFrameId2, frame2);
    const auto from = reinterpret_cast<std::uintptr_t>(returnAddress);
    const auto base = gBase.load(std::memory_order_relaxed);
    const std::uint32_t caller = base != 0 && from >= base && from - base < 0x10000000
        ? static_cast<std::uint32_t>(from - base) : kCallerOutsidePreyDll;
    ProbeRecord(ProbeStep::passCreated, frame, frame2, caller, flags);
}

void ProbeRecord(ProbeStep step, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d)
{
    if (!gRecording.load(std::memory_order_relaxed)) {
        return;
    }
    const std::int64_t qpc = Qpc();
    const DWORD thread = GetCurrentThreadId();
    const std::uint64_t index = gNext.fetch_add(1, std::memory_order_relaxed);
    Slot& slot = gRing[index % kRingSize];
    slot.seq.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    slot.qpc.store(qpc, std::memory_order_relaxed);
    slot.thread.store(thread, std::memory_order_relaxed);
    slot.step.store(static_cast<std::uint32_t>(step), std::memory_order_relaxed);
    slot.a.store(a, std::memory_order_relaxed);
    slot.b.store(b, std::memory_order_relaxed);
    slot.c.store(c, std::memory_order_relaxed);
    slot.d.store(d, std::memory_order_relaxed);
    slot.seq.store(index + 1, std::memory_order_release);

    switch (step) {
    case ProbeStep::tick: {
        DWORD none = 0;
        gMainThread.compare_exchange_strong(none, thread, std::memory_order_relaxed);
        const std::uint64_t ticks = gTicks.load(std::memory_order_relaxed) + 1;
        gTickIndex[ticks % kTickHistory].store(index, std::memory_order_relaxed);
        gTicks.store(ticks, std::memory_order_release);
        if (gWindowOpen.load(std::memory_order_acquire)) CloseIfDue(ticks, index);
        break;
    }
    case ProbeStep::endFrame: {
        DWORD none = 0;
        gRenderThread.compare_exchange_strong(none, thread, std::memory_order_relaxed);
        break;
    }
    case ProbeStep::panelClose: Open(kPanelClose, index, qpc); break;
    case ProbeStep::firstEyeRefused: Open(kFirstEyeRefused, index, qpc); break;
    case ProbeStep::lagChange: Open(kLagChange, index, qpc); break;
    case ProbeStep::prepareKick: Open(kPrepareKick, index, qpc); break;
    case ProbeStep::orphan: Open(kOrphan, index, qpc); break;
    default: break;
    }
}

DWORD SetFrameProbe(unsigned int enabled)
{
    if (enabled > 1) {
        return 1;
    }
    if (enabled != 0) {
        std::lock_guard lock(gInstallMutex);
        InstallHooks();
    }
    gRecording.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=recording value=") + (enabled != 0 ? "1" : "0") +
        " consumerHook=" + (gConsumerHooked.load() ? "1" : "0") + " passHook=" + (gPassHooked.load() ? "1" : "0") +
        " mutation=none");
    return 0;
}

void ServiceFrameProbe()
{
    if (!gStartAttempted.load(std::memory_order_acquire)) {
        if (ModulePinStatus() == 0) {
            return;
        }
        gStartAttempted.store(true, std::memory_order_release);
        wchar_t setting[8]{};
        GetEnvironmentVariableW(L"PREYVR_FRAME_PROBE", setting, 8);
        if (setting[0] == L'0') {
            Log("result=0 detail=off reason=PREYVR_FRAME_PROBE");
            return;
        }
        SetFrameProbe(1);
    }
    std::array<Window, kReadySlots> ready{};
    unsigned count = 0;
    {
        std::lock_guard lock(gWindowMutex);
        if (gWindowOpen.load(std::memory_order_relaxed) &&
            QpcMicros(Qpc() - gOpen.triggerQpc) > kWindowTimeoutUs) {
            Finish(gNext.load(std::memory_order_relaxed));
        }
        for (; count < gReadyCount; ++count) ready[count] = gReady[count];
        gReadyCount = 0;
    }
    for (unsigned i = 0; i < count; ++i) Emit(ready[i]);
}

std::string FrameProbeReport()
{
    const auto n = [](const std::atomic<unsigned long long>& value) { return value.load(std::memory_order_relaxed); };
    std::ostringstream out;
    out << " recording=" << (gRecording.load() ? 1 : 0) << " consumerHook=" << (gConsumerHooked.load() ? 1 : 0)
        << " passHook=" << (gPassHooked.load() ? 1 : 0) << " events=" << gNext.load() << " ticks=" << gTicks.load()
        << " triggers=" << n(gTriggers) << " windows=" << n(gWritten) << " controls=" << n(gControls)
        << " suppressed=" << n(gSuppressed) << " emitted=" << n(gEmitted) << " lost=" << n(gLost)
        << " open=" << (gWindowOpen.load() ? 1 : 0) << " main=" << gMainThread.load()
        << " render=" << gRenderThread.load();
    return out.str();
}
} // namespace preyvr::dll
