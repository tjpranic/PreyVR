#include "render/taa/TemporalAaLane.h"
#include "engine/EngineAccess.h"
#include "engine/EngineTexture.h"
#include "base/Logger.h"
#include "engine/RendererAccess.h"
#include "engine/SafeMemory.h"
#include "engine/NativeContract.h"
#include "render/taa/TemporalAa.h"
#include "render/taa/TemporalAaNative.h"
#include "render/taa/WorldMotion.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

namespace preyvr::dll {
namespace {
namespace tn = taa::native;
using Microsoft::WRL::ComPtr;
static_assert(tn::TextureDevice == kCTextureDevice);
static_assert(tn::HistorySlots == 2);
constexpr std::size_t kPrevViewsSize = tn::PrevViewSlots * tn::PrevViewSize;

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_taa_lane " + line);
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

// CRenderView::SetPreviousFrameCamera (gated above). Tests replace it here.
using SetPreviousCameraFn = void(__fastcall*)(void* view, const void* camera);
void SetPreviousCamera(void* view, const void* camera)
{
    reinterpret_cast<SetPreviousCameraFn>(gBase.load(std::memory_order_relaxed) + tn::SetPreviousFrameCamera)(view, camera);
}
void (*gSetPreviousCamera)(void*, const void*) = SetPreviousCamera;

// --- what is counted ------------------------------------------------------------------

using Count = std::atomic<unsigned long long>;
// On by default, as the menu's anti-aliasing is (Temporal); vr_options_wiring pins it.
std::atomic<bool> gEnabled{true};
std::atomic<bool> gActive{false};
Count gFrames{0}, gStarts{0}, gCopiesIn{0}, gCopiesOut{0}, gFreshStarts{0};
Count gResolveMissed{0}, gCounterOff{0}, gIdleMode{0}, gViewsIn{0};
Count gRefusedGate{0}, gRefusedStage{0}, gRefusedTextures{0}, gRefusedDevice{0};
std::atomic<const char*> gLastStart{"none"};
std::atomic<std::uint32_t> gCounterFirst{0}, gCounterSecond{0};
std::atomic<std::uint32_t> gSavedWidth{0}, gSavedHeight{0}, gSavedFormat{0};
std::atomic<bool> gMotionOn{true};
// V1^-1 x V2: written with gMotionReady false, and read only while it is true.
std::atomic<bool> gMotionReady{false};
float gMotion[16]{};
Count gMotionGiven{0}, gMotionPackedCount{0}, gMotionUnshaped{0}, gMotionNoViews{0}, gMotionBadViews{0}, gMotionPerDraw{0};
std::atomic<int> gPipelineLatched{-1}, gSeparationUm{0};

// --- render thread only -------------------------------------------------------------

struct Eye {
    // The counter this eye's next submission runs with, and the one this frame's ran with.
    std::uint32_t counter = 0, counterIn = 0;
    // `saved` holds this eye's last resolve output.
    bool fresh = false;
    bool havePrevViewProj = false, havePrevCamera = false, havePrevViews = false;
    std::uint8_t prevViewProj[tn::ViewProjSize]{};
    // The renderer's previous views (all viewports) as this eye's submission left
    // them: moving objects' motion vectors are taken against them.
    std::uint8_t prevViews[kPrevViewsSize]{};
    ComPtr<ID3D11Texture2D> saved;
};
std::array<Eye, 2> gEye;
// Each eye's camera from its last submission, aligned as the engine's own camera copies are.
alignas(16) std::uint8_t gPrevCamera[2][tn::CameraSize]{};
// Why the next applied frame must start afresh, or null while frames follow on.
const char* gRestart = "enable";
bool gFrameOn = false;        // this frame is being applied
unsigned long long gLastFrame = 0;
std::uintptr_t gStage = 0;
std::array<std::uintptr_t, 2> gHistoryCTexture{};
std::array<ID3D11Texture2D*, 2> gHistory{};   // the engine's, not held: re-read each frame
D3D11_TEXTURE2D_DESC gSavedDesc{};
ComPtr<ID3D11DeviceContext> gContext;         // this frame's
bool gHaveCentre = false;
alignas(16) std::uint8_t gCentrePrevCamera[tn::CameraSize]{};
// The first eye's previous view, which this frame's objects compile against.
float gCompiledView[16]{};
bool gHaveCompiledView = false;

void Bump(Count& count)
{
    count.fetch_add(1, std::memory_order_relaxed);
}

void ReleaseSaved()
{
    for (auto& eye : gEye) { eye.saved.Reset(); }
    gSavedDesc = {};
    gHistory = {};
    gHistoryCTexture = {};
    gSavedWidth.store(0, std::memory_order_relaxed);
    gSavedHeight.store(0, std::memory_order_relaxed);
    gSavedFormat.store(0, std::memory_order_relaxed);
}

// Not applying: the next applied frame starts afresh, for `reason` unless one is pending.
void Idle(const char* reason)
{
    if (gRestart == nullptr) { gRestart = reason; }
    gActive.store(false, std::memory_order_relaxed);
}

// The engine's two history textures, checked, and this lane's two of the same
// description on the renderer's device. False when they cannot be used.
bool AdoptHistory(void* renderer, const std::array<std::uintptr_t, 2>& ctexture)
{
    std::array<D3D11_TEXTURE2D_DESC, 2> desc{};
    std::array<ID3D11Texture2D*, 2> texture{};
    for (std::size_t i = 0; i < 2; ++i) {
        texture[i] = EngineTexture2D(ctexture[i], desc[i]);
        if (texture[i] == nullptr) {
            Bump(gRefusedTextures);
            return false;
        }
    }
    if (std::memcmp(&desc[0], &desc[1], sizeof(desc[0])) != 0 || desc[0].ArraySize != 1 || desc[0].SampleDesc.Count != 1) {
        Bump(gRefusedTextures);
        return false;
    }
    ID3D11Device* device = RendererDevice(renderer);
    ComPtr<ID3D11Device> owner;
    texture[0]->GetDevice(&owner);
    if (device == nullptr || owner.Get() != device) {
        Bump(gRefusedDevice);
        return false;
    }
    if (std::memcmp(&desc[0], &gSavedDesc, sizeof(desc[0])) != 0 || !gEye[0].saved || !gEye[1].saved) {
        D3D11_TEXTURE2D_DESC own = desc[0];
        own.Usage = D3D11_USAGE_DEFAULT;
        own.BindFlags = 0;
        own.CPUAccessFlags = 0;
        own.MiscFlags = 0;
        ReleaseSaved();
        for (auto& eye : gEye) {
            if (FAILED(device->CreateTexture2D(&own, nullptr, &eye.saved))) {
                ReleaseSaved();
                Bump(gRefusedDevice);
                Log("result=failed detail=create_history width=" + std::to_string(own.Width));
                return false;
            }
        }
        gSavedDesc = desc[0];
        gSavedWidth.store(own.Width, std::memory_order_relaxed);
        gSavedHeight.store(own.Height, std::memory_order_relaxed);
        gSavedFormat.store(static_cast<std::uint32_t>(own.Format), std::memory_order_relaxed);
        Log("history width=" + std::to_string(own.Width) + " height=" + std::to_string(own.Height) +
            " format=" + std::to_string(own.Format));
    }
    gHistory = texture;
    gHistoryCTexture = ctexture;
    return true;
}

// The previous view of the renderer's viewport, as 0xFE5560 picks it.
bool ReadPrevView(const void* renderer, float* view)
{
    int viewport = -1;
    const auto* bytes = static_cast<const std::uint8_t*>(renderer);
    if (!SafeRead(bytes + tn::RendererViewport, viewport) || viewport < 0 ||
        viewport >= static_cast<int>(tn::PrevViewSlots)) {
        return false;
    }
    return SafeCopy(view, bytes + tn::RendererPrevViews + viewport * tn::PrevViewSize, tn::PrevViewSize);
}

// Puts up what takes this frame's objects from the first eye's previous view to
// the second eye's, or leaves it down for this submission.
void OfferSecondEyeMotion(const void* renderer, const Eye& second)
{
    if (!gMotionOn.load(std::memory_order_relaxed)) { return; }
    int pipeline = -1;
    SafeRead(static_cast<const std::uint8_t*>(renderer) + tn::RendererGraphicsPipeline, pipeline);
    gPipelineLatched.store(pipeline, std::memory_order_relaxed);
    if (pipeline < tn::CompiledOncePipeline) {
        Bump(gMotionPerDraw);
        return;
    }
    float own[16];
    if (!gHaveCompiledView || !second.havePrevViews || !ReadPrevView(renderer, own)) {
        Bump(gMotionNoViews);
        return;
    }
    if (!worldmotion::Between(gCompiledView, own, gMotion)) {
        Bump(gMotionBadViews);
        return;
    }
    gSeparationUm.store(static_cast<int>(std::lround(worldmotion::Separation(gMotion) * 1.0e6)), std::memory_order_relaxed);
    Bump(gMotionGiven);
    gMotionReady.store(true, std::memory_order_release);
}

// Both eyes from the engine's counter, neither with history.
void Start(std::uintptr_t base, const char* reason)
{
    std::uint32_t counter = 0;
    SafeRead(base + tn::FrameCounter, counter);
    const auto counters = taa::StartCounters(counter);
    gEye[0].counter = counters.first;
    gEye[1].counter = counters.second;
    for (auto& eye : gEye) { eye.fresh = eye.havePrevViewProj = eye.havePrevCamera = eye.havePrevViews = false; }
    gLastStart.store(reason, std::memory_order_relaxed);
    const auto starts = gStarts.fetch_add(1, std::memory_order_relaxed) + 1;
    if (starts <= 20 || starts % 100 == 0) {
        Log("start reason=" + std::string(reason) + " counter=" + std::to_string(counter) + " starts=" +
            std::to_string(starts));
    }
}

} // namespace

void TaaLaneFrameBegin(void* renderer, void* view, unsigned long long frame)
{
    gFrameOn = false;
    if (!gEnabled.load(std::memory_order_acquire)) {
        if (gEye[0].saved) { ReleaseSaved(); }
        gRestart = "enable";
        gActive.store(false, std::memory_order_relaxed);
        return;
    }
    const std::uintptr_t base = Base();
    if (base == 0 || renderer == nullptr || view == nullptr) {
        Bump(gRefusedGate);
        Idle("refused");
        return;
    }
    int mode = 0;
    if (!SafeRead(base + tn::ModeEffective, mode) || !taa::RunsResolve(mode)) {
        Bump(gIdleMode);
        Idle("mode");
        return;
    }
    std::uintptr_t pipeline = 0, stage = 0;
    if (!SafeRead(base + tn::Pipeline, pipeline) || pipeline == 0 ||
        !SafeRead(pipeline + tn::PipelineAaStage, stage) || stage == 0) {
        Bump(gRefusedStage);
        Idle("refused");
        return;
    }
    std::array<std::uintptr_t, 2> ctexture{};
    for (std::size_t i = 0; i < 2; ++i) { SafeRead(base + tn::History + i * sizeof(std::uintptr_t), ctexture[i]); }
    const char* start = gRestart != nullptr ? gRestart : frame != gLastFrame + 1 ? "gap" : nullptr;
    // Re-read every frame: the engine recreates its targets on a resize.
    std::array<ID3D11Texture2D*, 2> live{};
    for (std::size_t i = 0; i < 2; ++i) {
        std::uintptr_t device = 0, texture = 0;
        if (SafeRead(ctexture[i] + tn::TextureDevice, device) && device != 0) { SafeRead(device, texture); }
        live[i] = reinterpret_cast<ID3D11Texture2D*>(texture);
    }
    if (ctexture != gHistoryCTexture || live != gHistory || !gEye[0].saved) {
        if (!AdoptHistory(renderer, ctexture)) {
            gHistory = {};
            gHistoryCTexture = {};
            Idle("refused");
            return;
        }
        if (start == nullptr) { start = "textures"; }
    }
    gContext = ImmediateContext(RendererDevice(renderer));
    if (!gContext) {
        Bump(gRefusedDevice);
        Idle("refused");
        return;
    }
    if (start != nullptr) { Start(base, start); }
    gStage = stage;
    gHaveCentre = SafeCopy(gCentrePrevCamera, static_cast<const std::uint8_t*>(view) + tn::ViewPreviousCamera,
                           tn::CameraSize);
    gRestart = nullptr;
    gLastFrame = frame;
    gFrameOn = true;
    gActive.store(true, std::memory_order_relaxed);
}

void TaaLaneEyeBegin(void* renderer, void* view, int eye, const void* eyeCamera)
{
    if (!gFrameOn || (eye != 0 && eye != 1) || eyeCamera == nullptr) { return; }
    auto& e = gEye[static_cast<std::size_t>(eye)];
    const std::uintptr_t base = gBase.load(std::memory_order_relaxed);
    e.counterIn = e.counter;
    SafeCopy(reinterpret_cast<void*>(base + tn::FrameCounter), &e.counter, sizeof(e.counter));
    gSetPreviousCamera(view, e.havePrevCamera ? static_cast<const void*>(gPrevCamera[eye]) : eyeCamera);
    if (e.havePrevViewProj) {
        SafeCopy(static_cast<std::uint8_t*>(renderer) + tn::RendererPrevViewProj, e.prevViewProj, tn::ViewProjSize);
    }
    if (e.havePrevViews) {
        SafeCopy(static_cast<std::uint8_t*>(renderer) + tn::RendererPrevViews, e.prevViews, kPrevViewsSize);
        Bump(gViewsIn);
    }
    if (eye == 0) {
        gHaveCompiledView = ReadPrevView(renderer, gCompiledView);
    } else {
        OfferSecondEyeMotion(renderer, e);
    }
    // The resolve's stamp says whether it uses history: this eye's, or none yet
    // (it starts from this frame).
    const std::uint32_t stamp = e.fresh ? taa::FreshStamp(e.counter) : taa::StaleStamp(e.counter);
    SafeCopy(reinterpret_cast<void*>(gStage + tn::StageLastResolve), &stamp, sizeof(stamp));
    if (e.fresh) {
        gContext->CopyResource(gHistory[taa::HistoryIn(e.counter)], e.saved.Get());
        Bump(gCopiesIn);
    } else {
        Bump(gFreshStarts);
    }
}

void TaaLaneEyeEnd(void* renderer, int eye, const void* eyeCamera)
{
    gMotionReady.store(false, std::memory_order_release);
    if (!gFrameOn || (eye != 0 && eye != 1) || eyeCamera == nullptr) { return; }
    auto& e = gEye[static_cast<std::size_t>(eye)];
    const std::uintptr_t base = gBase.load(std::memory_order_relaxed);
    std::uint32_t after = 0, stamp = 0;
    SafeRead(base + tn::FrameCounter, after);
    if (after != taa::Advance(e.counterIn)) { Bump(gCounterOff); }
    e.counter = taa::Advance(e.counterIn);
    // The resolve ran with this eye's counter: its output is this eye's next history.
    if (SafeRead(gStage + tn::StageLastResolve, stamp) && stamp == e.counterIn) {
        gContext->CopyResource(e.saved.Get(), gHistory[taa::HistoryOut(e.counterIn)]);
        Bump(gCopiesOut);
        e.fresh = true;
    } else {
        Bump(gResolveMissed);
        e.fresh = false;
    }
    e.havePrevViewProj =
        SafeCopy(e.prevViewProj, static_cast<const std::uint8_t*>(renderer) + tn::RendererPrevViewProj, tn::ViewProjSize);
    e.havePrevViews =
        SafeCopy(e.prevViews, static_cast<const std::uint8_t*>(renderer) + tn::RendererPrevViews, kPrevViewsSize);
    e.havePrevCamera = SafeCopy(gPrevCamera[eye], eyeCamera, tn::CameraSize);
}

void TaaLaneFrameEnd(void* view, bool complete)
{
    if (!gFrameOn) { return; }
    gFrameOn = false;
    // The view's own previous camera again, for whatever the frame draws next.
    if (gHaveCentre) { gSetPreviousCamera(view, gCentrePrevCamera); }
    gContext.Reset();
    gCounterFirst.store(gEye[0].counter, std::memory_order_relaxed);
    gCounterSecond.store(gEye[1].counter, std::memory_order_relaxed);
    if (!complete) {
        gRestart = "incomplete";
        return;
    }
    Bump(gFrames);
}

DWORD SetTaaLane(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    if (gEnabled.exchange(enabled != 0, std::memory_order_acq_rel) != (enabled != 0)) {
        Log(std::string("enabled=") + (enabled != 0 ? "1" : "0"));
    }
    return ERROR_SUCCESS;
}

bool TaaLaneEnabled()
{
    return gEnabled.load(std::memory_order_acquire);
}

DWORD SetTaaSecondEyeMotion(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    if (gMotionOn.exchange(enabled != 0, std::memory_order_relaxed) != (enabled != 0)) {
        Log(std::string("second_eye_motion=") + (enabled != 0 ? "1" : "0"));
    }
    return ERROR_SUCCESS;
}

bool TaaLaneSecondEyeMotion(float* relative)
{
    if (!gMotionReady.load(std::memory_order_acquire)) { return false; }
    std::memcpy(relative, gMotion, sizeof(gMotion));
    return true;
}

void TaaLaneMotionPacked(bool moved)
{
    Bump(moved ? gMotionPackedCount : gMotionUnshaped);
}

std::string TaaLaneReport()
{
    std::ostringstream s;
    s << "taaLane={enabled=" << (gEnabled.load() ? 1 : 0) << " active=" << (gActive.load() ? 1 : 0)
      << " gate=" << GateName() << " frames=" << gFrames.load() << " starts=" << gStarts.load()
      << " lastStart=" << gLastStart.load() << " copiesIn=" << gCopiesIn.load() << " copiesOut=" << gCopiesOut.load()
      << " viewsIn=" << gViewsIn.load() << " freshStarts=" << gFreshStarts.load() << " resolveMissed=" << gResolveMissed.load()
      << " counterOff=" << gCounterOff.load() << " idleMode=" << gIdleMode.load() << " refused=gate:"
      << gRefusedGate.load() << ",stage:" << gRefusedStage.load() << ",textures:" << gRefusedTextures.load()
      << ",device:" << gRefusedDevice.load() << " motion={on=" << (gMotionOn.load() ? 1 : 0)
      << " pipeline=" << gPipelineLatched.load() << " given=" << gMotionGiven.load() << " packed=" << gMotionPackedCount.load()
      << " unshaped=" << gMotionUnshaped.load() << " noViews=" << gMotionNoViews.load() << " badViews=" << gMotionBadViews.load()
      << " perDraw=" << gMotionPerDraw.load() << " separationUm=" << gSeparationUm.load() << "} counters=" << gCounterFirst.load()
      << ',' << gCounterSecond.load() << " history=";
    if (gSavedWidth.load() == 0) {
        s << "none";
    } else {
        s << gSavedWidth.load() << 'x' << gSavedHeight.load() << " format=" << gSavedFormat.load();
    }
    s << '}';
    return s.str();
}

} // namespace preyvr::dll
