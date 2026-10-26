# Keypad camera too close: cause and fix scope (2026-10-26)

Branch `custom`. TODO: "Focusing on keypads brings the camera *way* too close (High Priority)". Found
statically with Ghidra over the Steam DLL (SHA `7d6e322f…11a7`), plus the FOV recorded in the headset run
logs. The fix the user asked for is a configurable pull-back in VR Options. This page gives the engine
cause, the constraints a fix has to respect, and the changes made.

**Headset, 2026-10-26 (PreyVR-94): confirmed working by the wearer.** The run log
(`player-20261026-185440`, 3 examinations) gave these readings:
- **Prey's own camera sat 119 mm from the keypad**, with an FOV of 120.0° and a `viewingBuffer` of 80 mm.
- The eye was 77–109 cm behind that camera, and the camera was moved back 300 mm, so it ended up about
  42 cm from the screen.
- No examination was capped, had no eye, came from an unknown caller, or failed a read or a write.

The report's readings from the last begin (`last={screenMm eyeMm pullMm fovDeci bufferMm}`) were there to choose
the default from. With 30 cm kept, they were removed on 2026-10-26. The counters stay.

It follows the recommended route below:
- `src/dll/ExamineCamera.cpp`: the detour on the fit, with the eye cap and no scenery ray.
- `include/preyvr/ExaminePullback.h`: the math.
- `include/preyvr/ExamineNative.h`: the three body contracts.
- The option `screen_pullback` (SCREEN DISTANCE, Interface tab, 30 cm) and the verb `examine.pullback`.
- `tools/re/verify_examine_camera.py`: 37 byte checks.
- The `examine_pullback` and `examine_camera` tests.

## The defect

Using a keypad (or a workstation, a security station or a fabricator) glides the camera up to the screen.
On a monitor the screen then fills the view, which is what Arkane intended. In the headset the camera ends
up roughly a hand's width from the keypad. That is uncomfortably close to focus on, and the keypad spans
the whole field of view.

## Why: the chain

The zoom is Prey's **examination mode**, `ArkExaminationMode` at **ArkPlayer +0x9B0**. HeadTrackingHook
already reads its state as `kFocusCameraState`.

| Step | Where | What |
|---|---|---|
| Use → zoom | `0x13DEC40` / `0x13C8830` | If the kind's auto-examine flag is set (`0x13DC290` reads the cvar table at `0x22820D0`) and no examination is running, `0x15AB250` starts one. Otherwise the use goes straight to the screen (`0x13CA320`): no zoom. |
| Begin | `0x15AB280` (set state) | The type goes in **+0x94**: 1 = world UI (the entity has a world UI, `0x1486C60`), 2 = telescope, 3 = any other entity. The state goes in +0x98, the entity id in +0x9C. It calls the fit (`0x15AB5E6`), which writes the camera's target position to **+0x68** and its rotation to +0x50. The lerp start (the eye's offset from the player entity, and the eye's rotation) goes in +0x28 / +0x34, the start time in +0x74 and the duration in +0x78 (`cameraLerpDuration`). |
| **The fit** | **`0x15AA660`** (1999 bytes) | For type 1 it reads the entity's local bounds (vtable +0x100) and **the current view camera** (`ISystem` +0x388): `m_fov` (+0x30, vertical) and `m_ProjectionRatio` (+0x40). Then `t = tan(fov/2)` and `dist = max((h/2 + buffer) / t, (w/2 + buffer) / (t·ratio))`, where `buffer` is `viewingBuffer` (**+0x7C**). The camera goes at entity-local `(−w/2, dist, h/2)`, facing the screen (the entity's axes turned 180° about Z). In short, Prey places the camera so that the screen plus a margin **exactly fills the field of view**. `distFromTarget` (+0x90) is used only by type 3. |
| Every frame | `0x15ABD20`, called from ArkPlayerCamera::UpdateView `0x148B820` | For type 1 it **re-runs the fit every frame** (`0x15ABDA5`), always into +0x68. While +0x74 > 0 it blends from the start to the target, with smoothstep `3t² − 2t³`. Once that is done it returns +0x68 (as an offset from the player entity) with the rotation +0x50, plus the look-around input, clamped by `maxCameraRotation`. |
| Kept valid | `0x15ABD20` active branch | Every frame it casts a ray from **+0x68** to the entity's position (`0x1811A4810(out, entity)` at `0x15AC9BB`), skipping the player. If the ray hits anything else, or the camera is farther from the screen than the selector's interaction distance (**player +0xC64** = selector +0xC58 + 0x0C, the 2.5 m of R-022), it tries `bMovePlayerOnExamine` (`0x15A9590`). If that fails too, it **exits the examination** (state 2). |
| FOV | end of `0x148B820` | `viewParams.fov = 2·atan(tan(hfov/2) · H / W)`, where hfov is the zoom manager's (player +0x1898) and H, W are the renderer's height and width. |
| VR | `HeadTrackingHook.cpp` UpdateViewObserved | It runs after the original UpdateView, so the eye is the examination camera plus the head offset. Under `native_projection` (VrMode.cpp) the world camera keeps Prey's own frustum. |

### Why VR comes out closer

The eye buffer is square (2644×2644 in the 10-25 headset runs), so `H/W = 1` and **the vertical FOV
equals the FOV slider**. The runs log it as `near_fov=follow deci_degrees=1174..1176`, which is about
**117.5°**. A 16:9 monitor at the same slider gives a vertical FOV of `2·atan(tan(hfov/2)·9/16)`, about 86°.
The fit fills whichever of these it is given:

- **A screen that is taller than wide** (a keypad) is fitted on its height. In VR it lands at
  **9/16 = 0.56 of the flat distance** at the same slider, and at about 0.39 of the distance at the 98°
  measured on 10-06.
- **A wide screen** (a workstation) is fitted on its width on a monitor too, so it changes much less. This
  is why keypads are the case that stands out.
- **Absolute distances** also need the keypad's bounds and the `viewingBuffer` value, which comes from the
  encrypted player config. They are not known yet. For illustration only: a 15×25 cm keypad with a 5 cm
  buffer sits **about 10.6 cm** from the eye in VR, against 18.9 cm on a monitor at the same slider. That
  works out as `(0.125 + 0.05) / tan(58.75°)`.

In a flat game the screen filling the view is the whole point. In a headset, filling a 117° view means the
screen sits right at your face.

### What it covers

Every world-UI kind whose auto-examine cvar is on. The shipped defaults (`.data`) are:

- **on:** `ui_examine_keypad`, `ui_examine_workstation`, `ui_examine_securitystation`,
  `ui_examine_fabricator`
- **off:** `ui_examine_kiosks`, `ui_examine_keycard`, `ui_examine_operatordispenser` (these are already used
  without a zoom)

Telescopes (type 2) take their camera from the entity and are not affected. Type 3 uses `distFromTarget`.

## What a fix has to respect

1. **The fit runs every frame**, so a one-off write to +0x68 is overwritten on the next frame. The
   pull-back must be applied each time the fit runs (or fed in through its input, `viewingBuffer`).
2. **If the pull-back is in the target, the lerps stay smooth for free.** Prey blends into whatever +0x68
   holds. Starting an exit sets the type to 0, so the fit stops and the exit blends out from the last +0x68.
3. **Prey's checks act on the moved camera.** The range limit is not a concern: a fit of about 0.1–0.4 m
   plus a pull-back of under 1 m is well inside 2.5 m. **Line of sight is the real risk.** A camera pushed
   behind a wall fails the ray, and Prey then moves the player or closes the keypad. The pull-back must
   never put the camera into scenery.
4. **Clicks stay consistent.** The examination cursor (+0x60/+0x64, `0x15ABC40`) is a screen position on
   that same view camera, so moving the camera keeps the cursor and the screen in agreement. The screen
   just covers less of the view. The separate TODO "Depth/pointer offset issues" is not changed by this.
5. **VR only.** Flat play keeps Prey's camera.

## The change (recommended): VR option + a detour on the fit

**Option.** Add a VR Options entry on the INTERFACE tab, next to Menu Distance and Text Panel Distance.

- Suggested key `screen_pullback`, label `SCREEN DISTANCE`, unit cm.
- Default 30, range 0–100, step 5, with 0 reading `PREY'S OWN`.
- Help text, roughly: "How far the view stays back from keypads, terminals and other screens you use.
  Prey fits the screen to the whole view, which in a headset puts it right at your face. Never further
  back than you were standing."
- Pick the default from the `examine.report` numbers on the first headset run.

**Detour on `0x15AA660`.** There is a MinHook detour in the `*Native.h` style, gated by a body contract.
The fit has exactly two callers, `0x15AB5E6` (begin) and `0x15ABDA5` (every frame), and both pass
`self+0x68` as the position. The detour does this:

1. Call the original. Stop if it failed, if `self+0x94 != 1`, if VR is not driving the view, or if the
   option is 0.
2. `f` = the camera's forward, `rot·(0,1,0)` (CryEngine's forward is +Y). For an upright screen this is
   the normal of its face.
3. **Eye cap.** On the begin call, the view camera is still the wearer's eye `E`; latch
   `e = dot(fit − E, f)`, how far behind the fit point the eye was. Then use
   `pull = clamp(option, 0, max(e, 0))`. The camera therefore never moves backwards past where you stood.
   This also keeps it out of a wall facing the screen: the eye was in front of that wall. If you are
   already standing at a comfortable distance, there is no glide at all.
4. `*pos = fit − f·pull`.
5. Optional hardening: a scenery-only ray from `fit` back to the new position (`QuerySceneryBetween`;
   main thread, which UpdateView is). Stop a few cm short of anything it hits, so Prey's own
   line-of-sight check can never close the keypad.

**Files.**

- `include/preyvr/VrOptions.h`: add a `Setting`.
- `src/common/VrOptions.cpp`: add a `Spec` row and the INTERFACE tab entry.
- `src/dll/VrOptionsRuntime.cpp`: add a `{options::…, Set<…>, Get<…>}` row. The `vr_options_wiring` tests
  pin these.
- `include/preyvr/ExamineNative.h`, with these contracts:
  - `0x15AA660` (1999 bytes): the fit
  - `0x15ABD20` (3874): every frame
  - `0x15AB280` (2394): the state setter
  - offsets: examination `0x9B0`, `+0x68` position, `+0x7C` buffer, `+0x94` type, `+0x98` state,
    `+0x9C` entity
- `include/preyvr/ExaminePullback.h`: the header-only pull-back and cap math, with
  `tests/ExaminePullbackTests.cpp`.
- `src/dll/ExamineCamera.cpp/.h`: the detour, installed and enabled with VR start/stop like the other
  lanes, plus `examine.pullback <cm>` and `examine.report`. The report gives the type, the entity, the
  bounds w×h, the buffer, fov/ratio, the fit distance, the eye depth, the applied pull, and how many
  examinations closed by themselves.
- `tools/re/verify_examine_camera.py`:
  - the three contracts
  - the `+0x7C` read off `self` at `0x15AAAA1`
  - both call sites passing `self+0x68`
  - the `+0xC64` range read at `0x15AC9DA`
- `CMakeLists.txt`; `docs/TUNING-COMMANDS.md`; `docs/PLAYER-GUIDE.md`. Move the TODO entry to DONE once it
  is worn OK.

### Hook-free alternative

`viewingBuffer` (+0x7C) is read only by this fit; the one other access in the examination code is the
loader's own write at `0x15AAEE4`. On a square eye buffer both axes share `t`, so writing
`buffer = original + offset·tan(fov/2)` moves every world-UI camera back by exactly `offset`, with no
hook. The cost:

- **No eye cap or wall clamp**, so point 3 above is left to chance.
- The value has to be re-applied after the player config reloads, and whenever the FOV slider changes.
- It has to be restored when VR stops.

This route suits a quick A/B of the distance. As the shipped fix it is weaker than the detour.

### Not zooming at all (untested)

With `ui_examine_keypad 0`, Prey sends a keypad press straight to the screen from where you stand, the
way it already handles kiosks. With the hand-driven reticle this might feel more natural in VR, and it
fits the TODO's "physical touch controls". It needs `ui_examine_*` on the console allowlist, which is
fail-closed (`src/common/ConsolePolicy.cpp`), and a headset try. It could later become a
"don't zoom" choice in the same option.

## Headset checks for the build

1. A keypad, a workstation and a security station at the default value. Each should glide in smoothly,
   sit at a comfortable distance, and glide back out smoothly.
2. `examine.report` gives the real bounds, buffer and fit distance, so the default can be chosen from data.
3. Stand very close, then press use. There should be no backwards glide (the eye cap).
4. A keypad in a narrow corridor or alcove. The examination must not close by itself (the self-close
   counter stays at 0).
5. Clicks still land on the button under the cursor.
6. Option 0 behaves exactly like Prey's own camera.
