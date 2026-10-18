# TODO

Interactibility:
- Make melee truly physical
- Fine tune wrench combat
- Fine dextrous finger based interactions
- Add functionality to allow me to rotate objects I'm carrying
- Have weapons and hands stop against surfaces

UI:
- Wrist stats bar shows Psi energy before you install a neuromod
- Lasers don't work on the main menu or pause menu with Controller Prompts on, but they do work in the inventory for some reason.

VR Options Menu:
- Have bindings section let you change your bindings
- Reorganize the options on the menu under each tab into sections
- Audit every option and rework and/or remove and/or consolidate as many as needed

Bugs:
- Access violation that causes a crash. Seems to be due to a use-after-free camera related bug. Have got instrumentation in place to catch it. See CameraCullWatch.cpp.
- The movement stop on turn issue still exists, not as bad as it did before, but it still needs to be addressed
- Physically leaning is a bit wonky, it seems that the game's collision detection happens against the player's character model and causes the player to be move back a bit to resolve the contact
- Arm IK tends to bend the arms inward. Not enough space between shoulder and wrist, I think. Solution: longer arms? Player to character model scaling adjustments?
- Controller Prompts option doesn't seem to apply at startup, but only sometimes? I think the laser is resetting the option to off? Not sure.
    This is due to the game's naturally poor Steam Input fighting with the different input types. Disabling Steam Input seems to fix it.