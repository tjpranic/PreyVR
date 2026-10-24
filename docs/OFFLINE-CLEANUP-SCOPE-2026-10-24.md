# Offline cleanup batch (batch A): scope

Status: **built 2026-10-24 as PreyVR-67, and worn OK (10-24); not committed.** What was built, and where it differs from the plan, is in [Done](#done-2026-10-24) at the end; the plan below is as scoped. This is the T1 remainder of the [refactor audit](REFACTOR-AUDIT-2026-10-22.md), after the [FrameSchedule](FRAME-SCHEDULE-SCOPE-2026-10-24.md) and [Hands](HANDS-SCOPE-2026-10-24.md) batches. Everything here is checked by the build and ctest, without a headset. One package goes out at the end for a short smoke run, because phase 1 edits `ServiceXrFrame` and phase 2 touches every lane's initial value.

It covers:
- audit section 3's open T1 items (the XrSessionHost extractions, the options groundwork, the moves and renames, the `log [0|1]` helper, the ring search, D3/D4, and the verifier runner);
- the S6 renames that need no headset;
- S7's ring-search fold.

**Behaviour-identical throughout:** the same float operations in the same order, the same log lines, verbs, replies, ini keys and export names. Nothing in this batch is T3.

## What is there today

### Decisions written inline in `ServiceXrFrame` (XrSessionHost.cpp, 3,438 lines)
| Site | Lines | What it decides | Test today |
|---|---|---|---|
| `PlaceMenuPanels` | 1404-1424 | The menu and guide-card stack: up to 80 attempts, each 5% smaller, until both are visible | none |
| `wantPanel` | 2123-2124 | Whether the game menu is shown on a panel: `ui.panel` mode 2 always, mode 1 while a menu is open (or was, while the state is unknown) | none |
| The fade | 2747-2752 | The head-in-wall fade: up in 0.1 s, down in 0.2 s, dt capped at 0.1 s, quantised to 8 levels | none |
| The wrist gate | 2606-2609 | Nine conditions packed as bits by hand; `wrist.report gate=` prints the number, and 511 means open | none |
| `ScaleDeclaredFov` | 1127-1143 | The `xr.mirrorfov` percentage, scaling each tangent (not the angle) | none |
| The runtime IPD | 2076-2079, 2842-2846 | The distance between the two views, computed twice per frame from the same views | none |

The second IPD sits behind `inventoryPrepared`, which needs `panelActive`, which needs `haveViews`. So both computations always see the same located views, and computing once is exact.

### Options: the default and range written in several places
- The 66 Specs (VrOptions.cpp) are in an anonymous namespace, so no other file can name a default at compile time.
- VR start (VrMode.cpp:228-259) restates 6 Spec defaults as literals: carry hand, grenade hand, arms free, aim lasers, HUD layer and controller prompts.
- VrOptionsRuntime stores 5 settings itself, initialised by hand. One comment says "wrist: options::Defaults()".
- Each lane initialises its own copy, in its own units. Nothing checks that a lane accepts the range its Spec offers. A lane narrower than its Spec would refuse a menu value silently.
- `vr_options_runtime` stubs every lane, so it checks the wiring, not the lanes.

### Misleading names and homes
| Today | What it is | Users |
|---|---|---|
| `InputPathProbe.{h,cpp}` | The production input resolver; `InputPost` needs it | 4 includes |
| `HudBridge.h` (207 lines of Scaleform notes) | 19 of its 26 includers want only the menu-state gate (`HudGameplayInputAllowed` and its siblings) | 19 |
| `BindingDump.{h,cpp}` | Also hosts the production `PreyWheelOpen`, which MoveLane reads every frame | 3 |
| `HotkeyBridge.{h,cpp}` | The Ctrl+Alt research hotkeys; not a bridge | 2 |
| `LastRenderedEye()` | The last eye *built* | 3 (the export keeps its name) |
| ArmsLane.cpp | Hosts the raw-memory research verbs: `mem.peek/scan/deep/watch/hits/stack/str` and `arms.att/hide/joints` | the channel |
| The same thread | Called "game thread", "input-drain thread" and "Prey's main thread" in comments | about 10 files |

### Small copies
- **EyeRecords:** `FindBuiltEyeByPosition` and `IsDualFrameCentre` are the same newest-first ring search with the same tolerance.
- **The `log [0|1]` verb parse:** written twice, for `contact log` and `swing log`.
- **Test fixtures:**
  - `BuildSyntheticSupportedImage` is copied identically in EngineMapTests and RuntimeBootstrapTests (D3).
  - The `use::UseButton` tests live in HandCarryTests (D3).
  - The three launcher tests repeat the same 9-line preamble (D4).
- **The 26 `tools/re` verifiers:** nothing runs them. On this machine, 25 pass in under a second each. `verify_inventory_stereo` needs `pefile` and `unicorn`.

## Phases

All four phases go out as one package.

### 1. The XR session's decisions, pure and tested
Each one moves into the pure header it belongs with, gets a test, and is called from XrSessionHost in its place.
- **`ui::FitMenuStack`** (UiPanel.h): the 80-attempt shrink of the menu and guide card. The test covers the guide on and off, the shrink, and the refusal.
- **`ui::WantMenuPanel`** (UiPanel.h): the panel-mode decision, tested across the mode, view and menu-state table.
- **`ui::StepFade`** (UiPanel.h): the fade's alpha and level. The test covers the ramp times, the dt cap and the quantisation.
- **`wrist::Gate`** (WristHolo.h): the nine conditions as named fields, and `Bits()` with the same bit order. The test pins each bit's value, so `wrist.report gate=` reads the same.
- **`stereoframe::ScaleFovAngle`** (StereoFrame.h): the tangent scaling, tested at 100% (identity) and in tangent space. `ScaleDeclaredFov` stays as the XrFovf adapter.
- **The runtime IPD:** computed once, where the views are located, and reused for the inventory consumer.

### 2. Options defined once
- **`include/preyvr/VrOptionsSpec.h`:** the Spec table as `inline constexpr`, with `constexpr int options::Default(Setting)`. VrOptions.cpp keeps `Describe`, `Valid` and the rest, reading the same table.
- **VR start's 6 literals** become `options::Default(...)`. The values are the same today, and they follow the Spec from now on.
- **VrOptionsRuntime's 5 own stores** initialise from `Default`.
- **Lanes whose initial value is the Spec default, in the Spec's own unit,** initialise from `Default`. Lanes that start elsewhere keep their value, and the test below lists them by name:
  - some start off until VR start turns them on, such as the HUD layer;
  - some store another unit.
- **New ctest `vr_options_wiring`:** VrOptionsRuntime's real Wiring table against the real lanes, using the `command_channel` link pattern (offline, PreyDll never loaded, MinHook stubbed). For each of the 66 settings, it checks:
  - the lane's initial value against the Spec default, with every known difference listed and explained;
  - that the Spec's minimum, maximum and default each read back after being applied.

  Settings whose setters install hooks cannot run offline. They are listed and skipped.

### 3. Names and homes
Only C++ identifiers and file names change. Export names, verbs, `detail=` strings, log prefixes, ini keys and environment variables keep theirs.
- `InputPathProbe` → **`InputPath`**: a file rename; its functions are already named `InputPath*`.
- **`UiMode.h`:** the menu-state declarations, out of HudBridge.h. HudBridge.h includes it, and the 19 gate-only files include `UiMode.h` instead. The definitions stay in HudBridge.cpp, next to the main-thread poll that writes them, and the signatures are unchanged for the 8 tests and 2 replays that stub them.
- **`PreyWheel.{h,cpp}`:** `PreyWheelOpen` out of BindingDump. Its `preyvr_bindings` log lines are unchanged.
- `HotkeyBridge` → **`ResearchHotkeys`**.
- `LastRenderedEye()` → **`LastBuiltEye()`**; `PreyVR_GetLastRenderedEye` keeps its name.
- **`MemoryCommands.cpp`:** the raw-memory research verbs, out of ArmsLane. ArmsLane keeps the holster and draw lane, `arms.free`, `arms.draw` and `arms.report`. The unknown-verb reply for `mem.*` and `arms.*` stays the same.
- **The thread:** "Prey's main thread" in every comment.

### 4. Small shared code and test plumbing
- **`ring::FindNewest`** (a pure template header, with a test) under both EyeRecords searches. The miss counter stays in `FindBuiltEyeByPosition`.
- **The `log [0|1]` parse:** one helper for `contact log` and `swing log`. The `command_channel` test pins both replies.
- **D3:** `BuildSyntheticSupportedImage` moves into one test header. The `UseButton` tests move from HandCarryTests to InteractionUseTests.
- **D4:** the launcher tests' preamble becomes one dot-sourced helper.
- **New ctest `re_verifiers`:** `tools/re/run_verifiers.py` runs every `verify_*.py` against the installed PreyDll.dll.
  - It skips the whole test (ctest's skip code) when there is no game binary.
  - It skips one verifier, with the module named, when that verifier's Python modules are missing.
  - The pinned verifiers are run, never edited.

## Not in this batch
- **`InventorySwapchain` → a layer-swapchain name:** with B7's swapchain factory in the XR batch, which rewrites the same call sites.
- **AnimIk's `RigSide` enum:** a wrong side compiles and runs, and AnimIk has no offline test. It goes in a headset batch.
- **DualOverlay's name:** its environment variable, log prefix and report keys all say "dual overlay", so a C++ rename would leave the name split.
- **`SetCarryHand`/`SetGrenadeHand`:** left for your throwing redo.
- **Validating every setter through the Spec, and the verb fallbacks (A9):** these change what some commands refuse (T3). They go with the UI batch's verb table.
- **The Spec flags (`early`, `vrStart`, `dev`):** they belong to the instrumentation gate design (audit section 4, item 8).
- **`SubmissionEye()`:** `DualEyeSubmissionActive` has one caller, which needs only the bool, so there is nothing to share.
- **The XrInput `strncpy` warnings:** on the OpenXR action setup (T2), so they go in the XR batch.
- **The WARP device fixture:** one of its three copies is in the receipt-pinned NativeWristTests.

## Verification
- **Each phase:** build, full ctest (94 tests before; `vr_options_wiring` and `re_verifiers` are new), no new warnings, and the 75 export names checked against `exports-63.txt`.
- **Phases 1 and 4:** the moved code is compared line by line with what it replaced, and every float expression keeps its order.
- **Headset smoke, one package:**
  - the menu panel and guide card (placement, the curve, `ui.offset`);
  - the head-in-wall fade, up and down;
  - the wrist card, including `wrist.report gate=`;
  - the inventory layer;
  - a saved VR Options file applying at VR start, and each Hands-tab toggle;
  - movement and the wheel (PreyWheelOpen);
  - `mem.peek dll` and `arms.report` from the tuning script.

## Done 2026-10-24

Build passes, 97/97 ctest pass (`vr_options_wiring`, `position_ring` and `re_verifiers` are new), no new warnings, and the DLL's 75 export names are unchanged. No receipt pin is staled; PsiMedkit.cpp's was stale already. **Worn OK on PreyVR-67 (10-24).** Not yet committed. `git mv` staged the four file renames; everything else is unstaged.

**Phase 1: the XR session's decisions.**
- **UiPanel.h:** `FitMenuStack`, `WantMenuPanel` and `StepFade`. The Host's fade state is now a `ui::Fade`, and the fade has 8 levels (`ui::kFadeLevels`).
- **WristHolo.h:** `wrist::Gate`, the nine conditions by name; `Bits()` keeps their order.
- **StereoFrame.h:** `ScaleFov` on `EyeFovAngles`. `ScaleDeclaredFov` is now its XrFovf adapter.
- **The runtime IPD** is computed once, where the views are located.
- **Tests:**
  - `ui_panel` and `stereo_frame` compare the menu stack, the panel decision, the fade and the FOV scaling bit for bit with the code each replaced.
  - `wrist_holo` pins each gate bit.
- **One read moved:** `HudMenuIsOpen()` is now read every frame, where before it was read only while the menu state was known. It is an atomic load, so nothing observable changes.

**Phase 2: options.**
- **`options::Default(id)`**, which VR start's six literals now use.
- **`vr_options_wiring`** checks the real lanes and found:
  - every lane but six starts at its Spec default, and those six are exactly the ones VR start turns on;
  - every lane takes its Spec's whole range, except eight whose setters need Prey: Psi targeting, the medkit slot, shots from the muzzle, the HUD layer, the grenade and carry hands, the inventory layer and its stereo;
  - no setter tried to install a hook.
- **Where this differs from the plan: the lanes keep their own initial values.**
  - Most store their setting in another form: bools, an inverted flag (`wrist_on_hud`), milli-degrees, or metres as floats.
  - Starting them from the Spec would need a conversion at each lane. For the floats it would also change the value: 56 × 0.01f is not the float 0.56f.
  - The wiring test now holds every lane to its Spec instead.
  - Nothing needed a compile-time default, so the Spec table stays in VrOptions.cpp and there is no VrOptionsSpec.h.

**Phase 3: names and homes.**
- **Renamed files:** InputPathProbe → InputPath and HotkeyBridge → ResearchHotkeys. Their `preyvr_input_path` and `preyvr_hotkey` log prefixes are unchanged.
- **BindingDump → ActionMaps, renamed rather than split** (this differs from the plan).
  - `PreyWheelOpen` and the binding dump read the action maps through the same guarded reader, and splitting them would have copied it.
  - The `preyvr_bindings` lines are unchanged.
- **UiMode.h:**
  - 17 files include it instead of HudBridge.h.
  - ContactLane, SurfaceStopLane and SwingLane dropped an unused HudBridge.h include; they gate through PhysicalLane.h, which includes UiMode.h.
- **`LastRenderedEye()` → `LastBuiltEye()`.** Its doc now also names the both-eyes value. The export keeps its name.
- **MemoryCommands.{h,cpp}:** the seven `mem.*` verbs and `ResolveAddress`.
  - **`arms.att`, `arms.hide` and `arms.joints` stay in ArmsLane** (this differs from the plan). They read the arms character's attachments with the same reader as `AttachmentJoint`, which the torso lift uses.
  - The guarded text read that both files use is now `SafeText` in SafeMemory.h.
  - `command_channel` now also pins `mem.bogus`.
- **The thread:** 67 comment mentions in 40 files now say "main thread". "A game thread" and "the game threads", meaning any of the game's threads, are left as they were.

**Phase 4.**
- **`ring::FindNewest`** (preyvr/PositionRing.h) now runs both EyeRecords searches. `position_ring` compares it with both old loops in every case: counts past two laps, a stale slot, every position.
- **`EventLogSwitch`** parses `contact log` and `swing log`. `command_channel` checks on, off, a malformed value and another second word.
- **D3:**
  - tests/TestEngineImage.h holds the shared image.
  - The nine UseButton tests moved to InteractionUseTests, with their fixtures renamed HoldBase and HoldReady.
- **D4:** tests/LauncherTestSupport.ps1 holds the launcher tests' shared preamble.
- **`re_verifiers`:**
  - 25 verifiers pass; `verify_inventory_stereo` is skipped because this machine has no pefile.
  - With no game binary, the whole test is skipped (exit 77).
- **Docs:** TUNING-COMMANDS.md's `wrist.report` row lists the gate bits.

**Headset smoke for PreyVR-67:** the list under Verification above.
