#pragma once

// XrSessionHost's private state, shared by its seven files:
//  - XrSessionHost.cpp: ServiceXrFrame, Prey's frame (the orchestrator);
//  - XrSessionLifecycle.cpp: the device, instance, session, space and main
//    swapchain, events, start, stop and teardown;
//  - XrEyePair.cpp: the held eye images and their pair, the declared FOV, the
//    both-eyes first-eye hold and its overlay window;
//  - XrMenuPanel.cpp: the game menu's panel, the menu anchor, the laser, and
//    the panel settings;
//  - XrOverlayLayers.cpp: the HUD, reticle, wheel, note, text, wrist, slot and
//    fade layers;
//  - XrHoldFrames.cpp: hold frames and the loading screen;
//  - XrSessionReports.cpp: timing, coverage and the resolution chain.
// The state is as it was in XrSessionHost.cpp, one instance of each (`inline`).
// Include it from those seven files only.
#include "xr/XrSessionHost.h"
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
#include "base/NeverDestroyed.h"
#include "engine/RendererAccess.h"
#include "xr/XrPose.h"
#include "ui/screens/LoadingScreen.h"
#include "ui/options/VrOptionsLayer.h"
#include "ui/options/VrOptionsRuntime.h"
#include "equipment/body/BodyEquipmentLane.h"
#include "body/RoomScaleLane.h"
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

namespace preyvr::dll::xrsession {

using Microsoft::WRL::ComPtr;

// A **timed** mutex, and nothing ever blocks on it indefinitely.
//
// Diagnosed the hard way on 2026-08-31: a plain std::mutex here deadlocked. An
// external tool calls a control export while the render thread services frames,
// and a blocking lock between those two turns any mishap into a hang -- the
// export never returns, the caller reports a vague error, and the real cause is
// invisible. A hang is strictly worse than a failure, because a failure says
// what happened.
//
// So the control path waits briefly and then *refuses*, and the render path uses
// try_lock and simply skips the frame. Neither can ever wait on the other.
inline std::timed_mutex gMutex;

constexpr auto kControlLockTimeout = std::chrono::milliseconds(250);

inline std::atomic<DWORD> gStatus{static_cast<DWORD>(XrSessionStatus::idle)};

inline std::atomic<bool> gStopRequested{false};

inline std::atomic<bool> gStopAcknowledged{false};

inline std::atomic<unsigned long long> gSubmitted{0};

// Frames the head-in-wall fade was drawn (RoomScaleLane.h BodyHeadOut).
inline std::atomic<unsigned long long> gFadeFrames{0};

inline std::atomic<unsigned int> gUiPanelMode{0};

inline std::atomic<unsigned long long> gUiPanelFrames{0};

inline std::atomic<float> gRuntimeIpd{0};

inline std::atomic<XrReferenceSpaceType> gTrackingSpaceType{XR_REFERENCE_SPACE_TYPE_MAX_ENUM};

struct BackbufferSample { unsigned width=0,height=0; std::uint64_t changed=0,stamp=0; unsigned frames=0; };

inline LatestSnapshot<BackbufferSample> gBackbufferSample;

// **Hold frames** (docs/LOADING-LASER-2026-10-17.md). Every XR frame, the laser
// included, comes from ServiceXrFrame, so while Prey loads -- rendering no
// frames -- the compositor reshows the last one and the laser stays where it
// was. A thread of ours submits frames while Prey's are late: the last frame's
// layers again, with the laser where the hand is now. That means calling
// xrEndFrame off Prey's render thread, and SteamVR's xrEndFrame flushes Prey's
// D3D11 immediate context, which Prey's render thread may be using: so the
// device runs with D3D11's multithread protection while hold frames are on.
// xr.hold switches both; the render thread applies the change.
inline std::atomic<bool> gHoldWanted{true};

inline std::atomic<bool> gHoldActive{false};      // protection applied: hold frames allowed

inline std::atomic<unsigned> gHoldThresholdMs{100};

inline std::atomic<std::uint64_t> gLastServiceNs{0};

inline std::atomic<bool> gHoldInFrame{false};     // the hold thread holds gMutex for a frame

inline std::atomic<unsigned long long> gHoldFrames{0}, gHoldStalls{0}, gHoldRenderWaits{0}, gHoldRenderWaitMisses{0};

inline std::atomic<std::uint64_t> gHoldLongestStallNs{0}, gHoldLastStallNs{0};

inline std::atomic<const char*> gHoldRefusal{"none"};

// Outside Host, which a session's teardown resets: the protection outlives it.
inline std::atomic<int> gProtectionBefore{-1};    // -1 not read yet

inline bool gProtectionApplied=false;             // render thread, under gMutex

inline std::once_flag gHoldThreadStarted;

// The one wait the render thread makes: for a hold frame in flight, which ends
// within a display period.
constexpr auto kHoldYieldTimeout=std::chrono::milliseconds(40);

constexpr DWORD kHoldPollMs=5;

constexpr std::uint64_t kHoldLogStallNs=250000000ull;

// **The loading screen** (docs/LOADING-SCREEN-2026-10-20.md). Prey draws it in a
// loop of its own on the render thread, presented without RT_EndFrame, so no XR
// frame is made from it. That loop's present is detoured (LoadingScreen.h): the
// render thread copies the backbuffer into Host::loadingImage there, never
// waiting, and hold frames upload the copy into the menu panel's image and show
// it. gLoadingCaptured counts this load's copies, 0 when none has been made since
// Prey's last ordinary frame; only the render thread and teardown write it.
inline std::atomic<unsigned long long> gLoadingCaptured{0};

inline std::atomic<unsigned long long> gLoadingCaptures{0}, gLoadingUploads{0}, gLoadingFrames{0}, gLoadingLoads{0};

// **One frame's layers by value**, so a hold frame can submit them again.
// Ordered() points the projection at this set's own views, so always take the
// list from the copy being submitted.
struct FrameLayerSet {
    ui::FrameShown shown{};
    XrCompositionLayerProjectionView views[2]{};
    XrCompositionLayerProjection world{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerQuad menu{XR_TYPE_COMPOSITION_LAYER_QUAD},
        hud{XR_TYPE_COMPOSITION_LAYER_QUAD}, reticle{XR_TYPE_COMPOSITION_LAYER_QUAD},
        wheel{XR_TYPE_COMPOSITION_LAYER_QUAD}, note{XR_TYPE_COMPOSITION_LAYER_QUAD}, text{XR_TYPE_COMPOSITION_LAYER_QUAD},
        options{XR_TYPE_COMPOSITION_LAYER_QUAD}, beam{XR_TYPE_COMPOSITION_LAYER_QUAD}, cursor{XR_TYPE_COMPOSITION_LAYER_QUAD},
        wrist{XR_TYPE_COMPOSITION_LAYER_QUAD}, slot{XR_TYPE_COMPOSITION_LAYER_QUAD}, fade{XR_TYPE_COMPOSITION_LAYER_QUAD};

    std::vector<const XrCompositionLayerBaseHeader*> Ordered() {
        world.views=views;
        const auto order=ui::ComposeFrame(shown);
        std::vector<const XrCompositionLayerBaseHeader*> layers;
        for(unsigned n=0;n<order.count;++n)layers.push_back(Header(order.layers[n]));
        return layers;
    }

private:
    const XrCompositionLayerBaseHeader* Header(ui::FrameLayer which) const {
        const auto as=[](const auto& layer){return reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);};
        switch(which) {
        case ui::FrameLayer::world:return as(world);
        case ui::FrameLayer::menu:return as(menu);
        case ui::FrameLayer::hud:return as(hud);
        case ui::FrameLayer::reticle:return as(reticle);
        case ui::FrameLayer::wheel:return as(wheel);
        case ui::FrameLayer::note:return as(note);
        case ui::FrameLayer::text:return as(text);
        case ui::FrameLayer::options:return as(options);
        case ui::FrameLayer::beam:return as(beam);
        case ui::FrameLayer::cursor:return as(cursor);
        case ui::FrameLayer::wrist:return as(wrist);
        case ui::FrameLayer::slot:return as(slot);
        case ui::FrameLayer::fade:return as(fade);
        }
        return nullptr;
    }
};

// Where the laser points: the surface it is tested against and its cursor drawn
// on. VR Options' while it is up, else the game menu's. Kept with the held frame
// so a hold frame points where Prey's frame did.
struct HeldPointer { bool active=false; ui::Panel surface{}; };

struct Host {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrSpace localFallback=XR_NULL_HANDLE;
    bool spaceHasValidViews=false;
    XrVersion apiVersion=XR_API_VERSION_1_0;
    bool localFloorSupported=false;
    bool recalibrateSpace=false;
    std::vector<XrEventDataReferenceSpaceChangePending> spaceChanges;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN;
    XrSwapchain pointerSwapchain = XR_NULL_HANDLE;
    bool pointerReady=false,pointerAttempted=false;
    std::unique_ptr<LayerSwapchain> hudSwapchain;
    bool hudSwapchainLogged=false;
    // The text panel: subtitle and tutorial movies, lifted out of the scene
    // like the HUD and shown on a panel nearer than it, following the head.
    std::unique_ptr<LayerSwapchain> textSwapchain;
    bool textSwapchainLogged=false;
    // The note panel: notes and books, captured like the text panel but held
    // where they opened (ui::PopupHold) rather than following the head.
    std::unique_ptr<LayerSwapchain> noteSwapchain;
    bool noteSwapchainLogged=false;
    ui::PopupHold noteHold;
    // The radial wheel: lifted out of the HUD (HudLayer RequestHudPart) and held
    // where it opened, as the note panel is.
    std::unique_ptr<LayerSwapchain> wheelSwapchain;
    bool wheelSwapchainLogged=false;
    ui::PopupHold wheelHold;
    // The world reticle: the crosshair lifted out of the HUD, at the aim's hit.
    std::unique_ptr<LayerSwapchain> reticleSwapchain;
    bool reticleSwapchainLogged=false;
    // A render target view on the held first eye, matching the view format
    // the game draws its overlays with, and the backbuffer it held from.
    ID3D11RenderTargetView* heldTargetView=nullptr;
    ID3D11Texture2D* heldTargetImage=nullptr;
    DXGI_FORMAT heldTargetFormat=DXGI_FORMAT_UNKNOWN;
    std::uintptr_t heldBackBuffer=0;
    VrOptionsLayer optionsLayer; // the VR Options panel: placed, dragged and drawn (XR frame thread)
    std::unique_ptr<LayerSwapchain> wristSwapchain;
    std::unique_ptr<NativeWristTexture> wristFitter;
    bool wristVisible=false;
    std::uint64_t wristCapturedNs=0;
    // **The menu anchor (ui::MenuAnchor)**, shared by the game's menu panel and
    // VR Options, so both are placed from where the wearer faced. Taken when a
    // game menu opens (or VR Options opens without one), after a recenter, or
    // after the tracking space changes; kept through setting changes.
    std::optional<ui::MenuAnchor> menuAnchor;
    std::uint64_t menuAnchorReference=0;
    PaintedLayer slotImage{"slot_swapchain",SlotFeedbackWidth,SlotFeedbackHeight};
    bool slotReady=false;
    equipment::SlotNotice slotPainted=equipment::SlotNotice::None;
    // The head-in-wall fade (RoomScaleLane.h): a black quad in front of the eyes,
    // repainted only when its quantised alpha changes. 640x480: the first fade,
    // at 4x4, was refused at creation and never drew (the layers' size floor).
    static constexpr UINT kFadeWidth=640,kFadeHeight=480;
    PaintedLayer fadeImage{"fade_swapchain",kFadeWidth,kFadeHeight};
    int fadePainted=-1;
    ui::Fade fade{};
    std::vector<ID3D11Texture2D*> images;

    ID3D11Device* device = nullptr;   // Prey's, borrowed -- never released here
    // The resolution chain, measured rather than assumed. The submitted size is
    // Prey's backbuffer by construction (see CreateSessionAndSwapchain), so the
    // runtime's own recommendation was never consulted and the gap between them
    // was never a number. RE-HEADSET-RESOLUTION-2026-09-08 asks for exactly this
    // before any resolution work: measure the chain first.
    std::uint32_t recommendedWidth = 0, recommendedHeight = 0;
    std::uint32_t maxWidth = 0, maxHeight = 0;
    std::uint32_t recommendedSamples = 0, maxSamples = 0;
    std::uint32_t viewCount = 0;
    bool viewsDiffer = false;          // per-eye sizes are allowed to differ
    std::uint32_t heldWidth = 0, heldHeight = 0, heldFormat = 0;
    std::uint32_t submittedWidth = 0, submittedHeight = 0;
    // Set when Prey's backbuffer no longer matches the size this session was
    // built at, which makes every copy invalid until a restart.
    bool sizeMismatch = false;
    bool loggedSizeMismatch = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool sessionBegun = false;
    bool started = false;
    std::optional<ui::Panel> menuPanel;
    unsigned long long panelReference = 0;
    unsigned long long panelSettingsGeneration = 0;
    bool panelWasActive = false;
    unsigned long long surfaceSerial=0;

    // Stereo submission: one held image per eye.
    //
    // Prey renders one eye per frame, so the backbuffer is never both. These two
    // textures hold the most recent image of each eye so that every submitted
    // frame can carry a complete pair -- one of them a few frames older than the
    // other. That staleness is the price of alternate-eye and it is invisible in
    // a frozen scene, which is how the first depth test is run.
    ID3D11Texture2D* eyeImage[2] = {nullptr, nullptr};
    bool eyeImageValid[2] = {false, false};
    HudCapturePair hudCapturePair;

    // **The contract that rendered each eye's pixels, held with them.**
    //
    // FAIL-STR-044, raised by the playbook session from Crysis 2 VR's runtime
    // records (author-grade, not independently replayed -- treated as a design
    // warning rather than a measurement). Carrying the eye identity is necessary
    // and not sufficient: the two held images are captured on *different* frames,
    // so labelling both with the pose and FOV read at submission time pairs older
    // pixels with a newer contract. "Latest left plus latest right is not a pair,
    // and a fresh ticket does not make stale pixels fresh."
    //
    // Invisible today, because the rendered image does not yet follow the head, so
    // a newer pose describes the same picture. It becomes shear and rubber-banding
    // the moment the view seam lands -- which is why this is fixed before that
    // rather than after.
    XrPosef eyePose[2]{};
    unsigned long long eyeReference = ~0ull;
    XrFovf eyeFov[2]{};
    // The head the held images were rendered from and their magnification
    // (render/camera/ViewZoom.h), for what is placed in the world to match them.
    Pose eyeHead{};
    float eyeZoom = 1.f;
    // What the runtime asked for, kept beside what we declared. The two are
    // different frusta and the gap between them is spent pixels: comparing them
    // is the only way to say how much of the render the headset can show.
    XrFovf requestedFov[2]{};
    bool requestedFovValid[2]{};
    XrTime eyeDisplayTime[2]{};
    xrframe::FrameContract contract;
    // The last frame submitted with layers, for hold frames to submit again.
    // Cleared with the session and when the tracking space changes.
    FrameLayerSet heldLayers;
    HeldPointer heldPointer;
    bool heldValid=false;
    // The loading screen (gLoadingCaptured): the render thread's copy of Prey's
    // backbuffer, created and released on that thread; which capture the menu
    // panel's image holds (0 none this load); this load's uploads and the hold
    // frames that showed it.
    ID3D11Texture2D* loadingImage=nullptr;
    bool loadingRefused=false;
    unsigned long long loadingUploaded=0, loadingUploads=0, loadingFrames=0;
};

// Never destroyed: its swapchains' destructors call OpenXR (NeverDestroyed.h).
inline Host& gHost = NeverDestroyed<Host>();

// In XrResolution order.
inline LatestSnapshot<std::array<DWORD,static_cast<std::size_t>(XrResolution::count)>> gResolution;

inline LatestSnapshot<std::array<XrFovf,2>> gOptics;

// Stereo submission, off by default.
//
// With this clear the path is the original flat mirror: the same backbuffer into
// both eyes, declaring the runtime's own FOV. That is a mono image and a false
// declaration, but it is a *known* one that proved the plumbing, and it stays
// reachable so a regression here can be bisected against it.
inline std::atomic<bool> gStereoSubmission{false};

// Sends each eye's image to the other eye's socket.
//
// **Insurance, not a feature.** Everything says the mapping is already right --
// eye 0 takes the negative offset in BuildSyntheticEye, and the red-left /
// blue-right check through the optics confirmed slice 0 is the left eye. But
// inverted stereo does not look broken, it looks subtly wrong: depth that will
// not settle, and discomfort that is easy to blame on judder or on the frustum.
// If that is what a session reports, this settles it in one call instead of
// costing a rebuild -- and a rebuild is not always available.
inline std::atomic<bool> gSwapEyes{false};

// Rung 1b. Set by HoldDualFirstEye when the first of this frame's two scene
// submissions has been copied into its held eye image; taken and cleared at the
// top of the next ServiceXrFrame, so a hold can only ever pair with the frame it
// was made in. Written and taken on the render thread; atomic so reports can
// read it from the command thread.
inline std::atomic<bool> gDualFirstHeld{false};

// Open from the end of the second submission of a held frame to the top
// of the next ServiceXrFrame: the stretch in which the game draws its overlays.
inline std::atomic<bool> gDualOverlayOpen{false};

inline std::atomic<unsigned long long> gDualHolds{0}, gDualHoldRefused{0};

inline std::atomic<unsigned long long> gDualPairsSubmitted{0}, gDualPairsRefused{0};

// Ask for the sRGB swapchain format instead of the exact match -- FAIL-STR-033.
// Read once when the swapchain is created, so it must be set before StartXrSession.
inline std::atomic<bool> gPreferSrgb{false};

inline void Log(const std::string& line)
{
    lifecycle::Log("preyvr_xr_session " + line);
}

// **The laser's texture**: the cursor's dot in the first 32x32; then, past a
// transparent gap that keeps filtering from bleeding one into the other, the
// fading beam's strip (ui::LaserFade), opaque at the bottom row -- the
// controller's end -- to clear at the top, the tip: a quad's top edge is its
// +Y, the beam's forward. A solid beam is the dot's centre texel.
constexpr int kPointerWidth=48,kPointerHeight=32;

constexpr XrRect2Di kCursorRect{{0,0},{32,32}},kSolidBeamRect{{15,15},{1,1}},kFadingBeamRect{{42,0},{4,32}};

// **Off by default, because a profiler that is always on is a profiler nobody
// trusts.** Recording is a store and an increment, but the honest way to answer
// "did measuring change the number" is to be able to turn it off.
inline std::atomic<bool> gTimingEnabled{false};

inline preyvr::timing::IntervalSeries gServiceInterval;

inline preyvr::timing::DurationSeries gWaitFrame, gAcquireWait, gEndFrame, gServiceTotal;

// **Eye skew: how far apart in time the two submitted eyes are.**
//
// Prey renders one eye per frame, so a submitted pair is always one older image
// and one newer one. `eyeDisplayTime[2]` was already being written at the
// publication unit and never read by anything -- this reads it. The skew is the
// difference between the two eyes' display times, which is the temporal
// disparity a wearer perceives as the pair not agreeing.
//
// Reuses DurationSeries so the distribution comes from tested code: a p50 says
// what the pair normally looks like, and the max says whether an eye ever
// starved for much longer than one frame.
inline preyvr::timing::DurationSeries gEyeSkew;

inline std::atomic<int> gStaleEye{-1};              // which eye currently holds the older image

inline std::atomic<unsigned long long> gEyeSkewSamples{0};

// **A wearer's dial on panel size, in percent.** FitPanel's 60/40 caps are a
// comfort default and the corner test uses the frustum the runtime REPORTS,
// which on a Quest 3 is wider than the lenses show. Where the real edge sits is
// not derivable -- only a wearer can find it -- so this is a knob rather than a
// constant. Applied to the menu panel and the HUD alike, because they are the
// same presentation and one dial is easier to reason about than two.
inline std::atomic<unsigned int> gUiScalePercent{100};

// **The text panel sits nearer than the HUD, at the HUD panel's physical size.**
// Its texture is the whole frame squeezed onto a panel about 38x40 degrees, so
// book text and subtitles came out small and heavily minified (no mips) -- soft,
// "out of focus". Nearer at the same size is larger in angle, so less minified.
// Centimetres, 100..200; 200 is the HUD panel's own fit.
inline std::atomic<unsigned int> gTextPanelCm{140};

// **The menu panel's distance, centimetres, 100..400.** It was a fixed 2 m. The
// fit's caps are angles, so the panel keeps its apparent size at any distance:
// this is where it sits, and ui.scale is how big it looks. 100..400 is the
// range FitPanel accepts. Saved by the VR Options menu (ui_distance).
inline std::atomic<unsigned int> gUiDistanceCm{200};

// **Where the menu sits around the wearer**, whole degrees from straight ahead:
// right -90..90, up -45..45, around a cylinder about the head (ui::AroundHead),
// so it slides up upright rather than tipping back. Up stops at 45: an upright
// panel 60 degrees up is seen nearly edge-on (half its height) and, at 2 m,
// 3.5 m overhead. Applied after the fit, so it moves the panel without resizing
// it. Two scalars, like every other setting; a pair is stored before the one
// generation bump, so a refit reads both. Saved by the VR Options menu.
inline std::atomic<int> gUiOffsetRight{0},gUiOffsetUp{0};

// Command threads only publish settings. Cached panels belong to the XR frame
// thread under gMutex; touching their optionals from a setter is a data race.
inline std::atomic<unsigned long long> gPanelSettingsGeneration{0};

// The runtime's OWN display period, from XrFrameState, reported beside the
// configured budget so nobody has to assume the two agree. `xr.timing 1 90`
// configures 11111 us; if the runtime is at 72 Hz that budget is simply wrong,
// and only this number says so.
inline std::atomic<std::uint32_t> gRuntimePeriodUs{0};

// Eye images published with the render contract that produced them, and
// contracts refused. The declaration is bound to its pixels at that one point
// (FAIL-STR-044), so a refused contract is a frame not submitted rather than one
// submitted with the wrong frustum.
inline std::atomic<unsigned long long> gEyeContractsPublished{0};

inline std::atomic<unsigned long long> gEyeContractsRefused{0};

// **Why the flat mirror looks small, and the one knob for it.**
//
// Without a held eye pair -- at the main menu, or any time Prey is not rendering
// alternating eyes -- the mirror submits Prey's OWN declared frustum, because
// that is the frustum the pixels came from. Declaring the runtime's field over a
// narrower render would claim an angular size the image does not have.
//
// Measured on this build at the main menu: Prey declares 51.8 degrees horizontal
// and symmetric, while the runtime's view is 98 degrees and asymmetric, so the
// compositor lays the image into the central 41% and the menu reads as a small
// window rather than a screen. That is the honest projection working, not a bug.
//
// For a flat 2D menu, though, geometric honesty buys nothing: there is no depth
// to get wrong, only text to read. Scaling the declared field makes the image
// span more of the view. It is DEFAULT OFF at 100, because it is a visual
// trade-off that only a headset can settle, and for 3D content it is a genuine
// distortion rather than a preference.
inline std::atomic<unsigned int> gMirrorFovPercent{100};

// **What every overlay layer of one of Prey's frames is placed from**
// (ServiceXrFrame): the located views, the head between them, the head-tracking
// reference and the display time; whether the frame is drawn and has views, and
// whether the game's menu panel or VR Options is up.
struct FrameView {
    const XrView (&views)[2];
    Pose head;
    std::uint64_t reference;
    XrTime displayTime;
    bool rendered, haveViews, panelActive, optionsActive;
};

// Named across the seven files.
// XrSessionLifecycle.cpp
void PublishResolution();
void Teardown();
void PumpEvents();
// XrEyePair.cpp
XrFovf ScaleDeclaredFov(const XrFovf& fov);
std::optional<XrFovf> DeclaredFovFromLiveCamera();
bool SubmitStereoPair(
    ID3D11DeviceContext* context,
    ID3D11Texture2D* backBuffer,
    std::uint32_t imageIndex,
    const XrView* views,
    const RenderContract& contract, bool hudCaptured, bool dualFirstHeld);
// XrMenuPanel.cpp
void EnsurePointerTexture();
std::array<ui::Eye,2> OpticalEyes(const XrView (&views)[2]);
const ui::MenuAnchor& MenuAnchorFor(const Pose& head, const XrView (&views)[2], std::uint64_t reference);
void FillPointerLayers(const UiPointerVisual& pointer, const ui::Panel& surface, const Vec3& head,
                       XrCompositionLayerQuad& cursor, XrCompositionLayerQuad& beam, bool& cursorActive, bool& beamActive);
bool MenuPanelStale(unsigned long long reference);
void PlaceMenuPanel(const Pose& head, const XrView (&views)[2], unsigned long long reference);
void FillMenuLayer(XrCompositionLayerQuad& quad, const ui::Panel& panel, XrSwapchain swapchain,
                   XrCompositionLayerFlags flags);
// XrOverlayLayers.cpp
bool BuildHudLayer(const FrameView& view, ID3D11Texture2D* hudTexture, XrCompositionLayerQuad& layer, float& panelWidth);
bool BuildReticleLayer(const FrameView& view, const HudPartImage& image, const ReticleTarget* target,
                       float hudPanelWidth, XrCompositionLayerQuad& layer);
bool BuildWheelLayer(const FrameView& view, const HudPartImage& image, XrCompositionLayerQuad& layer);
bool BuildNoteLayer(const FrameView& view, ID3D11Texture2D* texture, float distance, float scale,
                    XrCompositionLayerQuad& layer);
bool BuildTextLayer(const FrameView& view, ID3D11Texture2D* texture, float distance, float scale,
                    XrCompositionLayerQuad& layer);
bool BuildWristLayer(const FrameView& view, ID3D11Texture2D* nativeWristTexture, XrCompositionLayerQuad& layer);
bool BuildSlotLayer(const FrameView& view, XrCompositionLayerQuad& layer);
bool BuildFadeLayer(const FrameView& view, XrCompositionLayerQuad& layer);
// XrHoldFrames.cpp
bool LockForRenderThread(std::unique_lock<std::timed_mutex>& lock);
void EndLoadingScreen();
void ApplyHoldProtection();
DWORD WINAPI HoldThread(LPVOID);

} // namespace preyvr::dll::xrsession
