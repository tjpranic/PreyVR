# TODO

VR Options Menu:
- Menu spawns in the same place the game menus do causing Z-fighting
    - Tangentially related, the laser won't go past the plane the VR options menu is on. Not necessarily a bug or anything wrong, but would be nice to have.
- Reorganize the options on the menu under each tab into sections
- Audit every option and rework and/or remove and/or consolidate as many as needed

Rendering:
- Foveated rendering. Figure out if this is possible?
- Arm IK tends to bend the arms inward. Not enough space between shoulder and wrist, maybe?

Controls:
- Rebind controls to be nicer on Steam Frame controllers
- Fix duplicated bindings
- Individual finger control support

Interactibility:
- Make melee truly physical
- Allow for hand-based interactions with objects in game (nicer grabbing, smacking things around)

UI:
Wrist stats bar shows Psi energy before you install a neuromod

UX:
- Game freezes a bit when first loading before showing the main menu in VR

Bugs:
- Access violation that causes a crash. Seems to be due to a use-after-free camera related bug. Have got instrumentation in place to catch it.
- Hands don't move with the controller when no weapon is out.