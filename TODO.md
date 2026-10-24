# PreyVR

## TODO

### Controls
- The X to transfer junk in the recycler doesn't work

### Bugs
- The starfield skybox displays at the wrong depth (?) or something to that effect (gives double vision).

### UX/Game Feel
- Rotate to face direction
    Add in an option to rotate the camera in the direction the right hand is facing. Alternatively, having the player body and camera follow the left/right hand when holding a certain button?
- Adjustable wrench bounding box
    Add in an adjustable wrench bounding box lengthening system that only registers hits against enemies. Essentially, an additional bounding box would be added to the wrench, but it would be longer and only used to find collisions with enemies. This would preserve existing correct physical interaction between wrench/hands and objects, but allow for smoother feeling combat.
- Two-handed wrench
    Add in a two-handed wrench mode that automatically triggers heavy wrench attacks. One handed mode would always be a light attack. This would replace the speed based light/heavy attack system.
- Immersive reloading
    Right now it just plays the animation. It would be better to allow the player to physically attach and detach weapon ammunition.
- The wrench tends to hit around the hand area instead of the head of the wrench
- Focusing on keypads brings the camera *way* too close
- Better throwing
    Add an option to impart extra force to objects that are physically thrown.
    Maybe an option to point with left hand where you want the grenade to go?
- Flashlight should be aimable with left hand
- Wrench hits use too much stamina
    I think swings *back* away from the target may be getting counted. Maybe add a very short cooldown after a hit connects?

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
- Instrumentation cleanup
    Removing (or gating behind a debug only build) of all the console logging and bug-finding instrumentation needs to be done at some point.
- Documentation cleanup
    Feature work has been tracked across multiple documents. These should be combined into one document.

### Immersion
- Fine dextrous finger controls
    Definitely want this for the glass button pressing jumpscare.
- Stopping hand -> object interactions from pushing objects through other objects
    e.g. Pressing down on a mug on a desk can push the mug through the desk
- Leaning over desks/railings
    Have the player body bend at the waist to allow leaning over furniture
- Optional body rendering
    An option to not render the body and only render the hands like in Half Life Alyx.
    Or an overhaul to polish it up fully.
    It also still feels like the shoulders are attached to the head and not the torso, will need to double check this.
- Physical crouching
    Crouching in real life should enable crouch mode. Standing up should disable crouch mode.

### HUD
- Psi energy shows on wrist before installing first neuromod
- Some UI elements still only display at the periphery of vision
    It makes them hard to see. They need to be moved a little closer in.
- The hand lasers start outside of player hands
    They seem to begin out of nowhere. Something like a fade in along the length (like when the laser fades out in menus when it's not hitting a surface) would make them look a bit nicer.
    The lasers also render over the top of everything.
- The neuromod cutscene that plays when you first use a neuromod only appears in the right eye
- Reticle drifts from the actual aim point sometimes
    Seems to happen after reloading?
- Interacting with computers and screen is off
    Depth/pointer offset issues

### VR Options Menu
- Have bindings section let you change your bindings
- Group related options in the tabs into sections
- Audit every option and rework and/or remove and/or consolidate as many as needed
- In game console
    In the VR Options menu, add a tab to show a console that allows me to enter tuning commands. It should also write the output to a log somewhere so agents can read it. Then remove the tuning command prompt stuff.

## IN PROGRESS

### Bugs
- Use-after-free camera bug
    Causes an access violation that then crashes the game. Have got instrumentation in place to catch it. See CameraCullWatch.cpp.
- Very rare render freeze
    The camera doesn't render any new frame for a short time. Not related to the use-after-free camera bug. Instrumentation in place: FrameProbe.cpp.
- Camera flashing bug
    A weird bug that causes certain parts of the world to flash strangely. Might be a culling issue. A fix + instrumentation is in place.
- Texture view use-after-free bug
    Instrumentation in place. See ResourceSetWatch.cpp.

## DONE

### Controls
- The movement stoppage bug
    > Prey re-read the stick from the physical gamepad whenever an action filter changed (mostly the interaction prompt as the view swept over usable objects) and zeroed what the mod had posted. Fixed in PreyVR-72, worn OK 2026-10-24. See docs/MOVE-STOP-2026-10-24.md.

### UX/Game Feel
- Shot from muzzle laser shows skewed
    The game naturally skews the aim laser when it comes from the muzzle. It should be straightened.

### UI
- Controller Prompts option doesn't seem to apply sometimes
- The game doesn't render loading screens
    It just shows a black screen instead.

### Immersion
- Objective markers don't display at the proper depth
    They need to be at the depth they appear at in the world. Repair markers (and possibly enemy detection markers) also affected.
- Stun gun max charge effect at the wrong depth
    Q-Beam has a similar issue
- Weapons and hands don't stop against surfaces
    They now stop at fixed scenery and at heavy (Heavy Objects setting, 10 kg) or immovable loose objects, which slide when pressed. See docs/SURFACE-STOP-SCOPE-2026-10-21.md.
- Hand animation plays when taking the wrench out
    It causes the right hand to spin. The animation should not play.

### Development