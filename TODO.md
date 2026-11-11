# PreyVR

## TODO

### Compatibility
- Need to make sure the mod controls are compatible with other headsets (Meta Quest, Valve Index, HTC Vive etc.)
- Since different headset use different per-eye resolution, Prey's FOV setting may need to be adjusted per-headset type
    Steam Frame = 110
    May need to build a calculator for this?

### Rendering
- DLSS (?)
- Upscaling (?)
- Foveated rendering (?)
- Frame geneneration (?)

### UX/Immersion
- Rotate to face direction (?)
    Add in an option to rotate the camera in the direction the right hand is facing. Alternatively, having the player body and camera follow the left/right hand when holding a certain button?
    Maybe a way to snap to the place I'm aiming at?
- Recenter player body when the player stops moving (?)
- Two-handed wrench (?)
    Add in a two-handed wrench mode that automatically triggers heavy wrench attacks. One handed mode would always be a light attack. This would replace the speed based light/heavy attack system.
- Two-handed carrying (?)
- Physical touch screen controls for computer monitors and keypads (?)
- The wrench tends to hit around the hand area instead of the head of the wrench
    Maybe adjust the hitbox so that it's just around the head of the wrench?
- Adjustable wrench bounding box
    Add in an adjustable wrench bounding box lengthening system that only registers hits against enemies. Essentially, an additional bounding box would be added to the wrench, but it would be longer and only used to find collisions with enemies. This would preserve existing correct physical interaction between wrench/hands and objects, but allow for smoother feeling combat.
- Immersive reloading
    Right now it just plays the animation. It would be better to allow the player to physically attach and detach weapon ammunition.
- Better throwing
    Maybe an option to point with left hand where you want the grenade to go?
- Wrench hits use too much stamina
    I think swings *back*, away from the target may be getting counted. Maybe add a very short cooldown after a hit connects?
- Fine dextrous finger controls
    Definitely want this for the glass button pressing jumpscare.
- Stopping hand -> object interactions from pushing objects through other objects
    e.g. Pressing down on a mug on a desk can push the mug through the desk
- Physical crouching
    Crouching in real life should enable crouch mode. Standing up should disable crouch mode.
- Optional body model rendering (?)
    Have it only render the hands like in Half Life Alyx?
- The Earth and Moon are very obviously rendered way too close
    Having them rendered at a more realisitic depth would be nice but challenging.
    Technically everything is working game wise, it would just be an improvement.
- Can't grab when two-handing weapons
    Grabs are only allowed with the left hand, will need to rethink how this works
- The in-game cutscenes show on a flat plane but it needs to be adjusted downward a bit
    The first-time Typhon neuromod usage trigger 3 of these cutscenes in a row. It also causes the game to pause between each cutscene.
    Might also want to have the interface VR options apply to cutscenes if applicable.
- Have container search interface on left hand laser hit
- Might want to remove the Q-beam holding offset
    May not be necessary.
- Audit body holster implementation
    Not sure how much work has been done on this and how it works.
- Aiming lasers can show briefly in-between loading screen and press A button screen (?)
    Only sometimes? Can't replicate anymore.

### Technical Debt
- Refactoring, simplification and consolidation
    Low-hanging fruit like dead code removal, renaming and de-duplication should be prioritised for the first pass. Next should be low-risk refactors that involve refactoring code that can verified with just tests and no VR headset runs.
    Code should also be reorganized and restructured to be more understandable, where possible.
    One thing I'm particularly concerned about is 1. bad abstractions and 2. logic that should be abstracted but isn't. A lot of hyper-focused work has been done on various issues that may leave similar logic unconnected and ungeneralized when they ought to be.
- Optimization
    The mod is starting to lag a bit due to all the feature work. Two types of optimizations are needed. Big O optimization of code and actual targeted optimization of bottlenecks (this will require instrumentation). Will need to look for quick wins on both, if any.
- Tuning parameter cleanup
    One set of functionality that's going to need to be refactored out at the end of the project is all the tuning parameters. If they can be made debug build only, then that is preferred, otherwise they should be removed.
    Some tuning options literally just gate bug fixes behind commands. Absolutely no reason for this, they need to be identified and removed.
- Instrumentation cleanup
    Removing (or gating behind a debug only build) of all the console logging and bug-finding instrumentation needs to be done at some point.
- Documentation cleanup
    Feature work has been tracked across multiple documents. These should be summarized and combined into one document.
    Also need to update any out of date comments.

### HUD/UI
- 2D marker (objective, repair etc.) position jitter a lot (?)
    They could use some smoothing.
- Weapon wheel should open where I'm looking

### VR Options Menu
- Have bindings section let you change your bindings (?)
- Group related options in the tabs into sections
- Development tab
    This tab will be used to run input tuning commands because sending commands through the "Tune Prey VR.cmd" script is way too annoying to use properly.
- The arms length option doesn't affect the whole arm, just the forearm

### Engine Instability
- Very rarely, the mod will fail to attach to the game
    Closing and re-running the mod works just fine, but it's worth looking into

## IN PROGRESS

## DONE

### Engine Instability
- Use-after-free camera bug (Pending recurrence)
    > A pause-menu close could leave the culler a frame behind for the rest of the session. The cull guard (CullPrepareGuard.cpp) pairs that frame. No sign of the lag in 61 runs, so the watch was removed on 2026-10-27; the guard stays. See docs/ENGINE-INSTABILITY.md.
- Camera flashing bug (Pending recurrence)
    > The same cull lag, fixed by the same guard.
- Texture view use-after-free bug (Pending recurrence)
    > Did not recur in 41 runs. The watch was removed on 2026-10-27. See docs/ENGINE-INSTABILITY.md for what to build if it comes back.
- Very rare render freeze (Pending recurrence)
    > The camera doesn't render any new frame for a short time. Not related to the use-after-free camera bug. Instrumentation in place: FrameProbe.cpp, now a flight recorder that opens on the stall itself and sums up where each thread waited (PreyVR-98). See docs/ENGINE-INSTABILITY.md.
- Phantom crash (Pending recurrence)
    Didn't leave an error dump since it crashed before the process could write one.
    > A stack overflow: Steam recorded exit code 0xC00000FD. OverflowCapture.cpp now writes a dump and a report that survive one, and the launcher keeps each session's Game.log (PreyVR-98). Waiting for it to happen again. See docs/ENGINE-INSTABILITY.md.

### Controls
- The movement stoppage bug (Archived)
    > Prey re-read the stick from the physical gamepad whenever an action filter changed (mostly the interaction prompt as the view swept over usable objects) and zeroed what the mod had posted. Fixed in PreyVR-72, worn OK 2026-10-24. See docs/MOVE-STOP-2026-10-24.md.
- The X to transfer junk in the recycler doesn't work (Archived)

### HUD/UI
- Controller Prompts option doesn't seem to apply sometimes (Archived)
- The game doesn't render loading screens (Archived)
    It just shows a black screen instead.
- Objective markers don't display at the proper depth (Archived)
    They need to be at the depth they appear at in the world. Repair markers (and possibly enemy detection markers) also affected.
- Stun gun max charge effect at the wrong depth (Archived)
    Q-Beam has a similar issue
- The neuromod cutscene that plays when you first use a neuromod only appears in the right eye (Archived)
- Interacting with computers and screen is off (Archived)
    > A screen highlights what Prey's own pointing ray hits, which starts at the eyes. With a gun drawn the crosshair starts at the muzzle, so the highlight sat 20-40 cm from it. The screen now gets the crosshair's ray. PreyVR-96, worn OK 2026-10-26. See docs/WORLD-UI-POINTER-2026-10-26.md.
- Reticle drifts from the actual aim point sometimes (Archived)
    Happens after examining a monitor or keypad.
    > Leaving an examination, Prey moves the crosshair inside its clip by the difference between its stored reticle position and the one the mod sends the HUD. Those differ in VR, so the crosshair kept an offset of several degrees. The world reticle now puts it back where the HUD movie places it. PreyVR-97, worn OK 2026-10-27. See docs/RETICLE-DRIFT-SCOPE-2026-10-27.md.
- The hand lasers start outside of player hands
    They seem to begin out of nowhere. Something like a fade in along the length (like when the laser fades out in menus when it's not hitting a surface) would make them look a bit nicer.
    The lasers also render over the top of everything.
- Hacking minigame doesn't display, it just shows a black screen
    > Prey hides the world behind the minigame, as it does for the PDA, and the mod didn't count it as a menu, so the headset got no image at all. It now opens on the menu panel, the left stick flies the cursor and there's no laser. PreyVR-122, worn OK 2026-10-31. See docs/HACKING-MINIGAME-2026-10-31.md.
- Disable aim laser on loading screen
    > The loading screen no longer has a laser, and neither does "press any key" after it. That prompt is Prey's pause menu in a mode of its own, so the mod reads the mode, and a real pause keeps its laser. PreyVR-123, worn OK 2026-10-31. See docs/LOADING-SCREEN-2026-10-20.md.
- Psychoscope zoom is broken
    > Prey zooms by narrowing the view's FOV, and the mod told the headset the true FOV of each frame, so the image covered less of the lens instead of magnifying. Zoomed frames are now declared at the FOV slider's frustum, with the eyes moved closer by the zoom, and the world reticle and wrist card follow the magnified image. Every zoom in the game goes through this. PreyVR-120, worn OK 2026-10-31. See docs/PSYCHOSCOPE-ZOOM-SCOPE-2026-10-31.md.
- Left hand laser doesn't hit at surface of objects unlike the right hand aim laser (?)
    Only sometimes? This might be intentional? Will need to investigate.
    > "Left hand laser doesn't hit at surface of objects" looks explained, from reading the code only. When something is selected, the left laser ends on the object's bounding box rather than its surface, and it only uses a real scene hit when nothing is selected. That would also fit the "only sometimes" in your note.
- Some UI elements still only display at the periphery of vision
    It makes them hard to see. They need to be moved a little closer in. Will need some additional VR options menu settings to adjust them.
    > Interface Size also sized the HUD, and Prey pins its corner groups 5% in from the edge of a canvas that is square in VR, so at 200% they sat 33° out (43° at the corners). The HUD now has its own settings on the Interface tab: HUD Size (200%), HUD Width and HUD Height (70% and 55% of Prey's layout, which bring the corners to about 25° and 20°) and HUD Distance. The equipped psi power moved to the wrist with the meters, and markers show only over what they mark, none at the edge of the view. PreyVR-141 to PreyVR-143, worn OK 2026-11-08. See docs/HUD-PLACEMENT-2026-11-07.md.
- Will need to adjust UI element depths
    Need to adjust for HUD and psychoscope.
    > The HUD was head-locked at 2 m, drawn over anything nearer, the helmet's rim included. It now floats at HUD Distance (1 m by default, 50-200 cm) with the wheel, subtitles and tutorial cards, and all keep their apparent size. It is not put at the helmet's depth, which is too close to focus on; pulling the corners in keeps them inside the visor instead. Markers, the crosshair, quick loot and prompts stay at world depth. The psychoscope needed nothing. PreyVR-141 to PreyVR-143, worn OK 2026-11-08. See docs/HUD-PLACEMENT-2026-11-07.md.
- Can't switch to some weapons when using the Psychoscope
    Not supposed to; the game restricts this.
- Remove the background from the wrist status HUD
    Also remove the resizing
    > The glass panel behind the meters is gone, and so is the fit that stretched them to fill the card: a status effect, an equipped power or a longer bar after an upgrade used to shrink every meter. The wrist now shows one fixed region of Prey's status, with room for all of those, and Wrist Display Size (now 50-200%) is the only thing that sizes it. It no longer grows as it fades in. The status holds still when Prey sways the HUD. PreyVR-150 to PreyVR-152, worn OK 2026-11-11. See docs/WRIST-HUD-SCOPE-2026-11-11.md.
- Psi energy shows on wrist before installing first neuromod
    > The wrist forced the psi meter on. It now asks Prey's own test, the one its HUD uses, so the meter shows from the first neuromod on, as it does in Prey, or while Prey has it open. PreyVR-150, worn OK 2026-11-11. See docs/WRIST-HUD-SCOPE-2026-11-11.md.

### UX/Immersion
- The whole player body sometimes jumps out of place for an instant, then back, while walking with the stick
    Intermittent, can't reproduce on command.
    > A regression from the lift fix. When room-scale moves the capsule after a walking body, it moves it from its physics, a step ahead of where the body was drawn, and the lift fix took the body after that move as where the next pose starts from: the next pose then carried nothing while the view moved a step, so the body stood a step behind for a frame. The pose now starts from where the body was drawn plus room-scale's own move. Found with a glitch recorder (PreyVR-147: 62 such frames, one step each, no IK pass ever skipped); fixed in PreyVR-148, worn OK 2026-11-08. See docs/BODY-FOLLOW.md.
- Going up or down the main lift causes body position jittering
    The actual body is moving up and down, might be a side-effect of all the body following and IK changes.
    > Two causes, both the mod's. The view: room-scale took a body standing on the moving lift for one at rest (its own velocity is relative to the floor), and its at-rest move, the engine's teleport, dropped the lift as the body's ground for a step. A body its floor carries now counts as moving, and settles wait for the floor to stop (PreyVR-144). The body: the IK pass places the body, arms and hands from the last frame's view, and the main lift moves at 27 m/s, 40 cm a frame; that view is now carried with the body to the pose, which also stops the hands trailing a step when walking (PreyVR-146, measured with PreyVR-145). Worn OK 2026-11-08. See docs/BODY-FOLLOW.md.
- Have Psi-powers aim with the left hand
    > Hands' Psi Aim Hand picks the hand: Right (the default) is Prey's own aim, which already follows the right hand; Left aims from the left index finger, and the left laser turns violet while aiming. The left trigger is Prey's own psi button either way, so psi hypos still work from the wheel (worn OK too). It replaced Abilities' Psychic Targeting (Original / Head / Left Controller); the mod's own psi trigger and the head aim are gone. PreyVR-135 and PreyVR-136 (the cleanup), worn OK 2026-11-07. See docs/PSI-MEDKIT-2026-09-30.md.
- Hands don't move when aiming a Typhon ability
    Or when opening the radial menu. Can't see the wrist status HUD when using the radial menu either.
    > The wheel and psi aiming stop the player's own clock, and Prey then stops starting the player's animation (every other character gets a tiny frame time instead), so the IK that moves the hands never ran. The mod now starts it with Prey's own call while the clock is stopped: the world and Prey's pose stay frozen, the hands, body and wrist HUD follow. PreyVR-134, worn OK 2026-11-05. See docs/TIME-STOP-SCOPE-2026-11-05.md.
- Zero-G doesn't have rotate controls
    > Prey rolls with its lean buttons (both together brake) and pitches the whole body with the right stick; the mod sent neither, and the view, the body's turns and the hands all assumed a level body. The bumpers now roll and brake and the right stick pitches, Smooth or in steps (Comfort's Zero-G Roll and Zero-G Pitch, smooth by default); the view, the body's turns, the hands, aim, lasers, throws and IK follow the tilted body. PreyVR-111 and PreyVR-112, worn OK 2026-10-30. Left for later: heavy carry while tilted, and moving where you look. See docs/ZERO-G-SCOPE-2026-10-30.md.
- Zero-G helmet overlay doesn't render properly
    It doesn't follow the camera view
    > The helmet hung off Prey's eye joint, which VR never moves, so it stayed level and faced the body. It now sits on the headset's eye and moves with your head (Prey's sway left out). Interface's Space Helmet hides it, and Helmet Height, Forward / Back and Size place it (worn at 50 mm down, 30 mm back, 120%). PreyVR-110, worn OK 2026-10-30. See docs/ZERO-G-SCOPE-2026-10-30.md.
- Shot from muzzle laser shows skewed (Archived)
    The game naturally skews the aim laser when it comes from the muzzle. It should be straightened.
- Weapons and hands don't stop against surfaces (Archived)
    > They now stop at fixed scenery and at heavy (Heavy Objects setting, 10 kg) or immovable loose objects, which slide when pressed. See docs/SURFACE-STOP-SCOPE-2026-10-21.md.
- Hand animation plays when taking the wrench out (Archived)
    It causes the right hand to spin. The animation should not play.
- Body following and roomscale (Archived)
    Body following and roomscale implementation needs to be redone from the ground up.
- Leaning over desks/railings (Archived)
    Have the player body bend at the waist to allow leaning over furniture.
- Focusing on keypads brings the camera *way* too close (Archived)
    > Prey places the zoom camera so the screen fills the whole view. In the headset that view is ~120° and square, so a keypad landed about 12 cm from the eyes. Interface's Screen Distance (30 cm) moves the camera back along its view, never past where you stood. PreyVR-94, worn OK 2026-10-26. See docs/KEYPAD-CAMERA-SCOPE-2026-10-26.md.
- Have pistols work with two-handed aiming
    > Prey's own animation cups the pistol's and the Disruptor's grip with the left hand, 9 cm from the right wrist. Squeezing the left grip beside or under the right controller now snaps the drawn hand into that cup; the right hand alone aims, and moving the left hand 30 cm away lets go. Steering from a line that short would be too twitchy. PreyVR-100, worn OK 2026-10-28.
- Snap turning unsnaps the support hand
    > Each snap turn changed the tracking reference, which reset the hold. The hold now resets only on a recenter. PreyVR-100, worn OK 2026-10-28.
- Flashlight should be aimable
    > The flashlight hung off Prey's eye joint, which VR never moves, so it pointed level along the body. It and the psychoscope's light now sit on the headset's eye at Prey's mount (17 mm behind and 35 mm below the eye) and point where you look. Always on. PreyVR-102, worn OK 2026-10-29. See docs/FLASHLIGHT-HEAD-SCOPE-2026-10-29.md.
- Arms can clip into the player model
    > The elbow chooser didn't know the body was there: with the hands at the chest it could put the elbow 12–15 cm inside it (measured on PreyVR-103 against capsules fitted to the drawn torso). It now prefers elbows outside the body, and the drawn elbow goes round the body to them, never through it. PreyVR-104, worn OK 2026-10-29. See docs/ARM-TORSO-SCOPE-2026-10-29.md.
- Arm IK should be biased to stay downwards
    They tend to "chicken-wing" when I rotate my hands
    > Two causes. The elbow followed the hand's sideways bend too closely, so any mismatch between the hand it assumed on the controller and your real one swung the elbow round as you rolled your hand. And the drawn shoulder was 1.2 cm narrower than Morgan's, so a hanging arm counted as inside his chest and the elbow was pushed out. The elbow priors are now 45 −8 30 15 (were 45 −8 12 25) and the shoulder is 197 mm out (was 185). Worn OK 2026-11-07 with `arm.prior` and `arm.shoulder`, now the defaults. See docs/ARM-POSE-2026-10-06.md, "2026-11-07: the elbow kept down".
- Body IK seems to cause the body to twist a bit when leaning down
    > Not the IK, and not the zero-G work. Within 5° of straight down the play space's yaw went wrong, off by how far the head's heading had moved, which turned the chest, hands and aim about the head until the head came back out of that range. Bending over is what gets a head that far down. The yaw is now read from the view instead of worked back from the camera. PreyVR-118, worn OK 2026-10-31. See docs/BODY-FOLLOW.md, the four yaws.
- Remove Q-Beam equip animation (?)
    Maybe all weapon draw animations should be disabled?
    Wrench draw animation is already disabled.
    Add an option to toggle all of them?
    > Every weapon now comes out and goes away at once instead of playing Prey's draw and stow animations, and each still makes its own sounds. Swaps are instant as a result: those animations took about 1 to 2 seconds between them. Equipment's Weapon Draw Animations brings them back, along with the wrench's hand spin. PreyVR-126, worn OK 2026-10-31. See docs/WEAPON-DRAW-SCOPE-2026-10-31.md.
- Aim lasers should come out of my pointer fingers
    This way it will handle like the weapon aiming
    > The lasers started at the controller's aim pose, off the hand, and the empty right hand's wasn't a true ray: it ended where a ray from the eye landed. They now leave the index fingertip along the controller's aim, and the empty hand's is a true ray, as a gun's is from its muzzle. That left the finger 24° below the laser, so the hands moved instead: they lean 39° on the controllers rather than 15°, which lays the finger within 3.5° of the aim, and an empty hand aims where a gun does. PreyVR-128, worn OK 2026-11-01. See docs/FINGER-LASER-SCOPE-2026-11-01.md.
- Auto-break two-hand grip option
    To complement the auto-snap
    > Hands' Auto-Break Support Hand lets any hold go once the left hand moves Auto-Break Distance from where it attached, on any weapon and however it attached, so toggle grip needs no second press. It is the only thing that ends a hold by distance: with it off, nothing does. That retired the pistol's fixed 30 cm release, and Auto-Snap alone no longer lets go by distance. PreyVR-130, worn OK 2026-11-02.
- Going through loading screens can cause the hands to not be out
    Affects the space helmet too.
    > A load replaces the player's arms with a new character. The IK held on to the old one and skipped every other character, the new arms included, until the weapon count changed. With a weapon out, Prey re-equips it as the load ends, which fixed it by itself. Empty-handed, nothing did until the next draw: 11 of the 12 such loads in the logs. The helmet and head lamp asked the IK for the player's character, so they went with it. The IK now always checks the player's current arms and rebinds when they change, and the helmet and head lamp read the player directly. PreyVR-138, worn OK 2026-11-07. See docs/LOAD-IK-OWNER-SCOPE-2026-11-07.md.
- Can't initiate reload when two-handing the shotgun or Q-beam
    > Any two-hand hold, on every weapon, dropped both reload presses as busy: a rule left over from the original layout, where the right grip was also the use button. X now reloads while the left hand holds the weapon (the selection is emptied first, so it never uses what the left hand points at), and the right grip reloads and holsters as it does one-handed. PreyVR-140, worn OK 2026-11-07.

### Rendering
- The starfield skybox displays at the wrong depth (?) or something to that effect (gives double vision). (Archived)
- The player body renders over the top of everything
    > Prey drew the body, arms and weapon in its near pass, which is always in front of the world. PreyVR-91/92 drew the body in the world instead, which left the stun gun's charge sparks showing through the gun and its charge meter frozen. Since PreyVR-101 the body stays in the near pass and that pass gets the world's depth (`body.depth 1`; the old way is `body.depth 2`), so the weapon's effects, its display and the PDA stay with it. Setup's Near Clip Distance (3 cm) stops the view cutting into the body when you look down. Worn OK 2026-10-25 and 2026-10-28. See docs/BODY-FOLLOW.md, "How the body is drawn".
- Temporal anti-aliasing
- Temporal AA seems to have a strange astigmatism; one eye (right) is sharper than the other
    > One eye was reading temporal history from the wrong place on animated models such as the recycler and airlocks. The mod draws one scene twice, and Prey builds each object's motion data once, for the first eye, so the second eye treated the gap between the eyes as motion. PreyVR-113 gave each eye its own previous view and fixed first-person objects, which moved the fault on world objects from the left eye to the right. PreyVR-121 corrects the right eye. Worn OK 2026-10-31. See docs/TEMPORAL-AA-SCOPE-2026-10-30.md.

### Technical Debt
- Restructuring
    > `src/` and `tests/` are grouped by feature, with each header beside its source and `include/` gone. The compiled code matches PreyVR-123 byte for byte. PreyVR-124, smoke tested OK 2026-10-31.

---

Feature set tracking list, in broad terms:
- Body Follow and Roomscale (**IMPLEMENTED**)
    Having the player character model follow the headset and camera like a real body would follow someone's head while still allowing for things like physical leaning and crouching. Body depth rendering is also grouped in here.
- Physical Interaction (**PARTIALLY IMPLEMENTED**)
    Player interaction with objects in the world. Encompasses touching, pushing and throwing stuff along with hitting things with the wrench.
- Stereoscopic Rendering (**IMPLEMENTED**)
    Using the dormant stereoscopic rendering code in Prey's CryEngine fork. Cutscenes, fades and UI elements need special handling to display in both eyes correctly.
- Temporal Anti-Aliasing (**IMPLEMENTED**)
- VR Options Menu (**IN PROGRESS**)
- Engine and Mod Stability (**IN PROGRESS**)
- Mod Launcher (**TODO**)
    A small window to configure launch settings and run the game with the VR mod enabled.
- Physical Weapon Interactions (**TODO**)
- VR Compatible Controls (**IMPLEMENTED**)
    VR handsets working as similar to gamepads where possible.
- HUD/UI (**PARTIALLY IMPLEMENTED**)
    Having UI and HUD elements display nicely and at the proper depth.
- Compatibility (**TODO**)
- ...
