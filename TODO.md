# PreyVR

## TODO

### Immersion:
- Fine dextrous finger controls
- Rotatable carried objects
- Weapons and hands don't stop against surfaces
- Leaning over desks/railings
    Have the player body bend at the waist to allow leaning over furniture
- Optional body rendering
    An option to not render the body and only render the hands like in Half Life Alyx
- Physical crouching
    Crouching in real life should enable crouch mode. Standing up should disable crouch mode.
- Hand animation plays when taking the wrench out
    It causes the right hand to spin. The animation should not play.

### UI:
- The game doesn't render loading screens
    It just shows a black screen instead.

### HUD:
- Psi energy shows on wrist before installing first neuromod
- Some UI elements still only display at the periphery of vision
    It makes them hard to see. They need to be moved a little closer in.

### VR Options Menu:
- Have bindings section let you change your bindings
- Audit every option and rework and/or remove and/or consolidate as many as needed

### Melee Combat:
- Adjustable wrench bounding box
    Add in an adjustable wrench bounding box lengthening system that only registers hits against enemies. Essentially, an additional bounding box would be added to the wrench, but it would be longer and only used to find collisions with enemies. This would preserve existing correct physical interaction between wrench/hands and objects, but allow for smoother feeling combat.
- Two-handed wrench
    Add in a two-handed wrench mode that automatically triggers heavy wrench attacks. One handed mode would always be a light attack. This would replace the speed based light/heavy attack system.

## IN PROGRESS

### Bugs
- Use-after-free camera bug
    Causes an access violation that then crashes the game. Have got instrumentation in place to catch it. See CameraCullWatch.cpp.
- Very rare render freeze
    The camera doesn't render any new frame for a short time. Not related to the use-after-free camera bug. Instrumentation in place: FrameProbe.cpp.

## DONE

### UI
- Controller Prompts option doesn't seem to apply sometimes