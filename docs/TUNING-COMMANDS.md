# Tuning command reference

Every command **Tune Prey VR.cmd** accepts, what it changes, its values and
default, and whether anything keeps it past this session. Updated 2026-10-24.

The commands are not a settings menu. Most of them are developer and diagnostic
switches. The ones a player would reasonably change are also rows in the
in-headset **VR Options** menu, which saves them. Use the menu for those. Use a
command here to try a value for one session, or to reach something the menu
does not show.

## Sending a command

- Run **Tune Prey VR.cmd** beside the launcher. Type a command at its prompt,
  or pass one directly: `Tune Prey VR.cmd ui.distance 250`.
- The tool writes the line to `commands.txt` in the newest run folder under
  `%LOCALAPPDATA%\PreyVR\runs`. The mod reads it, then writes its reply to
  `results.txt`, and the tool prints the reply.
- A reply starts with the command and `result=`:
  - `result=0` means it was applied, or for a report, read.
  - Anything else means it was refused and nothing changed. `1` and `87` mean a
    value out of range or the wrong number of values. `50` means the game's
    code did not match what the feature hooks, so it stays off.
- Values are whole numbers. Units are in the name or in the tables below: `cm`,
  `mm`, degrees, percent, or hundredths of the stick's travel for deadzones.
- Numbers are decimal. A leading zero changes nothing (`0310` is 310), and hex
  such as `0x96` is malformed.
- A malformed value, `abc` or `12abc`, is that command's default, so a typo
  cannot set an unintended value. Where a command has no default to fall back
  to, it refuses the line with `87` instead: the `arm.*` settings, `ui.offset`,
  and the numbers of `hud.setvar`, `hud.call` and `hud.probe`.
  `dbg.draw`, `aim.shot`, `near.fx`, `markers.depth`, `stars.depth`, `body.depth`,
  `near.fov` and `ik.align` also refuse one, because a fallback there would switch
  something on.
- A setting given some of its values but not all, such as `body.torso 30`,
  is refused with `87`.
- A line that is not a command is answered `result=rejected` and changes
  nothing. That covers an unknown command under a known prefix, such as
  `carry.bogus`. Its `detail=` says why:
  - `not_a_channel_verb_and_not_allowlisted`: no such command, and not a console
    variable the allowlist passes;
  - `missing_arguments`: a command short of the words it needs, such as `console`
    on its own.
- `report`'s `channelProcessed` and `channelRejected` count every line the mod
  answered and every line it rejected.

## A command with no value reports

**Typing a setting's command on its own shows its current value and changes
nothing.** For example, `ui.distance` replies `ui.distance result=0 cm=200`.

This used to be untrue for 73 commands. Typed bare, they set a default
or switched something on. For example, `ui.curve` reset curvature to 35,
`aim.origin` switched the aim origin to the hand, and `ik.mode` switched the
arms off. Those commands now reply with the value where the mod can read it
back, or with `note=no_value_nothing_changed` where it cannot. Each command
makes this check itself (`Valued` in `CommandChannel.cpp`). `ik.mode` and
`move.mode` reply with the mode in effect, also after a refused change.

The exceptions are one-shot actions, which act when they are typed:

- `capture`, `capture.hud`, `capture.inventory`, `dual.capture`
- `ik.calibrate`, `aim.calibratebarrel`
- `vr.enable`, `vr.disable`, `vr.recenter`, `view.recenter`, `view.calibrate`, `xr.start`
- `arms.draw`, `ik.dump`, `ik.skel`, `ik.helpers`, `near.dump`
- `menu`. A bare `menu` posts action 0, Up.

**Commands that take several values reset the ones you leave out to their
built-in defaults, not to their current values.** This applies to
`carry.throw`, `carry.heavy`, `carry.corpse`, `grenade.throw`,
`grenade.physics`, `ik.longreach` and `use.filter`. For example,
`carry.throw 200` sets the throw gain to 200 and puts the three speed limits
back to 160, 1600 and 500. `wrist.place` and `wrist.gate` are different: they
keep the current value of anything you leave out.

## What is kept, and what wins

- **VR Options menu choices are saved.** The save file is
  `%LOCALAPPDATA%\PreyVR\vr-options.ini`, and it holds the settings you have
  changed in the menu (or that were already in the file). It is read the
  first time VR starts and applied **every time VR starts, F11 included**.
- **Commands are session-only.** A command never writes to the save file, not
  even when you later change some other row in the menu.
- **`vr-options.ini` is the one source of VR Options settings.** The
  launcher's `PreyVR.json` holds only launch settings (render size, reference
  space, stereo mode) and passes none of the menu's.
- **The order things are applied in**, last wins:
  1. Built-in defaults.
  2. VR start's own settings. For example, it switches on the floating HUD,
     physical carrying, hand-thrown grenades and the hands without a weapon.
  3. **Your saved VR Options choices.**
  4. Commands you type during the session.
- So a command on a menu-backed setting lasts until the next VR start. At that
  point your saved choice comes back. If you never chose a value in the menu,
  nothing is saved for that row, and its default applies.
- To return a row to its default, delete its line from `vr-options.ini` while
  the game is closed. Deleting the whole file resets every row.

## Settings the VR Options menu saves

Each of these rows is saved. The command beside it changes the same value for
this session. "Menu only" means no command reaches it.

| Tab | Row (`vr-options.ini` key) | Values | Default | Command |
|---|---|---|---|---|
| Comfort | Turning (`turn`) | smooth (0), 15, 30, 45, 60, 90 degrees | 45 | `move.snap <0\|15..90>` |
| Comfort | Smooth turn speed (`turn_speed`) | 10–200 % | 100 | `move.turnscale <10..200>` |
| Comfort | Movement direction (`head_relative`) | body relative (0), head relative (1) | 1 | `move.headrelative <0\|1>` |
| Comfort | Move stick deadzone (`move_deadzone`) | 0–60 % of the stick | 15 | `move.deadzone <0..60>` |
| Comfort | Turn stick deadzone (`turn_deadzone`) | 0–60 % of the stick | 15 | `move.turndeadzone <0..60>` |
| Comfort | Sideways speed (`strafe_speed`) | 10–200 % of forward speed | 100 | `move.scales <strafe> <backward>` (each 1–200) |
| Comfort | Backward speed (`backward_speed`) | 10–200 % of forward speed | 100 | `move.scales <strafe> <backward>` |
| Hands | Aim lasers (`aim_lasers`) | on/off | on | `dbg.lasers <0\|1>` |
| Hands | Two-hand aim (`two_hand`) | on/off | on | `aim.twohand <0\|1>` |
| Hands | Foregrip input (`grip_toggle`) | hold (0), toggle (1) | hold | menu only |
| Hands | Support hand (`support_snap`) | free (0), snapped (1) | snapped | menu only |
| Hands | Psychoscope gesture (`psychoscope`) | on/off | off | menu only |
| Hands | Hands without a weapon (`arms_free`) | on/off | on | `arms.free <0\|1>` |
| Hands | Fingers follow the controller (`finger_curl`) | on/off | on | menu only |
| Hands | Arm length (`arm_length`) | 40–70 cm, shoulder to wrist | 56 | `arm.length <cm>` |
| Hands | Carry (`carry_hold`) | press to toggle (0), hold to carry (1) | press to toggle | `carry.hold <0\|1>` |
| Hands | Grenade throw (`grenade_hand`) | game throw (0), hand throw (1) | hand | `grenade.hand <0\|1>` |
| Hands | Object throw strength (`carry_throw_strength`) | 50–300 % | 140 | `carry.throw <gain> ...` (first value) |
| Hands | Grenade throw strength (`grenade_throw_strength`) | 50–400 % | 130 | `grenade.throw <vrFactor> ...` (first value) |
| Hands | Hands stop at surfaces (`surface_stop`): the drawn hands and weapon stop against fixed scenery and heavy or immovable loose objects | on/off | on | `surface <0\|1>` |
| Hands | Heavy objects (`heavy_mass`): loose objects heavier than this stop the hands and weapon, and slide when pressed | 1–100 kg | 10 | `surface heavy <kg>` |
| Hands | Physical contact (`physical_contact`) | on/off | on | menu only (`contact` reports) |
| Hands | Push strength (`push_strength`) | 25–200 % of 400 N | 100 | menu only |
| Hands | Free-swing wrench (`free_swing`; replaces `physical_melee`, now retired) | on/off | on | menu only (`swing` reports) |
| Hands | Minimum swing speed (`swing_speed`), at the point the wrench hits; at or above the heavy swing speed, every strike is heavy | 100–700 cm/s (a file saved at 80 or 90 loads as 100) | 120 | menu only |
| Hands | Heavy swing speed (`heavy_swing_speed`): Prey's charged hit from here, its whole bonus 300 cm/s on | 300–1500 cm/s | 700 | menu only |
| Interface | Interface size (`ui_scale`) | 20–200 % | 100 | `ui.scale <20..200>` |
| Interface | Visible-area limit (`ui_margin`) | 30–100 % | 72 | `ui.margin <30..100>` |
| Interface | Panel curvature (`ui_curve`) | 0–60 degrees | 35 | `ui.curve <0..60>` |
| Interface | Controls guide (`ui_guide`) | on/off | off | `ui.guide <0\|1>` |
| Interface | Menu distance (`ui_distance`) | 100–400 cm | 200 | `ui.distance <100..400>` |
| Interface | Menu left / right (`ui_offset_right`) | −90–90 degrees, + is right | 0 | `ui.offset <right> <up>` |
| Interface | Menu up / down (`ui_offset_up`) | −45–45 degrees, + is up | 0 | `ui.offset <right> <up>` |
| Interface | Text panel distance (`ui_text`) | 100–200 cm | 140 | `ui.text <100..200>` |
| Interface | Screen distance (`screen_pullback`): how far the view stays back from keypads, terminals and other screens you use, never further back than you stood; 0 is Prey's own camera ([keypad camera](KEYPAD-CAMERA-SCOPE-2026-10-26.md)) | 0–100 cm | 30 | `examine.pullback <cm>` |
| Interface | Laser hand (`pointer_hand`) | left (0), right (1), buttons only (2) | right | `ui.pointer <0..2>` |
| Interface | Controller prompts (`controller_prompts`) | on/off | on | `ui.prompts <0\|1>` |
| Interface | HUD style (`hud_layer`) | in the world (0), floating panel (1) | panel | `hud.layer <0\|1>` |
| Interface | Reticle (`reticle_world`) | on the HUD (0), in the world (1) | world | `aim.world <0\|1>` |
| Interface | Reticle size (`reticle_size`) | 50–200% | 100 | `aim.reticlesize <percent>` |
| Interface | Reticle fallback distance (`reticle_fallback_mm`) | 500–200000 mm | 10000 | `aim.reticleconverge <mm>` |
| Interface | Marker size (`marker_size`) | 50–200% | 100 | `markers.size <percent>` |
| Equipment | Body holsters (`holsters`) | on/off | off | menu only |
| Equipment | Wrist status (`wrist`) | on/off | on | menu only |
| Equipment | Wrist display size (`wrist_size`) | 80–140 % | 100 | menu only |
| Equipment | Status on HUD too (`wrist_on_hud`) | on/off | off | `wrist.face <0\|1>`, inverted: `wrist.face 0` keeps it on the HUD |
| Equipment | Wrist tick (`wrist_tick`) | on/off | on | `wrist.haptic <0\|1>` |
| Equipment | Wrist display position (`wrist_along`) | −50–200 mm | 75 | `wrist.place <along> <hover> <width> <tilt>` |
| Equipment | Wrist display height (`wrist_hover`) | 0–150 mm | 42 | `wrist.place` (second value) |
| Equipment | Wrist display tilt (`wrist_tilt`) | 0–90 degrees | 40 | `wrist.place` (fourth value) |
| Feedback | Controller feedback (`haptics`) | on/off | on | `haptics <0\|1> [strength]` |
| Feedback | Feedback strength (`haptic_strength`) | 0–100 % | 70 | `haptics <0\|1> <strength>` |
| Abilities | Psychic targeting (`psi_target`) | original (0), head (1), left controller (2) | original | `psi.target <0..2>` |
| Abilities | Left-hip medkit slot (`medkit_slot`) | on/off | off | `medkit.slot <0\|1>` |
| Abilities | Body slot feedback (`belt_hints`) | on/off | on | menu only |
| Setup | Lean zone (`lean_zone`): how far your head may lead the body before it steps after you; the torso bends over it | off (0), 5–50 cm | 25 | menu only (`body.report`'s `leanZoneCm`) |
| Setup | Hips and legs back (`hips_back`): the drawn hips and feet behind where Prey stands them | 0–20 cm | 8 | menu only (`body.report`'s `hipsBackCm`) |
| Setup | Near clip distance (`near_clip`): how close to your eyes the world, your body included, is still drawn; Prey's own is 10 cm. Smaller costs depth precision far away | 1–10 cm | 3 | menu only (`body.depth`'s `nearClip`) |
| Setup | Anti-aliasing (`antialiasing`): temporal smooths distant edges that crawl as you move, from each eye's recent frames, at some cost in frame rate; SMAA smooths each frame alone | SMAA (0), temporal (1) | temporal | `taa.vr <0\|1>` |
| Setup | Debug overlay (`debug_overlay`) | on (every layer) / off; the aim lasers are apart | off | `dbg.draw <on\|off\|mask>` |
| Experimental | Physical carrying (`carry_hand`) | on/off | on | `carry.hand <0\|1>` |
| Experimental | Stick charges to surfaces (`grenade_deploy`) | on/off | off | `grenade.deploy <0\|1>` |
| Experimental | Long-weapon hold-back (`long_reach`) | on/off | on | `ik.longreach <0\|1> ...` (first value) |
| Experimental | Shots from the muzzle (`muzzle_aim`) | on/off | off | `aim.shot <0\|1>` |
| Experimental | Inventory layer (`inventory_layer`) | on/off | off | `ui.inventory <0\|1>` |
| Experimental | Stereo inventory (`inventory_stereo`) | on/off | off | `ui.stereo <0\|1>` |
| Experimental | Inventory depth (`inventory_depth`) | 0–100 % | 25 | `ui.depth <0..100>` |

The Setup tab's other two rows are actions. *Calibrate current posture* is
`view.calibrate`. *Reset VR view* is `vr.recenter`, which is the same as
`view.recenter` and F12. The Equipment tab's *Clear holster assignments* has
no command.

A few rows behave differently from their commands:

- Physical carrying switches on only while the left hand is the use hand
  (`use.hand 1`, which VR start sets). Switching it off always works.

## Player-facing commands not in the menu

| Command | Values | Default | What it does |
|---|---|---|---|
| `body.torso <deg> <relax>` | 0–180, 0–10000 ‰/s | 35, 600 | How far you look away before the torso turns, and how fast it settles. The shoulders and the holster and medkit slots sit on it. The same setting as `arm.torso`. |
| `wrist.place <along> <hover> <width> <tilt>` | mm, mm, 40–300 mm, degrees | 75, 42, 125, 40 | Where the wrist card sits. Width is multiplied by the menu's wrist size. |
| `wrist.gate <enterFacing> <exitFacing> <enterViewing> <exitViewing> [dwellMs]` | ‰ −1000..1000, 0–2000 ms | 500, 220, 820, 620, 80 | How squarely you must look at the wrist before the card shows. |
| `wrist.native <scale> [background]` | 100–600 %, 0/1 | 400, 0 | Scale of the game's own status on the card; 1 keeps the game's backdrop. |
| `wrist.occlusion <mask>` | 0–3 | 3 | Hide the card behind 1 scene geometry, 2 your right hand or weapon. |
| `pose.curl <percent>` | 0–300 | 100 | Finger curl of the relaxed free hand. |
| `dual.overlay <0\|1>` | | 1 | Fades, markers, fullscreen videos and other overlays in both eyes; 0 right eye only, for comparison. |
| `dual.vr <0\|1>` | | 1 | Both eyes from one frame, or alternate eyes. The launcher's `DualStereo` chooses at start. |
| `vr.options <0\|1>` | | | Opens or closes VR Options, over the running game or a game menu. |

## Every command, by area

"Default" is the value a player sees in VR: where VR start overrides the
built-in value it reads `→ VR n`. Ranges are the setter's own. Commands with no
value report, as described above.

### VR session

| Command | Values | Default | What it does |
|---|---|---|---|
| `vr.enable` | action | | Starts VR (also F11). The launcher sends it. |
| `vr.disable` | action | | Stops every VR feature and the XR session. |
| `vr.status` | report | | VR phase and any failed step. |
| `vr.recenter` | action | | Yaw recenter, keeping the height baseline (F12). |
| `vr.options` | 0/1 | | Opens/closes VR Options. |
| `view.recenter` | action | | Same as `vr.recenter`. |
| `view.calibrate` | action | | Recenters and takes the current posture as the height baseline. |
| `view.height` | report | | Reference against current head height. |
| `view.observe` / `view.apply` / `view.position` | 0/1 | 0 → VR 1 | The view hook: install, write head rotation, write head position (6DoF). |
| `observer` | 0/1 | VR 1 | The render frame observer. 0 stops the mod drawing. |
| `input.hotkeys` | 0/1 | launcher 1 | Keyboard thread: ~ console, eye-offset keys, swap eyes, panic. |
| `window.hold` | 0/1 | launcher 1 | Prey runs as though its window had focus: no 25 ms-a-frame slowdown, no pause and no muted sound when minimised or unfocused. `answered=` counts the slowdowns it prevented, `audioSwallowed=` the mutes. 0 restores the game's behaviour. Not kept. |
| `report` | report | | Large counter snapshot, also written to the log. |

### Menus and panels

| Command | Values | Default | What it does |
|---|---|---|---|
| `ui.scale` | 20–200 % | 100 | Size of game panels and the HUD. Reports margin and guide too. **Menu.** |
| `ui.margin` | 30–100 % | 72 | Fraction of the reported view a panel must stay inside; the real size cap. **Menu.** |
| `ui.curve` | 0–60 degrees | 35 | Curvature of curved mono menus. **Menu.** |
| `ui.guide` | 0/1 | 0 | Controls card under game menus. **Menu.** |
| `ui.distance` | 100–400 cm | 200 | Game menu distance, same apparent size. VR Options is placed apart (dragged by the laser). **Menu.** |
| `ui.offset <right> <up>` | −90..90, −45..45 degrees | 0 0 | Moves the game menus around you on a cylinder, upright. Both values or neither. **Menu.** |
| `ui.text` | 100–200 cm | 140 | Distance of notes, books, subtitles and tutorial cards. An open book or note moves to the new distance in the direction it opened. **Menu.** |
| `examine.pullback` | 0–100 cm | 30 | Screen distance: how far the camera stays back from a keypad, terminal or other screen you use, never further back than the eye was when you pressed use; 0 is Prey's own camera ([keypad camera](KEYPAD-CAMERA-SCOPE-2026-10-26.md)). Bare: report. Report: `active` (VR drives it), `hooks` and `gate` (`ok`, `refused` = bytes mismatch, `hook_failed`); counts of `fits` (Prey's fit for any examination), `begins` (world UIs examined), `moved`, `capped` (held short by where the eye was), `noEye` (no begin seen), `otherCaller`, `unread`, `unwritten`. `PREYVR_EXAMINE_CAMERA=0` at launch leaves it unhooked. **Menu.** |
| `ui.pointer` | 0 left, 1 right, 2 buttons only | 1 | Which hand's laser points at menus. **Menu.** |
| `ui.prompts` | 0/1 | VR 1 | Game menus keep the controller's prompts under the laser: Prey's `hud_forceControllerGlyphs`, set and read back through the console. 0 puts Prey's 0 back. `applied=` is the value read back. **Menu.** |
| `ui.panel` | 0 scene, 1 automatic, 2 force | 0 → VR 1 | Whether modal screens go to the floating panel. 2 is diagnostic. |
| `ui.inventory` | 0/1 | 0 | Inventory and PDA on a see-through layer of their own. **Menu (Experimental).** |
| `ui.stereo` | 0/1 | 0 | Depth for the inventory layer's parts. **Menu (Experimental).** |
| `ui.depth` | 0–100 % | 25 | How much depth `ui.stereo` gives. **Menu (Experimental).** |
| `menu.nav` | 0/1 | 0 → VR 1 | Controller to game-menu navigation. |
| `menu.gate` | 0/1 | VR 1 | Drops navigator taps unless a menu is open, so the stick does not switch weapons. |
| `menu <action>` | 0 up … 13 exit | | Posts one menu tap: 0 up, 1 down, 2 left, 3 right, 4 accept, 5 cancel, 6 start, 7/8 previous/next tab, 9 secondary, 10 tertiary, 11/12 previous/next page, 13 exit (Back). |

### HUD

| Command | Values | Default | What it does |
|---|---|---|---|
| `hud.layer` | 0/1 | 0 → VR 1 | HUD on a floating panel (1) or drawn in the world as Prey draws it (0). **Menu.** |
| `hud.fit <0\|1>` | required | engine fit | 1 fits the HUD canvas inside the frame; 0 restores the engine's. |
| `hud.var [path ...]` | up to 16 | | Reads HUD Flash variables; bare shows the last result. |
| `hud.setvar <path> <number>` | | | Writes a HUD Flash variable. |
| `hud.call <function> <x> [y]` | | | Calls a HUD Flash function. |
| `hud.probe [x y]` | viewport fractions | | Probes the HUD's screen-to-canvas mapping; bare shows the last probe. |

### Movement and turning

| Command | Values | Default | What it does |
|---|---|---|---|
| `move.snap` | 0 smooth, 15–90 degrees | 45 | Snap-turn step. **Menu.** |
| `move.turnscale` | 10–200 % | 100 | Smooth-turn speed. **Menu.** |
| `move.headrelative` | 0/1 | 1 | Stick moves where you look (1) or where the body faces (0). **Menu.** |
| `move.deadzone` | 0–60 hundredths | 15 | Left stick deadzone. **Menu.** |
| `move.turndeadzone` | 0–60 hundredths | 15 | Right stick deadzone. **Menu.** |
| `move.scales <strafe> <backward>` | 1–200 % each | 100 100 | Sideways and backward speed against forward. **Menu.** |
| `move.mode` | 0 off, 1 observe, 2 apply | 0 → VR 2 | The left-stick locomotion lane. |
| `move.turn` / `move.fire` / `move.act` | 0/1 | 0 → VR 1 | Right-stick turning; right trigger fires; grip and face-button bindings. |
| `move.all` | 0/1 | | Turns move, turn, fire and act on or off together. |
| `move.firehold` | 0/1 | 1 | Holds the fire key while the trigger is held (GLOO, Disruptor). |
| `move.bind <slot> <keyId>` | slot 0–14 | | Remaps a controller action's key: 0 interact, 1 inventory, 2 jump, 3 crouch, 4 PC wheel (unbound by default), 5 Prey's Y, 6 sprint, 7 psychoscope, 8–10 quick slots left/right/down, 11 torch, 12 psi, 13–14 the wheel's LB/RB (Frame bumpers, sent only while Prey's wheel is open). |
| `move.reread` | 0/1 | 1 | 1: when Prey re-reads the sticks from the gamepad (leaving the weapon wheel, a keypad or a menu, any filter change), it finds the stick you are holding. 0: it finds the idle pad's zero and the player stops until the stick moves, as before 2026-10-24, for A/B. Counts either way (docs/MOVE-STOP-2026-10-24.md). |
| `move.hold` | 0–250 ms | 100 | How long a gap in the controller samples keeps the last stick instead of releasing it for a frame. 0 releases at once, as before 2026-10-24. |
| `move.report` | report | | The stick lanes' counters, also on the status line as `move={...}`: `released` (a held stick let go, by reason), `bridged`/`turnBridged` (sample gaps held across), `menuFlickers` (a menu flag up for one frame, which no longer stops you), `menuTook`, `reasserted`/`reassertRefused` (sticks the engine lost and was given back), and `reread` (Prey's re-reads by event; `wouldStop` = re-reads that would have stopped you, `kept` = those the fix kept). |
| `input.post` | 0/1 | 0 → VR 1 | Lets the mod post input to the engine; every controller lane needs it. |
| `input.force` | 0/1 | 0 | Bypasses the engine's posting check. Diagnostic. |
| `input.key <keyId> [state] [valueMilli]` | | | Posts one raw input event. Diagnostic. |
| `input.binds [all]` | | | Logs Prey's live key bindings (map, action, key) to PreyVR.log as `preyvr_bindings`: the gamepad's keys, or every key with `all`. Also logged once per session after two seconds of play. Read-only. |

### Body

The drawn body is the player's (docs/BODY-FOLLOW.md), and nothing here has a
switch: the overhaul's test switches and research commands went once it was
worn OK (2026-10-25), as Body Follows You's commands had on 2026-10-24.
- **Turning:** with the stick, Prey turns the body, as always. After each snap
  turn, at once: a snap turns the view about the body, as smooth turning does,
  so the camera keeps its place on the model; it turns about your head only if
  the body cannot be turned (`snapBodyTurn=0`). As you turn round in the room,
  after each frame's render: no faster than 360 degrees a second, but never
  while Prey is turning the player itself (a smooth turn), nor for 150 ms
  after. A physical turn made during a smooth turn is caught up after it.
- **Room-scale:** after each frame's render, with the turn, the capsule moves
  under you: your eyes as they would be with your head level. **Lean, and the
  torso bends:** your head may lead the body by the lean zone before it follows
  (25 cm by default, Setup's Lean Zone; more when you are bent well down). **Step, and it catches up:** once your head has
  rested upright for half a second, not lowered as a lean would lower it, the
  capsule glides under you. Each move is first tested to fit by the body's own collision test,
  so the capsule stops at walls and furniture, and the body pin bends the torso
  the rest of the way. A walking body keeps its motion. The view never moves
  for it. When your head goes through something, the view fades to black and
  firing, using and psi wait until it comes back.
- **The body pin:** after Prey animates the body, the mod poses it again under
  your head: your neck under the headset's, the spine bent to it, the pelvis
  down and back as you crouch, and the legs bent to the feet Prey planted. The
  arms hang from its collarbones. Looking down, the drawn neck bends with your
  head and its throat stays behind your eyes. Only the drawn body: the view is
  never moved.

| Command | Values | Default | What it does |
|---|---|---|---|
| `body.report` | report | | The body, in three parts. **The torso and the turns:** the shared torso the shoulders and body slots sit on (`hooked`, `active`, `torsoDeadzoneDeg`, `torsoRelax`, `ticks`, `notPlayer`, `noHead`, `headDeg`, `torsoDeg`); `snapBodyTurn` (1 when the body can be turned), `turnFault`, `snapTurns` (turns after a snap), `lastSnapTurnMdeg`, `snapWaits` (frames a snap's turn waited for gameplay or a busy reference), `followTurns` (frames the body followed a physical turn), `followPaused` (frames it waited because Prey was turning the player) and `readbackMaxMdeg` (how far the engine's rotation differed from the one asked for). **The body pin:** `pinReady` (the rig has every joint the pin needs), `straighten` (the waist Prey's crouch kinks can be straightened: the rig is the one it was measured on), `hipsBackCm` (Hips and Legs Back: the hips and feet behind Prey's), `pinJoints` (joints the pin places), `pinFrames` and `pinRefused` (frames posed, and frames it could not pose), `folded` (frames where the legs' fold set how far the back leans); for the last frame `dropMm` (your neck below the game's), `leanDeg` (the back from upright), `twistDeg` (the chest turned onto your torso), `waistMm` (the kinked waist moved back), `throatPushMm` (the neck pushed back to keep the throat behind your eyes), `sinkMm` (the feet sunk, when you are lower than the deepest squat) and `shortMm` (how far the neck stayed from under yours), and the most of the last three since the last report (`throatPushMaxMm`, `sinkMaxMm`, `shortMaxMm`); `eyeGapMm` and `eyeGapMaxMm`, how far the game's eye under the view lies from the camera joint in the pose being posed, near zero when the two are one frame's (walking, a stale view would show as about one frame's step). **Room-scale:** `roomAvailable` (the engine calls it needs matched; `unchecked` before the first gameplay frame), `roomFault` (an engine call faulted: off until Prey restarts), `roomActive`, `roomServices`; frames `moved`, and `movedMm`, `lastMoveMm` and `residualMm` (how far your eyes stayed from over the body); `leanZoneCm` (the Lean Zone option); `headDropMm` (your head below its calibrated height), `allowanceMm` (how far it may then lead the body: the lean zone, or more bent down), `uprightMm` (the height learnt as standing upright, below the calibrated one) and `settleMoves` (frames the capsule glided under you after a step); why it did not move: `notGameplay`, `noFrame`, `roomNotPlayer`, `noLiving`, `blocked` (a wall or furniture stopped part of a move), `fitRefused` (none of it fitted), `startOverlap` (the body was already touching something), `busy` (physics was stepping: the move waited a frame), `referenceBusy`, `stepSettling` (climbing a step), `jumps` (tracking glitches refused); how it moved: `keptMotion`, `startKept` (a walk's first frames), `restMoves`, `raced`; and the head in a wall: `headOut`, `headOutEvents`, `headOutFrames`, `fadeFrames`. The status line carries the first part as `torso={...}`. |

### Arms and hands (IK)

| Command | Values | Default | What it does |
|---|---|---|---|
| `arm.torso <deg> <relax>` | 0–180, 0–10000 ‰/s | 35 600 | Torso yaw deadzone and settling, for the shoulders and the body slots. The same setting as `body.torso`. |
| `arm.mode` | 0 native arm, 1 your shoulder | 1 | Where the arm is solved from. |
| `arm.shoulder <fwd> <out> <up>` | mm | | The shoulder joint from the centre eye, with the head level and facing the torso, in the torso's frame. With the body pin it rides the drawn chest from there. |
| `arm.len <upper> <fore>` | 150–450 mm | 285 275 | Drawn arm segments. |
| `arm.length <cm>` | 40–70 | 56 | Both segments at once, shoulder to wrist, split as the defaults are. **Menu** (Arm length). |
| `arm.stretch <clavicle> <arm>` | 0–500 ‰, 1000–1300 ‰ | 150 1100 | How far the clavicle and the arm may stretch. |
| `arm.prior <flexSd> <devRest> <devSd> <swivelSd>` | degrees | 45 −8 12 25 | The elbow solver's priors. |
| `arm.rom <flex> <ext> <radial> <ulnar>` | 10–120 degrees | 75 65 20 35 | The wrist's range of motion. |
| `arm.hold <sd> <tauMs>` | 0–360, 0–1000 | 45 60 | Elbow smoothing over time. |
| `arm.knee <permille>` | 500–1000 | 920 | Exact reach up to this fraction of the stretched arm. |
| `arm.capsule [<part> <fwd> <up> <half> <radius>]` | part `chest`, `ribs`, `waist` or `hips`; mm: −300–300, −300–300, 0–300, 0–300 | chest 30 −40 42 118, ribs 15 −45 41 116, waist −15 −25 38 111, hips −15 −40 50 117 | One of the drawn torso's capsules, laid across the body on its spine joint (spine4 down to spine1): centred `fwd` and `up` from the joint, its axis `half` either way to the sides, `radius` round it (0: none). The elbow is kept out of them (`arm.avoid`), the arms are measured against them, and `dbg.draw 512` draws them ([ARM-TORSO-SCOPE-2026-10-29.md](ARM-TORSO-SCOPE-2026-10-29.md)). The defaults matched the body's mesh in the headset (PreyVR-103). Not kept. |
| `arm.limbs <upper> <elbow> <fore> <wrist>` | 10–150 mm | 50 45 40 30 | How thick the drawn arm is (radius) where it is measured against the torso. Not kept. |
| `arm.avoid <margin> <wall> <degPerS>` | 0–50 mm; 1–50 mm; 30–3600 | 10 10 360 | The elbow kept out of the drawn torso. The elbow chooser prefers elbows that keep the arm `margin` clear of it, its cost rising as ((margin − clearance) / wall)²; the drawn elbow then goes round the body to the chosen one, never through it, at most `degPerS` when going the long way round or sliding along the body, and waits at the body while the chosen elbow is in it. Not kept. |
| `arm.report` | report | | Arm settings and solver statistics. `reachCmR/L` is your wrist's distance from the shoulder now; `reachMaxCmR/L` the longest since the last `arm.*` command. Hold your arms out straight between two reports to read your arm length. The arms against the torso: `torsoPlaced` (the capsules are on the drawn spine), `capsuleMm`, `limbsMm`; per arm `fitR/L` (the arm's sample nearest the torso, the part, and its surface from the torso's in cm, negative inside) and `clearCmR/L` (upper arm, elbow, forearm, wrist); since the last `arm.*` command `fitFramesR/L`, `insidePctR/L` (frames with any of the arm inside) and `worstCmR/L` with `worstAtR/L`. Keeping the elbow out: `avoidMm` (margin, wall), `avoidDegPerS`, and since the last `arm.*` command `trackR/L`, the frames the drawn elbow went straight to the chosen one, round the body, waited at its edge, and was pushed out by it. |
| `arms.free` | 0/1 | 0 → VR 1 | Hands at the controllers with no weapon drawn. **Menu.** |
| `arms.draw` | action | | Asks the game to draw the last weapon. |
| `arms.report` / `arms.att` / `arms.joints [name]` | report | | Holster state; the arms' attachments; joint positions. |
| `arms.hide <index> <0\|1>` | | | Test: hides one attachment. |
| `ik.longreach [on comfortMm maxBackMm supportBackMm twistDeg]` | 0/1, 100–1500, 0–400, 0–300, 0–60 | 1 450 200 100 30 | Long weapons held back toward the body, wider foregrip region, torso twist. **Menu** (on/off, Experimental). |
| `ik.widesupport` | 0/1 | 1 | Foregrip grab region up to 45 cm beside, 80 cm ahead (0: 25/70 cm). |
| `ik.supportlatch` | 0/1 | 1 | One settled support-hand socket per weapon. |
| `ik.mode` | 0 off, 1 observe, 2 apply | 0 → VR 2 | The IK arms lane. |
| `ik.drive` / `ik.align` | 0/1 | 0 → VR 1 | Controllers drive the hands; weapon orientation from its authored aim. |
| `ik.hands` | 1 right, 2 left, 3 both | 1 → VR 3 | Which hands IK drives. |
| `ik.reach` | 50–150 % | 100 → VR 65 | Linear reach compression; only used with `pose.reach 0`. |
| `ik.joints` | 1–768 | 101 | Joint-count signature of the first-person rig. |
| `ik.calibrate` | action | | Controller-to-wrist rotation, next frame. VR runs it on each weapon. |
| `ik.test <x> <y> <z>` | ±2000 mm | 0 0 0 | Test offset on the wrist goal. |
| `ik.trace` / `ik.dump` / `ik.helpers` | | | Diagnostics, written to the log. |
| `ik.skel [label]` | letters, digits, `-`, `_` (24 at most) | `manual` | Writes every joint of the player's rig to the log on the next frame the IK sees it (`preyvr_anim_ik skel`): a header with the label, the rig's location (its model-to-world transform) and the joint the game's `#camera` rides; the character's attachments (`name@joint`); then per joint its name, the joint table's fields, its parent (derived from the pose), and its relative pose, its absolute pose before the native pass (`pre`) and after it and the mod's arms (`abs`), in model space. |
| `pose.mode` | 0/1 | 1 | Anatomical hand pose (1) or the original lane (0). |
| `pose.grip <along‰> <outMm> <thumbMm> <leanDeci>` | all four | 550 35 0 150 | Where the controller sits in the drawn hand. |
| `pose.reach <mode> <knee%>` | both | 1 85 | Exact reach to the knee then soft (1), or `ik.reach` (0). |
| `pose.roll` | 0/1 | 1 | Rolls a held weapon about its barrel to fit the hand. |
| `pose.stretch` | 90–125 % | 120 | Arm stretch limit against the rig's arm. |
| `pose.curl` | 0–300 % | 100 | Free-hand finger curl. |
| `pose.report` / `pose.marks` | report | | Hand-pose settings and segments. |

### Use, carrying and grenades

| Command | Values | Default | What it does |
|---|---|---|---|
| `use.hand` | 0/1 | 0 → VR 1 | 1: the left hand points and grab (Prey's X) uses, or reloads with nothing to use; the right grip reloads. 0: right grip uses the eye ray. |
| `use.hold` / `use.trace` | 0/1 | 1 / 0 | Use held every frame while down; log selection changes. |
| `use.back` | 0–400 mm | 100 | How far behind the controller the use ray starts. |
| `use.filter <on> [minCutoff] [beta] [backlash]` | centi-Hz, ×100, centi-degrees | 1 100 200 75 | Smoothing on the use pointer. |
| `use.find <entityId>` / `use.report` | | | Test harness; lane state. |
| `carry.hand` | 0/1 | 0 → VR 1 | Carried objects in the left hand, thrown with it. **Menu (Experimental).** |
| `carry.hold` | 0/1 | 0 | 0: press grab (X) to pick up and again to let go, as Prey does; an object in the hand leaves with the hand's motion. 1: hold it to keep holding. **Menu.** |
| `carry.throw <gain> <throwCm/s> <maxCm/s> <heavyMaxCm/s>` | 50–300, 20–1000, 100–5000, 50–3000 | 140 160 1600 500 | Throw strength, drop/throw threshold, speed caps. **Menu** (gain). |
| `carry.heavy <kg> <diagCm> <holdMs> <shiftCm> <tauMs>` | | 25 120 500 60 100 | What counts as heavy and how a heavy prop follows. |
| `carry.corpse <minCm> <maxFromNativeCm> <tauMs>` | | 75 110 120 | Where a dragged body is held. |
| `carry.grabpoint` | 0/1 | 1 | Hold a light prop where the ray hit it (0: its centre in the palm). |
| `carry.trace` / `carry.report` | | | Logging; lane state. |
| `grenade.hand` | 0/1 | 0 → VR 1 | Grenades thrown with the right hand's motion. **Menu.** |
| `grenade.throw <vr%> <parity> <drop> <throw> <maxOver%> <lever> <fixed%>` | | 130 1 80 200 115 1 150 | Throw strength and the landing model. **Menu** (strength). |
| `grenade.deploy` | 0/1 | 0 | Keeps the game's sticking of charges to the surface you look at. **Menu (Experimental).** |
| `grenade.early` | 0/1 | 1 | Leaves at the trigger's release (0: at the animation's throw). |
| `grenade.spawn` | 0–2 | 0 | Spawn point: drawn grenade, fist, native firing position. |
| `grenade.physics <gravity> <damping>` | cm/s², %/s | 1270 85 | Fallback flight model. |
| `grenade.give <type> [count] [equip]` | emp, recycler, nullwave, lure | emp 3 1 | Test: puts grenades in the inventory. |
| `grenade.trace` / `grenade.report` | | | Logging; lane state. |

### Wrist

| Command | Values | Default | What it does |
|---|---|---|---|
| `wrist.face` | 0/1 | 1 | 1: health, suit and psi leave the HUD while the wrist status is on. **Menu**, inverted (Status on HUD too). |
| `wrist.haptic` | 0/1 | 1 | Tick as the card unfolds. **Menu** (Wrist tick). |
| `wrist.place <along> <hover> <width> <tilt>` | | 75 42 125 40 | Card placement. **Menu** (along, hover, tilt). |
| `wrist.gate` / `wrist.native` / `wrist.occlusion` | see above | | When the card shows, the game status's scale, what hides it. |
| `wrist.anchor` | 0/1 | 1 | On the drawn forearm (0: the old grip-anchored card). |
| `wrist.force` | 0/1 | 0 | Shows the card from any pose, for captures. |
| `wrist.report` | report | | Gate, look test, counters. `gate=` adds up the conditions that hold: 1 the option, 2 views, 4 rendered, 8 no menu panel, 16 VR Options closed, 32 gameplay input, 64 no two-hand aim, 128 not recentering, 256 tracking. 511 is open (`wrist::Gate`, preyvr/WristHolo.h). |

### Aim, reticle and shots

| Command | Values | Default | What it does |
|---|---|---|---|
| `aim.twohand` | 0/1 | 1 | Two-hand support aim: a foregrip steers, a handgun's cupped grip only braces. The report's `grip=foregrip\|handgun` is the equipped weapon's kind. **Menu.** |
| `aim.world` | 0/1 | 1 | Prey's crosshair in the world at the aim's hit (1), or on the HUD panel (0). Reports its targets and frames shown. **Menu** (Reticle). |
| `aim.reticlesize` | 50–200 % | 100 | The world crosshair's size against the HUD's. **Menu.** |
| `aim.scene` | 0/1 | 0 | Runs the aim's scene query on its own (diagnostics); the world reticle runs it whatever this is. The old Scene Reticle setting is retired, and a saved `scene_reticle` is skipped. |
| `aim.reticleconverge` | 500–200000 mm | 10000 | The world crosshair's distance when the aim hits nothing within 200 m; on the HUD, the point along the ray it marks. **Menu** (Reticle fallback distance). |
| `aim.shot` | 0/1 | 0 | Shots leave the muzzle along the controller's aim. **Menu.** |
| `aim.enable` | 0/1 | 0 → VR 1 | Controller aim. 0 aims with the head. |
| `aim.reticle` / `aim.reticledispatch` | 0/1 | 0 → VR 1 | The crosshair follows the controller; the movie call half of that write. |
| `aim.reticlecanvas` | 0 raw, 1 model, 2 native | 1 | How the reticle maps onto the HUD canvas. |
| `aim.reticlestage` | 0/1 | 0 | The engine's stage scale flag, canvas mode 2 only. |
| `aim.origin` | 0 eye, 1 hand, 2 muzzle | 0 | Where the aim ray starts. Diagnostic. |
| `aim.calibratebarrel` | action | | Learns the muzzle offset from the last shot. |
| `aim.shotprobe` / `aim.beam` | | | Research probes. |

### Feedback, abilities, physical

| Command | Values | Default | What it does |
|---|---|---|---|
| `haptics [0\|1] [strength]` | 0–100 | 1 70 | Controller pulses and their strength. **Menu.** |
| `psi.target` | 0 original, 1 head, 2 left controller | 0 | Psi power aiming. **Menu.** |
| `medkit.slot` | 0/1 | 0 | Left-hip medkit. **Menu.** |
| `swing [log [0\|1]]` | report | | Free-swing wrench: frames and queries with the wrench drawn, objects per query and by class (`static`, `rigid`, `articulated`, `other` = not tested, `self` = the player's own skeleton, skipped), `touches` (`headTouches` = found by the head's own sweep), `passes` (one per object per pass), and each decision: `struck` (`heavy` = Prey's charged hit), `failed` (a strike was due but the hit could not be called), `tooSlow`, `locked` (already struck this pass), `drained` (Prey's stamina check said no), `blocked` (scenery between the head and the contact; `sightUnknown` = the ray was refused). The hooks: `hooks`, `attacksBlocked` (Prey's attack with the wrench out), `substituted` (hits handed to Prey's hit), `impulses` and `collisions` (turned to the swing's direction), `handoffs` (props and bodies left to the hit's impulse). Cost (`usAvg`, `usMax`), dropped lines and the first refusing gate. Also logged every 10 s when it changes. Event lines are off by default (`swing log 1` turns them on, `log=` in the report): per pass a `begin` line (the object's class, vtable, foreign-data kind, entity id, class and name, a body's `host`, the surface's id, name, `breakable` and flags, the contact's kind, speeds and decision), a `strike` line for each strike (`result=struck`, `tier`, `scale`, `returned` = what Prey's hit returned, `fatigue` = the fatigue it charged per unit of scale, `stamina` before->after, `sight`), each `failed` (`why`), and the first `blocked` or `drained` of a pass, and a `swing` line per swing (`peakHeadCms`, `peakCentreCms`). `log 0` turns them off again. |
| `wrench.draw [0\|1]` | 0/1 | 1 | The wrench comes out and goes away without Prey's draw and stow animations (the draw spun the hand) ([wrench draw](WRENCH-DRAW-ANIMATION-2026-10-20.md)). 0 plays it again. Report: `hooks` and `gate` (`ok`, `refused` = bytes mismatch, `hook_failed`), `selects` and `pickups` (draws and first pickups skipped), `finished` (Prey's select-finished called a frame later), `gone` (the wrench was gone by then), `waiting`, `full` (a fifth waiting draw played), `elsewhere` (a draw off the main thread played), `sounds` (draw sounds played), `stows` (stowed at once), `stowSounds`, `stowsLowered` (a lowered wrench, left to Prey's animated stow), `fault`. Each skip, finish and stow is logged (`preyvr_wrench_draw skip` / `finish` / `stow`). |
| `wrench.sound [play 0\|1]` | 0/1 | 1 | The frame after each skipped draw, its sound plays on the player (`Play_Player_Arm_Wrench` and `Play_switch_weapon`, as the draw's PlaySound clips played them), and at each instant stow the stow's (`Play_Player_Unequip`); `played`, `playFailed`, `lastPlay` (`played`, `no_entity`, `no_proxy`, `unknown_trigger`, `fault`), and a `play result=` line each time. Report: `gate`, `fault`, `audioSystem`. The `capture` that found these triggers ([wrench draw](WRENCH-DRAW-ANIMATION-2026-10-20.md#the-equip-sound-investigated-2026-10-20-static)) was removed on 2026-10-23. |
| `surface [0\|1] [log [0\|1]] [weapon 0\|1] [heavy <kg>]` | report | on, log 0, weapon 1, heavy 10 | Surface stop (docs/SURFACE-STOP-SCOPE-2026-10-21.md): `0`/`1` is the menu's Hands Stop at Surfaces, `heavy` its Heavy Objects mass (1–100 kg). The report: `heavyKg`, `loose` (1 = the rays also see loose objects; 0 = their contracts failed, scenery alone), `frames`, `held` (frames with a hand held back), `holds` (holds started), `solves` by path (`fresh` = placed from the camera, `moved` = each probe's path from where it was drawn, `unstuck` = let go after 30 frames held while the real hand was in the open, `refused` = a scenery ray could not run), `fallback` (stopped along the camera's ray), `limited` (catching up), `unconverged`, `backFaces` (hits ignored: the ray was leaving a surface), `resets`, `weaponFrames` (the right hand probed with its weapon's box), what held a hand (`static`, `other` scenery; `heavy`, `immovable` props), `presses` (frames a heavy prop was handed to the contact lane to push), what the rays went on past (`passedLight`, `passedBodies`, `passedLive`, `unread` = a mass that could not be read), `offsetMaxCm`, rays per frame (`raysAvg`, `raysMax`), cost (`usAvg`, `usMax`) and the gate. Also logged every 10 s when it changes. `log 1` (or `log` alone) turns on `hold` lines (hand, path, offset, what was met: collider, `met=scenery type=` or `met=heavy|immovable mass=`, surface id, point, normal) and `free` lines (frames held, the most it was held back); off by default. `weapon 0` stops the hands alone, without the weapon's box (A/B). |
| `contact [log [0\|1]]` | report | | Physical contact: queries, objects per query, contacts by kind, begins/ends, pushes (`pushes`, `pushBegins`, `limited`, `sliding`, `pushFailed`, `dvMaxCms`, `push` = the push contracts passed; corpses: `bodies` = the articulated contracts passed, `partPushes`, `hosted` = refused, a live character's skeleton; `immovable` = refused, the game gave it no mass; `standing` = skipped, the player stands on it; `pressed`, `pressPushes` = presses from the surface stop, a heavy prop holding a hand back and pushed by how far the real hand is past it; `ground` = the player's physics class, `living` once read). With `log 1`, `stand` and `stand_end` lines mark what the player starts and stops standing on (`via=feet` = the feet's samples found its top, `via=engine` = the engine's ground collider), refusals, cost per frame (`usAvg`, `usMax`) and the first refusing gate. Also logged every 10 s when it changes. `log 1` (or `log` alone) turns on the per-event `begin`, `end` and `push` lines, `log 0` off again (`log=`); off by default. |

### Stereo and rendering

| Command | Values | Default | What it does |
|---|---|---|---|
| `dual.vr` | 0/1 | VR 1 (launcher `DualStereo`) | Both eyes from one game frame, or alternate eyes. |
| `dual.overlay` | 0/1 | VR 1 | After-world overlays in both eyes, fullscreen videos (the neuromod install) among them. |
| `dual.mode <0..3> [budget]` | | VR 3 | Raw submission mode. Use `dual.vr`, which keeps the ordering safe. |
| `dual.ipd` | 40–90 mm | 64 | Eye separation for flat mode 2 only. |
| `dual.observe` / `dual.capture` / `dual.report` | | | Diagnostics. |
| `near.enable` | 0/1 | 0 → VR 1 | Per-eye offset for the weapon and arms pass. |
| `near.halfipd` | 20–45 mm | VR runtime IPD / 2 | Half the eye separation for that pass. |
| `near.follow` / `near.fov` | 0/1; 0 or 11–1789 decidegrees | 1; follows | The weapon pass's field of view. An explicit `near.fov` stops following; a refused one leaves following as it was. |
| `near.fx` / `near.fxbridge` | 0/1 | 1 | Weapon-space stereo for near particles; the Q-Beam bridge. |
| `near.fxhide` / `near.zero` / `near.lineage` / `near.dump` | | 0 | Test switches. |
| `markers.depth` | 0/1 | 1 | World markers (objectives, repair, danger, enemy awareness, points of interest) at the depth they mark. Bare: report. |
| `markers.size` | 50–200 % | 100 | Marker Size for this session; VR Options saves it. |
| `stars.depth` | 0/1 | 1 | The stars at the sky's depth in both eyes; 0 draws them 1 m from the head as Prey does, for A/B ([starfield depth](STARFIELD-DEPTH-2026-10-25.md)). Bare: report. Report: `hooks` and `gate` (`ok`, `refused` = bytes mismatch, `hook_failed`), `draws` (ArkStars draws, one per eye), `far` (drawn 10 km out), `native` (drawn as Prey's, with `stars.depth 0`), `unread` (the element or its star block could not be read; drawn as Prey's), `distanceM`. `PREYVR_STAR_DEPTH=0` at launch leaves it unhooked. |
| `body.depth` | 0/1/2 | 1 | The player's body (arms, body, legs and the weapon on them) at the world's depth, so what is in front of it hides it ([how the body is drawn](BODY-FOLLOW.md#how-the-body-is-drawn)). 1: the near pass at the world's depth -- the body stays in Prey's near pass, so the weapon's effects and its display (the stun gun's charge sparks and meter) stay with it, and only that pass's depth changes: its depth slice becomes the full range and its projection's depth the world's. 2: the body drawn in the world (PreyVR-91; the weapon's near effects then show through it and its display stops updating). 0: Prey's near pass, over everything, for A/B. Only the main view: shadows and other passes are Prey's. Bare: report. Report: `mode` (`near`, `world`, `native`), `hooks` and `gate` (as `stars.depth`), `character` (1 when the player's character is known), `nearSlots` (near-pass slot draws seen), `world` (the player's drawn in the world, mode 2), `kept` (the player's in another pass, left near; `keptFlags` the last such pass's rendering flags), `others` (other characters' near draws), `native` (the player's left in the near pass, modes 0 and 1), `characterNear` and `paramsNear` (draws where the character or the render parameters also asked for the near pass), `viewports` (near render passes given the full depth range, mode 1), `nearest` (legacy switches into the near pass given the eye camera's depth range) and `nearestOther` (switches for a camera that is no eye's, left with the slice), `unread`. Then `nearDepth={...}`: `on`, `active` (on, and VR drawing), `remapped` (per-view packs given the world's depth), `fromView` and `fromEye` (the world's depth read from the pack itself, or kept for the eye for the legacy near camera), `noWorld` (packs before any world depth was seen for the eye), `refused` (matrices not perspective-shaped), `worldNearMm` and `worldFarM` (the world depth's two planes). `PREYVR_BODY_DEPTH=0` at launch leaves it unhooked. Then `nearClip={...}`, the view's near plane, which is where the body is cut (Setup's Near Clip Distance): `cm`, `written` (views given it), `kept` (views where Prey set its own), `cleared` (times the view's default was given back after VR). |
| `flashlight.report` | report | | The flashlight and the psychoscope's light on the headset's eye, like a head lamp, at the mount Prey gives them on its own eye ([flashlight](FLASHLIGHT-HEAD-SCOPE-2026-10-29.md)). Report: `active` (VR drives it), `hooks` and `gate` (`ok`, `refused` = bytes mismatch, `hook_failed`); `placed` (lights put on the headset's eye as Prey placed them), `deferred` (Prey placed one before the frame's view was known) and `late` (those put on the view before the render), `native` (left where Prey put them: no view that frame), `unread` (the light, its eye or its entity could not be read), `offThread` (Prey placed one off the main thread; left as Prey's), `unplaced` (the placement faulted); per light, `flashlight` and `psychoscope` (times placed) and `flashlightMountMm` / `psychoscopeMountMm` (the light from the eye, x right, y forward, z up, or `none` before it is first seen). `PREYVR_HEAD_LAMP=0` at launch leaves it unhooked. |
| `xr.mirrorfov` | 50–400 % | 100 | Field declared for flat-mirror frames (no eye pair). |
| `xr.native` / `xr.srgb` / `xr.submit` | 0/1 | 0 → VR 1 | Native projection; sRGB swapchain (before start only); per-eye submission. |
| `xr.stereo [ipdMm] [halfFov]` | 45–85 (0 off), 20–70 | | Fixed-IPD camera stereo, over the runtime's IPD. |
| `xr.frustum` | 0/1 | 0 | Renders the runtime's frustum. Prototype; leave off. |
| `xr.runtime <manifest>` / `xr.start` | | | Runtime choice before start; start the session. |
| `xr.timing [0\|1] [hz]` / `xr.resolution` / `xr.coverage` | | | Frame timing, resolution chain, frustum coverage. |
| `xr.hold [0\|1] [ms]` | 0/1, 30..2000 | 1, 100 | Frames from the mod while Prey renders none (a load), so the laser follows the hand and Prey's loading screen shows on the menu panel (docs/LOADING-SCREEN-2026-10-20.md). 0 turns them off along with the D3D11 multithread protection they need, for a performance comparison with `xr.timing`; loads then show the frame from before them. `ms` is how late Prey's frame must be first. The report's `loading*` fields count the loading loop's presents, the copies taken there, uploads to the panel, frames that showed it and loads ended. Not kept. |

### Diagnostics and legacy lanes

| Command | What it is |
|---|---|
| `dbg.draw [on\|all\|off\|mask]` | In-world debug drawing. Mask bits: 1 rays, 2 raw, 4 body, 8 wrist, 16 foregrip, 32 labels, 64 status, 128 hits, 256 physical sweep (weapon box, swept paths, last contact), 512 the arms against the drawn torso (its capsules, and rings on each arm green clear, amber within 1 cm, red inside; logged once a second as `arm_fit`). **Menu** (on = every layer). Drawn beside the aim lasers: `off` leaves them. |
| `dbg.lasers [0\|1]` | The aim lasers (rays + hits, 129) for this session, apart from `dbg.draw`. **Menu** (Hands > Aim lasers; on at VR start). |
| `dbg.report`, `dbg.marks` | Overlay counters and primitive positions. |
| `frame.probe [0\|1]` | Read-only flight recorder for the render freeze ([ENGINE-INSTABILITY.md](ENGINE-INSTABILITY.md#render-freeze)). It keeps the main thread's, render thread's and hold thread's frame steps (the frame start, Prey's render, the scene submissions, `xrWaitFrame`, the eye image, `xrEndFrame`, Prey's present) in a ring. When a gameplay stall makes the hold layer start holding frames, it writes 8 frames before to 12 after (at least until the stall ends) as `preyvr_frame_probe` lines, with a `summary` line: each thread's longest gap, between which two steps, and the longest single call. Loading screens open nothing; 16 windows a session. On from startup; `PREYVR_FRAME_PROBE=0` before launch leaves it out. |
| `taa.vr [0\|1]` | Anti-aliasing for this session (VR Options > Setup > Anti-aliasing saves it): per-eye temporal AA, phase 1 of [TEMPORAL-AA-SCOPE](TEMPORAL-AA-SCOPE-2026-10-30.md). Each eye gets its own TSAA history, previous camera, previous view-projection and post-processing counter (the second eye two counts ahead, so each walks the whole jitter pattern). `1` (the default) is TSAA, `r_AntialiasingMode 4`; `0` is SMAA 1X, mode 1. VR start applies the mode; a change while VR runs is queued to the console, again while it is busy (`preyvr_vr antialiasing=...`). Report `taaLane={...}`, also on the status line: `enabled`, `active` (applied last frame: a both-eyes frame in a temporal mode, 2-5), `gate`, `frames`, `starts` and `lastStart` (both eyes afresh, without history for one frame: `enable`, `gap` after a frame not drawn both-eyes, `incomplete`, `textures`, `refused`), `copiesIn`/`copiesOut` (each eye's history into the slot its resolve reads, and its output back out), `freshStarts`, `resolveMissed` (an eye whose resolve did not run with its counter), `counterOff`, `idleMode` (frames on but not in a temporal mode), `refused` (gate, stage, textures, device), `counters` and `history` (the size and DXGI format of the copies). The `r_AntialiasingTAA*`/`r_AntialiasingTSAA*` tuning cvars are allowlisted. |
| `taa.report` | Read-only temporal-AA probe, phase 0 of [TEMPORAL-AA-SCOPE](TEMPORAL-AA-SCOPE-2026-10-30.md); also on the minute status line as `taa={...}`. `taa.report` also gives the `taaLane={...}` report. Around each top-level scene submission it reads Prey's post-processing counter and the state a per-eye temporal history would separate; it writes nothing. `gate` (`ok`, `refused` = bytes mismatch, `untried` before PreyDll), `mode`/`effective` (`r_AntialiasingMode` and the copy the stages read), `counter`, `frames` and `frameDelta` (the counter's move from one frame's first submission to the next: 2 means post-processing runs once per eye). Per submission kind (`single`, `first`, `second`): `n`, `delta` (the counter's move across it), `prevViewProjChanged` (the renderer's previous view-projection changed), `stampIsCounter` (the AA stage's last resolve ran with this submission's counter; only in a temporal mode), `view` (the pipeline's render view afterwards: this one, none, another), `jittered` and the last `jitter`. Then `stage`, `lastResolve`, the two history `CTexture`s and `historyChanges`, and `tex0`/`tex1`: each one's device texture, the module its candidate pointers' vtables are in, and through the first that is d3d11.dll's, the history's size, DXGI `format`, mips, array, samples and bind flags (`hop=none` when none is). |
| `cull.prepare [0\|1]` | The occlusion-prepare guard: the fix for the cull lag behind the job-worker crash at `PreyDll+0x27EC40` and the world-flash glitch, closed pending recurrence ([ENGINE-INSTABILITY.md](ENGINE-INSTABILITY.md#cull-camera-crash-and-the-world-flashing)). 1 makes the engine's own occlusion prepare when a frame is about to draw the world without one; 0 turns it off. No argument: its counts (`kicks`, `kickFailed`) and the handshake state. On from startup, VR or not; `PREYVR_CULL_PREPARE=0` before launch starts it off. Only a kick that failed is logged (`preyvr_cull_prepare`). The cull and resource-set watches (`cull.report`, `cull.watch`, `rset.report`, `rset.watch`) were removed on 2026-10-27; `cull.probe` became `frame.probe`. |
| `capture [tag]`, `capture.hud [tag]`, `capture.inventory [tag]` | One frame of what the headset receives, to a file. Stalls the GPU once. |
| `console <command ...>`, or an allowlisted name alone | An engine console command through the fail-closed allowlist. |
| `mem.peek`, `mem.scan`, `mem.deep`, `mem.watch`, `mem.hits`, `mem.stack`, `mem.str` | Memory inspection and hardware breakpoints. Research only. |
| `weapon.observe [0\|1]` | Passive capture of the equipped weapon's attachment and firing position; `ik.mode` turns it on. The legacy `hand.*` lane and the `weapon.offset/apply/rotate/calibrate` mount writer were removed on 2026-10-23, superseded by `ik.*`. |
