# PreyVR

## TODO

### UX/Game Feel
- Rotate to face direction
    Add in an option to rotate the camera in the direction the right hand is facing
- Adjustable wrench bounding box
    Add in an adjustable wrench bounding box lengthening system that only registers hits against enemies. Essentially, an additional bounding box would be added to the wrench, but it would be longer and only used to find collisions with enemies. This would preserve existing correct physical interaction between wrench/hands and objects, but allow for smoother feeling combat.
- Two-handed wrench
    Add in a two-handed wrench mode that automatically triggers heavy wrench attacks. One handed mode would always be a light attack. This would replace the speed based light/heavy attack system.
- Immersive reloading
    Right now it just plays the animation. It would be better to allow the player to physically attach and detach weapon ammunition.

### Technical Debt
- Refactoring, simplification and consolidation
    Low-hanging fruit like dead code removal, renaming and de-duplication should be prioritised for the first pass. Next should be low-risk refactors that involve refactoring code that can verified with just tests and no VR headset runs.
    Code should also be reorganized and restructured to be more understandable, where possible.
    One thing I'm particularly concerned about is 1. bad abstractions and 2. logic that should be abstracted but isn't. A lot of hyper-focused work has been done on various issues that may leave similar logic unconnected and ungeneralized when they ought to be.
- Tuning parameter cleanup
    One set of functionality that's going to need to be refactored out at the end of the project is all the tuning parameters. If they can be made debug build only, then that is preferred, otherwise they should be removed.
- Instrumentation cleanup
    Removing (or gating behind a debug only build) of all the console logging and bug-finding instrumentation needs to be done at some point.

### Immersion
- Fine dextrous finger controls
- Rotatable carried objects
- Weapons and hands don't stop against surfaces
- Leaning over desks/railings
    Have the player body bend at the waist to allow leaning over furniture
- Optional body rendering
    An option to not render the body and only render the hands like in Half Life Alyx
- Physical crouching
    Crouching in real life should enable crouch mode. Standing up should disable crouch mode.

### HUD
- Psi energy shows on wrist before installing first neuromod
- Some UI elements still only display at the periphery of vision
    It makes them hard to see. They need to be moved a little closer in.
- Objective markers don't display at the proper depth
    They need to be at the depth they appear at in the world. Repair markers (and possibly enemy detection markers) also affected.
- The hand lasers start outside of player hands
    They seem to begin out of nowhere. Something like a fade in along the length (like when the laser fades out in menus when it's not hitting a surface) would make them look a bit nicer.

### VR Options Menu
- Have bindings section let you change your bindings
- Group related options in the tabs into sections
- Audit every option and rework and/or remove and/or consolidate as many as needed

## IN PROGRESS

### Bugs
- Use-after-free camera bug
    Causes an access violation that then crashes the game. Have got instrumentation in place to catch it. See CameraCullWatch.cpp.
- Very rare render freeze
    The camera doesn't render any new frame for a short time. Not related to the use-after-free camera bug. Instrumentation in place: FrameProbe.cpp.

## DONE

### UI
- Controller Prompts option doesn't seem to apply sometimes
- The game doesn't render loading screens
    It just shows a black screen instead.

### Immersion
- Hand animation plays when taking the wrench out
    It causes the right hand to spin. The animation should not play.