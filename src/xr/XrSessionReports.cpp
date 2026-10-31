#include "xr/XrSessionHost.h"
#include "xr/XrSessionState.h"
#include "engine/EngineAccess.h"

#include "xr/FrameTiming.h"
#include "xr/FrustumCoverage.h"

#include "base/Logger.h"
#include "diagnostics/probes/FrameProbe.h"
#include "engine/EngineMap.h"
#include "xr/XrFrameContract.h"
#include "xr/XrHold.h"
#include "engine/hooks/MainRenderHook.h"
#include "render/camera/EyeRecords.h"
#include "render/camera/HeadTrackingHook.h"
#include "controls/input/XrInput.h"
#include "ui/screens/UiMode.h"
#include "ui/screens/UiPointer.h"
#include "ui/hud/HudLayer.h"
#include "xr/LayerSwapchain.h"
#include "engine/RendererAccess.h"
#include "xr/XrPose.h"
#include "ui/screens/LoadingScreen.h"
#include "ui/options/VrOptionsLayer.h"
#include "ui/options/VrOptionsRuntime.h"
#include "equipment/body/BodyEquipmentLane.h"
#include "equipment/body/SlotFeedbackLane.h"
#include "ui/hud/NativeWristTexture.h"
#include "ui/hud/WristLane.h"
#include "aim/AimTakeover.h"
#include "ui/hud/ReticleFollow.h"
#include "diagnostics/overlay/DebugOverlay.h"
#include "ui/screens/UiPanel.h"
#include "base/LatestSnapshot.h"
#include "xr/HudCapturePair.h"
#include "render/camera/StereoCamera.h"
#include "render/camera/StereoFrame.h"
#include "ui/hud/WristHolo.h"
#include "xr/XrSwapchainFormat.h"

#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>
#include <chrono>
#include <mutex>
#include <memory>
#include <string>
#include <sstream>
#include <vector>

// The session's reports: timing (xr.timing), FOV coverage (xr.coverage) and
// the resolution chain (XrSessionState.h).

namespace preyvr::dll {
using namespace xrsession;

unsigned long long XrFadeFrames() { return gFadeFrames.load(std::memory_order_relaxed); }

DWORD SetXrTimingEnabled(unsigned int enabled, unsigned int displayHz)
{
    if (enabled == 0) {
        gTimingEnabled.store(false, std::memory_order_release);
        return 0;
    }
    // Requested, not performed: the clear happens on the render thread at the
    // top of the next service. Arming therefore takes effect one frame later,
    // which is the price of an epoch boundary that cannot straddle a writer.
    gServiceInterval.RequestReset();
    gWaitFrame.RequestReset();
    gAcquireWait.RequestReset();
    gEndFrame.RequestReset();
    gServiceTotal.RequestReset();
    gEyeSkew.RequestReset();
    if (displayHz > 0 && displayHz <= 1000) {
        gServiceInterval.SetBudgetMicroseconds(1000000u / displayHz);
    }
    gTimingEnabled.store(true, std::memory_order_release);
    return 0;
}

std::string XrTimingReport()
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(2);
    out << " timing=" << (gTimingEnabled.load(std::memory_order_acquire) ? 1 : 0);

    const auto emit = [&out](const char* name, const preyvr::timing::DurationStats& d) {
        if (!d.valid) { out << ' ' << name << "=none"; return; }
        // **Window and lifetime, always both.** The percentiles describe at most
        // the last 512 samples -- a few seconds, not a run -- while `Total` is
        // every sample this generation. Emitting only the first is what let a
        // 512-sample window be described as a 30-second one.
        out << ' ' << name << "P50=" << d.p50 << ' ' << name << "P95=" << d.p95
            << ' ' << name << "P99=" << d.p99 << ' ' << name << "Max=" << d.max
            << ' ' << name << "N=" << d.count << ' ' << name << "Total=" << d.total;
    };

    const auto interval = gServiceInterval.Compute();
    emit("frame", interval.duration);
    // **"Over budget", never "missed".** No compositor is consulted: this counts
    // service-to-service intervals longer than a configured threshold. One long
    // interval counts once however many display periods it spans, and a sample a
    // microsecond over counts the same as a stall.
    out << " frameBudgetUs=" << interval.budgetMicroseconds
        << " frameRuntimePeriodUs=" << gRuntimePeriodUs.load(std::memory_order_relaxed)
        << " frameOverBudgetWindow=" << interval.windowOverBudget
        << " frameOverBudgetTotal=" << interval.lifetimeOverBudget
        << " frameWindowSec=" << interval.windowSeconds
        << " frameSessionSec=" << interval.sessionSeconds
        << " timingGeneration=" << interval.generation;

    emit("wait", gWaitFrame.Compute());
    emit("acquire", gAcquireWait.Compute());
    emit("end", gEndFrame.Compute());
    emit("service", gServiceTotal.Compute());

    // **Stages are separate rings and are NOT paired by frame.** Subtracting one
    // stage's median from another's does not give a per-frame remainder, and an
    // early return can produce an interval sample with no matching service
    // sample. Comparing these populations needs frame-linked records, which this
    // instrument does not produce.
    emit("eyeSkew", gEyeSkew.Compute());
    // **This is the temporal disparity between the two submitted eyes.** With
    // one eye rendered per frame it should sit near one frame interval; a p50
    // far above `frameP50` means an eye is starving rather than alternating,
    // and a `staleEye` that never changes means the asymmetry is fixed rather
    // than the ordinary cadence.
    out << " eyeSkewSamples=" << gEyeSkewSamples.load(std::memory_order_relaxed)
        << " staleEye=" << gStaleEye.load(std::memory_order_relaxed);
    out << " timingNote=unpaired_stage_rings";
    return out.str();
}

std::string XrCoverageReport()
{
    std::unique_lock lock(gMutex,std::try_to_lock);
    if(!lock.owns_lock())return "coverage=busy";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(5);
    for (int eye = 0; eye < 2; ++eye) {
        const char* const name = eye == 0 ? " left" : " right";
        if (!gHost.requestedFovValid[eye]) {
            out << name << "=unavailable";
            continue;
        }
        const preyvr::xr::FovAngles rendered{gHost.eyeFov[eye].angleLeft,
                                             gHost.eyeFov[eye].angleRight,
                                             gHost.eyeFov[eye].angleUp,
                                             gHost.eyeFov[eye].angleDown};
        const preyvr::xr::FovAngles requested{gHost.requestedFov[eye].angleLeft,
                                              gHost.requestedFov[eye].angleRight,
                                              gHost.requestedFov[eye].angleUp,
                                              gHost.requestedFov[eye].angleDown};
        const auto coverage = preyvr::xr::TangentCoverage(rendered, requested);
        if (!coverage.valid) {
            out << name << "=invalid";
            continue;
        }
        out << name << "Used=" << coverage.utilisationArea
            << name << "Covered=" << coverage.satisfactionArea
            << name << "Short=" << (coverage.requestExceedsRender ? 1 : 0);
        // The equal-density target size, so the figure lands as pixels rather
        // than as a ratio to be multiplied out by hand later. Margin for
        // reprojection is NOT included; this is the floor, not a launch preset.
        const DWORD width = XrResolutionChain(XrResolution::backBufferWidth);
        const DWORD height = XrResolutionChain(XrResolution::backBufferHeight);
        if (width > 0 && height > 0) {
            out << name << "EqualDensity="
                << static_cast<int>(static_cast<float>(width) *
                                    preyvr::xr::EqualDensityScale(coverage.utilisationWidth))
                << "x"
                << static_cast<int>(static_cast<float>(height) *
                                    preyvr::xr::EqualDensityScale(coverage.utilisationHeight));
        }
    }
    return out.str();
}

DWORD XrResolutionChain(XrResolution field)
{
    std::array<DWORD,static_cast<std::size_t>(XrResolution::count)> state{};
    const auto index = static_cast<std::size_t>(field);
    return index<state.size() && gResolution.TryRead(state)?state[index]:0;
}

} // namespace preyvr::dll
