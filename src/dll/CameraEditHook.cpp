#include "CameraEditHook.h"
#include "SafeMemory.h"
#include "HookInstall.h"
#include "EngineAccess.h"

#include "XrSessionHost.h"
#include "MinHookInit.h"

#include "InputPost.h"
#include "HudBridge.h"
#include "UiPointer.h"
#include "AimTakeover.h"
#include "MoveLane.h"
#include "BindingDump.h"
#include "BodyFollow.h"
#include "ControllerPrompts.h"
#include "WrenchDraw.h"

#include "HeadTrackingHook.h"

#include "FrameCaptureWin32.h"
#include "CullCameraWatch.h"
#include "CullPrepareGuard.h"
#include "FrameProbe.h"
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

// A small ring of recently built eyes, matched by camera position. Sized well
// past the game/render thread separation so a view in flight can still find the
// record that produced it.
constexpr unsigned int kBuiltEyeSlots = 16;
std::array<BuiltEye, kBuiltEyeSlots> gBuiltEyes{};
std::atomic<unsigned long long> gBuiltEyeSerial{0};
std::atomic<unsigned long long> gBuiltEyeCount{0};
std::atomic<unsigned long long> gBuiltEyeMisses{0};
// The head cameras of recent both-eyes frames, the camera the engine prepares
// those frames from (IsDualFrameCentre). Written on the main thread before the
// frame renders; a few frames deep for the 3D engine's jobs.
struct FrameCentre {
    float position[3];
    unsigned long long serial;
};
constexpr unsigned int kFrameCentreSlots = 8;
std::array<FrameCentre, kFrameCentreSlots> gFrameCentres{};
std::atomic<unsigned long long> gFrameCentreCount{0};
std::atomic<int> gEyeLock{-1};   // -1 = alternate
// Rung 1b. When set, the stereo branch leaves the head camera on the frame and
// publishes a both-eyes contract; the render thread submits the frame twice and
// builds each eye there (BuildDualEyeCamera, DualSceneStereo.h).
std::atomic<bool> gDualStereo{false};
std::atomic<float> gAsymmetry{1.1f};

// Rung 3: inherit Prey's own projection instead of overwriting it.
//
// **Why this exists.** Every eye camera below this point was built by replacing
// the engine's fov, projection ratio and asymmetry with synthetic values. That
// was the right thing to do while the real frustum was unmeasured -- a synthetic
// half-FOV exercises the asymmetry path, which is exactly what A2b needed.
//
// It is the wrong thing to do once the frustum is known, and as of 2026-09-02 it
// is known: Prey renders at tangents -1.732051/+1.732051 horizontally and
// -0.974279/+0.974279 vertically with all four asymmetry fields zero, confirmed
// against the player's own 120-degree FOV slider.
//
// PreyVR is injected and cannot change what Prey renders. Its pixels come from
// the game's frustum, so that frustum is what must be declared to OpenXR. If the
// eye camera's projection has been overwritten with a synthetic one, **the image
// no longer matches the declaration** -- and a mismatch between what is drawn and
// what is claimed is not visible on a monitor, only inside a headset, where it
// reads as warped depth rather than as a bug.
//
// So when this is set the eye camera differs from the engine's by translation
// alone. Parallax comes from the eye offset, which is the only thing A2b actually
// measured, and the projection is left exactly as the engine built it.
//
// Off by default: leaving it off keeps every prior measurement meaning what it
// meant when it was taken.
std::atomic<bool> gNativeProjection{false};
// **Render the frustum the headset actually asked for.** Measured on this
// machine: only 41.25% of the submitted pixel rectangle falls inside the
// runtime's requested frustum, with nothing missing (R-121). The other 59% is
// rasterised and then discarded by the compositor.
//
// Off by default and deliberately so. This changes the scene projection, and
// the near pass carries its own FOV (`r_DrawNearFoV`) that was tuned against the
// old one -- so enabling this without matching the near pass leaves the weapon
// model at the wrong scale. It is a prototype behind an opt-in, not a default.
std::atomic<bool> gRuntimeFrustum{false};
std::atomic<unsigned long long> gRuntimeFrustumUsed{0}, gRuntimeFrustumMissing{0};

// The forward axis the edit last wrote, for the pass-camera probe to compare
// against. Three floats rather than a matrix: the question is only which way the
// camera faces, and a direction is cheap to publish without a lock.
std::atomic<float> gWrittenForwardX{0.0f};
std::atomic<float> gWrittenForwardY{0.0f};
std::atomic<float> gWrittenForwardZ{0.0f};
std::atomic<bool> gHaveWrittenForward{false};

void* gTarget = nullptr;
bool gHookCreated = false;

bool PrologueMatches(std::uintptr_t address, const std::uint8_t* expected, std::size_t length)
{
    return IsExecutableAddress(reinterpret_cast<const void*>(address)) &&
           std::memcmp(reinterpret_cast<const void*>(address), expected, length) == 0;
}

// Produces the edited camera for a synthetic stereo eye, composed onto whatever
// camera the engine was about to render with.
// ---------------------------------------------------------------------------
// Eye handoff: which eye is in the frame the render thread is finishing
// ---------------------------------------------------------------------------
//
// **The problem this replaces.** The submission path used to identify the eye by
// asking and waiting: set the lock, hold it for N frames so the game thread's
// camera edit could reach the render thread, then take the image. That works,
// and it is why the first stereo test was unambiguous -- but the wait is the
// whole cost. A full left-right cycle took 2*N rendered frames, so at a dwell of
// 4 each eye refreshed at about 11 Hz. That is the slideshow.
//
// **Why a queue is correct here.** The engine's MT/RT double buffer *delays*
// work but does not *reorder* it: the camera built for frame N is rendered
// before the camera built for frame N+1. So the eye does not need to be
// searched for or waited on -- it only needs to be carried alongside the frame.
// The game thread pushes the eye it just built, the render thread pops one per
// finished frame, and identity is exact with no dwell at all. Each eye then
// refreshes every 2 frames instead of every 2*N.
//
// **Order is an assumption, so it is measured rather than trusted.** `Lag()` is
// pushes minus pops, which is the pipeline depth in frames. It should sit at a
// small constant. If it drifts, the 1:1 correspondence this relies on is not
// holding, and the caller can say so instead of quietly rendering the wrong eye
// into the wrong socket -- which looks like broken stereo rather than like a bug
// in a queue, and would be miserable to diagnose from inside a headset.
namespace eyehandoff {

RenderContractQueue gQueue;
std::atomic<unsigned long long> gPushed{0}, gPopped{0}, gStarved{0}, gDropped{0};
std::atomic<unsigned long long> gMatched{0}, gResyncs{0}, gSkipped{0}, gUnmatched{0};
// The head camera of the frame the render thread doubled (NoteDualFrameCamera),
// taken by the Consume at that frame's RT_EndFrame. Render thread only.
std::array<float,12> gRenderedCamera{};
bool gHaveRenderedCamera = false;
void Publish(const RenderContract& contract) {
    ++gPushed;
    if(!gQueue.Push(contract)) ++gDropped;
}
bool Consume(RenderContract& contract) {
    const bool identified = gHaveRenderedCamera;
    gHaveRenderedCamera = false;
    if (identified) {
        std::size_t skipped = 0;
        if (gQueue.PopMatching(gRenderedCamera, contract, skipped)) {
            ++gPopped; ++gMatched;
            if (skipped != 0) {
                gSkipped += skipped;
                const auto resyncs = ++gResyncs;
                if (resyncs <= 8 || resyncs % 100 == 0) {
                    lifecycle::Log("preyvr_camera_edit eye_handoff resync skipped=" + std::to_string(skipped) +
                                   " resyncs=" + std::to_string(resyncs) +
                                   " dropped=" + std::to_string(gDropped.load()));
                }
            }
            return true;
        }
        // No record carries this frame's camera: the position fallback below,
        // as before identities, and logged so a systematic miss shows.
        const auto unmatched = ++gUnmatched;
        if (unmatched <= 4 || unmatched % 1000 == 0) {
            lifecycle::Log("preyvr_camera_edit eye_handoff unmatched=" + std::to_string(unmatched) +
                           " queued=" + std::to_string(gQueue.Size()) +
                           " nearestDiff=" + std::to_string(gQueue.NearestCameraDifference(gRenderedCamera)) +
                           " camera=" + std::to_string(gRenderedCamera[3]) + "," +
                           std::to_string(gRenderedCamera[7]) + "," + std::to_string(gRenderedCamera[11]));
        }
    }
    if(!gQueue.Pop(contract)) {++gStarved;return false;}
    ++gPopped;return true;
}
void Reset() {
    gQueue.Reset();gPushed=0;gPopped=0;gStarved=0;gDropped=0;
    gMatched=0;gResyncs=0;gSkipped=0;gUnmatched=0;
}

} // namespace eyehandoff

// What the eye camera we just handed the renderer actually projects with.
//
// **This is the third quantity, and nothing outside the process can see it.**
// xr-tape's own CHECKS.md says so: it can observe the FOV the runtime located and
// the FOV we declared, but "the engine's own projection matrix never crosses the
// OpenXR boundary, so no API layer can see it". The mod is the only place all
// three coexist, so the comparison has to live here.
//
// Published from the one function every path uses to build an eye, so a route
// that skips it cannot silently skip the check with it.
std::atomic<float> gRenderedTanLeft{0.0f};
std::atomic<float> gRenderedTanRight{0.0f};
std::atomic<float> gRenderedTanUp{0.0f};
std::atomic<float> gRenderedTanDown{0.0f};
std::atomic<bool> gRenderedTangentsValid{false};

void PublishRenderedTangents(
    const std::array<std::uint8_t, cameraedit::kCameraSize>& edited)
{
    const auto view = stereoframe::TangentsFromCamera(
        std::span<const std::uint8_t>(edited.data(), engine::CameraLayout::size));
    if (!view) {
        gRenderedTangentsValid.store(false, std::memory_order_release);
        return;
    }
    gRenderedTanLeft.store(view->tanLeft, std::memory_order_relaxed);
    gRenderedTanRight.store(view->tanRight, std::memory_order_relaxed);
    gRenderedTanUp.store(view->tanUp, std::memory_order_relaxed);
    gRenderedTanDown.store(view->tanDown, std::memory_order_relaxed);
    gRenderedTangentsValid.store(true, std::memory_order_release);
}

// The six fields Prey's CCamera needs to carry an asymmetric frustum. Shared by
// both replacement paths so they cannot drift: a projection written one way in
// one branch and another way in the other is a bug that only appears in the
// branch nobody is testing.
void WriteEyeProjection(std::array<std::uint8_t, cameraedit::kCameraSize>& edited,
                        const stereoframe::EyeProjection& projection)
{
    const auto write = [&edited](std::size_t offsetBytes, float value) {
        std::memcpy(edited.data() + offsetBytes, &value, sizeof(float));
    };
    write(engine::CameraLayout::fov, projection.fov);
    write(engine::CameraLayout::projectionRatio, projection.projectionRatio);
    write(engine::CameraLayout::asymLeft, projection.asymmetry.left);
    write(engine::CameraLayout::asymRight, projection.asymmetry.right);
    write(engine::CameraLayout::asymBottom, projection.asymmetry.bottom);
    write(engine::CameraLayout::asymTop, projection.asymmetry.top);
}

// Publishes an eye WITH the camera it produced, so a downstream consumer can
// identify it from content rather than from a mutable global. H-022: a global is
// a frame ahead of the render thread and every tag it carries is individually
// valid. Called by whichever thread built the camera, immediately, so the record
// holds the exact bits the render view will carry.
void PublishBuiltEye(const stereo::Matrix34& m, const stereo::Matrix34& centre, int eye)
{
    BuiltEye record{};
    record.position[0] = m[3];  record.position[1] = m[7];  record.position[2] = m[11];
    record.right[0] = m[0];     record.right[1] = m[4];     record.right[2] = m[8];
    record.centre[0] = centre[3]; record.centre[1] = centre[7]; record.centre[2] = centre[11];
    record.eye = eye;
    record.serial = gBuiltEyeSerial.fetch_add(1, std::memory_order_relaxed) + 1;
    gBuiltEyes[record.serial % kBuiltEyeSlots] = record;
    gBuiltEyeCount.store(record.serial, std::memory_order_release);
}

void PublishFrameCentre(const stereo::Matrix34& m)
{
    const unsigned long long serial = gFrameCentreCount.load(std::memory_order_relaxed) + 1;
    FrameCentre& slot = gFrameCentres[serial % kFrameCentreSlots];
    slot.position[0] = m[3]; slot.position[1] = m[7]; slot.position[2] = m[11];
    slot.serial = serial;
    gFrameCentreCount.store(serial, std::memory_order_release);
}

bool BuildSyntheticEye(
    std::array<std::uint8_t, cameraedit::kCameraSize>& edited,
    int eye,
    float ipdMetres,
    float halfFovDegrees)
{
    const stereo::Matrix34 baseMatrix = stereo::ReadMatrix(edited);
    const Pose basePose = stereo::PoseFromMatrix(baseMatrix);

    // Half the IPD along the camera's own right axis.
    const float half = ipdMetres * 0.5f;
    const Vec3 offset{eye == 0 ? -half : half, 0.0f, 0.0f};
    const Pose eyePose = stereo::OffsetInLocalFrame(basePose, offset);
    if (!stereo::WriteMatrix(edited, stereo::MatrixFromPose(eyePose))) {
        return false;
    }

    // Translation only, and nothing else touched. The engine's fov, projection
    // ratio and asymmetry stay exactly as it built them, so the rendered frustum
    // is still the one TangentsFromCamera reads and declares.
    // **Ahead of the native check on purpose.** Native means "keep Prey's
    // frustum"; this means "use the headset's". Both cannot hold, and silently
    // preferring native would make the opt-in look enabled while doing nothing --
    // the exact failure shape as a Scaleform call to a function that is not there.
    if (gRuntimeFrustum.load(std::memory_order_acquire)) {
        float left = 0, right = 0, up = 0, down = 0;
        if (XrRequestedEyeFov(eye, &left, &right, &up, &down)) {
            stereoframe::EyeView view{};
            view.tanLeft = std::tan(left);
            view.tanRight = std::tan(right);
            view.tanDown = std::tan(down);
            view.tanUp = std::tan(up);
            const float nearPlane = stereoframe::NearPlaneOf(edited);
            const auto projection = stereoframe::ProjectionFromTangents(view, nearPlane);
            if (projection) {
                WriteEyeProjection(edited, *projection);
                PublishRenderedTangents(edited);
                gRuntimeFrustumUsed.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }
        // **Falls through to the existing behaviour rather than guessing.** No
        // located view yet, or a degenerate one, means we do not know the
        // headset's frustum -- and rendering an invented one would be submitted
        // as though it were the headset's own. Counted, so a mode that never
        // actually engaged cannot read as one that did.
        gRuntimeFrustumMissing.fetch_add(1, std::memory_order_relaxed);
    }

    if (gNativeProjection.load(std::memory_order_acquire)) {
        // Prey's own frustum, unchanged -- so this should match what the
        // submission path declares. Published anyway rather than assumed: an
        // assert that only runs on the path you suspect proves nothing about the
        // path you trust.
        PublishRenderedTangents(edited);
        return true;
    }

    // Mirrored asymmetry, the way a headset actually reports it: the outer edge
    // extends further than the inner one. Symmetric values here would leave the
    // shift path untested until a headset was attached, which is precisely when
    // an untested path is most expensive.
    //
    // Built by the pure layer rather than here so that the property A2b depends
    // on -- scale 1.0 giving both eyes the same frustum -- is covered by a test
    // instead of by inspection.
    const stereoframe::EyeView view = stereoframe::SyntheticEyeView(
        eye, halfFovDegrees, gAsymmetry.load(std::memory_order_acquire));

    const float nearPlane = stereoframe::NearPlaneOf(edited);
    const auto projection = stereoframe::ProjectionFromTangents(view, nearPlane);
    if (!projection) {
        return false;
    }

    WriteEyeProjection(edited, *projection);
    // The synthetic path REPLACED the projection. This is the case no external
    // tool can catch: the declaration still reports Prey's frustum while the
    // pixels came from this one. Measured 2026-09-05 as wall-eyed divergence,
    // horizontal stretch, and a sun that swung with head yaw -- one cause.
    PublishRenderedTangents(edited);
    return true;
}

void __fastcall RenderWithCameraEdit(void* system)
{
    const SystemRenderFn original = gOriginal.load(std::memory_order_acquire);
    RenderContract renderContract{};
    const auto renderOriginal = [&](void* target) {
        // One record per native render, including refusals and flat/menu frames.
        // A missing record would shift every subsequent image's metadata.
        if(gStereoIpd.load()>0.f) eyehandoff::Publish(renderContract);
        // After every drain and the camera edit, so it tests the camera Render
        // is about to: if this frame's occlusion prepare was skipped though the
        // world is about to be drawn, make it now, or the frame's cull batch is
        // orphaned and the culler stays a frame behind (CullPrepareGuard.h).
        if (target != nullptr) {
            CullPrepareBeforeRender(static_cast<const std::uint8_t*>(target) + engine::SystemLayout::viewCamera);
        }
        // The frame probe's main-thread bracket around the world render
        // (FrameProbe.h): whether the cull queue drained inside it, and the cull
        // handshake on each side (CullPrepareGuard.h), which shows an orphaned batch.
        std::uint32_t write = 0, read = 0;
        CullQueueIndices(write, read);
        const std::uint32_t handshake = CullThreadState();
        ProbeRecord(ProbeStep::renderEnter, handshake, 0, write, read);
        original(target);
        CullQueueIndices(write, read);
        const std::uint32_t handshakeAfter = CullThreadState();
        ProbeRecord(ProbeStep::renderExit, handshakeAfter, 0, write, read);
        CullPrepareAfterRender(handshake, handshakeAfter);
    };

    const bool stereoArmed = gStereoIpd.load(std::memory_order_acquire) > 0.0f;
    // **Queued input is drained here, not on the thread that produced it.**
    // `CSystem::Render` is the engine's main thread and, unlike the gameplay
    // camera callback, it still runs while a menu is up -- which is exactly the
    // case menu navigation needs. Deliberately ahead of the armed check below:
    // driving a menu must not require stereo or head tracking to be enabled
    // first, and the early return would otherwise skip it.
    NoteBodyFollowRender();
    DrainQueuedInput();
    DrainQueuedHudCalls();
    DrainUiPointer();
    // Same thread and same frame as the drain: the lane produces at most two
    // axis events per frame and the drain consumes one, so producing anywhere
    // else would race the queue it feeds.
    UpdateMoveLane();
    UpdateTurnAndFireLanes();
    // After every lane that posts this frame, so it checks the scheme this
    // frame's events left; on the main thread, as the scheme's listeners need.
    ServiceControllerPromptsFrame();
    ServiceBindingDump();
    // The wrench's skipped draws finish here, a frame on (WrenchDraw.h).
    ServiceWrenchDrawFrame();

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
                (eye == 0 ? -.5f : .5f) * renderIpd, 0, 0}});
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
                (eye==0 ? -.5f : .5f)*renderIpd,0,0}});
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

    // Publish the forward axis we just wrote, so the pass-camera probe can ask
    // whether the camera the engine culls with is this one.
    {
        const stereo::Matrix34 written = stereo::ReadMatrix(edited);
        gWrittenForwardX.store(written[1], std::memory_order_relaxed);
        gWrittenForwardY.store(written[5], std::memory_order_relaxed);
        gWrittenForwardZ.store(written[9], std::memory_order_relaxed);
        gHaveWrittenForward.store(true, std::memory_order_release);
    }

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

bool FindBuiltEyeByPosition(const float position[3], BuiltEye& out)
{
    const unsigned long long count = gBuiltEyeCount.load(std::memory_order_acquire);
    if (count == 0) { gBuiltEyeMisses.fetch_add(1, std::memory_order_relaxed); return false; }
    // Exact-ish: the render view holds a by-value copy of the camera we built,
    // so the bits should be identical. A small tolerance covers a copy that
    // passed through a float conversion without admitting a different eye --
    // the two eyes are an IPD apart, which is orders of magnitude wider.
    constexpr float kTolerance = 1e-4f;
    const unsigned long long newest = count;
    const unsigned long long oldest = newest > kBuiltEyeSlots ? newest - kBuiltEyeSlots + 1 : 1;
    for (unsigned long long serial = newest; serial >= oldest; --serial) {
        const BuiltEye& candidate = gBuiltEyes[serial % kBuiltEyeSlots];
        if (candidate.serial != serial) { continue; }
        if (std::fabs(candidate.position[0] - position[0]) <= kTolerance &&
            std::fabs(candidate.position[1] - position[1]) <= kTolerance &&
            std::fabs(candidate.position[2] - position[2]) <= kTolerance) {
            out = candidate;
            return true;
        }
        if (serial == 1) { break; }
    }
    gBuiltEyeMisses.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool IsDualFrameCentre(const float position[3])
{
    const unsigned long long newest = gFrameCentreCount.load(std::memory_order_acquire);
    // Same tolerance as FindBuiltEyeByPosition: a by-value copy of the camera.
    constexpr float kTolerance = 1e-4f;
    const unsigned long long oldest = newest > kFrameCentreSlots ? newest - kFrameCentreSlots + 1 : 1;
    for (unsigned long long serial = newest; serial >= oldest && serial != 0; --serial) {
        const FrameCentre& candidate = gFrameCentres[serial % kFrameCentreSlots];
        if (candidate.serial == serial &&
            std::fabs(candidate.position[0] - position[0]) <= kTolerance &&
            std::fabs(candidate.position[1] - position[1]) <= kTolerance &&
            std::fabs(candidate.position[2] - position[2]) <= kTolerance) {
            return true;
        }
    }
    return false;
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

unsigned long long BuiltEyeLookupMissCount()
{
    return gBuiltEyeMisses.load(std::memory_order_relaxed);
}

DWORD SetRuntimeFrustum(unsigned int enabled)
{
    const bool on = enabled != 0;
    gRuntimeFrustum.store(on, std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_camera result=0 detail=runtime_frustum enabled=" << (on ? "1" : "0")
         << " used=" << gRuntimeFrustumUsed.load(std::memory_order_relaxed)
         << " missing=" << gRuntimeFrustumMissing.load(std::memory_order_relaxed);
    lifecycle::Log(line.str());
    return 0;
}

unsigned long long RuntimeFrustumUsedCount()
{
    return gRuntimeFrustumUsed.load(std::memory_order_relaxed);
}
unsigned long long RuntimeFrustumMissingCount()
{
    return gRuntimeFrustumMissing.load(std::memory_order_relaxed);
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
         << " projectionPolicy=" << (gNativeProjection.load(std::memory_order_acquire)
                                         ? "prey_native" : "synthetic");
    lifecycle::Log(line.str());
    return static_cast<DWORD>(CameraEditStatus::armed);
}

DWORD SetStereoAsymmetry(float outerScale)
{
    if (!std::isfinite(outerScale) || outerScale < 1.0f || outerScale > 1.5f) {
        lifecycle::Log("preyvr_camera_edit result=refused detail=asymmetry_out_of_bounds");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }
    gAsymmetry.store(outerScale, std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=asymmetry outerScale=" << outerScale;
    lifecycle::Log(line.str());
    return static_cast<DWORD>(gStatus.load(std::memory_order_acquire));
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

bool ConsumeRenderedContract(RenderContract& out) { return eyehandoff::Consume(out); }

void NoteDualFrameCamera(const std::uint8_t* camera)
{
    std::memcpy(eyehandoff::gRenderedCamera.data(), camera, sizeof(eyehandoff::gRenderedCamera));
    eyehandoff::gHaveRenderedCamera = true;
}

void ResetEyeHandoff()
{
    eyehandoff::Reset();
}

unsigned long long EyeHandoffLag()
{
    return eyehandoff::gQueue.Size();
}

std::string EyeHandoffReport()
{
    std::ostringstream out;
    out << " handoff=" << eyehandoff::gQueue.Size() << "q/" << eyehandoff::gPushed.load() << "pushed/"
        << eyehandoff::gMatched.load() << "matched/" << eyehandoff::gResyncs.load() << "resyncs/"
        << eyehandoff::gSkipped.load() << "skipped/" << eyehandoff::gUnmatched.load() << "unmatched/"
        << eyehandoff::gStarved.load() << "starved/" << eyehandoff::gDropped.load() << "dropped";
    return out.str();
}

unsigned long long EyeHandoffStarvedCount()
{
    return eyehandoff::gStarved.load(std::memory_order_relaxed);
}

unsigned long long EyeHandoffDroppedCount()
{
    return eyehandoff::gDropped.load(std::memory_order_relaxed);
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

bool LastWrittenCameraForward(Vec3& out)
{
    if (!gHaveWrittenForward.load(std::memory_order_acquire)) {
        return false;
    }
    out.x = gWrittenForwardX.load(std::memory_order_relaxed);
    out.y = gWrittenForwardY.load(std::memory_order_relaxed);
    out.z = gWrittenForwardZ.load(std::memory_order_relaxed);
    return true;
}

DWORD SetNativeProjection(unsigned int enabled)
{
    const bool on = enabled != 0u;
    gNativeProjection.store(on, std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=native_projection enabled="
         << (on ? "1" : "0");
    lifecycle::Log(line.str());
    return static_cast<DWORD>(gStatus.load(std::memory_order_acquire));
}

unsigned int NativeProjectionEnabled()
{
    return gNativeProjection.load(std::memory_order_acquire) ? 1u : 0u;
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

bool RenderedEyeTangents(float& tanLeft, float& tanRight, float& tanUp, float& tanDown)
{
    if (!gRenderedTangentsValid.load(std::memory_order_acquire)) {
        return false;
    }
    tanLeft = gRenderedTanLeft.load(std::memory_order_relaxed);
    tanRight = gRenderedTanRight.load(std::memory_order_relaxed);
    tanUp = gRenderedTanUp.load(std::memory_order_relaxed);
    tanDown = gRenderedTanDown.load(std::memory_order_relaxed);
    return true;
}

unsigned long long CameraEditRestoreFailureCount()
{
    return gRestoreFailures.load(std::memory_order_acquire);
}

} // namespace preyvr::dll
