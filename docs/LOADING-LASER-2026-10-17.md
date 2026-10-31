# The laser during loads: hold frames

**2026-10-31:** Prey's loading screen no longer has a laser
([why](LOADING-SCREEN-2026-10-20.md#no-laser-on-loading-screens-2026-10-31)). Hold frames still bring the laser along
when Prey stalls over a menu.

Status, 2026-10-17:
- **Headset run (`player-20261017-104010`): the laser followed the hand through a
  10.5 s load** (933 hold frames, about 89 a second) and two short stalls.
- **The multithread protection cost nothing measurable** (results below). It stays
  on by default.
- One render-thread wait timed out (`renderWaitMisses=1`); when and why is not
  established.
- **The cursor on the surface the laser hit** (fixed afterwards, 2026-10-17, below): worn OK
  (marked 2026-10-27).

## Why the laser froze

Every XR frame comes from `ServiceXrFrame`, which runs inside Prey's
`RT_EndFrame` (FrameObserverHook). That includes the controller sync, the beam and
cursor quads, and `xrEndFrame`. While Prey loads it renders few frames or none, so
the mod submits none. The compositor reshows the last frame, reprojected for the
head, so the world looks right. The beam is a world-locked quad, though, so it
stays where it was.

**Corrected 2026-10-20:** this said the loading panel looked right too. It never
reached the headset: Prey draws the loading screen in a loop of its own and
presents it without `RT_EndFrame`, so hold frames showed the frame from before
the load, black when loading from a menu. Cause and fix:
[the loading-screen notes](LOADING-SCREEN-2026-10-20.md).

## The fix: hold frames

A thread of ours (`HoldThread`, XrSessionHost) submits frames while Prey's are late:
- **When.** No Prey frame for `thresholdMs` (default 100). A frame counts as Prey's
  when `ServiceXrFrame`, `HoldDualFirstEye` or `HeldFirstEyeTarget` runs. The rule
  is `xrhold::StallTracker` (`include/preyvr/XrHold.h`).
- **What.**
  - The last successful frame's layers again (`FrameLayerSet`, kept by value). The
    runtime composites the last released image of each swapchain, as it already
    does for the slot, fade and pointer layers.
  - The laser from a fresh aim pose (`LocateHeldPointer`), drawn by the same code as
    Prey's frames (`ui::PointerQuadsFor`, `FillPointerLayers`).
  - The laser points at the same surface as in the frame it holds: the game menu
    (the loading screen is one), or VR Options.
- **What not.**
  - No swapchain image is acquired and no D3D11 call is made.
  - No tracking frame is published, and no menu input, haptics or carry record is
    produced. Taps during a load would otherwise fire when it ends.
- **Ending.** A begun frame always ends, and the frame contract is left idle either
  way, so a failure cannot wedge Prey's next frame. A pending tracking-space change
  submits nothing until the render thread applies it.
- **No dead swapchains.** Every swapchain the session destroys mid-session goes
  through `DestroyUnheldSwapchain`, the `destroy` of every `InventorySwapchainApi`.
  That covers a refused HUD, a capture resized and VR Options' panel. It drops the
  held frame until Prey submits another, so a hold frame never names a destroyed
  swapchain.

**Fixed afterwards, 2026-10-17:** the laser's cursor was drawn on the game menu's
surface even when the hit was on VR Options, at the Options panel's (u, v). Over a
game menu the dot landed on the menu plane. Over the running game it landed where
an empty surface puts it, near the tracking origin. The cursor now goes on the
surface that was hit (`HeldPointer::surface`), in Prey's frames and hold frames
alike. Worn OK.

### Threading

- **The session mutex.** Hold frames take `gMutex` for the whole frame. Prey's
  render thread still never waits on a control call. It waits only for a hold frame
  already in flight (`LockForRenderThread`, at most 40 ms; in practice under one
  display period), and its stamp stops new ones from starting.
  - Without that wait, Prey's first frame after a hitch would lose its held first
    eye. That would make it an unrendered frame: black.
  - The waits are counted: `renderWaits`, and `renderWaitMisses` for the 40 ms limit.
- **D3D11 multithread protection.** SteamVR's `xrEndFrame` flushes the app's
  immediate context (see the SteamVR OpenXR forum, "xrEndFrame() blocks instead of
  xrWaitFrame()"). From our thread that would race Prey's render thread, which owns
  that context. So, while hold frames are on, Prey's device runs with
  `ID3D11Multithread::SetMultithreadProtected(TRUE)`:
  - Every immediate-context call takes D3D11's internal lock, so the runtime's
    flush cannot interleave with a Prey call.
  - The cost is that lock on every call Prey makes. That is what the A/B test below
    measures.
  - The protection is switched only on the render thread under `gMutex`
    (`ApplyHoldProtection`), so no hold frame is in flight when it changes. Hold
    frames are allowed only after it is on, and stopped before it is off.
  - Off restores the earlier state: if Prey had it on, it stays on. A
    single-threaded device, or no `ID3D11Multithread`, refuses with a log line.

`xr.hold 0` turns both hold frames and the protection off; `xr.hold 1` turns them
back on; `xr.hold 1 150` also sets the threshold. On by default.

## Verified offline

- `tests/XrHoldTests.cpp` (CTest `xr_hold`) covers stall start and end, durations,
  frame counts, and a late poll where one frame came and the next is already
  overdue.
- `tests/UiPanelTests.cpp` (`ui_panel`) covers the laser quads: on the panel,
  pressed, off the panel, no hit, and the head on the beam's axis.
- The DLL builds and the full suite passes (75 tests).
- The hold path itself (OpenXR calls, the protection, the thread handoff) has no
  offline harness here. Its first evidence will be the headset run.

## Headset check

**1. Did the protection come on?** In PreyVR.log at VR start:

    preyvr_xr_session hold result=0 detail=on multithread_protection=on was=off threshold_ms=100

If `was=on`, Prey already had it on: there is no cost to measure, so skip to step 3.

**2. Performance A/B**, standing still in one scene, in **Tune Prey VR.cmd**:
1. `xr.timing 1 90`. Wait about 30 s, then `xr.timing`.
2. `xr.hold 0`. The log shows `detail=off multithread_protection=off`. Then
   `xr.timing 1 90`, wait 30 s, `xr.timing`.
3. `xr.hold 1`. Repeat once more, to see the spread between runs.

Compare `frameP50`/`frameP95`/`frameP99`, `waitP50` (the slack left before the
runtime's deadline; the lock's cost comes out of it) and `frameOverBudgetWindow`.
If on is measurably worse, the fallback is to run hold frames on Prey's render
thread while it idles, which needs no protection but more reverse engineering.

**3. The laser.** Load a save or change level, and move the laser around during the
load:
- It should follow the hand.
- The log shows `hold stall_ms=… frames=…` for each long stall.
- The minute status line has `hold={ … frames=… stalls=… }`.
- Watch for a black or stuttering frame when the load ends. `renderWaitMisses`
  above 0 would mean a hold frame overran the 40 ms wait.

## Results, 2026-10-17 (`player-20261017-104010`, SteamVR, 2688 x 2880, both eyes)

| | `frameP50` | `frameP95` | `frameP99` | over budget (window) | mean interval (whole run) | over budget (whole run) |
|---|---|---|---|---|---|---|
| Protection on, 37 s | 12.35 ms | 14.61 ms | 15.23 ms | 407/512 | 14.41 ms (37.04 s / 2570) | 2414/2570 (94%) |
| Protection off, 41 s | 11.66 ms | 14.35 ms | 14.91 ms | 326/512 | 14.41 ms (41.31 s / 2867) | 2666/2867 (93%) |
| Window across the switch | 13.08 ms | 14.68 ms | 15.12 ms | 511/512 | | |

- **The whole-run means agree to within 0.02%.** That is about 69 frames a second
  either way.
- **The 6-second windows differ by 0.7 ms at P50, and noise explains it.** The
  window that straddled the switch, half of it unprotected, was slower than both.
- **The game is over the 11.1 ms budget on most frames with or without the
  protection.** That is the both-eyes cost at this resolution, a separate
  question.
- **One run of each, with the view not held still.** Enough to rule out a
  noticeable cost, not to bound a small one.

The `xr.timing 1 90` reply printed the old window: the reset is applied on the
render thread's next frame, so the reply to the command that arms it still shows
the previous generation.
