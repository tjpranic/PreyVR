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

// **Prey's frame into an XR frame** (ServiceXrFrame), on Prey's render thread:
// wait and locate, the world image and the menu panel, the overlay layers in
// order, and the frame ended and kept for hold frames. The session's other
// parts are in the six files XrSessionState.h lists.
namespace preyvr::dll {
using namespace xrsession;

void ServiceXrFrame(void* renderer)
{
    // Renderer-frame obligation, independent of whether XR submits this frame.
    // Menu/flat paths still build cameras. Leaving their labels queued changed
    // alternating-eye parity when the backlog was eventually discarded.
    RenderContract renderContract{};
    dll::ConsumeRenderedContract(renderContract);
    // Taken with the contract, before any early return, for the same reason: a
    // mid-frame hold belongs to this frame or to none. The overlay window closes
    // first: nothing may draw into the held eye once it is being paired.
    gDualOverlayOpen.store(false, std::memory_order_release);
    const bool dualFirstHeld = gDualFirstHeld.exchange(false, std::memory_order_acq_rel);
    // Take each capture once, even on skipped submissions; never reuse a PDA
    // image after a dialog or another menu has replaced it.
    ID3D11Texture2D* inventoryRightTexture=nullptr;
    ID3D11Texture2D* inventoryTexture = InventoryLayerTexture(&inventoryRightTexture);
    ID3D11Texture2D* hudTexture = HudLayerTexture();
    ID3D11Texture2D* textTexture = TextLayerTexture();
    ID3D11Texture2D* noteTexture = NoteLayerTexture();
    const HudPartImage wheelImage = TakeHudPart(HudPart::wheel);
    const HudPartImage reticleImage = TakeHudPart(HudPart::reticle);
    ID3D11Texture2D* nativeWristTexture = NativeWristLayerTexture();
    RequestNativeWristCapture(false);
    SetInventoryConsumerReady(nullptr);
    if (gStatus.load(std::memory_order_acquire) != static_cast<DWORD>(XrSessionStatus::running)) {
        return; // the hot path
    }
    // The render thread does not wait on control calls. Skipping a frame is
    // free; blocking Prey's renderer on a control call is not. It waits only for
    // a hold frame in flight (LockForRenderThread).
    std::unique_lock lock(gMutex, std::defer_lock);
    if (!LockForRenderThread(lock) || !gHost.started) {
        return;
    }
    ApplyHoldProtection();
    EndLoadingScreen();

    if (gStopRequested.load(std::memory_order_acquire)) {
        if(!gStopAcknowledged.load(std::memory_order_acquire))return;
        // Torn down here rather than in StopXrSession, because the D3D resources
        // belong to this thread.
        Teardown();
        gStatus.store(static_cast<DWORD>(XrSessionStatus::stopped), std::memory_order_release);
        return;
    }

    PumpEvents();
    if(gStopRequested.load()) {
        // Preserve the session until the worker has disarmed native camera,
        // pose and input consumers, then acknowledged StopXrSession.
        return;
    }
    if (!gHost.sessionBegun) {
        return;
    }

    const std::uint32_t thread = GetCurrentThreadId();

    // **`xrWaitFrame` blocks on purpose**, and how long it blocks is the single
    // most diagnostic number here: time spent waiting is the runtime pacing us,
    // not the scene costing us, and reclaiming pixels would not touch it.
    const bool timing = gTimingEnabled.load(std::memory_order_acquire);
    // Applied here, on the thread that records, before any sample this frame.
    // A reset run from the command thread could race an in-flight writer and let
    // a fresh epoch inherit a stale sample.
    gServiceInterval.ApplyPendingReset();
    gWaitFrame.ApplyPendingReset();
    gAcquireWait.ApplyPendingReset();
    gEndFrame.ApplyPendingReset();
    gServiceTotal.ApplyPendingReset();
    gEyeSkew.ApplyPendingReset();
    const std::uint64_t serviceStart = timing ? preyvr::timing::MonotonicNanoseconds() : 0;
    if (timing) { gServiceInterval.Mark(serviceStart); }

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    const std::uint64_t waitStart = timing ? preyvr::timing::MonotonicNanoseconds() : 0;
    const XrResult waitResult = xrWaitFrame(gHost.session, &waitInfo, &frameState);
    if (timing) {
        gWaitFrame.AddNanoseconds(preyvr::timing::MonotonicNanoseconds() - waitStart);
    }
    if (XR_FAILED(waitResult)) {
        return;
    }
    if (frameState.predictedDisplayPeriod > 0) {
        gRuntimePeriodUs.store(
            static_cast<std::uint32_t>(frameState.predictedDisplayPeriod / 1000),
            std::memory_order_relaxed);
    }
    if (gHost.contract.OnWaited(frameState.predictedDisplayTime,
                                frameState.shouldRender != XR_FALSE, thread) !=
        xrframe::Result::ok) {
        return;
    }

    bool spaceReady=true;
    while(!gHost.spaceChanges.empty() && gHost.spaceChanges.front().changeTime<=frameState.predictedDisplayTime) {
        const auto& change=gHost.spaceChanges.front();
        if(!RebaseHeadTrackingSpace(FromXrPose(change.poseInPreviousSpace),change.poseValid!=XR_FALSE)) {
            spaceReady=false;break;
        }
        gHost.eyeImageValid[0]=gHost.eyeImageValid[1]=false;
        gHost.heldValid=false; // its poses are in the old space
        gHost.menuPanel.reset();gHost.guidePanel.reset();ClearUiPointer();
        // Poses in the old space mean nothing in the new one; a placement kept
        // relative to the anchor still does, and is put back from the new one.
        gHost.optionsLayer.Close(nullptr);gHost.menuAnchor.reset();
        gHost.noteHold.Reset(); // an open note or wheel is placed afresh in the new space
        gHost.wheelHold.Reset();
        gHost.recalibrateSpace=HeadTrackingHasReference()==0;
        gHost.spaceChanges.erase(gHost.spaceChanges.begin());
        Log("tracking_space changed images_invalidated=1");
    }
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = frameState.predictedDisplayTime;
    locate.space = gHost.space;
    XrViewState viewState{XR_TYPE_VIEW_STATE};
    std::uint32_t located = 0;
    XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    bool haveViews = spaceReady &&
        XR_SUCCEEDED(xrLocateViews(gHost.session, &locate, &viewState, 2, &located, views)) &&
        located == 2 &&
        (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0 &&
        (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
    if(spaceReady && !haveViews && !gHost.spaceHasValidViews && gHost.localFallback) {
        locate.space=gHost.localFallback;
        if(XR_SUCCEEDED(xrLocateViews(gHost.session,&locate,&viewState,2,&located,views)) && located==2 &&
           (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
           (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
            xrDestroySpace(gHost.space);gHost.space=gHost.localFallback;gHost.localFallback=XR_NULL_HANDLE;
            gTrackingSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;gHost.spaceChanges.clear();haveViews=true;
            Log("tracking_space selected=LOCAL reason=floor_not_locatable");
        }
    }
    if(haveViews) gHost.spaceHasValidViews=true;
    gHost.contract.OnViewsLocated(frameState.predictedDisplayTime, haveViews, haveViews);

    // Hand the head pose to the M1 camera hook. The midpoint of the two eyes is
    // the head, and CyclopsPose is already the project's answer for that -- it
    // takes orientation from the left eye rather than slerping two identical
    // quaternions, which would introduce error rather than remove it.
    //
    // Published here because this is where the pose already exists. The scope
    // records that this is *after* rasterisation and therefore not where the
    // playbook says to sample; the age counter exists to measure exactly how much
    // that costs before deciding whether to move it.
    // Located against the same predicted display time as the views, so the hands
    // and the eyes describe the same instant.
    Pose inputHead{};
    PoseValidity inputHeadValidity{};
    // The distance between the two located views; also what the inventory
    // layer's parallax uses below.
    float runtimeIpd = 0.0f;

    if (haveViews) {
        const Pose left = FromXrPose(views[0].pose);
        const Pose right = FromXrPose(views[1].pose);
        inputHead = stereo::CyclopsPose(left, right);
        const Vec3 separation{right.position.x-left.position.x,
                              right.position.y-left.position.y,right.position.z-left.position.z};
        runtimeIpd = std::sqrt(separation.x*separation.x + separation.y*separation.y +
                               separation.z*separation.z);
        gRuntimeIpd.store(runtimeIpd);
        for (int eye=0;eye<2;++eye) {
            gHost.requestedFov[eye]=views[eye].fov;
            gHost.requestedFovValid[eye]=true;
        }
        gOptics.Publish({views[0].fov,views[1].fov});
        inputHeadValidity.positionValid = inputHeadValidity.orientationValid = true;
        inputHeadValidity.positionTracked = (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_TRACKED_BIT) != 0;
        inputHeadValidity.orientationTracked = (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_TRACKED_BIT) != 0;
        dll::PublishHeadPose(inputHead, frameState.predictedDisplayTime);
    }

    if(spaceReady) {
        UpdateXrInput(gHost.session, gHost.space,
                      static_cast<long long>(frameState.predictedDisplayTime), inputHead, inputHeadValidity);
    } else {
        // The runtime has changed coordinates but our reference rebase could
        // not acquire its lock yet. Do not publish hands in the new space with
        // the old reference, or keep the previous input sample actionable.
        InvalidateTrackingSamples();
        ClearUiPointer();
    }

    if(gHost.recalibrateSpace && haveViews && RecenterHeadTracking(true)==0) gHost.recalibrateSpace=false;

    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(xrBeginFrame(gHost.session, &beginInfo))) {
        return;
    }
    gHost.contract.OnBegun();

    XrCompositionLayerProjectionView projViews[2]{};
    const auto panelSettingsGeneration=gPanelSettingsGeneration.load(std::memory_order_acquire);
    if(panelSettingsGeneration!=gHost.panelSettingsGeneration) {
        gHost.menuPanel.reset();
        gHost.guidePanel.reset();
        gHost.panelSettingsGeneration=panelSettingsGeneration;
    }
    const auto panelMode=gUiPanelMode.load();
    // Through a stalled menu poll the panel keeps its presentation (WantMenuPanel);
    // pointer and gameplay input still require a fresh modal sample.
    const bool wantPanel=ui::WantMenuPanel(panelMode,haveViews,HudMenuStateKnown(),HudMenuIsOpen(),gHost.panelWasActive);
    if (wantPanel != gHost.panelWasActive) {
        gHost.menuPanel.reset();
        gHost.guidePanel.reset();
        gHost.menuAnchor.reset(); // a game menu opening is placed afresh, in front of the wearer
        gHost.eyeImageValid[0]=gHost.eyeImageValid[1]=false;
        gHost.panelWasActive=wantPanel;
        ProbeRecord(wantPanel?ProbeStep::panelOpen:ProbeStep::panelClose);
        Log(std::string("result=0 detail=ui_panel active=")+(wantPanel?"1":"0"));
    }
    const auto reference=HeadTrackingReferenceGeneration();
    // Stereo inventory uses a planar disparity contract. Include this in the
    // fit/serial decision so switching modes also cancels an old curved drag.
    const bool planarInventory=(InventoryStereoEnabled() && HudInventoryIsOpen()) || inventoryRightTexture;
    const float curve=gHost.cylinderSupported && !planarInventory?
        static_cast<float>(gUiCurveDegrees.load())*kRadiansPerDegree:0;
    if (wantPanel && MenuPanelsStale(curve,reference)) PlaceMenuPanels(inputHead,views,reference,curve);
    const bool panelActive=wantPanel && gHost.menuPanel.has_value();
    if(panelActive && !gHost.guideAttempted) {
        gHost.guideAttempted=true;
        const auto api=SessionSwapchainApi();
        const auto format=FirstListedFormat(api.formats,gHost.session,
                                            {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB});
        if(format!=DXGI_FORMAT_UNKNOWN) {
            auto pixels=MakeMenuGuide();
            if(format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
                for(std::size_t i=0;i<pixels.size();i+=4)std::swap(pixels[i],pixels[i+2]);
            if(!pixels.empty()) {
                const auto upload=CreateStaticLayer(api,gHost.session,gHost.device,format,kGuideWidth,kGuideHeight,
                                                    pixels.data(),gHost.guideSwapchain);
                if(upload.waitFailed)gStopRequested.store(true);
                gHost.guideReady=upload.ready;
            }
        }
    }
    bool rendered = false;
    bool stereoPair = false;
    bool inventorySubmitted = false, inventoryPrepared = false;
    bool inventoryStereoSubmitted=false,inventoryStereoPrepared=false;
    D3D11_TEXTURE2D_DESC inventoryDescription{};
    if (!InventoryCaptureEnabled() && !inventoryTexture) gHost.inventorySwapchain.reset();
    if (!InventoryStereoEnabled() && !inventoryRightTexture) gHost.inventoryRightSwapchain.reset();

    if (gHost.contract.ShouldRenderThisFrame()) {
        if (const auto backBuffer = AcquireBackBuffer(renderer)) {
            // Check before acquiring an XR image. A resize skips the copy, but
            // must still reach xrEndFrame/OnSubmitted below with zero layers.
            // Returning here used to strand both an acquired image and a begun
            // frame; our FrameContract then refused every subsequent wait.
            D3D11_TEXTURE2D_DESC backDesc{};
            backBuffer->GetDesc(&backDesc);
            const bool matchingSize = backDesc.Width == gHost.width &&
                                      backDesc.Height == gHost.height;
            gHost.sizeMismatch = !matchingSize;
            if (!matchingSize && !gHost.loggedSizeMismatch) {
                gHost.loggedSizeMismatch = true;
                std::ostringstream line;
                line << "result=refused detail=backbuffer_resized"
                     << " sessionSize=" << gHost.width << "x" << gHost.height
                     << " nowSize=" << backDesc.Width << "x" << backDesc.Height
                     << " note=restart_to_adopt_the_new_size";
                Log(line.str());
            }
            // Timed together: they are one logical "get me an image to draw
            // into", and splitting them would suggest the acquire can be slow
            // independently of the wait it exists to set up.
            const std::uint64_t acquireStart =
                timing ? preyvr::timing::MonotonicNanoseconds() : 0;
            ID3D11Texture2D* panelSource = backBuffer.Get();
            // Finish an already redirected draw even if the command channel
            // disabled capture between the native callback and this boundary.
            if (matchingSize && panelActive && HudInventoryIsOpen() && (InventoryCaptureEnabled() || inventoryTexture)) {
                if (!gHost.inventorySwapchain) {
                    gHost.inventorySwapchain = SessionLayerSwapchain();
                }
                inventoryPrepared = gHost.inventorySwapchain->Prepare(gHost.session, backDesc, gHost.colorFormat);
                if(inventoryPrepared && (InventoryStereoEnabled() || inventoryRightTexture)) {
                    if(!gHost.inventoryRightSwapchain)gHost.inventoryRightSwapchain=SessionLayerSwapchain();
                    inventoryStereoPrepared=gHost.inventoryRightSwapchain->Prepare(gHost.session,backDesc,gHost.colorFormat);
                }
                inventoryDescription = backDesc;
                if (inventoryTexture) {
                    // This draw was redirected already. If XR upload fails,
                    // retain a complete opaque inventory in the legacy panel.
                    D3D11_TEXTURE2D_DESC capturedDesc{}; inventoryTexture->GetDesc(&capturedDesc);
                    if (InventoryTextureCompatible(backDesc, capturedDesc)) panelSource = inventoryTexture;
                    // Held through both eye uploads.
                    const auto context = ImmediateContext(gHost.device);
                    const auto upload = inventoryPrepared ? gHost.inventorySwapchain->Copy(context.Get(), inventoryTexture)
                                                          : LayerSwapchain::Upload::failed;
                    inventorySubmitted = upload == LayerSwapchain::Upload::ready;
                    if(inventorySubmitted && inventoryRightTexture && inventoryStereoPrepared) {
                        const auto rightUpload=gHost.inventoryRightSwapchain->Copy(context.Get(),inventoryRightTexture);
                        inventoryStereoSubmitted=rightUpload==LayerSwapchain::Upload::ready;
                        if(!inventoryStereoSubmitted) {
                            SetInventoryStereoEnabled(0);
                            inventoryStereoPrepared=false;
                            if(rightUpload==LayerSwapchain::Upload::sessionFault)gStopRequested.store(true);
                            Log("result=refused detail=inventory_right_upload mono_fallback=1");
                        }
                    }
                    rendered = inventorySubmitted;
                    if (!inventorySubmitted) {
                        RefuseInventoryCapture("swapchain_upload_failed");
                        inventoryPrepared = false;
                        Log("result=refused detail=inventory_swapchain_upload legacy_panel_fallback=1");
                        if (upload == LayerSwapchain::Upload::sessionFault) gStopRequested.store(true);
                    }
                }
            }
            AcquiredImage image;
            if (matchingSize && !inventorySubmitted) image.Acquire(SessionSwapchainApi(), gHost.swapchain);
            if(image.WaitFailed())gStopRequested.store(true);
            if (timing && matchingSize) {
                gAcquireWait.AddNanoseconds(
                    preyvr::timing::MonotonicNanoseconds() - acquireStart);
            }
            const std::uint32_t imageIndex = image.Index();
            if (image.Waited() && imageIndex < gHost.images.size()) {
                if (const auto context = ImmediateContext(gHost.device)) {
                    if (gStereoSubmission.load(std::memory_order_acquire) && !panelActive) {
                        stereoPair = SubmitStereoPair(
                            context.Get(), backBuffer.Get(), imageIndex,
                            views, renderContract, hudTexture!=nullptr, dualFirstHeld);
                    } else {
                        // First light is a **flat mirror**: the same backbuffer
                        // into both eyes. It proves the whole path -- device,
                        // session, swapchain, submission -- without depending on
                        // the per-eye camera work, which is a separate experiment
                        // with its own failure modes.
                        for (std::uint32_t slice = 0; slice < 2; ++slice) {
                            context->CopySubresourceRegion(
                                gHost.images[imageIndex],
                                D3D11CalcSubresource(0, slice, 1),
                                0, 0, 0, panelSource, 0, nullptr);
                        }
                    }
                    rendered=panelActive || !gStereoSubmission.load() || stereoPair;
                }
                if(!image.Release()) {rendered=false;gStopRequested.store(true);}

                // **What FOV to declare, and why it is not the runtime's.**
                //
                // These pixels were rendered by Prey, through Prey's frustum.
                // Declaring `views[eye].fov` -- what the headset would like -- is
                // a statement about the image that is not true, and the runtime
                // cannot detect it: it reprojects to whatever is claimed, so a
                // wrong claim shows up as wrong depth and wrong scale rather than
                // as an error. See docs/SUBMISSION_CONTRACT.md.
                //
                // So when stereo submission is armed we declare the frustum the
                // game actually drew with, read live. Both eyes get the same one,
                // because Prey renders symmetric and rung 3 separates the eyes by
                // translation alone.
                //
                // Fails closed: if the camera cannot be read we submit no layer
                // at all rather than fall back to the runtime's FOV. A black
                // headset is honest and diagnosable; a plausible-looking wrong
                // image is neither.
                // **Each eye is submitted with the contract that rendered it.**
                // FAIL-STR-044: the two held images come from different frames, so
                // reading a single live pose and FOV here would relabel the older
                // eye's pixels with the newer eye's contract. In stereo the held
                // values are used; the flat mirror has no held pair and uses the
                // live ones, which is correct because its pixels are this frame's.
                bool haveDeclared = false;
                XrFovf liveDeclared{};
                if (!stereoPair) {
                    const auto preyFov = DeclaredFovFromLiveCamera();
                    if (preyFov) {
                        liveDeclared = ScaleDeclaredFov(*preyFov);
                        haveDeclared = true;
                    }
                }
                if(!stereoPair && !panelActive && !haveDeclared) rendered=false;
                for (int eye = 0; eye < 2; ++eye) {
                    projViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                    projViews[eye].pose = stereoPair ? gHost.eyePose[eye] : views[eye].pose;
                    projViews[eye].fov = stereoPair
                        ? gHost.eyeFov[eye]
                        : (haveDeclared ? liveDeclared : views[eye].fov);
                    projViews[eye].subImage.swapchain = gHost.swapchain;
                    projViews[eye].subImage.imageArrayIndex = static_cast<std::uint32_t>(eye);
                    projViews[eye].subImage.imageRect.offset = {0, 0};
                    gHost.submittedWidth = gHost.width;
                    gHost.submittedHeight = gHost.height;
                    projViews[eye].subImage.imageRect.extent = {
                        static_cast<std::int32_t>(gHost.width),
                        static_cast<std::int32_t>(gHost.height)};
                }
            } else if(image.Waited()) {
                image.Release();gStopRequested.store(true);
            }
        }
    }

    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space = gHost.space;
    layer.viewCount = 2;
    layer.views = projViews;
    XrCompositionLayerQuad panelLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad rightPanelLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad guideLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerCylinderKHR cylinderLayer{XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR};
    ui::Surface surface{};
    if(panelActive) {
        // The prototype's disparity and pointer mapping are defined on a plane.
        // Do not wrap stereo imagery onto a cylinder with a different geometry
        // (the curve is 0 while the planar inventory is up).
        surface={*gHost.menuPanel,gHost.panelAngle};
        FillMenuLayers(panelLayer,cylinderLayer,surface,
                       inventorySubmitted?gHost.inventorySwapchain->Handle():gHost.swapchain,
                       inventorySubmitted?XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT:0);
        if(inventoryStereoSubmitted) {
            rightPanelLayer=panelLayer;
            panelLayer.eyeVisibility=XR_EYE_VISIBILITY_LEFT;
            rightPanelLayer.eyeVisibility=XR_EYE_VISIBILITY_RIGHT;
            rightPanelLayer.subImage.swapchain=gHost.inventoryRightSwapchain->Handle();
        }
        if(rendered)EnsurePointerTexture();
    }
    // **VR Options** (VrOptionsLayer). Not gated on this frame having an image:
    // over the running game a frame can miss its eye pair, and dropping the
    // placement then would re-anchor the panel wherever the wearer looks next.
    // Such a frame submits nothing. Over a game menu it takes that menu's
    // anchor, and sits in front of it; over the running game, an anchor afresh.
    VrOptionsLayer::Frame options;
    if(VrOptionsOpen()&&haveViews) {
        if(!gHost.optionsLayer.Placed()&&!wantPanel)gHost.menuAnchor.reset();
        options=gHost.optionsLayer.Update(gHost.session,gHost.space,gHost.device,MenuAnchorFor(inputHead,views,reference),
            static_cast<float>(gUiDistanceCm.load(std::memory_order_relaxed))*.01f,
            reference,inputHead,frameState.predictedDisplayTime);
        if(options.sessionFault)gStopRequested.store(true);
        if(options.active) {SetVrOptionsSurface(&options.surface,reference);EnsurePointerTexture();}
        else {RequestVrOptions(false);Log("result=refused detail=vr_options_layer native_menu_retained=1");}
    } else {
        gHost.optionsLayer.Close(gHost.menuAnchor?&*gHost.menuAnchor:nullptr);
        SetVrOptionsSurface(nullptr,reference);
    }
    const bool optionsActive=options.active;
    UiPointerVisual pointer;
    // The surface the laser is tested against and its cursor drawn on: VR
    // Options' while it is up, else the game menu's while the menu takes the
    // pointer. The cursor used to go on the game menu's even for a hit on VR
    // Options, at that panel's (u, v). A hold frame points the same way.
    HeldPointer heldPointer;
    if(optionsActive) {
        PublishUiPointer(nullptr,0,0,0);
        if(gHost.pointerReady){pointer=OptionsPointer(options.surface);heldPointer={true,options.surface};}
    } else {
        const bool menuPointer=panelActive&&rendered&&gHost.pointerReady&&!VrOptionsInputOwned();
        pointer=PublishUiPointer(menuPointer?&surface:nullptr,gHost.width,gHost.height,gHost.surfaceSerial);
        if(menuPointer)heldPointer={true,surface};
    }
    XrCompositionLayerQuad cursorLayer{XR_TYPE_COMPOSITION_LAYER_QUAD},beamLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    bool cursorActive=false,beamActive=false;
    FillPointerLayers(pointer,heldPointer.surface,inputHead.position,cursorLayer,beamLayer,cursorActive,beamActive);
    const bool guideActive=panelActive && gHost.guideReady && gHost.guidePanel.has_value();
    if(guideActive) {
        const auto& p=*gHost.guidePanel;
        // Opaque instruction card.
        FillQuad(guideLayer,gHost.space,p.pose,{p.width,p.height},gHost.guideSwapchain,{{0,0},{kGuideWidth,kGuideHeight}},0);
    }
    const FrameView view{views,inputHead,reference,frameState.predictedDisplayTime,rendered,haveViews,panelActive,
                         optionsActive};
    XrCompositionLayerQuad hudLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    float hudPanelWidth=0; // this frame's HUD panel at 2 m: what the world reticle keeps the size of
    const bool hudActive=BuildHudLayer(view,hudTexture,hudLayer,hudPanelWidth);
    // **The radial wheel.** Prey's Favorites Wheel, lifted out of the HUD while
    // it is open (HudLayer RequestHudPart) and held where it opened, as a book is
    // (ui::PopupHold): in front of where the wearer looked, at the HUD panel's
    // distance and size, so it looks as it did on the HUD but stays put while the
    // head turns. Asked for only with the HUD on its panel: drawn in the world
    // instead, the HUD keeps its wheel. Over a game menu nothing is lifted.
    RequestHudPart(HudPart::wheel,HudLayerEnabled() && haveViews && !panelActive);
    // **The world reticle.** Prey's crosshair, with the interaction prompt and
    // quick loot that follow it, lifted out of the HUD (HudPart::reticle) and
    // redrawn centred, then shown where the aim ray hits (ReticleFollow
    // PublishReticleTarget): its scene hit, else the fallback distance. The quad
    // faces the eyes and is as wide, for its distance, as the HUD panel is at 2 m,
    // times the size setting, so it looks the size it did on the HUD but at the
    // depth it marks. Asked for only with the HUD on its panel and a fresh target;
    // otherwise the crosshair stays on the HUD panel. A frame Prey hides it (the
    // wheel, a menu) draws nothing.
    ReticleTarget reticleTarget{};
    const bool reticleTargetFresh=TryGetReticleTarget(reticleTarget);
    RequestHudPart(HudPart::reticle,ReticleInWorld() && HudLayerEnabled() && haveViews && !panelActive && reticleTargetFresh);
    XrCompositionLayerQuad reticleLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const bool reticleActive=BuildReticleLayer(view,reticleImage,reticleTargetFresh?&reticleTarget:nullptr,hudPanelWidth,
                                               reticleLayer);
    XrCompositionLayerQuad wheelLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const bool wheelActive=BuildWheelLayer(view,wheelImage,wheelLayer);
    // The text and note panels' size and distance. The HUD panel's physical
    // size, brought nearer: FitPanel's size is an angle, so the scale carries the
    // 2 m width to the nearer distance. The fit's corner test still shrinks it
    // back inside the margin if needed.
    const float textDistance=static_cast<float>(gTextPanelCm.load(std::memory_order_relaxed))*.01f;
    const float textScale=static_cast<float>(gUiScalePercent.load(std::memory_order_relaxed))*.01f*(2.0f/textDistance);
    XrCompositionLayerQuad noteLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const bool noteActive=BuildNoteLayer(view,noteTexture,textDistance,textScale,noteLayer);
    XrCompositionLayerQuad textLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const bool textActive=BuildTextLayer(view,textTexture,textDistance,textScale,textLayer);
    XrCompositionLayerQuad wristLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const bool wristActive=BuildWristLayer(view,nativeWristTexture,wristLayer);
    XrCompositionLayerQuad slotLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const bool slotActive=BuildSlotLayer(view,slotLayer);
    // **VR Options is drawn over what is shown**: the game's menu while one is
    // up, else the world -- the running game (ui::ComposeFrame has the order).
    ui::FrameShown shown;
    shown.menu=panelActive;shown.menuCylinder=panelActive&&surface.angle>0;shown.menuRightEye=inventoryStereoSubmitted;
    shown.guide=guideActive;shown.hud=hudActive;shown.reticle=reticleActive;shown.wheel=wheelActive;shown.note=noteActive;shown.text=textActive;
    shown.options=optionsActive;
    shown.beam=beamActive;shown.cursor=cursorActive;shown.wrist=wristActive;shown.slot=slotActive;
    // By value, so a successful frame can be kept for hold frames.
    FrameLayerSet built;
    built.shown=shown;built.views[0]=projViews[0];built.views[1]=projViews[1];built.world=layer;
    built.menu=panelLayer;built.menuCylinder=cylinderLayer;built.menuRightEye=rightPanelLayer;built.guide=guideLayer;
    built.hud=hudLayer;built.reticle=reticleLayer;built.wheel=wheelLayer;built.note=noteLayer;built.text=textLayer;
    built.options=options.layer;built.beam=beamLayer;built.cursor=cursorLayer;built.wrist=wristLayer;
    built.slot=slotLayer;
    const auto layers=built.Ordered();

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = rendered ? static_cast<std::uint32_t>(layers.size()) : 0;
    endInfo.layers = rendered ? layers.data() : nullptr;
    const std::uint64_t endStart = timing ? preyvr::timing::MonotonicNanoseconds() : 0;
    const auto endResult=xrEndFrame(gHost.session, &endInfo);
    // Arm only after a complete successful frame. Until then the native movie
    // keeps rendering normally and the existing full-menu panel remains usable.
    // inventoryPrepared needs the menu panel, which needs located views: the
    // runtime IPD above is this frame's.
    if (rendered && endResult == XR_SUCCESS && inventoryPrepared && !gStopRequested.load() && InventoryCaptureEnabled()) {
        SetInventoryConsumerReady(&inventoryDescription,gHost.menuPanel?gHost.menuPanel->width:0,
            runtimeIpd,inventoryStereoPrepared);
    }
    if (timing) {
        const std::uint64_t now = preyvr::timing::MonotonicNanoseconds();
        gEndFrame.AddNanoseconds(now - endStart);
        gServiceTotal.AddNanoseconds(now - serviceStart);
    }
    gHost.contract.OnSubmitted(thread);
    PublishResolution();

    if (rendered && XR_SUCCEEDED(endResult)) {
        gHost.heldLayers=built;gHost.heldPointer=heldPointer;gHost.heldValid=true;
        if(wristActive)RecordWristLayerFrame();
        if (inventorySubmitted) {
            if (gInventoryLayerFrames.fetch_add(1)==0)
                Log("result=0 detail=first_inventory_layer_submitted separate_swapchain=1 array_size=1 world_copy=0");
            gHost.submittedWidth=gHost.width;gHost.submittedHeight=gHost.height;
        }
        if (panelActive) { gUiPanelFrames.fetch_add(1); }
        const unsigned long long count = gSubmitted.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count == 1) {
            Log("result=0 detail=first_frame_submitted");
        }
    }
}

void ObserveXrBackbuffer(void* renderer)
{
    if(!renderer)return;
    const auto texture=AcquireBackBuffer(renderer);
    if(!texture)return;
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    static BackbufferSample sample{}; // frame observer / render thread owns it
    const auto now=MonotonicNanoseconds();
    if(sample.width!=desc.Width || sample.height!=desc.Height) {
        sample={desc.Width,desc.Height,now,now,0};
    }
    sample.stamp=now;++sample.frames;gBackbufferSample.Publish(sample);
}

bool XrBackbufferReady(unsigned int width,unsigned int height)
{
    BackbufferSample s{};
    const auto now=MonotonicNanoseconds();
    return gBackbufferSample.TryRead(s) && FreshSample(now,s.stamp) && s.width>0 && s.height>0 &&
        s.frames>=20 && now>=s.changed && now-s.changed>=1000000000ull &&
        (!width || width==s.width) && (!height || height==s.height);
}

unsigned long long InventoryLayerFrameCount() { return gInventoryLayerFrames.load(); }

float XrRuntimeIpdMetres() { return gRuntimeIpd.load(); }

} // namespace preyvr::dll
