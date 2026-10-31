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
#include "UiPointer.h"
#include "HudLayer.h"
#include "LayerSwapchain.h"
#include "RendererAccess.h"
#include "XrPose.h"
#include "LoadingScreen.h"
#include "VrOptionsLayer.h"
#include "VrOptionsRuntime.h"
#include "BodyEquipment.h"
#include "RoomScaleLane.h"
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

// The overlay layers of Prey's frames, over the world (XrSessionState.h): the
// HUD panel, the world reticle, the wheel, the note and text panels, the wrist
// card, the slot card and the head-in-wall fade. ServiceXrFrame calls them in
// this order; each fills its layer and says whether it is shown.

namespace preyvr::dll {
using namespace xrsession;

namespace {

// One captured movie's texture into its own swapchain (the HUD, text, note,
// wheel and reticle panels), created on first use. True when this frame's image is ready to
// submit. A refusal logs which step, the runtime's answer and both formats,
// drops the swapchain and hands the movies back to the scene through `refuse`,
// where they are drawn per eye. XR frame thread, under gMutex.
bool UploadPanelCapture(std::unique_ptr<LayerSwapchain>& swapchain, bool& logged,
                        ID3D11Texture2D* texture, const char* detail, void (*refuse)(const char*))
{
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    if(!swapchain)swapchain=SessionLayerSwapchain();
    if(!swapchain->Prepare(gHost.session,desc,gHost.colorFormat)) {
        const auto refusal=swapchain->LastRefusal();
        std::ostringstream line;
        line<<"result=refused detail="<<detail<<" reason="<<LayerSwapchain::RefusalName(refusal)
            <<" xrResult="<<swapchain->LastResult()<<" format="<<gHost.colorFormat
            <<" captureFormat="<<desc.Format<<" size="<<desc.Width<<"x"<<desc.Height;
        Log(line.str());
        swapchain.reset();
        refuse(LayerSwapchain::RefusalName(refusal));
        return false;
    }
    if(!logged) {
        logged=true;
        std::ostringstream line;
        line<<"result=0 detail="<<detail<<" format="<<gHost.colorFormat<<" captureFormat="<<desc.Format
            <<" size="<<desc.Width<<"x"<<desc.Height;
        Log(line.str());
    }
    const auto upload=swapchain->Copy(ImmediateContext(gHost.device).Get(),texture);
    if(upload==LayerSwapchain::Upload::sessionFault){gStopRequested.store(true);refuse("image_wait_or_release");}
    return upload==LayerSwapchain::Upload::ready;
}

// A captured movie's panel: alpha-blended over the world, both eyes, the whole capture.
void FillPanelLayer(XrCompositionLayerQuad& layer, const ui::Panel& p, XrSwapchain swapchain,
                    const D3D11_TEXTURE2D_DESC& desc)
{
    FillQuad(layer,gHost.space,p.pose,{p.width,p.height},swapchain,
             {{0,0},{static_cast<int>(desc.Width),static_cast<int>(desc.Height)}},
             XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT);
}

} // namespace

namespace xrsession {

// **The HUD on its panel** (HudLayer): the captured HUD, fitted to the head at
// 2 m. `panelWidth` is that panel's width, which the world reticle keeps the
// size of; it is left 0 without one.
bool BuildHudLayer(const FrameView& view, ID3D11Texture2D* hudTexture, XrCompositionLayerQuad& layer, float& panelWidth)
{
    bool active=false;
    // A held eye may still contain the native reticle from before capture was
    // enabled/refused. Never add another HUD over that eye's baked-in copy.
    const bool hudRemovedFromWorld=gHost.hudCapturePair.CanOverlay(gStereoSubmission.load(),hudTexture!=nullptr);
    if(view.rendered && view.haveViews && !view.panelActive && HudLayerEnabled() && hudRemovedFromWorld) {
        auto captured=hudTexture;
        if(captured) {
            D3D11_TEXTURE2D_DESC desc{};captured->GetDesc(&desc);
            // A refusal drops the swapchain, so the next VR start tries afresh.
            active=UploadPanelCapture(gHost.hudSwapchain,gHost.hudSwapchainLogged,captured,"hud_swapchain",
                                      RefuseHudLayer);
            auto panel=ui::FitPanel(view.head,OpticalEyes(view.views),
                static_cast<float>(desc.Width)/desc.Height,2.0f,
                static_cast<float>(gUiScalePercent.load(std::memory_order_relaxed))*.01f);
            active=active && panel.has_value();
            if(active) {
                const auto& p=*panel;
                FillPanelLayer(layer,p,gHost.hudSwapchain->Handle(),desc);
                SetHudLayerPresentation(true,p.width,p.height,2);
                panelWidth=p.width;
            }
        }
    }
    if(!active)SetHudLayerPresentation(false);
    return active;
}

// **The world reticle** (the comment at its RequestHudPart in ServiceXrFrame):
// the crosshair lifted out of the HUD, as wide for its distance as the HUD panel
// is at 2 m. `target` is this frame's aim target, null when it is not fresh.
bool BuildReticleLayer(const FrameView& view, const HudPartImage& image, const ReticleTarget* target,
                       float hudPanelWidth, XrCompositionLayerQuad& layer)
{
    bool active=false;
    if(view.rendered && view.haveViews && !view.panelActive && image.texture && image.drawn && target &&
       hudPanelWidth>0 &&
       UploadPanelCapture(gHost.reticleSwapchain,gHost.reticleSwapchainLogged,image.texture,"reticle_swapchain",
                          [](const char* reason){RefuseHudPart(HudPart::reticle,reason);})) {
        D3D11_TEXTURE2D_DESC desc{};image.texture->GetDesc(&desc);
        const Vec3 offset{target->point.x-view.head.position.x,target->point.y-view.head.position.y,
                          target->point.z-view.head.position.z};
        const float seen=std::sqrt(offset.x*offset.x+offset.y*offset.y+offset.z*offset.z);
        const float width=ui::AngularWidth(seen,hudPanelWidth,2.0f,static_cast<float>(ReticleSizePercent())*.01f);
        if(const auto quad=ui::FacingQuad(target->point,view.head.position,width,
                                          width*static_cast<float>(desc.Height)/desc.Width)) {
            FillPanelLayer(layer,*quad,gHost.reticleSwapchain->Handle(),desc);
            active=true;
        }
    }
    if(HudLayerEnabled() && view.rendered && view.haveViews && !view.panelActive)NoteReticleShown(active);
    return active;
}

// **The radial wheel** (the comment at its RequestHudPart in ServiceXrFrame):
// held where it opened, at the HUD panel's distance and size.
bool BuildWheelLayer(const FrameView& view, const HudPartImage& image, XrCompositionLayerQuad& layer)
{
    bool active=false;
    const bool shown=view.rendered && view.haveViews && !view.panelActive && image.texture;
    const auto* anchor=gHost.wheelHold.Update(shown,MonotonicNanoseconds(),view.head,
        shown?OpticalEyes(view.views):std::array<ui::Eye,2>{},view.reference);
    if(anchor && UploadPanelCapture(gHost.wheelSwapchain,gHost.wheelSwapchainLogged,image.texture,
                                    "wheel_swapchain",[](const char* reason){RefuseHudPart(HudPart::wheel,reason);})) {
        D3D11_TEXTURE2D_DESC desc{};image.texture->GetDesc(&desc);
        if(const auto panel=ui::PlacePopup(*anchor,static_cast<float>(desc.Width)/desc.Height,2.0f,
                static_cast<float>(gUiScalePercent.load(std::memory_order_relaxed))*.01f)) {
            FillPanelLayer(layer,*panel,gHost.wheelSwapchain->Handle(),desc);
            active=true;
        }
    }
    return active;
}

// **The note panel.** Notes and books, lifted out of the scene like the HUD
// (HudLayer NoteLayerTexture) and held where they opened (ui::PopupHold):
// placed in front of where the wearer looked on the first frame one is shown
// and kept there while it stays up, so the head moves over the page instead
// of carrying it along. Re-placed each frame from that one anchor, so a
// changed distance or size applies where it opened.
// `distance` and `scale`: the text panels' (ServiceXrFrame).
bool BuildNoteLayer(const FrameView& view, ID3D11Texture2D* texture, float distance, float scale,
                    XrCompositionLayerQuad& layer)
{
    bool active=false;
    const bool shown=view.rendered && view.haveViews && !view.panelActive && texture;
    const auto* anchor=gHost.noteHold.Update(shown,MonotonicNanoseconds(),view.head,
        shown?OpticalEyes(view.views):std::array<ui::Eye,2>{},view.reference);
    if(anchor && UploadPanelCapture(gHost.noteSwapchain,gHost.noteSwapchainLogged,texture,"note_swapchain",
                                    RefuseNoteLayer)) {
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        if(const auto panel=ui::PlacePopup(*anchor,static_cast<float>(desc.Width)/desc.Height,distance,
                                           scale)) {
            FillPanelLayer(layer,*panel,gHost.noteSwapchain->Handle(),desc);
            active=true;
        }
    }
    return active;
}

// **The text panel.** Subtitles and tutorial cards, lifted out of the scene
// the same way (HudLayer TextLayerTexture) and fitted to the head every
// frame, so each keeps the place it has on screen. Neither panel is tied to
// the HUD layer's own capture: a frame may have text and no HUD.
bool BuildTextLayer(const FrameView& view, ID3D11Texture2D* texture, float distance, float scale,
                    XrCompositionLayerQuad& layer)
{
    bool active=false;
    if(view.rendered && view.haveViews && !view.panelActive && texture &&
       UploadPanelCapture(gHost.textSwapchain,gHost.textSwapchainLogged,texture,"text_swapchain",RefuseTextLayer)) {
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        if(const auto panel=ui::FitPanel(view.head,OpticalEyes(view.views),static_cast<float>(desc.Width)/desc.Height,
                                         distance,scale)) {
            FillPanelLayer(layer,*panel,gHost.textSwapchain->Handle(),desc);
            active=true;
        }
    }
    return active;
}

// **The wrist card** (WristLane): the hologram on the drawn forearm, or the
// grip-anchored card, gated on `wrist::Gate` and shown while looked at.
bool BuildWristLayer(const FrameView& view, ID3D11Texture2D* nativeWristTexture, XrCompositionLayerQuad& layer)
{
    bool active=false;
    TrackingFrame wristFrame{};
    // Each condition as a bit (wrist.report shows which one closed the gate).
    const bool wristTracking=TryGetTrackingFrame(wristFrame)&&wristFrame.displayTime==view.displayTime&&
        FreshSample(MonotonicNanoseconds(),wristFrame.publishedNs)&&
        IsPoseUsable(wristFrame.head,wristFrame.headValidity,kMaxPoseAgeNs)&&
        IsPoseUsable(wristFrame.hands[0].gripPose,wristFrame.hands[0].gripValidity,kMaxPoseAgeNs);
    const wrist::Gate gate{WristDisplayEnabled(),view.haveViews,view.rendered,!view.panelActive,!view.optionsActive,HudGameplayInputAllowed(),
        !TwoHandedAimHeld(),!stereo::Recentering(view.reference),wristTracking};
    const unsigned wristGate=gate.Bits();
    const bool wristInput=wristGate==wrist::Gate::kOpen;
    NoteWristGate(wristGate);
    // The hologram on the drawn forearm (WristLane), or Funk's grip-anchored
    // card (wrist.anchor 0). Both composite the same native status texture.
    const bool hologram=!WristLegacyAnchor();
    WristDecision hologramDecision{};
    auto wristPose=equipment::WristPose(wristFrame.hands[0].gripPose);
    if(hologram){
        hologramDecision=DecideWrist(wristInput,wristFrame,gHost.eyeDisplayTime[0],view.displayTime,
            static_cast<float>(NativeWristWidth)/NativeWristHeight);
        gHost.wristVisible=hologramDecision.active;
        wristPose=hologramDecision.pose;
    } else gHost.wristVisible=wristInput&&equipment::WristVisible(wristFrame.head,wristPose,gHost.wristVisible);
    DebugOverlayWristDecision(wristInput,gHost.wristVisible);
    RequestNativeWristCapture(hologram?hologramDecision.capture:gHost.wristVisible);
    if(nativeWristTexture)gHost.wristCapturedNs=MonotonicNanoseconds();
    // A frame without a new capture reuses the last one for a moment rather
    // than blinking the card out (the HUD's draw and ours are not lock-stepped).
    const bool reuseWrist=hologram&&!nativeWristTexture&&gHost.wristFitter&&
        FreshSample(MonotonicNanoseconds(),gHost.wristCapturedNs,250000000ull);
    if(gHost.wristVisible){
        if(nativeWristTexture||reuseWrist){
            if(!gHost.wristFitter)gHost.wristFitter=std::make_unique<NativeWristTexture>();
            const auto context=ImmediateContext(gHost.device);
            const float wristAlpha=hologram?hologramDecision.alpha:1.f;
            if(auto fitted=nativeWristTexture?gHost.wristFitter->Fit(context.Get(),nativeWristTexture,wristAlpha,hologram):
                                              gHost.wristFitter->Refit(context.Get(),wristAlpha,hologram)){
                if(!gHost.wristSwapchain)gHost.wristSwapchain=SessionLayerSwapchain();
                D3D11_TEXTURE2D_DESC d{};fitted->GetDesc(&d);
                // GPU fitting swizzles BGRA to RGBA, but preserves encoded colour
                // values and premultiplication. Inherit the main XR interpretation.
                const auto format=gHost.colorFormat==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||
                    gHost.colorFormat==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB?
                    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;
                if(gHost.wristSwapchain->Prepare(gHost.session,d,format)){
                    const auto result=gHost.wristSwapchain->Copy(context.Get(),fitted);
                    active=result==LayerSwapchain::Upload::ready;
                    if(result==LayerSwapchain::Upload::sessionFault)gStopRequested.store(true);
                }
            }
        }
        active=active&&view.reference==HeadTrackingReferenceGeneration();
        if(active){
            const float width=WristCardWidthMetres();
            FillQuad(layer,gHost.space,wristPose,
                     hologram?XrExtent2Df{hologramDecision.width,hologramDecision.height}:
                              XrExtent2Df{width,width*NativeWristHeight/NativeWristWidth},
                     gHost.wristSwapchain->Handle(),{{0,0},{NativeWristWidth,NativeWristHeight}},
                     XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT);
        }
    }
    return active;
}

// **The slot card** (SlotFeedback): a holster or medkit notice, painted by GDI
// and repainted only when the notice changes.
bool BuildSlotLayer(const FrameView& view, XrCompositionLayerQuad& layer)
{
    bool active=false;
    equipment::SlotMessage slotMessage{};TrackingFrame slotFrame{};
    if(view.rendered&&view.haveViews&&!view.panelActive&&!view.optionsActive&&TryGetTrackingFrame(slotFrame)&&ReadSlotFeedback(slotFrame,slotMessage)){
        const auto panel=equipment::SlotPanel(view.head,OpticalEyes(view.views));
        // GDI pixels are sRGB, independently of the scene's colour encoding. The
        // card is shorter than the default floor (480); its own size is the floor.
        if(panel)gHost.slotImage.Ensure(SessionSwapchainApi(),gHost.session,gHost.device,Log);
        if(panel&&gHost.slotImage.Ready()){
            if(!gHost.slotReady||gHost.slotPainted!=slotMessage.kind){
                auto pixels=DrawSlotFeedback(slotMessage.kind);
                if(gHost.slotImage.Format()==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
                    for(size_t n=0;n+3<pixels.size();n+=4)std::swap(pixels[n],pixels[n+2]);
                const auto context=ImmediateContext(gHost.device);
                if(context&&!pixels.empty()){
                    const auto upload=gHost.slotImage.Paint(context.Get(),pixels.data());
                    gHost.slotReady=upload==LayerSwapchain::Upload::ready;
                    if(upload==LayerSwapchain::Upload::sessionFault)gStopRequested.store(true);
                    if(gHost.slotReady)gHost.slotPainted=slotMessage.kind;
                }
            }
            active=gHost.slotReady&&stereo::ReferenceCurrent(view.reference,HeadTrackingReferenceGeneration())&&
                slotMessage.Current(MonotonicNanoseconds(),slotFrame.epoch,view.reference,HudMenuEpoch());
            if(active){const auto& p=*panel;
                FillQuad(layer,gHost.space,p.pose,{p.width,p.height},gHost.slotImage.Handle(),
                         {{0,0},{SlotFeedbackWidth,SlotFeedbackHeight}},0);
            }
        }
    }
    return active;
}

// The head-in-wall fade (RoomScaleLane.h): while the head is through geometry
// the view goes black -- in over 0.1 s, out over 0.2 s -- so walking into a
// wall cannot show what is behind it. Over everything.
bool BuildFadeLayer(const FrameView& view, XrCompositionLayerQuad& layer)
{
    bool active=false;
    // Repainted per level change only, so its size (Host::kFadeWidth)
    // costs nothing per frame.
    constexpr unsigned kFadeWidth=Host::kFadeWidth, kFadeHeight=Host::kFadeHeight, kFadeLevels=ui::kFadeLevels;
    const bool fadeWanted=view.rendered&&view.haveViews&&!view.panelActive&&!view.optionsActive&&BodyHeadOut();
    const int level=ui::StepFade(gHost.fade,view.displayTime,fadeWanted);
    if(level>0&&view.rendered&&view.haveViews){
        gHost.fadeImage.Ensure(SessionSwapchainApi(),gHost.session,gHost.device,Log);
        if(gHost.fadeImage.Ready()&&level!=gHost.fadePainted){
            // Black is zero in either channel order; alpha is linear even in sRGB.
            const auto alpha=static_cast<unsigned char>(level*255/kFadeLevels);
            std::vector<unsigned char> pixels(static_cast<size_t>(kFadeWidth)*kFadeHeight*4,0);
            for(size_t n=3;n<pixels.size();n+=4)pixels[n]=alpha;
            if(const auto context=ImmediateContext(gHost.device)){
                const auto upload=gHost.fadeImage.Paint(context.Get(),pixels.data());
                if(upload==LayerSwapchain::Upload::ready)gHost.fadePainted=level;
                if(upload==LayerSwapchain::Upload::sessionFault)gStopRequested.store(true);
            }
        }
        if(gHost.fadeImage.Ready()&&gHost.fadePainted==level){
            // 0.2 m ahead of the head -- clear of any compositor near plane --
            // and 5 m square: about 85 degrees each way.
            const auto& q=view.head.orientation;
            const Vec3 ahead=Rotate(q,Vec3{0,0,-.2f});
            FillQuad(layer,gHost.space,
                     Pose{q,{view.head.position.x+ahead.x,view.head.position.y+ahead.y,view.head.position.z+ahead.z}},
                     {5.f,5.f},gHost.fadeImage.Handle(),
                     {{0,0},{static_cast<int32_t>(kFadeWidth),static_cast<int32_t>(kFadeHeight)}},
                     XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT);
            active=true;
            gFadeFrames.fetch_add(1,std::memory_order_relaxed);
        }
    }
    return active;
}

} // namespace xrsession

} // namespace preyvr::dll
