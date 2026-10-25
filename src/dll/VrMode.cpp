#include "VrMode.h"
#include "TorsoLane.h"
#include "VrOptionsRuntime.h"
#include "Logger.h"
#include "XrSessionHost.h"
#include "XrInput.h"
#include "MainRenderHook.h"
#include "StereoEyeCamera.h"
#include "FrameObserverHook.h"
#include "ConsoleBridgeWin32.h"
#include "HeadTrackingHook.h"
#include "NearViewStereo.h"
#include "NearParticleStereo.h"
#include "MarkerStereo.h"
#include "StarfieldStereo.h"
#include "BodyDepthHook.h"
#include "DualSceneStereo.h"
#include "DualOverlay.h"
#include "NearFovOverride.h"
#include "InputPost.h"
#include "InputPath.h"
#include "MoveLane.h"
#include "StickReread.h"
#include "AimTakeover.h"
#include "ReticleFollow.h"
#include "SceneQuery.h"
#include "HudLayer.h"
#include "UiPointer.h"
#include "AnimIkTakeover.h"
#include "InteractionLane.h"
#include "CarryLane.h"
#include "GrenadeLane.h"
#include "DebugOverlay.h"
#include "EnvSwitch.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/VrOptions.h"
#include "FocusHold.h"
#include "ControllerPrompts.h"
#include "InventoryPointerCapture.h"
#include "LoadingScreen.h"
#include "preyvr/DebugOverlayScene.h"
#include "preyvr/LatestSnapshot.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace preyvr::dll {
namespace {
enum class Phase { off, preparing, settings, tracking, rendering, active, failed };
Phase phase=Phase::off;
std::string failure;
unsigned setting=0;
unsigned long long deadline=0, consoleCount=0, calibratedOwner=0, calibratedReference=0;
// The both-eyes and HUD-panel counters, logged once a minute while VR is active
// so a headset session records them without a tuner command.
unsigned long long nextStatusReport=0;
constexpr unsigned long long kStatusReportMs=60000;
bool consolePending=false;
unsigned desiredWidth=0,desiredHeight=0;
unsigned long long firstFrame=0;
bool nearFollow=true;
unsigned followedNearFov=0;
unsigned long long nextInputProbe=0;
const char* settings[]={"r_MotionBlur 0","r_AntialiasingMode 1"};
void Log(const std::string& s) { lifecycle::Log("preyvr_vr " + s); }
void StopFeatures() {
    SetHudLayerEnabled(0);
    // Contract first, so no both-eyes record is published after its submissions stop.
    SetDualStereoContract(0); SetDualSceneMode(0,0); SetDualOverlayRedraw(0);
    SetSyntheticStereo(0,50);
    SetViewHookApplying(0); SetViewPositionApplying(0);
    SetNearViewStereo(0); SetNearFovDeciDegrees(0);
    SetReticleFollowEnabled(0); SetAimTakeoverEnabled(0);
    SetAnimIkControllerDrive(0); SetAnimIkMode(0);
    SetMenuNavigation(0); SetMoveLaneMode(0);
    SetTurnLaneEnabled(0); SetFireLaneEnabled(0); SetInteractionEnabled(0);
    SetTorsoLaneActive(0);
    // Prey's keyboard/mouse prompts again, if VR start had set the controller's.
    SetControllerPrompts(0);
    // Leave the input drain alive to deliver the owned releases on the next frame.
    StopXrSession();
}
void Fail(const char* step) {
    failure=step; phase=Phase::failed; StopFeatures();
    Log("state=failed step="+failure+" retry=F11");
}
}
void EnableVrMode() {
    if(phase!=Phase::off && phase!=Phase::failed) return;
    failure.clear(); calibratedOwner=0;
    if(SetFrameObserverEnabled(true)!=2 || EnsureRenderHookInstalled()!=0) {
        Fail("render_hooks"); return;
    }
    SetNativeProjection(1); // returns CameraEditStatus, not a zero-success result.
    if(!NativeProjectionEnabled() || SetViewHookObserving(1)!=0) {
        Fail("view_hook"); return;
    }
    wchar_t value[24]{};
    desiredWidth=GetEnvironmentVariableW(L"PREYVR_RENDER_WIDTH",value,24)?static_cast<unsigned>(_wtoi(value)):0;
    desiredHeight=GetEnvironmentVariableW(L"PREYVR_RENDER_HEIGHT",value,24)?static_cast<unsigned>(_wtoi(value)):0;
    // Panel curvature and the laser hand are VR Options settings: vr-options.ini
    // holds them (ApplySavedVrOptions), not the launcher's environment. The menu
    // panel's own settings apply now, so its first frame is already the saved one.
    LoadVrOptions();
    ApplySavedPanelOptions();
    phase=Phase::preparing;deadline=GetTickCount64()+120000;nextInputProbe=0;
    Log("state=waiting_for_renderer controls=F11_enable,F12_recenter");
}
void DisableVrMode() {
    StopFeatures(); phase=Phase::off; Log("state=off");
}
void TickVrMode() {
    // Poll the current process's foreground window only. Refocus with a key held
    // must not trigger activation or recenter; an actual new press is required.
    static bool focusWas=false, f10Was=false, f11Was=false, f12Was=false;
    DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    const bool focus=pid==GetCurrentProcessId();
    const bool f11=(GetAsyncKeyState(VK_F11)&0x8000)!=0;
    const bool f12=(GetAsyncKeyState(VK_F12)&0x8000)!=0;
    const bool f10=(GetAsyncKeyState(VK_F10)&0x8000)!=0;
    if(focus && focusWas) {
        if(f10 && !f10Was)RequestVrOptions(!VrOptionsOpen());
        if(f11 && !f11Was) EnableVrMode();
        if(f12 && !f12Was) RecenterHeadTracking();
    }
    focusWas=focus; f11Was=f11; f12Was=f12;
    f10Was=f10;
    if(XrSessionLossPending()) {Fail("openxr_session_ended");return;}
    if(phase==Phase::preparing) {
        if(GetTickCount64()>deadline) { Fail("render_size_timeout"); return; }
        if(!XrBackbufferReady(desiredWidth,desiredHeight))return;
        // A native window and renderer can exist before gEnv.pInput. In the
        // cold-start control this was still null at injection, then resolved on
        // retry. Wait for that explicit not-ready result; reject a bad contract.
        if(GetTickCount64()<nextInputProbe)return;
        nextInputProbe=GetTickCount64()+250;
        const auto inputPath=ResolveInputPath();
        if(inputPath==2)return;
        if(inputPath!=0 || SetInputPostEnabled(1)!=0){Fail("input_contract");return;}
        // The laser's inventory hook, here rather than on its first menu event.
        // That event runs on Prey's main thread, and installing from there while
        // the tracking step below installs its hooks back to back starved the
        // main thread on MinHook's lock (a spin that polls with Sleep(1), held
        // through each install's thread freeze) until the last of them: Prey
        // drew nothing for 1.7-2.0 s and the menu froze (5 of 7 starts on
        // 2026-10-17; the other two installed before the burst began). Not
        // fatal: a failure here leaves the first event to install it, as before.
        if(const auto capture=EnsureInventoryPointerCapture())
            Log("inventory_capture=deferred result="+std::to_string(capture));
        // The loading screen's present (LoadingScreen.h), here for the same
        // reason. Not fatal: without it loads show the frame from before them.
        if(const auto loading=EnsureLoadingScreenHook())
            Log("loading_screen=unavailable result="+std::to_string(loading));
        firstFrame=XrSubmittedFrameCount();
        if(XrSessionStatusValue()!=1) {
            SetXrPreferSrgbFormat(1);
            if(StartXrSession()!=1) { Fail("openxr_start"); return; }
        }
        if(!XrInputCreated()) { Fail("openxr_actions"); return; }
        SetUiPanelMode(1);SetMenuNavigationGate(1);SetMenuNavigation(1);
        phase=Phase::settings;setting=0;consolePending=false;deadline=GetTickCount64()+15000;
        Log("state=starting renderer=settled");
    }
    if(phase==Phase::settings) {
        if(GetTickCount64()>deadline) { Fail("renderer_settings_timeout"); return; }
        if(consolePending) {
            if(VerifiedRendererSettingCount()==consoleCount) return;
            if(VerifiedRendererSettingResult()!=0) { Fail("renderer_settings"); return; }
            consolePending=false; ++setting;
        }
        if(setting<std::size(settings)) {
            consoleCount=VerifiedRendererSettingCount();
            const auto result=QueueVerifiedRendererSetting(settings[setting]);
            if(result==4) return;
            if(result!=0) { Fail("queue_renderer_settings"); return; }
            consolePending=true; return;
        }
        phase=Phase::tracking; deadline=GetTickCount64()+120000;
        Log("state=waiting_for_headset put_on_headset=1");
    }
    if(phase==Phase::tracking) {
        if(GetTickCount64()>deadline) { Fail("headset_tracking_timeout"); return; }
        TrackingFrame frame{};
        if(!TryGetTrackingFrame(frame) || !FreshSample(MonotonicNanoseconds(),frame.publishedNs) ||
            !IsPoseUsable(frame.head,frame.headValidity,kMaxPoseAgeNs)) return;
        const float ipd=XrRuntimeIpdMetres();
        if(!std::isfinite(ipd) || ipd<.045f || ipd>.085f) { Fail("runtime_eye_separation"); return; }
        const auto width=XrResolutionChain(XrResolution::backBufferWidth),height=XrResolutionChain(XrResolution::backBufferHeight);
        if(!width || !height) { Fail("render_dimensions"); return; }
        const auto nearFov=static_cast<unsigned>(std::lround(
            2*std::atan(std::tan(1.0471975512f)*static_cast<float>(height)/width)*572.9577951f));
        if(RecenterHeadTracking()!=0) return;
        if(SetViewHookApplying(1)!=0 || SetViewPositionApplying(1)!=0 ||
            SetSyntheticStereo(ipd,50)!=2 || SetXrStereoSubmission(1)!=1 ||
            SetNearViewStereo(1)!=0 || SetNearViewIpdMetres(ipd)!=0 || SetNearFovDeciDegrees(nearFov)!=0) {
            Fail("stereo_and_tracking"); return;
        }
        // Both eyes in every frame (rung 1b, DualSceneStereo.h) unless
        // PREYVR_DUAL_STEREO=0 asks for the alternate-eye path. The render-thread
        // mode is armed before the main thread starts publishing both-eyes
        // contracts, so no such contract can arrive without a submission to fill
        // it. Not fatal: without the hooks the alternate-eye path continues.
        {
            if(EnvSwitchOff(L"PREYVR_DUAL_STEREO")) Log("dual_stereo=off reason=PREYVR_DUAL_STEREO");
            else if(SetDualSceneMode(3,0)==0) {
                SetDualStereoContract(1);Log("dual_stereo=on");
                // What the game draws after the world, into the held eye too
                // (DualOverlay.h). PREYVR_DUAL_OVERLAY=0 leaves it in one eye.
                if(EnvSwitchOff(L"PREYVR_DUAL_OVERLAY")) Log("dual_overlay=off reason=PREYVR_DUAL_OVERLAY");
                else if(SetDualOverlayRedraw(1)==0) Log("dual_overlay=on");
                else Log("dual_overlay=unavailable overlays=second_eye_only");
            }
            else Log("dual_stereo=unavailable fallback=alternate_eye");
        }
        // DrawNear particles in the weapon's camera space (preyvr/NearParticles.h).
        // Not fatal: without the hook they keep the native double parallax.
        EnsureNearParticleStereo();
        // World markers at the depth they mark (MarkerStereo.h). Not fatal either.
        EnsureMarkerDepth();
        // The stars at the sky's depth, not 1 m from the head (StarfieldStereo.h).
        // Not fatal: without the hook Prey draws them as before.
        EnsureStarfieldDepth();
        // The player's body in the world, hidden by what is in front of it, not
        // drawn over everything (BodyDepthHook.h). Not fatal either.
        EnsureBodyDepth();
        // Prey's re-read of the sticks finds the lanes' stick (StickReread.h).
        // Not fatal: the lanes' read-back still restores it within a frame.
        if(InstallStickReread()!=0) Log("stick_reread=unavailable fallback=read_back");
        if(SetMoveLaneMode(2)!=0 || SetTurnLaneEnabled(1)!=0 || SetFireLaneEnabled(1)!=0 ||
            SetInteractionEnabled(1)!=0 || SetAimTakeoverEnabled(1)!=0 ||
            SetReticleFollowEnabled(1)!=0 || SetReticleDispatchEnabled(1)!=0 ||
            SetAnimIkMode(2)!=0 || SetAnimIkControllerDrive(1)!=0 ||
            SetAnimIkWeaponAlignment(1)!=0 || SetAnimIkHands(3)!=0 || SetAnimIkReachPercent(65)!=0) {
            Fail("motion_controls"); return;
        }
        // World interaction from the LEFT hand: it points, its grip uses; the
        // right grip only reloads (InteractionLane.h). Not fatal: without the
        // selector hook the right grip stays the native use/reload button.
        if(SetUseHand(1)!=0) Log("use_hand=right reason=selector_unavailable");
        // Phase 2: what the left hand carries is IN it, held while the grip is,
        // thrown with the hand's motion (CarryLane.h). Not fatal: without the
        // hooks the game's own carry stays, toggled like in phase 1.
        else if(SetCarryHand(options::Default(options::CarryHand))!=0) Log("carry_hand=native reason=hooks_unavailable");
        // Grenades leave the right hand at the trigger's release with its
        // motion (GrenadeLane.h). Not fatal: without the hooks the game's own
        // charged throw along the view stays.
        if(SetGrenadeHand(options::Default(options::GrenadeHand))!=0) Log("grenade_hand=native reason=hooks_unavailable");
        // The shared torso the shoulders and body slots sit on (TorsoLane.h).
        // Not fatal: without the hook each keeps its own estimate, and a snap
        // turn lands at the view instead.
        if(SetTorsoLaneActive(1)!=0) Log("torso=unavailable shoulders=own_estimate");
        // The arms stay at the controllers with no weapon drawn: holstered (the
        // right grip held) or carrying (AnimIkTakeover arms.free; arms.free 0 =
        // the game lowers them out of view, as before).
        SetAnimIkFreeArms(options::Default(options::ArmsFree));
        // The aim lasers by default (each hand's ray to what it points at); VR
        // Options' Aim lasers turns them off, and its saved choice applies below.
        // The debug overlay is not switched on here: it follows its own row, or
        // dbg.draw for the session, and leaves the lasers as they are.
        SetAimLasers(options::Default(options::AimLasers));
        // The floating HUD by default; VR Options' HUD style can turn it off.
        if(SetHudLayerEnabled(options::Default(options::HudLayer))!=0)Log("hud_layer=unavailable native_hud=retained");
        // The menus keep the controller's prompts under the laser
        // (ControllerPrompts.h); VR Options' Force Controller Prompts can turn it off.
        SetControllerPrompts(options::Default(options::ControllerPrompts));
        // The carry and grenade hands, the free arms, the lasers, the HUD layer
        // and the prompts start off; above, each takes its VR Options default.
        // Saved VR Options choices last, so they win over the defaults above
        // (an F11 restart included).
        ApplySavedVrOptions();
        phase=Phase::rendering;deadline=GetTickCount64()+15000;
    }
    if(phase==Phase::rendering) {
        if(GetTickCount64()>deadline) { Fail("no_rendered_frames"); return; }
        if(XrSubmittedFrameCount()<firstFrame+3 || XrResolutionChain(XrResolution::sizeMismatch)!=0)return;
        phase=Phase::active;
        nextStatusReport=GetTickCount64()+kStatusReportMs;
        Log("state=active panel=automatic recenter=F12_or_menu_and_both_grips");
    }
    if(phase==Phase::active) {
        if(XrSessionStatusValue()!=1) {Fail("openxr_session_ended");return;}
        if(GetTickCount64()>=nextStatusReport) {
            nextStatusReport=GetTickCount64()+kStatusReportMs;
            // The same text dual.report and hud.layer answer with.
            Log("status dual={"+DualSceneReport()+DualOverlayReport()+" } hud={"+HudLayerReport()+
                " } torso={"+TorsoLaneReport()+" } move={"+MoveLaneStatusReport()+StickRereadReport()+
                " } reticle={"+ReticleWorldReport()+SceneQueryReport()+" } "+MarkerDepthReport()+" "+StarfieldDepthReport()+" "+BodyDepthReport()+" focus={"+
                FocusHoldReport()+" } hold={"+XrHoldReport()+" } prompts={"+ControllerPromptsReport()+" }");
        }
        const auto owner=AnimIkOwnerGeneration();
        const auto reference=HeadTrackingReferenceGeneration();
        if(AnimIkOwnerCharacter()!=0 && !stereo::Recentering(reference) &&
           (owner!=calibratedOwner || reference!=calibratedReference) && CalibrateAnimIk()==0) {
            calibratedOwner=owner;
            calibratedReference=reference;
        }
        FollowNearFov();
    }
}
// **The near pass must project exactly like the world.** The arms and the held
// weapon are drawn by the near (viewmodel) pass at r_DrawNearFoV, the world at
// the camera's own frustum, and the image is declared to the runtime with the
// WORLD's tangents. Since the IK places the hands in world space, any other near
// FOV draws them at the wrong angle from the eye: the activation value assumed
// a 120-degree horizontal FOV slider, and with the slider at 98 (cl_hfov 98,
// measured 2026-10-06) the hands were drawn at 0.66 of their angle, pulled
// towards the centre -- short arms, a weapon above the real hand. The near
// FOV now follows the vertical FOV the world camera actually rendered.
void FollowNearFov() {
    if(!nearFollow)return;
    float left=0,right=0,up=0,down=0;
    if(!RenderedEyeTangents(left,right,up,down))return;
    const float vertical=std::max(std::fabs(up),std::fabs(down));
    if(!std::isfinite(vertical) || vertical<.2f || vertical>10.f)return;
    const auto deci=static_cast<unsigned>(std::lround(2*std::atan(vertical)*572.9577951f));
    // Against the value in force, not the last one followed: activation and a
    // VR restart set their own, which must be replaced again.
    if(deci!=NearFovDeciDegrees() && SetNearFovDeciDegrees(deci)==0) {
        if(deci!=followedNearFov)Log("near_fov=follow deci_degrees="+std::to_string(deci));
        followedNearFov=deci;
    }
}
DWORD SetNearFovFollow(unsigned enabled) {
    if(enabled>1)return 1;
    nearFollow=enabled!=0; followedNearFov=0;
    Log(std::string("near_fov_follow=")+(nearFollow?"1":"0"));
    return 0;
}
bool NearFovFollowing() { return nearFollow; }
unsigned NearFovFollowed() { return nearFollow?followedNearFov:0; }
std::string VrModeReport() {
    const char* names[]={"off","preparing","settings","tracking","rendering","active","failed"};
    std::ostringstream s;
    s<<"vr="<<names[static_cast<int>(phase)]<<" vrFailure="<<(failure.empty()?"none":failure)
     <<" uiPanel="<<UiPanelMode()<<" uiPanelFrames="<<UiPanelFrameCount();
    return s.str();
}
}
