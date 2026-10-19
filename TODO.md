# TODO

## Known Bugs:
- Access violation that causes a crash. Seems to be due to a use-after-free camera related bug. Have got instrumentation in place to catch it. See CameraCullWatch.cpp.
- The movement stop on turn issue still exists, not as bad as it did before, but it still needs to be addressed
- Physically leaning is a bit wonky, it seems that the game's collision detection happens against the player's character model and causes the player to be move back a bit to resolve the contact
- The hand animation plays when taking the wrench out and it causes the right hand to spin. It should be removed.
- The game doesn't render loading screens, it just shows a black screen instead.

## VR Options Menu:
- Have bindings section let you change your bindings
- Reorganize the options on the menu under each tab into sections
- Audit every option and rework and/or remove and/or consolidate as many as needed

## HUD:
- Wrist stats bar shows Psi energy before you install a neuromod
- Some UI elements still only display at the periphery of my field of vision which makes them hard to see. They need to be moved a little closer in.

## Melee Combat:
- Add in an adjustable wrench bounding box lengthening system that only registers hits against enemies. Essentially, an additional bounding box would be added to the wrench, but it would be longer and only used to find collisions with enemies. This would preserve existing correct physical interaction between wrench/hands and objects, but allow for smoother feeling combat.
- Add in a two-handed wrench mode that automatically triggers heavy wrench attacks. One handed mode would always be a light attack. This would replace the speed based light/heavy attack system.

## Immersion:
- Fine dextrous finger based interactions
- Add functionality to allow me to rotate objects I'm carrying
- Have weapons and hands stop against surfaces

## Polishing:
- The player torso should be anchored to the next such that it moves with the player and the waist should stretch instead. Right now, if I play sitting down and then stand up, the torso doesn't move but the shoulders do move (up) as they are anchored to the neck. This normally wouldn't occur during actual gameplay, but it would be a tiny visual improvement over the current functionality.

## Known issues that I'm not fixing:
- Controller Prompts option doesn't seem to apply sometimes.
    This is due to the game's naturally poor Steam Input implementation fighting with the different input types. Disabling Steam Input seems to fix it.