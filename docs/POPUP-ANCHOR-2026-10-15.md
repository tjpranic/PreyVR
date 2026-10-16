# Books and notes held where they open

Branch `custom`, 2026-10-15. Code: `src/common/UiPanel.cpp` (`GazeHead`, `AnchorPopup`,
`PopupHold`, `PlacePopup`), `src/dll/HudLayer.cpp` (the note capture),
`src/dll/XrSessionHost.cpp` (the note panel). Confirmed in the headset the same day;
one defect found and fixed ([below](#headset-result-2026-10-15)).

## The problem

Book and note popups (`DanielleTranscribeNote`) shared one captured texture with
subtitles and tutorial cards, and that panel was fitted to the live head every frame
(`ui::FitPanel(inputHead, ...)`). A page moved with every head movement, which made it
hard to read.

## What changed

- **Notes and books have their own capture and panel.** `HudLayer` routes each text
  movie to a panel: `DanielleTranscribeNote` to the note panel, `DanielleSubtitles` and
  `DanielleTutorialCard` to the text panel, each with its own texture, refusal latch
  and counters. Kept apart so a subtitle playing when a note opens cannot place the
  note, and a held note cannot leave subtitles behind.
- **Subtitles and tutorial cards are unchanged:** they still follow the head, 1.4 m
  away (`ui.text`).
- **The note panel is held where it opened** (`ui::PopupHold`):
  - The first frame a note is shown takes an anchor from the head: the way the face is
    turned (`UprightHead`), plus the pitch of the gaze limited to 35° up or down
    (`kPopupMaxPitch`), with no roll. The eyes are carried with it, so the fit is sized
    as if the wearer were looking straight at it.
  - Each frame the panel is fitted from that anchor (`PlacePopup`): straight ahead of the
    gaze at the text panel's distance and size, facing the eyes, with its edges level.
    The same anchor gives the same panel, so it stays still; a changed `ui.text`,
    `ui.scale` or `ui.margin` re-places it from where it opened.
  - The anchor is let go once no note has been shown for 300 ms (`kPopupGraceNs`), so a
    frame or two the capture misses does not move it, and the next note opens wherever
    the wearer is looking then.
  - A completed recenter (Reset view) places an open note afresh in front of the wearer,
    as it does a menu. A tracking-space change or session teardown lets it go.
- **Layer order** (`ui::ComposeFrame`): world, HUD, note, text, then the laser, wrist,
  slot and fade. A subtitle reads over an open book. VR Options and game menus keep the
  note panel out, as they do the HUD and text panel.
- The text panel's upload code was folded into `UploadPanelCapture` / `FillPanelLayer`,
  which the note panel shares. The log names `text_swapchain` and `note_swapchain`.

Decided with the user: only books and notes are anchored; subtitles and tutorial cards
keep following the headset; no setting to switch anchoring off.

## Diagnostics

`hud.layer` now reports the two panels separately:
`text={captured= refused= fault= DanielleSubtitles= DanielleTutorialCard=}` and
`note={captured= refused= fault= DanielleTranscribeNote=}`. A refused note swapchain
logs `preyvr_hud_layer note_disabled reason=...`, and the note goes back to being
drawn in the scene until the HUD layer is enabled again.

## Tests

`UiPanelTests` covers the gaze head (pitch kept, roll removed, held at the limit,
straight down), the placement (distance, facing, level edges, a steep rolled opening
still fits), the hold (anchored on the first frame shown, unmoved by head turns,
kept through a missed frame and within the grace, let go after it, placed afresh on
the next opening, on a completed recenter only once and not mid-recenter, never from
an unusable head) and the new layer order. Release build: 71/71 CTest.

## Headset check

1. Open a book or note looking straight ahead, then turn and tilt the head: the page
   should stay put. Look away and back.
2. Open one while looking down at a desk: it should appear down there, tilted toward
   you. Open one while looking at the floor: held at 35° below eye level.
3. Turn pages; close it and open another somewhere else: the new one opens where you
   look.
4. With a note open: snap-turn, smooth-move, walk a step, Reset view (it should come
   back in front of you), and open and close VR Options (it should stay where it was).
5. A subtitle or tutorial card while a note is open: the subtitle follows the head and
   draws over the note.
6. `hud.layer`: `note` captured climbing while a note is open, refused and fault at 0.

## Headset result (2026-10-15)

Package `PreyVR-popup-anchor-11-wip`: **confirmed working** by the wearer, with one defect.

With a book open, opening VR Options put the page behind the panel, following the headset.
The HUD and text captures were gated on the input gate, which is false while VR Options
owns the controllers. So the book went back into the game's own image, which is fixed to
the head (F-021 in [FAILURE_REGISTRY.md](FAILURE_REGISTRY.md)).

Fixed: the captures now gate on `HudGameplayShown()`. While VR Options is open, the book,
subtitles and HUD stay captured but are left off the frame. The book keeps its anchor, so
after VR Options closes it is where it was. Built and tested offline (71/71); awaiting the
headset recheck.

## Not established

- Whether the note movie draws every frame while open. The 300 ms grace covers short
  gaps; a longer one would re-place the page where the wearer looks.
- Whether 35° is the right pitch limit, and 1.4 m the right reading distance for a page
  held still.
- The panel is the whole captured frame, with the page in the middle. Cropping the
  capture to the page, or mipmaps, would sharpen it further (as noted for the text
  panel on 2026-10-11).
