# Job-worker crash at PreyDll+0x27EC40: a freed pass camera in the cull queue

Status, 2026-10-17: mechanism established from the crash dump and static code.
The trigger is **not** established. A read-only instrument (`CullCameraWatch`)
is in the DLL to catch it on the next occurrence. Steam PreyDll SHA-256
`7d6e322f…05311a7`.

Status, 2026-10-21: the step that puts the culler behind is found ([The
handshake](#the-handshake-2026-10-21)): a frame that draws the world without
its occlusion prepare leaves its cull batch queued for good. The same lag is
the likely cause of a "parts of the world flash" glitch. A guard that makes the
missing prepare is built ([The guard](#the-guard-2026-10-21)), offline only.
Which of the prepare's tests failed on that frame is still inferred.

## Symptom

`EXCEPTION_ACCESS_VIOLATION` on a `JobSystem_Worker`, PreyVR.dll not on the
stack: `0x27EC40` (or `0x27EC2A`) ← `0x27F820` ← `0x27CD4F` ← `0xDD6B72` ←
`0xDD7565` ← `0x4D7E8C`.

| When | Build | Read address | Context |
|---|---|---|---|
| 2026-09-10 01:51 | candidate 04 | `0xFFFFFFFFFFFFFFFF` at `0x27EC2A` | forced XR session-loss test ([RE-VR-INTERFACE](RE-VR-INTERFACE-2026-09-10.md)) |
| 2026-09-10 | candidate 09 | `0x23C` at `0x27EC40` | normal play, Lobby ([RE-WEAPON-WHEEL](RE-WEAPON-WHEEL-2026-09-10.md)) |
| 2026-10-17 08:15 | world-reticle-19-wip (DLL identical to 18) | `0x23C` at `0x27EC40` | normal play, apartment, about 20 ms after a snap turn |

The September crashes predate dual stereo, so the crash is not specific to dual
stereo or the reticle work.

## Mechanism (dump of 2026-10-17)

1. `0x27EA80` tests an object box against the job's camera. It walks the camera's
   `m_pMultiCamera` (`CCamera+0x228`, [CCAMERA_LAYOUT](CCAMERA_LAYOUT.md)), reading
   `+0x23C` (`m_sceneMaskFilter`) of each entry.
2. The camera pointer (`SRenderingPassInfo+0x18` in the job's parameters,
   `0x261DDB14270`) points at freed, reused memory. Its 0x240 bytes are an array of
   `{heap pointer, 0x0000026100000000}`, so `+0x228` yields `0x26100000000`, which
   reads as `{data = NULL, count = 963}`. In the 2026-09-10 candidate-04 dump the same
   field held two floats (`0x3F80000000000000`).
3. Pass cameras are engine-owned copies. `CreateGeneralPassRenderingInfo` (`0x1E5B30`)
   stores `p3DEngine->vtable[0x608](camera)`, which is `0x20B2D0`. That function pushes
   the camera into the pool at `C3DEngine + (fillThreadId + 0xA) * 0x80`:
   `{data, capacity, overflow pages, count}`, plus 0x1000-byte overflow pages of 7
   cameras each.
4. The pool reset is `C3DEngine` vtable `+0x90` (`0x2105A0`), called from `CSystem`
   (`0xDF66BC`, per frame). It reaches `0x212680`. When the pool overflowed, that
   function **allocates a new array, frees the old one and every page**, then sets the
   count to 0. After such a reset, every pointer the pool handed out dangles.
5. The render-content job's camera does **not** come from the current pass. The cull
   consumer (`0x23EBD0`) pops an occlusion item (`CObjManager+0x400`, 0x40 bytes) and
   copies the item's `+0x38` into the job's pass info `+0x18` (`0x23ED73`), just
   before enqueuing it (`0x27F450` → job `0x27CCE0`).
6. All 8 workers ran that job with the same dead camera and frame IDs 14747/14746.
   The main thread's own pass for that frame used a different, live camera
   (`0x262C5CCFBA0`), and sat inside `CSystem::Render` → `RenderWorld`.

So an occlusion item, or the camera it names, survived a pool reset.

At the crash, the main thread was in PreyVR's `CSystem::Render` detour and the render
thread was in PreyVR's dual `RT_RenderScene` hook. That is circumstance, not
attribution.

**Not found:** what lets an item outlive its camera. Candidates include:
- an item carried across frames;
- a reset with the wrong fill-thread ID;
- an off-thread registration, such as `PrecacheRenderNode` (`0x20D960` also
  registers cameras) racing the coalesce.

No public report of this crash in unmodded Prey turned up.

## The instrument

`src/dll/CullCameraWatch.cpp` hooks `0x2105A0` and calls the original exactly once,
unchanged. Before and after the original, it reads:

- both camera pools, with their overflow page ranges;
- the occlusion queue's write/read indices (`+0x410`/`+0x440`), and the camera of
  every item still queued;
- the second queue's two raw indices (`+0x4A4`/`+0x4A8`).

It writes nothing to the engine. It is installed by the command worker once the
module is pinned, VR or not. `PREYVR_CULL_WATCH=0` leaves it out; `cull.watch 0`
makes it pass straight through.

An exact-bytes gate (8 spans plus the vtable slot) guards every offset it reads.
`tools/re/verify_cull_camera_watch.py <PreyDll.dll>` checks that gate against the
binary.

Only octree items (type 1) are classified; their camera at `+0x38` is the one
the render-content job is given. Type 0 is a batch's end marker and types 2 and 3
take other branches, so those are only counted (`markers`, `otherTypes`). Before
2026-10-20 they were classified too, and every end marker showed up as a false
`outside`.

A reset can change nothing: the pool it clears may have registered nothing since
its last reset (the first frame after a menu). When exactly one pool is empty,
that pool is the one being reset (`inferred=1`).

Each octree item's camera is classified against the pools as they were just
before the reset:

| Field | Meaning |
|---|---|
| `stale` | Camera in the pool this reset reclaims. |
| `doomed` | Stale, and the reset freed that storage (`freed=1`). This is the crash condition. |
| `previous` | Camera in the other pool (last frame's). Probably normal pipelining. |
| `outside` | Non-null and in neither pool, so already dangling. |
| `null` | No camera. |

### Log lines (`preyvr_cull_watch` in PreyVR.log)

- `detail=hook_installed … mutation=none`: the watch is live.
- `detail=doomed`: a reset that freed cameras still queued. It has its own
  budget (the first 64, then one in 64), so stale events cannot crowd it out.
- `detail=event`: `stale`, `outside` or an unreadable queue. The first 64 are
  logged, then one in 512. The line carries both pools before→after, the queue
  indices and the oldest stale/outside item (`node`, `camera`, `age` = position
  from the read index).
- `detail=lag depth=N was=M`: the lag depth changed (the first 64 changes, then
  one in 64). Each change also opens a frame-probe window. The depth is how many
  frame starts the oldest queued octree item has crossed:
  - 0: nothing queued;
  - 1: last frame's items (`previous`);
  - 2: the reset pool's items (`stale`);
  - 3: items in neither pool (`outside`, already dangling).
  `summary` adds `depth`, `depthMax`, `depthChanges` and `atDepth` (resets at
  each depth).
- `detail=pending_at_reset`: the first 8 resets that had items queued but no
  event, as a baseline.
- `detail=first_freeing_reset`: the first reset that freed pool storage.
- `summary` every 60 s, the same text as `cull.report`.

### Reading the next crash

| Before the crash | Reading |
|---|---|
| `doomed>0` in the last event before the crash, with `camera` equal to the dump's pass camera | Mechanism confirmed live. Next, find what kept that item queued: the `age`, the queue indices and `previous` say whether it is a lagging consumer or an item from two frames back. |
| `outside>0` | An item was pushed with a camera that was already dead. The producer, not the reset, is the bug. |
| `stale>0` but `doomed=0` | Same ordering fault without a free. It culls with a recycled camera and does not crash. |
| No events, crash anyway | The item was pushed after the last reset. The instrument cannot see that window; instrument the producer next. |
| `offThread>0` in a summary | A reset ran on an unexpected thread. |

Compare a VR run against a flat run, with the mod injected and VR off. The watch
runs in both.

## The frame probe (2026-10-20)

`src/dll/FrameProbe.cpp` records every frame step of the main thread, the render
thread and the cull consumer job into a lock-free ring.

**Triggers.** Each of these opens a window:
- a VR-panel close (`panel_close`);
- a first-eye refusal (`first_eye_refused`);
- a lag-depth change (`lag_change`).

Triggers that follow while a window is open join it.

**Windows.** A window runs from 8 frame starts before its trigger to 12 after,
extended by later triggers to at most 60. The command worker writes it to the
log, off the game threads:
- every window with a refusal or a lag change, up to 16 a session;
- the first 3 plain panel closes, as the control;
- a window still open after 2 s is written with what it has.

`cull.probe 0|1` switches recording. `PREYVR_FRAME_PROBE=0` leaves it out.

**The recorded steps, each with the queue's write/read where it applies:**

| Step | Thread | Seam |
|---|---|---|
| `tick` | main | the pool reset (the watch's hook), with the depth and pending |
| `render_enter` / `render_exit` | main | `CSystem::Render` calling the original (CameraEditHook) |
| `pass_created` | any | `CreateGeneralPassRenderingInfo` (PassCameraProbe's hook, shared). Frame ids, caller RVA, flags |
| `consumer_enter` / `consumer_exit` | job | the consumer job `0x23F1B0`, the probe's own read-only hook. The consumer's frame ids |
| `scene_enter` / `scene_exit` | render | top-level `RT_RenderScene`, outcome `single`, `doubled`, `build_miss`, `refused` or `no_teardown` |
| `end_frame` | render | `RT_EndFrame` (frame observer) |
| `panel_open` / `panel_close`, `first_eye_refused` | render | the XR frame |
| `lag_change` | main | the watch |

**Lines.** Each window starts with a header,
`preyvr_frame_probe window=N kinds=… control=0|1 events=…`. Each event line is
`w=N ±µs <thread> <step> <args>`:
- the time is relative to the trigger;
- the thread is `M` (main), `R` (render), or `J<id>` (a job or other thread);
- the trigger's own line ends in `<trigger`.

The window ends with `window=N end lost=…`.

**What to look for in a refusal window:**
- Which frame the main thread rendered: the `pass_created` frame id, between
  `render_enter`/`render_exit`.
- Whether the render thread drew a scene for it: `scene_exit` before the
  `end_frame` that refused.
- Whether that frame's consumer run finished before the next `tick`: compare
  the consumer frame id with the pass frame id, and the queue read index with
  the write index.

The first window that shows the consumer starting a frame late, beside a control
window that does not, names the step that went wrong. An `outcome=no_teardown` or
a missing `scene_exit` on the refused frame points at the render thread. A
consumer run that the main thread overtook points at the engine's frame pacing.

## Field results, 2026-10-19 (60 runs since the watch went in)

- **Normal play:** at every reset the occlusion queue is empty (`pendingResets=0`).
- **The trigger:** the cull consumer can fall a whole frame behind, and stays behind
  for the rest of the session. In all 15 runs where this happened, it began 2–3 ms
  after `xr_session result=refused detail=dual_first_eye_not_held`. That line is
  logged when a VR-panel menu closes on a frame where the main thread published a
  both-eyes contract but the render thread had no doubled scene. It never began any
  other way.
  - 25 of 150 panel closes were refused. Open duration does not separate refused
    closes from clean ones.
  - The refusal only marks the bad frame. The Sep-10 crashes predate dual stereo.
- **Each refusal adds a frame.**
  - After one refusal the queued items belong to the previous pool (`previous`).
  - After a second, they belong to the pool being reset (`stale`). Seen in
    17-190713, 17-192755 and 18-180109: up to 1.5 M stale items per session.
- **Doomed items occurred without a crash** in two-refusal runs: 1,163
  (17-192755) and 1,362 (18-180109). The Oct-17 crash run also had two refusals,
  21:13:31 and 21:15:38, the second 17 s before the crash.
- **Instrument flaws, fixed 2026-10-20:**
  - `type:0` items (the consumer's end markers) carry no camera, so `outside`
    counts are false positives.
  - Those false events used up the logging budget, so no doomed event was logged
    in detail.
- **Not yet known** (answered 2026-10-21, [The handshake](#the-handshake-2026-10-21)):
  how the bad transition frame shifts the culler. A probe
  should log, around each panel close, every `CSystem::Render`, `RT_RenderScene`
  (doubled or not), `RT_EndFrame`, `PrepareOcclusion` and `EndOcclusion`.
- **Reproduce:** open and close VR-panel menus until the log shows a refusal; a
  second refusal leaves the culler two frames behind.

## The handshake (2026-10-21)

**The run.** `player-20261021-194310`, PreyVR-54. The user saw parts of the
world flash. At 19:50:09 a pause-menu close was refused
(`dual_first_eye_not_held`), and the watch went to lag depth 1. It stayed there
until the session ended at 19:52:32: 9,747 resets, 1.6 M `previous` items.
That stretch had 35 snap turns.

**The frame probe's first refusal window (window 4).**
- During the menu the main thread rendered no world: `render_enter`/`render_exit`
  about 2 µs apart, with no pass.
- On the first world frame after it (pass 29957), the main thread queued 98 items
  and its end marker, **but no consumer job ran that frame.**
- From the next frame on, every consumer run took exactly the previous frame's
  batch: read index to the previous write index. One run drains one batch, so
  the extra batch never clears.
- The control at game start (window 1) is also a first world frame after frames
  without one. Its consumer ran in the same frame.

**Why the consumer did not start.** The job (`0x23F1B0`, "CheckOcclusion")
starts from a two-party handshake on `CCullThread`, which is `CObjManager+0x200`.
The state at `+4`:

| Writer | Does | Where |
|---|---|---|
| `PrepareCullbufferAsync` (`0x23FFB0`) | state = 1, then kicks the z-buffer jobs: transfer (`0x2409C0`), reproject (`0x240EB0`), rasterize | `0x240790` |
| `RasterizeZBuffer` (`0x240CB0`), the last of them | 3 → 4 and launch the consumer with the stored pass, else 2 | `0x240D47` |
| `CullStart` (`0x23F020`), from `RenderWorld` (`0x21E6F0` → thunk `0x281E20`) | 2 → 4 and launch, else 3 and store the pass | `0x23F061` |
| `EndOcclusion` (`0x23EFE0`, C3DEngine vtable `+0x48`) | waits for the z-buffer jobs; a state other than 0 or 4 only drops the producer count (`0x27E5C0`) | |

Nothing discards a queued batch. A frame that reaches `CullStart` with no
prepare in flight parks in 3. Nothing will launch the consumer for it, and the
batch waits for the next frame's run.

**The order in a frame.** `CCryAction::PostUpdate` (`0x5C6BA0`) does:
1. `p3DEngine->PrepareOcclusion(pSystem->GetViewCamera())`, vtable `+0x40` = `0x20DA10`;
2. the UI update, `OnPreRender` listeners and others;
3. `pSystem->Render()`, ISystem `+0x48` = `CSystem::Render`. PreyVR's detour
   drains its queued input here, before the original;
4. `p3DEngine->EndOcclusion()`;
5. `RenderEnd`.

`PrepareOcclusion` does nothing unless all three of its tests pass:
- `cvars+0x1A4` is nonzero;
- the byte at `gEnv+0x2AB` is 0;
- the camera's translation (`+0x0C/+0x1C/+0x2C`) is more than 0.05 from 0 on some
  axis (`0x1C7F77C`).

`CSystem::Render` draws the world only under the last two tests, and draws a
black 800×600 image otherwise. That black image is the
`dual_overlay image rect=0,0,800,600 argb=0xff000000` seen at startup. The two
read the camera at different points of the frame. If it reaches a world
position between them, the prepare is skipped and the render is not: an
orphaned batch.

**Inferred, not yet observed:**
- that the camera (or the `gEnv` flag) is what changed between steps 1 and 3 on
  that frame;
- what changed it.

PreyVR's input drain sits inside that gap and the UI update is beside it. The
drain moved into the `CSystem::Render` detour on 2026-09-06, before the
September crashes, and nobody reports this crash in unmodded Prey. The run
cannot tell these apart; the observation below records it.

**The visible effect (inferred).** At depth 1, each frame's world content is
culled with the previous frame's camera and drawn with the current one. What
just came into view is missing for one frame: a sliver at the edge on a head
turn, a wide wedge on a 45° snap turn. At depth 2 the cameras are recycled
pool slots, so the frustum used is some other camera of the frame. Depth 2 plus
a freeing reset is the crash.

## The guard (2026-10-21)

`src/dll/CullPrepareGuard.cpp`. It runs on the main thread inside the
`CSystem::Render` detour, after the drains and the camera edit, just before the
original:
- If the state is 1 or 2, this frame's prepare is in flight; nothing to do.
- Otherwise, if `PrepareOcclusion`'s own three tests pass for `m_ViewCamera`,
  the world is about to be drawn with no prepare. The guard calls
  `PrepareOcclusion(m_ViewCamera)` on `gEnv->p3DEngine`, after checking its
  vtable.

That is the engine's own call, a little later in the same frame. The handshake
then pairs whichever side comes first.

**Observation, which stays on with the guard off:**
- A hook on `PrepareOcclusion` (`0x20DA10`) records each call. It calls the
  original unchanged and counts the skips by test: `skippedOrigin`,
  `skippedNoWorld`, `skippedRenderTasks`. MinHook relocates the RIP-relative load
  it displaces.
- The render bracket reads the state on both sides. A 3 after a state other than
  1 or 2 is an orphan, certain to stay queued. A 3 after a 1 is the normal race,
  which `RasterizeZBuffer` resolves.
- Frame-probe steps:
  - `prepare` records the states and the tests (`gates=rwc`: r render tasks, w
    world, c camera; `-` failed);
  - `prepare_kick` and `orphan` are triggers that open windows;
  - `render_enter`/`render_exit` now carry `state=`.

**Lines (`preyvr_cull_prepare`):**
- `detail=installed … hooked=1 epsilon=0.05`.
- `detail=kick n=… state=4->1 lastPrepare=skipped_origin camera=x,y,z`. The first
  32 are logged, then one in 256. `lastPrepare` is what the engine's own
  `PrepareOcclusion` decided since the previous render: `ran`, `skipped_*`, or
  `none` (not called at all).
- `detail=kick_failed`: always logged.
- `detail=orphan n=… state=4->3 guard=0|1 lastPrepare=…`.
- `detail=first_origin_skip`.
- `summary` every 60 s, the same text as `cull.prepare`.

**Controls.**
- `cull.prepare 0|1`; `PREYVR_CULL_PREPARE=0` starts with the guard off.
- An exact-bytes gate (8 spans plus the `+0x40` slot) guards every offset.
  `verify_cull_camera_watch.py` checks it, together with the reads, jumps and
  calls the spans imply.

**Reading the next run.**

| Seen | Reading |
|---|---|
| `kick … lastPrepare=skipped_origin`, the watch stays at depth 0 through a refusal | Confirmed: the camera reached a world position between the prepare and the render, and the guard paired the frame. |
| `kick … lastPrepare=none` | `CSystem::Render` ran with no `PrepareOcclusion` since the last render: another caller of Render. |
| `kick … lastPrepare=ran` | Should not happen: the prepare ran yet the state was not 1 or 2 at the render. Read the window. |
| `orphan guard=1` or a lag change with no kick | A path the guard does not cover, such as `m_pProcess` without `PROC_3DENGINE`, which draws the world with no camera test. |
| `guard=0` (A/B): `orphan` then `lag depth=1` | The mechanism, observed directly. |

## Verification

- Offline: `cull_camera_watch` CTest builds fake engine, pool, page and queue memory.
  It covers stale/doomed/previous/outside/null, ring and counter wrap, refusals,
  bounded logging, the gate and the command boundaries.
- `tools/re/verify_cull_camera_watch.py` passes against the supported image and
  refuses another.
- **Not yet run in the game.** The cost per reset is a few hundred bytes of reads,
  plus a scan only when items are queued, but it is unmeasured.
- 2026-10-20, offline:
  - `cull_camera_watch` adds item types, idle-reset inference, the depth
    sequence 0→1→2→3→0, the doomed budget and probe ticks.
  - `frame_probe` covers window bounds, the control and refusal quotas, merged
    and capped triggers, ring overwrite, the timeout, thread tags, both seams'
    records and the commands.
  - The verifier also checks the consumer job's bytes, its call into the gated
    consumer loop, and both hook entries.
  - The full suite passes (86 tests).
  - The watch's per-reset cost is unchanged, and the probe adds about a dozen
    ring writes a frame. **Not yet run in the game.**
- 2026-10-21, offline:
  - `cull_prepare_guard` drives the guard and the observation detour over fake
    `CObjManager`, cvars, `gEnv` flag, engine and camera memory. It covers:
    - normal frames;
    - kicks from states 0, 3 and 4;
    - each of the three tests, including the 5 cm boundary and NaN;
    - orphans with the guard off, against the normal 1 → 3 race;
    - a failed kick, the wrong receiver, unread states;
    - the log budgets, the gate and the commands.
  - `frame_probe` covers the new steps and window kinds.
  - The verifier passes against the supported image (17 spans).
  - The full suite passes (92 tests). **Not yet run in the game.**
