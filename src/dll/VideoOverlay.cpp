#include "VideoOverlay.h"
#include "DualOverlay.h"
#include "EngineAccess.h"
#include "HookInstall.h"
#include "Logger.h"
#include "SafeMemory.h"
#include "XrSessionHost.h"
#include "preyvr/FullscreenVideoNative.h"
#include "preyvr/NativeContract.h"
#include "preyvr/VideoLetterbox.h"

#include <atomic>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <sstream>

namespace preyvr::dll {
namespace {
namespace vn = video::native;

void Log(const std::string& line) { lifecycle::Log("preyvr_dual_overlay " + line); }

static_assert(contract::Gated(vn::Contracts, vn::FullscreenDraw));
static_assert(contract::Gated(vn::Contracts, vn::GetViewport));

// The draw: the Bink backend, the movie, and R8D, which it hands on to the
// backend. The fourth register is passed through untouched rather than assumed
// unused.
using DrawFn = void(__fastcall*)(void* backend, void* movie, unsigned int handedOn, void* passThrough);
using GetViewportFn = void(__fastcall*)(void* renderer, int* x, int* y, int* width, int* height);

// What Resolve finds in a PreyDll whose bodies are the ones read.
struct Engine {
    std::uintptr_t draw = 0;
    GetViewportFn getViewport = nullptr;
    void* renderer = nullptr;
};

std::atomic<DrawFn> gOriginal{nullptr};
std::atomic<GetViewportFn> gGetViewport{nullptr};
std::atomic<void*> gRenderer{nullptr};
std::mutex gInstallMutex;
bool gInstallTried = false;
std::atomic<bool> gInstalled{false};
std::atomic<const char*> gGate{"unchecked"};
std::atomic<std::uint64_t> gDraws{0}, gCopied{0}, gRefused{0};
std::atomic<const char*> gLastRefusal{"none"};
std::atomic<unsigned> gLogged{0};

// --- the draw (render thread) -------------------------------------------------------
// The video's size, where the draw reads it. False when the movie or its Bink
// handle cannot be read.
bool ReadVideoSize(const void* movie, std::int32_t& width, std::int32_t& height)
{
    const void* bink = nullptr;
    if (movie == nullptr || !SafeRead(static_cast<const std::uint8_t*>(movie) + vn::MovieBink, bink) ||
        bink == nullptr) {
        return false;
    }
    const auto* at = static_cast<const std::uint8_t*>(bink);
    return SafeRead(at + vn::BinkWidth, width) && SafeRead(at + vn::BinkHeight, height);
}

// The first draws of the session, with the sizes the decision was made from.
void NoteDraw(std::int32_t width, std::int32_t height, const int (&viewport)[4], bool copied, const char* refusal)
{
    if (gLogged.fetch_add(1, std::memory_order_relaxed) >= 4) {
        return;
    }
    std::ostringstream line;
    line << "video size=" << width << 'x' << height << " viewport=" << viewport[2] << 'x' << viewport[3]
         << " copied=" << (copied ? 1 : 0) << " refusal=" << (refusal != nullptr ? refusal : "none");
    Log(line.str());
}

void __fastcall FullscreenDrawDetour(void* backend, void* movie, unsigned int handedOn, void* passThrough)
{
    const DrawFn original = gOriginal.load(std::memory_order_acquire);
    // Two loads when off, or outside a held frame's overlay window.
    if (!DualOverlayRedrawEnabled() || !DualOverlayWindowOpen()) {
        original(backend, movie, handedOn, passThrough);
        return;
    }
    // Asked before the draw, as the draw asks it: whether the draw will clear
    // the target to black and letterbox.
    int viewport[4]{};
    std::int32_t width = 0, height = 0;
    const bool sized = ReadVideoSize(movie, width, height);
    if (sized) {
        gGetViewport.load(std::memory_order_acquire)(gRenderer.load(std::memory_order_acquire), &viewport[0],
                                                     &viewport[1], &viewport[2], &viewport[3]);
    }
    original(backend, movie, handedOn, passThrough);
    gDraws.fetch_add(1, std::memory_order_relaxed);
    const char* refusal = nullptr;
    if (!sized) {
        refusal = "unread";
    } else if (!video::Letterboxed(viewport[2], viewport[3], width, height)) {
        // Without the clear the world around the video is still the second
        // eye's, and a copy would put it in the first eye too.
        refusal = "not_letterboxed";
    }
    const bool copied = refusal == nullptr && CopyBackBufferIntoHeldEye(&refusal);
    if (copied) {
        gCopied.fetch_add(1, std::memory_order_relaxed);
    } else {
        gRefused.fetch_add(1, std::memory_order_relaxed);
        gLastRefusal.store(refusal, std::memory_order_relaxed);
    }
    NoteDraw(width, height, viewport, copied, refusal);
}

// --- the gate and the hook (main thread) ----------------------------------------
bool GateBodies(std::uintptr_t base)
{
    __try {
        return contract::BodiesMatch(base, vn::Contracts);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// The draw, GetViewport and the renderer, once PreyDll is loaded and both
// bodies are the ones read: ERROR_NOT_READY before, ERROR_BAD_EXE_FORMAT for
// other bytes.
DWORD Resolve(Engine& engine)
{
    engine = {};
    const auto base = PreyBase();
    if (base == 0) { return ERROR_NOT_READY; }
    if (!GateBodies(base)) { return ERROR_BAD_EXE_FORMAT; }
    engine.draw = base + vn::FullscreenDraw;
    engine.getViewport = reinterpret_cast<GetViewportFn>(base + vn::GetViewport);
    engine.renderer = reinterpret_cast<void*>(base + vn::Renderer);
    return ERROR_SUCCESS;
}
// Tests replace the engine here.
DWORD (*gResolve)(Engine&) = Resolve;

bool Install(std::uintptr_t target)
{
    const hook::Result result =
        hook::Install(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&FullscreenDrawDetour), gOriginal);
    if (!result) {
        Log(std::string("result=failed detail=video_") + hook::StepName(result.step) +
            " status=" + std::to_string(result.status) + " target=FullscreenDraw");
        return false;
    }
    return true;
}
// Tests replace the installer here.
bool (*gInstall)(std::uintptr_t) = Install;

} // namespace

DWORD EnsureVideoOverlay()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstallTried) { return gInstalled.load(std::memory_order_acquire) ? 0 : ERROR_NOT_SUPPORTED; }
    Engine engine;
    const DWORD ready = gResolve(engine);
    if (ready == ERROR_NOT_READY) {
        Log("result=unavailable detail=video_no_preydll");
        return ERROR_NOT_READY;
    }
    gInstallTried = true;
    if (ready != ERROR_SUCCESS) {
        gGate = "refused";
        Log("result=unavailable detail=video_bytes_mismatch target=FullscreenDraw");
        return ERROR_NOT_SUPPORTED;
    }
    // Before the hook: the detour can run the moment it is live.
    gGetViewport.store(engine.getViewport, std::memory_order_release);
    gRenderer.store(engine.renderer, std::memory_order_release);
    if (!gInstall(engine.draw)) {
        gGate = "hook_failed";
        return ERROR_NOT_SUPPORTED;
    }
    gGate = "ok";
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=video_hook_installed target=FullscreenDraw rows=" + std::to_string(std::size(vn::Contracts)));
    return 0;
}

std::string VideoOverlayReport()
{
    std::ostringstream out;
    out << " video={hook=" << (gInstalled.load() ? 1 : 0) << " gate=" << gGate.load() << " draws=" << gDraws.load()
        << " copied=" << gCopied.load() << " refused=" << gRefused.load() << " lastRefusal=" << gLastRefusal.load()
        << '}';
    return out.str();
}

} // namespace preyvr::dll
