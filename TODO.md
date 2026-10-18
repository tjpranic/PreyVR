# TODO

VR Options Menu:
- Reorganize the options on the menu under each tab into sections
- Audit every option and rework and/or remove and/or consolidate as many as needed

Rendering:
- Arm IK tends to bend the arms inward. Not enough space between shoulder and wrist, maybe?

Interactibility:
- Make melee truly physical
- Allow for hand-based interactions with objects in game (nicer grabbing, smacking things around)
- Add functionality to allow me to rotate objects I'm carrying

UI:
- Wrist stats bar shows Psi energy before you install a neuromod
- Lasers don't work on the main menu or pause menu with Controller Prompts on, but they do work in the inventory for some reason.

Bugs:
- Access violation that causes a crash. Seems to be due to a use-after-free camera related bug. Have got instrumentation in place to catch it. See CameraCullWatch.cpp.
- The movement stop on turn issue still exists, not as bad as it did before, but it still needs to be addressed