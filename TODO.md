# TODO

## Known Bugs:
- Access violation that causes a crash. Seems to be due to a use-after-free camera related bug. Have got instrumentation in place to catch it. See CameraCullWatch.cpp.
- The hand animation plays when taking the wrench out and it causes the right hand to spin. The animation should not play.
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
- Have the player body bend at the waist so I can lean over desks/railings
- An option to not render the body and only render the hands like in Half Life Alyx?
- Physical crouching?

## UI:
- Controller Prompts option doesn't seem to apply sometimes. This is partly due to the game's naturally poor Steam Input implementation fighting with the different input types. Disabling Steam Input seems to help, but this issue is a WAY more annoying than I thought and a proper fix should be sought.