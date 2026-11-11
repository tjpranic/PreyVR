# HUD placement: the periphery and the depth (2026-11-07)

Branch `custom`. Two TODOs under HUD/UI:

- "Some UI elements still only display at the periphery of vision. They need to be moved a little
  closer in. Will need some additional VR options menu settings to adjust them."
- "Will need to adjust UI element depths. Need to adjust for HUD and psychoscope."

PreyVR-141 worn 2026-11-08: "HUD is much more readable". Two changes followed in PreyVR-142, and
subtitles at HUD Distance in PreyVR-143 ([after the first wear](#after-the-first-wear-2026-11-08)).
**PreyVR-143 worn OK 2026-11-08** ("tested and verified working"); both TODOs moved to DONE.

## Why the HUD sat at the edge of the view

Three causes, all measured from the code, the run logs and the HUD movie.

1. **Interface Size also sized the HUD.** `ui_scale` scaled the menu panel and the HUD panel alike.
   The wearer runs it at 200% because that is what the menus need. On a square canvas `FitPanel`
   then makes the HUD 72° x 72°: its edges are at ±36.1°.
2. **Prey pins its corner groups to the canvas edge.** `SafeFrame.initStage(gameWidth, gameHeight,
   stageWidth, stageHeight, SafeSize)` is always called with SafeSize 90 (both calls in the root
   script). It puts each of `safe`'s nine edge quadrants `round(extent * (100 - 90) / 200) -
   round(extent / 2)` from `safe`: 0.9 of the half-extent toward its edge, and 0 on the axis it is
   centred on. In VR the HUD's canvas is the eye's render size, 2644 x 2644. So the corners reach
   the panel's corners, where a 16:9 screen keeps them at 9/16 of that height.
3. **Off-screen markers are pinned to the edge of the lenses.** `0x16659F0` clamps a marker that asks
   for it to [0.05, 0.95] of the head camera's frame. In a 110° frame that is 52° out.

Where things sat, from `FitPanel`'s caps (60° x 40°, times the scale; the vertical cap binds for a
square canvas):

| HUD size | Edge of the panel | Corner groups, each axis | Corner groups, diagonal |
|---|---|---|---|
| 100% | 20.0° | 18.1° | 24.9° |
| 150% | 28.6° | 26.2° | 34.8° |
| 200% (the wearer's) | 36.1° | 33.2° | 42.8° |

The subtitles and tutorial cards panel has the same scale. At the wearer's 200% and Text Panel
Distance 1.0 m it hits the 2x clamp, 72° across, so a subtitle sits about 33° down. This build does
not move it (see [Not done](#not-done)).

What each group holds (`tools/re/map_hud_parts.py tree`):

| Group | Holds |
|---|---|
| NW | objective, timer |
| N, N_2 | radiation, boost, location fanfare, enemy meter; error and feedback text |
| NE | objective, tutorial prompt |
| W | pickups, controls box, pickup prompt |
| E | waveform, autosave, keycode |
| SW | power, flashlight, stealth, climb, status effects (the meters are on the wrist) |
| S | tutorial hint, stamina, PC hotbar |
| SE | ammo, speedometer, oxygen, throw, gamepad hotbar |

## The depth question

Every overlay quad is composited over the projection layer. When a quad covers something nearer
than its own depth, the two cues contradict each other: it is drawn in front, but its disparity
puts it behind. Before this build the HUD was head-locked at 2 m. The helmet's rim is real geometry
centimetres from the eyes, so wherever the HUD's corners covered the rim, the HUD looked as if it
were drawn through the helmet.

**The HUD is not put at the helmet's depth.** Eyes converge 24.1° on a point 15 cm away, 3.7° at
1 m and 1.8° at 2 m (64 mm IPD). A HUD at the rim's depth would double whenever the wearer looked
at the world, far from the headset's focus. A real visor HUD is collimated: its symbols look far
away, beyond the visor. What removes the conflict is keeping the HUD inside the visor's clear
opening, which pulling the corners in does.

The rule, decided with the wearer on 2026-11-07:
- Head-locked HUD content sits at one comfortable depth, 1 m by default and adjustable. Nearer
  than 2 m, so less of the world comes in front of it.
- World-anchored content stays at world depth: the reticle with its prompt and quick loot, and
  the markers ([marker depth](MARKER-DEPTH-2026-10-21.md)).
- Psychoscope: the wearer sees no issue now. The scan brackets and damage numbers have no ID, so
  the marker depth fix does not cover them and they still read as infinitely far. They can be
  paired as markers are if that changes.

## What was built

Four settings on the VR Options Interface tab, after Laser Hand and before Reticle Size.

| Setting | Key | Range | Default | Lane |
|---|---|---|---|---|
| HUD Size | `hud_size` | 20–200 % | 200 | `gHudScalePercent` (XrSessionState.h) |
| HUD Width | `hud_width` | 30–100 % of Prey's layout | 70 | HudLayer `SetHudWidthPercent` |
| HUD Height | `hud_height` | 30–100 % of Prey's layout | 55 | HudLayer `SetHudHeightPercent` |
| HUD Distance | `hud_distance` | 50–200 cm | 100 | `gHudDistanceCm` |

- **HUD Size** defaults to 200%, the size Interface Size gave the HUD, which the wearer kept. A file
  saved before has no `hud_size`, so it loads at 200 and the HUD looks as it did. Interface Size now
  covers the menus, notes and books, subtitles and tutorial cards.
- **HUD Width / Height** scale the 13 non-zero corner coordinates (`hud::CornerCoordinates`) toward
  `safe`. This applies to the HUD panel's own draw and to the wheel's replay, since the hotbars and
  the controls box are corner groups too. Each value is read, set and put back inside the same
  display callback, as a part's overrides are (`SpreadScope`, HudLayer.cpp). Nothing is changed
  for the wrist's replay, which crops the meters where Prey puts them. 55% height puts the corners
  close to where a 16:9 screen has them (486 of 864 units is 56%). At HUD Size 200 the defaults put the corners at
  24.6° across and 19.8° up and down (30.6° diagonally), against 33.2° before.
- **Markers** stopped at the same corners in PreyVR-141. PreyVR-142 replaced that with world-only
  markers ([below](#after-the-first-wear-2026-11-08)).
- **HUD Distance** places the HUD panel and the wheel. `FitPanel` now accepts 0.5 m to 4 m
  (`ui::kMinPanelDistance`). The world reticle's width uses the HUD's real distance, so it keeps
  its apparent size, and `HudLayerReticle` maps the crosshair through the published distance.

No new commands: the settings are menu-only, as the TODO's tuning cleanup asks.

### Checks

- `map_hud_parts.py verify`: 40 of 40. The new checks: the 13 coordinates `initStage` sets off
  `safe`, with the other 5 at 0.0; its margin `(100 - SafeSize) / 200`; and both calls passing 90.
  The tool now decodes AS2 doubles instead of printing `num`.
- `hud_parts`: the corner list against the layout, the reticle's overrides never touching it, and
  the spread arithmetic.
- `marker_depth`: world only (below).
- `ui_panel`: a 0.5 m panel covers the 2 m angle; nothing nearer than 0.5 m or further than 4 m.
- `vr_options_contracts`, `vr_options_runtime`, `vr_options_wiring`: keys, defaults, ranges, tab
  order, and each lane starting at its default and taking its whole range.
- ctest: 117 of 117 for PreyVR-141 and PreyVR-142.

### In the headset

- The HUD's corners well inside the view, and inside the helmet's opening in zero-G or EVA. If the
  rim still overlaps them, lower HUD Width / Height.
- Nothing overlapping: the top-left and top-right objectives, and the SW group against the
  stamina bar. If they collide, raise the setting.
- An objective marker shows only over its target. Turn away, and it is gone until the target is in view again.
- The equipped psi power on the wrist, left of the meters, and gone from the HUD.
- The wheel's hotbar and controls box pulled in the same way.
- `hud.layer`: `spread={width=70 height=55 draws=… refused=0}`. `vr.status` `markers={… hidden=…
  onTarget=…}`.

## Not done

- **Subtitles and tutorial cards** are still sized by Interface Size, so at 200% they sit about 33°
  down. Their movie (DanielleSubtitles) has its own layout, not read yet. The candidates: give the
  text panel HUD Size, or pull its content in as the corners are. Their depth follows HUD Distance
  since PreyVR-143 (below).
- **Psychoscope brackets and damage numbers** remain at infinite depth (above).
- **The wheel's assign flourish** (`safe.quick_mc`) flies toward where `power_mc` is in Prey's own
  layout, the HUD's lower left, not to the wrist.

## After the first wear (2026-11-08)

The wearer, on PreyVR-141: "HUD is much more readable." Two requests, built as PreyVR-142.

### 1. Markers in the world only

"Remove the off-screen markers, just have them world only." This supersedes the 2026-10-21
decision to keep a pinned marker at its target's distance, and PreyVR-141's corner box.

- In OnPreRender, a projection that asks for Prey's edge clamp (`0x16659F0` with `clamp`) is
  recomputed unclamped from its view-space point (`markers::InFrame`: the function's own `nx = (x /
  w + 1) / 2`, `ny = 0.5 - y / (2 w)`).
- If the target is in the frame, the marker goes over it. That includes a target in Prey's 5%
  margin, which Prey pinned to 0.95 (`onTarget`).
- If the target is outside the frame or behind the head, the marker is moved to `kOffCanvas` (-4,
  -4 normalized, far off the canvas, labels and all; `hidden`). Prey would pin a target behind the
  head to the edge too.
- This happens before the canvas conversion and the depth pairing read the position, so all three
  agree. Prey's `onScreen` flag is left as Prey set it. Projections that do not ask for the clamp,
  and any outside OnPreRender, are untouched.
- Cut as unused: `markers::EdgeBox`, `KeepInside`, `HudLayer HudCornerTangents`,
  `hud::CornerTangent`. The HUD Width / Height help no longer mentions markers.

### 2. The equipped psi power on the wrist

"The Psi power selection HUD element should go on the wrist with the other player status HUD
elements." That is `_root.safe.quadrant_SW.power_mc` (`EquippedPower`). Prey lays it out beside the
meters: `powerOpen` shows it and moves the three meters `meterOffset` (100 units) right, keeping their
12-unit stagger, and widens the backdrop; `powerClose` reverses that.

When Prey shows it (byte-verified, `verify_native_wrist.py`):

| Site | Does |
|---|---|
| `0x15B78B0` | equipping a power (index at `+0x234`) while none is (15) sends `powerOpen` |
| `0x15B63A0` | sets the index to 15, then sends `powerClose` |
| `0x15BCC50` | a refresh that finds 15 sends `powerClose` |

So it follows whether a power is equipped, not the dynamic HUD's timer, and the wrist draws it as
Prey shows it without forcing it. The meters' backdrop and the status effects are forced shown for
the wrist (`StatusOverride`).

- `power_mc` left `hud::ExcludedFromStatus`, so the wrist's replay draws it.
- The face's exclusions are now `hud::MovedToWrist`: every quadrant_SW clip the wrist draws. A
  test pins that it is exactly `QuadrantSWChildren` less what the replay leaves out, and
  `map_hud_parts.py verify` (41 of 41) checks `QuadrantSWChildren` against the movie.
- The card frames what it is given. Equipping a power widens it at once; unequipping shrinks it
  after about 0.7 s (NativeWristTexture's held bounds). With the icon the meters are a little
  smaller on the same card; Wrist Display Size makes up for it. (Since 2026-11-11 the card shows
  one fixed frame, and the meters keep their size: [wrist HUD scope](WRIST-HUD-SCOPE-2026-11-11.md).)

### 3. Subtitles at HUD Distance (PreyVR-143)

The wearer, on PreyVR-142: "the subtitle depth isn't controlled by the new interface depth option. I
think it should be."

- The text panel (subtitles and tutorial cards, one capture) is head-locked like the HUD, so it now
  sits at HUD Distance (`gHudDistanceCm`). Text Panel Distance (`ui_text`) places notes and books
  only, which are held where they open.
- Like the HUD, it keeps its apparent size at any HUD Distance: the size it had at Text Panel
  Distance's default, 1.4 m (scale = Interface Size x 2 / 1.4). That leaves the default look
  unchanged. At the wearer's Interface Size 200% both the old and the new rule reach `FitPanel`'s 2x
  cap, so it looks as it did and only its depth moves.
