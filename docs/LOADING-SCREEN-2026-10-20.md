# The loading screen in the headset

Status, 2026-10-20:
- **Cause found** (static, plus one run's log). **Accepted in the headset** (run
  `player-20261020-192708`, package `PreyVR-48`): the loading screen showed.
- The fix is a detour on the loading loop's present, plus hold frames that show
  its copy on the menu panel.

## Results, 2026-10-20 (`player-20261020-192708`, SteamVR, 2688 x 2880)

| | Stall | Loop presents copied | Uploads | Frames shown |
|---|---|---|---|---|
| Load from the main menu | 10.50 s | 1346 | 385 | 488 |
| Second load | 0.64 s | 91 | 15 | 15 |

- **Load time unchanged**: 10.50 s, against 10.38 s for the same load before the
  fix (`player-20261020-181530`).
- The screen was first shown 0.09 s after the first copy. `renderWaitMisses=0`.
- Prey's loop ran about 130 presents a second, so it is not capped at 60 here.
- **Hold frames ran at about 47 a second during the long load, against 59 before
  the fix.** That is the laser's rate during loads. The likely cost is the upload,
  but it is not measured.
- **At the end of the long load, the panel closed for 0.24 s and opened again**
  (`ui_panel active=0`, then `1`), so "press any key" was placed afresh rather
  than where the screen was. The run before the fix shows the same close
  (`player-20261020-181530`). The first frame after the load does not see a menu
  open yet.

## The bug

Loading a save showed black in the headset while the desktop showed Prey's loading
screen. The "press any key" screen after it was fine.

## Why

With `gfx_loadtimethread 1` (Prey's default), the loading screen is not drawn in
Prey's normal frames:
1. FlashUI (`0x2D8810`) hands its loading screen to the renderer (`+0x768`).
2. Prey's render thread then sits in a loop in `SRenderThread::Run` (`0xFE96F0`)
   until the load ends. Each pass draws the loading screen (the callback's
   LoadtimeRender) and presents it through renderer vtable `+0x8E0` (`0xF7E820`),
   which calls `Present` itself. A second thread, `RenderLoadingThread`, processes
   the main thread's render commands meanwhile.
3. `ProcessCommands` defers every `RT_EndFrame` while the loop runs and calls it
   once afterwards.

Every XR frame is made in the `RT_EndFrame` detour (FrameObserverHook ->
`ServiceXrFrame`), so none was made from the loading screen. The hold thread
([the hold-frame notes](LOADING-LASER-2026-10-17.md)) kept resubmitting the last
frame from before the load. Loading from the main menu, that was the menu panel
showing whatever Prey drew last before the loop: black.

Run `player-20261020-181530`: `ui_panel active=1` until the load, then
`hold stall_ms=10376 frames=617` (10.4 s with no `RT_EndFrame`) while the desktop
showed the screen.

Addresses and spans: [the address registry](ADDRESS_REGISTRY.md#loading-screen--2026-10-20).

## The fix

- **The detour** (`src/dll/LoadingScreen.cpp`). Prey's render thread, inside the
  loop, just before the loop's `Present`: `CaptureXrLoadingScreen` copies the
  backbuffer, which holds the loading screen, into a texture of ours
  (`Host::loadingImage`), and counts the copy (`gLoadingCaptured`).
  - It never waits. It runs holding the loop's lock (`SRenderThread+0x18`),
    which `RenderLoadingThread` takes for some commands. A wait for the headset
    there could slow the load.
  - The texture is created the first time, under `gMutex` taken only if free.
- **Hold frames show it** (`ServiceHoldFrame`). While a copy of this load is
  waiting, a hold frame:
  - uploads the newest copy into slice 0 of the session's swapchain, the menu
    panel's image, under `gMutex`;
  - submits the menu panel (flat or curved, as menus are) with nothing behind it,
    and the laser on it.
- **Where it goes.** On the game menu's panel:
  - loading from a menu, where that menu was;
  - otherwise placed in front of the wearer, as a menu opening is, and kept as the
    menu's, so a "press any key" menu after the load stays where the screen was.
- **The end.** Prey's next ordinary frame (`EndLoadingScreen`, at the top of
  `ServiceXrFrame`) clears the copy count and logs the load. If anything was
  uploaded, the frame held from before the load is dropped: its swapchain image
  has been overwritten.
- `xr.hold 0` turns this off with the rest of the hold frames. Loads then show the
  frame from before them again.

### Threading

- The render thread's copy and the hold thread's upload are D3D11 calls on Prey's
  immediate context from two threads. Hold frames already require Prey's device
  to run with D3D11 multithread protection on, so each call is atomic. The GPU
  runs them in the order they were issued, so an upload reads a finished copy.
- Neither copy changes pipeline state, and Prey binds neither texture.
- Swapchain acquire and release happen only under `gMutex`, as `xrEndFrame` does,
  so they never overlap.
- The loop's own pacing is unchanged: vsync, or without it the limiter at
  `0xF7CD10` (`sys_MaxFPS`, 60 when 0). Hold frames run at the headset's rate and
  upload a copy only when a newer one exists.

### Also fixed

- **The menu predicate's loading element** (`HudBridge.cpp`). It asked for
  `"LoadingScreen"`, which never resolved; Prey's own lookups use
  `"DanielleLoadingScreen"`. Its movie was never drawn over gameplay in 87 logged
  runs (DualOverlay logs each movie the first time it is drawn), so this should not
  open the menu panel during play.
- **The loading-laser notes** said the loading panel looked right during hold
  frames. Corrected.

## Verified offline

- `tools/re/verify_loading_screen.py` checks, in the supported PreyDll:
  - both gate spans;
  - that the loop is the only caller of vtable `+0x8E0`;
  - that both presents use the same swapchain field;
  - the `RT_EndFrame` deferral and the one call after it;
  - `gfx_loadtimethread`'s default of 1;
  - the element name.
- The DLL builds and the full suite passes (86 tests).
- The detour, the copy and the upload have no offline harness. Their first
  evidence will be the headset run.

## Headset check

1. **At VR start**, PreyVR.log has
   `preyvr_loading_screen result=0 detail=hook_installed target=loadtime_present`.
2. **Load a save from the main menu.** In the headset:
   - the loading screen should appear on the menu panel within about 0.1 s;
   - anything on it that moves on the desktop (progress, tips) should move in
     the headset too;
   - the laser should follow the hand over it.
   The log shows `loading_screen captured size=…`, then `loading_screen shown …`,
   and at the end `loading_screen ended captures=… uploads=… frames=…`.
3. **"Press any key"** should appear where the loading screen was, with no jump.
4. **Change level through a transit door** (a load from gameplay). The loading
   screen should appear in front of you. `ui_panel active=1 reason=loading_screen`
   is logged when it is placed.
5. **Load time.** Compare with run `player-20261020-181530`: 10.4 s for the same
   save, read from `hold stall_ms`.
6. **During play**, the menu panel must not open by itself. If it does, the
   loading element is visible over gameplay after all. Report it with the log; the
   menu predicate change is one line to revert.
7. Watch for a black or stuttering frame at the end of a load.
   `renderWaitMisses` in the minute status line should stay 0.
