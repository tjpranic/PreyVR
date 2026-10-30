# PreyVR

## TODO

### Rendering
- DLSS (?)
- Upscaling (?)
- Foveated rendering (?)

### UX/Immersion
- Rotate to face direction (?)
    Add in an option to rotate the camera in the direction the right hand is facing. Alternatively, having the player body and camera follow the left/right hand when holding a certain button?
- Recenter player body when the player stops moving (?)
- Two-handed wrench (?)
    Add in a two-handed wrench mode that automatically triggers heavy wrench attacks. One handed mode would always be a light attack. This would replace the speed based light/heavy attack system.
- Two-handed carrying (?)
- Physical touch screen controls for computer monitors and keypads (?)
- The wrench tends to hit around the hand area instead of the head of the wrench
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
- Optional body model rendering
    Have it only render the hands like in Half Life Alyx?
- The Earth and Moon are very obviously rendered way too close
    Having them rendered at a more realisitic depth would be nice but challenging.
    Technically everything is working game wise, it would just be an improvement.
- Zero-G doesn't have rotate controls
- Zero-G helmet overlay doesn't render properly
    It doesn't follow the camera view

### Technical Debt
- Refactoring, simplification and consolidation
    Low-hanging fruit like dead code removal, renaming and de-duplication should be prioritised for the first pass. Next should be low-risk refactors that involve refactoring code that can verified with just tests and no VR headset runs.
    Code should also be reorganized and restructured to be more understandable, where possible.
    One thing I'm particularly concerned about is 1. bad abstractions and 2. logic that should be abstracted but isn't. A lot of hyper-focused work has been done on various issues that may leave similar logic unconnected and ungeneralized when they ought to be.
- Restructuring
    Related source files should be grouped with directories.
    Structure by feature set? (UI, In-Game Controls, Debug/Crash Instrumentation, VR Body and Motion Logic, Engine Hooking, Physics, etc.).
- Optimization
    The mod is starting to lag a bit due to all the feature work. Two types of optimizations are needed. Big O optimization of code and actual targeted optimization of bottlenecks (this will require instrumentation). Will need to look for quick wins on both, if any.
- Tuning parameter cleanup
    One set of functionality that's going to need to be refactored out at the end of the project is all the tuning parameters. If they can be made debug build only, then that is preferred, otherwise they should be removed.
    Some tuning options literally just gate bug fixes behind commands. Absolutely no reason for this, they need to be identified and removed.
- Instrumentation cleanup
    Removing (or gating behind a debug only build) of all the console logging and bug-finding instrumentation needs to be done at some point.
- Documentation cleanup
    Feature work has been tracked across multiple documents. These should be summarized and combined into one document.

### HUD/UI
- Psi energy shows on wrist before installing first neuromod
- Some UI elements still only display at the periphery of vision
    It makes them hard to see. They need to be moved a little closer in.
- Left hand laser doesn't hit at surface of objects unlike the right hand aim laser
    Only sometimes? This might be intentional? Will need to investigate.
- Disable aim laser on loading screen

### VR Options Menu
- Have bindings section let you change your bindings
- Group related options in the tabs into sections
- Audit every option and rework and/or remove and/or consolidate as many as needed
    Audit findings:
    - Comfort should be renamed to Movement
    - The arms length option doesn't affect the whole arm, just the forearm
    - Having the support hand option set to Free is pointless as far as I can tell, it should be removed and Snapped should be the default
    - Hands Without a Weapon should be removed and always on
    - Need to add an option to auto-snap when in range
    - Aim lasers causes a ray to be drawn when firing a weapon
    - Can't grab when two-handing weapons
        Grabs are only allowed with the left hand, will need to rethink how this works
    - Setup tab should be first
    - ...
- Development tab
    This tab will be used to run input tuning commands because sending commands through the "Tune Prey VR.cmd" script is way too annoying to use properly.

## IN PROGRESS

### Engine Instability
- Very rare render freeze
    > The camera doesn't render any new frame for a short time. Not related to the use-after-free camera bug. Instrumentation in place: FrameProbe.cpp, now a flight recorder that opens on the stall itself and sums up where each thread waited (PreyVR-98). See docs/ENGINE-INSTABILITY.md.
- Phantom crash
    Didn't leave an error dump since it crashed before the process could write one.
    > A stack overflow: Steam recorded exit code 0xC00000FD. OverflowCapture.cpp now writes a dump and a report that survive one, and the launcher keeps each session's Game.log (PreyVR-98). Waiting for it to happen again. See docs/ENGINE-INSTABILITY.md.

## DONE

### Engine Instability
- Use-after-free camera bug (Pending recurrence)
    > A pause-menu close could leave the culler a frame behind for the rest of the session. The cull guard (CullPrepareGuard.cpp) pairs that frame. No sign of the lag in 61 runs, so the watch was removed on 2026-10-27; the guard stays. See docs/ENGINE-INSTABILITY.md.
- Camera flashing bug (Pending recurrence)
    > The same cull lag, fixed by the same guard.
- Texture view use-after-free bug (Pending recurrence)
    > Did not recur in 41 runs. The watch was removed on 2026-10-27. See docs/ENGINE-INSTABILITY.md for what to build if it comes back.

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

### UX/Immersion
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

### Rendering
- The starfield skybox displays at the wrong depth (?) or something to that effect (gives double vision). (Archived)
- The player body renders over the top of everything
    > Prey drew the body, arms and weapon in its near pass, which is always in front of the world. PreyVR-91/92 drew the body in the world instead, which left the stun gun's charge sparks showing through the gun and its charge meter frozen. Since PreyVR-101 the body stays in the near pass and that pass gets the world's depth (`body.depth 1`; the old way is `body.depth 2`), so the weapon's effects, its display and the PDA stay with it. Setup's Near Clip Distance (3 cm) stops the view cutting into the body when you look down. Worn OK 2026-10-25 and 2026-10-28. See docs/BODY-FOLLOW.md, "How the body is drawn".
- Temporal anti-aliasing

---

Feature set tracking list, in broad terms:
- Body follow and roomscale (**IMPLEMENTED**)
    Having the player character model follow the headset and camera like a real body would follow someone's head while still allowing for things like physical leaning and crouching. Body depth rendering is also grouped in here.
- Physical interaction (**PARTIALLY IMPLEMENTED**)
    Player interaction with objects in the world. Encompasses touching, pushing and throwing stuff along with hitting things with the wrench.
- Stereoscopic rendering (**IMPLEMENTED**)
    Using the dormant stereoscopic rendering code in Prey's CryEngine fork. Cutscenes, fades and UI elements need special handling to display in both eyes correctly.
- VR Options Menu (**IN PROGRESS**)
- Engine Instability Bugs (**IN PROGRESS**)
- ...
