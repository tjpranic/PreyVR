// The `PreyVR_*` exports: the DLL's C entry points for DllSmokeHost, research
// tools and the live procedures in docs/. Each one forwards to the module that
// owns the state; none holds any of its own.
#include "app/Bootstrap.h"
#include "engine/EngineAccess.h"

#include "engine/hooks/MainRenderHook.h"
#include "render/camera/StereoEyeCamera.h"
#include "render/camera/EyeRecords.h"
#include "xr/XrSessionHost.h"
#include "engine/console/ConsoleBridgeWin32.h"
#include "diagnostics/capture/FrameCaptureWin32.h"

#include "engine/hooks/FrameObserverHook.h"
#include "base/Logger.h"
#include "aim/AimTakeover.h"
#include "diagnostics/probes/RuntimeSnapshotWin32.h"
#include "engine/EngineMap.h"
#include "render/camera/StereoFrame.h"

namespace preyvr::dll {

// On-demand Capture B. Refuses unless the gate has already verified this process
// (smoke status 2), so it can never read a process we have not identified.
// Read-only: no COM call, no write, and a failure is reported rather than thrown.
DWORD CaptureRenderViewsToLog()
{
    if (SmokeStatus() != 2) {
        lifecycle::Log("preyvr_renderview refused reason=host_not_verified");
        return 1;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        lifecycle::Log("preyvr_renderview refused reason=PreyDll.dll_not_loaded");
        return 1;
    }
    const auto capture = CaptureRenderViewsNow(reinterpret_cast<std::uintptr_t>(preyDll));
    snapshot::Report(
        capture,
        [](std::string_view line, void*) { lifecycle::Log(line); },
        nullptr);
    return capture.complete ? 0 : 2;
}

} // namespace preyvr::dll

namespace {

// The `...Ptr` exports take their one argument in the pointer slot, so a thread
// start (one pointer) can call them; this is that argument as a number.
unsigned int PtrArg(void* value)
{
    return static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(value));
}

} // namespace

extern "C" __declspec(dllexport) DWORD PreyVR_GetSmokeStatus()
{
    return preyvr::dll::SmokeStatus();
}

extern "C" __declspec(dllexport) DWORD PreyVR_SetFrameObserverEnabled(DWORD enabled)
{
    return preyvr::dll::SetFrameObserverEnabled(enabled != 0);
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetFrameObserverStatus()
{
    return preyvr::dll::FrameObserverStatus();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetObservedFrameCount()
{
    return preyvr::dll::ObservedFrameCount();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetOpenXRPreflightStatus()
{
    return preyvr::dll::OpenXRPreflightStatus();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetModulePinStatus()
{
    return preyvr::dll::ModulePinStatus();
}

// Capture B, on demand. Returns 0 complete, 2 partial, 1 refused. Results go to
// the smoke log as `preyvr_renderview` lines. Never runs on load.
extern "C" __declspec(dllexport) DWORD PreyVR_CaptureRenderViews()
{
    return preyvr::dll::CaptureRenderViewsToLog();
}

// --- Test harness -----------------------------------------------------------
//
// These make a rendered result and a deterministic scene reachable from an
// automation script, which is what turns "does the view move?" from something a
// human squints at into something with a number attached. Both are inert until
// the frame observer is enabled, because both are serviced from inside it.

// Arms a one-shot backbuffer dump of the next observed frame. `tag` is written
// into the dump header so an A/B pair is distinguishable without trusting
// filenames. Returns 0 ok, 1 refused (one already pending), 2 unavailable,
// 3 failed.
extern "C" __declspec(dllexport) DWORD PreyVR_RequestFrameCapture(DWORD tag)
{
    return preyvr::dll::RequestFrameCapture(static_cast<std::uint32_t>(tag));
}

// Queues one allowlisted console command. Anything not on the list is refused
// here rather than passed to the engine. Returns 0 ok, 1 denied, 2 unavailable,
// 3 signature mismatch, 4 busy, 5 too long.
extern "C" __declspec(dllexport) DWORD PreyVR_QueueConsoleCommand(const char* command)
{
    return preyvr::dll::QueueConsoleCommand(command);
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetCameraEditAppliedCount()
{
    return preyvr::dll::CameraEditAppliedCount();
}

// Non-zero here means an edit was applied but the camera did not come back
// byte-identical. The hook disarms itself on the first such frame.
extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetCameraEditRestoreFailureCount()
{
    return preyvr::dll::CameraEditRestoreFailureCount();
}

// Alternating-eye stereo with a synthetic IPD and FOV, no double-render and no
// headset. With the scene frozen via t_Scale 0, consecutive frames differ only
// by the eye, so they form a genuine stereo pair that New-StereoView can judge.
// Captures are stamped with the eye index automatically. Pass ipd 0 to disarm.
extern "C" __declspec(dllexport) DWORD PreyVR_SetSyntheticStereo(float ipdMetres, float halfFovDegrees)
{
    return preyvr::dll::SetSyntheticStereo(ipdMetres, halfFovDegrees);
}

// Scales the synthetic per-eye frustum asymmetry. 1.0 makes both eyes symmetric
// so the only difference is the eye offset; 1.1 is the default and exercises the
// asymmetry path. Measuring both at once is what made the first stereo pair
// unjudgeable.
extern "C" __declspec(dllexport) DWORD PreyVR_SetStereoAsymmetry(float outerScale)
{
    return preyvr::dll::SetStereoAsymmetry(outerScale);
}

// Locks synthetic stereo to one eye: 0 left, 1 right, anything else alternates.
// Replaces per-frame eye tagging, which cannot work across the engine's game and
// render threads - they are offset by its MT/RT double buffer, so a tag written
// by one is not reliably read by the other for the same frame. Hold an eye, let a
// few frames pass, capture; then hold the other.
extern "C" __declspec(dllexport) DWORD PreyVR_SetStereoEyeLock(unsigned int eye)
{
    return preyvr::dll::SetStereoEyeLock(eye);
}

// Rung 3: build each eye by translation alone and keep Prey's own projection,
// so the frustum we declare to OpenXR is the frustum the game actually drew.
// Pointer-taking variant, per F-009 - Frida's NativeFunction aborts calls into
// this DLL, so anything that must be driven from a probe needs a thread-callable
// shape.
extern "C" __declspec(dllexport) DWORD PreyVR_SetNativeProjectionPtr(void* enabled)
{
    return preyvr::dll::SetNativeProjection(PtrArg(enabled));
}

// Which eye the main thread last built a camera for, or -1 (LastBuiltEye).
extern "C" __declspec(dllexport) int PreyVR_GetLastRenderedEye()
{
    return preyvr::dll::LastBuiltEye();
}

// Reads Prey's live view camera and returns the frustum PreyVR must DECLARE to
// OpenXR, through the shipped code path rather than a re-derivation.
//
// **Why this exists as an export at all.** The live values were first checked by
// reading the camera from a Frida script and doing the arithmetic there. That
// confirms the fields are readable, but it tests a re-implementation rather than
// `TangentsFromCamera` itself -- and a re-derivation that agrees with itself is
// exactly the self-referential check this project has already been caught by
// once, in the asymmetry test. This calls the real function so the numbers that
// come back are the ones the mod would actually submit.
//
// Pure read: nothing is armed, nothing is written, no hook is required.
//
// Pointer-taking so it matches LPTHREAD_START_ROUTINE and can be driven on a real
// thread, per F-009 -- Frida's NativeFunction aborts calls into this DLL.
struct PreyVRDeclaredFov {
    unsigned int valid;          // 0 if the camera could not be read or was refused
    float tanLeft, tanRight, tanDown, tanUp;
    float angleLeft, angleRight, angleDown, angleUp;   // radians
    // The source fields, echoed so the derivation can be audited rather than
    // trusted -- and so a convention error shows up as a wrong input, not just a
    // wrong answer.
    float fov, projectionRatio, nearPlane;
};

extern "C" __declspec(dllexport) DWORD PreyVR_ReadDeclaredFovPtr(PreyVRDeclaredFov* out)
{
    if (out == nullptr) {
        return 0;
    }
    *out = PreyVRDeclaredFov{};

    const std::uint8_t* const camera = preyvr::dll::LiveViewCamera();
    if (camera == nullptr) {
        return 0;
    }
    const auto span = std::span<const std::uint8_t>(camera, preyvr::engine::CameraLayout::size);

    const auto readFloat = [camera](std::size_t offset) {
        float value = 0.0f;
        std::memcpy(&value, camera + offset, sizeof(value));
        return value;
    };
    out->fov = readFloat(preyvr::engine::CameraLayout::fov);
    out->projectionRatio = readFloat(preyvr::engine::CameraLayout::projectionRatio);
    out->nearPlane = readFloat(preyvr::engine::CameraLayout::edgeNearLeftTop + sizeof(float));

    const auto view = preyvr::stereoframe::TangentsFromCamera(span);
    if (!view) {
        return 0;
    }
    const auto angles = preyvr::stereoframe::AnglesFromTangents(*view);
    out->tanLeft = view->tanLeft;
    out->tanRight = view->tanRight;
    out->tanDown = view->tanDown;
    out->tanUp = view->tanUp;
    out->angleLeft = angles.angleLeft;
    out->angleRight = angles.angleRight;
    out->angleDown = angles.angleDown;
    out->angleUp = angles.angleUp;
    out->valid = 1;
    return 1;
}

// ---------------------------------------------------------------------------
// Pointer-argument variants, for automation
// ---------------------------------------------------------------------------
// args[0] = IPD in metres, args[1] = half-FOV in degrees.
extern "C" __declspec(dllexport) DWORD PreyVR_SetSyntheticStereoPtr(const float* args)
{
    if (args == nullptr) {
        return static_cast<DWORD>(preyvr::dll::CameraEditStatus::failed);
    }
    return preyvr::dll::SetSyntheticStereo(args[0], args[1]);
}

// --- Hurdle 2: an OpenXR session hosted inside Prey ------------------------
extern "C" __declspec(dllexport) DWORD PreyVR_StartXrSession()
{
    return preyvr::dll::StartXrSession();
}

extern "C" __declspec(dllexport) DWORD PreyVR_StopXrSession()
{
    return preyvr::dll::StopXrSession();
}

// Arms stereo submission: a real image per eye, and Prey's own frustum declared
// over them rather than the runtime's. Pointer-taking per F-009.
extern "C" __declspec(dllexport) DWORD PreyVR_SetXrStereoSubmissionPtr(void* enabled)
{
    return preyvr::dll::SetXrStereoSubmission(PtrArg(enabled));
}

// Asks for the sRGB swapchain format -- the double-encoded-gamma fix. Must be
// set before PreyVR_StartXrSession. Pointer-taking per F-009.
extern "C" __declspec(dllexport) DWORD PreyVR_SetXrPreferSrgbFormatPtr(void* enabled)
{
    return preyvr::dll::SetXrPreferSrgbFormat(PtrArg(enabled));
}

// Eye-handoff diagnostics. The lag is pushes minus pops -- the pipeline depth in
// frames -- and is how the ordering assumption is checked rather than trusted: it
// should sit at a small constant, and drift means eye identity is no longer safe.
extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetEyeHandoffLag()
{
    return preyvr::dll::EyeHandoffLag();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetXrSubmittedFrameCount()
{
    return preyvr::dll::XrSubmittedFrameCount();
}
