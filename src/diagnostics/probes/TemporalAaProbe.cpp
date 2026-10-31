#include "diagnostics/probes/TemporalAaProbe.h"
#include "engine/EngineAccess.h"
#include "engine/EngineTexture.h"
#include "base/Logger.h"
#include "engine/SafeMemory.h"
#include "engine/NativeContract.h"
#include "render/taa/TemporalAaNative.h"

#include <windows.h>
#include <d3d11.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace preyvr::dll {
namespace {
namespace tn = taa::native;

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_taa " + line);
}

// --- the gate -------------------------------------------------------------------------

enum class Gate : int { untried = 0, ok = 1, refused = 2 };
std::atomic<int> gGate{0};
std::atomic<std::uintptr_t> gBase{0};

bool GateBodies(std::uintptr_t base)
{
    __try {
        return contract::BodiesMatch(base, tn::Contracts);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// PreyDll's base, once it is loaded and the bodies are the ones read:
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

// Render thread only, so the first resolve has no race. A refusal stays refused.
std::uintptr_t Base()
{
    const int gate = gGate.load(std::memory_order_acquire);
    if (gate == static_cast<int>(Gate::ok)) { return gBase.load(std::memory_order_relaxed); }
    if (gate == static_cast<int>(Gate::refused)) { return 0; }
    std::uintptr_t base = 0;
    const DWORD ready = gResolve(base);
    if (ready == ERROR_NOT_READY) { return 0; }
    if (ready != ERROR_SUCCESS) {
        gGate.store(static_cast<int>(Gate::refused), std::memory_order_release);
        Log("result=unavailable detail=bytes_mismatch rows=" + std::to_string(std::size(tn::Contracts)));
        return 0;
    }
    gBase.store(base, std::memory_order_relaxed);
    gGate.store(static_cast<int>(Gate::ok), std::memory_order_release);
    Log("result=0 detail=gate_ok rows=" + std::to_string(std::size(tn::Contracts)));
    return base;
}

const char* GateName()
{
    switch (static_cast<Gate>(gGate.load(std::memory_order_acquire))) {
    case Gate::ok: return "ok";
    case Gate::refused: return "refused";
    case Gate::untried: break;
    }
    return "untried";
}

// --- what is counted ------------------------------------------------------------------

// The counter is kept in 24 bits, so a difference is too.
constexpr std::uint32_t kCounterMask = 0xFFFFFF;

using Count = std::atomic<unsigned long long>;

struct KindStats {
    Count n{0};
    // How far the counter moved across the submission.
    Count delta0{0}, delta1{0}, deltaOther{0};
    // The renderer's previous view-projection changed across it.
    Count prevViewProjChanged{0};
    // The AA stage's last resolve ran with the counter this submission began with.
    Count stampIsCounter{0};
    // The pipeline's render view afterwards: this submission's view, none, or another.
    Count viewMatch{0}, viewNull{0}, viewOther{0};
    Count jittered{0};
    std::atomic<std::uint32_t> jitterX{0}, jitterY{0};
};
constexpr const char* kKindNames[] = {"single", "first", "second"};
std::array<KindStats, 3> gKinds;

// Per frame: the counter at each frame's first (or only) submission against the last.
Count gFrames{0}, gFrameDelta1{0}, gFrameDelta2{0}, gFrameDeltaOther{0};
std::atomic<std::uint32_t> gLastCounter{0}, gLastStamp{0};
std::atomic<std::uintptr_t> gStage{0};
std::array<std::atomic<std::uintptr_t>, tn::HistorySlots> gHistory{};
Count gHistoryChanges{0};

// Render thread only: the submission in flight and the frame before.
struct Pending {
    bool valid = false;
    std::uint32_t counter = 0;
    bool havePrevViewProj = false;
    std::uint8_t prevViewProj[tn::ViewProjSize]{};
};
Pending gPending;
bool gHaveFrameStart = false;
std::uint32_t gFrameStart = 0;
bool gHistorySeen = false;

// --- the history textures ---------------------------------------------------------------

// The textures last described, and what was found. At most kMaxInspections
// descriptions, so pointers that churn cannot make this a per-frame cost.
constexpr unsigned kMaxInspections = 8;
std::array<std::uintptr_t, tn::HistorySlots> gInspected{};   // render thread only
unsigned gInspections = 0;                                    // render thread only
std::mutex gTextureMutex;
std::array<std::string, tn::HistorySlots> gTextureText{"none", "none"};

// A history CTexture: its device texture (+0x30), then the device texture itself
// and the pointers at its +0x00/+0x08/+0x10, each named by the module its vtable
// is in. The first that is d3d11.dll's is asked for ID3D11Texture2D; nothing is
// called on any other object.
std::string DescribeHistory(std::uintptr_t texture)
{
    if (texture == 0) { return "null"; }
    std::uintptr_t device = 0;
    if (!SafeRead(texture + tn::TextureDevice, device) || device == 0) { return "device=null"; }
    std::uintptr_t at00 = 0, at08 = 0, at10 = 0;
    SafeRead(device, at00);
    SafeRead(device + 0x08, at08);
    SafeRead(device + 0x10, at10);
    const struct {
        const char* name;
        std::uintptr_t object;
    } candidates[] = {{"self", device}, {"+0x00", at00}, {"+0x08", at08}, {"+0x10", at10}};
    std::ostringstream s;
    s << "device=0x" << std::hex << device << std::dec;
    const char* hop = nullptr;
    D3D11_TEXTURE2D_DESC desc{};
    for (const auto& candidate : candidates) {
        const std::string module = VtableModule(candidate.object);
        s << ' ' << candidate.name << '=' << module;
        if (hop != nullptr || module != "d3d11.dll") { continue; }
        const int answer = QueryTexture2D(candidate.object, desc);
        if (answer == 1) {
            hop = candidate.name;
        } else {
            s << (answer == 0 ? "(not_texture2d)" : "(fault)");
        }
    }
    if (hop == nullptr) {
        s << " hop=none";
        return s.str();
    }
    s << " hop=" << hop << " size=" << desc.Width << 'x' << desc.Height << " format=" << desc.Format
      << " mips=" << desc.MipLevels << " array=" << desc.ArraySize << " samples=" << desc.SampleDesc.Count
      << " bind=0x" << std::hex << desc.BindFlags << std::dec;
    return s.str();
}

void InspectHistory(const std::array<std::uintptr_t, tn::HistorySlots>& history)
{
    if (history == gInspected || gInspections >= kMaxInspections) { return; }
    ++gInspections;
    gInspected = history;
    std::array<std::string, tn::HistorySlots> text;
    for (std::size_t i = 0; i < tn::HistorySlots; ++i) { text[i] = DescribeHistory(history[i]); }
    Log("history inspection=" + std::to_string(gInspections) + " tex0={" + text[0] + "} tex1={" + text[1] + "}");
    std::lock_guard lock(gTextureMutex);
    gTextureText = text;
}

void Bump(Count& count)
{
    count.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

void TaaProbeBefore(const void* renderer, const void* /*view*/, TaaSubmission kind)
{
    gPending.valid = false;
    const std::uintptr_t base = Base();
    std::uint32_t counter = 0;
    if (base == 0 || renderer == nullptr || !SafeRead(base + tn::FrameCounter, counter)) { return; }
    if (kind != TaaSubmission::second) {
        Bump(gFrames);
        if (gHaveFrameStart) {
            const std::uint32_t delta = (counter - gFrameStart) & kCounterMask;
            Bump(delta == 1 ? gFrameDelta1 : delta == 2 ? gFrameDelta2 : gFrameDeltaOther);
        }
        gHaveFrameStart = true;
        gFrameStart = counter;
    }
    gPending.counter = counter;
    gPending.havePrevViewProj = SafeCopy(gPending.prevViewProj,
        static_cast<const std::uint8_t*>(renderer) + tn::RendererPrevViewProj, tn::ViewProjSize);
    gPending.valid = true;
}

void TaaProbeAfter(const void* renderer, const void* view, TaaSubmission kind)
{
    if (!gPending.valid) { return; }
    gPending.valid = false;
    const std::uintptr_t base = gBase.load(std::memory_order_relaxed);
    auto& stats = gKinds[static_cast<std::size_t>(kind)];
    Bump(stats.n);
    const auto* r = static_cast<const std::uint8_t*>(renderer);

    std::uint32_t counter = 0;
    if (SafeRead(base + tn::FrameCounter, counter)) {
        const std::uint32_t delta = (counter - gPending.counter) & kCounterMask;
        Bump(delta == 0 ? stats.delta0 : delta == 1 ? stats.delta1 : stats.deltaOther);
        gLastCounter.store(counter, std::memory_order_relaxed);
    }

    std::uint8_t prevViewProj[tn::ViewProjSize]{};
    if (gPending.havePrevViewProj && SafeCopy(prevViewProj, r + tn::RendererPrevViewProj, tn::ViewProjSize) &&
        std::memcmp(prevViewProj, gPending.prevViewProj, tn::ViewProjSize) != 0) {
        Bump(stats.prevViewProjChanged);
    }

    std::uintptr_t pipeline = 0, stage = 0, pipelineView = 0;
    if (SafeRead(base + tn::Pipeline, pipeline) && pipeline != 0) {
        if (SafeRead(pipeline + tn::PipelineAaStage, stage) && stage != 0) {
            gStage.store(stage, std::memory_order_relaxed);
            std::uint32_t stamp = 0;
            if (SafeRead(stage + tn::StageLastResolve, stamp)) {
                gLastStamp.store(stamp, std::memory_order_relaxed);
                if (stamp == gPending.counter) { Bump(stats.stampIsCounter); }
            }
        }
        if (SafeRead(pipeline + tn::PipelineView, pipelineView)) {
            Bump(pipelineView == reinterpret_cast<std::uintptr_t>(view) ? stats.viewMatch
                 : pipelineView == 0                                    ? stats.viewNull
                                                                        : stats.viewOther);
        }
    }

    float jitter[2]{};
    if (SafeRead(r + tn::RendererJitter, jitter[0]) && SafeRead(r + tn::RendererJitter + 4, jitter[1])) {
        stats.jitterX.store(std::bit_cast<std::uint32_t>(jitter[0]), std::memory_order_relaxed);
        stats.jitterY.store(std::bit_cast<std::uint32_t>(jitter[1]), std::memory_order_relaxed);
        if (jitter[0] != 0.0f || jitter[1] != 0.0f) { Bump(stats.jittered); }
    }

    std::array<std::uintptr_t, tn::HistorySlots> history{};
    bool haveHistory = true;
    for (std::size_t i = 0; i < tn::HistorySlots; ++i) {
        haveHistory = SafeRead(base + tn::History + i * sizeof(std::uintptr_t), history[i]) && haveHistory;
    }
    if (haveHistory) {
        bool changed = false;
        for (std::size_t i = 0; i < tn::HistorySlots; ++i) {
            changed = gHistory[i].exchange(history[i], std::memory_order_relaxed) != history[i] || changed;
        }
        if (changed && gHistorySeen) { Bump(gHistoryChanges); }
        gHistorySeen = true;
        // After a frame's last submission, so a description never lands between eyes.
        if (kind != TaaSubmission::first) { InspectHistory(history); }
    }
}

std::string TaaProbeReport()
{
    const std::uintptr_t base = gBase.load(std::memory_order_relaxed);
    int mode = -1, effective = -1;
    if (gGate.load(std::memory_order_acquire) == static_cast<int>(Gate::ok)) {
        SafeRead(base + tn::ModeCvar, mode);
        SafeRead(base + tn::ModeEffective, effective);
    }
    std::ostringstream s;
    s << "taa={gate=" << GateName() << " mode=" << mode << " effective=" << effective
      << " counter=" << gLastCounter.load() << " frames=" << gFrames.load() << " frameDelta=1:" << gFrameDelta1.load()
      << ",2:" << gFrameDelta2.load() << ",other:" << gFrameDeltaOther.load();
    s << std::setprecision(6);
    for (std::size_t i = 0; i < gKinds.size(); ++i) {
        const auto& k = gKinds[i];
        s << ' ' << kKindNames[i] << "={n=" << k.n.load() << " delta=0:" << k.delta0.load() << ",1:" << k.delta1.load()
          << ",other:" << k.deltaOther.load() << " prevViewProjChanged=" << k.prevViewProjChanged.load()
          << " stampIsCounter=" << k.stampIsCounter.load() << " view=match:" << k.viewMatch.load()
          << ",null:" << k.viewNull.load() << ",other:" << k.viewOther.load() << " jittered=" << k.jittered.load()
          << " jitter=" << std::bit_cast<float>(k.jitterX.load()) << ',' << std::bit_cast<float>(k.jitterY.load())
          << '}';
    }
    s << " stage=0x" << std::hex << gStage.load() << std::dec << " lastResolve=" << gLastStamp.load()
      << " history=0x" << std::hex << gHistory[0].load() << ",0x" << gHistory[1].load() << std::dec
      << " historyChanges=" << gHistoryChanges.load();
    {
        std::lock_guard lock(gTextureMutex);
        s << " tex0={" << gTextureText[0] << "} tex1={" << gTextureText[1] << '}';
    }
    s << '}';
    return s.str();
}

} // namespace preyvr::dll
