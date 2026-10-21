# World markers at the depth they mark (2026-10-21)

Branch `custom`. TODO: "Objective markers don't display at the proper depth... Repair markers
(and possibly enemy detection markers) also affected." Found statically (Ghidra and capstone over the
Steam DLL, and the markers movie read offline). **Headset, 2026-10-21 (PreyVR-57): confirmed working
by the wearer.**

**Marker size.** Before 2026-10-21 no setting controlled it, in Prey or the mod. Since then VR
Options has **Marker Size** (Interface tab, `marker_size`, 50–200%, default 100; `markers.size`):
- Each marker the depth table names is scaled, icon and label together, about the point it marks
  (the clip's origin).
- The scale is applied through the clip's `_xscale`/`_yscale`, read and set for both eyes' draws and
  put back after, like the depth shift.
- It works with `markers.depth 0` too. Damage numbers and the psychoscope brackets are not in the
  table, so they keep Prey's size.

Why there was nothing before:
- The movie scales every marker clip by `_root.scalePercent`. `cry_onResize` → `SafeFrame.initMarkers`
  sets it from the viewport alone: `round(min(gameH/1080, gameW/1920) · 100)`.
- Prey's own options only switch markers on or off (`hud_showObjectiveMarkers`,
  `hud_showEnemyMarkers`).
- `hud_onScreenNearSize`/`FarSize`/`NearDistance`/`FarDistance` are referenced only by the game
  cvars' unregister list (`0x17291AF`), never registered. They are CryEngine leftovers.
- The mod's Interface Size (`ui_scale`) scales the panels, not the markers, which are drawn into the
  scene.

## The defect

Every world marker is in one Flash movie, `DanielleMarkers` (`libs/ui/gfx/danielle_markers.gfx`, in
patch.pak). It is the one gameplay movie still drawn into the scene. In a both-eyes frame it is drawn
into the backbuffer (the second eye), then redrawn into the held first eye at the same pixels
(`HudLayer` `RedrawIntoHeldEye`, [STEREO-DUAL-SUBMISSION](STEREO-DUAL-SUBMISSION-2026-10-10.md)).
With zero disparity every marker reads as infinitely far away. Run logs from 10-19 to 10-21 show
`DanielleMarkers first=redrawn` and nothing else from the gameplay movies in the scene.

What the movie holds. Each kind of marker is a clip named after its engine ID, placed directly with
`_x = X; _y = Y` under parents with no transform:

| Kind | Clip | Producer (game thread) | Flash call |
|---|---|---|---|
| Objectives | `_root.ar_mc.objectives.<EntityId>` | `0x1577040` | `updateObjectiveMarkerPosition(id, X, Y, level, alpha, textAlpha)` |
| Repair, danger | `_root.ar_mc.ar.repair<n>` / `danger<n>` | `0x13259C0` → `0x1324C40` | `repairAdd/Update`, `dangerAdd/Update(id, X, Y, rot, arrow)` |
| Enemy awareness | `_root.ar_mc.ar.markersAttach.<EntityId>` | `0x15A7C10` | SetArray `markerUpdateArray`, 11 values each `[id, X, Y, ...]` |
| Points of interest | `_root.ar_mc.objectives.poiAttach.<EntityId>` | `0x132EAA0` | SetArray `poiPositions`, `[id, X, Y]` each |
| Target | `_root.ar_mc.ar.<int>` | `0x15B5400` | `targetAdd(id, text, X, Y, on)`, `targetUpdate(id, X, Y, on)` |
| Damage numbers | `_root.ar_mc.dmg.hit<i>` | `0x1377F60` | `hitDamageUpdate` `[X+dx, Y+dy, scale]`, no ID |
| Psychoscope brackets | `_root.scanning_targets...` | `0x15DAB20`, `0x15DA060` | smoothed X/Y, no ID |

All of them are placed in `CArkUIHUD::OnPreRender` `0x1667440`. It builds one view-projection from the
frame's camera (the head) at `0x228CAA0`: LookAt times a symmetric perspective from `m_fov` and the
projection ratio. Every marker is projected through `0x16659F0`
`out*(out{x, y, onScreen}, const Vec3* world, bool clamp)`, which computes the view depth and
discards it. Off-screen or behind targets are pinned to the `[0.05, 0.95]` box. Canvas units
(1920×1080, the element's viewport) come from `0x16658C0` (project and convert) or `0x16657C0`
(convert only, used by the awareness markers). No marker sends a distance to the movie. VR mode keeps
Prey's own frustum (`SetNativeProjection(1)`), so this matrix matches the eyes and markers already
point the right way; only depth is missing.

## The fix

`preyvr/MarkerDepth.h` (pure, `marker_depth` tests) and `src/dll/MarkerStereo.cpp`.

**Depth, game thread.** Six hooks are installed together or not at all, each gated on its prologue:
- `0x16659F0` keeps each marker's point in the head's view space, using the same matrix.
- `0x16658C0` and `0x16657C0` turn it into a pending record: canvas X/Y, plus how far the clip moves
  per metre of eye offset.
- `OnPreRender` clears the pending records at the start of each frame.
- The element's `CallFunction` (`+0x210` `0x2F8E70`) and `SetArray` (`+0x288` `0x2FFC20`) are read for
  `DanielleMarkers` only. They give each marker's ID with the canvas X/Y it was projected to,
  bit-identical. A single call claims the newest pending record at that X/Y, and an array group
  claims the oldest, so two markers pinned to the same edge point stay apart.
- The result is a table of clip path to shift, kept for 0.5 s after its last update.

Argument layout, as read from the builders and readers:
- `SUIArguments+0` is a `DynArray<TUIData>`. The count is at `p-4`, with bit 31 a flag.
- `TUIData` entries are 0x20 apart: type at `+0` (0 int, 1 float, 2 EntityId, 4 string), value at
  `+0x10` (a string's is a character pointer).
- Ints and EntityIds reach Flash as signed Numbers, so a clip's name is the ID's decimal text.

**Which depth** (decided 2026-10-21): on screen, the target's view depth. Off screen, the target's
*distance*, taken along the direction the marker is drawn in. One formula covers both:
`distance / sqrt(1 + tx² + ty²)`, where `tx`, `ty` are the drawn position's tangents. Markers behind
walls keep their true depth. Nothing is drawn nearer than 0.25 m, the world reticle's floor.

**Shift, render thread.** An eye `o` metres right of the head sees the marker move
`-o · P00 / (2·depth)` normalized units (no vertical change). `P00` is the length of the matrix's x
row. Canvas units per normalized unit come from Prey's own conversion, `0x16657C0(1) − 0x16657C0(0)`,
taken once a frame after a conversion of Prey's has succeeded (it does not check that the element
exists).

Inside the movie's one display callback (`HudLayer` `RedrawDisplay`), each tabled clip's `_x` is read
with the movie root's GetVariable. It is then set for the native draw (the second submission, the
right eye, `+IPD/2`) and for the held first eye (`-IPD/2`), and restored. The IPD is the stereo
path's own (`SyntheticStereoIpdMetres`). About 9 canvas px per eye at 2 m.

Not covered (no ID): damage numbers and the psychoscope brackets. Damage numbers could be paired by
slot order and the brackets by their fixed clips, if wanted.

## Commands and diagnostics

- `markers.depth [0|1]`: on by default; 0 draws markers where Prey puts them, for A/B. With no value
  it only reports. `PREYVR_MARKER_DEPTH=0` leaves them unhooked.
- The once-a-minute status line adds
  `markers={depth hooks projections named unmatched badArgs pendingFull noted table full frames moved last unresolved restoreFailed lastShiftCentipx}`.
- The log names `first_moved path=` and `first_unresolved path=` once each.
- Reading the counters:
  - `named` far below the marker calls means the X/Y pairing is failing.
  - `unresolved` climbing with `moved` at 0 means the movie does not resolve the clip paths.
    Numeric clip names (`...objectives.1234._x`) are the untested assumption.

## Headset check

1. An objective in view, then walk toward it: its marker sits at the objective, and both eyes fuse on
   it as on the object. Near and far objectives both.
2. A broken object with a repair icon, close by: the icon is on the object, not far beyond it.
3. Enemy awareness markers above an enemy's head, at the enemy's depth.
4. Turn away until a marker is pinned at the edge: it stays at the target's distance and does not jump.
5. Behind a wall: the marker shows through, at its true depth.
6. `markers.depth 0` against `markers.depth 1`.
7. In the status line, `moved` should climb and `unresolved` stay near 0.
