# Per-thread frame schedule: scope

Status: **phases 1-3 done 2026-10-24, worn OK on PreyVR-63 and PreyVR-64; phase 4 later.** This is item S1 of the [refactor audit](REFACTOR-AUDIT-2026-10-22.md) ("four schedulers are hidden in modules named for something else"). What was built, and where it differs from the plan below, is in [Done](#done-2026-10-24) at the end.

The tables below describe the code **before** the change, with its old line numbers.

## What runs where today

Four hooks tick other modules' per-frame work. Each one was named for its first job, and lanes were added to it one at a time.

| Seam | Thread | Hook (file:line) | What it runs, in order |
|---|---|---|---|
| **Gameplay** | main, game update | `ArkPlayer::UpdateCachedReticleViewPosAndDir`, AimTakeover.cpp:175 `UpdateCachedRayWithTakeover` | Builds the `GameplayPoseFrame` from the engine's cached ray (:175-292). `SolveTwoHandedFrame` (:294), **`UpdateSurfaceStop`** (:296, which writes `frame.surface`), publish (:298). Then `UpdatePsiMedkit`, `UpdatePsychoscopeGesture`, `UpdateBodyEquipment`, **`UpdateSwingLane` → `UpdateContactLane`** ("a strike tells contact what not to push"), `UpdateInteractionLane`, `UpdateCarryLane`, `UpdateGrenadeLane`, `UpdateArmsLane`, `UpdateWristLane`, `DebugOverlayGameFrame` (:299-310). Last, the aim override writes the cached ray. |
| **Main render** | main, `CSystem::Render` | CameraEditHook.cpp:390 `RenderWithCameraEdit` | `NoteBodyFollowRender` (a frame-order stamp), `DrainQueuedInput`, `DrainQueuedHudCalls`, `DrainUiPointer`, `UpdateMoveLane`, `UpdateTurnAndFireLanes`, `ServiceControllerPromptsFrame`, `ServiceBindingDump`, `ServiceWrenchDrawFrame` (:426-440). Then the stereo camera edit. **Around** Prey's own `Render` (:394-416): `eyehandoff::Publish`, `CullPrepareBeforeRender`, a frame-probe record, the original, another record, `CullPrepareAfterRender`. |
| **Render thread** | render, `RT_EndFrame` | FrameObserverHook.cpp:39 `ObserveEndRendererScene` | Frame-probe record, `ServiceConsoleQueue`, `ObserveXrBackbuffer`, `ServiceFrameCapture`, `ServiceXrFrame` (:43-64), then the original. |
| **Worker** | the command channel's own thread, 50 Hz | CommandChannel.cpp:1286 `PollThread` | `ServiceCullCameraWatch`, `ServiceFrameProbe`, `ServiceCullPrepareGuard`, `TickVrMode`, `ServiceVrOptions`, `ServiceFocusHold`, `ServiceControllerPrompts`. Every 10th pass (5 Hz) it reads, truncates and executes commands.txt. |

**The main thread's frame, as BodyFollow's frame-order probes record it:**
1. `ArkPlayer::Update` (BodyFollow.cpp:916): a queued snap turn lands, then the body follows.
2. `ArkPlayerCamera::UpdateView` (HeadTrackingHook.cpp:229): the snap-turn fallback, head tracking into the view, `NoteBodyFollowView`.
3. The arm IK callback (AnimIkTakeover.cpp:1879 `NoteBodyFollowIk`). Its place varies, and BodyFollow counts which.
4. The gameplay seam.
5. The main-render seam. `UpdateMoveLane` queues the next snap turn here; if no earlier seam took it, it applies late at :398.

Meanwhile the render thread runs its seam for the previous frame, and the worker runs on its own.

**Why this matters:**
- **The order is load-bearing but invisible.** Nothing names these as schedules.
- **Adding a lane means editing a hook that has nothing to do with it.** AimTakeover.cpp has 20 includes for that reason.
- **The aim replay test stubs 12 functions only to test the aim override** (`tools/re/vr_scheme_review/aim_review.cpp:59-70`): the 11 lane updates and `DebugOverlayGameFrame`.
- **VR start depends on the "command channel" thread.** `TickVrMode` runs there, so the later work to gate commands out of player builds would also gate VR start.
- **The per-tick verbs don't say their thread.** `ServiceXrFrame` runs on the render thread; `ServiceVrOptions` runs on the worker.

## Goal and non-goals

**Goal:** one file, `src/dll/FrameSchedule.{h,cpp}`, that lists for each seam what runs and in what order, with the comments that say why. Each hook calls one schedule function per phase. **Behaviour-identical:** the same calls in the same order, on the same threads, with the same early returns.

**Not in this batch:**
- **No lane interface or registry.** Every reviewer reached the same verdict: lanes differ in thread, signature and enable semantics, and static registration would hide the order again. The schedule is plain functions with explicit call lists.
- **No order changes.** If an order looks wrong, it gets a note in this doc and a separate change.
- **Not moving the gameplay frame producer or the aim override out of AimTakeover.** That is the Hands batch (audit N5).
- **Not the feature-owned seams.** Snap-turn delivery, the frame-order probes, and each stereo fix's own render hook stay where they are, because their placement is the point. The schedule header lists them, so the whole frame is described in one place.

## Design

```cpp
namespace preyvr::dll::schedule {
// Main thread, gameplay seam (AimTakeover), before the frame is published:
// stages that change it. Today: surface stop.
void PrepareGameplayFrame(GameplayPoseFrame& frame, bool allowed);
// The same seam, after the frame is published: the gameplay lanes, in order.
// Called only for a frame that was published, as now.
void TickGameplayLanes(const GameplayPoseFrame& frame, bool allowed, bool tracking);
// Main thread, CSystem::Render (CameraEditHook), before the camera edit:
// drains and the input lanes. Runs in menus and with stereo off, as now.
void TickMainRender();
// Around the engine's own Render: the cull guard and the frame probe.
struct EngineRenderBracket { std::uint32_t handshake = 0; };
EngineRenderBracket BeforeEngineRender(void* system);
void AfterEngineRender(const EngineRenderBracket&);
// Render thread, RT_EndFrame (FrameObserverHook), before the original.
void TickRenderThread(void* renderer, unsigned long long frame);
// Worker thread, 50 Hz (WorkerThread.cpp).
void TickWorker();
}
```

**What stays in each hook:**
- **AimTakeover:** the engine call, building the frame, `SolveTwoHandedFrame` and the publish (the aim module's own), and the aim override.
- **CameraEditHook:** the camera edit itself, `UpdateAimReticleForRender` (tied to which camera is live), and `eyehandoff::Publish`, which is the eye contract and stays first inside the bracket.
- **FrameObserverHook:** its frame counters, the milestone stamps and the original call.

**The worker:**
- A new `WorkerThread.cpp` owns the thread. `TickWorker()` runs the seven services and then, every 10th pass, `ServiceCommandFile()`. That is CommandChannel's read/truncate/execute, which stays in CommandChannel.
- Bootstrap starts the worker exactly where it starts the channel today (Bootstrap.cpp:224).
- **The log lines must not change:** `preyvr_channel result=0 detail=armed` and `preyvr_channel result=0 detail=started commands=...`. Start-PreyVR.ps1:227 waits for `detail=started` before it sends the startup batch.

## Phases

1. **Gameplay, main-render and render-thread lists move into FrameSchedule.cpp verbatim,** and the three hooks call them. The aim replay's 12 lane stubs shrink to the two gameplay schedule functions. About +120/−60 lines.
2. **The worker loop moves out of CommandChannel** into WorkerThread.cpp, with CommandChannel keeping `Execute` and `ServiceCommandFile`. The `command_channel` test is unaffected: it calls `Execute` and never starts the thread. About +60/−40.
3. **CameraEditHook is split into its three real jobs** (946 lines, mostly moves):
   - **MainRenderHook:** the `CSystem::Render` detour and its install.
   - **StereoEyeCamera:** the eye camera build and projection (`BuildSyntheticEye`, `BuildDualEyeCamera`, `WriteEyeProjection`, rendered tangents).
   - **EyeRecords:** the per-eye registry (`eyehandoff`, the BuiltEye and FrameCentre rings, the eye lock).
   - Thirteen files include CameraEditHook.h; it stays as an umbrella until they move to the narrower headers. The `PreyVR_*` export names stay.
4. **Optional, and later: per-entry timing** of each scheduled call (microseconds per lane, off by default) for the Optimization TODO. The lists are written so this is one wrapper per seam.

## Verification

- **Each phase:** build, 93/93 ctest, and the four VR scheme replays (aim_review exercises the gameplay seam). Compare the call order before and after against the table above.
- **Headset smoke (phases 1 and 2):**
  - the hands: IK, surface stop, pushing objects, wrench strikes;
  - carry and grenades, use and interaction, holsters, psychoscope, medkit, the wrist card;
  - menus and the options panel (drains), movement and snap turn, controller prompts, the wrench draw;
  - the HUD and XR frames, VR start (the worker), and alt-tab (FocusHold).
- **Headset smoke (phase 3):** stereo in both eyes, the near weapon, markers, loading screens and door transitions, which use the eye contract.

## Risks

| Risk | Guard |
|---|---|
| A call moves out of order | Move verbatim and review the diff against this doc's table. Nothing is reordered. |
| Early-return behaviour changes | Lanes still tick only after a publish. The main-render ticks stay ahead of the stereo-armed check, and the render-thread ticks stay ahead of the original call. |
| VR start timing, or the launcher handshake | The worker starts at the same point and logs the same lines. |
| A lane runs on another thread | Every call stays in the hook it runs from today. The lanes' `OnEngineMainThread` checks are unchanged. |
| Tests that compile these files | aim_review includes AimTakeover.cpp; its stubs are updated. command_channel `#include`s CommandChannel.cpp; WorkerThread's call to `ServiceCommandFile` resolves to it. |
| Phase 3 touches stereo | It gets its own package and smoke run. |

## Decisions

1. **Is CameraEditHook's split (phase 3) part of this batch?** **Yes** (your decision, 10-24), as its own package after phases 1-2.
2. **Per-entry timing (phase 4): now, or with the Optimization item?** **Later** (your decision, 10-24).

## Done 2026-10-24

Build passes, 93/93 ctest pass, no new warnings, and the DLL's 75 export names are unchanged. **Worn OK on PreyVR-63 and PreyVR-64 (10-24).** Not yet committed.

**PreyVR-63: phases 1 and 2.**
- `src/dll/FrameSchedule.{h,cpp}`:
  - the gameplay, main-render, engine-render bracket, render-thread and worker lists, moved verbatim with their order comments;
  - the header describes the main thread's whole frame, including the feature-owned seams it does not schedule.
- The hooks each call the schedule: AimTakeover calls `PrepareGameplayFrame` and `TickGameplayLanes`, CameraEditHook calls `TickMainRender` and the bracket, and FrameObserverHook calls `TickRenderThread`. AimTakeover loses 10 lane includes.
- `src/dll/WorkerThread.{h,cpp}` owns the thread. `StartCommandChannel` is now `StartWorkerThread`.
  - CommandChannel keeps `Execute`, plus `LogCommandChannelStarted` and `ServiceCommandFile` for the worker.
  - The `preyvr_channel` start, armed and failure lines are unchanged.
- aim_review stubs the two gameplay schedule calls instead of the 12 lane and overlay calls.
- The `command_channel` test now also pins the command-file step (comments skipped, truncated before running, one reply per line) and the start line the launcher waits for.

**PreyVR-64: phase 3, on top of 63.**
- `CameraEditHook.cpp` (946 lines) is cut at its declarations into three files. Every piece went to exactly one file, and a line-by-line comparison shows only the includes and the four cross-file calls below changed.
  - `MainRenderHook.{h,cpp}`: the `CSystem::Render` detour and its install, stereo arming and status, the eye lock and `BuildDualEyeCamera`.
  - `StereoEyeCamera.{h,cpp}`: `BuildSyntheticEye`, `WriteEyeProjection`, the projection policy (runtime frustum, native projection, asymmetry) and the rendered tangents.
  - `EyeRecords.{h,cpp}`: the built-eye and frame-centre rings, and the render-contract queue (`eyehandoff`).
- **The four cross-file calls:**
  - `eyehandoff::Publish` is now `PublishRenderContract`.
  - Three direct loads became the getters that make them: `CameraEditStatusValue()` (twice) and `NativeProjectionEnabled()`.
- `CameraEditHook.h` is removed rather than kept as an umbrella. All 12 includers moved to the narrow headers, and the comments that named the old file name the new ones.
- SetSyntheticStereo's doc comment, which sat above `BuiltEye`, now sits on it.

**Where this differs from the plan above:**
- **The aim takeover's enable flag is read twice per frame, not 11 times:** once for the prepare stage and once for all the lanes. A toggle from the command channel now reaches every lane in the same frame, rather than possibly mid-list.
- **The `endFrame` probe record stays in FrameObserverHook,** beside the frame counter it records, ahead of the schedule as before.
- **`BuildDualEyeCamera` and the eye lock are in MainRenderHook, not StereoEyeCamera/EyeRecords.** They need the hook's resolved `UpdateFrustum` and its stereo settings, and keeping them there needed no new accessors.
- **The S7 fold of the two ring searches into one template is not done.** This was a move; it is still open.

**Headset smoke, per package:** the checklists under Verification above. PreyVR-63 needs the phase 1-2 list; PreyVR-64 adds the phase 3 list (stereo in both eyes, the near weapon, markers, loading screens, door transitions).
