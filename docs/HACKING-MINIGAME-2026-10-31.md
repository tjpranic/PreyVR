# Hacking minigame: black view (2026-10-31)

Branch `custom`. TODO: "Hacking minigame doesn't display, it just shows a black screen". Found statically
with Ghidra over the Steam DLL (SHA `7d6e322f…11a7`), plus one headset run's log.

**Built as PreyVR-122. Headset, 2026-10-31: confirmed working by the wearer.** Run `player-20261031-172331`:
`menu_state open=1 elements=DanielleHacking2`, 973 stick posts over 9.6 s of hacking, and no refused frame on the way
out. `tools/re/verify_hacking_ui.py` keeps the static claims, and `scheme_review` replays the real move lane through
hacking.

## The defect

Using a hackable device plays the hand animation, then the headset goes black until the minigame ends or a
menu opens.

## Why

**Prey hides the world behind the minigame, and the mod did not know it was a menu.**

1. `ArkHackingUI` sets the no-world flag `gEnv+0x2AB` (`0x224DC2B`) when `Acc_Hack_In` ends (`0x1623B30`), or at
   once when the animation cannot play (`0x1623100`, `0x1626760`). `0x1623820` clears it on the way out.
2. `CSystem::Render` tests the flag at `0xE0BAC7` and skips `RenderWorld`. No scene means no doubled scene.
3. The PDA does the same, but `DaniellePDA` is in `kMenuElements`, so the panel copies the backbuffer. Nothing
   listed `DanielleHacking2`, so the mod expected a stereo pair, refused the frame
   (`dual_first_eye_not_held`) and ended it with zero layers.

Only the PDA and hacking write the flag, so no other screen has this fault. The pause menu does not set it:
[ENGINE-INSTABILITY.md](ENGINE-INSTABILITY.md) said so until this was found.

**Observed runtime.** `player-20261031-121500`, the only one of 214 runs with this signature:

| Time (UTC) | Log |
| --- | --- |
| 01:26:08.64 | `interact … InteractiveScreens.Workstation2 Screen` |
| 01:26:09.64 | First refusal, 1 s later (the hand animation) |
| to 01:26:14.67 | 600+ refusals at about 120 fps (about 75 fps in play: the world had stopped), no panel, no `menu_state` |
| 01:26:15.93 | Pause menu: `ui_panel active=1` |

**Inference.** The run is hacking by elimination: the log never names the movie, because the Flash census runs
only inside the dual window, and no menu was up to account for the flag.

## How it plays

`0x1626CD0` takes the actions, reached from `ArkPlayerInput` (`0x158F910` -> `0x119C710`). Nothing is read while
a tutorial card is up.

- **Cursor:** `hack_moveprompt_x/y`, on `xi_thumblx/y`, stored at `+0x48/+0x4C`. Keyboard actions go to `+0x50`,
  and the update uses those instead when the control scheme is 2.
- **Prompt:** at each target `0x16269A0` picks one of `hack_final1-4` (X, Y, A, B) at random. A wrong press stuns
  the cursor.

Listing the element alone gives the wrong result: X, Y, A and B reach Prey through the menu router, but the router
turns the stick into D-pad taps, and the move lane stops posting. The minigame would show, with a cursor that
cannot move.

## What was built

- **`DanielleHacking2` in `kMenuElements`** (`HudBridge.cpp`). Panel, gameplay lanes off, tutorial card on the panel.
- **`HudHackingInputAllowed`** (`UiMode.h`): hacking is the only menu up. A pause menu or a dialog over it takes the
  stick back.
- **The move lane keeps posting the left stick** (`MoveLane.cpp`), without head-relative movement, which would
  rotate the cursor's directions as the head turned. Only that lane: `gSticksPlay` stays false, so the right stick
  does not turn the player behind the panel. `awaitNeutral` stays armed, so a stick still held as hacking ends does
  not walk. `ReassertHeld` is skipped, because the player map is off and its stored axes do not follow the posts.
- **No D-pad taps** (`XrInput.cpp`). The `default` map keeps `ui_up` and the like bound.
- **No laser** (`HudMenuTakesPointer`, `XrSessionHost.cpp`). It also stops the mouse events that can move Prey off the pad's scheme, the one
  case where the left stick would do nothing.

## Headset checklist

- The minigame opens on the panel about a second after using the device.
- The left stick flies the cursor, and its directions hold when you turn your head.
- X, Y, A and B each work when prompted; a wrong one stuns the cursor.
- No laser while hacking. It comes back if you pause over the minigame.
- The right stick does nothing. Your view has not turned afterwards.
- Pausing over the minigame gives the stick to the pause menu, and resuming gives it back.
- Success, failure and running out of time each return to the world, and a stick held at the end does not walk.

In the log: `menu_state open=1 elements=DanielleHacking2`, then `hacking_stick active=1`, and `hacking_stick
active=0` with `posted=` higher.

The movie sends Prey an `aspectRatio` (`+0x90`) that the cursor's X divides by, which is the first place to look if
it ever seems stretched on the square backbuffer.
