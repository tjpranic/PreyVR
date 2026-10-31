# Engine instability

The crashes and freezes that are the engine's rather than a feature's: what each
one is, what is known, what stays in the mod for it, and what to do when it
happens again. Status as of 2026-10-27:

| Issue | Status |
|---|---|
| [Cull-camera crash and the world flashing](#cull-camera-crash-and-the-world-flashing) | Fixed by the cull guard. Closed pending recurrence. |
| [Texture-view crash](#texture-view-crash) | Seen once, not in the 41 runs since. Closed pending recurrence; no fix built. |
| [Quit crash](#quit-crash) | Fixed in PreyVR-71. |
| [Phantom crash](#phantom-crash-a-stack-overflow) | A stack overflow. The overflow capture waits for the next one. |
| [Render freeze](#render-freeze) | Open. The frame probe waits for the next stall. |

PreyVR-98 (2026-10-27) has the overflow capture, the stall frame probe and the
launcher's evidence copy; it has not been worn yet ([what to check](#checking-preyvr-98)).

The removed instruments (the cull watch, the resource-set watch, the cull frame
probe) are in git at `bcc91ab`, with their docs: `CULL-CAMERA-CRASH-2026-10-17.md`
and `CRASHES-2026-10-24.md`. [RESEARCH_LOG.md](RESEARCH_LOG.md) has the dated history.

## Where the evidence is

- **The run folder**, `%LOCALAPPDATA%\PreyVR\runs\player-<time>`: PreyVR.log,
  and the overflow capture's files.
- **Prey's own crash files**, in the game root: `error.dmp`, `error.log` and
  `Game.log`.
  - The next crash replaces the error files, and Prey keeps only one backup of
    `Game.log` (`LogBackups\Game.log`).
  - So before each launch, the launcher copies the three into the newest run
    folder, if that session wrote them (`Save-PreyVRPreviousRunEvidence` in
    `tools/Start-PreyVR.ps1`).
- **Steam's process log**, `C:\Program Files (x86)\Steam\logs\gameprocess_log.txt`.
  Every Prey exit, with its exit code, months back:
  `grep "AppID 480490 no longer tracking"`. The codes, read as signed 32-bit:

  | Exit code | Meaning |
  |---|---|
  | `0` | clean exit |
  | `-1073740791` (`0xC0000409`) | fail-fast: the old quit crash |
  | `-1073741571` (`0xC00000FD`) | stack overflow: the phantom crash |

- **SteamVR's compositor log**, `vrcompositor.txt` and `.previous.txt`, about
  three days.
  - Each Prey session ends with a "Cumulative stats" block: presents, dropped
    frames, timeouts.
  - Its `AcquireSync FAILED` and `WaitForAcquire timed out` warnings come in every
    session, even before Prey connects. They mark late frames to the
    millisecond; they do not say which side was late.
- **Windows** keeps a dump (`%LOCALAPPDATA%\CrashDumps`) and an Event Log entry
  only for a fail-fast. A crash Prey's handler took leaves no Windows trace.
  - The recurring `LiveKernelEvent 141` entries re-send an old report dated
    2025-08-07. They are not new GPU resets.
- **PDBs:** every build makes `PreyVR.pdb`, kept in
  `build/packages/PreyVR-<N>-symbols`. A dump's PreyVR frames resolve only with
  that exact build's PDB.
- **Tools:**
  - `cdb.exe` is inside the Store WinDbg package. Find it with
    `Get-AppxPackage Microsoft.WinDbg*`; it updates itself. Pass `-y <empty dir>`
    to stay offline.
  - PreyDll's functions are read in Ghidra.

## Cull-camera crash and the world flashing

**Status:** fixed by the cull guard, and closed pending recurrence on 2026-10-27.
- Since the guard (2026-10-21), 61 runs (4.6 h) show no backlog in the culler.
- No crash since 2026-10-17, and no flashing reported.

**Signature.** A `JobSystem_Worker` access violation:
- at `0x27EC40` (read `0x23C`) or `0x27EC2A` (read `-1`);
- stack `0x27F820` < `0x27CD4F` < `0xDD6B72` < `0xDD7565` < `0x4D7E8C`;
- PreyVR.dll not on it.

Seen 2026-09-10 (twice, before dual stereo existed) and 2026-10-17.

### The mechanism (observed static, from the 2026-10-17 dump)
- **Pass cameras are engine copies in a pool.** `CreateGeneralPassRenderingInfo`
  (`0x1E5B30`) registers each pass's camera (`vtable+0x608` = `0x20B2D0`) in a
  per-fill-thread pool on `C3DEngine`.
- **The reset frees them.** The pool's per-frame reset (`C3DEngine` vtable
  `+0x90` = `0x2105A0` → `0x212680`, called from `CSystem` at `0xDF66BC`) frees
  the old array and its overflow pages whenever the pool overflowed. Every camera
  it handed out then dangles.
- **The job's camera is the cull item's, not the current pass's.** The cull
  consumer (`0x23EBD0`) copies an occlusion item's `+0x38` into the
  render-content job's pass at `0x23ED73`.
- **The fault.** An item still queued across a freeing reset culls with a freed
  camera: `0x27EA80` walks its `m_pMultiCamera` (`+0x228`) and reads `+0x23C`.

### The trigger (observed in the field, 2026-10-19 to 10-21)
**The handshake.** The cull consumer starts from a handshake on `CCullThread`
(`CObjManager+0x200`, state at `+4`):

| Writer | Does |
|---|---|
| `PrepareCullbufferAsync` (`0x23FFB0`) | sets 1 and kicks the z-buffer jobs |
| `RasterizeZBuffer` (`0x240CB0`), the last of them | 3 → 4 and launches the consumer, else 2 |
| `CullStart` (`0x23F020`), from `RenderWorld` | 2 → 4 and launches, else 3 and stores the pass |
| `EndOcclusion` (`0x23EFE0`) | waits; nothing discards a queued batch |

**The order in a frame.** `CCryAction::PostUpdate` (`0x5C6BA0`) calls
`PrepareOcclusion(m_ViewCamera)` (`C3DEngine` vtable `+0x40` = `0x20DA10`) and
then `CSystem::Render`. Both skip:
- while the `gEnv+0x2AB` no-world flag is set (only the PDA and the hacking
  minigame set it);
- while the camera is within 0.05 of the origin.

The prepare also needs `cvars+0x1A4`.

**What goes wrong.** On the frame the PDA closes:
- the flag is still set at the prepare and clear at the render, about 4 ms later;
- so the prepare is skipped and the render is not, and `CullStart` parks in 3;
- that frame's batch stays queued;
- one consumer run drains one batch, so the culler is a frame behind for the
  rest of the session.

**How it becomes the crash.**
- Before the guard, the lag began only on such a close, in all 16 runs where it
  happened. The dual lane's `dual_first_eye_not_held` refusal marks that frame.
- A second such close put the culler two frames behind. Its queued items' cameras
  were then in the pool being reset: the crash condition. It was seen without a
  crash in two runs (1,163 and 1,362 items).
- The 2026-10-17 crash run had two such closes, the second 17 s before the crash.

**Not identified:** what clears the flag inside that 4 ms gap. PreyVR's input
drain runs there, as does Prey's UI update.

**The flashing (inference).** One frame behind, the world is culled with the
previous frame's camera and drawn with the current one. What just came into view
is missing for a frame: a sliver on a head turn, a wedge on a snap turn. No
flashing has been seen with the guard on.

### The guard (`src/engine/fixes/CullPrepareGuard.{h,cpp}`)
**What it does.** On the main thread, in `CSystem::Render`'s detour, just before
the original: if no prepare is in flight (state not 1 or 2) and
`PrepareOcclusion`'s own tests pass for `m_ViewCamera`, it calls
`PrepareOcclusion` itself on `gEnv->p3DEngine`, after checking that object's
vtable. That is the engine's own call, a little later in the same frame.

**Safety and controls.**
- **Build gate:** an exact-bytes gate over 8 spans and the `+0x40` slot.
  `tools/re/verify_cull_prepare_guard.py` checks it, and the reads, jumps and calls
  it implies, against the supported PreyDll (`re_verifiers`).
- **Switches:** `cull.prepare 0|1`; `PREYVR_CULL_PREPARE=0` starts it off.
- **Report:** `cull.prepare` with no value gives `kicks`, `kickFailed`,
  `wrongReceiver`, `unreadable` and the handshake state.
- **Log:** `installed`, `guard value=`, and the first 8 `kick_failed` lines.

**In the field.**
- In its first headset run (2026-10-21) it paired a refused close, and the culler
  never fell behind.
- In the 61 runs since, it kicked 10 times, each on that same frame (the
  engine's prepare skipped by the no-world flag).
- **Correction, 2026-10-31:** this section called those closes the pause menu's.
  The flag's only writers are the PDA and hacking
  ([tools/re/verify_hacking_ui.py](../tools/re/verify_hacking_ui.py)), and in the
  last 40 runs 30 of the 34 logged refusals follow a PDA close; the other 4 were
  one hacking session.

### If it recurs
1. Keep the run's `error.dmp`, `error.log` and `Game.log`. The launcher copies
   them at the next launch.
2. If the game is still up, `cull.prepare`: `kickFailed` above 0 means the guard
   met a frame it could not pair.
3. Restore the cull watch from `bcc91ab`. It hooks the pool reset read-only and
   classifies every queued item's camera against the pools. Its lag depth shows a
   backlog the guard missed, and its `doomed` count confirms the mechanism live.

## Texture-view crash

**Status:** one occurrence, 2026-10-24 15:12:44, PreyVR-70, Lobby, 14 minutes in.
- Not seen in the 41 runs since.
- Closed pending recurrence on 2026-10-27. No fix built.

**Signature.** A `JobSystem_Worker` access violation reading `0x80` in
`d3d11.dll` (`mov rax,[rdx+80h]`, `rdx = 0`). The stack, as PreyDll RVAs:

| Frame | RVA | Function | What it does (observed static) |
|---|---|---|---|
| 12 | `0xF63CF8` | `FUN_180F63C70` | binds one view per shader stage (PS: `vt+0x40`) |
| 11 | `0xF6680C` | `FUN_180F66720` | applies a resource set's `{type, stages, slot, view}` entries (`+0x78`, count `+0x80`) |
| 10 | `0xF00B2A` | `FUN_180F00A90` | draws one compiled render item (also F-016's function, [FAILURE_REGISTRY](FAILURE_REGISTRY.md)) |
| 9, 8 | `0xED97A5`, `0xED9CF5` | `FUN_180ED96A0`, `FUN_180ED9C70` | one render item; the draw job |

**What the dump showed (observed).**
- **The bound view.** Its d3d11 internals were zeroed: destroyed. The resource
  set's entry it came from held a different, fresh view by the time of the dump:
  it was rewritten after the worker read it.
- **The other threads.** The render thread was in `RT_RenderScene` from
  DualSceneStereo's first submission, waiting for these draw jobs. The main
  thread was waiting for the render thread. No other thread was in texture or
  resource-set code.

**How a set is rebuilt (observed static).**
- `FUN_180F63F10` (set vtable `0x1DD1D38`, `+0x10`) zeroes the count and
  re-appends every entry into the same array (`FUN_180F63840`; views from
  `FUN_18100CE90`), reallocating only on growth.
- `FUN_180F6EF50`, the "update a resource set" step with 16 callers, always
  calls it, at `0xF6F01F`.
- The draw path only reads.

**What the rebuild watch found (observed runtime, PreyVR-71 to 97, 61.7 M rebuilds).**
- Every rebuild was the render thread's, from `0xF6F01F`, about 10,000 a second,
  nearly all during a scene submission.
- None was on another thread during a submission.
- The array never reallocated, so a reader walking a freed array is ruled out in
  practice.

**Leading inference.** The render thread rewrites a shared set, with a texture's
new view (streaming, for example), while draw jobs it has already launched still
read it. That needs a view to change at that moment, hence rare. Not established:
that rebuilds overlap running draw jobs.

### If it recurs
1. Keep the run's `error.dmp`, `error.log` and `Game.log`.
2. **The next instrument:** draw jobs in flight (a read-only hook on
   `FUN_180ED9C70`, entry and exit). Count the render-thread rebuilds made while
   any are running, and those that change a view pointer, with their stacks.
3. **The fix it would point to:** defer releasing a texture's old view until the
   frame's draw jobs finish, or serialise those rebuilds with the jobs.
4. **Not first:** an SEH guard around the bind in `FUN_180F63C70`. It detours a
   per-draw function, and it catches only a zeroed view, not a reused allocation.

**Evidence.**
- The run is `player-20261024-145830`, with its `error.dmp`.
- [evidence/crash-2026-10-24](evidence/crash-2026-10-24/) holds the logs, both
  thread listings and the cdb scripts.
- Replay:
  `cdb -z <run>\error.dmp -i "<Prey Release dir>;<PreyVR-70 dir>;C:\Windows\System32" -cf docs/evidence/crash-2026-10-24/crash2.cdb`.

## Quit crash

**Fixed in PreyVR-71** (2026-10-24). Steam has recorded no fail-fast exit since.

**Cause.** Every quit fail-fast (`0xC0000409`) inside PreyVR's global destructors,
during `ExitProcess`, after Windows had stopped SteamVR's threads.
- `gHost`'s swapchains called `xrDestroySwapchain`.
- ResearchHotkeys' thread was still joinable, so its destructor aborted.

**Fix.** `src/base/NeverDestroyed.h` keeps every global that owns XR handles, D3D
objects or a thread, and never destroys it:
- `gHost`, ResearchHotkeys' `gThread` and DebugOverlay's `gRenderer`;
- HudLayer's captures, text panels, parts, `redrawDepth`, `inventoryRight` and
  `nativeWrist`.

The runtime teardown is unchanged.

**The rule:** no OpenXR, D3D or thread calls from a global destructor. Check new
globals with a `dumpbin /symbols` scan for `??__F`.

## Phantom crash: a stack overflow

**Status:** one occurrence. The overflow capture (PreyVR-98) waits for the next.

**Evidence (observed).**
- **The run:** `player-20261025-163504`, PreyVR-89, about 25 s into gameplay,
  while stick-walking and smooth-turning.
- **The exit:** Steam recorded `-1073741571` (`0xC00000FD`), the only stack
  overflow in its record.
- **Timeline:**
  - PreyVR.log stops at 16:36:15.86 with nothing unusual: the watches were clean
    6 s before, and the physics lanes report `fault=0`.
  - SteamVR lost the process at 16:36:22.5.
- **Game.log** ended `Begin handle Exception` / `Notifying pending crash dump!`.
  Prey's handler deleted the old error files and wrote none.

**Why no dump.** Prey's handler runs on the thread that faulted, which has only a
few kilobytes of stack left after an overflow. It started and died.

**Not known:**
- which thread overflowed;
- whether it was a recursion or one huge stack frame.

PreyVR's own large frames (functions reserving over 4 KB, PreyVR-97):
- `SettingsPath` and `Sha256File`, 66 KB each. They run only at startup and on a
  VR Options load or save.
- In the IK path, which may run on job workers: `BuildBodyRig` 9.2 KB (new in
  PreyVR-86), `ProcessAdikWithTakeover` 5.7 KB, and `PlanBody` and `DriveHand`
  about 4 KB each.

None is known to have been involved. Prey's own `error.dmp` files cannot show
stack sizes: they carry no TEBs or memory-region information.

### The overflow capture (`src/diagnostics/capture/OverflowCapture.{h,cpp}`)
**How it works.**
- **Armed** at bootstrap, after the landmark gate: a first vectored exception
  handler and a capture thread with its own 256 KB stack.
- **The handler** acts only on `EXCEPTION_STACK_OVERFLOW`, once per process.
  - It records the thread and its stack bounds.
  - It wakes the capture thread and waits up to 60 s.
  - Then the exception goes on to Prey's handler, as before.
- **The capture thread** writes, beside PreyVR.log:
  - `preyvr-overflow-<pid>.dmp`: a minidump at the fault, with every thread's
    stack, TEB data (each stack's reservation) and memory-region info;
  - `preyvr-overflow-<pid>.txt`: the report;
  - one `preyvr_crash detail=stack_overflow …` line in PreyVR.log.
- **The report gives:**
  - the thread's role (`main`, `render`, `worker`, `hold`, else `other`) and its
    name, if it has one;
  - its stack's reserved, committed and used size;
  - its frames: the top 64 and the bottom 16, the bottom being the thread's own
    start, with runs of one address collapsed;
  - the return addresses that repeat most.
- **Opt-out:** `PREYVR_OVERFLOW_CAPTURE=0`.

**Limits.**
- The dump is written in-process. A lock the faulting thread holds (the loader's,
  a heap's) can stall it; after 60 s the process dies as it would have.
- An unwind can stop early at a frame with no unwind data. The dump still has the
  whole stack.

**Reading a report:**

| In the report | Reading |
|---|---|
| `repeating:` led by one address ×hundreds | Recursion. Look that address up in the build's PDB (`PreyVR.dll+…`) or in Ghidra (`PreyDll.dll+…`). |
| No repeats, `usedKb` near `reserveKb` | One frame too large, or a deep varied chain. The top frames name it. |
| `role=other` with a small `reserveKb` | A thread PreyVR does not name, such as a job worker. The `bottom:` frames give its entry function. |

The `overflow_capture` test overflows a 128 KB thread in a child process and
checks the dump, the report and the log line. Its report looks like this:

```text
pid=27032 thread=22840 role=render name=""
exception=0xC00000FD at=preyvr_overflow_capture_tests.exe+0xB747
stack reserveKb=128 committedKb=124 usedKb=112 rsp=0x495E553E90
frames=206
top:
  #0 preyvr_overflow_capture_tests.exe+0xB747
  #1-#63 preyvr_overflow_capture_tests.exe+0xB75C
bottom:
  #190-#202 preyvr_overflow_capture_tests.exe+0xB75C
  #203 preyvr_overflow_capture_tests.exe+0xB59A
  #204 KERNEL32.DLL+0x2DDA7
  #205 ntdll.dll+0x7EE4C
repeating:
  preyvr_overflow_capture_tests.exe+0xB75C x202
```

## Render freeze

**What it is.** In gameplay, the picture stops updating for 100 to 400 ms while
the hold layer re-shows the last frame.

**What is known (observed, 61 runs from 2026-10-21 to 10-27).**
- **Most `hold stall_ms=` lines are not gameplay:**
  - loading screens (about 9.5 s, or 740 to 900 ms for short loads);
  - about 1 s at VR start;
  - about 150 ms just after each load;
  - about 150 ms on quit.
- **23 were gameplay stalls.** Ten were in run `player-20261025-143133`
  (PreyVR-86) within 48 s, when the user saw the freeze and a flash on leaning
  back. The rest were single 100 to 220 ms frames, about one every 7 minutes.
- **SteamVR dropped frames in every session of PreyVR-86 to 93** (10-25
  afternoon: 1 to 24 each, 257 in that run), and in none of the 35 sessions
  before or the 6 after.
  - Builds 94 to 97 contain the same code, but the PC was restarted between 93
    and 94.
  - Build and environment cannot be told apart from these logs.

**Ruled out.**
- **Overlay swapchains made during play:** 3 of 187 were followed by a stall.
- **Game menus:**
  - The `preyvr_use menu open/closed` lines around run 143133's stalls were not
    menus. No Prey menu or VR Options was open.
  - The input gate also reads "menu open" when the main thread's menu poll is
    over 200 ms old, so those lines were the stalls themselves.
  - The line now says why: `why=menu`, `why=vr_options` or `why=stale`.

### The frame probe (`src/diagnostics/probes/FrameProbe.{h,cpp}`)
A flight recorder for the stall.
- **What it keeps:** the main, render and hold threads' frame steps, in a
  lock-free ring of 8192 events. It has no hook of its own: it records from seams
  other lanes own.
- **The trigger:** the hold thread's `hold_begin`, when Prey's render thread has
  not serviced an XR frame for the hold threshold (100 ms).
- **The window:** 8 main frames before the trigger to 12 after, and at least
  until the stall ends plus 4 frames. It is cut at 120 frames or 5 s.
  - A loading screen seen at the trigger or captured during the window makes it
    a load, and nothing is written.
  - At most 16 windows a session.
- **The output:** the command worker writes each window as `preyvr_frame_probe`
  lines, ending in a `summary`.
- **Controls:** `frame.probe 0|1`; `PREYVR_FRAME_PROBE=0` leaves it off.

**The steps:**

| Step | Thread | Where | Fields |
|---|---|---|---|
| `tick` | main | `CSystem::Render`'s detour starts: the frame clock | |
| `gameplay` | main | the gameplay seam | |
| `render_enter`, `render_exit` | main | around Prey's own `Render` | |
| `scene_enter`, `scene_exit` | render | a top-level `RT_RenderScene` | `type`, `outcome` |
| `end_frame` | render | `RT_EndFrame`, before the XR frame | `frame` |
| `xr_wait` | render | `xrWaitFrame` returned | `us`, `render` |
| `xr_acquire` | render | the eye image acquired and waited for | `us` |
| `xr_end` | render | `xrEndFrame` returned | `us`, `layers` |
| `present` | render | Prey's `RT_EndFrame`, its present among it, returned | `us` |
| `panel_open`, `panel_close` | render | the VR panel shown or hidden | |
| `load` | render | a loading screen's first capture | |
| `hold_begin`, `hold_end` | hold | the stall's start (the trigger) and end | `gap_ms`, `loading`; `stall_ms`, `held` |

**Each event line:**
```text
preyvr_frame_probe w=<window> <+|-><microseconds from the trigger>us <thread> <step> <fields>
```
The thread is `M` (main), `R` (render), `H` (hold) or `J<id>` (another).

**The summary** names each thread's longest gap, between which two of its steps,
and the longest single timed call:
```text
preyvr_frame_probe window=1 summary main=312ms(render_enter->render_exit) render=305ms(end_frame->xr_wait) call=xr_wait:300ms
```

**Reading the summary:**

| Summary | Reading |
|---|---|
| `call=xr_wait:<long>`, the render gap ending at `xr_wait` | The runtime held the frame (SteamVR), not Prey. Check the compositor log at that second. |
| `call=xr_acquire:<long>` | The compositor had not released the eye image. |
| `call=present:<long>` | Prey's present, or the GPU behind it. |
| render gap `scene_enter->scene_exit` | The scene submission: draw jobs, or GPU work the render thread waited on. |
| main gap `render_enter->render_exit` | The main thread inside Prey's `Render`, usually waiting for the render thread. Read the render gap with it. |
| main gap `render_exit->gameplay` or `gameplay->tick` | Game update on the main thread (entities, physics, scripts). |

## Checking PreyVR-98

**At startup, PreyVR.log should have:**
- `preyvr_crash result=0 detail=armed on=stack_overflow dbghelp=1`;
- `preyvr_frame_probe result=0 detail=recording value=1 trigger=hold_stall`;
- `preyvr_cull_prepare … detail=installed`, then `guard value=1`.

**While playing:** close the PDA once or twice. `cull.prepare` should then show
`kicks` and `kickFailed=0`. A freeze leaves a `preyvr_frame_probe window=…` block.

**At the next launch:** the launcher prints `Kept the last session's Game.log
with its diagnostics.`, and the previous run folder has `Game.log`.
