#include "XrSessionHost.h"
#include "XrSessionState.h"
#include "EngineAccess.h"

#include "preyvr/FrameTiming.h"
#include "preyvr/FrustumCoverage.h"

#include "Logger.h"
#include "FrameProbe.h"
#include "preyvr/EngineMap.h"
#include "preyvr/XrFrameContract.h"
#include "preyvr/XrHold.h"
#include "MainRenderHook.h"
#include "EyeRecords.h"
#include "HeadTrackingHook.h"
#include "XrInput.h"
#include "UiMode.h"
#include "UiGuide.h"
#include "UiPointer.h"
#include "HudLayer.h"
#include "LayerSwapchain.h"
#include "RendererAccess.h"
#include "XrPose.h"
#include "LoadingScreen.h"
#include "VrOptionsLayer.h"
#include "VrOptionsRuntime.h"
#include "BodyEquipment.h"
#include "SlotFeedback.h"
#include "NativeWristTexture.h"
#include "WristLane.h"
#include "AimTakeover.h"
#include "ReticleFollow.h"
#include "DebugOverlay.h"
#include "preyvr/UiPanel.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/HudCapturePair.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/StereoFrame.h"
#include "preyvr/WristHolo.h"
#include "preyvr/XrSwapchainFormat.h"

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

// Hold frames (docs/LOADING-LASER-2026-10-17.md): frames of ours while Prey's
// are late, with the laser where the hand is now; and the loading screen on
// them (docs/LOADING-SCREEN-2026-10-20.md). XrSessionState.h lists the files.

namespace preyvr::dll {
using namespace xrsession;

namespace {

// A capture of this load waits to be shown. Under gMutex.
bool LoadingScreenWaiting()
{
    return gLoadingCaptured.load(std::memory_order_acquire)!=0 && gHost.loadingImage!=nullptr;
}

// **The loading screen goes on the game menu's panel**: where the menu was, if
// one was up when the load began (loading a save from a menu); else placed now
// in front of the wearer, as a menu opening is, and kept as the menu's, so a
// menu up when the load ends ("press any key") stays where the screen was.
// Hold thread, under gMutex.
bool PlaceLoadingPanel(const XrView (&views)[2])
{
    if(!gHost.panelWasActive) {
        gHost.menuPanel.reset();gHost.guidePanel.reset();gHost.menuAnchor.reset();
        gHost.panelWasActive=true;
        Log("result=0 detail=ui_panel active=1 reason=loading_screen");
    }
    const auto reference=HeadTrackingReferenceGeneration();
    const float curve=gHost.cylinderSupported?static_cast<float>(gUiCurveDegrees.load())*kRadiansPerDegree:0;
    if(MenuPanelsStale(curve,reference))
        PlaceMenuPanels(stereo::CyclopsPose(FromXrPose(views[0].pose),FromXrPose(views[1].pose)),views,reference,curve);
    return gHost.menuPanel.has_value();
}

// **The latest capture into the menu panel's image**: slice 0 of the session's
// swapchain, which the panel shows whole. A D3D11 copy on the hold thread, which
// the multithread protection hold frames need covers, as it covers the runtime's
// own use of Prey's context there. True when the panel's image holds a capture
// of this load. Hold thread, under gMutex.
bool UploadLoadingScreen()
{
    const auto captured=gLoadingCaptured.load(std::memory_order_acquire);
    if(captured==gHost.loadingUploaded)return true;
    AcquiredImage image(SessionSwapchainApi(),gHost.swapchain);
    if(!image.Acquired())return gHost.loadingUploaded!=0;
    if(!image.Waited()) {gStopRequested.store(true);return false;}
    bool copied=false;
    if(image.Index()<gHost.images.size()) {
        if(const auto context=ImmediateContext(gHost.device)) {
            context->CopySubresourceRegion(gHost.images[image.Index()],D3D11CalcSubresource(0,0,1),0,0,0,
                                           gHost.loadingImage,0,nullptr);
            copied=true;
        }
    }
    if(!image.Release()) {gStopRequested.store(true);return false;}
    if(copied) {
        gHost.loadingUploaded=captured;
        ++gHost.loadingUploads;
        gLoadingUploads.fetch_add(1,std::memory_order_relaxed);
    }
    return gHost.loadingUploaded!=0;
}

void RecordStall(const xrhold::StallEnd& stall)
{
    gHoldStalls.fetch_add(1,std::memory_order_relaxed);
    gHoldLastStallNs.store(stall.durationNs,std::memory_order_relaxed);
    if(stall.durationNs>gHoldLongestStallNs.load(std::memory_order_relaxed))
        gHoldLongestStallNs.store(stall.durationNs,std::memory_order_relaxed);
    if(stall.durationNs>=kHoldLogStallNs || stall.heldFrames)
        Log("hold stall_ms="+std::to_string(stall.durationNs/1000000)+" frames="+std::to_string(stall.heldFrames));
}

// **One hold frame**, on the hold thread with gMutex held: the last frame's
// layers again, with the laser where the hand is now. No D3D11 call is made
// here; the runtime's own use of Prey's context is what the protection covers.
// A begun frame always ends, and the contract is left idle either way, so a
// failure here can never wedge Prey's next frame.
bool ServiceHoldFrame()
{
    PumpEvents();
    if(gStopRequested.load(std::memory_order_acquire) || !gHost.sessionBegun)return false;
    const std::uint32_t thread=GetCurrentThreadId();
    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    if(XR_FAILED(xrWaitFrame(gHost.session,&waitInfo,&frameState)))return false;
    const bool contracted=gHost.contract.OnWaited(frameState.predictedDisplayTime,
        frameState.shouldRender!=XR_FALSE,thread)==xrframe::Result::ok;
    // A tracking-space change is the render thread's to apply; until then the
    // held poses mean nothing.
    const bool spaceChanging=!gHost.spaceChanges.empty() &&
        gHost.spaceChanges.front().changeTime<=frameState.predictedDisplayTime;
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime=frameState.predictedDisplayTime;
    locate.space=gHost.space;
    XrViewState viewState{XR_TYPE_VIEW_STATE};
    std::uint32_t located=0;
    XrView views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
    const bool haveViews=!spaceChanging &&
        XR_SUCCEEDED(xrLocateViews(gHost.session,&locate,&viewState,2,&located,views)) && located==2 &&
        (viewState.viewStateFlags&XR_VIEW_STATE_POSITION_VALID_BIT)!=0 &&
        (viewState.viewStateFlags&XR_VIEW_STATE_ORIENTATION_VALID_BIT)!=0;
    if(contracted)gHost.contract.OnViewsLocated(frameState.predictedDisplayTime,haveViews,haveViews);

    // **The loading screen** (CaptureXrLoadingScreen): while Prey's loading loop
    // runs, its latest image on the game menu's panel in place of the frame held
    // from before the load, with nothing behind it, and the laser on it.
    const bool loading=LoadingScreenWaiting() && haveViews && PlaceLoadingPanel(views) && UploadLoadingScreen();
    FrameLayerSet set=loading?FrameLayerSet{}:gHost.heldLayers;
    HeldPointer heldPointer=gHost.heldPointer;
    if(loading) {
        heldPointer={true,ui::Surface{*gHost.menuPanel,gHost.panelAngle}};
        FillMenuLayers(set.menu,set.menuCylinder,heldPointer.surface,gHost.swapchain,0);
        set.shown.menu=true;
        set.shown.menuCylinder=heldPointer.surface.angle>0;
    }
    set.shown.beam=set.shown.cursor=false;
    const auto hand=UiPointerHand();
    if(haveViews && heldPointer.active && gHost.pointerReady && hand<2) {
        UiPointerVisual pointer;
        if(LocateHeldPointer(gHost.session,gHost.space,frameState.predictedDisplayTime,static_cast<Hand>(hand),
                             pointer.aim,pointer.pressed)) {
            pointer.active=true;
            pointer.hit=ui::Intersect(heldPointer.surface,pointer.aim);
            const auto head=stereo::CyclopsPose(FromXrPose(views[0].pose),FromXrPose(views[1].pose));
            FillPointerLayers(pointer,heldPointer.surface,head.position,set.cursor,set.beam,
                              set.shown.cursor,set.shown.beam);
        }
    }
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if(XR_FAILED(xrBeginFrame(gHost.session,&beginInfo))) {
        gHost.contract.Reset();
        return false;
    }
    const bool begun=contracted && gHost.contract.OnBegun()==xrframe::Result::ok;
    // Without the screen there is only the held frame, and there may be none: a
    // capture that could not be shown yet.
    const bool show=!spaceChanging && frameState.shouldRender!=XR_FALSE && (loading || gHost.heldValid);
    const auto layers=set.Ordered();
    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime=frameState.predictedDisplayTime;
    endInfo.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount=show?static_cast<std::uint32_t>(layers.size()):0;
    endInfo.layers=show?layers.data():nullptr;
    const auto endResult=xrEndFrame(gHost.session,&endInfo);
    if(!begun || gHost.contract.OnSubmitted(thread)!=xrframe::Result::ok)gHost.contract.Reset();
    const bool submitted=XR_SUCCEEDED(endResult) && show;
    if(submitted && loading) {
        if(gHost.loadingFrames++==0)Log("loading_screen shown captures="+std::to_string(gLoadingCaptured.load())+
            " curved="+(set.shown.menuCylinder?"1":"0"));
        gLoadingFrames.fetch_add(1,std::memory_order_relaxed);
    }
    return submitted;
}

} // namespace

namespace xrsession {

// **Prey's render thread takes gMutex here, and waits only for a hold frame in
// flight.** That frame ends within a display period, and none starts after it,
// because the caller stamped this frame first. Waiting matters: a both-eyes
// frame whose first eye is refused submits nothing, so a skip at the end of a
// hitch would show a black frame. A control call holding the lock still costs
// the frame, as before.
bool LockForRenderThread(std::unique_lock<std::timed_mutex>& lock)
{
    gLastServiceNs.store(MonotonicNanoseconds(),std::memory_order_release);
    if(lock.try_lock())return true;
    if(!gHoldInFrame.load(std::memory_order_acquire))return false;
    gHoldRenderWaits.fetch_add(1,std::memory_order_relaxed);
    if(lock.try_lock_for(kHoldYieldTimeout))return true;
    gHoldRenderWaitMisses.fetch_add(1,std::memory_order_relaxed);
    return false;
}

// **Prey's frames are back**: the loading loop has ended. From here the menu
// panel's image is Prey's again, so a frame held from before the load, whose
// images were overwritten by the screen, is not submitted again. Render thread,
// under gMutex.
void EndLoadingScreen()
{
    const auto captured=gLoadingCaptured.exchange(0,std::memory_order_acq_rel);
    if(captured==0)return;
    if(gHost.loadingUploads!=0)gHost.heldValid=false;
    gLoadingLoads.fetch_add(1,std::memory_order_relaxed);
    Log("loading_screen ended captures="+std::to_string(captured)+" uploads="+std::to_string(gHost.loadingUploads)+
        " frames="+std::to_string(gHost.loadingFrames));
    gHost.loadingUploaded=gHost.loadingUploads=gHost.loadingFrames=0;
}

// **The only place the protection changes**: Prey's render thread, under
// gMutex, so no hold frame (which needs gMutex) is in flight. Hold frames are
// allowed only after the protection is on, and stopped before it comes off.
void ApplyHoldProtection()
{
    const bool wanted=gHoldWanted.load(std::memory_order_acquire);
    if(wanted==gProtectionApplied || gHost.device==nullptr)return;
    if(!wanted)gHoldActive.store(false,std::memory_order_release);
    ComPtr<ID3D11Multithread> protection;
    const auto context=ImmediateContext(gHost.device);
    const bool interfaced=context!=nullptr &&
        SUCCEEDED(context.As(&protection)) &&
        protection!=nullptr;
    if(wanted) {
        // A single-threaded device has no locks to turn on.
        const char* refusal=!interfaced?"no_multithread_interface":
            (gHost.device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?"single_threaded_device":nullptr;
        if(refusal) {
            gHoldWanted.store(false,std::memory_order_release);
            gHoldRefusal.store(refusal,std::memory_order_release);
            Log(std::string("hold result=refused detail=")+refusal+" laser_during_loads=frozen");
        } else {
            const BOOL before=protection->SetMultithreadProtected(TRUE);
            gProtectionBefore.store(before?1:0,std::memory_order_release);
            gProtectionApplied=true;
            gHoldActive.store(true,std::memory_order_release);
            Log(std::string("hold result=0 detail=on multithread_protection=on was=")+(before?"on":"off")+
                " threshold_ms="+std::to_string(gHoldThresholdMs.load()));
        }
    } else {
        // Put back only what we changed.
        if(interfaced && gProtectionBefore.load(std::memory_order_acquire)==0)protection->SetMultithreadProtected(FALSE);
        gProtectionApplied=false;
        Log(std::string("hold result=0 detail=off multithread_protection=")+
            (gProtectionBefore.load()==1?"on_as_before":"off"));
    }
}

// The hold thread, started with the first session and idle without one.
DWORD WINAPI HoldThread(LPVOID)
{
    xrhold::StallTracker stalls;
    for(;;) {
        Sleep(kHoldPollMs);
        const bool running=gStatus.load(std::memory_order_acquire)==static_cast<DWORD>(XrSessionStatus::running);
        const std::uint64_t threshold=static_cast<std::uint64_t>(gHoldThresholdMs.load(std::memory_order_relaxed))*1000000ull;
        std::optional<xrhold::StallEnd> ended;
        const bool stalled=stalls.Observe(MonotonicNanoseconds(),running?gLastServiceNs.load(std::memory_order_acquire):0,
                                          threshold,ended);
        if(ended)RecordStall(*ended);
        if(!stalled || !gHoldActive.load(std::memory_order_acquire))continue;
        std::unique_lock lock(gMutex,std::try_to_lock);
        if(!lock.owns_lock())continue;
        // Again under the lock: Prey's frame may have been stamped meanwhile.
        // With nothing held, a loading screen waiting is still something to show.
        if(MonotonicNanoseconds()-gLastServiceNs.load(std::memory_order_acquire)<threshold ||
           !gHoldActive.load(std::memory_order_acquire) || !gHost.started || !gHost.sessionBegun ||
           gStopRequested.load(std::memory_order_acquire) || (!gHost.heldValid && !LoadingScreenWaiting()))continue;
        gHoldInFrame.store(true,std::memory_order_release);
        const bool submitted=ServiceHoldFrame();
        gHoldInFrame.store(false,std::memory_order_release);
        if(submitted) {
            stalls.CountHeldFrame();
            gHoldFrames.fetch_add(1,std::memory_order_relaxed);
        }
    }
}

} // namespace xrsession

DWORD SetXrHold(unsigned int enabled, unsigned int thresholdMs)
{
    if (enabled > 1 || (thresholdMs != 0 && (thresholdMs < 30 || thresholdMs > 2000))) {
        return ERROR_INVALID_PARAMETER;
    }
    if (thresholdMs != 0) gHoldThresholdMs.store(thresholdMs, std::memory_order_relaxed);
    gHoldRefusal.store("none", std::memory_order_release);
    // Applied by the render thread on its next frame (ApplyHoldProtection).
    gHoldWanted.store(enabled != 0, std::memory_order_release);
    return 0;
}

std::string XrHoldReport()
{
    const auto ms = [](const std::atomic<std::uint64_t>& ns) { return ns.load(std::memory_order_relaxed) / 1000000; };
    const int before = gProtectionBefore.load(std::memory_order_acquire);
    std::ostringstream out;
    out << " wanted=" << (gHoldWanted.load() ? 1 : 0) << " active=" << (gHoldActive.load() ? 1 : 0)
        << " protectionWas=" << (before < 0 ? "unread" : before ? "on" : "off")
        << " thresholdMs=" << gHoldThresholdMs.load() << " frames=" << gHoldFrames.load()
        << " stalls=" << gHoldStalls.load() << " lastStallMs=" << ms(gHoldLastStallNs)
        << " longestStallMs=" << ms(gHoldLongestStallNs) << " renderWaits=" << gHoldRenderWaits.load()
        << " renderWaitMisses=" << gHoldRenderWaitMisses.load() << " refusal=" << gHoldRefusal.load()
        // The loading screen: the loading loop's presents, the copies taken
        // there, uploads into the menu panel, hold frames showing it, loads ended.
        << " loadingPresents=" << LoadingScreenPresentCount() << " loadingCaptures=" << gLoadingCaptures.load()
        << " loadingUploads=" << gLoadingUploads.load() << " loadingFrames=" << gLoadingFrames.load()
        << " loadingLoads=" << gLoadingLoads.load();
    return out.str();
}

void CaptureXrLoadingScreen(void* renderer)
{
    if (renderer == nullptr ||
        gStatus.load(std::memory_order_acquire) != static_cast<DWORD>(XrSessionStatus::running) ||
        !gHoldActive.load(std::memory_order_acquire) || gStopRequested.load(std::memory_order_acquire)) {
        return;
    }
    // Prey's render thread, inside the loading loop's lock, so nothing here
    // waits. It reads only what stays fixed while the session runs, and its own
    // image, which only this thread creates and releases (Teardown runs here
    // too). The hold thread reads that image under gMutex, so it is created
    // under gMutex, taken only if free.
    if (!gHost.started || gHost.device == nullptr || gHost.loadingRefused) return;
    const auto backBuffer = AcquireBackBuffer(renderer);
    if (!backBuffer) {
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    // Same size gate as Prey's frames: the panel shows the whole of an image
    // the backbuffer's size.
    if (desc.Width == gHost.width && desc.Height == gHost.height) {
        if (gHost.loadingImage == nullptr) {
            std::unique_lock lock(gMutex, std::try_to_lock);
            if (lock.owns_lock()) {
                D3D11_TEXTURE2D_DESC copy = desc;
                copy.Usage = D3D11_USAGE_DEFAULT;
                copy.BindFlags = 0;
                copy.CPUAccessFlags = 0;
                copy.MiscFlags = 0;
                if (FAILED(gHost.device->CreateTexture2D(&copy, nullptr, &gHost.loadingImage))) {
                    gHost.loadingImage = nullptr;
                    gHost.loadingRefused = true;
                    Log("loading_screen result=failed step=create_image format=" + std::to_string(desc.Format));
                }
            }
        }
        if (gHost.loadingImage != nullptr) {
            if (const auto context = ImmediateContext(gHost.device)) {
                context->CopyResource(gHost.loadingImage, backBuffer.Get());
                // After the copy is issued: a hold frame that reads the count
                // copies from the image after it.
                gLoadingCaptures.fetch_add(1, std::memory_order_relaxed);
                if (gLoadingCaptured.fetch_add(1, std::memory_order_acq_rel) == 0) {
                    Log("loading_screen captured size=" + std::to_string(desc.Width) + "x" +
                        std::to_string(desc.Height) + " format=" + std::to_string(desc.Format));
                }
            }
        }
    } else {
        static bool resizedLogged = false; // render thread only
        if (!resizedLogged) {
            resizedLogged = true;
            Log("loading_screen result=refused detail=backbuffer_resized");
        }
    }
}

} // namespace preyvr::dll
