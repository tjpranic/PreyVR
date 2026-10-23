#include "MainRenderHook.h"
#include "EyeRecords.h"
#include "StereoEyeCamera.h"
#include "SafeMemory.h"
#include "HookInstall.h"
#include "EngineAccess.h"

#include "MinHookInit.h"

#include "AimTakeover.h"
#include "FrameSchedule.h"

#include "HeadTrackingHook.h"

#include "FrameCaptureWin32.h"
#include "Logger.h"
#include "preyvr/CameraEdit.h"
#include "preyvr/EngineMap.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/StereoFrame.h"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <sstream>

namespace preyvr::dll {
namespace {

// CSystem::Render (R-058). Prologue stops before the jne's rel32 so the
// signature is the stable part: push r15 / sub rsp,0xD0 / cmp byte [rcx+0x9D7],0
// / mov r15,rcx. The +0x9D7 test is what makes it distinctive rather than a
// generic MSVC prologue.
constexpr std::uintptr_t kSystemRenderRva = 0xE0BA30;
constexpr std::array<std::uint8_t, 19> kSystemRenderPrologue{
    0x41, 0x57, 0x48, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00, 0x80,
    0xB9, 0xD7, 0x09, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xF9,
};

// CCamera::UpdateFrustum. Rebuilds the corners, the six planes at +0x10C, the
// plane sign tables and the cached position at +0x230.
constexpr std::uintptr_t kUpdateFrustumRva = 0x121D70;
// Gated on the `camera.update_frustum` landmark.

using SystemRenderFn = void(__fastcall*)(void* system);
using UpdateFrustumFn = void(__fastcall*)(void* camera);

// Timed, for the reason XrSessionHost is: a control export called from an
// external tool can have its call aborted mid-flight, and a plain mutex held at
// that moment is never released -- poisoning every later call with a hang rather
// than an error. Observed live 2026-08-31, where it silently stopped the yaw
// from arming and left a capture that looked fine and proved nothing.
std::timed_mutex gMutex;
constexpr auto kControlLockTimeout = std::chrono::milliseconds(250);
std::atomic<DWORD> gStatus{static_cast<DWORD>(CameraEditStatus::unavailable)};
std::atomic<SystemRenderFn> gOriginal{nullptr};
std::atomic<UpdateFrustumFn> gUpdateFrustum{nullptr};
std::atomic<unsigned long long> gApplied{0};
std::atomic<unsigned long long> gRestoreFailures{0};
std::atomic<bool> gLoggedFirstApplication{false};
std::atomic<float> gStereoIpd{0.0f};
std::atomic<float> gStereoHalfFov{50.0f};
std::atomic<unsigned long long> gEyeCounter{0};
std::atomic<int> gLastEye{-1};

std::atomic<int> gEyeLock{-1};   // -1 = alternate

// Rung 1b. When set, the stereo branch leaves the head camera on the frame and
// publishes a both-eyes contract; the render thread submits the frame twice and
// builds each eye there (BuildDualEyeCamera, DualSceneStereo.h).
std::atomic<bool> gDualStereo{false};

void* gTarget = nullptr;
bool gHookCreated = false;

bool PrologueMatches(std::uintptr_t address, const std::uint8_t* expected, std::size_t length)
{
    return IsExecutableAddress(reinterpret_cast<const void*>(address)) &&
           std::memcmp(reinterpret_cast<const void*>(address), expected, length) == 0;
}

void __fastcall RenderWithCameraEdit(void* system)
{
    const SystemRenderFn original = gOriginal.load(std::memory_order_acquire);
    RenderContract renderContract{};
    const auto renderOriginal = [&](void* target) {
        // One record per native render, including refusals and flat/menu frames.
        // A missing record would shift every subsequent image's metadata.
        if(gStereoIpd.load()>0.f) PublishRenderContract(renderContract);
        // The cull guard and the frame probe around it (FrameSchedule.cpp).
        const schedule::EngineRenderBracket bracket = schedule::BeforeEngineRender(target);
        original(target);
        schedule::AfterEngineRender(bracket);
    };

    const bool stereoArmed = gStereoIpd.load(std::memory_order_acquire) > 0.0f;
    // The drains and the input lanes (FrameSchedule.cpp). Deliberately ahead of
    // the armed check below: driving a menu must not require stereo or head
    // tracking to be enabled first, and the early return would otherwise skip it.
    schedule::TickMainRender();

    if (!stereoArmed || system == nullptr) {
        if (original != nullptr) {
            UpdateAimReticleForRender();
            renderOriginal(system);
        }
        return;
    }

    auto* camera = reinterpret_cast<std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(system) + engine::SystemLayout::viewCamera);
    const std::span<std::uint8_t> live(camera, cameraedit::kCameraSize);

    cameraedit::RestorePoint restore{};
    if (!cameraedit::Capture(live, restore)) {
        if (original != nullptr) {
            renderOriginal(system);
        }
        return;
    }

    // Build the edited camera entirely in our own memory first. UpdateFrustum is
    // called on this copy, so the engine function never touches game state and
    // an aborted edit costs nothing.
    std::array<std::uint8_t, cameraedit::kCameraSize> edited{};
    std::memcpy(edited.data(), restore.bytes.data(), cameraedit::kCameraSize);

    const UpdateFrustumFn updateFrustum = gUpdateFrustum.load(std::memory_order_acquire);

    Pose renderHead{};
    const bool haveRenderHead=ConsumeHeadRenderPose(edited.data(),renderHead,
        renderContract.displayTime,renderContract.reference,renderContract.spaceEpoch);
    bool built = false;
    const bool dualFrame = gDualStereo.load(std::memory_order_acquire);
    if (dualFrame) {
        // Rung 1b: both eyes in this frame. The engine culls and prepares the
        // frame from the head camera, left exactly as it is; the render thread
        // submits it twice and builds, publishes and draws each eye there
        // (BuildDualEyeCamera), so the eye records hold the bits the render view
        // actually carries. This record states both eye poses for one display
        // time, offset from the head the same way BuildSyntheticEye offsets the
        // camera.
        const float renderIpd = gStereoIpd.load(std::memory_order_acquire);
        built = true;
        gLastEye.store(kRenderContractBothEyes, std::memory_order_release);
        SetFrameCaptureTagOverride(-1);
        renderContract.eye = kRenderContractBothEyes;
        renderContract.pose = renderHead;
        for (int eye = 0; eye < 2; ++eye) {
            renderContract.eyePoses[eye] = Compose(renderHead, Pose{Quaternion{}, Vec3{
                stereo::EyeSign(eye) * .5f * renderIpd, 0, 0}});
        }
        const auto fov = stereoframe::TangentsFromCamera(edited);
        if (fov) renderContract.tangents = {fov->tanLeft, fov->tanRight, fov->tanUp, fov->tanDown};
        renderContract.valid = haveRenderHead && fov.has_value();
    } else {
        // Alternate every frame. With the simulation frozen, consecutive frames
        // differ only by the eye, which is exactly a stereo pair.
        // A locked eye is held for as long as it is set, so a capture taken a
        // few frames later is unambiguously that eye no matter how the game and
        // render threads are phased. Alternating remains available, but nothing
        // should depend on identifying which frame it produced.
        const int locked = gEyeLock.load(std::memory_order_acquire);
        const int eye = (locked == 0 || locked == 1)
            ? locked
            : static_cast<int>(gEyeCounter.fetch_add(1, std::memory_order_relaxed) & 1ull);
        const float renderIpd = gStereoIpd.load(std::memory_order_acquire);
        const stereo::Matrix34 centre = stereo::ReadMatrix(edited);
        built = BuildSyntheticEye(edited, eye,
                                  renderIpd,
                                  gStereoHalfFov.load(std::memory_order_acquire));
        if (built) {
            PublishBuiltEye(stereo::ReadMatrix(edited), centre, eye);
            gLastEye.store(eye, std::memory_order_release);
            // Stamp the capture so the two dumps of a pair cannot be confused.
            SetFrameCaptureTagOverride(eye);
            // Hand the eye to the render thread alongside the frame it belongs
            // to, so submission never has to wait to find out which one it got.
            renderContract.eye=eye;
            renderContract.pose=Compose(renderHead,Pose{Quaternion{},Vec3{
                stereo::EyeSign(eye)*.5f*renderIpd,0,0}});
            const auto fov=stereoframe::TangentsFromCamera(edited);
            if(fov) renderContract.tangents={fov->tanLeft,fov->tanRight,fov->tanUp,fov->tanDown};
            renderContract.valid=haveRenderHead && fov.has_value();
        }
    }

    if (!built || updateFrustum == nullptr) {
        renderContract.valid=false;
        if (original != nullptr) {
            renderOriginal(system);
        }
        return;
    }
    updateFrustum(edited.data());

    // Gated on both predicates: orthonormal keeps the engine from negating its
    // plane normals, and non-degenerate closes the hole where an all-zero matrix
    // satisfies the engine's own check.
    if (!cameraedit::RotationIsSafeToWrite(edited)) {
        lifecycle::Log("preyvr_camera_edit result=refused detail=unsafe_rotation");
        // Disarm stereo: leaving it armed after rejecting its camera would retry
        // the same bad build every frame.
        gStereoIpd.store(0.0f, std::memory_order_release);
        SetFrameCaptureTagOverride(-1);
        if (original != nullptr) {
            renderOriginal(system);
        }
        return;
    }

    std::memcpy(camera, edited.data(), cameraedit::kCameraSize);
    // The camera this both-eyes frame is prepared from, so DrawNear particle
    // geometry built from it can be told apart (IsDualFrameCentre), and so the
    // render thread can find this frame's record by it (NoteDualFrameCamera).
    if (dualFrame) {
        const stereo::Matrix34 head = stereo::ReadMatrix(edited);
        PublishFrameCentre(head);
        renderContract.camera = head;
        renderContract.haveCamera = true;
    }

    UpdateAimReticleForRender();

    if (original != nullptr) {
        renderOriginal(system);
    }

    // **Restore everything, and verify it.** A restore that is merely performed
    // is a hope; FAIL-HAND-037 was a half-IPD translation escaping into
    // consumers that had no idea it was there, alternating every frame.
    std::memcpy(camera, restore.bytes.data(), cameraedit::kCameraSize);
    if (!cameraedit::MatchesRestorePoint(live, restore)) {
        gRestoreFailures.fetch_add(1, std::memory_order_relaxed);
        gStereoIpd.store(0.0f, std::memory_order_release);
        SetFrameCaptureTagOverride(-1);
        lifecycle::Log("preyvr_camera_edit result=restore_failed detail=disarmed");
        return;
    }

    const unsigned long long applied = gApplied.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!gLoggedFirstApplication.exchange(true)) {
        // Logged once rather than per frame: at 144 Hz a per-frame line would
        // bury everything else in the smoke log within seconds.
        const auto position = cameraedit::PositionOf(restore.bytes);
        std::ostringstream line;
        line << "preyvr_camera_edit result=0 detail=first_application mode=stereo"
             << " ipd=" << gStereoIpd.load(std::memory_order_acquire)
             << " eye=" << gLastEye.load(std::memory_order_acquire)
             << " originalPos=" << position[0] << ',' << position[1] << ',' << position[2]
             << " applied=" << applied;
        lifecycle::Log(line.str());
    }
}

bool EnsureHook()
{
    if (gHookCreated) {
        return true;
    }

    const auto base = PreyBase();
    if (base == 0) {
        return false;
    }

    const auto renderAddress = base + kSystemRenderRva;
    const auto frustumAddress = base + kUpdateFrustumRva;
    if (!PrologueMatches(renderAddress, kSystemRenderPrologue.data(), kSystemRenderPrologue.size())) {
        lifecycle::Log("preyvr_camera_edit result=unavailable detail=system_render_prologue");
        return false;
    }
    const engine::Landmark* frustum = engine::FindLandmark("camera.update_frustum");
    if (frustum == nullptr || frustum->rva != kUpdateFrustumRva ||
        !PrologueMatches(frustumAddress, frustum->expected.data(), frustum->expected.size())) {
        lifecycle::Log("preyvr_camera_edit result=unavailable detail=update_frustum_prologue");
        return false;
    }
    gUpdateFrustum.store(reinterpret_cast<UpdateFrustumFn>(frustumAddress),
                         std::memory_order_release);

    gTarget = reinterpret_cast<void*>(renderAddress);
    const auto installed = hook::Install(gTarget, reinterpret_cast<void*>(&RenderWithCameraEdit), gOriginal);
    if (!installed) {
        lifecycle::Log(std::string("preyvr_camera_edit result=failed detail=") + hook::StepName(installed.step) +
                       " minhook=" + MH_StatusToString(installed.status));
        return false;
    }

    gHookCreated = true;
    gStatus.store(static_cast<DWORD>(CameraEditStatus::ready), std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_camera_edit result=ready targetRva=0x" << std::hex << std::uppercase
         << kSystemRenderRva << " target=0x" << renderAddress;
    lifecycle::Log(line.str());
    return true;
}

} // namespace

DWORD EnsureRenderHookInstalled()
{
    // **Menu input needs this seam without arming a camera edit.** The drain for
    // queued input runs inside the `CSystem::Render` hook, and that hook was only
    // ever installed by the stereo and head-tracking paths -- so posting worked,
    // reported success, queued its events and never delivered one. The live run
    // showed `queued=4 posted=0 drainThread=0`: the counters found it, the return
    // codes did not.
    //
    // Installing without arming is safe by construction: with nothing armed the
    // hook forwards to the original after draining.
    return EnsureHook() ? 0u : 1u;
}

DWORD SetDualStereoContract(unsigned int enabled)
{
    const bool on = enabled != 0;
    if (gDualStereo.exchange(on, std::memory_order_acq_rel) != on) {
        lifecycle::Log(std::string("preyvr_camera_edit result=0 detail=dual_stereo value=") +
                       (on ? "1" : "0"));
    }
    return 0;
}

bool DualStereoContractEnabled()
{
    return gDualStereo.load(std::memory_order_acquire);
}

float SyntheticStereoIpdMetres()
{
    return gStereoIpd.load(std::memory_order_acquire);
}

bool BuildDualEyeCamera(std::uint8_t* camera, int eye)
{
    const float ipd = gStereoIpd.load(std::memory_order_acquire);
    const UpdateFrustumFn updateFrustum = gUpdateFrustum.load(std::memory_order_acquire);
    if (camera == nullptr || !(ipd > 0.0f) || (eye != 0 && eye != 1) || updateFrustum == nullptr) {
        return false;
    }
    std::array<std::uint8_t, cameraedit::kCameraSize> edited{};
    std::memcpy(edited.data(), camera, edited.size());
    const stereo::Matrix34 centre = stereo::ReadMatrix(edited);
    // The same construction the alternate-eye path uses, so the two routes give
    // identical eyes for identical heads.
    if (!BuildSyntheticEye(edited, eye, ipd, gStereoHalfFov.load(std::memory_order_acquire))) {
        return false;
    }
    updateFrustum(edited.data());
    if (!cameraedit::RotationIsSafeToWrite(edited)) {
        return false;
    }
    PublishBuiltEye(stereo::ReadMatrix(edited), centre, eye);
    std::memcpy(camera, edited.data(), edited.size());
    return true;
}

DWORD SetSyntheticStereo(float ipdMetres, float halfFovDegrees)
{
    std::unique_lock lock(gMutex, kControlLockTimeout);
    if (!lock.owns_lock()) {
        lifecycle::Log("preyvr_camera_edit result=refused detail=busy step=SetSyntheticStereo");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }

    if (ipdMetres == 0.0f) {
        gStereoIpd.store(0.0f, std::memory_order_release);
        gLastEye.store(-1, std::memory_order_release);
        SetFrameCaptureTagOverride(-1);
        if (gHookCreated) {
            gStatus.store(static_cast<DWORD>(CameraEditStatus::ready), std::memory_order_release);
        }
        lifecycle::Log("preyvr_camera_edit result=0 detail=stereo_disarmed");
        return gStatus.load(std::memory_order_acquire);
    }

    // A human IPD is 55-75mm. Anything outside that is a caller error rather
    // than an unusual head, and a wildly wrong separation is exactly the input
    // that produces a nauseating image instead of an obviously broken one.
    if (!std::isfinite(ipdMetres) || ipdMetres < 0.045f || ipdMetres > 0.085f ||
        !std::isfinite(halfFovDegrees) || halfFovDegrees < 20.0f || halfFovDegrees > 70.0f) {
        lifecycle::Log("preyvr_camera_edit result=refused detail=stereo_out_of_bounds");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }

    if (!EnsureHook()) {
        gStatus.store(static_cast<DWORD>(CameraEditStatus::unavailable), std::memory_order_release);
        return static_cast<DWORD>(CameraEditStatus::unavailable);
    }

    gStereoIpd.store(ipdMetres, std::memory_order_release);
    gStereoHalfFov.store(halfFovDegrees, std::memory_order_release);
    gEyeCounter.store(0, std::memory_order_release);
    gLoggedFirstApplication.store(false, std::memory_order_release);
    gStatus.store(static_cast<DWORD>(CameraEditStatus::armed), std::memory_order_release);

    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=stereo_armed ipd=" << ipdMetres
         << " halfFovDegrees=" << halfFovDegrees
         << " projectionPolicy=" << (NativeProjectionEnabled() != 0u
                                         ? "prey_native" : "synthetic");
    lifecycle::Log(line.str());
    return static_cast<DWORD>(CameraEditStatus::armed);
}

DWORD SetStereoEyeLock(unsigned int eye)
{
    const int value = (eye == 0u || eye == 1u) ? static_cast<int>(eye) : -1;
    gEyeLock.store(value, std::memory_order_release);
    // The capture tag follows the lock, so it is correct again whenever one is
    // held -- and meaningless, by design, when alternating.
    SetFrameCaptureTagOverride(value);
    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=eye_lock eye="
         << (value < 0 ? "alternate" : (value == 0 ? "left" : "right"));
    lifecycle::Log(line.str());
    return static_cast<DWORD>(gStatus.load(std::memory_order_acquire));
}

void SetStereoEyeLockFromRenderThread(int eye)
{
    // **Deliberately lock-free, and deliberately not SetStereoEyeLock.**
    //
    // The stereo submission path drives eye alternation from inside Prey's
    // render loop. SetStereoEyeLock takes gMutex with a timeout, and a control
    // mutex on the render thread is exactly the shape that turns a slow frame
    // into a hitch and a contended one into a stall. This touches a single
    // atomic and nothing else.
    //
    // It also does not move the frame-capture tag. That tag exists to keep two
    // offline dumps from being confused with each other, which is a debugging
    // concern; submission has its own eye identity and does not read it.
    gEyeLock.store((eye == 0 || eye == 1) ? eye : -1, std::memory_order_release);
}

int LastRenderedEye()
{
    return gLastEye.load(std::memory_order_acquire);
}

DWORD CameraEditStatusValue()
{
    return gStatus.load(std::memory_order_acquire);
}

unsigned long long CameraEditAppliedCount()
{
    return gApplied.load(std::memory_order_acquire);
}

unsigned long long CameraEditRestoreFailureCount()
{
    return gRestoreFailures.load(std::memory_order_acquire);
}

} // namespace preyvr::dll
