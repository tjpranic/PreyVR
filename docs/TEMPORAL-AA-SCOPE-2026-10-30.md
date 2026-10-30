# Temporal AA per eye: feasibility and scope (2026-10-30)

Branch `custom`. The wearer's report: jaggies on distant geometry, worst on near-horizontal and near-vertical
edges. Lowering Prey's FOV slider from 120 to 110 helped. Raising the render size to 3240x3240 did not. The mod
forces SMAA 1X (`r_AntialiasingMode 1`), a single-frame filter. It can't stop stair-steps crawling as the head
moves, or rebuild detail thinner than a pixel. Temporal AA is the usual fix. It has been off since 2026-09-08
([H-022b](HYPOTHESES.md)) because Prey's history blends the other eye's image.

**Investigation only. Nothing is built.** Static, with the existing read-only Ghidra project (`PreyVR`,
`/Prey/PreyDll.dll`) over the Steam DLL (SHA `7d6e322f…11a7`). Addresses are RVAs unless written `0x18…`.
Evidence: [R-136](ADDRESS_REGISTRY.md) and `tools/re/verify_temporal_aa.py` (28 checks).

## Verdict

**Feasible, moderate size.** All of Prey's temporal-AA state found here is small and enumerable. It is touched on
the render thread inside each scene submission, which `DualSceneStereo` already brackets one eye at a time.
The core needs no new engine hook: save and restore a handful of values around each eye's submission, and copy
one history texture per eye.

What can't be settled statically, and needs a probe run first:
- that post-processing (and so the counter) runs once per eye;
- the history texture's D3D format and size;
- the device-texture hop to `ID3D11Texture2D`.

## How Prey's temporal AA works (observed static)

| Piece | Where | What it does |
|---|---|---|
| Mode | `r_AntialiasingMode` at `0x182B1C74C`; effective copy `0x182B1C750`, clamped to 5 (`0xECEC10`) | flags = `1 << min(mode, 5)` (`0xFE3760`) |
| Dispatch | `0xF9C790`, the AA stage's execute; stage = graphics pipeline `+0x288` (`0xFB2A0E`) | `flags & 0xE` runs spatial SMAA (`0xF99500`, modes 1-3); `flags & 0x3C` runs the temporal resolve (`0xF99FE0`, modes 2-5) |
| History colour | `$PrevBackBuffer0/1`, one `CTexture*` each at `0x182B3F690`/`0x182B3F698`; created in `0x1071510` whenever the effective mode isn't 0 | The resolve writes `history[c & 1]` and reads `history[(c+1) & 1]`, if `c - stage+0x20 < 10`; else it uses the current frame. It returns its output as the new scene colour |
| Previous camera (reprojection) | A ring of four double-precision 4x4 matrices inside the AA stage, `+0xBA8`, `0x80` per slot | Reads slot `(c-1) & 3`, writes slot `c & 3`; stamps `stage+0x20 = c` |
| Other inputs | slot 2 `$ZTarget` (`0x182B3F778`), slot 3 `$VelocityObjects` (`0x182B3F7F0`) | Depth for camera reprojection; object motion vectors for things that move |
| View cameras | `0xFB1670`, called first by the resolve and by 8 other passes | Builds the per-view block from the render view's current camera (`+0x11A0`) and **previous camera (`+0x13E0`)**, which `SetPreviousFrameCamera` (R-064, view vtable slot 9) writes |
| Jitter | `RT_RenderScene` `0xF4206E`..`0xF422AD` | Sample = `c mod N` (2-sample table at `0x2279A78` for 2TX, and longer Halton sequences); written to the renderer's sub-pixel offset `+0xD70` (R-129) |
| **The counter `c`** | `0x182BB5150` | **Advanced only by the post-processing driver `0xF85230`, once per run** (`0xF8567E`, mod 2^24). The same driver copies the renderer's view-projection `+0xCF0` into `+0xD30` ("previous") just before |

The verifier also scans all of `.text`: the counter has exactly two writers, the static initialiser
(`0x1BC5C`) and that increment. Nothing else swaps the history pointers. `0x1013100` only clears them on
teardown, and `0xF35E70` only collects them.

## What goes wrong with both eyes per frame (inference from the above)

The mod submits the scene twice a frame, and the first eye is held *after* post-processing
(`DualSceneStereo.cpp`). So the counter should advance twice a frame: the first eye runs at `c`, the second at
`c+1`, the next frame's first eye at `c+2`. Then:

| State | First eye | Second eye |
|---|---|---|
| History read | `history[1]`, written by the second eye last frame | `history[0]`, written by the first eye this frame |
| Previous camera (ring) | the second eye's, last frame | the first eye's, this frame |
| Jitter, 2-sample pattern | always sample 0 | always sample 1 |
| View previous camera (`+0x13E0`) | the head's, from the game thread; the mod never sets it per eye | same |
| Renderer `+0xD30` | the second eye's, last frame | the first eye's, this frame |

**Every eye reprojects against the other eye.** That is the "mirage" of 2026-09-08 (then one eye per frame;
same cause). With 2TX, neither eye gets any temporal supersampling at all.

## What per-eye temporal AA needs

The rule: each eye sees an unbroken mono timeline. All of it goes in `DualSceneStereo`, around each
`original(renderer, view, …)` call. It runs on the render thread, between submissions it already separates.

1. **Counter per eye.** Keep `cA`, `cB`. Before an eye's submission write its value to `0x182BB5150`; after,
   read it back (now +1). **Start `cB = cA + 2`.** Then:
   - each eye walks the whole jitter sequence;
   - with a ring of 4 and the eyes 2 apart, each eye's `(c-1) & 3` slot holds *its own* last frame (worked
     through in the [appendix](#appendix-the-ring-with-eyes-two-apart));
   - the freshness test still passes (`-1` and `2`, both `< 10`).

   No stage pointer is needed.
2. **History per eye, by copy.** Both eyes now have the same parity, so they share the output and input
   slots. Keep one texture per eye (mod-owned, same description):
   - before the eye: `CopyResource(history[(c+1)&1] ← saved[eye])`;
   - after the eye: `CopyResource(saved[eye] ← history[c&1])`.

   That's four full-frame copies a frame. If the history is 16-bit float RGBA (the probe will say), that's about
   56 MB each at 2644², roughly 0.1 ms each on this GPU. Engine-created extra textures with pointer swaps would avoid the copies, but calling the
   creator `0x100AE50` from the mod is the riskier route; do it only if the copies cost too much.
3. **View previous camera per eye.** In `PrepareEye` (and before the first eye), call
   `SetPreviousFrameCamera(view, last frame's camera for this eye)`. Without it, moving objects get motion
   vectors from the head's previous camera, half an IPD off.
4. **Renderer `+0xD30` per eye** (64 bytes). Restore before, save after. Its consumers aren't enumerated, but it
   is what a mono frame would hold, so this is safe either way.
5. **Resets** (snap turn, recentre, teleport, level load, leaving a menu). The clean reset sets `stage+0x20` far
   enough back that the resolve skips history, which needs the stage pointer. Adding to both counters does not
   reset the second eye: the first eye's fresh stamp keeps the second eye's test passing. Test first whether
   TAA's own neighbourhood clamp hides a one-frame ghost before building this.

Then allow modes 3 (SMAA 2TX) and 4 (TSAA) under VR, behind a switch, and allowlist
`r_AntialiasingTAASharpening` and its neighbours, which exist in this build.

## Plan

0. **Probe, no visual change.** A `taa.report` that logs, per submission:
   - the counter before and after (expect +1 each, +2 a frame);
   - the effective mode;
   - both history `CTexture`s → `+0x30` → the `ID3D11Texture2D`, through `QueryInterface`, with its format and
     size;
   - whether `history[c&1]` changes after the resolve (it shouldn't).

   *Decision rule:* if the counter doesn't advance per submission, the eye table above is wrong; redo it from
   the probe before building.
1. **Per-eye state** (items 1-4) behind a research switch `taa.vr 0|1`, off by default, with the mode set
   separately. Unit tests for the counter/ring arithmetic, as pure functions.
2. **Headset A/B**: SMAA 1X against TSAA (4) and SMAA 2TX (3), on the distant edges the wearer reported. Check
   for:
   - no mirage on the weapon;
   - ghosting on moving NPCs;
   - snap turn, recentre and loading screens;
   - frame time (`frames=` in the status line).
3. **Polish**: resets (item 5), sharpening, a VR Options entry, docs. Only after 2 is worn OK.

## Phase 3: the menu option (built 2026-10-30, PreyVR-107, worn OK 2026-10-30)

The wearer asked for a VR Options switch between SMAA and temporal AA, and saw no smearing or ghosting in
phase 1, so sharpening is left as Prey's.
- **The option:** **Setup > Anti-Aliasing** (`antialiasing` in vr-options.ini): SMAA (0) or Temporal (1),
  **Temporal by default.** The lane now starts on to match; `vr_options_wiring` pins that.
- **One setter:** `SetTemporalAntialiasing` (VrMode.cpp), which the menu, the saved file and `taa.vr` all go
  through. It sets the lane and marks the AA mode to change. While VR runs, VrMode's tick queues
  `r_AntialiasingMode 4` or `1`, again while the console is busy, and logs `preyvr_vr antialiasing=...`.
- **VR start:** applies the saved choice (`ApplySavedRendererOptions`, right after the panel settings) before
  its renderer settings, which apply the matching mode.
- **`taa.vr`** is now the option's session command (a player command, as other menu settings' commands are),
  without the `aa=` field.

**Headset, 2026-10-30: "Tested and confirmed working."**

## Phase 1 results (headset, 2026-10-30, PreyVR-106, run `player-20261030-150105`)

**Worn OK: "It looks good, I like it."** `taa.vr 1` at 04:02:31, about 2.5 minutes of play:

| Reading | Value | Meaning |
|---|---|---|
| `frames`, `starts` | 6537 frames, 1 start (the enable) | applied every frame, never restarted |
| `copiesIn` / `copiesOut` | 13072 / 13074 | every eye of every frame after the start (2 x 6536 in, 2 x 6537 out) |
| `freshStarts`, `resolveMissed`, `counterOff`, `refused` | 2, 0, 0, all 0 | only the start went without history; every resolve ran with its eye's counter |
| `idleMode` | 2 | two frames before the queued `r_AntialiasingMode 4` took effect |
| probe `frameDelta` | `1:6536` with the lane, `2:2479` before it | each eye's counter now moves once a frame |
| probe `stampIsCounter`, `jittered` | 6537 for each eye | the resolve ran in each eye's submission with that eye's counter |
| probe `jitter` | first (-0.375, 0.125) px at 11494, second (-0.125, -0.375) at 11496 | samples 2 and 0 of the grid (`counter & 3`): the eyes now walk the pattern independently |

**Cost.** About 67 fps in the first minute with the lane and 60 in the second. That's against about 66 in
mode 4 without the lane (the probe run) and about 78 in SMAA 1X, in different scenes. The lane's copies are
lost in the noise; the cost is TSAA itself. Still not a controlled one-spot A/B.

**Not exercised in the field:** starts after menus and loads (none happened in the window; the lane test
covers them).

## Phase 1: per-eye temporal AA (built 2026-10-30, PreyVR-106, worn OK 2026-10-30)

The wearer judged mode 4 in its broken cross-eye state to "definitely help" the distant aliasing, so the lane
was built.
- **Code:** `src/dll/TemporalAaLane.cpp`, wired around each eye in `DualSceneStereo.cpp` (headset frames
  only); the arithmetic is in `include/preyvr/TemporalAa.h`.
- **Switch:** `taa.vr 1|0` ([TUNING-COMMANDS](TUNING-COMMANDS.md)). Off by default. `1` also queues
  `r_AntialiasingMode 4`, which VR start then applies instead of 1; `0` queues 1.
- **Gate:** seven bodies now (`SetPreviousFrameCamera` `0xEE82D0` added). The verifier has 42 checks.

Per eye, around its submission (items 1-4 above, plus resets):
- **Counter.** The eye's own (the second eye two ahead) is written in, and read back after (+1).
- **Stamp.** The lane writes the AA stage's stamp itself: one count behind when the eye has history, ten behind
  when it doesn't. Whether the resolve uses history is therefore the lane's decision alone. Left to the other
  eye's stamp, the first eye at counter `0xFFFFFF` would read the second's wrapped stamp as stale. The pure
  test found this.
- **History.** Copied into the slot the resolve reads, and out after the resolve (only when the stamp shows
  it ran with this eye's counter).
- **Previous camera.** `SetPreviousFrameCamera` with the eye's last camera, or its own camera on a start. The
  view's own previous camera goes back after the frame.
- **Previous view-projection.** The renderer's `+0xD30` is restored before the eye and kept after it.
- **Starts.** Both eyes start afresh (one frame without history) when:
  - the lane is enabled;
  - after a frame that wasn't drawn both-eyes (`gap`: menus, loading);
  - a frame stopped after its first eye;
  - new history textures appear;
  - after a refusal.

  A history texture that isn't D3D11's, another device, or other bytes leave the frame as Prey's.

Tests:
- `temporal_aa`: a byte-level model of the resolve; without the lane each eye reads the other's, with it each
  reads its own, across the 24-bit wrap.
- `temporal_aa_lane`: the real lane against a fake engine with WARP history textures read back. A mutant
  without the copy-in fails it.

**Headset checklist.**
1. In a level, write `taa.vr 1`. `results.txt` should say `aa=0` and `enabled=1`, and `active=1` from the
   next frame.
2. At the same spot, compare `taa.vr 0` (SMAA 1X) and `taa.vr 1` on distant edges.
3. Check:
   - no mirage on the weapon or hands;
   - smear on hands or weapon in fast motion;
   - ghosting on moving NPCs;
   - snap turn and recentre;
   - softness, overall.
4. Frame rate: `frames=` in the minute status line, `taa.vr 0` against `1`, at one spot.
5. Afterwards, `taa.report`. In the lane report, expect:
   - `copiesIn` near `copiesOut`;
   - `resolveMissed` and `counterOff` at 0;
   - `starts` only at the enable and after menus or loads.

   With the lane on, the probe's `frameDelta` turns to `1:`.

Tuning, if needed: `r_AntialiasingTAASharpening` and the other `r_AntialiasingTAA*`/`TSAA*` cvars are now
allowlisted. `r_AntialiasingTAAPattern` picks longer jitter patterns.

## Phase 0 results (headset, 2026-10-30, PreyVR-105, run `player-20261030-143337`)

**The decision rule passes: build phase 1.** About 2 minutes in mode 1 (SMAA 1X), then about 95 s in
mode 4 (TSAA) via `commands.txt`, then `taa.report`:

| Reading | Value | Meaning |
|---|---|---|
| `frameDelta` | `2:8916`, nothing else | post-processing runs once per eye, every frame; the eye table holds |
| `delta` | `first` 1:8917, `second` 1:8916 | one advance per submission |
| `prevViewProjChanged` | 8914 of 8917, 8912 of 8916 | the renderer's previous view-projection is replaced per eye (item 4 needed) |
| `stampIsCounter` (mode 4) | 6007 first, 6006 second, equal to `jittered` | the stage at `pipeline+0x288` resolves in every submission with that submission's counter. **Stage identity confirmed** |
| `jitter` | first (-0.125, -0.375) px at counter 17832; second (0.125, 0.375) px at 17831 | TSAA's default pattern is the **4-sample rotated grid**, `counter & 3` (`0xF42129`, table `0x2279AC0`; `r_AntialiasingTAAPattern` `0x182B1C758`, value 1 picks by mode). The report fell between a frame's two submissions, so the readings are one frame apart: samples 0 and 3, exactly the table. **Today each eye gets two of the four positions** (first {0,2}, second {1,3}); per-eye counters give each eye all four |
| `view` | `null` after every submission | the pipeline clears `+0x2E0` before `RT_RenderScene` returns, so this check can't see the view from outside. Not needed: the mod holds the view itself |
| `tex0`/`tex1` | `hop=+0x00 size=2644x2644 format=10 mips=1 array=1 samples=1 bind=0x28` | `CDeviceTexture+0x00` is the `ID3D11Texture2D`; the history is full eye size, `R16G16B16A16_FLOAT`, SRV+RTV. `historyChanges=0`: stable for the session |

**Cost, rough.** In mode 4 the gameplay minute ran about 66 fps, against about 78 in mode 1 that morning in
another scene. That's not a controlled comparison; phase 1 must A/B the mode in one spot. Mode 4 runs only the
resolve, not SMAA (flags `0x10`). The per-eye history copies are four of 55.9 MB a frame (about 450 MB of
traffic, roughly 0.7 ms at this GPU's bandwidth): measure them too.

**What changes in the plan.** The stage pointer is now a confirmed runtime read
(`*(*0x182B2FC50 + 0x288)`), so:
- resets (item 5) can write the stage's `+0x20` directly;
- the ring can be saved and restored if the two-apart counters ever need replacing.

## Phase 0: the probe (built 2026-10-30, PreyVR-105, worn 2026-10-30)

`src/dll/TemporalAaProbe.cpp`, called around every top-level submission in `DualSceneStereo.cpp`. It reads
and never writes, so nothing in the image changes. It is gated on six whole bodies
(`include/preyvr/TemporalAaNative.h`: the post driver, the resolve, the mode flags and clamp, the pipeline's AA
execute, the view info); other bytes leave it idle with `gate=refused`. It reports through `taa.report` and
as `taa={...}` on the minute status line ([TUNING-COMMANDS](TUNING-COMMANDS.md)). Test: `temporal_aa_probe`
(a fake PreyDll and a WARP texture). Verifier: 38 checks.

The pipeline is settled statically: a thunk at `0xFCE940` loads `0x182B2FC50` and jumps to the AA execute,
which takes the stage from `+0x288`. `0xFB1670` reads the render view from the same object's `+0x2E0`
(`0xFB1732`).

**What to run.** One minute or more of play in a level. Optionally, for the stage and jitter readings:
1. write `r_AntialiasingMode 4` to the run's `commands.txt`;
2. play about 30 s (the cross-eye ghosting is expected);
3. write `taa.report`, then `r_AntialiasingMode 1`.

**How to read it.**

| Reading | Means | If not |
|---|---|---|
| `frameDelta` mostly `2:` | post-processing runs once per eye; the eye table above holds | `1:` means once per frame: redo the eye table before building |
| `first`/`second` `delta=…,1:n` | each submission advances the counter by one | |
| `prevViewProjChanged` ≈ `n` for both | the renderer's previous view-projection is replaced per eye (item 4 is needed) | |
| `view=match` | `+0x2E0` is this submission's view | the pipeline lead is wrong; find the stage another way |
| `tex0`/`tex1` `hop=… size=… format=…` | the D3D texture behind each history slot, and its size and format for the copies (item 2) | `hop=none`: the device texture's layout needs another look |
| mode 4: `stampIsCounter` ≈ `n` for both | the stage at `pipeline+0x288` resolves in each eye's submission with that submission's counter | the stage identity is wrong |
| mode 4: `jittered`, different `jitter` per kind | jitter follows the counter per submission | |

## Risks

- **Hands and weapon.** They move with the controllers, not the camera. If the near pass writes no velocity,
  TAA will smear them during fast hand motion. Neighbourhood clamping limits it; the headset decides.
- **Softness.** TAA trades crawl for some blur; the sharpening cvar exists.
- **78 fps against 120 Hz.** Fewer frames means larger motion between them and more rejection or ghosting.
- **Mode 5 (FXAA) passes through the resolve's setup** (`flags & 0x3C`). Whether its shader reads history isn't
  established; R-107 records it as non-temporal, from a wearer test.
- **Other counter users** (`0xF9F940`, `0xFD0260`, `0xFD8400`, `0xFCDFA0`) see per-eye counters too. That should
  help any of them that keep history, but they're unexamined.

## Appendix: the ring with eyes two apart

Eye A writes slot `a+f` (mod 4) in frame `f`, and eye B writes `a+f+2`. In frame `f`:
- A reads `a+f-1`. The last write to that slot was A's in frame `f-1`; B last wrote it in frame `f-3`.
- B reads `a+f+1`. The last write to that slot was B's in frame `f-1`; A next writes it in frame `f+1`.

## Replay

`python tools/re/verify_temporal_aa.py` (offline, the file on disk). Ghidra: open `PreyVR` read-only, program
`/Prey/PreyDll.dll`, functions above.
