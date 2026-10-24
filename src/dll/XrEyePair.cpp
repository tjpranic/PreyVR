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
#include "BodyFollow.h"
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

// The eye pair: each eye's image held with the contract that rendered it, and
// submitted as a complete pair; the frustum declared for it; the both-eyes
// frame's first-eye hold and its overlay window (XrSessionState.h).

namespace preyvr::dll {
using namespace xrsession;

namespace {

// Logs a line at most once, so a per-frame refusal cannot fill the log.
void LogOnce(const std::string& line)
{
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true, std::memory_order_acq_rel)) {
        Log(line);
    }
}

// The two held eye images, created on first use at the backbuffer's size and
// format. Shared by the per-frame submission and the rung-1b mid-frame hold, so
// both see the same textures.
bool EnsureEyeImages(const D3D11_TEXTURE2D_DESC& desc)
{
    for (int eye = 0; eye < 2; ++eye) {
        if (gHost.eyeImage[eye] != nullptr) {
            continue;
        }
        D3D11_TEXTURE2D_DESC hold = desc;
        hold.Usage = D3D11_USAGE_DEFAULT;
        // A render target so the debug overlay (dbg.draw) can draw into the eye
        // it belongs to. CopyResource ignores bind flags; without the flag the
        // overlay is refused and the image is unaffected.
        hold.BindFlags = D3D11_BIND_RENDER_TARGET;
        hold.CPUAccessFlags = 0;
        hold.MiscFlags = 0;
        gHost.heldWidth = hold.Width;
        gHost.heldHeight = hold.Height;
        gHost.heldFormat = static_cast<std::uint32_t>(hold.Format);
        if (FAILED(gHost.device->CreateTexture2D(&hold, nullptr, &gHost.eyeImage[eye]))) {
            hold.BindFlags = 0;
            gHost.eyeImage[eye] = nullptr;
        }
        if (gHost.eyeImage[eye] == nullptr &&
            FAILED(gHost.device->CreateTexture2D(&hold, nullptr, &gHost.eyeImage[eye]))) {
            gHost.eyeImage[eye] = nullptr;
            LogOnce("result=failed step=create_eye_image");
            return false;
        }
    }
    return true;
}

} // namespace

namespace xrsession {

XrFovf ScaleDeclaredFov(const XrFovf& fov)
{
    const unsigned int percent = gMirrorFovPercent.load(std::memory_order_acquire);
    if (percent == 100u) { return fov; }
    const auto scaled = stereoframe::ScaleFov({fov.angleLeft, fov.angleRight, fov.angleDown, fov.angleUp}, percent);
    XrFovf out{};
    out.angleLeft = scaled.angleLeft;
    out.angleRight = scaled.angleRight;
    out.angleUp = scaled.angleUp;
    out.angleDown = scaled.angleDown;
    return out;
}

// Reads Prey's live view camera and returns the frustum to declare.
//
// The same code path the verified PreyVR_ReadDeclaredFovPtr uses, so what is
// submitted is what was measured on 2026-09-02 -- 120 degrees horizontal by
// 88.507 vertical, zero asymmetry, confirmed against the player's FOV slider.
//
// Returns nothing rather than guessing. Every caller must treat that as "do not
// submit a layer".
std::optional<XrFovf> DeclaredFovFromLiveCamera()
{
    const std::uint8_t* const camera = LiveViewCamera();
    if (camera == nullptr) {
        return std::nullopt;
    }
    const auto view = stereoframe::TangentsFromCamera(
        std::span<const std::uint8_t>(camera, engine::CameraLayout::size));
    if (!view) {
        return std::nullopt;
    }
    const auto angles = stereoframe::AnglesFromTangents(*view);
    XrFovf fov{};
    fov.angleLeft = angles.angleLeft;
    fov.angleRight = angles.angleRight;
    fov.angleDown = angles.angleDown;
    fov.angleUp = angles.angleUp;
    return fov;
}

// Holds one image per eye and submits a complete pair every frame.
//
// Prey renders one eye at a time, so the backbuffer is only ever half of a
// stereo pair. This keeps the other half from the last time that eye was on
// screen. The frame service consumes one eye label at every renderer frame,
// including menu/flat/skipped XR frames, and passes that label with this buffer.
//
// Returns false until both eyes have been seen at least once. Submitting a pair
// with one empty half would show a black eye, which reads as a broken headset
// rather than as a warming-up mod.
bool SubmitStereoPair(
    ID3D11DeviceContext* context,
    ID3D11Texture2D* backBuffer,
    std::uint32_t imageIndex,
    const XrView* views,
    const RenderContract& contract, bool hudCaptured, bool dualFirstHeld)
{
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);

    if (!EnsureEyeImages(desc)) {
        return false;
    }

    // **The eye arrives with the frame.** No dwell, no lock, no waiting: the
    // camera hook published which eye it built and this pops the one belonging
    // to the frame just finished. Every rendered frame therefore updates an eye,
    // so a pair refreshes every 2 frames rather than every 2*dwell.
    //
    // -1 means the main thread has not published yet -- starting up, or the
    // render thread ran ahead. Hold the existing pair for a frame rather than
    // copying a backbuffer whose eye is unknown, which would be a coin flip.
    //
    // A frame built before a snap or a recenter and arriving after it is still
    // submitted: its poses are in tracking space, which neither changes. Only a
    // change of the space itself strands it (SubmittableRenderContract).
    if(!SubmittableRenderContract(contract,dll::HeadTrackingSpaceEpoch())) {
        gEyeContractsRefused.fetch_add(1);
        static unsigned refused=0;
        if(++refused%300==1) Log("result=refused detail=render_contract eye="+std::to_string(contract.eye)+
            " valid="+std::to_string(contract.valid)+" time="+std::to_string(contract.displayTime)+
            " space="+std::to_string(contract.spaceEpoch)+"/"+std::to_string(dll::HeadTrackingSpaceEpoch())+
            " lag="+std::to_string(dll::EyeHandoffLag())+" dropped="+std::to_string(dll::EyeHandoffDroppedCount()));
        gHost.eyeImageValid[0]=gHost.eyeImageValid[1]=false;
        return false;
    }
    if(gHost.eyeReference!=contract.reference) {
        gHost.eyeImageValid[0]=gHost.eyeImageValid[1]=false;
        gHost.eyeReference=contract.reference;
    }
    static unsigned accepted=0;
    if(++accepted<=4 || accepted%1000==0) {
        std::ostringstream receipt;
        receipt<<"render_contract accepted eye="<<contract.eye<<" sourceTime="<<contract.displayTime
            <<" pose="<<contract.pose.position.x<<","<<contract.pose.position.y<<","<<contract.pose.position.z
            <<" q="<<contract.pose.orientation.x<<","<<contract.pose.orientation.y<<","<<contract.pose.orientation.z<<","<<contract.pose.orientation.w;
        Log(receipt.str());
    }
    const int eye = contract.eye;
    if (eye == kRenderContractBothEyes) {
        // **Rung 1b: both eyes come from this one frame.** The first scene
        // submission was copied mid-frame into its held image (HoldDualFirstEye)
        // and the backbuffer holds the second. Both images and both poses are
        // published together from this one record. A frame whose first half was
        // not held is refused outright: pairing a fresh right eye with an older
        // left one is exactly the cross-time pair this route exists to remove.
        if (!dualFirstHeld) {
            const auto refused = gDualPairsRefused.fetch_add(1, std::memory_order_relaxed) + 1;
            // Every one, whatever the log throttle: it marks the frame the cull
            // lag starts on (docs/CULL-CAMERA-CRASH-2026-10-17.md).
            ProbeRecord(ProbeStep::firstEyeRefused, static_cast<std::uint32_t>(refused));
            if (refused <= 4 || refused % 300 == 0) {
                Log("result=refused detail=dual_first_eye_not_held refused=" + std::to_string(refused));
            }
            gHost.eyeImageValid[0] = gHost.eyeImageValid[1] = false;
            return false;
        }
        const int first = gSwapEyes.load(std::memory_order_acquire) ? 1 : 0;
        const int second = 1 - first;
        context->CopyResource(gHost.eyeImage[second], backBuffer);
        // The first image was taken before the HUD is drawn; RecordEye for it
        // happened at the hold. The second carries the native HUD unless the HUD
        // layer redirected it.
        gHost.hudCapturePair.RecordEye(second, hudCaptured);
        for (int rendered = 0; rendered < 2; ++rendered) {
            const int target = rendered == 0 ? first : second;
            const Pose& source = contract.eyePoses[rendered];
            RenderContract eyeContract = contract;
            eyeContract.eye = rendered;
            eyeContract.pose = source;
            const float shown[4] = {std::tan(views[target].fov.angleLeft), std::tan(views[target].fov.angleRight),
                                    std::tan(views[target].fov.angleUp), std::tan(views[target].fov.angleDown)};
            DrawDebugOverlay(gHost.device, context, gHost.eyeImage[target], eyeContract, shown);
            gHost.eyePose[target] = ToXrPose(source);
            gHost.eyeFov[target] = {std::atan(contract.tangents[0]), std::atan(contract.tangents[1]),
                std::atan(contract.tangents[2]), std::atan(contract.tangents[3])};
            gHost.requestedFov[target] = views[target].fov;
            gHost.requestedFovValid[target] = true;
            gEyeContractsPublished.fetch_add(1, std::memory_order_relaxed);
            gHost.eyeDisplayTime[target] = contract.displayTime;
            gHost.eyeImageValid[target] = true;
        }
        const auto pairs = gDualPairsSubmitted.fetch_add(1, std::memory_order_relaxed) + 1;
        if (pairs == 1) {
            Log("result=0 detail=first_dual_pair displayTime=" + std::to_string(contract.displayTime));
        }
    } else if (eye == 0 || eye == 1) {
        const int target = gSwapEyes.load(std::memory_order_acquire) ? (1 - eye) : eye;
        context->CopyResource(gHost.eyeImage[target], backBuffer);
        // Drawn into this eye with the contract that rendered it, so the
        // overlay and the pixels beneath it can never belong to different poses.
        const float shown[4] = {std::tan(views[target].fov.angleLeft), std::tan(views[target].fov.angleRight),
                                std::tan(views[target].fov.angleUp), std::tan(views[target].fov.angleDown)};
        DrawDebugOverlay(gHost.device, context, gHost.eyeImage[target], contract, shown);
        gHost.hudCapturePair.RecordEye(target,hudCaptured);
        // Publish the pixels and the contract that produced them together. The
        // copy and these three writes are the one publication unit; nothing
        // downstream may pair this image with any other frame's pose or FOV.
        gHost.eyePose[target] = ToXrPose(contract.pose);
        gHost.eyeFov[target] = {std::atan(contract.tangents[0]),std::atan(contract.tangents[1]),
            std::atan(contract.tangents[2]),std::atan(contract.tangents[3])};
        // Stored in the SAME publication unit as the declaration it is compared
        // against, so a coverage figure can never pair this frame's request with
        // another frame's render.
        gHost.requestedFov[target] = views[target].fov;
        gHost.requestedFovValid[target] = true;
        // Checked here, at the one point the declaration is bound to the pixels
        // it describes -- the same publication unit FAIL-STR-044 established.
        gEyeContractsPublished.fetch_add(1, std::memory_order_relaxed); // same immutable camera record
        gHost.eyeDisplayTime[target] = contract.displayTime;
        gHost.eyeImageValid[target] = true;
    }

    if (!gHost.eyeImageValid[0] || !gHost.eyeImageValid[1]) {
        return false;
    }
    // Both halves are present, so the pair about to be submitted is complete and
    // its skew is meaningful. Sampled here rather than at the copy, because a
    // single eye's timestamp is not a skew until the other one exists.
    if (gHost.eyeDisplayTime[0] != 0 && gHost.eyeDisplayTime[1] != 0) {
        const XrTime a = gHost.eyeDisplayTime[0], b = gHost.eyeDisplayTime[1];
        const auto skew = static_cast<std::uint64_t>(a > b ? a - b : b - a);
        gEyeSkew.AddNanoseconds(skew);
        gEyeSkewSamples.fetch_add(1, std::memory_order_relaxed);
        // Which eye is behind, not merely how far. A skew that stays on one eye
        // is a scheduling asymmetry; one that alternates is the ordinary
        // one-frame-per-eye cadence.
        gStaleEye.store(a < b ? 0 : 1, std::memory_order_relaxed);
    }
    for (std::uint32_t slice = 0; slice < 2; ++slice) {
        context->CopySubresourceRegion(
            gHost.images[imageIndex], D3D11CalcSubresource(0, slice, 1),
            0, 0, 0, gHost.eyeImage[slice], 0, nullptr);
    }
    return true;
}

} // namespace xrsession

DWORD SetMirrorFovPercent(unsigned int percent)
{
    // Clamped, not rejected. Below 100 shrinks the image further, which nobody
    // wants but is harmless; far above it declares a field wider than the
    // headset has and the compositor simply crops.
    const unsigned int clamped = percent < 50u ? 50u : (percent > 400u ? 400u : percent);
    gMirrorFovPercent.store(clamped, std::memory_order_release);
    Log("mirror_fov percent=" + std::to_string(clamped));
    return 0;
}

DWORD MirrorFovPercent() { return gMirrorFovPercent.load(std::memory_order_relaxed); }

DWORD SetXrStereoSubmission(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on) {
        // A backlog from a previous arm would decide the first frames' eyes.
        dll::ResetEyeHandoff();
        // The handoff only ever sees both eyes if the camera hook is free-running.
        // A lock left set from an offline capture would publish one eye forever,
        // and the pair would never complete.
        dll::SetStereoEyeLockFromRenderThread(-1);
    }
    gStereoSubmission.store(on, std::memory_order_release);
    std::ostringstream line;
    line << "result=0 detail=stereo_submission enabled=" << (on ? "1" : "0");
    Log(line.str());
    return static_cast<DWORD>(gStatus.load(std::memory_order_acquire));
}

bool XrRequestedEyeFov(int eye, float* left, float* right, float* up, float* down)
{
    if (eye < 0 || eye > 1 || left == nullptr || right == nullptr ||
        up == nullptr || down == nullptr) {
        return false;
    }
    std::array<XrFovf,2> optical{};
    if (!gOptics.TryRead(optical)) { return false; }
    const XrFovf& fov = optical[eye];
    if (!(fov.angleRight > fov.angleLeft) || !(fov.angleUp > fov.angleDown)) {
        return false;
    }
    *left = fov.angleLeft;
    *right = fov.angleRight;
    *up = fov.angleUp;
    *down = fov.angleDown;
    return true;
}

unsigned long long EyeContractsPublishedCount() { return gEyeContractsPublished.load(std::memory_order_relaxed); }

unsigned long long EyeContractsRefusedCount() { return gEyeContractsRefused.load(std::memory_order_relaxed); }

bool HoldDualFirstEye(void* renderer)
{
    const auto refuse = [] {
        gDualHoldRefused.fetch_add(1, std::memory_order_relaxed);
        return false;
    };
    if (renderer == nullptr ||
        gStatus.load(std::memory_order_acquire) != static_cast<DWORD>(XrSessionStatus::running) ||
        !gStereoSubmission.load(std::memory_order_acquire)) {
        return refuse();
    }
    // This runs inside the engine's scene submission: it never waits, except
    // for a hold frame in flight. That marks the frame as Prey's, so no new
    // hold frame starts while it renders (LockForRenderThread).
    std::unique_lock lock(gMutex, std::defer_lock);
    if (!LockForRenderThread(lock) || !gHost.started || !gHost.sessionBegun || gHost.device == nullptr) {
        return refuse();
    }
    const auto backBuffer = AcquireBackBuffer(renderer);
    if (!backBuffer) {
        return refuse();
    }
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    // Same size gate as submission: an image of another size cannot be paired.
    bool held = desc.Width == gHost.width && desc.Height == gHost.height && EnsureEyeImages(desc);
    if (held) {
        if (const auto context = ImmediateContext(gHost.device)) {
            const int first = gSwapEyes.load(std::memory_order_acquire) ? 1 : 0;
            context->CopyResource(gHost.eyeImage[first], backBuffer.Get());
            // Taken before the HUD is drawn, so the native HUD is not in it.
            gHost.hudCapturePair.RecordEye(first, true);
            // Identity only, never dereferenced: what an overlay must be drawing
            // into for HeldFirstEyeTarget to repeat it into this eye.
            gHost.heldBackBuffer = reinterpret_cast<std::uintptr_t>(backBuffer.Get());
        } else {
            held = false;
        }
    }
    if (!held) {
        return refuse();
    }
    gDualFirstHeld.store(true, std::memory_order_release);
    gDualHolds.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void OpenDualOverlayWindow()
{
    // Only over a held first eye: without one there is nothing to draw into, and
    // the frame's both-eyes pair is refused anyway.
    if (gDualFirstHeld.load(std::memory_order_acquire)) {
        gDualOverlayOpen.store(true, std::memory_order_release);
    }
}

bool DualOverlayWindowOpen()
{
    return gDualOverlayOpen.load(std::memory_order_acquire);
}

ID3D11RenderTargetView* HeldFirstEyeTarget(ID3D11RenderTargetView* destination, const char** refusal)
{
    const auto refuse = [&](const char* why) -> ID3D11RenderTargetView* {
        if (refusal != nullptr) *refusal = why;
        return nullptr;
    };
    if (!gDualOverlayOpen.load(std::memory_order_acquire) || !gDualFirstHeld.load(std::memory_order_acquire)) {
        return refuse("window_closed");
    }
    if (destination == nullptr) return refuse("no_destination");
    // This runs inside the engine's overlay draws: it never waits, except for a
    // hold frame in flight (LockForRenderThread).
    std::unique_lock lock(gMutex, std::defer_lock);
    if (!LockForRenderThread(lock) || gHost.device == nullptr) return refuse("busy");
    // Only what is drawn into the frame's own backbuffer belongs to the eye;
    // Flash filters and other offscreen targets stay as they are.
    ComPtr<ID3D11Resource> drawn;
    destination->GetResource(&drawn);
    const bool intoBackBuffer = drawn != nullptr && reinterpret_cast<std::uintptr_t>(drawn.Get()) == gHost.heldBackBuffer;
    if (!intoBackBuffer) return refuse("not_backbuffer");
    ID3D11Texture2D* eye = gHost.eyeImage[gSwapEyes.load(std::memory_order_acquire) ? 1 : 0];
    if (eye == nullptr) return refuse("no_eye_image");
    // The game's own view of the backbuffer, so the second draw encodes its
    // colours exactly as the first did. The held image has the backbuffer's format.
    D3D11_RENDER_TARGET_VIEW_DESC view{};
    destination->GetDesc(&view);
    if (gHost.heldTargetView != nullptr && (gHost.heldTargetImage != eye || gHost.heldTargetFormat != view.Format)) {
        gHost.heldTargetView->Release();
        gHost.heldTargetView = nullptr;
    }
    if (gHost.heldTargetView == nullptr) {
        if (FAILED(gHost.device->CreateRenderTargetView(eye, &view, &gHost.heldTargetView))) {
            gHost.heldTargetView = nullptr;
            return refuse("view_create");
        }
        gHost.heldTargetImage = eye;
        gHost.heldTargetFormat = view.Format;
    }
    return gHost.heldTargetView;
}

std::string DualPairReport()
{
    std::ostringstream out;
    out << " pairHolds=" << gDualHolds.load(std::memory_order_relaxed)
        << " pairHoldRefused=" << gDualHoldRefused.load(std::memory_order_relaxed)
        << " pairsSubmitted=" << gDualPairsSubmitted.load(std::memory_order_relaxed)
        << " pairsRefused=" << gDualPairsRefused.load(std::memory_order_relaxed);
    return out.str();
}

DWORD SetXrSwapEyes(unsigned int enabled)
{
    const bool on = enabled != 0u;
    gSwapEyes.store(on, std::memory_order_release);
    std::ostringstream line;
    line << "result=0 detail=swap_eyes enabled=" << (on ? "1" : "0");
    Log(line.str());
    return static_cast<DWORD>(gStatus.load(std::memory_order_acquire));
}

bool XrSwapEyes() { return gSwapEyes.load(std::memory_order_acquire); }

} // namespace preyvr::dll
