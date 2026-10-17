# TODO

VR Options Menu:
- Reorganize the options on the menu under each tab into sections
- Audit every option and rework and/or remove and/or consolidate as many as needed

Rendering:
- Foveated rendering. Figure out if this is possible?
- Arm IK tends to bend the arms inward. Not enough space between shoulder and wrist, maybe?

Controls:
- Individual finger control support
- Pressing B to exit the inventory results in the player crouching. Technically correct behavior given B is the crouch button but should still be looked at. Maybe not have B leave menus?

Interactibility:
- Make melee truly physical
- Allow for hand-based interactions with objects in game (nicer grabbing, smacking things around)

UI:
- Wrist stats bar shows Psi energy before you install a neuromod

UX:
- Game freezes a bit when first loading before showing the main menu in VR

Bugs:
- Access violation that causes a crash. Seems to be due to a use-after-free camera related bug. Have got instrumentation in place to catch it.
- Hands don't move with the controller when no weapon is out.