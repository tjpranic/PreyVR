#include "StereoEyeCamera.h"
#include "MainRenderHook.h"

#include "XrSessionHost.h"

#include "Logger.h"
#include "preyvr/EngineMap.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/StereoFrame.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <sstream>

namespace preyvr::dll {
namespace {

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

} // namespace

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
    const Vec3 offset{stereo::EyeSign(eye) * half, 0.0f, 0.0f};
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
    return CameraEditStatusValue();
}

DWORD SetNativeProjection(unsigned int enabled)
{
    const bool on = enabled != 0u;
    gNativeProjection.store(on, std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=native_projection enabled="
         << (on ? "1" : "0");
    lifecycle::Log(line.str());
    return CameraEditStatusValue();
}

unsigned int NativeProjectionEnabled()
{
    return gNativeProjection.load(std::memory_order_acquire) ? 1u : 0u;
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

} // namespace preyvr::dll
