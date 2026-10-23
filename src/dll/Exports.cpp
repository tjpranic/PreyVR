// The `PreyVR_*` exports: the DLL's C entry points for the live harness
// (tools/live/preyvr-harness.js), DllSmokeHost and research tools. Each one
// forwards to the module that owns the state; none holds any of its own.
#include "Bootstrap.h"
#include "EngineAccess.h"

#include "MainRenderHook.h"
#include "StereoEyeCamera.h"
#include "EyeRecords.h"
#include "XrSessionHost.h"
#include "ConsoleBridgeWin32.h"
#include "FrameCaptureWin32.h"

#include "FrameObserverHook.h"
#include "Logger.h"
#include "AimTakeover.h"
#include "HeadTrackingHook.h"
#include "ReticleFollow.h"
#include "XrInput.h"
#include "HotkeyBridge.h"
#include "IkTargetProbe.h"
#include "InputPathProbe.h"
#include "CommandChannel.h"
#include "NearViewStereo.h"
#include "WeaponAttachment.h"
#include "DebugWatch.h"
#include "RuntimeSnapshotWin32.h"
#include "preyvr/EngineMap.h"
#include "preyvr/StereoFrame.h"

#include <atomic>
#include <sstream>

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

extern "C" __declspec(dllexport) DWORD PreyVR_GetLastFrameCaptureResult()
{
    return preyvr::dll::LastFrameCaptureResult();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetCompletedFrameCaptureCount()
{
    return preyvr::dll::CompletedFrameCaptureCount();
}

// Queues one allowlisted console command. Anything not on the list is refused
// here rather than passed to the engine. Returns 0 ok, 1 denied, 2 unavailable,
// 3 signature mismatch, 4 busy, 5 too long.
extern "C" __declspec(dllexport) DWORD PreyVR_QueueConsoleCommand(const char* command)
{
    return preyvr::dll::QueueConsoleCommand(command);
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetLastConsoleResult()
{
    return preyvr::dll::LastConsoleBridgeResult();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetSubmittedConsoleCommandCount()
{
    return preyvr::dll::SubmittedConsoleCommandCount();
}

// The CSystem::Render hook (MainRenderHook.h): 0 unavailable, 1 ready, 2 armed
// (stereo is editing the camera each frame), 3 failed.
extern "C" __declspec(dllexport) DWORD PreyVR_GetCameraEditStatus()
{
    return preyvr::dll::CameraEditStatusValue();
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
extern "C" __declspec(dllexport) DWORD PreyVR_SetNativeProjection(unsigned int enabled)
{
    return preyvr::dll::SetNativeProjection(enabled);
}

// Pointer-taking variant, per F-009 - Frida's NativeFunction aborts calls into
// this DLL, so anything that must be driven from a probe needs a thread-callable
// shape.
extern "C" __declspec(dllexport) DWORD PreyVR_SetNativeProjectionPtr(void* enabled)
{
    return preyvr::dll::SetNativeProjection(PtrArg(enabled));
}

// Which eye the most recent render used, or -1.
extern "C" __declspec(dllexport) int PreyVR_GetLastRenderedEye()
{
    return preyvr::dll::LastRenderedEye();
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

// The view seam: ArkPlayerCamera::UpdateView. Observe first -- the SViewParams
// layout is from a neighbouring engine generation and is confirmed by reading the
// FOV back, not assumed.
extern "C" __declspec(dllexport) DWORD PreyVR_SetViewHookObservingPtr(void* enabled)
{
    return preyvr::dll::SetViewHookObserving(PtrArg(enabled));
}

extern "C" __declspec(dllexport) DWORD PreyVR_SetViewHookApplyingPtr(void* enabled)
{
    return preyvr::dll::SetViewHookApplying(PtrArg(enabled));
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetViewHookObservedCount()
{
    return preyvr::dll::ViewHookObservedCount();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetViewHookAppliedCount()
{
    return preyvr::dll::ViewHookAppliedCount();
}

// M1 head tracking.
extern "C" __declspec(dllexport) DWORD PreyVR_RecenterHeadTracking()
{
    return preyvr::dll::RecenterHeadTracking();
}

// Eye-handoff diagnostics. The lag is pushes minus pops -- the pipeline depth in
// frames -- and is how the ordering assumption is checked rather than trusted: it
// should sit at a small constant, and drift means eye identity is no longer safe.
extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetEyeHandoffLag()
{
    return preyvr::dll::EyeHandoffLag();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetEyeHandoffStarvedCount()
{
    return preyvr::dll::EyeHandoffStarvedCount();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetEyeHandoffDroppedCount()
{
    return preyvr::dll::EyeHandoffDroppedCount();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetXrSubmittedFrameCount()
{
    return preyvr::dll::XrSubmittedFrameCount();
}

// ---------------------------------------------------------------------------
// H-005 producer hunt. Hardware breakpoints, so nothing in the game is patched.
//
// The sequence, all from the console:
//   1. PreyVR_ArmIkCapture 64        -- opens the log gate, execute-watches R-077
//   2. play for a few seconds with a weapon drawn
//   3. PreyVR_DisarmIkCapture        -- closes the gate again
//   4. read the rows, pick a target address
//   5. PreyVR_ArmIkProducerWatch <address>
//   6. move, so the animator recomputes it
//   7. read the producers -- those RVAs are the answer
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport) DWORD PreyVR_ArmIkCapturePtr(void* maxCaptures)
{
    return preyvr::dll::ArmIkCapture(PtrArg(maxCaptures));
}

extern "C" __declspec(dllexport) DWORD PreyVR_DisarmIkCapture()
{
    return preyvr::dll::DisarmIkCapture();
}

// The gate byte before we touched it, and what it reads now. Equal after a disarm
// is the proof the engine was put back; a capture that found nothing while the
// gate reads 0 means the gate never opened, which is a different problem from a
// site that never executed.
extern "C" __declspec(dllexport) DWORD PreyVR_GetIkLogGateOriginal()
{
    return preyvr::dll::IkLogGateOriginalValue();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetIkLogGateCurrent()
{
    return preyvr::dll::IkLogGateCurrentValue();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetIkTargetRowCount()
{
    return preyvr::dll::IkTargetRowCount();
}

// One folded IK target. `index` in, everything else out; millimetres and
// thousandths so it crosses as integers.
struct PreyVRIkTargetReadout {
    unsigned int index;
    unsigned int valid;
    unsigned long long targetAddress;
    unsigned long long limbId;
    unsigned long long owner;
    unsigned long long skeleton;
    unsigned int hits;
    int posX, posY, posZ;
    unsigned int quatMagnitude;
    unsigned int readable;
};

extern "C" __declspec(dllexport) DWORD PreyVR_ReadIkTargetPtr(PreyVRIkTargetReadout* io)
{
    if (io == nullptr) {
        return 0;
    }
    preyvr::dll::IkTargetRow row{};
    if (!preyvr::dll::ReadIkTargetRow(io->index, row)) {
        io->valid = 0;
        return 0;
    }
    io->valid = 1;
    io->targetAddress = row.targetAddress;
    io->limbId = row.limbId;
    io->owner = row.owner;
    io->skeleton = row.skeleton;
    io->hits = row.hits;
    io->posX = row.posX;
    io->posY = row.posY;
    io->posZ = row.posZ;
    io->quatMagnitude = row.quatMagnitude;
    io->readable = row.readable;
    return 1;
}

extern "C" __declspec(dllexport) DWORD PreyVR_ArmIkProducerWatchPtr(void* targetAddress)
{
    return preyvr::dll::ArmIkProducerWatch(
        reinterpret_cast<unsigned long long>(targetAddress), 16u);
}

extern "C" __declspec(dllexport) DWORD PreyVR_DisarmIkProducerWatch()
{
    return preyvr::dll::DisarmIkProducerWatch();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetIkProducerCount()
{
    return preyvr::dll::IkProducerCount();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetIkProducerRvaPtr(void* index)
{
    return preyvr::dll::IkProducerRva(PtrArg(index));
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetIkProducerHitsPtr(void* index)
{
    return preyvr::dll::IkProducerHits(PtrArg(index));
}

// 1 means the reported RVA is the instruction *after* the store. Published rather
// than left as lore, so the offset is not subtracted twice or not at all.
extern "C" __declspec(dllexport) DWORD PreyVR_GetIkProducerRipIsAfterWrite()
{
    return preyvr::dll::IkProducerRipIsAfterWrite();
}

// Watch-engine health. Threads armed vs missed decides whether an empty result is
// evidence of absence; foreign traps say something else owns the debug registers,
// which in practice means a debugger is attached and the results are not ours.
extern "C" __declspec(dllexport) DWORD PreyVR_GetWatchArmedMask()
{
    return preyvr::dll::WatchArmedMask();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetWatchArmedThreads()
{
    return preyvr::dll::WatchArmedThreadCount();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetWatchMissedThreads()
{
    return preyvr::dll::WatchMissedThreadCount();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetWatchForeignTraps()
{
    return preyvr::dll::WatchForeignTrapCount();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetWatchHitCountPtr(void* slot)
{
    return preyvr::dll::WatchHitCount(PtrArg(slot));
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetWatchCaptureCount()
{
    return preyvr::dll::WatchCaptureCount();
}

// ---------------------------------------------------------------------------
// Positional 6DoF. Separate switch from rotation, because they fail differently
// and one switch would make an in-headset A/B unable to say which half is wrong.
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport) DWORD PreyVR_SetViewPositionApplyingPtr(void* enabled)
{
    return preyvr::dll::SetViewPositionApplying(PtrArg(enabled));
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetViewPositionApplied()
{
    return preyvr::dll::ViewPositionAppliedCount();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetViewPositionRefused()
{
    return preyvr::dll::ViewPositionRefusedCount();
}

// Head offset from the engine eye point, in millimetres. The number that says
// positional tracking is doing something: it moves as you lean, and sits near
// zero when you do not.
extern "C" __declspec(dllexport) DWORD PreyVR_GetViewPositionOffsetMm()
{
    return preyvr::dll::ViewPositionOffsetMillimetres();
}

// ---------------------------------------------------------------------------
// Native input path (H-006 / R-070). Read-only: resolves and validates, calls
// nothing.
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport) DWORD PreyVR_ResolveInputPath()
{
    return preyvr::dll::ResolveInputPath();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetInputPathPointer()
{
    return preyvr::dll::InputPathInputPointer();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetInputPathVtable()
{
    return preyvr::dll::InputPathVtable();
}

// **Check this before trusting the RVA below.** 0 means the setter/getter
// alignment pair was not found and the published index is the header-derived
// guess, not a measurement.
extern "C" __declspec(dllexport) DWORD PreyVR_GetInputPathAlignmentConfirmed()
{
    return preyvr::dll::InputPathAlignmentConfirmed();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetInputPathAlignmentSlot()
{
    return preyvr::dll::InputPathAlignmentSlot();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetInputPathPostInputEventRva()
{
    return preyvr::dll::InputPathPostInputEventRva();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetInputPathSlotRvaPtr(void* slot)
{
    return preyvr::dll::InputPathSlotRva(PtrArg(slot));
}

// ---------------------------------------------------------------------------
// Ordered watch captures.
//
// `FoldProducers` aggregates by RVA, which answers "who writes this" and throws
// away the thing that answers "who writes it LAST" -- the order. The ring already
// records a monotonic sequence per trap, so ordering costs nothing but exposure.
//
// `stackTop` is included deliberately: two of the sites found on 2026-09-05 are a
// dword-wise memberwise copy, which is a shared helper. RE-009's caveat is that a
// helper's own RVA names nothing, and its caller is the real owner.
// ---------------------------------------------------------------------------

namespace {

bool ReadCaptureAt(void* index, preyvr::dll::WatchCapture& out)
{
    return preyvr::dll::ReadWatchCapture(PtrArg(index), out);
}

unsigned long long ToPreyRva(unsigned long long address)
{
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return 0;
    }
    const auto base = reinterpret_cast<unsigned long long>(preyDll);
    return address > base ? address - base : 0ull;
}

} // namespace

// The trap's instruction pointer as a PreyDll RVA. Remember the write watch
// reports AFTER the store retires, so the storing instruction precedes this.
extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetWatchCaptureRipRvaPtr(void* index)
{
    preyvr::dll::WatchCapture capture{};
    return ReadCaptureAt(index, capture) ? ToPreyRva(capture.rip) : 0;
}

// `[rsp]` at the trap, as a PreyDll RVA. A hint, not a proven caller -- it is only
// a return address if the trap landed at a point where one is on top.
extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetWatchCaptureStackTopRvaPtr(void* index)
{
    preyvr::dll::WatchCapture capture{};
    return ReadCaptureAt(index, capture) ? ToPreyRva(capture.stackTop) : 0;
}

// Monotonic across every trap, so captures can be put back in the order they
// happened regardless of which slot or thread produced them.
extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetWatchCaptureSequencePtr(void* index)
{
    preyvr::dll::WatchCapture capture{};
    return ReadCaptureAt(index, capture) ? capture.sequence : 0;
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetWatchCaptureThreadIdPtr(void* index)
{
    preyvr::dll::WatchCapture capture{};
    return ReadCaptureAt(index, capture) ? capture.threadId : 0;
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetWatchCaptureSlotPtr(void* index)
{
    preyvr::dll::WatchCapture capture{};
    return ReadCaptureAt(index, capture) ? capture.slot : 0;
}

// ---------------------------------------------------------------------------
// The hand takeover (R-082). Applies a bounded offset immediately after
// PreyDll+0x87BC36 writes the IK target position -- the site that writes LAST in
// every cycle, and therefore the only place a write survives to be consumed.
// ---------------------------------------------------------------------------

namespace {
// args[0] Vec3 address, args[1] match RVA, args[2..4] offset xyz as floats
// bit-cast into the array, args[5] seconds. One pointer, per F-009.
struct ApplyOffsetArgs {
    unsigned long long vec3Address;
    unsigned long long matchRva;
    float dx, dy, dz;
    unsigned int seconds;
    unsigned int slot;
};
} // namespace

extern "C" __declspec(dllexport) DWORD PreyVR_ArmIkApplyOffsetPtr(const ApplyOffsetArgs* args)
{
    if (args == nullptr) {
        return 1;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return 6;
    }
    // The RVA is resolved here against the live module, so the caller never
    // passes an absolute address that could be stale from another session.
    const auto matchRip =
        reinterpret_cast<unsigned long long>(preyDll) + args->matchRva;
    return preyvr::dll::ArmApplyOffset(args->slot, args->vec3Address, matchRip,
                                       args->dx, args->dy, args->dz, args->seconds);
}

extern "C" __declspec(dllexport) DWORD PreyVR_DisarmIkApplyOffsetPtr(void* slot)
{
    return preyvr::dll::DisarmApplyOffset(PtrArg(slot));
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetIkApplyAppliedCount()
{
    return preyvr::dll::ApplyOffsetAppliedCount();
}

// Traps that were not the matching site. **High skipped with zero applied means
// the match address is wrong**, which is a different fault from not being armed.
extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetIkApplySkippedCount()
{
    return preyvr::dll::ApplyOffsetSkippedCount();
}
