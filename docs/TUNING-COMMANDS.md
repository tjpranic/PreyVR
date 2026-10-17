# Tuning command reference

Every command **Tune Prey VR.cmd** accepts, what it changes, its values and
default, and whether anything keeps it past this session. Updated 2026-10-13.

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

## A command with no value reports

**Typing a setting's command on its own shows its current value and changes
nothing.** For example, `ui.distance` replies `ui.distance result=0 cm=200`.

This used to be untrue for 73 commands. Typed bare, they set a default
or switched something on. For example, `ui.curve` reset curvature to 35,
`aim.origin` switched the aim origin to the hand, and `ik.mode` switched the
arms off. Those commands now reply with the value where the mod can read it
back, or with `note=no_value_nothing_changed` where it cannot. Each command
makes this check itself (`Valued` in `CommandChannel.cpp`). `ik.mode`,
`move.mode` and `hand.mode` reply with the mode in effect, also after a refused
change.

The exceptions are one-shot actions, which act when they are typed:

- `capture`, `capture.hud`, `capture.inventory`, `dual.capture`
- `ik.calibrate`, `aim.calibratebarrel`, `hand.calibrate`, `weapon.calibrate`
- `vr.enable`, `vr.disable`, `vr.recenter`, `view.recenter`, `view.calibrate`, `xr.start`
- `arms.draw`, `ik.dump`, `ik.skel`, `ik.helpers`, `near.dump`
- `menu`. A bare `menu` posts action 0, Up.

`body.turn` and `body.nudge` refuse to run without their values.

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
| Comfort | Body follows you (`body_follow`) | off (0), turn only (1), turn and move (2) | 2 | `body.follow <0\|1>`, `body.roomscale <0\|1>` |
| Comfort | Body turn deadzone (`body_turn_deadzone`) | 0–180 degrees | 35 | `arm.torso <degrees> <relax ‰/s>` |
| Comfort | Body step distance (`body_step`) | 10–300 mm | 30 | `body.zone <startMm> <stopMm> [followMs]` |
| Comfort | Body glide (`body_glide`) | 0–1000 ms | 50 | `body.zone <startMm> <stopMm> <followMs>` |
| Hands | Aim lasers (`aim_lasers`) | on/off | on | `dbg.lasers <0\|1>` |
| Hands | Two-hand aim (`two_hand`) | on/off | on | `aim.twohand <0\|1>` |
| Hands | Foregrip input (`grip_toggle`) | hold (0), toggle (1) | hold | menu only |
| Hands | Support hand (`support_snap`) | free (0), snapped (1) | snapped | menu only |
| Hands | Psychoscope gesture (`psychoscope`) | on/off | off | menu only |
| Hands | Hands without a weapon (`arms_free`) | on/off | on | `arms.free <0\|1>` |
| Hands | Fingers follow the controller (`finger_curl`) | on/off | on | menu only |
| Hands | Carry (`carry_hold`) | press to toggle (0), hold to carry (1) | hold | `carry.hold <0\|1>` |
| Hands | Grenade throw (`grenade_hand`) | game throw (0), hand throw (1) | hand | `grenade.hand <0\|1>` |
| Hands | Object throw strength (`carry_throw_strength`) | 50–300 % | 140 | `carry.throw <gain> ...` (first value) |
| Hands | Grenade throw strength (`grenade_throw_strength`) | 50–400 % | 130 | `grenade.throw <vrFactor> ...` (first value) |
| Interface | Interface size (`ui_scale`) | 20–200 % | 100 | `ui.scale <20..200>` |
| Interface | Visible-area limit (`ui_margin`) | 30–100 % | 72 | `ui.margin <30..100>` |
| Interface | Panel curvature (`ui_curve`) | 0–60 degrees | 35 | `ui.curve <0..60>` |
| Interface | Controls guide (`ui_guide`) | on/off | off | `ui.guide <0\|1>` |
| Interface | Menu distance (`ui_distance`) | 100–400 cm | 200 | `ui.distance <100..400>` |
| Interface | Menu left / right (`ui_offset_right`) | −90–90 degrees, + is right | 0 | `ui.offset <right> <up>` |
| Interface | Menu up / down (`ui_offset_up`) | −45–45 degrees, + is up | 0 | `ui.offset <right> <up>` |
| Interface | Text panel distance (`ui_text`) | 100–200 cm | 140 | `ui.text <100..200>` |
| Interface | Laser hand (`pointer_hand`) | left (0), right (1), buttons only (2) | right | `ui.pointer <0..2>` |
| Interface | HUD style (`hud_layer`) | in the world (0), floating panel (1) | panel | `hud.layer <0\|1>` |
| Interface | Reticle (`reticle_world`) | on the HUD (0), in the world (1) | world | `aim.world <0\|1>` |
| Interface | Reticle size (`reticle_size`) | 50–200% | 100 | `aim.reticlesize <percent>` |
| Interface | Reticle fallback distance (`reticle_fallback_mm`) | 500–200000 mm | 10000 | `aim.reticleconverge <mm>` |
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
| Setup | Debug overlay (`debug_overlay`) | on (every layer) / off; the aim lasers are apart | off | `dbg.draw <on\|off\|mask>` |
| Experimental | Physical carrying (`carry_hand`) | on/off | on | `carry.hand <0\|1>` |
| Experimental | Stick charges to surfaces (`grenade_deploy`) | on/off | off | `grenade.deploy <0\|1>` |
| Experimental | Long-weapon hold-back (`long_reach`) | on/off | on | `ik.longreach <0\|1> ...` (first value) |
| Experimental | Shots from the muzzle (`muzzle_aim`) | on/off | off | `aim.shot <0\|1>` |
| Experimental | Physical wrench (`physical_melee`) | on/off | off | menu only (`physical` reports) |
| Experimental | Minimum swing speed (`swing_speed`) | 80–250 cm/s | 120 | menu only |
| Experimental | Object nudges (`physical_contacts`) | on/off | off | menu only |
| Experimental | Object nudge strength (`contact_strength`) | 0–100 % | 50 | menu only |
| Experimental | Inventory layer (`inventory_layer`) | on/off | off | `ui.inventory <0\|1>` |
| Experimental | Stereo inventory (`inventory_stereo`) | on/off | off | `ui.stereo <0\|1>` |
| Experimental | Inventory depth (`inventory_depth`) | 0–100 % | 25 | `ui.depth <0..100>` |

The Setup tab's other two rows are actions. *Calibrate current posture* is
`view.calibrate`. *Reset VR view* is `vr.recenter`, which is the same as
`view.recenter` and F12. The Equipment tab's *Clear holster assignments* has
no command.

A few rows behave differently from their commands:

- Body step distance keeps the zone's stop distance below the new start, which
  `body.zone` requires.
- Body turn deadzone sets the arms' and the body's copy together, as
  `arm.torso` does. `body.torso` sets only the body's copy.
- Physical carrying switches on only while the left hand is the use hand
  (`use.hand 1`, which VR start sets). Switching it off always works.

## Player-facing commands not in the menu

| Command | Values | Default | What it does |
|---|---|---|---|
| `body.torso <deg> <relax>` | 0–180, 0–10000 ‰/s | 35, 600 | The body's copy of the turn deadzone and how fast it settles. Use `arm.torso` to keep the arms in step. |
| `body.zone <start> <stop> [followMs]` | start 5–500 mm, stop below start, follow ≤1000 ms | 30, 5, 50 | Room-scale: how far your head moves before the body follows, how close it brings it, how fast. The menu sets start and follow. |
| `body.radius <mm>` | 100–600 | 350 | Capsule radius the room-scale wall sweep assumes. |
| `body.level <0\|1>` | | 1 | Keeps the body's rotation level (no pitch or roll). |
| `wrist.place <along> <hover> <width> <tilt>` | mm, mm, 40–300 mm, degrees | 75, 42, 125, 40 | Where the wrist card sits. Width is multiplied by the menu's wrist size. |
| `wrist.gate <enterFacing> <exitFacing> <enterViewing> <exitViewing> [dwellMs]` | ‰ −1000..1000, 0–2000 ms | 500, 220, 820, 620, 80 | How squarely you must look at the wrist before the card shows. |
| `wrist.native <scale> [background]` | 100–600 %, 0/1 | 400, 0 | Scale of the game's own status on the card; 1 keeps the game's backdrop. |
| `wrist.occlusion <mask>` | 0–3 | 3 | Hide the card behind 1 scene geometry, 2 your right hand or weapon. |
| `pose.curl <percent>` | 0–300 | 100 | Finger curl of the relaxed free hand. |
| `dual.overlay <0\|1>` | | 1 | Fades, markers and other overlays in both eyes; 0 right eye only, for comparison. |
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
| `ui.pointer` | 0 left, 1 right, 2 buttons only | 1 | Which hand's laser points at menus. **Menu.** |
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
| `move.bind <slot> <keyId>` | slot 0–12 | | Remaps a controller action's key: 0 interact, 1 inventory, 2 jump, 3 crouch, 4 wheel (Y held), 5 swap (Y tapped), 6 sprint, 7 psychoscope, 8–10 quick slots left/right/down, 11 torch, 12 psi. |
| `input.post` | 0/1 | 0 → VR 1 | Lets the mod post input to the engine; every controller lane needs it. |
| `input.force` | 0/1 | 0 | Bypasses the engine's posting check. Diagnostic. |
| `input.key <keyId> [state] [valueMilli]` | | | Posts one raw input event. Diagnostic. |
| `input.binds [all]` | | | Logs Prey's live key bindings (map, action, key) to PreyVR.log as `preyvr_bindings`: the gamepad's keys, or every key with `all`. Also logged once per session after two seconds of play. Read-only. |

### Body

| Command | Values | Default | What it does |
|---|---|---|---|
| `body.follow` | 0/1 | 1 | The body turns to face where you look. **Menu** (Body follows you). |
| `body.roomscale` | 0/1 | 1 | The body walks with you across the room. **Menu** (turn and move). |
| `body.zone <start> <stop> [followMs]` | see above | 30 5 50 | Room-scale start, stop and glide. **Menu** (start and glide). |
| `body.torso <deg> <relax>` | | 35 600 | The body's turn deadzone only. |
| `body.radius` | 100–600 mm | 350 | Wall-sweep capsule radius. |
| `body.level` | 0/1 | 1 | Level body rotation. |
| `body.turn <deg> [delayS]` | ±1..180, 0–60 s | | Test: one body turn; the view must not turn. |
| `body.nudge <rightCm> <fwdCm> [delayS]` | ±100 cm | | Test: one capsule move; the view must not move. |
| `body.report` | report | | Body-follow and room-scale counters. |

### Arms and hands (IK)

| Command | Values | Default | What it does |
|---|---|---|---|
| `arm.torso <deg> <relax>` | 0–180, 0–10000 ‰/s | 35 600 | Torso yaw deadzone and settling, for the arms and the body together. **Menu** (Body turn deadzone). |
| `arm.mode` | 0 native arm, 1 your shoulder | 1 | Where the arm is solved from. |
| `arm.shoulder <fwd> <out> <up>` | mm | | The shoulder joint from the centre eye, in the torso's frame. |
| `arm.len <upper> <fore>` | 150–450 mm | 285 275 | Drawn arm segments. |
| `arm.stretch <clavicle> <arm>` | 0–500 ‰, 1000–1300 ‰ | 150 1100 | How far the clavicle and the arm may stretch. |
| `arm.prior <flexSd> <devRest> <devSd> <swivelSd>` | degrees | 45 −8 12 25 | The elbow solver's priors. |
| `arm.rom <flex> <ext> <radial> <ulnar>` | 10–120 degrees | 75 65 20 35 | The wrist's range of motion. |
| `arm.hold <sd> <tauMs>` | 0–360, 0–1000 | 45 60 | Elbow smoothing over time. |
| `arm.knee <permille>` | 500–1000 | 920 | Exact reach up to this fraction of the stretched arm. |
| `arm.report` | report | | Arm settings and solver statistics. |
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
| `ik.anchor` | 0/1 | 1 | Hands anchored at the view camera (0: the old reticle anchor). |
| `ik.joints` | 1–768 | 101 | Joint-count signature of the first-person rig. |
| `ik.calibrate` | action | | Controller-to-wrist rotation, next frame. VR runs it on each weapon. |
| `ik.test <x> <y> <z>` | ±2000 mm | 0 0 0 | Test offset on the wrist goal. |
| `ik.trace` / `ik.dump` / `ik.skel` / `ik.helpers` | | | Diagnostics, written to the log. |
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
| `use.hand` | 0/1 | 0 → VR 1 | 1: the left hand points and its grab (the left trigger) uses, the right grip reloads. 0: right grip uses the eye ray. |
| `use.hold` / `use.trace` | 0/1 | 1 / 0 | Use held every frame while down; log selection changes. |
| `use.back` | 0–400 mm | 100 | How far behind the controller the use ray starts. |
| `use.filter <on> [minCutoff] [beta] [backlash]` | centi-Hz, ×100, centi-degrees | 1 100 200 75 | Smoothing on the use pointer. |
| `use.find <entityId>` / `use.report` | | | Test harness; lane state. |
| `carry.hand` | 0/1 | 0 → VR 1 | Carried objects in the left hand, thrown with it. **Menu (Experimental).** |
| `carry.hold` | 0/1 | 1 | Hold grab (the left trigger) to keep holding, or press to pick up and press to drop. **Menu.** |
| `carry.throw <gain> <throwCm/s> <maxCm/s> <heavyMaxCm/s>` | 50–300, 20–1000, 100–5000, 50–3000 | 140 160 1600 500 | Throw strength, drop/throw threshold, speed caps. **Menu** (gain). |
| `carry.heavy <kg> <diagCm> <holdMs> <shiftCm> <tauMs>` | | 25 120 500 60 100 | What counts as heavy and how a heavy prop follows. |
| `carry.corpse <minCm> <maxFromNativeCm> <tauMs>` | | 75 110 120 | Where a dragged body is held. |
| `carry.grabpoint` | 0/1 | 1 | Hold a light prop where the ray hit it (0: its centre in the palm). |
| `carry.impulse` | 0–2 | 0 | The game's release impulse: kept and compensated, kept, skipped. A/B only. |
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
| `wrist.report` | report | | Gate, look test, counters. |

### Aim, reticle and shots

| Command | Values | Default | What it does |
|---|---|---|---|
| `aim.twohand` | 0/1 | 1 | Two-hand support aim. **Menu.** |
| `aim.world` | 0/1 | 1 | Prey's crosshair in the world at the aim's hit (1), or on the HUD panel (0). Reports its targets and frames shown. **Menu** (Reticle). |
| `aim.reticlesize` | 50–200 % | 100 | The world crosshair's size against the HUD's. **Menu.** |
| `aim.scene` | 0/1 | 0 | Runs the aim's scene query on its own (diagnostics); the world reticle runs it whatever this is. The old Scene Reticle setting is retired, and a saved `scene_reticle` is skipped. |
| `aim.reticleconverge` | 500–200000 mm | 10000 | The world crosshair's distance when the aim hits nothing within 200 m; on the HUD, the point along the ray it marks. **Menu** (Reticle fallback distance). |
| `aim.shot` | 0/1 | 0 | Shots leave the muzzle along the controller's aim. **Menu.** |
| `aim.enable` | 0/1 | 0 → VR 1 | Controller aim. 0 aims with the head. |
| `aim.reticle` / `aim.reticledispatch` | 0/1 | 0 → VR 1 | The crosshair follows the controller; the movie call half of that write. |
| `aim.reticlecanvas` | 0 raw, 1 model, 2 native | 1 | How the reticle maps onto the HUD canvas. |
| `aim.reticlestage` | 0/1 | 0 | The engine's stage scale flag, canvas mode 2 only. |
| `aim.bodyyaw` | 0/1 | 1 | Hand offsets turn with the body (0: with the camera, a known defect kept for A/B). |
| `aim.origin` | 0 eye, 1 hand, 2 muzzle | 0 | Where the aim ray starts. Diagnostic. |
| `aim.calibratebarrel` | action | | Learns the muzzle offset from the last shot. |
| `aim.shotprobe` / `aim.beam` | | | Research probes. |

### Feedback, abilities, physical

| Command | Values | Default | What it does |
|---|---|---|---|
| `haptics [0\|1] [strength]` | 0–100 | 1 70 | Controller pulses and their strength. **Menu.** |
| `psi.target` | 0 original, 1 head, 2 left controller | 0 | Psi power aiming. **Menu.** |
| `medkit.slot` | 0/1 | 0 | Left-hip medkit. **Menu.** |
| `physical` | report | | Physical wrench and object nudge state; set them in the menu. |

### Stereo and rendering

| Command | Values | Default | What it does |
|---|---|---|---|
| `dual.vr` | 0/1 | VR 1 (launcher `DualStereo`) | Both eyes from one game frame, or alternate eyes. |
| `dual.overlay` | 0/1 | VR 1 | After-world overlays in both eyes. |
| `dual.mode <0..3> [budget]` | | VR 3 | Raw submission mode. Use `dual.vr`, which keeps the ordering safe. |
| `dual.ipd` | 40–90 mm | 64 | Eye separation for flat mode 2 only. |
| `dual.observe` / `dual.capture` / `dual.report` | | | Diagnostics. |
| `near.enable` | 0/1 | 0 → VR 1 | Per-eye offset for the weapon and arms pass. |
| `near.halfipd` | 20–45 mm | VR runtime IPD / 2 | Half the eye separation for that pass. |
| `near.follow` / `near.fov` | 0/1; 0 or 11–1789 decidegrees | 1; follows | The weapon pass's field of view. An explicit `near.fov` stops following. |
| `near.fx` / `near.fxbridge` | 0/1 | 1 | Weapon-space stereo for near particles; the Q-Beam bridge. |
| `near.fxhide` / `near.zero` / `near.lineage` / `near.dump` | | 0 | Test switches. |
| `xr.mirrorfov` | 50–400 % | 100 | Field declared for flat-mirror frames (no eye pair). |
| `xr.native` / `xr.srgb` / `xr.submit` | 0/1 | 0 → VR 1 | Native projection; sRGB swapchain (before start only); per-eye submission. |
| `xr.stereo [ipdMm] [halfFov]` | 45–85 (0 off), 20–70 | | Fixed-IPD camera stereo, over the runtime's IPD. |
| `xr.frustum` | 0/1 | 0 | Renders the runtime's frustum. Prototype; leave off. |
| `xr.runtime <manifest>` / `xr.start` | | | Runtime choice before start; start the session. |
| `xr.timing [0\|1] [hz]` / `xr.resolution` / `xr.coverage` | | | Frame timing, resolution chain, frustum coverage. |
| `xr.hold [0\|1] [ms]` | 0/1, 30..2000 | 1, 100 | Frames from the mod while Prey renders none (a load), so the laser follows the hand. 0 turns them off along with the D3D11 multithread protection they need, for a performance comparison with `xr.timing`. `ms` is how late Prey's frame must be first. Not kept. |

### Diagnostics and legacy lanes

| Command | What it is |
|---|---|
| `dbg.draw [on\|all\|off\|mask]` | In-world debug drawing. Mask bits: 1 rays, 2 raw, 4 body, 8 wrist, 16 foregrip, 32 labels, 64 status, 128 hits. **Menu** (on = every layer). Drawn beside the aim lasers: `off` leaves them. |
| `dbg.lasers [0\|1]` | The aim lasers (rays + hits, 129) for this session, apart from `dbg.draw`. **Menu** (Hands > Aim lasers; on at VR start). |
| `dbg.report`, `dbg.marks` | Overlay counters and primitive positions. |
| `cull.report`, `cull.watch [0\|1]` | Read-only watch on the engine's pass-camera reset, for the job-worker crash at `PreyDll+0x27EC40`. On from startup, VR or not; `PREYVR_CULL_WATCH=0` before launch leaves it out. Its `preyvr_cull_watch` lines in PreyVR.log are what to send with a crash. |
| `capture [tag]`, `capture.hud [tag]`, `capture.inventory [tag]` | One frame of what the headset receives, to a file. Stalls the GPU once. |
| `console <command ...>`, or an allowlisted name alone | An engine console command through the fail-closed allowlist. |
| `mem.peek`, `mem.scan`, `mem.deep`, `mem.watch`, `mem.hits`, `mem.stack`, `mem.str` | Memory inspection and hardware breakpoints. Research only. |
| `frame.capture`, `frame.character`, `frame.offset`, `frame.apply`, `frame.propagate`, `frame.near` | Per-character render-matrix override. Research. |
| `hand.*` (`mode`, `joint`, `offset`, `right`, `left`, `drive`, `wrist`, `turnyaw`, `scale`, `calibrate`, `character`) | The legacy hand-rig lane, superseded by `ik.*`. VR start never arms it, and `hand.mode 2` is refused while `ik.mode 2` runs. |
| `weapon.*` (`observe`, `offset`, `apply`, `rotate`, `calibrate`) | The legacy weapon-mount lane, refused while `ik.mode 2` runs. |
