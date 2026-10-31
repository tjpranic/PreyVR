# Source

`PreyVR.dll` is injected into a running copy of Prey (2017) and turns it into an OpenXR game. It does not replace the engine. It hooks Prey's own functions, gives them the headset's camera and the controllers' poses, and leaves simulation, rendering and UI to Prey wherever it can. If it cannot recognise the game build, it installs nothing and Prey runs as a normal desktop game.

This file maps the code as it stands. [`docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md) records how each stage was proved and why the design took this shape.

## Terms

The code and its comments use these words throughout.

- **Lane:** one feature's runtime code: what it does each frame, how it turns on and off, and what it reports. Many are named `...Lane`.
- **Seam:** the engine function a feature attaches to.
- **Gate:** the check a feature must pass before it hooks or calls the engine. If the check fails, Prey's own behaviour stays.
- **Contract:** one row of a gate: an address, a size and a hash of that function's bytes.
- **Pure code:** code with no Windows, D3D11, OpenXR or engine access, so it can be tested without the game.
- **Both-eyes frame:** a frame in which Prey's one render view is drawn twice, once per eye.

Comments also cite records in `docs/`: `R-nnn` is an address in [`ADDRESS_REGISTRY.md`](../docs/ADDRESS_REGISTRY.md), `F-nnn` a failure in [`FAILURE_REGISTRY.md`](../docs/FAILURE_REGISTRY.md), and `H-nnn` a hypothesis in [`HYPOTHESES.md`](../docs/HYPOTHESES.md). Words such as *rung*, *hurdle*, *M2* and *phase* are stage names from those records and the scope documents.

## Layout

The code is grouped by feature: 352 files and about 67,000 lines (as of 2026-11-01). Headers sit beside their sources. Each top-level folder has a folder of the same name under `tests/`, apart from `app/`.

| Folder | What it owns | Start with |
| --- | --- | --- |
| `app/` | The DLL's entry point, the load gate, VR start and stop, the frame schedule, the worker thread and the `PreyVR_*` exports. | [`FrameSchedule.h`](app/FrameSchedule.h), [`VrMode.cpp`](app/VrMode.cpp) |
| `base/` | The logger, vector math, the clock, the latest-value box and small helpers. | [`LatestSnapshot.h`](base/LatestSnapshot.h) |
| `engine/` | Reaching Prey: memory layouts, landmarks, byte contracts and guarded reads. `hooks/` installs detours and holds the two frame hooks, `console/` types allowlisted console commands, and `fixes/` corrects engine behaviour that VR exposes. | [`EngineMap.h`](engine/EngineMap.h), [`NativeContract.h`](engine/NativeContract.h), [`HookInstall.h`](engine/hooks/HookInstall.h) |
| `xr/` | The OpenXR session on Prey's own D3D11 device: frames, swapchains, composition layers, hold frames and timing. | [`XrSessionHost.h`](xr/XrSessionHost.h), [`XrSessionState.h`](xr/XrSessionState.h) |
| `render/` | `camera/`: head tracking and per-eye cameras. `stereo/`: both-eyes submission, and fixes for what breaks in stereo (near pass, particles, markers, stars, video, body depth). `taa/`: temporal AA per eye. | [`HeadTrackingHook.h`](render/camera/HeadTrackingHook.h), [`DualSceneStereo.h`](render/stereo/DualSceneStereo.h) |
| `aim/` | The controller as the aim: the gameplay seam, the frame it builds, the muzzle and shot ray, and the two-handed hold. | [`AimTakeover.h`](aim/AimTakeover.h), [`GameplayFrame.h`](aim/GameplayFrame.h) |
| `body/` | The player's body: torso, arms, hands and room-scale. `ik/` drives the arms through Prey's animation IK. | [`TorsoLane.h`](body/TorsoLane.h), [`AnimIkTakeover.h`](body/ik/AnimIkTakeover.h) |
| `controls/` | `input/`: OpenXR actions, posting events into Prey, haptics and prompts. `locomotion/`: moving, turning and zero-G. `menus/`: controller menu navigation. | [`XrInput.h`](controls/input/XrInput.h), [`InputPost.h`](controls/input/InputPost.h), [`MoveLane.h`](controls/locomotion/MoveLane.h) |
| `equipment/` | What the player wears or draws. `body/`: holsters and the medkit slot. `head/`: head lamp and helmet. `psi/`: psi targeting, medkit and psychoscope. `weapon/`: the weapon draw skip and its sounds. | [`BodyEquipmentLane.h`](equipment/body/BodyEquipmentLane.h), [`WeaponDraw.h`](equipment/weapon/WeaponDraw.h) |
| `physics/` | Hands in the world. `contact/`: the weapon box, pushing objects, scene queries and surface stop. `hands/`: use, carry and grenades. `swing/`: the free-swing wrench. | [`ContactLane.h`](physics/contact/ContactLane.h), [`CarryLane.h`](physics/hands/CarryLane.h) |
| `ui/` | `hud/`: the floating HUD, reticle and wrist hologram. `options/`: VR Options. `screens/`: the menu panel, laser pointer, menu state, loading screen and examine camera. | [`HudLayer.h`](ui/hud/HudLayer.h), [`UiMode.h`](ui/screens/UiMode.h), [`VrOptions.h`](ui/options/VrOptions.h) |
| `diagnostics/` | `capture/`: frame dumps, hardware breakpoints and the stack overflow dump. `commands/`: the text-file command channel. `overlay/`: the debug overlay. `probes/`: read-only measurements. | [`CommandChannel.h`](diagnostics/commands/CommandChannel.h) |

**Folders are a map, not layers.** `base/` includes nothing else from `src/`. Every other folder depends on the rest in one include cycle: `engine/hooks/` runs the schedule in `app/`, for example, and `app/` includes nearly everything. Do not assume a folder is lower-level than another.

## Pure code and DLL code

Both kinds sit side by side in the same folders, so a feature is usually split in two: pure rules in `SwingStrike.cpp`, say, and the engine half in `SwingLane.cpp`.

A file is DLL code when `CMakeLists.txt` lists it in a DLL target or it declares `namespace preyvr::dll` or `preyvr::lifecycle`. Everything else is pure. Pure files may include only pure files and no platform headers, and the `pure_boundary` test fails if one does.

| Target | What it is |
| --- | --- |
| `preyvr_core` | Static library of the pure `.cpp` files. A new pure `.cpp` must be listed here. |
| `preyvr_win32_bootstrap` | Two read-only startup helpers: the runtime snapshot and the OpenXR preflight. |
| `preyvr_dll_objects` | Object library of the DLL's sources. `PreyVR.dll` links all of them in list order, and the `command_channel` test links all but four. |
| `preyvr` | `PreyVR.dll`: those objects plus MinHook, ImGui and the OpenXR loader, which CMake fetches. |

The build is C++20. Includes are rooted at `src/`, even within a folder: `#include "physics/contact/ContactLane.h"`. `Version.h` is generated from `cmake/Version.h.in`. DLL code lives in `preyvr::dll`, and pure code in short per-feature namespaces such as `preyvr::swing`.

## From injection to VR

**Load.** Nothing here touches the game unless the checks pass ([`Bootstrap.cpp`](app/Bootstrap.cpp)).
1. `DllMain` only starts a thread, so no work runs under the loader lock.
2. Every landmark (a known byte pattern at a known address) is compared against `PreyDll.dll` in memory. On any mismatch the mod logs `status=unsupported` and stops, with no hooks and no worker thread.
3. It records a read-only snapshot of engine state, runs the OpenXR preflight (which starts nothing), and arms the stack overflow capture.
4. It starts the worker thread and pins the module, so the DLL stays loaded until the process exits.

Prey is still a desktop game at this point. The hooks exist but are off.

**VR start.** F11 or the `vr.enable` command calls `EnableVrMode`, and `TickVrMode` on the worker thread then steps through these phases ([`VrMode.cpp`](app/VrMode.cpp)):

| Phase | Waits for | Then |
| --- | --- | --- |
| `preparing` | Prey's backbuffer and input path | Starts the OpenXR session and controller actions. |
| `settings` | Renderer settings accepted through the console and read back | Moves on to tracking. |
| `tracking` | A fresh head pose | Arms stereo and every lane, then applies saved VR Options last, so they win over the defaults. |
| `rendering` | Three submitted frames | Reports VR as active. |
| `active` | Nothing | Logs a status line each minute, and keeps IK calibration and the near FOV in step. |
| `failed` | F11 | `Fail` stops everything and logs which step failed. |

Failures come in two kinds. Without stereo, tracking or the core motion controls, VR cannot run, so VR start fails. An extra feature that refuses (markers, stars, helmet, head lamp and so on) logs the refusal and leaves Prey's own behaviour in place.

## One frame

[`FrameSchedule.h`](app/FrameSchedule.h) and [`FrameSchedule.cpp`](app/FrameSchedule.cpp) are the reference: they say what runs on which thread and why the order matters. A few stereo fixes keep their own hooks outside the schedule, because their placement is the point.

| Thread | Runs from | Work |
| --- | --- | --- |
| Main | Prey's cached aim ray update, `CSystem::Render` | Gameplay lanes, input drains, the camera edit. |
| Animation | `ProcessAnimationDrivenIK` | Arm and body IK. Its place in the frame varies. |
| Render | `RT_RenderScene`, `RT_EndFrame` | The eye submissions and the whole OpenXR frame. |
| Worker | The mod's own loop, 50 Hz | VR start, options, focus. The command file is polled at 5 Hz. |
| Hold | The mod's own loop | Submits frames while Prey's render thread is stalled or loading. |

**The main thread's frame:**
1. `ArkPlayer::Update`: a queued snap turn lands and the torso is estimated.
2. `ArkPlayerCamera::UpdateView`: the head pose goes into the view.
3. The aim seam: `AimTakeover` builds a `GameplayPoseFrame` (one tracking sample plus the engine's camera and yaw), lets the stages that change it run, publishes it, and runs the gameplay lanes on it in order.
4. `CSystem::Render`: input and HUD queues drain, the move and turn lanes post their events, the view camera is edited for this frame, and Prey renders. The camera is restored afterwards.

**The render thread's frame**, about a frame behind:
1. `DualSceneStereo` submits the one render view twice, with a camera per eye. The first eye is copied to a held image between submissions.
2. `RT_EndFrame` runs the XR frame (`ServiceXrFrame`): it pairs the held eye with the backbuffer, which holds the second eye, using the frame's render contract, adds the composition layers and submits.

The whole OpenXR frame runs at `RT_EndFrame`, because that is the only place Prey's backbuffer is valid. The same frame syncs the controllers and publishes the `TrackingFrame` the main thread reads. The menus, HUD, reticle and wrist hologram reach the headset as OpenXR layers of their own (`XrMenuPanel.cpp`, `XrOverlayLayers.cpp`). Loading screens need their own hook, because Prey's load loop presents without `RT_EndFrame` ([`LoadingScreen.h`](ui/screens/LoadingScreen.h)).

## Sharing data between threads

Engine threads must never wait on the mod, so data crosses threads by being published.

- **[`LatestSnapshot`](base/LatestSnapshot.h)** holds the latest value of something. A reader that meets contention gets nothing rather than blocking.
- **Samples carry their time.** Consumers refuse a stale one ([`MonotonicClock.h`](base/MonotonicClock.h)).
- **The render contract and eye records** ([`RenderContract.h`](render/camera/RenderContract.h), [`EyeRecords.h`](render/camera/EyeRecords.h)) carry what the main thread built to the render thread. Consumers match them by content, such as the camera position, and not by a "current eye" flag: the render thread is a frame behind, so a flag would describe the wrong eye (H-022).
- **Input goes through a queue** ([`InputQueue.h`](controls/input/InputQueue.h)). Any thread can produce an event, but only the main thread posts it into Prey.
- **Menu state** ([`UiMode.h`](ui/screens/UiMode.h)) is sampled on the main thread and read as plain values from anywhere. Every input lane gates on it.

## Touching the engine safely

Every address in the tree is for one build, Steam's `PreyDll.dll` with SHA-256 `7D6E322F...B05311A7`.

- **Named addresses:** shared layouts live in [`EngineMap.h`](engine/EngineMap.h). A feature's own addresses are named constants in its `*Native.h` or at the top of its `.cpp`.
- **Byte contracts:** newer lanes list the functions they use in a `*Native.h` and call `contract::BodiesMatch` before they hook or call one. `static_assert(contract::Gated(...))` proves that a function called through a named constant is in the table that was checked. Some earlier seams, such as `AimTakeover`, compare the function's opening bytes instead.
- **Static verifiers:** `tools/re/verify_*.py` check the same facts against `PreyDll.dll` on disk, and ctest runs them as `re_verifiers`. The load gate compares landmark bytes, not the file hash. The hash is checked by `tools/re/preydll.py`.
- **Guarded memory:** read engine memory through [`SafeMemory.h`](engine/SafeMemory.h), so a bad pointer fails a read and does not crash the game.
- **Hooks:** install every detour through [`HookInstall.h`](engine/hooks/HookInstall.h), which orders the steps and cleans up after a failure.
- **Drawing:** a mod draw on Prey's D3D11 context restores all state with [`D3D11SavedState.h`](base/D3D11SavedState.h).
- **Shutdown:** a global that holds a runtime object uses [`NeverDestroyed.h`](base/NeverDestroyed.h), because destroying it at process exit crashes.

Before adding a hook, look for something the engine already honours: a callback, a cvar or its own input path. Lanes puppeteer the engine, so `MoveLane` posts analog stick events and Prey's movement code does the rest.

## How it is controlled and observed

| Surface | Where | For |
| --- | --- | --- |
| VR Options | [`ui/options/`](ui/options/) | Players. Saved in `%LOCALAPPDATA%\PreyVR\vr-options.ini`. |
| Command channel | [`diagnostics/commands/`](diagnostics/commands/) | Testing and tuning, without an injector: `commands.txt` beside the log, answered in `results.txt`. Verbs are a fixed list, in [`docs/TUNING-COMMANDS.md`](../docs/TUNING-COMMANDS.md). |
| Keys | [`VrMode.cpp`](app/VrMode.cpp) | F10 VR Options, F11 start VR, F12 recenter, while Prey has focus. |
| Environment switches | [`EnvSwitch.h`](base/EnvSwitch.h) | Launcher kill switches, such as `PREYVR_DUAL_STEREO=0`. |
| Exports | [`Exports.cpp`](app/Exports.cpp) | `PreyVR_*` C entry points for the live harness and the smoke host. |

**Logging.** There is one log, `PreyVR.log`, with lines of the form `preyvr_<area> key=value`. The launcher gives each run its own folder under `%LOCALAPPDATA%\PreyVR\runs\` through `PREYVR_LOG_PATH`. Launchers and tests read these lines, so renaming one can break them.

**Reports.** Lanes expose their counters as `...Report()` text. The `report` command writes every counter, and `VrMode` logs a status line once a minute while VR is active.

## Adding a feature

The free-swing wrench in [`physics/swing/`](physics/swing/) is a good model.

1. **Find the seam.** Follow [`docs/RE-INVESTIGATION-GUIDE.md`](../docs/RE-INVESTIGATION-GUIDE.md), and record addresses in the registry.
2. **Write the pure half.** `SwingStrike.h/.cpp` decide what counts as a strike. List the `.cpp` in `preyvr_core` and test it (`swing_strike`).
3. **Write the contract.** `SwingNative.h` holds the addresses and the hashed functions, with `tools/re/verify_free_swing.py` beside it and the `swing_native` test.
4. **Write the lane.** `SwingLane.h/.cpp` hold the engine half, in `preyvr::dll`. List the `.cpp` in `preyvr_dll_objects` and register a `DLL` test (`swing_lane`).
5. **Schedule it.** Add the call to `FrameSchedule.cpp`, with a comment if its position matters. A hook installed at VR start belongs in `VrMode.cpp`, with its stop in `StopFeatures`.
6. **Expose it.** A setting is one `Spec` in [`VrOptions.cpp`](ui/options/VrOptions.cpp) and one wiring row in [`VrOptionsRuntime.cpp`](ui/options/VrOptionsRuntime.cpp). A new command needs a row in `docs/TUNING-COMMANDS.md`, or the `command_channel` test fails.
7. **Prove it.** Write the investigation up in `docs/`, keep what backs it in [`data/evidence/`](../data/README.md), and try it in a headset. Offline tests and byte checks do not show that it works in a running game.

## Things that catch people out

- **Lane tests see inside the lane.** They compile the lane's `.cpp` and call file-local names, so renaming inside a lane can break its test. This is also why some small headers stay self-contained.
- **`preyvr_dll_objects` order is link order.** Keep it stable. Proofs that a change left the code section byte-identical depend on it.
- **Verifiers scan `src/` for `*Native.h`.** A scan pointed at a folder that no longer exists finds nothing and passes silently, so keep new ones on `src/`.
- **Two state headers are private.** [`AnimIkState.h`](body/ik/AnimIkState.h) and [`XrSessionState.h`](xr/XrSessionState.h) belong to the files listed at the top of each. Include them from nowhere else.
- **`diagnostics/` is not all optional.** `DebugOverlay` also draws the aim lasers players see.
- **Tests are named by tools and docs.** Keep the executable's name (`TARGET`) when renaming a test.
- **Old paths in dated documents are history.** Dated files in `docs/` keep paths such as `src/dll/` on purpose. Living documents are kept current.
- **Some switches are temporary.** [`TODO.md`](../TODO.md) plans to remove or debug-gate the tuning parameters and instrumentation, so do not build a feature on one.

## Read next

- [`docs/README.md`](../docs/README.md): the index of the research notebook.
- [`docs/STEREO-DUAL-SUBMISSION-2026-10-10.md`](../docs/STEREO-DUAL-SUBMISSION-2026-10-10.md): how both eyes are drawn.
- [`docs/BODY-FOLLOW.md`](../docs/BODY-FOLLOW.md): the player's body, turning and room-scale.
- [`docs/ENGINE-INSTABILITY.md`](../docs/ENGINE-INSTABILITY.md): crashes, freezes, and what guards against them.
- [`docs/FAILURE_REGISTRY.md`](../docs/FAILURE_REGISTRY.md): traps that have each cost a session. Check it before trying a seam.
- [`tests/README.md`](../tests/README.md) and [`tools/README.md`](../tools/README.md): how the code is tested and what the tools do.
- [`data/README.md`](../data/README.md): where evidence and captures live.
