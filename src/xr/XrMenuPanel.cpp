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

// The game menu's panel, placed from the menu anchor; the laser's texture and
// quads; and the panel's settings (XrSessionState.h).

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
void FillPointerLayers(const UiPointerVisual& pointer, const ui::Panel& surface, const Vec3& head,
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

// Whether the game menu's panel must be fitted again: none yet, or a recenter
// has completed since it was placed.
bool MenuPanelStale(unsigned long long reference)
{
    return !gHost.menuPanel || (!stereo::Recentering(reference) && reference!=gHost.panelReference);
}

// **The game menu's panel**, fitted from the shared menu anchor (Host::menuAnchor).
// Prey's frames, and hold frames showing the loading screen; under gMutex.
void PlaceMenuPanel(const Pose& head, const XrView (&views)[2], unsigned long long reference)
{
    const auto& anchor=MenuAnchorFor(head,views,reference);
    const float aspect=static_cast<float>(gHost.width)/static_cast<float>(gHost.height);
    // **Upright, at eye level, facing the wearer square-on**, ui.distance
    // ahead of the way the face was turned when the anchor was taken (on
    // opening, not now: a setting change re-places the menu from the same
    // anchor). Fitted from the head itself the menu followed its pitch and
    // roll: opened while looking down, it came out low, tilted and rolled.
    // The anchor's eyes are levelled so the fit sizes it for a level gaze,
    // which also keeps a menu opened while looking down from failing the
    // corner test (UiPanel.h).
    const float distance=static_cast<float>(gUiDistanceCm.load(std::memory_order_relaxed))*.01f;
    const float scale=static_cast<float>(gUiScalePercent.load(std::memory_order_relaxed))*.01f;
    // ui.offset carries it around the head, after the fit (ui::PlaceMenu): the
    // fit sizes for straight ahead, and an offset view would shrink or refuse a
    // panel the wearer chose to put aside.
    const int offsetRight=gUiOffsetRight.load(std::memory_order_relaxed);
    const int offsetUp=gUiOffsetUp.load(std::memory_order_relaxed);
    if(const auto panel=ui::PlaceMenu(anchor,aspect,distance,scale,ui::kDefaultFitMargin,
                                      static_cast<float>(offsetRight)*kRadiansPerDegree,
                                      static_cast<float>(offsetUp)*kRadiansPerDegree))
        gHost.menuPanel=panel;
    gHost.panelReference=reference;
    ++gHost.surfaceSerial;
    Log("ui_surface distance_m="+std::to_string(distance)+
        " offset_degrees="+std::to_string(offsetRight)+","+std::to_string(offsetUp)+
        " placed="+(gHost.menuPanel?"1":"0"));
}

// The game menu's panel as a quad of the whole of `swapchain`'s first slice.
void FillMenuLayer(XrCompositionLayerQuad& quad, const ui::Panel& panel, XrSwapchain swapchain,
                   XrCompositionLayerFlags flags)
{
    FillQuad(quad,gHost.space,panel.pose,{panel.width,panel.height},swapchain,
             {{0,0},{static_cast<int>(gHost.width),static_cast<int>(gHost.height)}},flags);
    quad.subImage.imageArrayIndex=0;
}

} // namespace xrsession

DWORD SetUiPanelMode(unsigned int mode) {
    if(mode>2) return 1;
    gUiPanelMode.store(mode);if(mode==0)ClearUiPointer();return 0;
}

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

} // namespace preyvr::dll
