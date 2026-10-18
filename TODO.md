# TODO

Interactibility:
- Make melee truly physical
- Fine dextrous finger based interactions
- Add functionality to allow me to rotate objects I'm carrying

UI:
- Wrist stats bar shows Psi energy before you install a neuromod
- Lasers don't work on the main menu or pause menu with Controller Prompts on, but they do work in the inventory for some reason.

VR Options Menu:
- Reorganize the options on the menu under each tab into sections
- Audit every option and rework and/or remove and/or consolidate as many as needed

Bugs:
- Access violation that causes a crash. Seems to be due to a use-after-free camera related bug. Have got instrumentation in place to catch it. See CameraCullWatch.cpp.
- The movement stop on turn issue still exists, not as bad as it did before, but it still needs to be addressed
- Physically leaning is a bit wonky, it seems that the game's collision detection happens against the player's character model and causes the player to be move back a bit to resolve the contact
- Arm IK tends to bend the arms inward. Not enough space between shoulder and wrist, maybe?