# PreyVR refactoring audit — 2026-10-22 (branch `custom`, HEAD 8d05696, clean tree)

This was read-only: five parallel reviews, each covering one slice, with nothing edited or run in the game.
1. Physical gameplay lanes
2. Body, arms, aim and locomotion
3. Rendering, XR session and stereo
4. Options, commands, UI and bootstrap wiring
5. Cross-cutting structure, build, tests, tools and docs

**[V]** marks a claim spot-checked by hand after the reviews came back. Line numbers are at 8d05696. Line counts are reviewer estimates. Counts that came from grep, not a link map, say so.

**Baseline:** 93/93 ctest pass at 8d05696. **Correction, 10-23:** the "no warnings" reading came from a build that recompiled nothing. A clean rebuild shows pre-existing warnings, listed under T1 in section 3.

**Status:**
- **2026-10-23, the easy-win batch is done.** See the **Done 10-23** notes in sections 2 and 3.
  - Build passes and 93/93 ctest pass, with no new warnings.
  - The DLL's 159 export names are unchanged; they were compared before and after the split.
  - **Committed by you as `baa6878`** ("Refactoring").
- **2026-10-23, the cut batch is done** (your decisions 1, 2, 3, 5 and 9; see section 4).
  - Removed: the legacy hand rig, WeaponAttachment's mount writer, 84 unused exports, AimRayProbe, PassCameraProbe's comparison (its hook is now PassCreationHook), the getters and setters left with no caller, and 19 test-only pure functions with their tests.
  - The version is now `0.6.0-preview`.
  - Size: 78 files, about 4,270 lines removed net. Build passes and 93/93 ctest pass (no target emptied), with no new warnings. The export table is exactly the kept 75.
  - **Worn OK on PreyVR-61, and committed by you as `ba6599c`** ("Dead code removal").
- **2026-10-23, your decisions on section 4:**
  - Item 6, torso settings: option (a), one owner (BodyFollow). It goes in the Hands batch.
  - Item 7, the grenade queue check: skipped. **You plan to redo throwing**, so grenade and carry-throw refactors are deprioritised.
  - Item 4, the reachable diagnostics: all four cut.
- **2026-10-23, the offline batch is done: the four cuts and the T1 helpers.** See the **Done 10-23 (offline batch)** notes in section 3.
  - Build passes and 93/93 ctest pass; `render_frame_table` went, `command_channel` is new.
  - The only warnings are the six pre-existing ones. The DLL's 75 export names are unchanged.
  - Tracked files: 71 changed, +505/−2,292. New files: PhysicalLane.h, VerifiedSetting.h, DllTestSupport.h and CommandChannelTests.cpp, about 790 lines, 630 of them tests.
  - **Worn OK on PreyVR-62, and committed by you as `f9cc865`** ("More refactoring").
- **2026-10-24, the FrameSchedule batch is done (S1).** It follows [the scope](FRAME-SCHEDULE-SCOPE-2026-10-24.md), whose "Done" section lists what moved and where it differs from the plan.
  - Phases 1-2, the tick lists and the worker thread: **PreyVR-63**.
  - Phase 3, the CameraEditHook split into MainRenderHook, StereoEyeCamera and EyeRecords: **PreyVR-64**, built on top of 63.
  - Build passes, 93/93 ctest pass, no new warnings, and the 75 export names are unchanged.
  - **Worn OK on PreyVR-63 and PreyVR-64 (10-24), and committed by you as `24af274`** ("Frame scheduling refactor").
- **2026-10-24, the Hands batch is built** ([its scope](HANDS-SCOPE-2026-10-24.md), with your three decisions: the splits in, the rename now, Carry and Grenade converted too).
  - PreyVR-65 holds HandWorld, `IkAnchor`, the `reticleOrigin` rename, `DriveHand`'s structs and the torso owner. PreyVR-66 adds the AimTakeover, BodyFollow and AnimIk splits.
  - Build passes, 94/94 ctest pass, no new warnings, and the 75 export names are unchanged.
  - **Worn OK on PreyVR-65 and PreyVR-66 (10-24), and committed by you as `84d6c19`** ("Hand code refactor").
  - The one issue seen, the fade when leaning over a desk, is the known 30 cm residual limit (fix 3 in [TURN-LEAN-ARMS-2026-10-19.md](TURN-LEAN-ARMS-2026-10-19.md)). It predates the batch, and you left it for later.
- **2026-10-24, the offline cleanup batch (batch A) is built** ([its scope](OFFLINE-CLEANUP-SCOPE-2026-10-24.md)): the XrSessionHost extractions, the options wiring test, the moves and renames, the ring search, the `log` helper, D3/D4 and the verifier runner.
  - Build passes, 97/97 ctest pass (`vr_options_wiring`, `position_ring` and `re_verifiers` are new), no new warnings, and the 75 export names are unchanged.
  - **Worn OK on PreyVR-67 (10-24), and committed by you as `3e1bb1a`** ("Big refactor").
- **2026-10-24, the XR batch is built** ([its scope](XR-SCOPE-2026-10-24.md), with your decisions: the rename, phase 3 in, one package, the other renderer reads too).
  - B7's pieces (XrPose.h, LayerSwapchain's shared pieces, RendererAccess.h), the overlay sections as functions, and the split of XrSessionHost.cpp into seven files and a private header.
  - Build passes, 97/97 ctest pass, the four `strncpy` warnings are gone, and the 75 export names are unchanged.
  - **Worn OK on PreyVR-68 (10-24), and committed by you as `e74a389`** ("XR refactoring").
- **2026-10-24, the UI batch's phases 1 and 2 are built** ([its scope](UI-SCOPE-2026-10-24.md)).
  - The B9 verb table: 165 rows with Audience tags, and the counting and minimum arguments unchanged.
  - `QueueConsoleRetrying`, and a doc check of the table against TUNING-COMMANDS.md.
  - B10's HudLayer helpers (`CopyStencil`, `RedirectInto`, `EnsureDepthCopy` reused, `NativeTargets` in CaptureFlash).
  - Build passes, 97/97 ctest pass, no new warnings, and the 75 export names are unchanged.
  - Packaged as PreyVR-69, and committed by you as `51b0f87` ("UI and HUD logic refactor").
  - **Phase 3 is built** (your decisions: yes to 1 to 4 and 6; the lasers split stays out).
    - One counting rule, `missing_arguments`, and the `arm.*` settings as rows.
    - `CommandArgs.h`: decimal parsing shared with the lanes.
    - The Spec's fallbacks, the replies' `result=0`, `near.fov` keeping follow, 87 for too few values or for one with no default, and `report` logging only itself.
    - The pinned `command_channel` oddities in section 5 are fixed, apart from the carry and grenade calls before install.
    - Build passes, 97/97 ctest pass, and the 75 export names are unchanged.
    - Packaged as PreyVR-70, **worn OK (10-24)**, and committed by you as `e8a30af` ("Command handling refactor").
    - That session's one crash was the engine's, not this batch's ([CRASHES-2026-10-24.md](CRASHES-2026-10-24.md)).

**How this relates to [the 10-17 audit](DUPLICATION-AUDIT-2026-10-17.md):**
- The older audit is still the reference for its items. Its "Constraints that shape every fix" and section F ("don't merge") still apply in full.
- This audit does three things: updates the status of the open items, adds what about 16k lines of feature work since 1d8bae9 introduced, and adds a structural view. The older audit was about duplication only.

**Tiers:**
- **T1:** behaviour-identical and verifiable offline by ctest and the build.
- **T2:** low risk, but it touches hooks, engine calls or XR submission, so one smoke headset run is needed per batch.
- **T3:** changes behaviour.

---

## 1. Structural findings: the abstraction problems

These are the issues behind most of the duplication. Fixing them first makes the per-item cleanups smaller.

### S1. Four schedulers are hidden inside modules named for something else [V]
Every lane is wired into these by hand, and their order matters. Nothing in the code names them as schedules.

| Thread | Where it lives | What it runs |
|---|---|---|
| Gameplay camera callback | `AimTakeover.cpp:291-310`, inside the aim detour | Publishes `GameplayPoseFrame`, then runs SurfaceStop → PsiMedkit → Psychoscope → BodyEquipment → Swing → Contact → Interaction → Carry → Grenade → Arms → Wrist → DebugOverlay. The order is load-bearing ("Before contact: a strike tells contact what not to push"). |
| Main thread (`CSystem::Render`) | `CameraEditHook.cpp:432-449` | BodyFollow note, input drain, HUD-call drain, UI pointer, move/turn/fire lanes, controller prompts, binding dump, wrench draw. Also the cull/frame-probe bracket around the original call (403-426). |
| Render thread | `FrameObserverHook.cpp:56-70` | Console queue, capture, `ServiceXrFrame`, aim-ray sample |
| Worker | `CommandChannel.cpp:1433-1441`, the command channel's poll thread | Cull watch, frame probe, cull prepare guard, `TickVrMode`, `ServiceVrOptions`, FocusHold, ControllerPrompts. VR start depends on the "command channel" thread. |

**Symptoms:**
- `tools/re/vr_scheme_review/aim_review.cpp:60-70` has to stub all 11 lane updates to test the aim override.
- There are 29 includers of `AimTakeover.h` [V] and 17 forward declarations of `GameplayPoseFrame` [V].
- The per-tick verbs (`Update*`, `Service*`, `Drain*`, `Tick*`, `Note*`, `Sample*`) don't say which thread they run on. `ServiceXrFrame` runs on the render thread, while `ServiceVrOptions` runs on the worker.

**Fix:**
- `GameplayFrame.h` (T1): the struct, `SurfaceOffset`, `TryGet`/`Ensure`, out of AimTakeover.h. **Done 10-23;** see section 3.
- Then `FrameSchedule.cpp` (T2): four explicit ordered tick lists, in the same order as now, keeping the comments that explain the order.
- CommandChannel becomes one 5 Hz entry in the worker list.
- **Done 10-24, worn OK on PreyVR-63/64:** `FrameSchedule.{h,cpp}` and `WorkerThread.{h,cpp}`. aim_review stubs the two gameplay schedule calls instead of the 12 lane and overlay calls.
- **Not** a polymorphic `ILane`/Feature registry. Lanes differ in thread, hook ownership and enable semantics, and the VR start sequence has documented order and conditional chains (`VrMode.cpp:61, 133-140, 191-195, 227-231`). Every reviewer reached the same verdict.

### S2. No single "where is my hand, head or body in the world this frame" abstraction
- **Controller in the world:** 21 `animik::ControllerWorldFromHead` calls in 11 files. The 10-17 audit counted about 9.
  - The sites are AimTakeover:112-114/351/381, AnimIkTakeover:1132/1229/1275, CarryLane:428/433, ContactLane:832, DebugOverlay:699, GrenadeLane:482, ShotRay:199/207, SurfaceStopLane:316, WeaponAttachment:133/135 and PsiTargeting.h:21.
  - The drawn hand, "controller + `SurfaceOffset`", is pasted 6 times: ShotRay:200/208, GrenadeLane:484, ContactLane:838, AnimIk:1137/1278.
  - The sites use four anchor policies, and nothing at the call site shows which one is in use:
    - cameraCentre: Shot, Contact, Surface, Carry, Grenade, Psi, Wrist
    - cameraCentre, else nativeEye: AimTakeover:110, DebugOverlay:698/724
    - the `ik.anchor` A/B: AnimIk
    - nativeEye: AimTakeover:351/381, WeaponAttachment:133, ReticleFollow:490
- **Tracking↔engine mapping, re-derived inline:** 8 copies, each bit-identical by construction.
  - AnimIk:677-681 `WorldToOpenXr` is `stereo::EngineToTracking`.
  - WristLane:107-111 and AnimIk `XrMap::ToModel` (786-787) are `ControllerWorldFromHead(..).position`.
  - DebugOverlay:126 is `carry::TrackingToWorld`, which is also the core of RoomScale.cpp:22.
  - DebugOverlay:122, InteractionLane:622-626 and RoomScale.cpp:28-29 are the same inverse.
  - `WorldFromReference` is copied between PhysicalInteractions.cpp:35-38 and SurfaceStopLane.cpp:225-229.
  - `YawQuaternion` is re-inlined in 4 places.
- **The "is this frame placeable" gate** is written per site with different subsets of `cameraCentreValid && headYawUsable && isfinite(yaw) && !(referenceGeneration & 1)` [V]:
  - AimTakeover:291, CarryLane:425, InteractionLane:569, PhysicalInteractions:31, PsiMedkit:74, SurfaceStopLane:433, XrSessionHost:2728
  - plus UiPanel.cpp:272, Haptics.cpp:28/37, VrMode:277, XrSessionHost:936/1374/2610.
- **Fix:**
  1. `PlaySpaceMap{yaw, anchor, head}` in StereoCamera.h, with `ToWorld`, `PointToWorld`, `PointToXr`, `DirToWorld`, `DirToXr` and `WorldFromReference`. **T1.**
  2. Pure `Recentering(gen)`, `RecenteredSince(gen, seen)` and `Placeable(frame)`, with tests. **T1.**
  3. `HandWorld(frame, hand, PoseKind, Anchor, maxAgeNs, Drawn, Pose&)` with an explicit `enum class Anchor`. Each site passes its current policy, so the camera-vs-eye choice (the IK crosstalk A/B) becomes visible. Mostly **T2**.
  - Section F still applies: the yaw extractors, the wraps and the torso estimators stay separate.

### S3. Two generations of lane conventions, each with its scaffolding copied by hand
- **Older lanes (Carry, Grenade, Interaction):**
  - `g` globals, `Ensure*Lane` installers and inline prologue arrays.
  - `DWORD` setters that refuse, and their own number parsers.
  - **No offline lane test at all.**
  - They still triplicate the B6 engine wrappers: `ReadBytes`, `VCall`, `CallGetPlayer`, `CallGetEntity`, `CallEntityId`, `CallPhysics`, `CallWorldTm`, `CallBounds`, the name getters and `CopyName`. The copies are at CarryLane:235-340/386-404, GrenadeLane:269-350/459-473 and InteractionLane:138-233.
- **Newer lanes (Contact, Swing, SurfaceStop, WrenchDraw, WrenchSound):**
  - A `Natives` struct with a `gResolve` seam, FNV contracts in `*Native.h`, and `void` setters that clamp.
  - A settings-generation log line, a 10 s report, and full lane tests that `#include` the `.cpp`.
  - Contact, Swing and SurfaceStop each carry their own copy of:
    - `EngineThread()` [V], at Contact:75, Swing:82 and Surface:53
    - `LogBudget()` [V]
    - the thread-local re-entry guard
    - `MaybeReport` (10 s throttle)
    - the gate chain off→fault→tracking→menu→head_out→camera→pose
    - QPC microsecond stats
    - the "fault unless NOT_READY/INVALID_THREAD_ID" rule, which also appears 5 times in SceneQuery
    - the trusted-dt window 1/240..1/20
    - `AngleBetween`
    - the broadphase plus per-entity box test (Contact `Query` :82-127 and Swing :202-255)
    - `VtableOf` [V], at Contact:301 and Surface:105
- **The engine-main-thread check** is written 10 times. Besides the three above: ArmsLane:190, BodyEquipment:41/138, PsiMedkit:50/288, Psychoscope:20, SceneQuery:27.
- **Fix:**
  - Header-only `src/dll/PhysicalLane.h`: `ReentryGuard`, `GateReason`, `LineBudget`, `ReportThrottle`, `MicrosStats`, `TransientEngineError`, `TrustedDt`, `YawDelta`. About −190 LOC, **T1**.
  - Move `Broadphase`/`CollideBox` into PhysicsCalls.h.
  - `OnEngineMainThread()` in InputPost.h.
  - Each lane keeps its own log strings and its file-local names, because the tests call them.
- **Finish B6** with `EngineEntity.h` and the EngineMap constants listed under B6 in section 2. **T2.** It is best done after giving Carry, Grenade and Interaction a `Natives`/`gResolve` seam and an offline test; adding the seam is T2, and after that every change to those lanes is T1.

### S4. Options and control surfaces: no single definition, and no player/dev split
- **Options:** there are 66 settings. All of them go through the Spec-driven `Wiring` table for the panel, saving and applying (VrOptionsRuntime.cpp:152-228), but every one is also hand-wired at its lane.
  - Typically the default is written in 3–6 places and the range in 3–5.
  - Setters follow three conventions: refuse with 1, refuse with 87, or clamp silently.
  - VrMode.cpp:231-253 restates 6 Spec defaults as literals.
  - Five settings (psychoscope, holsters, wrist, wrist size, belt hints) store their state inside VrOptionsRuntime itself instead of in their lane.
  - Example, `hud_layer`: Spec default 1 (VrOptions.cpp:59), the lane starts false (HudLayer.cpp:41), VrMode sets it to 1 (:250), and the verb falls back to **0** (CommandChannel.cpp:809).
- **Control surfaces:**
  - 66 ini options, about 226–238 verbs, 11 `PREYVR_*` environment variables, 9 Ctrl+Alt research hotkeys, 159 exports [V], and 675 `Log(` calls through 45 prefix wrappers.
  - **No code separates player options from dev knobs, and there is no `_DEBUG`/`NDEBUG` gating anywhere** [V]. Only the docs and the inconsistent `dbg.` prefix do: `dbg.lasers` is a player option.
- **The player-facing Aim Lasers are DebugOverlay layers** (DebugOverlay.cpp:575, DebugOverlayScene.h:35). Gating instrumentation today would remove the lasers.
- **Fix, which also does the groundwork for the "tuning parameter cleanup", "instrumentation cleanup" and "options menu" TODO items:**
  - Move `specs[]` into a constexpr header with `Default/Valid/Clamp(id)`. Lanes initialise from `options::Default(id)`, checked by static_assert, and validate through the Spec.
  - Add Spec flags: `early`, `vrStart` (replacing VrMode's literals) and `dev`.
  - Add range tests that walk `Wiring`. **T1.**
  - Then a B9 verb table with an `Audience{player, tuning, research, legacy}` tag (T2).
  - Then a `PREYVR_DEV` option exposed as `inline constexpr bool kDev`, with `lifecycle::Diag()`. Use `if constexpr` so both configurations compile, and add one ctest configuration built with it OFF.
  - Split the lasers out of DebugOverlay first.

### S5. God files
| File | Lines | What is in it |
|---|---|---|
| XrSessionHost.cpp | 3,440 | 12 responsibilities. `ServiceXrFrame` alone is about 960 lines. |
| AnimIkTakeover.cpp | 2,807 | Hook, rig reading and identification, `DriveHand` (430 lines, 14 parameters, 9 of them optional pointers), arms, reports. Anchor selection is repeated 5 times; all 5 are equivalent to `ArmAnchor`. |
| Bootstrap.cpp | 1,636 | Lines 1-335 are lifecycle. The other ~80% is 159 `extern "C"` forwarders [V]. |
| CommandChannel.cpp | 1,505 | 179 `verb==` branches through 13 dispatchers. Five sub-dispatchers exist only because of MSVC C1061, and their names mislead: `ExecutePose` holds use/ik/body/dbg/aim/markers verbs. **No test** [V]. |
| BodyFollow.cpp | 1,305 | Hex-string gates, torso follow, room-scale move, living-body physics calls, head-in-wall check (6 lanes use it as a gate), frame-order diagnostics. |
| CameraEditHook.cpp | 976 | The main-thread scheduler (S1), eye-camera construction and projection modes, and the eye registry (BuiltEye and FrameCentre rings, handoff queue). |

**Proposed splits.** All of them are moves; do each after the B7 and S3 cleanups so the moved code is already smaller.
- **XrSessionHost:**
  - `XrSessionHostInternal.h`: `Host`, the mutex, status atomics, `Log`
  - `XrSessionLifecycle.cpp`, about 700 lines
  - `XrEyePair.cpp`, about 430
  - `XrMenuPanel.cpp`, about 420
  - `XrOverlayLayers.cpp`, about 470, falling to about 250 after B7
  - `XrHoldFrames.cpp`, about 400
  - `XrSessionReports.cpp`, about 200
  - `ServiceXrFrame` stays as an orchestrator of about 300 lines.
  - **Constraint:** the top of `ServiceXrFrame` (1919-1937) must still consume the contract, `gDualFirstHeld` and every capture before its first early return. Every new file must keep the `preyvr_xr_session ` log prefix.
- **CameraEditHook:**
  - `MainFrameHook`, the seam, which becomes one tick list in FrameSchedule
  - `StereoEyeCamera`
  - `EyeRecords`, the single per-eye registry; see S7
  - **Done 10-24** (PreyVR-64) as `MainRenderHook`, `StereoEyeCamera` and `EyeRecords`. CameraEditHook.h is gone, and its 12 includers use the narrow headers.
- **AnimIkTakeover:** hook/setters, rig, `DriveHand` (with a `HandDriveIo` struct), arms, and reports.
- **BodyFollow:** BodyFollow, RoomScaleMove, `phys::Living` (into PhysicsCalls) and BodyFollowDiagnostics.
- **AimTakeover:** `GameplayFrame` (producer), the gameplay tick list in FrameSchedule, and the ray override.
- **Bootstrap:** exports move to `Exports.cpp`. **T1**; check it with `dumpbin /exports` before and after.

### S6. Misleading names
- **Module names:**
  - **CameraEditHook** is the main-render seam and scheduler; its own header (17-20) says the name is historical. **Done 10-24:** it is now MainRenderHook, with the schedule in FrameSchedule.
  - **AimTakeover** is the gameplay-frame producer and scheduler.
  - **InputPathProbe** is the production input resolver (InputPost.cpp:53-66 needs it).
  - **InventorySwapchain**, `InventorySwapchainApi` and `kInventoryMinWidth` serve 11 layers. `DestroyUnheldSwapchain` is declared in its header but defined in XrSessionHost.
  - **BindingDump** hosts the production `PreyWheelOpen()` (MoveLane.cpp:418).
  - **HudBridge** hosts the global gate `HudGameplayInputAllowed`, which 16 files use.
  - **HotkeyBridge** is keyboard input, not a bridge. **DualOverlay** is a stereo fix, and its name clashes with DebugOverlay.
  - **MarkerStereo**'s API is named `MarkerDepth*`.
  - **PhysicalInteractions** now only publishes the weapon box. `PhysicalOverlayState`/`DebugOverlayPhysical` are used only by Swing.
- **Lane vs Takeover:** the two words mean the same thing. AnimIkTakeover.h calls itself a lane, and "Lane" is applied to 9 of about 20 modules that all have the same `Update(frame, allowed)` shape.
- **Six pure/dll pairs share a header name:** BodyEquipment, BodyFollow, PhysicalInteractions, SceneQuery, ShotRay, SlotFeedback. BodyFollow.cpp and ShotRay.cpp exist in both `src/common` and `src/dll`.
- **Field and identifier names:**
  - `GameplayPoseFrame::nativeEye` is the cached reticle origin; the header itself says "historical name" (AimTakeover.h:44). Rename it `reticleOrigin`, about 25 uses.
  - `LastRenderedEye` is really the last eye *built*; keep the `PreyVR_GetLastRenderedEye` export, because the harness calls it. `SyntheticStereoIpdMetres` is the live stereo IPD.
  - AnimIk numbers hands 0 = right, but `Hand{left=0,right=1}` (XrInput.h:16) is the opposite. There are about 10 inline flips; add `enum class RigSide`. AnimIk's `HandRig` struct also collides with the HandRigTakeover module.
  - The same thread is called "game thread" (Carry, Grenade, Interaction), "input-drain thread" (Contact, Swing, Surface, SceneQuery) and "Prey's main thread" (WrenchDraw, WrenchSound).
  - `SlotIs` takes a slot index, guarded, in BodyFollow:257, but a byte offset, unguarded, in ContactLane:129.
  - `sn` means `swing::native` in SwingLane but `wrenchsound::native` in WrenchSound.
  - `SetBodyFollow`, `SetBodyFollowActive` and `SetBodyFollowMode` need clearer names. `SetCarryHand` and `SetGrenadeHand` are on/off switches.
- **Build names:** CMakeLists:10 says `0.3.1-static-ik-integration`, but the tags are at v0.5.1-preview. `PREYVR_BUILD_SMOKE_DLL` (:7, :92) still describes an "inert smoke DLL".
- **Rules for any rename:**
  - Rename C++ identifiers only, and keep export names, command verbs and `detail=` strings.
  - An unknown ini key refuses the whole file (VrOptions.cpp:199-200), so renaming an ini key needs a migration entry.
  - Keep `using` aliases where tests use the old name, for example `InventorySwapchain`.

### S7. Per-eye state and IPD are spread out
- **Eye identity is tracked in 7 places:**
  1. `gLastEye`
  2. the BuiltEye ring
  3. the FrameCentre ring
  4. the handoff queue's `contract.eye`
  5. DualSceneStereo's `gStep`, exposed only as a bool
  6. HudLayer's thread-local `stereoEye`
  7. the swap mapping `gSwapEyes ? 1-eye : eye`, written 4 times
- The content-addressed lookups are correct by design (H-022), and the per-fix edit/restore steps should stay separate (section F). What is worth sharing:
  - `EyeRecords`, which also folds the identical ring searches `FindBuiltEyeByPosition`/`IsDualFrameCentre` (CameraEditHook:674-716) into one tested template;
    - **Partly done 10-24:** EyeRecords exists, moved verbatim.
    - **Done 10-24 (batch A):** the two searches share `ring::FindNewest`.
  - `int SubmissionEye()` in place of the bool. **Dropped 10-24:** its one caller needs only the bool.
  - `constexpr stereo::EyeSign(eye)`. The `eye == 0 ? -half : half` convention appears 7 times [V].
- **The IPD lives in 5 stores, fed from 2 sources.** The world, near pass and markers use the IPD latched at VR start; inventory parallax uses the live runtime IPD each frame. Changing the IPD mid-session would desynchronise them (latent). Moving to one source is T3.

---

## 2. Status of the 10-17 audit's open items

| Item | Status at 8d05696 |
|---|---|
| B6 entity/physics wrappers | **Done 10-23:** `phys::kTypeSlot/kStatusSlot/kActionSlot` are now aliases of `cn::`; the queue-table note moved from CarryLane into PhysicsCalls.h; Grenade's comments point at PhysicsCalls.h and say it skips the queue check. **Otherwise, about ⅓ done.** `PhysicsCalls.h` (`QueryDynamics`, `SetVelocity`, `ArticulatedHosted`, `VelocityQueueReady`) is used by Carry, Contact and Surface. Grenade keeps its own `CallDynamics`/`SetVelocityAction`/`CallSetVelocity` (:353-385). Its comments at :352/:369 point at the wrong file, and it never checks `VelocityQueueReady`; see section 5. **New copies:** `VtableOf` ×2, `TypeOf` ×3, `Virtual<Fn>` ×3, `SlotIs`/`SlotTarget` ×3, `EntityId`, and two more hand-built `pe_status_dynamics` buffers in ContactLane (:245-252, :318-322). **Constants:** the entity-system RVA 0x224DA38 is written 5 times [V]; player entity +0x38 three times plus `SceneQueryNative`; 0x7777 three times; the carry offsets twice; the physical-world RVA twice. The prologue arrays at Carry:51-70 and Grenade:65-100 have no verifier; moving them to EngineMap landmarks would give them one. |
| B7 XrSessionHost layer boilerplate | **Open, and grown.** `InventorySwapchainApi{…}` ×8. HUD upload = `UploadPanelCapture`; the log lines match character for character. Quad fill: 8 hand-written copies besides `FillPanelLayer`. Pose↔`XrPosef` ×18 (was 17). `GetImmediateContext` ×13 (was 11), with 29 manual `Release()` calls. sRGB picker ×5; `xrswapchain::SelectFormat` is a confirmed drop-in. GDI DIB ×3. The 8192 size rule ×4. **New:** renderer→swapchain→`GetBuffer` ×7 and renderer→device ×7, which become `RendererDevice`/`AcquireBackBuffer` in EngineAccess.h. Slot and fade are the same "painted quad"; pointer and guide are the same "static quad". |
| B8 options in 4–6 places | **Open, accurate.** See S4. No B8 test was added. |
| B9 CommandChannel dispatch | **Open, and grown:** 179 branches (was 168), 102 of them trivial setters (was 96). The `arg` lambda has 4 copies plus about 6 inline `ParseInt` calls; the lanes parse 3 more ways. The console retry is still written twice (1062-1066, 1409-1413). The A9 fallbacks all equal the Spec default except three, which a table must carry as explicit overrides: `hud.layer`→0, `move.headrelative`→2 (which the lane refuses) and `move.snap`→its current value. The `log [0\|1]` subcommand parse is copied 3 times (625-668). |
| B10 HudLayer repeats | **Open, and grown:** the target redirect is now 6 copies (CaptureText and RedrawDisplay are new). `EnsureTexture`'s depth half duplicates `EnsureDepthCopy`. |
| B11 pose composition | **Open, and grown;** see S2. GetFiringPosition is still declared 3 times (Grenade:85/119/424, ShotRay:45/99/175, WeaponAttachment:86/102/151 with a literal RVA). ReticleFollow's projection is live, while `aim::ProjectToScreen` is used only by tests, so this is no longer a duplicate. MoveLane: `StickAxis` is used now; the three hand-rolled latches (:328-333, :441-443, :487-491) remain. The cinematic gate and CanEquip/Equip are at 7 sites. dt scaffolding: BodyEquipment, Psychoscope, PsiMedkit and VrOptionsRuntime could share an `XrStep`. |
| B12 leftovers | **Done 10-23:** the wrist-card width (`WristCardWidthMetres()`), and InputPost's raw event and disarm (`BuildRawEvent`, `DisarmAfterFault`). **Checked and kept 10-23:** the pointer-on-surface visual; the three copies gate differently (freshness, surface reference) and share about two lines. **Open:** HudBridge two-float/one-float callers. |
| D3 fixtures | Open: `BuildSyntheticSupportedImage` ×2, WARP device ×3 files, `use::UseButton` tests still in HandCarryTests:312-520. |
| D4 launcher-test preamble | Open (×3). |
| E4 launchers, E5 probes, E7 minor | All open. tools/README mentions no `tools/re` verifier. |

---

## 3. Work by tier

### T1: offline, behaviour-identical

**Dead code needing no decision:**
- **No callers:**
  - **Done 10-23:** `QueryPhysicalSegment` (SceneQuery) and `RenderMatrixIsNear` (RenderFrame) removed, with RenderFrame.h's doc block for it.
  - **Done 10-23:** ContactLane's `Hosted` alias (its one use calls `phys::ArticulatedHosted`), and WrenchSound's `kPlayerId` (now `sn::PlayerEntityId`).
  - **Correction 10-23:** HandCarry.cpp's `InteractionUse.h` include *is* used (`use::PointerEnd`, :248). Kept.
  - **Skipped:** `LogOnce` (XrSessionHost) has one caller and no defect; inlining it gains nothing.
- **Done 10-23, re-declared:** PhysicsCalls.h's slot constants are now aliases of `contact::native::TypeSlot/StatusSlot/ActionSlot`.
- **Open, simplify:** `QuerySegment` can forward to `QueryObjectsBetween`. That one is T2: SceneQuery has no lane test, and its per-function `busy` flag would become shared.
- **Kept, redundant check:** SurfaceStopLane:433's `referenceGeneration&1` can never be true, because AimTakeover:291 returns first. It costs nothing and still holds if AimTakeover changes.
- **Done 10-23, orphaned or stale comments:**
  - XrSessionHost: `DeclaredFovFromLiveCamera`'s, `gRuntimePeriodUs`'s and `SubmitStereoPair`'s docs moved back above their code; the DepthSubmission leftover is deleted; the UiGuide note now names the VR option instead of PreyVR.json.
  - CameraEditHook: an orphaned doc for an experiment cut on 10-18 is deleted.
  - CarryLane: the queue-table note moved into PhysicsCalls.h, next to the tables.
  - GrenadeLane: both comments now point at PhysicsCalls.h.
  - AimTakeover.h and AnimIk: the anchor comments now say the camera centre is the default anchor.
  - StereoFrame.h: "double-render experiment" now says CameraEditHook builds each eye with it.
  - CommandChannel.h: the 30-verb legacy list is replaced by a pointer to docs/TUNING-COMMANDS.md.
  - CommandChannel.cpp: `window.hold` no longer claims the launcher minimises the window, and `aim.reticleconverge` now describes the RETICLE FALLBACK DISTANCE option.
- **Stale docs:**
  - **Done 10-23:** src/README.md, include/preyvr/README.md and tests/README.md rewritten to match the tree.
  - **Open:** ARCHITECTURE.md (08-27, pre-implementation). Do it last (section 3, "Last").
- **Build names:**
  - **Done 10-23:** the `PREYVR_BUILD_SMOKE_DLL` option text and the CMake comment no longer call PreyVR.dll an "inert smoke DLL".
  - **Done 10-23:** the version string is `0.6.0-preview` (section 4, item 9).
- **Pre-existing warnings, found 10-23.** None of them come from this batch's edits. A clean rebuild reports six:
  - C4996 `strncpy` ×4 in XrInput.cpp (161, 162, 201, 202);
  - C4389 signed/unsigned in `std::` code from VrOptionsTests;
  - C4459 in InventorySwapchainTests, whose global `r` shadows a WRL local.

  The XrInput ones are on the OpenXR action setup, so fixing them is T2.

**Small shared helpers (each header-only, so DLL-including tests keep linking):**
- **Done 10-23 (offline batch): `src/dll/PhysicalLane.h`** (namespace `lane`). Contact, Swing and SurfaceStop now share:
  - `ReentryGuard`, `TakeLine` (the line budget; limits stay 40/40/20), `ReportThrottle`, and `GateReason` (same order, same `why` strings);
  - `Stopwatch`/`AddMicros` and `TrustedDt`.
  - PhysicsCalls.h gains `VtableOf`, `Broadphase` and `CollideBox`; SceneQuery.h gains `TransientEngineError` (4 copies in SceneQuery.cpp).
  - The lanes lose about 100 lines; with the new header's comments, the total is +48.
  - **Kept separate:** the `SafeQuery` wrappers, Contact's fault handling (it also ends its contacts), the two `TypeOf`s, the proxy velocity spaces, and `CollideSphere`.
- **Done 10-23:** `OnEngineMainThread()` (InputPost.h) replaces all 10 copies. The three lane `EngineThread()` functions are gone. It is equivalent at every site, including the ones that skipped the `== 0` test, because `GetCurrentThreadId` is never 0.
- **Done 10-23 (offline batch): play-space mapping** as free functions beside `stereo::EngineToTracking`, the style StereoCamera.h already used:
  - `TrackingToEngine` (points), `TrackingDirToEngine`/`EngineDirToTracking` (directions) and `TrackingOriginInWorld`.
  - They replace the copies in AnimIk (`WorldToOpenXr`, `XrMap::ToModel`), WristLane, DebugOverlay (two wrappers, 7 sites), InteractionLane, RoomScale, PhysicalInteractions and SurfaceStopLane (`WorldFromReference`), and `carry::TrackingToWorld`.
  - A test checks `TrackingToEngine` is bit-identical to `ControllerWorldFromHead`'s position.
- **Done 10-23 (offline batch): recenter checks.** `stereo::Recentering(gen)` and `stereo::ReferenceCurrent(sample, live)` replace about 25 hand-written `& 1` / `% 2` checks in 13 files.
  - No combined `Placeable(frame)` was added: the sites test different subsets of camera, yaw and generation, so one predicate would change some of them.
- **Done 10-23:** `EnvSwitchOff(L"…")` (src/dll/EnvSwitch.h) replaces all 7 kill-switch copies. The tests that set the variables still pass.
- **Partly done:**
  - **Done 10-23:** `YawDistance` in VrMath.h replaces ContactLane's and SurfaceStopLane's `AngleBetween`; `2.0f * kPi` is the same float as their literal. `FiniteVec` is now `Finite`.
  - **Done 10-23 (offline batch):**
    - `stereo::EyeSign` at 7 sites; multiplying by ±1 is exact.
    - `kRadiansPerDegree`/`kDegreesPerRadian` for 29 literals, plus BodyFollow's and HandPose's local copies (HandPose's was misnamed `kDegrees`). Only literals that round to the same float were replaced.
    - **Kept:** `0.01745329f` (AnimIk) and `57.2958f` (ShotRay) are other floats; `180.0f / kPi` rounds one ulp below 57.29578f; `x * pi / 180` evaluates in another order.
    - `ExpAlpha` at 4 sites.
  - `ExpAlpha` is for AnimIk:1212/1952/1967 and TwoHandedAim:94. Not the `/τ` form at ArmPose:281 and HandCarry:301, which rounds differently.
- **Done 10-23 (offline batch):** `StepSeconds(previous, displayTime)` in MonotonicClock.h, at 4 sites. Each site keeps its own repeat-skip.
- **Done 10-23 (offline batch):** `VerifiedSetting` (src/dll/VerifiedSetting.h): the state plus `Advance(key, command, log)`. It is used by ControllerPrompts (`gGlyphs`) and FocusHold (`gPause`), with log lines unchanged. VR start's timed settings loop is a different shape and stays.
- **Checked and kept 10-23:** the pointer-on-surface helper (B12 row).
- **Done 10-24 (batch A):** the `log [0|1]` verb helper (`EventLogSwitch`).
- **Partly done 10-23:** `carry::TrackingToWorld` is gone (see play-space mapping above). The rest of HandCarry.h's math waits for the throwing redo.
- **Done 10-23:** the `phys::` slot aliases (above), and AnimIk's per-frame `GetModuleHandleW` in the IK detour, which now reads `PreyBase()`.

**Pure logic out of XrSessionHost, with new tests.** **Done 10-24 (batch A):** `ui::FitMenuStack`, `ui::WantMenuPanel`, `ui::StepFade`, `wrist::Gate`, `stereoframe::ScaleFov`, and the IPD computed once.
- menu-stack fit loop (1404-1437)
- fade ramp and quantisation (2749-2755)
- wrist gate bitmask (2608-2611)
- panel-mode decision (2124-2126)
- `ScaleDeclaredFov` (1137-1153)
- compute the runtime IPD once per frame (it is computed twice: 2078-2081 and 2845-2849)

**Moves and renames:**
- **Done 10-23, `GameplayFrame.h`** (S1): `GameplayPoseFrame`, `SurfaceOffset`, `EnsureGameplayPoseObservation` and `TryGetGameplayPoseFrame`.
  - AimTakeover.h includes it, so nothing broke.
  - The 12 files that used nothing else from AimTakeover.h now include GameplayFrame.h directly: Arms, BodyFollow, Carry, Grenade, Interaction, PhysicalInteractions, PsiMedkit, Psychoscope, SlotFeedback, Swing, WeaponAttachment, Wrist.
  - The other 17 call AimTakeover's own functions or use `aim::` types.
- **Done 10-23, `Exports.cpp`** (S5): all 159 `PreyVR_*` exports, `PtrArg`, and `CaptureRenderViewsToLog`, which now reads `SmokeStatus()` (the same acquire load).
  - Bootstrap.cpp is down to 292 lines and its lifecycle includes.
  - The export table was checked by name before and after: 159 names, identical.
  - PsiMedkit.cpp's receipt pin (20260930-psi-medkit-static) was already stale at 8d05696, so this batch's edits to it don't stale a holding pin.
- **Done 10-24 (batch A):** `UiMode.h` out of HudBridge, signatures kept. BindingDump is renamed ActionMaps rather than split, since `PreyWheelOpen` shares its guarded reader.
- **Done 10-24 (batch A):** the `mem.*` verbs moved to MemoryCommands.cpp. `arms.att/hide/joints` stay in ArmsLane with `AttachmentJoint`'s reader.
- **Partly done 10-24 (batch A), the renames in S6:** InputPath, ResearchHotkeys, ActionMaps, `LastBuiltEye` and the thread's name. The rest are left to the batches that touch that code; see [the batch A scope](OFFLINE-CLEANUP-SCOPE-2026-10-24.md#not-in-this-batch).

**Options groundwork:** the constexpr Spec, lanes initialising from it, and range tests (S4). **Done 10-24 (batch A), differently:** `options::Default`, used for VR start's six literals, and `vr_options_wiring`, which holds every lane's initial value and range to its Spec. The lanes keep their own initial values because most store another form, such as bools, milli-degrees or metres as floats. Also `ButtonSpec[]`: button text currently lives in VrOptionsUi.cpp:55-67 while all setting text is in VrOptions.cpp. Also a per-event table for HapticPolicy.h:22-28.

**Test infrastructure:**
- **Done 10-23 (offline batch): `tests/DllTestSupport.h`.** It provides the captured log with `Logged`/`Count`, and opt-in macros: `PREYVR_TEST_CAPTURE_LOG()`, `PREYVR_TEST_DROP_LOG()` and `PREYVR_TEST_STUB_MINHOOK()`.
  - 13 tests migrated.
  - Left alone: HookInstall (real MinHook), the three lane tests (not owned during the batch, but drop-in compatible), and the two receipt-pinned tests.
- **Done 10-23:** `preyvr_add_test(... DLL ...)`. One flag adds the DLL's lifecycle include and `WIN32_LEAN_AND_MEAN NOMINMAX`.
  - It replaces the 16 repeated `INCLUDES`/`DEFS` pairs and covers every DLL-source test and the 4 replays.
  - The 7 tests that were built **without** the DLL's defines now build the same translation unit the DLL ships, and they still pass.
- **Done 10-24 (batch A):** D3 (the synthetic image and the UseButton tests) and D4 (the launcher preamble). The WARP device fixture stays copied: one copy is in the receipt-pinned NativeWristTests.
- **Done 10-23 (offline batch): `tests/CommandChannelTests.cpp`** (ctest `command_channel`).
  - **What it pins:** 303 command lines over all 13 dispatchers and 175 of about 202 verbs, each with its exact reply and how it was counted. Also the `report` verb's 128 field names, and the A9 fallbacks.
  - **Build change:** the DLL's sources became an OBJECT library (`preyvr_dll_objects`) that both `preyvr` and the test link. PreyVR.dll built byte-identical with that change alone.
  - **Safety:** verbs that start sessions or threads, install hooks, post input, read raw memory or capture frames are excluded and listed in the test.
  - **Oddities pinned as they are** (for B9): see section 5.
- **Done 10-24 (batch A):** `re_verifiers` (tools/re/run_verifiers.py) runs the 26 verifiers against the installed PreyDll.dll, and skips when there is none. 25 pass here; `verify_inventory_stereo` needs pefile.
- **Blocked:** register `test_extract_native_hud.py`. The receipt-pinned `extract_native_hud.py` calls `hashlib.file_digest`, which needs Python 3.11+, and this machine has 3.10.6, so 1 of its 9 tests errors. It also needs pycryptodome. Registering it needs a newer Python on the machine, or a CMake version check that skips it.

### T2: one smoke headset run per batch
- **FrameSchedule batch:** the four tick lists, the worker loop out of CommandChannel, and the CameraEditHook split.
  - **Done 10-24:** PreyVR-63 (lists and worker) and PreyVR-64 (the split), both worn OK. See [the scope](FRAME-SCHEDULE-SCOPE-2026-10-24.md).
- **Hands batch:** `HandWorld` (S2), the AnimIk split with `DriveHand`'s parameter struct and the 5 anchor copies routed through `ArmAnchor`, and the AimTakeover and BodyFollow splits.
  - **Scoped 10-24:** [HANDS-SCOPE-2026-10-24.md](HANDS-SCOPE-2026-10-24.md), with the torso owner (section 4, item 6) and the `nativeEye` rename.
  - **Done 10-24:** PreyVR-65 and PreyVR-66, both worn OK. Carry and Grenade were converted too.
- **Physics batch:**
  - Finish B6: `EngineEntity.h`, the EngineMap constants, and Grenade on `phys::`.
  - The base+RVA overload of `hook::HookAt` and the atomic `InstallOnce` for the three old installers.
  - One shared loose-body reader under Contact and SurfaceStop. SurfaceStop takes its gate from ContactLane (`GateLooseBodies`), both lanes read the same objects separately, and they define "immovable" differently: Surface uses mass≤0, Contact uses mass==0 && invMass==0. Keep each lane's own verdict.
  - SurfaceStopLaneTests:127 stubs `GateLooseBodies`, so it stays an out-of-line function.
- **XR batch** (scoped 10-24: [XR-SCOPE-2026-10-24.md](XR-SCOPE-2026-10-24.md)): B7 (`XrPose.h`, a swapchain factory, `PaintedQuad`/`StaticQuad`, `SelectFormat`, `ComPtr`, `AcquireBackBuffer`/`RendererDevice`, one acquire→wait→release helper with an explicit fault policy), then the XrSessionHost split.
- **UI batch:**
  - The B9 verb table with Audience tags, `ParseNumber<T>` and `QueueConsoleCommandWithRetry`.
  - B10.
  - Split the lasers out of DebugOverlay.
- **Prologue gates:** use `hook::InstallVerified` where they are hand-rolled: NearViewStereo:373, PassCameraProbe:118, RenderFrame:173, HeadTrackingHook:376, DualOverlay:162, CameraEditHook:154, DualSceneStereo:611, AimTakeover:433-462, AnimIk:2059-2083, WeaponAttachment:146-156/294-318. Use `InstallVerified`, not `HookAt`, so the existing `detail=` strings stay.

### Last
- **`src/dll` subfolders**, as one `git mv` commit (T1): core/, seams/, xr/, render/, ui/, gameplay/, diagnostics/.
  - `src/dll/NativeHudValue.h` must stay at the root: its receipt pins it by path.
  - `include/preyvr` stays as it is: 4 path-pinned headers, and the pinned verifiers hard-code their paths.
  - **Churn:**
    - about 77 CMake source lines
    - 21 `#include "../src/dll/X.cpp"` lines
    - 4 unpinned verifiers that read `src/dll/*.cpp` by path
    - stale receipt paths that would dangle for NativeWristTexture, PsiMedkit and two tests
    - a graphify refresh
  - Leave the 97 `src/dll/...` references in dated docs alone.
- **Rewrite ARCHITECTURE.md** once the structure settles. It should cover:
  - the four threads and seams with their tick order
  - the data flow: OpenXR → TrackingFrame → head reference → GameplayPoseFrame → lanes → engine; and the render contract → XR submission
  - the pure/contract/dll tiers, and where engine addresses live: EngineMap.h, `*Native.h`, and file-local gates in about 40 dll files
  - the fail-closed gate policy and the lane template
  - the control surfaces
  - the stereo route
  - the test pyramid and headset gate
  - which instrumentation must stay
- **Don't move or archive dated docs:** receipts link 31 `docs/*.md` paths.

---

## 4. Decisions needed from you

1. **Remove the legacy hand-rig and weapon-mount cluster? About 1,600 LOC, T2.**
   - **Reachability [V]:** `HandRigTakeover` (hook 0x82EE10) installs only through the `hand.mode` verb or `PreyVR_SetHandRigModePtr`. VR start never arms it, and since VR start sets `AnimIkMode` 2, `hand.mode 2` is refused. WeaponAttachment's mount drive runs only from HandRig's detour. TUNING-COMMANDS.md:428 already calls them "superseded by ik.*".
   - **What would go:**
     - HandRigTakeover
     - the WeaponAttachment mount drive (:50-84, 159-221, 512-678)
     - MotionController's `RotateJointAboutPivot`, `WorldTurnToModel` and `ControllerPoseInWorld`
     - the `hand.*` (11) and `weapon.offset/apply/rotate/calibrate` verbs
     - about 35 hand/weapon report fields
     - AnimIk's interlocks at :1923 and :2092-2096
     - the related exports
   - **Keep** `weapon.observe` and `EquippedRig`.
   - `tools/Invoke-PreyVRIkTest.ps1:68` reads `handMode` and needs updating.
   - Section F forbids only *merging* the legacy yaw, so deleting it is consistent with F.
   - **Done 10-23 (you: "if it's unused, cut it").**
     - Deleted HandRigTakeover.{h,cpp}, the 11 `hand.*` verbs, `weapon.offset/apply/rotate/calibrate`, 31 report fields, AnimIk's two interlocks, and Invoke-PreyVRIkTest's `handMode` guard.
     - Cut WeaponAttachment's mount writer (offset, rotation drive, `WriteMount`, `ControllerAimRotation`). Its observation half is kept: rig identity, barrel calibration, the firing-position observer, and the mount read at equip (`weaponMountMm`, `weaponBaseline`).
     - The hand-rig MotionController helpers went with the item 5 batch.
     - TUNING-COMMANDS.md is updated.
2. **Unused exports.** 85–99 of the 159 have no consumer in tools, tests, the harness or the docs; this comes from grep, not a link map. Choose one:
   - cut them;
   - move them to `Exports.cpp` and compile it only in dev builds;
   - keep them.
   - **Done 10-23: cut.**
     - Of the 159, 84 were removed: the 82 that nothing references, plus `SetHandRigModePtr` and `SetXrSwapEyesPtr`, which only the audit notes named.
     - 75 are kept: 62 that the live harness (`tools/live/preyvr-harness.js`) or DllSmokeHost calls, and 13 that procedure docs still tell someone to call (XRSIM_INTEGRATION.md, LIVE_SUBMISSION_TEST_PROTOCOL.md, LIVE_CAPTURE_PLAN.md and others).
     - The built DLL's export table was checked against that kept set: exactly 75 names, no more and no fewer.
     - Getters and setters left with no caller were then cut too, along with the state only they read. That covers BodyFollow ×2, CommandChannel ×3 (its counters stay, because the report prints them), DebugWatch ×2, HeadTrackingHook ×4, HotkeyBridge ×4 (and its press count), IkTargetProbe ×1, MoveLane's no-op `SetFireLaneThreshold`, RenderFrame ×2, and WrenchDraw ×1.
     - BodyEquipment's `ReadWristVitals` was cut with its `status` snapshot, which nothing ever published, so it always returned false.
3. **Research probes:**
   - **AimRayProbe:** enabled only through an unreferenced export, but sampled every render frame (FrameObserverHook:69).
   - **IkTargetProbe:** reachable only through the harness; the H-005 question was answered 09-05.
   - **PassCameraProbe's forward-axis comparison and its 6 exports:** keep the hook, because FrameProbe uses it, and rename it for that job. Cut the comparison together with CameraEditHook's `gWrittenForward*`.
   - **RuntimeSnapshot:** runs on every startup (Bootstrap.cpp:216-228).
   - **Done 10-23:**
     - **AimRayProbe** is deleted, including its per-frame sample in FrameObserverHook; nothing could switch it on once its exports were gone.
     - **PassCameraProbe** is replaced by `PassCreationHook.{h,cpp}`: just the R-071 hook that FrameProbe rides on. The comparison and CameraEditHook's `gWrittenForward*` / `LastWrittenCameraForward` are cut. Its log prefix `preyvr_pass_camera` is unchanged.
     - **Kept: IkTargetProbe**, because the harness drives it, and **RuntimeSnapshot**, because it writes the startup log.
4. **Feature leftovers:**
   - WrenchSound's capture path: 3 hooks plus a timing window, about 180 LOC.
   - WrenchDraw's capturing branch.
   - Grenade's flight watch, about 200 LOC.
   - The `carry.impulse` A/B modes; mode 2 drops objects through the floor.
   - `grenade::Landing`, used only by tests.
   - **Not cut 10-23: these are reachable, not unused.** The wrench-sound capture is switched on by `wrench.sound capture`, the grenade flight watch runs on every throw, and `carry.impulse` selects its modes by command. They belong to the TODO's instrumentation and tuning cleanups. `grenade::Landing` was cut with item 5.
   - **Cut 10-23, your decision:** RenderFrame and its table (`frame.*`), the `carry.impulse` A/B modes (mode 0 is now the only behaviour), the wrench-sound capture (the replay stays), and the grenade flight watch.
5. **Pure functions used only by tests:**
   - `aim::ProjectToScreen`, `ApplyRadialDeadzone`
   - StereoCamera's `ReferenceFrameForHeadTracking`, `ComposeHeadOntoGameRotation`, `MakeRecenterReference`, `RightOf`, `UpOf`
   - `StereoFrame::BuildStereoPlan`, InventoryDepth's `MapDepth`/`PlaneMetres`
   - MotionController's `SmoothTurn`, `Mount*`, `WeaponPoseFromController`, `TwoHandedWeaponPose`
   - InteractionQuery/WrenchQuery, which you chose to keep on 10-18.
   - **Done 10-23.** A comment-stripping scan over every header found the full set; each name was re-checked with `git grep` and ripgrep before it was cut.
     - **Cut, with their tests:**
       - ArmPose `WristDeviationDeg`
       - CameraEdit `ApplyYaw` and its bounds helpers
       - GrenadeThrow `Landing`
       - HandCarry `MotionHistory::Latest`
       - InteractionUse `OwnerName`
       - InventoryDepth `MapDepth`/`PlaneMetres`, and the two observed constants
       - MotionController `RotateJointAboutPivot`, `SmoothTurn`, `TwoHandedWeaponPose`, `WeaponPoseFromController`, `WorldTurnToModel`, `JointPose` and `GripTransform`
       - NearParticles `BuilderName`
       - RuntimeSnapshot `RenderCameraMatchesSource`; its tests now compare `RenderCameraResidual` against the limit, as production does
       - StereoCamera `ComposeHeadOntoGameRotation`, `MakeRecenterReference` and `ReferenceFrameForHeadTracking`
       - StereoFrame `BuildStereoPlan`
       - VrMath `ApplyRadialDeadzone`
       - WeaponAim `ProjectToScreen`
       - RenderFrame `TryGetRenderMatrix`
     - **Kept because live code calls them:** `WrapAngle` (SnapTurn), `RightOf`/`UpOf` (PoseFromMatrix), `RotationIsOrthonormal`/`RotationIsDegenerate`, and `ControllerPoseInWorld`/`MountRotationFromControllerDelta`, which tools/xr_session_probe uses.
     - **Kept as test oracles or accessors for live code:** ControllerProfiles `ActionOf`, DebugRegisters `Dr7SlotEnabled`, FrameTiming `BudgetMicroseconds`, ShotRay `ConvergenceErrorMetres`, and the lanes' `*EventLog` getters.
     - **Left:** RenderFrame's matrix capture (`frame.*`, research) is now never read back in production; it goes with the instrumentation cleanup.
6. **Torso settings: give them one owner (T2/T3).** They are stored twice, in AnimIk `PoseSettings` and BodyFollow `gDeadzoneMilliDeg/gRelaxPerMille`. `arm.torso` writes both, `body.torso` writes only BodyFollow, and the Body Turn Deadzone option goes to AnimIk.
   - **Decided 10-23: (a).** BodyFollow owns the settings and AnimIk's fallback filter reads them; it goes in the Hands batch.
   - **Done 10-24** (PreyVR-65): `body.torso`, `arm.torso` and the option all set the one copy.
7. **Should Grenade get the `VelocityQueueReady` gate** that Carry and Contact use? This changes behaviour, although it is latent on the pinned build.
   - **Decided 10-23: skip.** Throwing is being redone, so the grenade parts of B6 and the carry/grenade release-snapshot sharing are dropped.
8. **The instrumentation gate design** (S4: `PREYVR_DEV`, `kDev`, `Diag()`, Audience tags). It is a later TODO item, but the refactors above should be shaped for it.
9. **The build version string,** added 10-23. CMakeLists:10 says `0.3.1-static-ik-integration`, while the tags are at v0.5.1-preview. It is logged as `preyvr_smoke_start version=`. What should it say?
   - **Done 10-23:** `0.6.0-preview`, following the tag scheme; the last tag is v0.5.1-preview (09-13), 92 commits back.

---

## 5. Possible bugs found along the way (not refactors; none confirmed in the game)
- **Carry and grenade commands before their lanes install (found 10-23 by the CommandChannel test).**
  - `CallGetPlayer` calls `gBase + kGetPlayerRva` while `gBase` is still 0 (CarryLane.cpp:243, GrenadeLane.cpp:246), so `carry.report` and the other `carry.*`/`grenade.*` verbs jump to a raw RVA.
  - The fault guard catches it offline, but in-game that low address could be mapped.
  - Fix: return 0 when `gBase` is 0. It is two lines and changes behaviour only in that broken case. Not done; your call, given the throwing redo.
- **CommandChannel oddities pinned by its test (10-23), for B9:**
  - **Counting:**
    - Lane-dispatched verbs (about half) count as neither processed nor rejected.
    - Unknown lane-prefixed verbs reply `result=1` uncounted.
    - Verbs missing required arguments are rejected as `not_a_channel_verb_and_not_allowlisted`.
    - A short `hud.call` replies `result=rejected` but counts as processed.
  - **Parsing:** integers parse with base 0 (`0310` is octal).
  - **Against A9:**
    - `wrist.*` uses `atoi`; `carry.*`/`grenade.*` accept partial numbers.
    - `surface heavy abc` gives 1 kg; `ui.prompts abc` turns prompts on.
    - `near.fov`, `ik.align`, `dbg.draw`, `aim.shot`, `near.fx` and `markers.depth` refuse malformed values (`ui.offset` too, which is documented).
  - **Replies:**
    - `near.fov` turns `near.follow` off even when refused.
    - Multi-value setters given too few values reply `result=0`.
    - Several replies lack `result=`.
  - **The `report` log line** carries every reply in the batch so far.
- **VR stop doesn't undo everything VR start arms [V].** `StopFeatures` (VrMode.cpp:59-75) never resets these:
  - `SetUseHand`, `SetCarryHand`, `SetGrenadeHand` and `SetAnimIkFreeArms`
  - `SetAimLasers`, `SetUiPanelMode` and `SetMenuNavigationGate`
  - the reticle dispatch, and IK alignment, hands and reach

  The impact is unknown. It may not matter if nobody stops VR mid-session.
- **Research hotkeys are live in the player package.** `Start-PreyVR.ps1:79` sends `input.hotkeys 1`, and Ctrl+Alt+PgUp/PgDn calls `SetSyntheticStereo`, which overrides the runtime IPD. This was read from the code, not tried in the game.
- **The console state can desync.** HotkeyBridge never retries a busy console. Its second command after `Sleep(40)` can be dropped while `gConsoleOpen` flips anyway. Unverified.
- **The IPD can desync** if it changes mid-session (S7).
- **Grenade skips the queue-ready check** before setting velocity (B6 row).
- **Three verb fallbacks disagree with the Spec:** `hud.layer` and `move.headrelative`, listed in B9.
- **Smaller defects:**
  - `channelProcessed` undercounts, because sub-dispatched verbs return before the increment. Nothing reads it.
  - Six verbs reply without `result=`, contrary to TUNING-COMMANDS.md:19: `haptics`, `aim.scene`, `vr.options`, `contact`, `surface`, `swing`.
  - An unknown `wrist.*` verb answers `result=1` but is not counted as rejected.

---

## 6. Additions to section F (looks duplicated, keep separate)
- **Proxy velocities:** Contact's is in world space and includes the player's own motion, so a push follows walking (ContactQuery.h:65-67). Swing's is arm-only in reference space, so walking must not strike. Share only the dt window.
- **Immovable verdicts:** Surface and Contact each keep their own (S3, T2 batch). Share the *reading*, not the verdict.
- **`CopyName`:** WrenchSound's writes "?" and substitutes '_', and Swing's `CopyText` differs too; neither matches the three identical copies.
- **Swing's unguarded `TypeOf`** runs inside one composite `__try` on purpose.
- **`TrackedWeaponBox`** uses the same sample, while SurfaceStop's `AddWeapon` is re-anchored to this frame's grip.
- **Per-fix stereo edit/restore steps:**
  - CameraEdit restores bytes and verifies them.
  - NearView restores a row, with a re-entry guard and a written-row cache.
  - Marker restores Flash values.
  - NearParticle suppresses the NearView edit.
  - DualScene restores the centre camera.

  A single helper for all of them would become a helper that forks on flags.
- **`AngleBetweenDegrees`** in AimRayProbe and PassCameraProbe returns 0 and −1 on degenerate input; it is not the lanes' `AngleBetween`.
- **MoveLane's turn rescale** is deliberately one-dimensional, so it is not `ShapeStick`.
- **Formatting:** don't mass-reformat the three densely formatted files (VrOptionsRuntime.cpp, Psychoscope.cpp, BodyEquipment.cpp); it would destroy blame. There is no `.clang-format`.

## 7. Suggested order
1. **T1 cleanup, with no headset.** **Done 10-23:** the safe dead code, stale comments and READMEs, the CMake option text, the `DLL` test flag, `DllTestSupport.h`, the CommandChannel characterization test, and the version string.
2. **T1 abstractions:**
   - done 10-23: `GameplayFrame.h`, `OnEngineMainThread`, `EnvSwitchOff`, `YawDistance`, `Exports.cpp`, `PhysicalLane.h`, the play-space functions, the recenter checks, the small helpers and `VerifiedSetting`;
   - done 10-24 in batch A, worn OK on PreyVR-67: the XrSessionHost pure extractions, the options wiring test, the renames, and the `log [0|1]` helper.
3. **Your decisions in section 4,** then the approved deletions. **Done 10-23.**
4. **T2 batches,** one headset smoke run each: FrameSchedule, Hands, Physics, XR, UI. **FrameSchedule done 10-24, worn OK on PreyVR-63/64. Hands done 10-24, worn OK on PreyVR-65/66, committed `84d6c19`. XR done 10-24, worn OK on PreyVR-68, committed `e74a389`. UI phases 1 and 2 committed `51b0f87`; phase 3 built 10-24 as PreyVR-70, awaiting the headset.** Physics comes after the throwing redo.
5. **Last:** the folder move and the ARCHITECTURE.md rewrite.
