#include "render/camera/ZoomLane.h"
#include "engine/EngineAccess.h"
#include "base/Logger.h"
#include "engine/EngineMap.h"
#include "engine/NativeContract.h"
#include "render/camera/ViewZoom.h"
#include "render/camera/ViewZoomNative.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace preyvr::dll {
namespace {
namespace zn = viewzoom::native;

void Log(const std::string& line) { lifecycle::Log("preyvr_view_zoom " + line); }

static_assert(contract::Gated(zn::Contracts, zn::FinalHfov));
static_assert(zn::PlayerCamera == engine::ArkPlayerLayout::camera);

using FinalHfovFn = float(__fastcall*)(const void* zoomManager);

// tan(FOV/2) of the unzoomed view, 0 while there is none. Written by the main
// thread, read by whichever thread builds a frame's eyes.
std::atomic<float> gBaseTangent{0.0f};
std::atomic<const char*> gGate{"unchecked"};
std::atomic<float> gNow{1.0f};
std::atomic<std::uint64_t> gZooms{0}, gZoomed{0}, gUnread{0};
std::uintptr_t gBase = 0;   // PreyDll's, once its body is the one read
bool gChecked = false;      // main thread
// The deepest of the zoom in progress, for the line at its end. Main thread.
float gPeak = 0.0f;
std::uint64_t gPeakFrames = 0;

// --- the engine's values and the gate (main thread) ----------------------------------------
bool ReadEngine(const std::uint8_t* player, float& hfov, float& slider)
{
    __try {
        const auto finalHfov = reinterpret_cast<FinalHfovFn>(gBase + zn::FinalHfov);
        hfov = finalHfov(player + zn::PlayerZoomManager);
        const auto* cvars = *reinterpret_cast<const std::uint8_t* const*>(gBase + zn::GameCVars);
        slider = *reinterpret_cast<const float*>(cvars + zn::CVarsHfov);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// Tests replace the engine here.
bool (*gRead)(const std::uint8_t*, float&, float&) = ReadEngine;

bool GateBodies(std::uintptr_t base)
{
    __try {
        return contract::BodiesMatch(base, zn::Contracts);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// PreyDll's base, once it is loaded and the body is the one read: ERROR_NOT_READY
// before, ERROR_BAD_EXE_FORMAT for other bytes.
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

// Once it has an answer: other bytes leave zooms as they were.
bool Gate()
{
    if (gChecked) { return gBase != 0; }
    std::uintptr_t base = 0;
    const DWORD ready = gResolve(base);
    if (ready == ERROR_NOT_READY) { return false; }
    gChecked = true;
    if (ready != ERROR_SUCCESS) {
        gGate = "refused";
        Log("result=unavailable detail=bytes_mismatch target=ZoomFinalHfov");
        return false;
    }
    gBase = base;
    gGate = "ok";
    Log("result=0 detail=ready");
    return true;
}

// A zoom over: how deep it went, so a run's log shows it.
void EndZoom()
{
    gNow.store(1.0f, std::memory_order_relaxed);
    if (gPeakFrames == 0) { return; }
    std::ostringstream line;
    line << std::fixed << std::setprecision(2) << "result=0 detail=zoom peak=" << gPeak << " frames=" << gPeakFrames;
    Log(line.str());
    gPeak = 0.0f;
    gPeakFrames = 0;
}

void NoUnzoomedView()
{
    gBaseTangent.store(0.0f, std::memory_order_relaxed);
    EndZoom();
}

} // namespace

void NoteViewZoom(void* playerCamera, float fov, bool driving)
{
    if (!driving || playerCamera == nullptr || !Gate()) {
        NoUnzoomedView();
        return;
    }
    const auto* player = static_cast<const std::uint8_t*>(playerCamera) - zn::PlayerCamera;
    float hfov = 0.0f, slider = 0.0f, base = 0.0f;
    if (!gRead(player, hfov, slider) || !viewzoom::BaseTangent(fov, hfov, slider, base)) {
        gUnread.fetch_add(1, std::memory_order_relaxed);
        NoUnzoomedView();
        return;
    }
    gBaseTangent.store(base, std::memory_order_relaxed);
    const float zoom = viewzoom::Magnification(std::tan(fov * 0.5f), base);
    if (zoom == 1.0f) {
        EndZoom();
        return;
    }
    gNow.store(zoom, std::memory_order_relaxed);
    gZoomed.fetch_add(1, std::memory_order_relaxed);
    if (gPeakFrames++ == 0) { gZooms.fetch_add(1, std::memory_order_relaxed); }
    gPeak = std::max(gPeak, zoom);
}

float ViewZoomOf(float tangent)
{
    return viewzoom::Magnification(tangent, gBaseTangent.load(std::memory_order_relaxed));
}

std::string ZoomLaneReport()
{
    std::ostringstream s;
    s << std::fixed << std::setprecision(3) << "zoom={gate=" << gGate.load()
      << " base=" << gBaseTangent.load(std::memory_order_relaxed) << " now=" << gNow.load(std::memory_order_relaxed)
      << " zooms=" << gZooms.load() << " zoomed=" << gZoomed.load() << " unread=" << gUnread.load() << "}";
    return s.str();
}

} // namespace preyvr::dll
