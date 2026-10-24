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

// The game menu's panel: placed from the menu anchor, fitted with its guide
// card, shown flat or curved; the laser's texture and quads; and the panel's
// settings (XrSessionState.h).

namespace preyvr::dll {
using namespace xrsession;

namespace xrsession {

void EnsurePointerTexture()
{
    if(gHost.pointerAttempted)return;
    gHost.pointerAttempted=true;
    const auto api=SessionSwapchainApi();
    const auto format=FirstListedFormat(api.formats,gHost.session,
                                        {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB});
    if(format==DXGI_FORMAT_UNKNOWN)return;
    std::array<std::uint8_t,kPointerWidth*kPointerHeight*4> pixels{};
    for(int y=0;y<kPointerHeight;++y)for(int x=0;x<kPointerWidth;++x) {
        const float dx=static_cast<float>(x)-15.5f,dy=static_cast<float>(y)-15.5f;
        const float opacity=x<32?std::clamp(14.5f-std::sqrt(dx*dx+dy*dy),0.f,1.f):
            x>=40?ui::LaserFade(1-(static_cast<float>(y)+.5f)/kPointerHeight):0.f;
        const auto alpha=static_cast<std::uint8_t>(opacity*255);
        const auto offset=static_cast<std::size_t>((y*kPointerWidth+x)*4);
        pixels[offset]=format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB?90:255;
        pixels[offset+1]=235;pixels[offset+2]=format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB?255:90;pixels[offset+3]=alpha;
    }
    const auto upload=CreateStaticLayer(api,gHost.session,gHost.device,format,kPointerWidth,kPointerHeight,
                                        pixels.data(),gHost.pointerSwapchain);
    if(upload.waitFailed){gStopRequested.store(true);return;}
    if(!upload.waited)return;
    gHost.pointerReady=upload.ready;
    Log(std::string("ui_pointer texture_ready=")+(gHost.pointerReady?"1":"0"));
}

// The located eyes as UiPanel describes them: pose and frustum angles.
std::array<ui::Eye,2> OpticalEyes(const XrView (&views)[2])
{
    std::array<ui::Eye,2> eyes{};
    for (int eye = 0; eye < 2; ++eye) {
        const auto& p = views[eye].pose;
        const auto& f = views[eye].fov;
        eyes[eye] = {FromXrPose(p), f.angleLeft, f.angleRight, f.angleUp, f.angleDown};
    }
    return eyes;
}

// The shared menu anchor (Host::menuAnchor), taken afresh when there is none
// or a recenter has completed since it was taken (an odd generation is one in
// progress). XR frame thread, under gMutex.
const ui::MenuAnchor& MenuAnchorFor(const Pose& head, const XrView (&views)[2], std::uint64_t reference)
{
    if (!gHost.menuAnchor || (!stereo::Recentering(reference) && reference != gHost.menuAnchorReference)) {
        gHost.menuAnchor = ui::AnchorMenu(head, OpticalEyes(views));
        gHost.menuAnchorReference = reference;
    }
    return *gHost.menuAnchor;
}

// The laser (ui::PointerQuadsFor) as two layers on the pointer texture: the dot
// for the cursor; for the beam, the dot's centre texel, or the fading strip when
// it hits nothing. Prey's frames and hold frames both draw it here.
void FillPointerLayers(const UiPointerVisual& pointer, const ui::Surface& surface, const Vec3& head,
                       XrCompositionLayerQuad& cursor, XrCompositionLayerQuad& beam, bool& cursorActive, bool& beamActive)
{
    cursorActive=beamActive=false;
    if(!pointer.active)return;
    const auto fill=[](XrCompositionLayerQuad& target,const ui::Panel& p,const XrRect2Di& rect) {
        FillQuad(target,gHost.space,p.pose,{p.width,p.height},gHost.pointerSwapchain,rect,
                 XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT);
    };
    const auto quads=ui::PointerQuadsFor(pointer.aim,pointer.hit,pointer.pressed,surface,head);
    if(quads.cursor){fill(cursor,*quads.cursor,kCursorRect);cursorActive=true;}
    if(quads.beam){fill(beam,*quads.beam,quads.fades?kFadingBeamRect:kSolidBeamRect);beamActive=true;}
}

// Whether the game menu's panel must be fitted again: none yet, the curve
// changed, or a recenter has completed since it was placed.
bool MenuPanelsStale(float curve, unsigned long long reference)
{
    return !gHost.menuPanel || curve!=gHost.panelAngle || (!stereo::Recentering(reference) && reference!=gHost.panelReference);
}

// **The game menu's panel**, and the guide card under it when that is on,
// fitted from the shared menu anchor (Host::menuAnchor). Prey's frames, and hold
// frames showing the loading screen; under gMutex.
void PlaceMenuPanels(const Pose& head, const XrView (&views)[2], unsigned long long reference, float curve)
{
    const auto& anchor=MenuAnchorFor(head,views,reference);
    const auto& upright=anchor.upright;
    const auto& optical=anchor.eyes;
    const float aspect=static_cast<float>(gHost.width)/static_cast<float>(gHost.height);
    constexpr float guideRatio=static_cast<float>(kGuideHeight)/kGuideWidth;
    constexpr float gapRatio=.015f;
    const float margin=UiFitMargin();
    const bool wantGuide=gUiGuide.load(std::memory_order_relaxed);
    // Without the card the menu is fitted on its own aspect, so the height
    // the card and its gap used to reserve goes back to the menu instead of
    // being left empty -- which is the whole point of switching it off.
    const float stackAspect=wantGuide?1/(1/aspect+guideRatio+gapRatio):aspect;
    // **Upright, at eye level, facing the wearer square-on**, ui.distance
    // ahead of the way the face was turned when the anchor was taken (on
    // opening, not now: a setting change re-places the menu from the same
    // anchor). Fitted from the head itself the menu followed its pitch and
    // roll: opened while looking down, it came out low, tilted and rolled.
    // The anchor's eyes are levelled so the fit sizes it for a level gaze,
    // which also keeps a menu opened while looking down from failing the
    // corner test (UiPanel.h). The stack's centre is what sits at eye
    // level: with the guide card on, the menu rides half the card above it.
    const float distance=static_cast<float>(gUiDistanceCm.load(std::memory_order_relaxed))*.01f;
    auto layout=ui::FitPanel(upright,optical,stackAspect,distance,
        static_cast<float>(gUiScalePercent.load(std::memory_order_relaxed))*.01f,margin);
    if(layout) {
        gHost.menuPanel.reset();gHost.guidePanel.reset();
        if(const auto stack=ui::FitMenuStack(*layout,aspect,guideRatio,gapRatio,wantGuide,curve,optical,margin)) {
            gHost.menuPanel=stack->menu;
            gHost.guidePanel=stack->guide;
        }
    }
    // ui.offset carries the fitted stack around the head as one piece. After
    // the fit, not before: the fit sizes for straight ahead, and an offset
    // view would shrink or refuse a panel the wearer chose to put aside.
    const int offsetRight=gUiOffsetRight.load(std::memory_order_relaxed);
    const int offsetUp=gUiOffsetUp.load(std::memory_order_relaxed);
    if(gHost.menuPanel && (offsetRight || offsetUp)) {
        const float right=static_cast<float>(offsetRight)*kRadiansPerDegree,up=static_cast<float>(offsetUp)*kRadiansPerDegree;
        gHost.menuPanel->pose=ui::AroundHead(upright,gHost.menuPanel->pose,right,up);
        if(gHost.guidePanel)gHost.guidePanel->pose=ui::AroundHead(upright,gHost.guidePanel->pose,right,up);
    }
    gHost.panelReference=reference;
    gHost.panelAngle=curve;
    ++gHost.surfaceSerial;
    Log("ui_surface angle_degrees="+std::to_string(curve*kDegreesPerRadian)+" distance_m="+std::to_string(distance)+
        " offset_degrees="+std::to_string(offsetRight)+","+std::to_string(offsetUp)+
        " placed="+(gHost.menuPanel?"1":"0"));
}

// The game menu's panel as a quad of the whole of `swapchain`'s first slice,
// and as the cylinder it is shown on when curved (surface.angle > 0).
void FillMenuLayers(XrCompositionLayerQuad& quad, XrCompositionLayerCylinderKHR& cylinder, const ui::Surface& surface,
                    XrSwapchain swapchain, XrCompositionLayerFlags flags)
{
    const auto& p=surface.panel;
    FillQuad(quad,gHost.space,p.pose,{p.width,p.height},swapchain,
             {{0,0},{static_cast<int>(gHost.width),static_cast<int>(gHost.height)}},flags);
    quad.subImage.imageArrayIndex=0;
    if(surface.angle>0) {
        const auto axis=ui::CylinderAxis(surface);
        cylinder.space=quad.space;cylinder.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        cylinder.subImage=quad.subImage;
        cylinder.layerFlags=flags;
        cylinder.pose=ToXrPose(axis);
        cylinder.radius=surface.panel.width/surface.angle;
        cylinder.centralAngle=surface.angle;
        cylinder.aspectRatio=surface.panel.width/surface.panel.height;
    }
}

} // namespace xrsession

DWORD SetUiPanelMode(unsigned int mode) {
    if(mode>2) return 1;
    gUiPanelMode.store(mode);if(mode==0)ClearUiPointer();return 0;
}

DWORD SetUiCurveDegrees(unsigned int degrees) {
    if(degrees>60)return ERROR_INVALID_PARAMETER;
    gUiCurveDegrees.store(degrees);return 0;
}

unsigned UiCurveDegrees(){return gUiCurveDegrees.load();}

unsigned int UiPanelMode() { return gUiPanelMode.load(); }

unsigned long long UiPanelFrameCount() { return gUiPanelFrames.load(); }

DWORD SetUiScalePercent(unsigned int percent)
{
    if (percent < 20 || percent > 200) {
        Log("result=refused detail=ui_scale_out_of_range");
        return 1;
    }
    gUiScalePercent.store(percent, std::memory_order_release);
    // Invalidate on the frame thread, including when this setting arrives
    // before the session exists. A later write is picked up next frame.
    gPanelSettingsGeneration.fetch_add(1, std::memory_order_release);
    std::ostringstream line;
    line << "result=0 detail=ui_scale percent=" << percent;
    Log(line.str());
    return 0;
}

DWORD UiScalePercent() { return gUiScalePercent.load(std::memory_order_acquire); }

DWORD SetTextPanelDistanceCm(unsigned int centimetres)
{
    if (centimetres < 100 || centimetres > 200) {
        Log("result=refused detail=text_panel_distance_out_of_range");
        return 1;
    }
    gTextPanelCm.store(centimetres, std::memory_order_release);
    Log("result=0 detail=text_panel_distance cm=" + std::to_string(centimetres));
    return 0;
}

DWORD TextPanelDistanceCm() { return gTextPanelCm.load(std::memory_order_acquire); }

DWORD SetUiDistanceCm(unsigned int centimetres)
{
    if (centimetres < 100 || centimetres > 400) {
        Log("result=refused detail=ui_distance_out_of_range");
        return 1;
    }
    gUiDistanceCm.store(centimetres, std::memory_order_release);
    // As ui.scale: the frame thread refits an open menu at the new distance.
    gPanelSettingsGeneration.fetch_add(1, std::memory_order_release);
    Log("result=0 detail=ui_distance cm=" + std::to_string(centimetres));
    return 0;
}

DWORD UiDistanceCm() { return gUiDistanceCm.load(std::memory_order_acquire); }

DWORD SetUiOffsetDegrees(int right, int up)
{
    if (right < -90 || right > 90 || up < -45 || up > 45) {
        Log("result=refused detail=ui_offset_out_of_range");
        return 1;
    }
    gUiOffsetRight.store(right, std::memory_order_release);
    gUiOffsetUp.store(up, std::memory_order_release);
    gPanelSettingsGeneration.fetch_add(1, std::memory_order_release);
    Log("result=0 detail=ui_offset right_degrees=" + std::to_string(right) + " up_degrees=" + std::to_string(up));
    return 0;
}

int UiOffsetRightDegrees() { return gUiOffsetRight.load(std::memory_order_acquire); }

int UiOffsetUpDegrees() { return gUiOffsetUp.load(std::memory_order_acquire); }

// **Why this exists.** `ui.scale` stopped having any effect above about 130 at
// 2688x2880, which reads as a hard maximum but is not one: the fit loops were
// shrinking the panel back until it sat inside 72 percent of the frustum the
// runtime reports. This exposes that 72.
//
// Raising it is a real trade, not a free win. The reported frustum is already
// wider than a Quest 3's lenses actually show, so at 100 percent the corners
// are beyond what the wearer can see -- which is exactly why the inset was
// there. It is reversible, and a wearer is the only instrument that can settle
// where their own edge is.
DWORD SetUiFitMarginPercent(unsigned int percent)
{
    if (percent < 30 || percent > 100) {
        Log("result=refused detail=ui_margin_out_of_range");
        return 1;
    }
    gUiFitMarginPercent.store(percent, std::memory_order_release);
    gPanelSettingsGeneration.fetch_add(1, std::memory_order_release);
    Log("result=0 detail=ui_margin percent=" + std::to_string(percent));
    return 0;
}

DWORD UiFitMarginPercent() { return gUiFitMarginPercent.load(std::memory_order_acquire); }

// The onboarding card under the menu. Ours, not Prey's, and it is charged to
// the same vertical budget as the menu -- so switching it off is also the
// cheapest way to make the menu bigger.
DWORD SetUiGuideEnabled(unsigned int enabled)
{
    const bool on = enabled != 0;
    gUiGuide.store(on, std::memory_order_release);
    gPanelSettingsGeneration.fetch_add(1, std::memory_order_release);
    Log(std::string("result=0 detail=ui_guide enabled=") + (on ? "1" : "0"));
    return 0;
}

DWORD UiGuideEnabled() { return gUiGuide.load(std::memory_order_acquire) ? 1u : 0u; }

} // namespace preyvr::dll
