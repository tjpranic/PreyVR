# PreyVR duplication audit — 2026-10-17 (branch `custom`, clean tree)

This was read-only: nothing was edited, built or run. Five parallel reviews covered:
1. DLL hook and probe infrastructure
2. Gameplay lanes
3. OpenXR, render, UI, options and commands
4. Tests, build and headers
5. Tools and scripts

**[V]** marks a claim that was spot-checked against the code after the reviews came back. Every line count is a reviewer estimate.

Line numbers are as of commit 8c07a51, before the section A fixes. Files those fixes touched have shifted by a few lines.

**Status:**
- **2026-10-17:** the section A bugs were fixed; see the status table under A.
- **2026-10-18:** B1, D1 and D2 were done, for about 800 lines net; see their status notes. Build passes, 76/76 ctest pass, no new warnings. Nothing has been committed or run in the headset.
- **Not started:** the rest of B–E.
- **CameraEditHook research experiments (C):** the user decided on 2026-10-18 to keep them for now.

## Constraints that shape every fix

- **Shared C++ helpers must be header-only (`inline`).** These tests `#include "../src/dll/X.cpp"` directly, link only minhook, and stub `EnsureMinHook`, `ModulePinStatus` and `lifecycle::Log`: CullCameraWatchTests, FocusHoldTests, NearFovOverrideTests and InventoryPointerCaptureTests, among others. Tests also call file-local names directly (`SpanMatches`, `CodeMatches`, `MatchesCode`, `kCode`), so keep those as aliases.
- **Log line formats may be grepped by tools.** Keep `detail=` strings when collapsing installers.
- **Seven `tools/re` scripts have SHA-256 pins in receipts:**
  - verify_body_equipment
  - verify_native_wrist
  - verify_psychoscope
  - verify_twohand_no_alias
  - extract_native_hud
  - map_native_hud
  - verify_inventory_stereo (already stale)

  Editing one stales its pin. `.gitattributes` also forces their line endings.
- **`tools/Start-PreyVR.ps1` must stay self-contained.** It ships alone and runs on Windows PowerShell 5.1, and `tests/Launcher*.Tests.ps1` parse its source by function name.
- **Hook, layer and input changes still need a headset run.** That matches how the current uncommitted work is being handled.

---

## A. Bugs found during the audit (fix first; these are not refactors)

### Status, 2026-10-17

The fixes are uncommitted on `custom`. They build, all 76 ctest tests pass, and they are packaged as `build/packages/PreyVR-13`. None has been run in the game or the headset yet.

| # | Status |
|---|---|
| A1 | **Fixed.** `src/dll/ReticleRayHook.{h,cpp}` owns the one detour and is gated on the `aim.get_cached_ray` landmark. Those 57 bytes hash to psi's contract value, so the gate is as strict as psi's was. ShotRay and PsiMedkit each register a filter that runs after the native getter. Psi's `Gate()` skips that contract row once the shared hook is installed. **Headset check:** turn on muzzle_aim plus psi or medkit. Expect one `preyvr_reticle_ray result=0 detail=installed` line, `getterHooked=1` in the shot report, and psi/medkit enabling without `ERROR_BAD_EXE_FORMAT`. |
| A2 | **Fixed.** `InventorySwapchain` takes a minimum size per instance (default 640×480, `kInventoryMinWidth/Height`). The slot layer passes its own size, and a refusal now logs `detail=slot_swapchain`. A new `SizeFloor` test covers it in `InventorySwapchainTests`. |
| A3 | **Fixed.** `MH_Uninitialize` is gone from `FrameObserverHook`, along with the duplicate `EnsureMinHook`. |
| A4 | **Fixed.** Both installers now remove the hook and clear the original when enabling fails. |
| A5 | **Fixed.** `MH_QueueEnableHook` × 3, then `MH_ApplyQueued`; on any failure all three are disabled and removed. |
| A6 | **Fixed.** Each half of `EnsureTexture` resets its resources on failure. Calling `EnsureDepthCopy` from the depth half is still part of B10. |
| A7 | **Fixed.** Hold frames now use `input::Pressed` with the same `gTriggerDown` state as live frames. Both paths run under the session lock. |
| A8 | **Fixed.** A new `XrSwapEyes()` getter; the hotkey toggles the real flag, and `HotkeyEyeSwap()` reads it. |
| A9 | **Not changed, on purpose.** Falling back on a malformed value is documented intent (`CommandChannel.cpp:89-91`, "a typo cannot arm something"), and every fallback lands somewhere safe: off, reset, or refused. Decide refuse vs fallback as part of B9. |
| A10 | **Fixed:**<br>• ControlDemo's `?:`.<br>• IkTest's empty-file guard.<br>• Startup, IkTest and HudValidation find the run by the newest `PreyVR.log` (Send-PreyVR's rule). An explicit `-RunDir` accepts either layout. ControlDemo keeps `<run>\log`: it only works with xr-sim launches.<br>• Launch and AimCheck fall back to `build\headless`.<br>• HudValidation takes the capture folder from the `PreyVR.dll` loaded in the running game.<br>• `verify_h005_skinning.DEFAULT_MODULE` resolves `PREYVR_PREYDLL`, then the upstream `D:\SteamLibrary`, then Steam's `libraryfolders.vdf`. The cull_camera_watch, h022 and vr_scheme_native verifiers now import it.<br><br>**Not changed (E3):** the game-install and xr-sim root defaults (`D:\…`) in Launch, Doctor, Run-Headless, Restore-PreyIntroVideos, AimCheck, ControlDemo, ControllerSweep and UnderXrSim. |
| A11 | **New. Latent, not fixed.** HeadTrackingHook's `CRenderView::SetCamera` hook (0xEE7E80) would make DualSceneStereo's `view.set_camera` prologue gate refuse. It is reachable only through the unused `PreyVR_SetHeadTrackingEnabledPtr` export, and goes away with that hook's removal in C. |

A scan of every engine RVA that appears in two or more modules found no other double hook. GetFiringPosition (0x1694BC0) already has a single owner, WeaponAttachment's `EnsureFiringPositionHook`, which ShotRay calls.

### As found

| # | Issue | Where | Notes |
|---|---|---|---|
| A1 **[V]** | Two lanes hook the same engine function, `IArkPlayer::GetReticleViewPositionAndDir` 0x157CBB0 | `ShotRay.cpp:71,643` (`HookAt "ray_getter"`); `PsiMedkit.cpp:166` (`native::Reticle`) plus a 57-byte FNV gate at `PsiMedkitNative.h:17` | ShotRay installs on any `muzzle_aim` apply, including 0 (`ShotRay.cpp:661-663`, `VrOptionsRuntime.cpp:176`). **If ShotRay installs first:** PsiMedkit's `Gate()` hash fails (`ERROR_BAD_EXE_FORMAT`), so PSYCHIC TARGETING and LEFT-HIP MEDKIT can't enable. **If Psi installs first:** ShotRay's getter hook silently fails, losing the pellet scope, and `Install()` still reports success. Fix: one owner of the getter detour that chains both overrides, gated on the `aim.get_cached_ray` landmark. Found by reading code, not observed in logs: the logs never show psi or medkit enabled. |
| A2 **[V]** | Slot-feedback layer never draws | `SlotFeedback.h:10` makes it 1024×224, but `InventorySwapchain.cpp:17-21` `Supported()` requires height ≥ 480. `XrSessionHost.cpp:2624` latches `slotAttempted`; `:2640` resets silently, with no log | Only visible with holsters or medkit enabled (both default off). Fix: give `InventorySwapchain` a minimum-size parameter. The fade layer was already forced to 640×480 to dodge the same limit (`:2676-2679`). |
| A3 **[V]** | `FrameObserverHook` calls `MH_Uninitialize()` on failure | `FrameObserverHook.cpp:201,222` | Disables every other installed hook while `EnsureMinHook`'s cached ready flag still says true, which is the trap `MinHookInit.h` documents. `EnsureMinHook` is also called twice in a row (:186, :194). |
| A4 **[V]** | `CameraEditHook` never removes the hook after an enable failure | `CameraEditHook.cpp:1410`, `:1774` | A retry then gets `MH_ERROR_ALREADY_CREATED` forever. |
| A5 | `DualSceneStereo` enables hooks one at a time | `DualSceneStereo.cpp:694-700` | A partial failure leaves earlier hooks live while `gInstalled=false`, contradicting its comment at :676. `HudLayer.cpp:946` and `PsiMedkit.cpp:176` already use the atomic `MH_QueueEnableHook`+`MH_ApplyQueued`. |
| A6 **[V]** | `HudLayer::EnsureTexture` can return true with a null RTV | `HudLayer.cpp:705-708` | If `CreateTexture2D` succeeds and `CreateRenderTargetView` fails, the next call sees matching sizes and skips creation. `EnsureDepthCopy` (741-755) handles this correctly; reuse it for the depth half too. |
| A7 | Trigger threshold differs in hold frames | `XrInput.cpp:640-641` uses a raw `>=0.5f`; the live path at `:492` uses `input::Pressed` with 0.5/0.4 hysteresis | |
| A8 | Hotkey eye-swap keeps a stale copy | `HotkeyBridge.cpp:36,176-181` `gEyeSwap` vs `XrSessionHost` `gSwapEyes` (376) | `PreyVR_SetXrSwapEyesPtr` changes the real flag, so the hotkey stops toggling. The change is also logged twice. |
| A9 | Malformed command values fall back inconsistently | `CommandChannel.cpp:680` (`hud.layer`→0, Spec default 1); `:1178` (`move.headrelative`→2, which `MoveLane.cpp:804` refuses); `ui.scale`→100 | A typo resets one setting, disables another, and is refused for a third. Refuse with `ERROR_INVALID_PARAMETER` instead. |
| A10 | Tool scripts that fail on this machine | **[V]** `Invoke-PreyVRControlDemo.ps1:200` uses `?:` (PS7-only; a parse error in 5.1). `Invoke-PreyVRIkTest.ps1:47-59` throws on an empty `results.txt` under StrictMode. Four scripts assume `runs\<newest>\log\`, but `Start-PreyVR` writes `runs\player-*\PreyVR.log`. **[V]** `verify_h005_skinning.py:13` `DEFAULT_MODULE` points at `D:\SteamLibrary` (Prey is on `E:\Steam Games`). Launch, AimCheck and HudValidation default to `build\configure`, which doesn't exist (only `build\headless` does). | |

---

## B. C++ consolidation, ranked by value × safety

### B1. Vector and quaternion math library (high value, none to low risk, about 250 LOC)

**Status 2026-10-18: done for every bit-identical copy.**

What went into `VrMath.h`:
- Inline `Add/Sub/Scale/Dot/Cross/Length/Finite(Vec3)/Conjugate` and `kPi`.
- `QuaternionFromBasis` (in VrMath.cpp).

What was replaced:
- **Local copies,** removed from about 30 files: the `Mul`, `Len`, `Sub3/Add3/Scale3`, `Conj`, `ConjugateOf` and `Inv` names now call the shared helpers.
- **The public copies:** `debugdraw::`, `carry::`, `physical::`, `scene::Finite`, `stereo::Conjugate` and `stereo::QuaternionFromBasis` are now using-declarations, so qualified callers still compile.
- **The four matrix→quaternion copies** now call the shared one:
  - HandPose `FromColumns` and WristHolo `FromBasis` are gone.
  - `carry::FromMatrix34` keeps its column normalisation and calls it.
- **The quaternion `carry::Inverse` and `physical::Inverse(Quaternion)`** are now `Conjugate`. `physical::Inverse(Pose)` stays.
- **The local `kPi` copies** were checked first: every spelling is the same float, `0x40490FDB`.

The compiler found the copies: with a shared `preyvr::Add` in scope, any leftover same-signature copy becomes ambiguous through ADL.

**Deliberately left:**
- Angle-between: the copies return different values for degenerate input, and some use acos where others use atan2.
- `Slerp` vs `twohand::Mix` and `FromTo` vs `Swing`: different behaviour.
- The smoothing factor, `Inverse(Pose)`/`Transform`, and the AnimIkTakeover pose algebra: these belong with B11.
- The dll/BodyFollow `kDegrees`/`kRadians` literals: not provably the same float.
- **Vec3 helpers are re-defined in about 25 files.** **[V]**
  - `src/common`: ArmPose:11-16, DebugDraw:8-13, GrenadeThrow:18, HandCarry:11-15, HandPose:12-17, InteractionUse:11-15, MotionController:10-19, ShotRay:8-12, TwoHandedAim:7-10, WristHolo:9-14, AimState:15, RuntimeSnapshot:71.
  - `src/dll`: AimRayProbe:67, AimTakeover:109, AnimIkTakeover:627-629/737/2341, InteractionLane:251-254, ShotRay:145-149, WristLane:26-29.
  - Tests: about 8 files.
- **Three namespaces export them publicly:** `debugdraw::` (DebugDraw.h:147-155), `carry::` (HandCarry.h:40-47, "shared with the DLL"), and `physical::` (inline in PhysicalInteractions.h:8-12). VrMath.h has none of them.
- **Quaternion conjugate/inverse has 12 named copies and about 6 inline ones.** The public ones are stereo::Conjugate, dd::Conjugate, carry::Inverse and physical::Inverse. The file-locals are in AnimIk, MotionController, WeaponAim, TwoHandedAim, WeaponRigAlignment, AnimIkTakeover, WristLane (unused) and WeaponAttachment.
- **Matrix→quaternion is implemented 4 times with identical branches and formulas:** HandPose.cpp:26 `FromColumns`, WristHolo.cpp:25 `FromBasis`, HandCarry.cpp:24 `FromMatrix34` (normalizes the columns first), and StereoCamera.cpp:61 `QuaternionFromBasis`. MotionController.cpp:316 already has a comment asking for this to be shared.
- **Other repeats:**
  - Slerp: HandPose:255 vs TwoHandedAim:14.
  - Shortest-arc rotation: ArmPose:213 vs TwoHandedAim:22.
  - Smoothing factor `1-exp(-dt/τ)`: 6 copies.
  - `Pose Inverse/Transform` exists only in `physical::`, so AnimIkTakeover re-implements Compose and RotateJointAboutPivot (:1396, :1532, :975-983, :1425).
  - `kPi` and `kDeg`: about 13 copies.
- **Proposal:** put inline `Add/Sub/Scale/Dot/Cross/Length/Finite/Conjugate/Inverse(Pose)/Transform/QuaternionFromBasis/Slerp/SmoothingAlpha/kPi/kDegToRad` in VrMath.h, and turn the namespace copies into `using preyvr::X;` so qualified callers still compile.
- **This must land together with B12 (tests).** Test files that do `using namespace preyvr;` and keep their own anonymous `Add` would become ambiguous calls.
- **Angle-between in degrees:** there are 5 copies with different degenerate-case results:
  - ShotRay:45 → 0
  - InteractionUse:292 → **180** (RayFilter depends on this)
  - AimState:53 → 0
  - AimRayProbe:40 / PassCameraProbe:63 → 0 / −1
  - AnimIkTakeover:2342 → −1

  Share one implementation that returns `optional`, and let each caller keep its own sentinel.

### B2. MinHook install boilerplate (high value, low risk, about 300 LOC; A3–A5 are already fixed in place)
- **[V]** `template HookAt<Fn,N>` is byte-identical in CarryLane.cpp:895, GrenadeLane.cpp:793, InteractionLane.cpp:391 and ShotRay.cpp:602. NearParticleStereo.cpp:217 has the same function with a `uint32_t` rva.
- **About 20 hand-written single-target installers:**
  - CameraEditHook :1366/:1729
  - HeadTrackingHook :182/:530
  - PassCameraProbe :106, NearViewStereo :389, NearFovOverride :92, CullCameraWatch :485
  - DualOverlay :142, FocusHold :218, FrameObserverHook :180, DualSceneStereo :601
  - AimTakeover :461-490, AnimIkTakeover :1923-1950, WeaponAttachment :146-157/:307-337
  - HandRigTakeover ~570-600, BodyFollow :686-701, MoveLane :253 `InstallOne`
- The "Shared and idempotent… See MinHookInit.h" comment block is pasted 12 times.
- **Proposal:** `src/dll/HookInstall.h` (inline) with:
  - `InstallVerifiedHook(base, rva, prologue, detour, std::atomic<Fn>&, prefix, name)`, which publishes the original before enabling and removes the hook on failure
  - `InstallHooksAtomically(span<Spec>)` using the queue/apply form
  - `CreateHookOnce` / `EnableHook` for the keep-trampoline files: NearFovOverride, CullCameraWatch, FrameObserverHook
- Keep each caller's `detail=` text.

### B3. SEH safe-memory primitives (medium-high value, none to low risk, about 190 LOC)
- **About 35 `__try{memcpy/memcmp/deref}__except` primitives:**
  - DualSceneStereo:153 `SafeRead<T>`
  - IkTargetProbe:40/67
  - InputPathProbe:53/63
  - BindingDump:19 `GuardedCopy`
  - DebugWatch:132
  - NearViewStereo:239/249
  - FocusHold:189/196/237
  - CullCameraWatch:176/183
  - HudBridge:169/316/327
  - NearFovOverride:40
  - AnimIkTakeover:211/221/231/438
  - BodyFollow:185, MoveLane:183/193, CarryLane:232, GrenadeLane:264, InteractionLane:137, ShotRay:190, ArmsLane:112, WeaponAttachment:104
- They are semantically equivalent; the differences are cosmetic (out-param vs 0-sentinel, null pre-checks).
- **The `VirtualQuery` "is this address executable" check has 7 copies:** CameraEditHook:250, DualSceneStereo:585, DualOverlay:162, ConsoleBridgeWin32:78/114, FrameObserverHook:102, HudBridge:241.
- **Vtable slot checks:** CullCameraWatch:201, BodyFollow `SlotIs`, NativeHudValue.h:32 and PhysicalInteractions `Gate`.
- **Proposal:** `src/dll/SafeMemory.h` containing `SafeCopy/SafeEqual/SafeWrite/SafeRead<T>/SafeReadOr<T>/IsExecutableAddress(addr, requireModule)/VtableSlotIs`.
- **SEH is the right choice on hot render threads.** NearViewStereo runs it about 190 times per frame, and `VirtualQuery` is a syscall.

### B4. Engine constants, layouts and contracts in one place (high value, none to low risk, about 300 LOC)
- **The FindLandmark-by-id loop has 7 copies:** AimRayProbe:26, PassCameraProbe:44, DualSceneStereo:575, CameraEditHook:1706/1741/1815, DualOverlay:154, AimTakeover:99. → `engine::FindLandmark(id)`.
- **Lane prologue arrays that match EngineMap landmarks byte for byte:**
  - NearFovOverride:18-24 = `renderer.begin`
  - CameraEditHook:48-52 and HeadTrackingHook:73-77 = `camera.update_frustum`
  - HeadTrackingHook:71/83 and :362-366 (prefixes of `view.set_camera` and `camera.update_view`)
  - AnimIkTakeover:47 kProcessAdik
  - ShotRay:37 kWeaponFiringReticle. Its getter prefix (:72) is gone: the getter now goes through ReticleRayHook's landmark gate.
  - InteractionLane:36/40
  - ArmsLane:44 (a prefix)
- **Native contracts:**
  - `struct Contract{rva,size,hash}` is declared 5 times: EquipmentNative.h:5, NativeWristNative.h:5, PhysicalNative.h:7, PsiMedkitNative.h:12, SceneQueryNative.h:9.
  - The FNV-1a loop is written 5 times: BodyEquipment.cpp:33, NativeHudValue.h:9, PsiMedkit.cpp:149, PhysicalInteractions.cpp:73, SceneQuery.cpp:17.
  - Rows are repeated across tables: `{0x157C990,48,…}` appears 3 times.
  - **Hazard:** PsiMedkitNative.h:8-11 vs 14-31, PhysicalNative.h:5-6 vs 9-23 and SceneQueryNative.h:7-8 vs 11-16 each declare an RVA twice, as a scalar *and* as a table row. The gate checks the table entry, but the code calls the scalar, so if the two drift the gate verifies a different function than the one called.
  - → `include/preyvr/NativeContract.h`: one type, `constexpr Fnv1a`, `BodiesMatch(base, span)`, with the scalars defined as `X.rva`. Keep each caller's SEH and error mapping.
- **Layouts re-declared:**
  - `kCameraSize 0x240` has 7 copies: EngineMap, CameraEdit.h:28, RuntimeSnapshot.h:27, StereoCamera.h:237, StereoFrame.h:28, CullCameraWatch:32, DualSceneStereo:61.
  - RuntimeSnapshot.h:34-37 equals EngineMap values, even though its comment says "These duplicate nothing".
  - DualSceneStereo:57-88 re-declares SThreadInfo and the frame block (`0x49A0+0x68 == frameBlock 0x4A08`, etc.).
  - CameraEditHook:459-461 re-declares 0x888/0x198/6, then uses the EngineMap names at :582.
  - InputPathProbe:20 `kGEnvRva`.
  - GetPlayer `0x157C990` appears in ArmsLane:38, CarryLane:72, GrenadeLane:102, InteractionLane:49, BodyEquipment:50 and SceneQuery:33, plus 3 headers.
  - Entity-system and physics-slot constants appear 3 times (Carry, Grenade, Interaction).
  - Skeleton and pose-array layout (stride 0xA8, QuatT 0x1C, etc.) appears in AnimIkTakeover:58-80, HandRigTakeover:43-56, WeaponRigAlignment:18/36-71 and ArmsLane:351-364. There are two `QuatT` structs: AnimIkTakeover:400 and WeaponAttachment:56.
  - ArkPlayerInput (`+0x8E0`, vtable 0x1E580E0, `+0x94` cinematic) is used in 6 files with 4 different declarations.
  - WrenchQuery `RayHit` is the same 0x50 layout as `scene::Hit`. InteractionQuery.h:13-14 repeats EngineMap.
- → Alias everything to EngineMap with `static_assert`, and add `ThreadInfoLayout`, `SkeletonLayout` and `ArkPlayerLayout`.

### B5. Engine-access helpers (medium value, no risk, about 110 LOC)
- **[V]** `GetModuleHandleW(L"PreyDll.dll")` is called 88 times across 39 files, some of them on per-frame paths. The module is pinned, so cache it as `PreyBase()`. FocusHold's injectable `gModuleBase` is used by its test, so keep it.
- **The live view-camera read has about 10 copies:** Bootstrap:647, CameraEditHook:800, NearViewStereo:210 (dead), ReticleFollow:120, HandRigTakeover:96, WeaponAttachment:174, XrSessionHost:1139/1217, AimTakeover:174/223 (read twice in one call).
- **Other repeated lookups:**
  - The backbuffer from the renderer swapchain: 5 times (XrSessionHost 2123/2809/3168, FrameCaptureWin32 169/229).
  - Renderer→device: 3 times (HudLayer 761/873/935).
  - The directory of this module: twice (FrameCaptureWin32:51, XrSessionHost:480).
- **The OpenXR loader's location is decided twice, differently:** `OpenXRPreflightWin32.cpp:311-320` falls back to `openxr_loaderd.dll`, while `EnsureLoaderPresent` falls back to the bare name. Share the path function.
- → `src/dll/EngineAccess.h`.

### B6. Engine entity and physics wrappers in three lanes (high value, none to low risk, about 180 LOC)
- CarryLane:252-462, GrenadeLane:275-473 and InteractionLane:148-227 each carry their own copies of:
  - `VCall`, `CallGetPlayer`, `CallGetEntity`, `CallEntityId`, `CallPhysics`, `CallWorldTm`
  - `CallName/ArchetypeName/ClassName`, `CallBounds`, an identical `CopyName`
  - `CallDynamics`, `SetVelocityAction` (40 B), `CallSetVelocity`
  - `PlayerEntity/Velocity/Feet`
- **Differences:** Grenade's `CallDynamics` skips mass, and its `PlayerFeet` adds a finiteness check.
- **Don't fold in** AnimIkTakeover:802 `CopyName`: it writes "?" on a fault.
- → `src/dll/EngineEntity.h/.cpp`.

### B7. XrSessionHost layer boilerplate (high value, no risk, about 230 LOC)
- **`InventorySwapchainApi{…}` initializer, 8 copies:** XrSessionHost 943, 2163, 2169, 2399, 2583, 2637, 2701; VrOptionsLayer:78.
- **The HUD upload at 2397-2426 is exactly `UploadPanelCapture` (939-969).**
- **Quad-layer fill, 8 hand-written copies** besides `FillPanelLayer` (972): 2305, 2380, 2440, 2602, 2661, 2738, the lambda at 991, and VrOptionsLayer:97.
- **Pose↔`XrPosef` written inline 17 times.** `PoseOf` (1004) and `XrInput.cpp:131 FromXrPose` already exist. → `XrPose.h`.
- **Smaller repeats in XrSessionHost:**
  - `OpticalEyes` re-inlined at 2427 and 2618.
  - Locate-views + validity check: 3 copies.
  - `GetImmediateContext`/`Release`: 11 copies.
  - The `SubmitStereoPair` per-eye block: 2 copies (1363 vs 1386).
- **The sRGB format picker is written 5 times:** XrSessionHost 517 (prefers RGBA), 2072, 2625, 2690 (prefer BGRA), and VrOptionsLayer:15. The tested `xrswapchain::SelectFormat` already covers them all.
- **Pixel and format handling:**
  - BGRA→RGBA swizzle: 2 copies.
  - GDI DIB drawing: 3 copies (UiGuide:5, SlotFeedback:26, WristPanel:7).
  - DXGI format family: 4 copies (InventorySwapchain:6, VrOptionsUi:28, DebugOverlay:460, NativeWristTexture:171).
  - The 4-format whitelist: 3 copies.
  - The 640..8192 size rule: 3 copies.
- → `PixelLayer` helper, `GdiCanvas` RAII and `DxgiFormats.h`.

### B8. Options defined in four to six places (medium value, about 50 LOC; mainly stops the copies drifting apart)
- Each VR option's default, range and fallback is written in four to six places:
  - Spec (VrOptions.cpp)
  - XrSessionHost atomic initializer and setter range check
  - UiPanel clamp
  - CommandChannel fallback
  - Lane default
- Example, ui_margin:
  - VrOptions.cpp:31
  - XrSessionHost:1094, :2968
  - UiPanel.h:31 + .cpp:117
  - CommandChannel:748
- The same pattern holds for ui_scale, curve, distance, offsets, text and the wrist settings.
- `VrOptionsUi` and `VrOptionsRuntime`'s `Wiring` table are already Spec-driven.
- **Proposal:**
  - Setters whose range equals the Spec validate through `options::Valid(id,v)`.
  - Add one test that walks `Wiring` and checks min/max and min-1/max+1, and one that checks lane initial values equal `Defaults()`.
- Keep the deliberately wider ranges: body.zone, move.scales and the reticle clamps.

### B9. CommandChannel dispatch (medium value, low risk, about 150–200 LOC)
- 168 `else if (verb==…)` branches, 96 of them shaped `Valued(args)?SetX(arg(1,d)):0`. Three helper functions exist only because the chain hit MSVC's C1061 nesting limit (see the comments at 374, 555, 579).
- **The `arg` lambda has 4 copies, plus lane copies that parse differently:**
  - CommandChannel: base 0, strict (`"010"` reads as 8)
  - ArmsLane: hex
  - Carry and Grenade: base 10, lax (`"5x"` reads as 5)
  - WristLane: `atoi` (`"x"` reads as 0)
- The console busy-retry (40×25 ms) is written twice, identically (918-939 and 1265-1286). HotkeyBridge never retries.
- → A verb table, one `ParseNumber<T>`, and `QueueConsoleCommandWithRetry` in ConsoleBridge.

### B10. HudLayer internal repeats (medium value, no to low risk, about 80 LOC)
- `CaptureNativeStatus` (367-404) and `ReplayPart` (467-503) are the same isolation transaction.
- The restore journals are identical (285-313 vs 435-466).
- **Repeated snippets:**
  - Target-redirect save/set/restore: 5 copies (373, 471, 602, 798, 907).
  - "Take a fresh capture, then reset its stamp": 5 copies.
  - `NativeTargets` re-inlined: 873-879.
  - `SetNumberGuarded`/`SetValueGuarded`, with RVA 0x18ABCC0 written twice.
  - The `CopyName` duplicate at 648.

### B11. Gameplay pose composition (medium value, low risk, about 300 LOC)
- **"Controller pose in world" from `GameplayPoseFrame`, about 9 copies:**
  - AimTakeover:118/369/399
  - AnimIkTakeover:1025/1118/1164
  - CarryLane:487, GrenadeLane:482
  - ShotRay:219/226, WeaponAttachment:131
  - PsiTargeting.h:28, DebugOverlay:650
  - → `HandWorld(frame, hand, kind, anchor, out, maxAge)`. Pass the anchor and max age explicitly: the camera-vs-eye anchor is the IK crosstalk A/B, and psi uses 100 ms.
- **The tracking↔engine mapping is re-derived inline:**
  - AnimIkTakeover:633 equals `stereo::EngineToTracking`.
  - WristLane:112, AnimIk XrMap and PhysicalInteractions:159 are forward maps.
  - `carry::TrackingToWorld` equals DebugOverlay:96.
  - DebugOverlay:92 equals InteractionLane:650.
  - → Add the inverse and direction forms to StereoCamera.h.
- **Release snapshot:** CarryLane:1051 and GrenadeLane:1122 capture it the same way, and :614 / :553 convert it the same way. The yaw fallback differs, so make it a parameter.
- **GetFiringPosition 0x1694BC0, 3 declarations:** GrenadeLane:423 (no result-pointer check), ShotRay:200 and WeaponAttachment:84.
- **ReticleFollow:192-217 re-implements `aim::ProjectToScreen`** (WeaponAim:65). The behind-the-eye epsilon differs (1e-3 vs 1e-4).
- **MoveLane:**
  - It re-implements `locomotion::StickAxis` (turn :470, fire :510) and `input::ReleaseGate` (awaitNeutral, turnBlocked, awaitTriggerRelease).
  - `VrMath ApplyRadialDeadzone` duplicates `ShapeStick` and is used only by tests.
- **SceneQuery:** the 4 wrappers repeat the guard and fault logic (SceneQuery.cpp:59/77/90/103). The fault rule appears a fifth time in PsiMedkit:265.
- **Player input and equip:**
  - The ArkPlayer input/cinematic gate and the `CanEquip`+`Equip` calls are repeated in ArmsLane, BodyEquipment, GrenadeLane, Psychoscope and PsiMedkit.
  - Each site checks with different strictness; keep that.
- **Lane-step scaffolding:** the reset-on-key-change, skip-repeated-displayTime and dt pattern repeats (BodyEquipment:113, Psychoscope:40, PsiMedkit:283, VrOptionsRuntime:320). dt is computed in about 15 places with different clamps.

### B12. Small exact duplicates (low value each, no risk)
- **`Hex()`:**
  - Identical in DebugWatch:277, IkTargetProbe:95 and InputPathProbe:73.
  - FocusHold:114 is uppercase.
- **[V] `Narrow` (Bootstrap:62) equals `Utf8` (VrOptionsUi:44).**
- **`MonotonicNanoseconds`:** LatestSnapshot.h:14 vs FrameTiming.cpp:155. FrameTiming's own comment says "one definition".
- **Repeated patterns:**
  - Atomic-max loop: 6 copies.
  - Log-once compare-exchange: 21 copies.
  - `Fixed`/`Point` formatters: 5 copies.
- **Repeated literals:**
  - Pose age `200000000`: 34 copies.
  - `100000000`: 7 copies.
  - `XrResolutionChain(4/5/14)` magic indices in 6 files.
- **Others:**
  - `LogResult` in XrSessionHost:398 and XrInput:41.
  - `ApplySavedVrOptions` and `ApplySavedPanelOptions` (VrOptionsRuntime 256/273) run the same loop.
  - The 0.6/0.35 stick hysteresis is hard-coded at VrOptionsRuntime:99 instead of using `MenuNavigatorPolicy`.
  - The TwoHandedAim squeeze hysteresis at .45/.65 duplicates `input::Pressed`.
  - The `.45f` literals at MoveLane:609 and BodyEquipment:129 equal `kGripPress.release`.
- **Bootstrap:**
  - 38 `static_cast<unsigned>(reinterpret_cast<uintptr_t>(p))` export wrappers.
  - 5 `WatchCapture` getters.
- **In-file repeats:**
  - IkTargetProbe:180/206: the same fold.
  - DualSceneStereo:630/655: identical loops.
  - HudBridge: the accessor gate 3 times, `CallHudTwoFloat`/`OneFloat`.
  - InputPost: the raw event and the disarm block, each twice.
  - UiPointer:70 vs VrOptionsRuntime:340: the same pointer visual.
  - The wrist-card size formula: DebugOverlay:198 and XrSessionHost:2607.

---

## C. Dead or superseded code (needs your sign-off; biggest single LOC win)

| Item | Approx LOC | Evidence / caveat |
|---|---|---|
| `CameraEditHook` research experiments A3/A4/A6/A7 + render-view probe (:143-236, :456-922, :935-1012, :1065-1135, :1626-2029; header :250-470; Bootstrap :512-716/:767-788) | ~1,350 | The shipped path is DualSceneStereo mode 3, and VrMode never calls these. `tools/live/preyvr-harness.js:265` still drives `SetDoubleRenderStereo`. 104 of 185 exports are unreferenced by tools or tests. If kept, there are about 120 more lines of internal repeats (watchdog arming ×4, budget check ×4). |
| `HeadTrackingHook` `CRenderView::SetCamera` hook (:65-91, :143-235, :811-841, plus 4 exports) | ~110 | The detour only counts and forwards. `gUpdateFrustum` is unused, and the outer `gApplied` is never incremented. |
| `DualSceneStereo` mode 1/2 (`BuildEyes`, `SetDualSceneIpdMillimetres`) | ~60 | Superseded by mode 3, but still reachable through the `dual.ipd`/`dual.mode` verbs. |
| **[V]** DepthSubmission (XrSessionHost: include, Host fields, extension enable, `gDepth*`, `GameDepthTexture`/`GameDepthPlanes`; `DepthSubmission.cpp/.h` + test) | ~250 | Nothing calls it. Removing it changes the instance's extension list. |
| **[V]** `FrameClock.h/.cpp` | ~140 | Tests only. |
| **[V]** `WristPanel.cpp/.h` + `WristPanelTests` + CMake 653-657 | ~79 | Removed from the DLL in 27b15fe; NativeWristTexture replaced it. |
| AimState, InteractionQuery, WrenchQuery; MotionController `WeaponPoseFromController`/`TwoHandedWeaponPose`/`MountRotationFromControllerDelta`/`SmoothTurn`; `aim::Compose` | ? | Tests and tools only. |
| Unused functions: `carry::YawOf`, WristLane `Conj`, NearViewStereo `CameraRightAxis`, `ConsumeRenderedEye`, `InputPathGEnv`, `WatchHandlerInstalled`, `gFirstQpc` (written, never read) | ~60 | |
| FOV-divergence leftovers (`gWorstDivergenceMilliTan`, `gDivergenceLogged`, the comment at 1162-1177) | ~20 | `gFovAgree`/`gFovDiverge` now count something else but are exported, so this is an ABI decision: rename them or keep returning 0. |
| `Invoke-PreyVRStartup.ps1:177-347` re-does `vr.enable` in PowerShell | ~150-200 | **[V]** The near-FOV formula is identical to `VrMode.cpp:174-175`. The script's own comment (:99-103) calls the two bring-up paths "the real defect". Medium risk: it uses a fixed 64 mm IPD and no dual stereo, and the root README documents it. |
| `preyvr-harness.js` `startVr` (:489-630) | ~140 | A third bring-up copy. It uses `r_AntialiasingMode 3` where VrMode uses 1, and `thawScene` also restores 3. |
| `Publish-PreyVR051.ps1` | 124 | A finished one-off. Its `-Publish` mode pushes and creates a release, which conflicts with the commit-local rule. |
| `graphify/run-graphify.ps1` | ~48 | A shim that drops parameters; its README is stale. |

---

## D. Tests and build

- **D1. Every test file has its own harness (high value, about 300 LOC).**
  - **Status 2026-10-18: done.**
    - `tests/TestSupport.h` holds `Require`/`Check` and has no project includes, because three tests that compile DLL sources directly have no `include/` path.
    - `tests/TestGeometry.h` holds `kDeg`, `AboutX`/`AboutY` and `AxisAngle`, and includes VrMath.
    - 67 tests take the shared assertion through a using-declaration placed where the old definition was. Nine use the geometry builders, and the Vec3 helpers in the tests now come from VrMath.
  - **Kept per file, on purpose:**
    - Every `Near`/`NearV`/`NearQuat`: the epsilon and `<` vs `<=` are each test's own.
    - The two throw-based `CHECK` macros (InventorySwapchain, NativeWrist).
    - FrustumCoverage's `Deg()`: its `x * kPi / 180` order rounds differently from `x * kDeg`.
  - The only visible change is that every failure message now starts with `FAIL: `.
  - All 69 of 69 `*Tests.cpp` define their own `Require`/`Check`/`CHECK`, with 6 stderr-prefix variants. Nothing parses the prefix.
  - Near/approx helpers have 21+ copies with **different** epsilons (1e-3, 1e-4, 1e-5) and different `<` vs `<=`. Quaternion compares handle q/−q differently.
  - Also copied: `AboutX/AboutY` (4 copies, identical), normalizing `AxisAngle` (4), and `kDeg` (4).
  - → `tests/TestSupport.h` in `preyvr::test`, with an explicit epsilon and no default; each file keeps its own `kEps`. Don't build expected values with the production `handpose::AxisAngle`.
- **D2. CMake test registration (high value, about 220 LOC).**
  - **Status 2026-10-18: done.** `CMakeLists.txt` went from 790 to 565 lines.
  - **New helpers:**
    - `preyvr_add_test(<ctest-name> <source> [TARGET][SOURCES][INCLUDES][DEFS][LIBS][ARGS])`, now used by 69 of the 70 test executables. `dll_load_fail_closed` links nothing, so it keeps its own block.
    - `preyvr_openxr_loader_copy`, and a single manifest post-build step.
    - The PowerShell tests are a `foreach`.
  - **Verification:**
    - Configuring before and after gave identical `ctest --show-only=json-v1` output, and all 109 generated .vcxproj and every other generated file matched once paths and GUIDs were normalised.
    - The loader-OFF branch was checked the same way.
  - **Not done:** the `preyvr_win32` INTERFACE target, since it would change some targets' compile definitions.
  - 54 identical 4-line blocks (362-621) and 16 Win32 variants (623-729). `preyvr_enable_warnings(` appears 80 times.
  - → A `preyvr_add_test(name src [TARGET][SOURCES][LIBS][INCLUDES][DEFS][WIN32])` function. Keep the 7 non-stem CTest names, such as `aim_state_fixture`.
  - Add a `preyvr_win32` INTERFACE target. Today the 4 tests that `#include` dll `.cpp` files compile without the `WIN32_LEAN_AND_MEAN NOMINMAX` that the DLL uses.
  - The post-build manifest is written twice (269-289 vs 291-307), and the loader copy three times.
- **D3. Fixtures.**
  - `BuildSyntheticSupportedImage` is identical in EngineMapTests:18 and RuntimeBootstrapTests:21.
  - WARP device + staging readback appears 5 times across 3 files.
  - The log-capture stub is identical in CullCameraWatch and FocusHold tests.
  - HandCarryTests:318-500 tests `use::UseButton`; move it to InteractionUseTests.
- **D4. PowerShell launcher tests.** The 14-line preamble is repeated 3 times, and the assert-throws pattern 6 times. → `tests/LauncherTestCommon.ps1`. The temp directories under `build/` are never cleaned up.
- **D5. `tools/re/vr_scheme_review` is a separate CMake project.** It hard-codes the minhook path and never runs in ctest. Fold it into the root build as tests.
- **Side notes:**
  - `tests/README.md` says "eleven" targets; there are about 75.
  - `test_extract_native_hud.py` isn't wired into CTest.

## E. Tools and scripts

- **E1. Command-channel client and run-dir lookup (high value, low risk, about 100 LOC).** The run-dir bug from A10 is already fixed in place in three scripts; this item is now only the consolidation.
  - Sending a command (write `commands.txt`, poll `results.txt`) has 5 copies: Startup:133, Send-PreyVR:50, ControlDemo:77, HudValidation:82, IkTest:47.
  - The run-dir lookup has 5 copies; only Send-PreyVR's handles both folder layouts.
  - Report-field parsing has 3 variants.
  - → `tools/PreyVRChannel.ps1`. Send-PreyVR ships in the package, so either add the module to the package or leave Send-PreyVR self-contained.
- **E2. Python PE parsing (high value, about 220 LOC).**
  - The same e_lfanew → sections → `at(rva,n)` parse is written 18 times.
  - FNV-1a64 appears 5 times, the `*Native.h` table regex 5 times, reloc parsing twice (word for word), C++ byte-array reading twice, rel32 decoding 5 times, and the hash literal 5 times.
  - `verify_h005_skinning.py` has become a shared module by accident: 16 scripts import from it.
  - → `tools/re/preydll.py` with a `resolve_module()` that tries the env var, then D:, then E:.
  - Re-export `DEFAULT_MODULE` from it: the pinned scripts import that name, so they pick up the fixed path without their own bytes changing. Migrate only the unpinned scripts.
  - `verify_h021_static.verify(path, spec_json)` is already a data-driven verifier that h005/h005b/h013/h022/vr_scheme could use.
- **E3. Path resolution.**
  - Build tree, game path and xr-sim root are each resolved in 6–8 scripts with contradictory defaults.
  - The game-path registry lookup exists once, in `Start-PreyVR:124`; the rest hard-code `D:\SteamLibrary`.
  - → `Resolve-PreyVRArtifact` / `Resolve-PreyInstall` / `$PreyVRXrSimRoot`.
  - Done so far (A10): the Launch and AimCheck build-tree fallback, and the Python module resolver. Note that Steam's uninstall registry key for Prey is empty on this machine, so `Resolve-PreyInstall` needs Steam's `libraryfolders.vdf` as a fallback, the way the Python resolver does.
- **E4. Launchers.**
  - Start-PreyVR and Invoke-PreyVRLaunch share their core: ProcessStartInfo, SteamAppId, PREYVR_LOG_PATH, run folder, injector call. Share an `Invoke-PreyVRInjector`, but keep both entry points.
  - xr-sim env setup is done two different ways (UnderXrSim:76-122 vs Launch:178-192, which hand-builds the layer env). The xr-sim liveness check is written 3 times.
- **E5. Probes.**
  - Instance creation with newest-API-first appears 3 times: xr_adapter_probe:186, xr_session_probe:154, XrSessionHost:596.
  - The LUID→adapter walk appears 3 times.
  - Two different XrResult name tables.
  - → A header-only `XrProbeCommon.h`, for the probes only.
- **E6. [V] Committed `.pyc` files.**
  - The three files under `tools/re/__pycache__/` are tracked in git, and there is no ignore rule.
  - Six scripts set `sys.dont_write_bytecode` as a workaround.
- **E7. Minor repeats.**
  - frame_diff wrappers: twice (IpdSweep vs StereoAnalysis).
  - AimCheck internals: the reading lookup 3 times, the frame wait twice.
  - preyvr-harness.js: `callTolerant` 79 times.
  - Graphify-Update-All/CodeOnly: the updater path twice.
  - tools/README omits most of the tools and doesn't mark which is current.

---

## F. Looks duplicated but should NOT be merged

- **Memory reads:**
  - `RuntimeSnapshotWin32.cpp:19 PageIsReadable` (a VirtualQuery walk) vs the SEH reads: the walk never touches the page, so it cannot consume a PAGE_GUARD bit. It can live in SafeMemory.h, but must keep its semantics.
  - Composite single-`__try` blocks (CullCameraWatch ReadPool/ReadQueue, DualSceneStereo BuildEyes, FocusHold, NearFovOverride ApplyToRenderer, HudDispatchPointer) are all-or-nothing on purpose.
- **Engine-call and verification policies:**
  - SEH-guarded engine calls (HudBridge, BodyFollow) vs NearFovOverride's "never guard the engine's original": decide the policy before writing a generic guarded-call helper.
  - Full-body FNV hashes vs prologue memcmp are deliberately different strengths. Share the implementation, not the policy.
  - Feature-local gates deliberately kept out of EngineMap: ConsoleBridgeWin32:66, IkTargetProbe:29, CullCameraWatch `kCode`, FocusHold.
  - The three routes to the renderer (gEnv+0x120, the singleton RVA, system+0x28) are distinct globals.
- **Stereo and camera math:**
  - The per-eye offsets in CameraEditHook (camera translation), NearViewStereo (VP row) and NearParticleStereo (particles) are in different spaces.
  - DualSceneStereo BuildEyes vs BuildSyntheticEye: one keeps the exact rotation bits, the other does a Pose round-trip.
  - FOV structs: `EyeFovAngles` stores down before up, `xr::FovAngles` up before down. A careless merge swaps them.
- **Gameplay policies:**
  - The legacy HandRigTakeover/WeaponAttachment yaw (`CameraYawOf − ReferenceYaw`), the IK anchor A/B and the three torso estimators.
  - Reach modes: SoftReach vs ScaleReach/ClampToReach.
  - Release models: carry gain/cap vs grenade parity/lever.
  - Wrist visibility: legacy grip card vs wrist::Presenter.
  - The per-state-machine press latches.
- **Numeric helpers with different edge behaviour:**
  - Yaw and wrap: `armpose::YawOf` (Y-up), `carry::YawOf` (Z-up, unused), the `Wrap`/`WrapAngle` variants, and the RecenterYaw 0.087 / Holster 0.2 thresholds. They differ in NaN handling and in degeneracy when looking straight up or down. Share only the identical core.
  - Test epsilons and `<`/`<=` are real per-test choices.
- **UI and input timing:**
  - MenuNavigator repeat (0.45/0.16) vs VrOptionsUi adjust repeat (0.4/0.1): different feel, on purpose.
  - Options quarantine vs PlayResume vs ModalMenuRouter wait for neutral at different scopes.
- **Rendering, frames and timing:**
  - `D3D11SavedState` vs NativeWristTexture `ComputeState`: compute-stage state with a hazard-safe restore order. At most, co-locate them.
  - LatestSnapshot, the RenderFrameTable seqlock, EventQueue and RenderContractQueue are different concurrency contracts. Note that the seqlock reads plain floats racily.
  - RenderFrame/RenderFrameTable, StereoFrame, XrFrameContract and RenderContract are distinct modules; only the constant and FOV structs overlap.
  - FrameTiming, FrameClock and the XrHold StallTracker are different concepts; only the clock function is a true duplicate.
- **Scripts:**
  - Start-PreyVR's inline logic.
  - Receipt-pinned verifiers, and the per-verifier mutation controls and claim wording.
  - Run-Headless vs UnderXrSim (build+ctest vs runtime wrapper).
  - xr_adapter_probe vs xr_session_probe as binaries: share code, keep both.

## Suggested order
1. ~~Fix A1–A6.~~ Done on 2026-10-17 (A1–A8 and A10); awaiting a headset run. B2 should keep these fixed failure paths.
2. Header-only, zero-behaviour work:
   - ~~Land these together: B1 (VrMath) + D1 (TestSupport.h).~~ Done on 2026-10-18.
   - Then B3 SafeMemory.h, B4 NativeContract.h + FindLandmark + layout aliases, and B5 EngineAccess.h.
3. ~~Build: D2 `preyvr_add_test`.~~ Done on 2026-10-18.
   - Note: `tools/re/vr_scheme_review` (D5) already fails to link on its own: about 65 unresolved DLL-lane symbols, none of them B1's. It needs folding into the root build, or an updated source list.
4. Prune dead code (C). Keep the CameraEditHook experiments (user, 2026-10-18); the rest still needs sign-off.
5. Tools: E6 (`git rm --cached` the .pyc files and add an ignore rule), then E1, E2 (unpinned scripts only) and E3.
6. Larger refactors that need a headset run afterwards: B6, B7, B9, B10, B11.

Rough totals (reviewer estimates):
- C++ consolidation: about 2,000 LOC.
- Dead code: about 2,300 LOC, mostly the CameraEditHook experiments.
- Tests and build: about 600 LOC.
- Tools: about 900–1,100 LOC.
