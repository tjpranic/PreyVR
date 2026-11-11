# Wrist status: no glass, no resizing (2026-11-11)

Branch `custom`. TODO (HUD/UI): "Remove the background from the wrist status HUD. Also remove the resizing." The
user also asked that the wrist's size come only from the existing VR Options size settings, and to fold in "Psi
energy shows on wrist before installing first neuromod" if it was cheap.

The work was found statically, from the source, the HUD movie (`danielle_hud.gfx`, SHA `35974883…c9dc`, through
`tools/re/map_hud_parts.py`) and byte reads of the Steam DLL (SHA `7d6e322f…11a7`). It was built as PreyVR-150 (ctest 117 of 117,
`verify_native_wrist.py` 16 of 16).

**Worn on PreyVR-150, 2026-11-11.** The wearer confirmed that psi shows only after a neuromod and that Wrist
Display Size works, but the status was cut off. **PreyVR-151 widens the frame** ([below](#after-the-first-wear-preyvr-151)).
**Worn on PreyVR-151:** "Better, but the display is still cut off, and it looks stretched". **PreyVR-152 fixes the
stretch** ([below](#after-the-second-wear-preyvr-152)), which was the real cause (ctest 117 of 117).

**PreyVR-152 worn OK, 2026-11-11: "Tested and verified working. Everything looks good."** Both TODOs are in DONE.
Run `player-20261111-195350` confirms the mapping: `nativeWrist={captured=2001 refused=0 layout=2001/0
stage=1920x1080 safe=100,100 capture=2644x2644 cover=2.448 ppu=2.000}`, exactly what the cover model predicts.
The wearer's `wrist.report`: `psi=1 sizeMm=352,154 frame=-200,-380,900,100 content=-96,-137,350,92 samples=62
outside=10`. The sampled box is inside the frame. But 10 of the 62 samples reached past an edge, and only the
last box is kept, so which edge is not known. The likely source is a transient animation: `power_mc`'s authored
bounds reach x −458 and y 128 (its flash and glow), past the frame's left and bottom, and this sample already
reaches y 92. Nothing looked cut off to the wearer. Left as it is: see
[open](#open-after-the-wear).

## Decisions (the user, 2026-11-11)

- **D1. Wrist Display Size is the card's only size.** Its range is now 50–200%; it was 80–140%. It stays on the
  Equipment tab.
- **D2. The unfold is removed.** The card only fades.
- **D3. Equipping a power follows Prey's own layout.** The meters slide right and the icon appears on their left,
  as on the flat HUD.

## Before

The card was one quad on the left forearm. Each frame it took this path:

1. **Capture** ([HudLayer.cpp](../src/ui/hud/HudLayer.cpp), `CaptureNativeStatus`, render thread). Prey's HUD movie
   drew a second time into a cleared texture the size of the eye target, with only `quadrant_SW`'s meters,
   `power_mc` and `status_effects`. `StatusOverride` flattened the quadrant, scaled it to 400% of the widget scale
   (`wrist.native`), forced the three meters visible and hid Prey's own backdrop.
2. **Fit** ([NativeWristTexture.cpp](../src/ui/hud/NativeWristTexture.cpp)). One pass found the box around every
   visible pixel. A second held it: it grew at once, but shrank only after 60 frames. A third scaled that box to
   fill the card, then drew **the glass** under it: the "background", a dark rounded rectangle with a bright rim and
   scan lines.
3. **Place** ([WristLane.cpp](../src/ui/hud/WristLane.cpp)). The card was 125 mm × Wrist Display Size × the unfold
   wide, and the unfold grew it from 90% to 100% as it faded in.

**The resizing was the fit.** Positions are in `quadrant_SW`'s units, with y pointing down as in Flash:

| What | Where |
|---|---|
| Meters | icons from x 89, bars from x 130–158; y −112 to 0 (health at the top, psi at the bottom) |
| Bar length | `(100 + max) × scale`: health up to 360 units at max 300, psi at scale 0.8, armor a fixed length |
| Equipped power | icon x −28 to 120, y −114 to 22; the meters move 100 units right while it shows |
| Status effects | a row just above the meters, y −212 to −111 |

Each of these changed the size of the meters:
- **A status effect** roughly doubled the box's height, so every meter shrank.
- **Equipping a power** widened the box, and so did longer bars after an upgrade.
- **Either one going away** grew the meters back about 0.7 s later.

The fit also hid something. Prey sways and shakes the HUD by moving `_root.safe` (`Camera.HUDPos`), and the fit
re-centred on the content every frame. A fixed crop has to pin `safe` instead.

## Built (PreyVR-150)

- **A fixed frame** ([WristFrame.h](../src/ui/hud/WristFrame.h), pure and tested). `kFrame` is x −40..640,
  y −220..30 in quadrant units, which holds every row in the table above. `wrist::Place` reads the stage
  (`cam.m_halfStageWidth/Height`, `safe._xscale/_yscale`) and the capture's size. For the replay alone it then
  writes, and puts back right after the draw:
  - `safe` at the stage centre, so no HUD sway reaches the wrist (the reticle part does the same);
  - `quadrant_SW` flat, at an absolute scale, and placed so that the frame's centre is the capture's centre at 2.4
    pixels per unit.

  On the headset's 2644 × 2644 eye target, the frame is pixels 506..2138 × 1022..1622: a 1:1 copy into the
  1632 × 600 card.
- **No fitting, no glass** ([NativeWristTexture.cpp](../src/ui/hud/NativeWristTexture.cpp)). The rectangle comes
  with the capture (`NativeWristImage::frame`), and the pass maps it onto the card. It keeps the colour bytes,
  premultiplication, the fade, and four-tap minification for a capture too small to fit the frame at that density.
  The held bounds and the glass are gone.
- **Size** (`wrist::Size`). The card is the frame at 0.32 mm a unit × Wrist Display Size: 218 × 80 mm at 100%,
  most of it clear. Its centre moves 32 mm along and 12 mm up from the placed point, so the meters' middle
  (x 200, y −56), not the frame's, sits at Wrist Display Position and Height. The occlusion samples span the
  meters (±130 units). `wrist.place` no longer takes a width, and the hologram and the grip card share one size.
- **No unfold** (`Presenter::Scale` removed).
- **Retired:** `wrist.native` (both its scale and its backdrop flag) and `WristCardWidthMetres`. Prey's backdrop
  stays hidden.
- **Psi before the first neuromod.** The replay used to force `psi_mc` visible. It now shows psi only while Prey
  would ([NativeWristNative.h](../src/ui/hud/NativeWristNative.h)):
  - **Prey's own test.** `0x15AD110`, called with `*(player + 0x678)`, is true when the owner's byte `+0x39A` is
    set, or when one of its 16 power slots (`owner + 8 + 0x40`) is learned (vtable `+0x18` above −1, listed by
    `0x15B8140`). The HUD refresh that opens the meters together (`0x1726B50`) and the psi refresh (`0x15D8540`)
    send `psiOpen` only while it holds.
  - **The other paths.** Two paths open the meter without that test (`0x15D5D70` from the power code `0x15BB290`
    and from the setter `0x15D8FF0`), so the wrist also shows psi while `_root.psiM.isOpen` is true.
  - **Where it runs.** WristLane reads the test on the main thread, about 6 times a second, behind a whole-body
    contract (`PsiShownContracts`). Until it can be read (`psi=-1`), psi is shown, as before.
- **Diagnostics for the first wear** (`wrist.report`):
  - `content=` is the box one capture in 30 really held, in quadrant units, read back without waiting.
  - `outside=` counts the samples that reached past the frame. It should stay 0.
  - `psi=` is the test's last answer, and `sizeMm=` the card's size.
  - `hud.layer`'s `nativeWrist={… psi=shown/hidden}` counts the replays.

### Not as scoped

- **The frame came from the authored layout, not from an offline bounds tool.** Shape bounds over every timeline
  frame come out far too large, because masks, noise overlays and waveform loops dominate them (health alone
  spans −226..798 × −342..170). So `kFrame` is set from the clips' placements and the bar-length formulas, and
  `content=` checks it on the first wear.
- **0.32 mm a unit is an estimate** of the old no-power, no-effect size. The old fitted size depended on which
  clips had visible pixels, which was not measured. If 100% looks wrong, Wrist Display Size covers it, or the
  constant changes.

### Tests

- `wrist_holo`: `Place` lands the frame on its rectangle for a pixel stage, a scaled non-square stage, a
  half-scale `safe` and a small capture; it refuses an unusable stage; `ToFrameUnits` and `Outside`; the card's
  size and offset at 100% and 200%.
- `native_wrist`:
  - **another blob, inside the frame or out of it, leaves the meters' pixels unchanged** (the regression this
    exists for);
  - the mapping is exact at known points, in all four formats;
  - nothing shows outside the content, so there is no glass;
  - fade and refit;
  - the content readback and how often it samples;
  - minification;
  - pipeline state and predication are restored.
- `command_channel`: the `wrist.native` row is removed.
- `verify_native_wrist.py`: the three new contract rows, plus the predicate's and both psi refresh sites' bytes.

## After the first wear (PreyVR-151)

The wearer: "I'm seeing an issue where the UI elements are cut-off. We need to increase the size of the display
container (and have it increase in size with the Wrist Display Size setting)."

- **Run `player-20261111-192058`.** The capture was sound: `nativeWrist={captured=2250 refused=0 fault=0
  layout=2250/0 psi=1257/993}`. Every replay was placed and drawn, and psi was hidden in 993 of them, which is the
  neuromod check the wearer made. `wrist.report` was not run, so the log has no `content=`.
- **The cause is the frame.** It left only 8 to 22 units past the furthest authored positions: power icon −28 vs
  −40, the health bar's end at 618 vs 640, status effects −212 vs −220, power icon 22 vs 30. That leaves no room
  for the icons' pop-in and glow, the numbers or a row of status effects (50 units each, `StatusEffects.xOffset`).
- **It already followed Wrist Display Size.** The card and its contents scale together, so a larger size only
  enlarged what was cut. The frame has to be larger in Prey's units, and it still scales with the option.
- **The change** (`wrist::kFrame`):
  - −200,−380 to 900,100, so 1100 × 480 units, at 2 pixels a unit: the texture is 2200 × 960. On the 2644 eye
    target that is still an exact copy (pixels 222..2422 × 842..1802).
  - The meters keep their size: 0.32 mm a unit is unchanged. The invisible card is 352 × 154 mm at 100%.
  - The meters stay where they were on the forearm: the card's centre is now 48 mm along and 27 mm up from the
    meters' middle.
- **Tests**: `wrist_holo` and `native_wrist` are updated for the frame. A new check covers what overran the first
  frame by a margin on every side, which now fits.
- **To confirm on the wear:** nothing is cut off with a power equipped and status effects showing.
  `wrist.report` should show `outside=0` and a `content=` box inside `frame=`.

## After the second wear (PreyVR-152)

The wearer, on PreyVR-151: "Better, but the display is still cut off, and it looks stretched."

- **Stretched and cut off at the sides are one fault.** `wrist::Place` assumed each axis maps onto the capture by
  its own ratio: 2644 / 1920 across and 2644 / 1080 down, from `m_halfStageWidth/Height` (960 and 540, which are
  `Stage.width/height × 0.5`).
- **What Prey does instead** (already measured: R-118, R-122 and F-017 in RESEARCH_LOG.md, with
  `aim::ViewportToHudCanvas`): the HUD's 1920 × 1080 canvas is scaled **uniformly** by the larger ratio, so that
  it covers the frame, and centred. On the square eye target that is 2644 / 1080 = 2.448 in both axes.
- **So the content came out 1.78× too wide.** Across, the replay set 2.0 / 1.377 = 1.45% per unit, which drew at
  1.45 × 2.448 = 3.55 pixels a unit against the 2 the crop expected. Down, the scale was right, which is why it
  read as stretched rather than smaller. The frame then held only 1 / 1.78 of the width around its centre: 382
  units in PreyVR-150 and 619 in 151. That is why widening it helped but did not fix it.
- **The fix.** `wrist::CoverScale` is the larger ratio, the same scale for both axes. On the headset the quadrant
  scale is 200 × 1080 / 2644 = 81.7% on both axes, and the frame is still pixels 222..2422 × 842..1802.
- **What the meters look like now.** They keep the height they had. Their width is now correct, so a bar is
  1 / 1.78 as long as on PreyVR-150 and 151. Wrist Display Size is still the only size.
- **The evidence is now logged.** `hud.layer`'s `nativeWrist={… stage=WxH safe=x,y capture=WxH cover= ppu=}` shows
  what the replay read and used. Expected on the headset: `stage=1920x1080 safe=100,100 capture=2644x2644
  cover=2.448 ppu=2.000`.
- **Tests.** In `wrist_holo`, the projection now models cover. A new check fails unless a square in quadrant units
  lands as a square in the capture; the old formula gave 355 × 200 pixels. The checks run on the headset canvas,
  a pixel stage, a 16:9 screen and a wide screen.

## Headset checks

- The meters keep one size with no power, with a power equipped, with a status effect, and after a health or psi
  upgrade. A power makes them slide right; nothing shrinks.
- There is no wobble on the arm while walking, jumping or taking damage.
- There is no glass, and the meters are readable against a bright wall and a dark room. If they aren't, the native
  fallback is Prey's own backdrop (`bg_mc`), not new art.
- Wrist Display Size changes the size, and 100% is about the old size.
- The meters sit where they used to on the forearm (Position and Height).
- Psi is absent before the first neuromod and present after it. Elsewhere it shows as before.
- **Read after the wear:** `wrist.report` should show `outside=0`, and `content=` with a power and an effect
  showing.

## Open after the wear

- **`outside=10` of 62.** Something brief reaches past the frame. To find out which edge, `wrist.report` would have
  to keep the union of the boxes that were outside, not only the last box. Then the frame could be widened on that
  side alone. Widening it everywhere to the power's authored bounds (x −458, y 128) would make it about 1360 units
  across. That no longer fits at 2 pixels a unit on a 2644 target, so it would be minified.

## Out of scope

- Funk's grip card (`wrist.anchor 0`) uses the same texture and size. Removing it belongs to the tuning cleanup.
