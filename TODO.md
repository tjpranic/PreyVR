# PreyVR

## TODO

### Bugs
- The handheld device (PDA) thing renders over the top of the hand
- Reticle drifts from the actual aim point sometimes
    Happens after examining a monitor or keypad.

### Rendering
- DLSS?
- Upscaling?
- Foveated rendering?

### UX/Immersion
- Rotate to face direction (?)
    Add in an option to rotate the camera in the direction the right hand is facing. Alternatively, having the player body and camera follow the left/right hand when holding a certain button?
- Two-handed wrench (?)
    Add in a two-handed wrench mode that automatically triggers heavy wrench attacks. One handed mode would always be a light attack. This would replace the speed based light/heavy attack system.
- Two-handed carrying (?)
- Physical touch screen controls for computer monitors and keypads (?)
- The wrench tends to hit around the hand area instead of the head of the wrench
- Flashlight should be aimable
    With either a hand or the head.
- Adjustable wrench bounding box
    Add in an adjustable wrench bounding box lengthening system that only registers hits against enemies. Essentially, an additional bounding box would be added to the wrench, but it would be longer and only used to find collisions with enemies. This would preserve existing correct physical interaction between wrench/hands and objects, but allow for smoother feeling combat.
- Immersive reloading
    Right now it just plays the animation. It would be better to allow the player to physically attach and detach weapon ammunition.
- Better throwing
    Add an option to impart extra force to objects that are physically thrown.
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
- Arms can clip into the player model
- Recenter player body when the player stops moving?

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

### HUD
- Psi energy shows on wrist before installing first neuromod
- Some UI elements still only display at the periphery of vision
    It makes them hard to see. They need to be moved a little closer in.
- The hand lasers start outside of player hands
    They seem to begin out of nowhere. Something like a fade in along the length (like when the laser fades out in menus when it's not hitting a surface) would make them look a bit nicer.
    The lasers also render over the top of everything.

### VR Options Menu
- Have bindings section let you change your bindings
- Group related options in the tabs into sections
- Audit every option and rework and/or remove and/or consolidate as many as needed
    Pre-audit findings:
    - The arms length option doesn't affect the whole arm, just the hands.
- Development tab
    This tab will be used to run input tuning commands because sending commands through the "Tune Prey VR.cmd" script is way too annoying to use properly.

## IN PROGRESS

### Engine Instability
- Use-after-free camera bug
    Causes an access violation that then crashes the game. Have got instrumentation in place to catch it. See CameraCullWatch.cpp.
- Very rare render freeze
    The camera doesn't render any new frame for a short time. Not related to the use-after-free camera bug. Instrumentation in place: FrameProbe.cpp.
- Camera flashing bug
    A weird bug that causes certain parts of the world to flash strangely. Might be a culling issue. A fix + instrumentation is in place.
- Texture view use-after-free bug
    Instrumentation in place. See ResourceSetWatch.cpp.
- Phantom crash
    Didn't leave an error dump since it crashed before the process could write one.

## DONE

### Controls
- The movement stoppage bug (Archived)
    > Prey re-read the stick from the physical gamepad whenever an action filter changed (mostly the interaction prompt as the view swept over usable objects) and zeroed what the mod had posted. Fixed in PreyVR-72, worn OK 2026-10-24. See docs/MOVE-STOP-2026-10-24.md.
- The X to transfer junk in the recycler doesn't work (Archived)

### UI
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

### Rendering
- The starfield skybox displays at the wrong depth (?) or something to that effect (gives double vision). (Archived)
- The player body renders over the top of everything
    > Prey drew the body, arms and weapon in its near pass, which is always in front of the world. It is now drawn in the world (`body.depth`), and Setup's Near Clip Distance (3 cm) stops the view cutting into it when you look down. PreyVR-92, worn OK 2026-10-25. See docs/BODY-FOLLOW.md, "How the body is drawn".

---

Feature set tracking list, in broad terms:
- Body follow and roomscale (**IMPLEMENTED**)
    Having the player character model follow the headset and camera like a real body would follow someone's head while still allowing for things like physical leaning and crouching.
- Physical interaction (**PARTIALLY IMPLEMENTED**)
    Player interaction with objects in the world. Encompasses touching, pushing and throwing stuff along with hitting things with the wrench.
- Stereoscopic rendering (**IMPLEMENTED**)
    Using the dormant stereoscopic rendering code in Prey's CryEngine fork. Cutscenes, fades and UI elements need special handling to display in both eyes correctly.
- VR Options Menu (**IN PROGRESS**)
- ...