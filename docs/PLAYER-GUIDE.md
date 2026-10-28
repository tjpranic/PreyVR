# Prey VR player preview

This guide describes the integration branch, including changes after the released
v0.5.1 preview. New comfort settings below require an integration build.

Start your headset connection and its OpenXR runtime, then double-click **Start Prey VR.cmd** in the extracted package. For Quest with Virtual Desktop, connect to the PC first and use your configured OpenXR runtime. Steam must be running. The launcher finds the Steam installation or asks you to select `Prey.exe` once.

The launcher starts Prey, loads the mod, waits for the requested render size, and enables VR. Put on the headset when prompted. Press **A** at the title screen, select **Continue** or **Load Game**, and press **A** at the loading-screen prompt. No desktop mouse is needed for this route.

## Controls

Where Touch and the Steam Frame differ, both are named. Index follows Touch, with its left A/B as X/Y; its
left stick click is pause, so it has no sprint (bind one in SteamVR). The buttons follow Prey's own gamepad
layout wherever VR leaves one free.

| Control | Gameplay | Menu / inventory |
| --- | --- | --- |
| Left stick | Head-relative locomotion | — |
| Left stick click (Index: left trackpad press) | Steam Frame: sprint. Touch, Index: open inventory | Prey's LS: autosort, centre the map |
| Right stick | 45-degree snap turn (default) | Navigate |
| Right stick click | Psychoscope | Prey's RS: drop, split a stack, reset the map's zoom |
| Steam Frame: left D-pad | Prey's quick slots (left, right, down); up: torch | Navigate |
| Right A / B | Jump / crouch | Select / back |
| X (Touch: left; Steam Frame: right) | Prey's X: **grab** and use what the left hand points at (press to pick up, press again to drop); with nothing to use, reload | X action shown by Prey |
| Y (Touch: left; Steam Frame: right) | Prey's Y, as Prey handles it: search, special use, the psychoscope's lock; tap to swap weapon, hold for Prey's own wheel | Y action shown by Prey |
| Steam Frame: bumpers | In Prey's wheel (hold Y): left uses a suit repair kit, right eats food (Prey's LB / RB). Otherwise nothing | Previous / next tab (Prey's LB / RB), on release |
| Steam Frame: View (left) | Open inventory | Close the menu |
| Right grip | Tap: reload (never uses what the left hand points at). Hold: holster the weapon. With the weapon holstered: draw it again | Next tab, on release (Steam Frame: next page, Prey's RT) |
| Left trigger | Prey's psi power (hold to use), or with psychic targeting on, aim it and let go to cast | Previous page (LT), on release; clicks instead when the laser is on the left |
| Right trigger | Fire (hold for automatic fire and to charge the Disruptor); with a grenade: hold it, let go during a throwing motion to throw it | Clicks with the laser (default); next page (RT), on release, when the laser is on the left |
| Left grip | Hold a long weapon's support grip; the left-hip medkit slot and the psychoscope gesture, when enabled | Previous tab, on release (Steam Frame: previous page, Prey's LT) |
| Menu button (Touch: left; Steam Frame: right) | Tap: pause. Hold: VR options | Tap: pause / resume. Hold: VR options |
| Menu + both grips | Reset VR view | Reset and bring the panel in front of you |
| F11 | Enable VR or retry setup | Enable VR or retry setup |
| F12 | Reset VR view | Reset and reposition the panel |
| F10 | — | Open / close VR options |

Release a held stick, trigger, or button after closing a menu before using it in gameplay. This prevents an inventory input from immediately moving, turning, firing, or jumping.

For two-handed aiming, bring your left hand to the weapon's foregrip and squeeze
the left grip. Keep it squeezed while aiming with both hands; release to return
smoothly to one hand. A squeeze away from the weapon does nothing. With the pistol
or the Disruptor, bring the left controller under or beside the right one and
squeeze: the drawn left hand cups the grip the way the game holds it. The right
hand alone still aims; bracing one controller on the other is what steadies it.
Moving the left hand away (about 30 cm) lets go. Changing weapons, opening a menu,
recentering or losing tracking requires releasing and grabbing again; a snap turn
does not.
To recenter, squeeze both grips and press **Menu** (or Menu, then both grips): it neither pauses nor opens VR
Options. F12 also works. With Menu held first, the right grip does not reload either.

To use something, point at it with the left hand: the cyan ray ends on what Prey selected (it bends to it when
Prey picked it from near the line) and Prey shows its usual prompt. Press **X** (Prey's X) for the
prompt's tap action (take, carry, search, take the highlighted item of a locker) or keep it held for the
hold action (drag a body, carry a heavy object); Prey's **Y** prompts are Y. What you pick up goes to the left
hand: a light object flies to the hand and is held by the side you pointed at, a heavy one floats ahead and
follows the hand, a body is dragged from it. Press X again to let go of it: pressed during a throwing motion,
it leaves with your hand's speed (a turret is dropped as in Prey). The right trigger throws it the game's way.
With nothing to use, X reloads, as Prey's X does. `carry.hold 1` (VR Options > Hands > Carry: hold to carry)
keeps the object only while X is held instead, and letting go during a throwing motion throws it with the
hand's speed. To move grab, rebind **Grab and Use (Prey's X)** in SteamVR's controller bindings for Prey.

**Grenades** (EMP, Recycler, Nullwave, Lure) sit in the right hand. Hold the right trigger, throw with your
arm and let go of the trigger: the grenade leaves at that instant with your hand's speed and direction, and comes
down about where a real ball thrown the same way would. Let go with the hand still and it drops at your feet.
Looking at a nearby surface no longer sticks the grenade to it. `grenade.hand 0` restores the game's own
charged throw along the view.

With no weapon drawn -- holstered, or while carrying something -- both hands stay visible at the controllers
(open; the left one closes around what it carries). Hold the right grip to holster the weapon, squeeze it again
to draw it. `arms.free 0` lets the game lower the arms out of view again. Grab does nothing while the left hand holds the foregrip, at the left-hip medkit slot (whose trigger is the
medkit's) or with Menu held for the recenter chord. `use.hand 0` restores the previous layout (right grip uses and reloads along the weapon's aim).
`aim.twohand 0` disables support aiming; `aim.twohand 1` enables it (default).

To equip a weapon, open inventory with **View** (Touch: **left stick click**), highlight it with the right stick, press **X** for Prey's Equip action, then **B** to return to the game. Follow the native button prompts for other items. B backs out one level at a time in nested screens.

The Touch profile has been tested in the injected game through xr-sim; physical controller acceptance remains. Index bindings are included: left A/B correspond to X/Y, pressing the **left trackpad** opens inventory, and **left stick click** opens pause. **Steam Frame** controllers get their own profile when SteamVR offers it (`xr_input_extension name=XR_VALVE_frame_controller_interaction enabled=1`, `detail=bindings profile=frame path=...`, and `interaction_profile ... valve/frame_controller` in the log). Each button then does what a gamepad's does in Prey, so the game's prompts name the button to press: right **X** and **Y** are Prey's X (grab and use; reload with nothing to use) and Y (search, special use; tap to swap weapon), **View** opens the inventory and closes menus, **Menu** pauses, the stick clicks sprint (left) and toggle the psychoscope (right), and the left **D-pad** is Prey's quick slots in play (up: torch) and navigates the game's menus and VR Options (left/right adjust there, up/down move). Inside Prey's weapon wheel (hold **Y**) the **bumpers** are Prey's LB and RB, a suit repair kit and food; outside it they do nothing in play, and in menus they change tabs. Without the profile SteamVR presents them as Touch: right X and Y act as B, the D-pad arrives as left X (down) and Y (the rest), and View is the menu button. To rebind any action, use SteamVR's controller bindings for Prey. Other controller profiles do not yet have dedicated mappings. Headset rendering uses OpenXR; the panel fits both runtime eye frusta, including their asymmetry.

## Comfort settings

### In-headset VR options (integration build)

Hold the **menu button** (Touch: left; Steam Frame: right) **for about 0.65 seconds**. VR Options opens over
whatever you are looking at: the game itself, or a game menu such as the pause
menu. **The game is not paused** -- in play it keeps running behind the panel,
with the HUD hidden and your movement, turning, firing and use held until you
close it. A short tap of the same button is still the game's own pause. If a
game menu opens or closes under VR Options (a cutscene, a loading screen, the
menu you opened it from), VR Options closes.
F10 is a desktop shortcut; `vr.options 1` is the diagnostic equivalent. Both
work in play and in menus.

Each setting is a checkbox, a choice list or a slider, on nine tabs (listed
below). The panel opens 2 metres straight ahead of where you faced, over the
game menu if one is up. **To move it, hold the trigger with the laser on any blank part of it** --
a setting's name or help text, empty space, not a control (the hint line says
so when you are on one). It follows the laser round you, staying upright and
facing you, and slides straight up or down rather than tilting; push the laser
hand's stick forward to send it further away, back to bring it closer (nearer
reads larger). Let go of the trigger to leave it there. It opens there again,
relative to where you face, until VR restarts (F11) or the game closes. The game menus' distance and
offset settings do not move it. Point and pull the trigger or press **A**, or move
with the **right stick** up/down (or a Steam Frame's left **D-pad**) and press
**A**; **right stick left/right** steps the slider or list under the laser (or
the one last selected), and repeats while held; the **left stick** scrolls a
long list; a **bumper** (Steam Frame) or **grip** changes tab when let go (both grips together change nothing) and **B** closes it, back to the game or the
game menu. Releasing the controls is required before they can operate what is
behind it. See [the panel's notes](VR-OPTIONS-IMGUI-2026-10-13.md).

- **Comfort:** snap angle / smooth turning, head-relative movement, smooth-turn
  speed, the move and turn stick deadzones, and sideways and backward walking
  speed. The character's body turns to face where you face: with the stick,
  with each snap turn, and as you turn round in the room (looking over your
  shoulder does not turn it; turning further does). It walks with you as you
  move round the room, stopping at walls and furniture, and bends with you as
  you crouch and lean: lean, and the torso bends; step, and once you stand
  still it catches up under you. The stick and snap turns turn you about the body, so you
  keep your place on it. Push your head into a wall and the view fades to
  black; firing, using and psi wait until you pull back.

  Reset VR view is on the Setup page.
- **Hands:** **aim lasers** (on by default: a laser from each hand to what it
  points at -- where the right hand aims, faint with no weapon drawn, and what
  the left hand can grab or use), two-hand aim on/off, hold/toggle foregrip, snapped/free support hand,
  the optional psychoscope gesture, the hands without a weapon, **fingers follow
  the controller** (on by default: a free hand's thumb rests on a button or
  lifts, the index points or curls on the trigger, the other three close with
  the grip), **arm length** (shoulder to wrist, 56 cm by default: lower it if
  your arms look bent when yours are straight, raise it if your hands stop short
  of the controllers), the carry grip (hold, or press to toggle), hand or game grenade
  throwing, how hard thrown objects and grenades fly, **Hands Stop at Surfaces**
  (on by default: your drawn hands and weapon stop against walls, floors, desks,
  doors and glass and slide along them, though your real hands go on; shots,
  grabbing and psi start where the drawn hand is, while wrench strikes follow your
  real swing) with **Heavy Objects** (10 kg by default: loose objects heavier than
  this stop your hands too, and slide away slowly while you press into them;
  lighter ones, corpses and characters never stop them), **Physical Contact**
  with its push strength (on by default: your weapon and free hands push loose
  objects and bodies; light ones keep up, heavy ones resist, friction lets you
  drag them, a body moves limb by limb, and what you stand on is never pushed),
  and the **Free-Swing Wrench** with its minimum and heavy swing speeds (on by
  default: swing the wrench in real life to hit what it touches; a harder swing
  hits harder, and from the heavy swing speed it lands Prey's charged hit, with
  more damage, knockback and stamina. Prey's own damage, sounds, broken glass and
  stamina apply; out of stamina it pushes but does not strike, and you cannot hit
  through walls. Each hit buzzes the controller. The attack button does nothing
  while the wrench is out).
- **Interface:** size, visible-area limit, panel curvature, controls guide, where
  the game's menus float (distance, left/right, up/down), the text panel's
  distance, **Screen Distance** (how far your view stays back when you use a
  keypad, terminal, security station or fabricator; 30 cm by default, never
  further back than you were standing; Prey's own camera puts the screen a hand's
  width from your eyes; on a screen, zoomed or not, what highlights is what the
  crosshair sits on), which hand's laser points at menus, **controller prompts** (on by
  default: menus show your controller's buttons, not the keyboard's, while the
  laser points at them), the HUD style (floating
  panel, or in the world as Prey draws it), and the reticle: in the world or on
  the HUD, its size, and how far it sits when your aim hits nothing.
- **Equipment:** optional body holsters, clear holster assignments, wrist status,
  wrist-hologram size, keeping health, suit and psi on the HUD too, the wrist
  tick, and the display's position along the forearm, height and tilt. Holsters
  default off; the wrist status defaults on.
- **Feedback:** controller feedback and its strength.
- **Abilities:** psychic targeting (Original / Head / Left Controller), the
  optional left-hip medkit slot, and body slot feedback. Original targeting and
  slot off are the defaults.
- **Setup:** calibrate posture, reset VR view, **Lean Zone** (how far you can
  lean before the body steps after you; 25 cm by default, off follows you at
  once), **Hips and Legs Back** (how far behind your eyes the drawn hips and legs
  stand; 8 cm by default, raise it if the body seems to lean back when you look
  down), **Near Clip Distance** (how close to your eyes things are still drawn;
  3 cm by default, lower it if you can see through your own body when you look
  down, raise it if distant surfaces flicker; Prey's own is 10 cm), and
  **Debug Overlay (Testing)**,
  which draws each hand's ray and target, the body slots, the wrist test and the
  foregrip region in the world. Off by default, and separate from the aim
  lasers: switching it off leaves them as they are. See
  [the overlay note](DEBUG-OVERLAY-2026-10-06.md).
- **Controls:** what every button does on the controller you hold, by the names
  SteamVR gives them: play, menus and any time. It follows SteamVR: rebind an
  action there (Settings > Controllers > Manage Controller Bindings > Prey) and the
  tab shows the new button. Read only.
- **Experimental**, features still being proven: physical carrying in the left
  hand (on by default; off restores the game's carry in front of you), sticking
  charges to the surface you look at, long weapons held back toward the body,
  **Shots from the Muzzle**, **Scene Reticle**, and the inventory on a layer of its
  own, with optional depth.
  **Shots from the Muzzle** makes every shot leave the weapon's muzzle along
  your controller's aim, so it lands where the barrel points at any distance,
  also from the hip or around a corner (otherwise the shot is aimed from your
  eyes and misses by up to several degrees close by). Off by default;
  `aim.shot 1` in **Tune Prey VR.cmd** turns it on for a session. See
  [the shot note](SHOT-RAY-2026-10-06.md).

**Psi powers:** equip a power using Prey's normal quick wheel. The **left trigger** is Prey's own psi button
(Original targeting): hold it to use the power, aimed as Prey aims it. It does nothing at the left-hip medkit
slot or on a weapon's foregrip. **Psychic targeting:** choose Head or Left Controller on the Abilities
tab, then hold the left trigger to target and release it to cast. Head follows where you look; Left Controller follows the
left controller's aim axis. Release the support grip before targeting. The game
keeps its native power costs, cooldowns, target filters and unlock restrictions.
Menus, recentering, tracking loss and long frame gaps cancel a pending cast;
release the trigger before trying again. Original restores the existing controls.

**Medkit slot:** once enabled, bring the left controller to your left hip,
release grip, then squeeze **left grip**. A short pulse confirms selection.
Keep grip held and press **left trigger** to request one medkit use; you may
move the hand away from the hip first. Release grip before another use. Prey
checks the native inventory and whether the item can be consumed, so an empty
inventory or full health does not force a heal. The slot uses an estimated body
position and currently has no visible medkit prop. This feature and psychic
targeting have offline/static checks; native and headset acceptance remain.
See [implementation and acceptance checks](PSI-MEDKIT-2026-09-30.md).

**Holsters:** with a weapon equipped, bring the right controller to your right
hip or across to your left chest, release grip, then squeeze. An empty slot stores
that weapon and requests its normal stow animation. Squeeze the same slot again
to draw it. If its weapon is already equipped, the squeeze stows it. Drawing a
different slot switches through Prey's native equipment transition. Release
between actions; drifting into a slot with grip held does nothing. Two-handed
aiming, menus and recenter take priority. Clear assignments on the Equipment tab
to bind different weapons; weapons remain in your inventory. Assignments are
session-local, not saved. This first version uses inferred hip/chest positions,
not tracked torso/hip hardware, and has no visible holstered weapon props.

**Wrist status (hologram on the left forearm):** check it like a watch. Raise the left
forearm and turn its back towards you, then look at it. After a moment a small
glass panel unfolds just above the sleeve, behind the wrist. It shows the game's own
health, suit and psi meters and any status effects such as bleeding or radiation,
and the left controller gives a faint tick. The panel stays on the arm you see. It
turns towards your eyes by up to 40°, and its text stays level even with the arm held
out in front. It fades when you look away or turn the wrist back. It stays hidden
while you hold a weapon with both hands, in menus and the inventory, and when the arm
is inside a wall or your right hand or weapon is in front of it. A hand sweeping past
your view does not open it. Size adjusts from 80% to 140%.
With the wrist status on, health, suit and psi are **no longer on the screen in front
of you**: they live on the wrist. The rest of the gameplay HUD (reticle, prompts,
ammo, objectives) is unchanged, and the game still warns of low health with its red
screen edges and heartbeat. Turn WRIST STATUS off to get the meters back in front.
On by default. See [the hologram note](WRIST-HOLOGRAM-2026-10-07.md).

**Toggle foregrip:** squeeze near the foregrip to attach; release the grip button
and squeeze again to detach. **Free support hand** keeps its visual position on
the controller while both hands still steer the weapon (a pistol is never steered
by the left hand, so Free shows nothing there). Weapon changes, menus, tracking
loss and recenter invalidate the attachment; snap turns keep it.

**Psychoscope gesture is off by default.** Once enabled, bring the left hand to
your forehead, squeeze, and pull down at least about 14 cm. Release before the
next gesture. A lift from near the eyes requests the same native toggle to put it
away. Each deliberate stroke requests one toggle; the game retains its unlock,
cinematic and movement restrictions. This does not add a visible visor prop.
Gesture feel and native acceptance still need headset testing.

Menu choices save automatically to **`%LOCALAPPDATA%\PreyVR\vr-options.ini`**:
every setting the panel shows, once you have changed it. That file is the
one place they are kept: they are applied each time VR starts (F11 included),
over VR start's own defaults. A setting you never changed in the menu keeps its
default. **Tune Prey VR.cmd** changes a setting for this session only, until the
next VR start; it never changes the file. To return a setting to its default,
delete its line from that file with Prey closed, or rename the file to reset
them all. `PreyVR.json` keeps only the launch settings below. Every tuning
command, and what the menu saves, is in [the tuning reference](TUNING-COMMANDS.md).

The panel has been worn in a headset (2026-10-13 and 2026-10-14); this build's
tidy-up -- one panel, the nine tabs above, each controller's own bindings --
has offline tests only. See [the panel's notes](VR-OPTIONS-IMGUI-2026-10-13.md)
and, for the original card, [its validation](VR-OPTIONS-2026-09-27.md).

### Launcher settings

The launcher saves its launch settings in `PreyVR.json` beside the launcher.
Edit the existing fields with Prey closed, or pass the corresponding PowerShell
parameter to `Start-PreyVR.ps1`. Explicit parameters override saved values.

| Setting | Default | Accepted values |
| --- | --- | --- |
| `Width`, `Height` | `2016`, `2160` | Per-eye render size (see Display below) |
| `ReferenceSpace` | `"auto"` | `"auto"` prefers LOCAL_FLOOR, then STAGE, then LOCAL; `"local"` forces LOCAL |
| `DualStereo` | `1` | `1` both eyes from one frame; `0` alternate eyes (see Display below) |

**The desktop window is not what the headset sees.** `Width` and `Height` set the
headset's image. Prey places the desktop window itself, at that size, so on a smaller
screen part of it is off the screen. The mod holds focus for Prey while it runs, so a
notification or a click elsewhere does not pause the game or mute it. The keyboard
shortcuts (F10, F11, F12) only work while the window has focus. See
[the desktop window notes](DESKTOP-WINDOW-2026-10-17.md).

Everything else is a VR Options setting. An older `PreyVR.json` may still hold
`SnapTurnDegrees`, `HeadRelativeMovement`, `UiScalePercent`, `UiCurveDegrees`,
`UiGuide`, `PointerHand` or `HudLayer`: the launcher moves any you had changed
into `vr-options.ini` once (unless the menu already holds that setting, which
wins), removes them from `PreyVR.json`, and says what it moved. It also drops
`NewVROptionsUI`, which chose between two VR Options panels before there was
one.

Turning and movement direction are on the Comfort tab. For this session only,
use `move.snap 30` or `move.headrelative 0` in **Tune Prey VR.cmd**. A command
typed without a value reports the current setting and changes nothing. Return
the stick to neutral after changing modes.

The in-headset option is what persists.

`view.height` reports the chosen reference space and calibrated height.
`view.calibrate` (Setup, Calibrate Current Posture) sets a new baseline from the
current tracked head position; ordinary recenter preserves it. The body takes
that height as standing, so calibrate standing up straight; calibrated seated,
your seated height is the body's standing height. Crouching physically bends
the drawn body, but Prey's own crouch, with its smaller collision, is still the
B button. Avatar-height matching and a vignette are not implemented.

## Display

Menus and inventory open on a floating panel **2 metres** away, straight ahead of the way you are facing, at eye level and square to you, however your head is tilted when it opens. It stays where it opened until the menu closes. The new VR Options panel opens 2 metres ahead and can be dragged aside (see VR options above). Changing menu distance or offset moves an open menu relative to where you faced when it opened, not where you are looking now. Reset view to bring a panel back in front of you. The new VR Options panel's **Interface** tab sets the game menus' distance and offset and saves them; for this session only, `ui.distance 150` in **Tune Prey VR.cmd** moves it (100 to 400 cm); it keeps the same apparent size, so `ui.scale` stays the size control. A bare `ui.distance` reports the current distance. `ui.offset 15 -20` (also this session only) moves it around you in degrees: the first number to your right (negative is left, -90 to 90), the second up (negative is down, -45 to 45). It slides straight up or down rather than tilting toward you, so it stays upright, at the same distance across the floor and the same size; `ui.offset 0 0` brings it back to straight ahead, and a bare `ui.offset` reports the current offset. An optional card shows the menu controls (VR Options, Interface tab, Controls Guide; hidden by default). The combined layout stays within conservative limits of **60° horizontally and 40° vertically**, shrinking further for narrower runtime frusta.

The native gameplay HUD is captured once into a transparent target and displayed on a floating panel about 2 metres in front of you. Its graphics and interaction prompts stay Prey's own. The radial weapon and ability wheel comes off that panel when it opens, with the hotbar and the Equip/Cancel prompt that open with it: they appear at the same size and distance, **in front of where you are looking, and stay there** while you choose, rather than following your head. Outside the wheel, the hotbar stays on the HUD panel. **Prey's own crosshair sits in the world, on what you aim at**: it comes off the HUD panel and is drawn at the surface your weapon points at (10 metres away when nothing is in the way within 200 metres), so it stays sharp when you look at your target instead of doubling as it did on the panel 2 metres away. It keeps the size it had on the HUD at any distance, and every weapon's own crosshair, hit markers and charge indicators. The interaction prompt and quick loot follow it, as they do on a flat screen, at the same depth. VR Options' Interface tab sets it in the world or on the HUD panel, its size (50–200%), and the distance used when nothing is hit; it needs the HUD on its floating panel. With the HUD drawn in the world instead, the crosshair stays in the HUD. Subtitles and tutorial cards appear on a panel of their own, 1.4 metres away and in front of the HUD, and follow your head like it, while the world stays visible around them; being nearer, their text is larger and sharper. Books and notes open at the same distance and size, **in front of where you are looking when they open, and stay there** while you read: look around the page, or away and back, and it does not move with you. Opened while looking down at a desk, the page appears down there, tilted to face you (up to 35° above or below eye level). Close it and the next one opens wherever you look then; Reset view brings an open one back in front of you. The VR Options panel's Text Panel Distance row moves both and saves it; `ui.text 120` in **Tune Prey VR.cmd** moves them for this session (100 to 200 cm; nearer is larger, 200 matches the HUD panel).

Objective markers stay in the world view, in both eyes, at no particular depth: they read as far away, which suits distant objectives better than nearby ones.

The default render size is **2016 × 2160 per eye**, independent of monitor DPI. This restores the tall aspect used in earlier headset testing; **2688 × 2880** offers more detail at the same aspect and higher GPU cost. Edit `Width` and `Height` in `PreyVR.json`, then restart Prey. Menus can have unused space within their floating panel; do not switch the world render to 16:9 just to fill that panel. This is a starting preset, not automatic headset-resolution detection or a guarantee of complete FOV coverage on every headset.

The launcher migrates the old, unversioned **2560 × 1440** default once, backing up `PreyVR.json` first. Custom sizes remain unchanged. To intentionally retain that old size, add `"ResolutionDefaultsVersion": 1` to the JSON, or launch with both `-Width 2560 -Height 1440`. A single command-line dimension is rejected. The HUD style -- a floating panel, or drawn in the world as Prey draws it -- is on VR Options' Interface tab.

Both eyes are rendered from the same game frame by default (`DualStereo: 1`). `DualStereo: 0` (or launching with `-DualStereo 0`, which is also saved) returns to the earlier method that renders one eye per frame; it shows doubled edges when you or the scene move. Use it only to check whether a problem belongs to the both-eyes method, and report what you find. Fades, markers and anything else Prey draws over the world are drawn into both eyes; `dual.overlay 0` in **Tune Prey VR.cmd** leaves them in the right eye only, for comparison, and `dual.overlay 1` restores them. Once VR is ready, the launcher prints which method is running. If the both-eyes method is unavailable, it says so and Prey continues with alternate eyes.

While Prey loads, it draws only its loading screen, outside its normal frames, so the mod shows that screen on the menu panel and keeps the laser following your hand ([the hold-frame notes](LOADING-LASER-2026-10-17.md), [the loading-screen notes](LOADING-SCREEN-2026-10-20.md)). That needs Direct3D's thread protection on for the whole session, which may cost a little frame rate. `xr.hold 0` in **Tune Prey VR.cmd** turns both off for this session (loads show the frame from before them again, with the laser frozen); `xr.hold 1` turns them back on.

## Recovery and support

If startup fails, the launcher names the failed stage and the diagnostic folder under `%LOCALAPPDATA%\PreyVR\runs`. While VR is running, that folder's `PreyVR.log` also records the rendering, HUD and body-following counters once a minute (lines starting `preyvr_vr status`, with `body={...}` last); include it when reporting a problem. Resolve the headset/runtime issue, return focus to Prey, and press F11 to retry. Reconnect the runtime before retrying a lost XR session. F12 and the grip chord need a fresh tracked headset pose.

This package supports the verified **Steam Prey 2017 x64 build** only. The launcher checks `PreyDll.dll` before loading anything; it does not patch game files. Keep the package files together. It also refuses to launch over another detected fleet game.

This is a preview. The new interface has simulator and in-game checks; headset comfort, every weapon, and every in-world terminal still need player acceptance. Rendering both eyes every frame costs frame rate, and that cost has not yet been measured properly at headset resolution.
