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

## Known issue: the eyes disagree on moving objects (reported 2026-10-30, fixed in PreyVR-113, worn OK)

**The report.** With Temporal on, one eye looks sharper than the other (the right), only on some objects:
- the pistol, whose surface shows artefacts "as if the render buffer isn't being cleared";
- airlocks, whose textures look sharper in the right eye.

SMAA doesn't do it. A first guess, screen-space reflections, was ruled out by the wearer: with Prey's SSR and
SSDO both off, it was still there. Then: "I can see through the pistol slightly, like the depth of it is being
blended instead of tested."

That is TAA history showing through: the resolve fetched the pixel's history from the wrong place, and the
colour clip let it pass.

**The cause, found statically: moving objects' motion vectors are computed against the other eye.**

The resolve (`PostAA_PS` in `postaa.cfx`, `Engine/Shaders.pak`) reprojects each pixel from depth with
`matReprojection` (from the stage's ring, per eye since the lane), *unless* `$VelocityObjects` has a motion
vector there (`vObj.x != 0`), which it then uses instead. Static scenery takes the first path and is right in
both eyes. Objects that move, animate or are first-person take the second.

| Piece | Where | What it does |
|---|---|---|
| Who writes a motion vector | `CMotionBlur::SetupObject` `0xED8550` | Keeps each object's world matrix in a ring of 3 by frame ID; sets `FOB_HAS_PREVMATRIX` (`0x40000000`) if it moved past `r_MotionBlurThreshold` (`0x182B1C248`), **or always** for `FOB_MOTION_BLUR \| FOB_NEAREST` (`0xC00000`: animated characters and first-person objects) |
| The z-pass shader | `commonzpass.cfi`, `vertexlib.cfi` `Matrix_Inst_General_Prev` | Previous position = `CV_PrevViewProj[Nearest]Matr × PI_MotionBlurData × pos`; current = `CV_ViewProj[Nearest]Matr × instance × pos`; the difference is written to `$VelocityObjects` |
| `CV_PrevViewProjMatr`, `CV_PrevViewProjNearestMatr` | Packer `0xFB57A0`: view-info `+0x220` → payload `+0x110`, `+0x260` → `+0x150` | **Projections only**: the builder `0xFB2AC0` stores the world projection at `+0x220` and the near projection (`0xFB1530`) at `+0x260`, both jittered |
| `PI_MotionBlurData` | Instance setup `0xF001D0`: `0xFD4150` (the previous world matrix from the ring) then `0xFD4570` | **Previous world × the renderer's previous view**, with the view's translation dropped for `FOB_NEAREST` (bit 23) |
| The renderer's previous view | `renderer + 0xAB0 + 0x40 × viewport` (`0xFE5560`), viewport ID at `+0x8060` (set by `RT_SetViewport` `0xF7EA90`, 0..7) | Written **only by the post-processing driver** `0xF85230`: `0xFE7440(renderer, 0xFE49E0())`, this submission's view (`renderer + 0xF0`), right before the `+0xCF0 → +0xD30` copy the lane already handles |

So each submission's moving objects take their previous position from the *previous submission's* view, which
is the other eye's:
- **left eye:** the right eye's view from the previous frame. Moving objects are off by the eye separation.
- **right eye:** the left eye's view from this frame. Off by the eye separation, and missing a frame of head
  motion.

**First-person objects (the pistol, hands) have a second error.** Their previous view has its translation
dropped, so only its rotation counts. But the mod moves the *current* near view-projection by half the IPD per eye
(`NearViewStereo`, view-info `+0xA0`), and nothing moves the previous one (`+0x260`). Every pistol pixel's
motion vector points about half its stereo disparity away (around 100 px at 40 cm) even when nothing moves. TAA
then pulls in what's there: the background or another part of the gun, frame after frame.

**Why only some objects.** Only `FOB_MOTION_BLUR`/`FOB_NEAREST` objects, and those that moved, write motion
vectors. Which eye looks worse isn't settled statically; both are wrong.

**The fix, built 2026-10-30 as PreyVR-113, worn OK 2026-10-30 ("Tested and confirmed fixed."):**
1. **Lane** (`TemporalAaLane.cpp`): the renderer's previous views are kept per eye, as `+0xD30` already is. All
   8 slots from `renderer + 0xAB0` (`0x200` bytes) are saved after each eye and given back before it; only the
   post driver writes them. The gate now checks ten bodies, adding the slot's writer `0xFE7440`, its reader
   `0xFE5560` and `RT_SetViewport` `0xF7EA90`.
2. **Near stereo** (`NearViewStereo.cpp`, `preyvr/NearMotion.h`): the previous near projection (view-info
   `+0x260`) is moved by the same eye displacement as the near VP, and put back after the pack, as the near VP
   is.
   - The near VP is the translation-free view times that projection. So moving the near VP by `d` in the world
     is moving the projection by `d` in view space, and both are the same change to row 3:
     `-(d.x·row0 + d.y·row1 + d.z·row2)` of the *unedited* near VP.
   - It is only done when `+0x260` is projection-shaped (column 3 `(0, 0, ±1, 0)`).
   - Motion uses only x, y and w, so the depth edit isn't repeated there.

**Tests:**
- `temporal_aa_lane`: the fake post driver writes this run's view to its viewport's slot. Each eye now sees
  its own; with the lane off, each sees the other's. Mutation-checked: dropping the restore fails it.
- `near_motion` (pure): a pistol that has not moved against the head has no motion in either eye, whichever way
  the head turned since. Without the shift its motion is the eye's parallax. Mutation-checked.
- `verify_temporal_aa.py`: 64 checks, the chain above included.

**Headset checklist (PreyVR-113):**
- **What to look at:** the pistol held still and moved, the airlocks, an animated NPC, in each eye. Expect no
  see-through or "uncleared" smear; the eyes should match.
- **Status line:**
  - `taaLane` `viewsIn` close to `copiesIn`;
  - `nearPrevShifted` keeping pace with `nearApplied`;
  - `nearPrevRefused=0`.
- **If the pistol is better but airlocks are not, or the reverse:** the two parts can be told apart. Part 2 only
  touches first-person objects.

**Also noted (not the cause): SSR under the lane.** SSR `0xF9F940` reads its previous view-projection from its
own ring of 4 by the counter (`+0x2064`, read `(c-1) & 3`). But it samples `$HDRTargetPrev` (`0x182B3F818`),
which the HDR stage `0xFB00C0` refreshes once per submission after SSR. The lane therefore pairs this eye's
camera with the other eye's image. The wearer's SSR-off test didn't change what they saw, so this is minor.
The fix would be a per-eye copy of `$HDRTargetPrev`.

**Ruled out:**
- **TSAA mip bias** (`r_AntialiasingTSAAMipBias`, `0xF7D710`): global.
- **`0xFD8400` (`PostEffectsGame`)**: 64x64 targets by `c & 1`.
- **`0xFD0260`**: noise seed.
- **`0xFCDFA0`**: one-time history clear.

## Known issue: world objects with motion vectors are wrong in the second eye (reported 2026-10-31, fixed in PreyVR-121, worn OK)

PreyVR-113 did not finish the job above. On the wearer's report, the recycle symbol on the recycler is a
different texture in each eye, and the left eye looks right.

**Worn OK 2026-10-31: "Tested and confirmed working."** The fault was gone at once, so the wearer skipped the
`taa.motion` A/B. The cause below is therefore a static reading backed by the fix working, not by turning it off
and on. Before the fix, airlocks had become sharper in the left eye, the reverse of before PreyVR-113, as
predicted. The other prediction didn't hold: they saw no smearing on enemies, and that's unexplained.

Run `player-20261031-142929`, 86,440 frames:
- `pipeline=2` and `perDraw=0`: objects compile once, as read statically.
- `given=86436`, with 4 `noViews` for the run's 4 restarts, and `badViews=0`.
- `separationUm=64941` against an IPD of 65,044 µm.
- `packed` about 90 and `unshaped` about 5 per second eye. Those 5 were expected to be 0. They hold steady through
  the run and aren't identified. They're packs whose `+0x220` isn't perspective-shaped, so they go out as Prey
  built them, and they showed no effect in the headset.

**The symptom.** The symbol is painted in the diffuse map only (`recycler_a_3_diff`; not in the normal or
specular maps), so reflections and normal maps can't be the cause. The recycler and `airlock_a` are characters
(`.cdf` with a `CA_SKIN` attachment).

**Inference: the cause.** Both eyes draw one render view, and each object's constants are built once.
- `SwitchUsageMode` (`0xEE82F0`) returns at once when the mode is unchanged. Only the switch to Reading (1)
  runs `CompileModifiedRenderObjects` (`0xEE46D0`).
- The mod holds back ReadingDone after the first submission, so the second submission's switch to Reading does
  nothing. Objects compile once, inside the first eye's `RT_RenderScene`.
- Compile runs `0xF001D0`, which stores previous world x previous view (`0xFD4570`) in the per-instance
  constants. `DrawAsync` (`0xF00A90`) only binds that buffer.
- The velocity shader then multiplies those constants by `CV_PrevViewProjMatr`, which is per eye.

So the second eye computes each flagged object's previous position from the first eye's previous view. Its
motion vector is a constant stereo disparity, even with the head and object still: focal 926 px x 65 mm IPD /
distance, or about 60 px at 1 m and 30 px at 2 m. The resolve then reads history from that far away, and the
neighbourhood clip only limits the damage in flat areas. First-person objects escape, because `0xFD4570` drops
the view's translation for them. That is why the pistol was fixed.

The error moved with PreyVR-113. Before it, compile read the view the second eye's post run had left, so the
first (left) eye was wrong and the right eye "sharper", as reported. The lane's restore put the first eye's view
there, so the error should now be in the second (right) eye.

**Why still objects are flagged.** A character gets `FOB_MOTION_BLUR` when `ca_MotionBlurMovementThreshold`
(default 0) is below its movement, the sum of `1 - |dot(q, prev q)|` over its joints (`0x82EE10`). An unmoved pose
should give 0, but float rounding doesn't. A model of that arithmetic, not the game's own bones, gave movement
above 0 for about 47 to 87% of poses, and exactly 0 for about 3 to 35%. Which objects are affected is then
effectively arbitrary but stable per pose, which would fit "only certain objects". `0xFD4150` and `0xED8550`
need the object within `r_MotionBlurMaxViewDist` (16 m at the default spec).

**What this rests on.** `r_GraphicsPipeline` picks the path: 2 or more draws compiled objects, and below 2
compiles per draw, per eye, in which case this reading is wrong. Its registered default is 2 and no config in the
game or its paks sets it, and the worn run read 2 (`pipeline=` in `taaLane`). PreyVR-121 leaves the second eye
alone below 2. Whether the recycler is flagged in a given pose hasn't been read.

**Ruled out.**
- `$VelocityObjects` is cleared each submission (`0xF3B070`).
- The temporal-AA branches outside the resolve are the jitter, a global mip bias (default -0.25) and render target
  setup. Each eye walks all four jitter samples.

**The fix (built 2026-10-31, PreyVR-121).** For the second eye only, view-info `+0x220` becomes
`V1^-1 x V2 x P` while it packs, restored after. That cancels the first eye's view from the constants, for one
matrix a submission and no per-object work.
- **`V1`** is the view the objects really compiled against: the lane reads the viewport's slot just before the
  first submission, because by the second that eye has stored this frame's view.
- **`V2`** is the second eye's own previous view, which the lane has just put back.
- **The maths** (`preyvr/WorldMotion.h`) keeps the exact stored matrices, in doubles, rather than an IPD
  estimate, so the rounding in `V1` cancels the rounding compiled into the constants. It's a full matrix, not a
  row shift, in case the eyes are canted.
- **The seam** is `NearViewStereo`'s pack, with the `+0x260` edit, under the same guards.

It stands aside, and counts why, when a previous view is missing or isn't a view, when objects compile per draw,
or when `+0x220` isn't perspective-shaped. All 15 gate rows must match, including the compile, the constants and
the compiled draw. `taa.motion 0|1` turns it off and on for an A/B ([TUNING-COMMANDS](TUNING-COMMANDS.md)).

Other velocity writers: the main view's G-buffer stage only draws compiled objects, so nothing there takes the
per-draw `PI_MotionBlurData` path (`0xF1D0A0`), which would already hold the right view. Water reflections
(`waterreflectionspass.cfi:294`) also read this constant, as a view-projection, which `+0x220` already isn't,
so they should lose nothing. Raising `ca_MotionBlurMovementThreshold` would hide the idle cases only.

**Tests.**
- `world_motion`: at rest, moving, canted eyes, and 2 km from the origin.
- `temporal_aa_lane`: the matrix is only offered to the second eye, from the compiled view, and is withheld for
  each refusal.
- `verify_temporal_aa.py`: 79 checks.

**Headset checklist.**
1. At the recycler, the symbol should match in both eyes. `taa.motion 0` should bring the difference back in the
   right eye and `taa.motion 1` clear it, with no other change. That A/B is the proof of the cause.
2. An airlock should look the same in both eyes.
3. Move the head while looking at the recycler: no new smearing or swimming in either eye.
4. The status line or `taa.report` should show `pipeline=2`, `given` and `packed` climbing, `unshaped` about 5 per
   frame, `noViews` about one per restart, `badViews=0`, and `separationUm` near the IPD (about 65000).

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
  *2026-10-30:* it always writes velocity (`FOB_NEAREST`), but against the wrong previous view and without the
  eye offset. See [the known issue](#known-issue-the-eyes-disagree-on-moving-objects-reported-2026-10-30-fixed-in-preyvr-113-worn-ok).
- **Softness.** TAA trades crawl for some blur; the sharpening cvar exists.
- **78 fps against 120 Hz.** Fewer frames means larger motion between them and more rejection or ghosting.
- **Mode 5 (FXAA) passes through the resolve's setup** (`flags & 0x3C`). Whether its shader reads history isn't
  established; R-107 records it as non-temporal, from a wearer test.
- **Other counter users** (`0xF9F940`, `0xFD0260`, `0xFD8400`, `0xFCDFA0`) see per-eye counters too. Examined
  2026-10-30, after the wearer's report: SSR (`0xF9F940`) is, mildly. See
  [the known issue](#known-issue-the-eyes-disagree-on-moving-objects-reported-2026-10-30-fixed-in-preyvr-113-worn-ok).

## Appendix: the ring with eyes two apart

Eye A writes slot `a+f` (mod 4) in frame `f`, and eye B writes `a+f+2`. In frame `f`:
- A reads `a+f-1`. The last write to that slot was A's in frame `f-1`; B last wrote it in frame `f-3`.
- B reads `a+f+1`. The last write to that slot was B's in frame `f-1`; A next writes it in frame `f+1`.

## Replay

`python tools/re/verify_temporal_aa.py` (offline, the file on disk). Ghidra: open `PreyVR` read-only, program
`/Prey/PreyDll.dll`, functions above.
