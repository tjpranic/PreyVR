# TODO

VR Options Menu:
- Menu spawns in the same place the game menus do causing Z-fighting
- Reorganize the options on the menu under each tab into sections
- Audit every option and rework and/or remove and/or consolidate as many as needed

Rendering:
- Foveated rendering. Figure out if this is possible?

Controls:
- Redo controls to be nicer
- Individual finger control support
- Fix duplicated bindings

Interactibility:
- Make melee truly physical
- Allow for hand-based interactions with objects in game (nicer grabbing, smacking things around)

UX:
- Game freezes a bit when first loading before showing the main menu in VR
- Desktop game window spawns halfway off-desktop
- Lasers freezes on load
- Make it run minimized on the desktop

Bugs:
- Snap turn causes a rendering artifact for a split second. It looks like the view is lagging by a frame.
- Access violation that causes a crash. Seems to be due to a use-after-free camera related bug. Have got instrumentation in place to catch it.
- Hands don't move with the controller when no weapon is out.