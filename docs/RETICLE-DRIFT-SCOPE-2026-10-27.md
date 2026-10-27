# Reticle drifts off the laser after examining a screen: cause and fix scope (2026-10-27)

Branch `custom`. TODO: "Reticle drifts from the actual aim point sometimes. Happens after examining a monitor or
keypad." This was found statically, from two sources:
- Ghidra over the Steam DLL (SHA `7d6e322f…11a7`).
- The extracted DanielleHUD movie (`build/native-hud/verified/patch/libs/ui/gfx/danielle_hud.gfx`, SHA
  `35974883…c9dc`; `map_hud_parts.py verify` passes).

**Status: fix 1, PreyVR-97, worn OK 2026-10-27.** The wearer confirmed it fixed. Changes:
- `include/preyvr/NativeHudIsolation.h`: four more `ReticleOverrides`, and `MaxOverrides` raised from 8 to 10.
- `tests/HudPartsTests.cpp`: requires the two children at 0, and 10 overrides.
- `tools/re/map_hud_parts.py verify` adds four movie checks, run by `re_verifiers`:
  - `retLoc` and `iconLoc` are the two children.
  - `reticlePosition` moves only them, relative to `centerXOff`/`centerYOff`.
  - Nothing else in `Reticles` moves them.
  - Both are authored at (0, 0).

Release build, 110/110 tests. Fix 2 is not built.

## The defect

Before any examination, the crosshair sits on the right hand's laser. After the player zooms in on a keypad or
terminal and leaves, the crosshair sits a fixed angle away from the laser's hit. It keeps that offset until the next
examination ends, and that examination sets a new one. How big the offset is depends on where the hand pointed at
the moment the examination closed.

This is not caused by the screen-aim work (PreyVR-95/96, 10-26). The TODO line dates from `51b0f87` on 10-24.

## Why: the chain

Prey's HUD movie places the crosshair at two levels. The mod drives one of them and Prey's examination drives the
other.

| Step | Where | What |
|---|---|---|
| The two clips | `code.HUD.Reticles` (constructor) | `Loc` = `_root.safe.reticle_mc`. Inside it, `retLoc` = `reticle_master` (every crosshair, the hit markers) and `iconLoc` = `interact_icons`. Both children are authored at (0, 0) in `reticle_mc`. |
| Normal placement | `Reticles.reticleXOffset(v)` / `reticleYOffset(v)` | Stores `centerXOff = v` and moves the **parent**: `Loc._x = round(StageWidth·v) − round(StageWidth/2)`, and `quadrant_C._x` likewise. The children are never touched. |
| Examination placement | `Reticles.reticlePosition(X, Y)` | Moves the **children**, relative to the parent: `retLoc._x = iconLoc._x = round(StageWidth·(X − centerXOff))·inverseScale`, and Y likewise. It is the only code in the movie that moves either child. (The root timeline also calls it, but only from an authoring test frame.) |
| Prey's producers | `0x15ABC40` (call `0x15ABCFB`) | Every frame while the examination state (`player+0x9B0+0x98`) is 1, it dispatches `reticlePosition(cursor)`. The cursor is `+0x60/+0x64`, the stick-integrated value. |
| | `0x15AB280` state 1→2 (call `0x15AB3FA`) | Leaving an examination, of any type, copies player `+0x17EC/+0x17F0` into the cursor and dispatches `reticlePosition(+0x17EC, +0x17F0)` once. The begin (state 0→1) also copies them, but dispatches nothing. There are no other `reticlePosition` references in the DLL. |
| Prey's invariant | `0x1583A30` | Writes `+0x17EC = 0.5` and `+0x17F0` from the cvar, and dispatches **the same two values** as `reticleXOffset/YOffset`. So in flat Prey the field equals `centerXOff`, and the exit dispatch leaves the children at 0. |
| What the mod does | `ReticleFollow::WriteReticleForCamera`, called from `UpdateAimReticleForRender` and enabled by VR start | Writes `+0x17EC/+0x17F0` as a **fraction of the eye camera's frustum**. Prey's cached-ray unprojection (`0x1585320`) needs that. It dispatches `reticleXOffset/YOffset` as a **HUD-canvas fraction**: `HudLayerReticle`, a fraction of the 2 m HUD panel, then `ViewportToHudCanvas` (R-118). The two numbers differ, which breaks the invariant. |
| The leftover | the exit dispatch | `reticle_master` and `interact_icons` are left at `StageWidth·(viewport − canvas)` inside `reticle_mc`. Nothing resets them until the next exit. |
| What the wearer sees | the world reticle (`HudPart::reticle`, `ReticleOverrides`) | The replay centres `reticle_mc`, `quadrant_C` and `safe`, and the texture's centre is placed on the aim ray's hit. The children are not overridden, so the crosshair and the interact icons are drawn off-centre, off the laser. The HUD-panel crosshair (the fallback) carries the same offset. |

While an examination is running, the per-frame dispatch moves the children too: by (cursor − the hand's canvas
fraction). So the world reticle is also off the laser while a keypad is zoomed in.

## Size

This uses the project's formulas and this machine's settings: a 2644² eye buffer, a 120° view, `ui_scale=200`, and
the HUD panel 2.91 m wide at 2 m. Eye frusta are taken as symmetric. The real per-eye asymmetry adds a bias of its
own, even with the hand centred.

| Hand at exit (yaw, pitch) | Leftover (stage fraction) | Approx. offset |
|---|---|---|
| 0°, 0° | 0, 0 | 0° |
| 15° right, 0° | −0.026, 0 | 4° |
| 0°, 15° down | 0, −0.107 | 9° |
| 20° right, 15° down | −0.036, −0.107 | 5°, 9° |

The crosshair moves back toward the head's forward direction. Vertical is worse, because the canvas fills the
square frame's height. A gun held low when leaving a keypad gives a clearly visible offset. This fits "sometimes":
leaving with the hand still on the screen gives little offset.

## Fix options

**1. Recommended: pin the two children in the world reticle's replay.** Add four entries to
`hud::ReticleOverrides` (`include/preyvr/NativeHudIsolation.h`):

- `_root.safe.reticle_mc.reticle_master._x` and `._y` = 0
- `_root.safe.reticle_mc.interact_icons._x` and `._y` = 0

Raise `MaxOverrides` from 8 to 10.

What it gives:
- The crosshair always marks the aim ray's hit: during an examination and after it, for every examination type.
- No native call or hook, and no state to track.
- Zero is both the authored position and the position flat Prey's exit restores.
- `PartOverrides` restores Prey's values after the replay, as it does for the existing six.
- Cost: four more read/write/restore pairs per HUD frame.

Coverage:
- `tests/HudPartsTests.cpp` (`by.size()==6` becomes 10, and the children are at 0).
- A `map_hud_parts.py verify` check that `reticlePosition` moves only `reticle_master` and `interact_icons`.

Limit: the HUD-panel crosshair still carries the offset. That is the crosshair shown when the world reticle cannot
be: "reticle in world" off, or no fresh aim target, such as lost hand tracking.

**2. Complement, optional: re-centre the movie after an examination.** On the first ReticleFollow dispatch after
the examination state leaves 1, dispatch `reticlePosition(canvas.x, canvas.y)` with the values just sent as
`reticleXOffset/YOffset`. This sets `X − centerXOff` to 0, so both children return to 0 in the movie itself.

What it needs:
- `CallHudTwoFloat` (exists).
- A read of the state at `+0x98`: `ExamineNative.h` has the offsets, and `ExamineCamera` already latches the layout.

Both run on the main thread, and the state is set in the same call that dispatches, so seeing a state other than 1
means Prey's dispatch has already run. On its own this fixes the panel fallback after the exit, but not the world
reticle during the examination.

Rejected:
- Making `+0x17EC` the canvas fraction: it breaks the unprojection of the cached ray.
- Making the dispatched value the viewport fraction: it brings back R-118's canvas slide.
- Hooking the generic UI dispatcher `0x11797C0` to filter `reticlePosition`: invasive for a movie-level fix.

## Reading it in the headset

Before and after the fix, leave a zoomed keypad with the gun held low. Then queue
`hud.var _root.safe.reticle_mc.reticle_master._x _root.safe.reticle_mc.reticle_master._y`, followed by `hud.var`
to read the result. Expected readings:
- Before the fix: non-zero after an exit, and 0 before the first examination.
- With fix 1 alone: still non-zero, because the replay overrides it and restores it, but the world reticle stays
  on the laser.
- With fix 2: 0 after the exit.

The status line's `parts={reticle={... overrides=N/0}}` must keep its refused count at 0.
