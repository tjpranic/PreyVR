# Controller prompts in game menus — 18 October 2026

Two headset reports had one cause. The inventory showed PC prompts instead of
gamepad prompts. Y opened an item's menu for a moment, then closed it. Both
came from the menu laser, which Prey treats as mouse input.

Target: Steam `PreyDll.dll`, SHA-256 `7d6e322f…05311a7`. All addresses are
RVAs. They were read from a decompile of the analysed local Ghidra project
(`/Prey/PreyDll.dll`), not from a live process.

## Mechanism

- **The laser selects mouse mode on purpose.** `UiPointer.cpp` posts a zero
  `maxis_x` before every hover move while a menu is open
  (`RE-UI-POINTER-2026-09-10.md`, candidate 05). Hand jitter makes that
  nearly every frame.
- **Last-input-device listener `0x182D3A0`.** A keyboard or mouse event
  (device 0/1) broadcasts scheme 2 through `0x182D7D0`. It skips the broadcast
  when `[gGameCVars]+0x924` is set, but records 2 as the last device either
  way. XInput (device 3) broadcasts 3, or 4 when
  `+0x920` (`hud_playstationContext`) is set.
- **PDA listener `0x1639E00`.**
  - Sets element flag 1 (`hasCursor`) only for scheme 2.
  - Sends `SetControlScheme` and `setNavigationPrompts` to DaniellePDA.
  - Rebuilds the current page's prompts (`0x163C9F0`). For the inventory that
    calls `0x164E070`, then `0x164E980`, the prompt bar whose glyphs follow
    the scheme.
  - Mouse-event flag 2, which native dispatch `0x2CEF30` checks, is not
    touched.
- **Item menu, PC route.** Flash `inventoryRightClick` → `0x162D4E0` → page
  `+0x28` interface (vtable `0x1E673B8`) slot +0x40, `0x164BFA0` → `0x164C550`
  (`inventoryPopUpMenuOpen`).
  - Hardware mouse `0x2CFCC0` maps event 4/6 to right down and 5 to right up.
  - `HudDispatchPointer` accepts only 0–2, so the laser cannot right-click.
- **Item menu, gamepad route.** Page action handler `0x164CE30`:
  - `menu_examine` (`xi_y`, press) opens the popup when an item is
    highlighted, nothing is dragged and an option exists (`0x164E0C0`).
  - `menu_confirm` (A) picks up or places, and `menu_use` (X) uses.
  - The hover prompt `@ui_Menu` is bound to `menu_examine` (`0x164EDD0`).
- **Why the popup closed.**
  - Y switched the scheme to 3 and opened the popup. The laser's next move
    switched it back to 2.
  - The C++ side of a scheme change does not close the popup.
  - Two native paths do. One is the movie's own `SetControlScheme` handling,
    which is in the encrypted paks and not readable. The other is highlight
    handler `0x162D600`, which calls slot +0x38 `0x164C0D0`; that closes an
    open popup when the highlighted item changes.

## Prey's own setting

`hud_forceControllerGlyphs` is registered in `0x17180E0` at
`gGameCVars+0x924`, default 0, flags 0, with the help text *"If this is set
then we won't switch to keyboard glyphs/prompts when we receive keyboard
input."* Its change callback `0x1726630` moves a scheme of 1 or 2 to 3, or 5
with a Steam controller, as it is set. Flags 0 means it is not saved.

## Implementation

- `ControllerPrompts.cpp` sets it through the verified console path,
  `QueueVerifiedRendererSetting` (readback `GetIVal`). That path is shared with
  VR start and the focus hold on the command worker.
- VR start sets it to 1. VR Options' Interface > Controller Prompts saves the
  choice (`controller_prompts`, default on), and a saved choice wins.
- VR stop puts 0 back, but only if the lane set it or may have.
- `ui.prompts <0|1>` sets it for the session. The minute status line carries
  `prompts={…}`.
- The laser still posts its mouse axis. With the cvar set, that only records
  the last device.
- The console allowlist gained this one name. `hud_playstationContext` and
  other `hud_` cvars stay denied.

## Evidence and acceptance

Offline: build `build/headless` Release, **79/79** ctest. That count includes
the new `controller_prompts` lane test and the VR Options checks.

Not established. No run has ever kept the laser on a menu outside mouse mode
(every recorded pointer run had the mode at 2). Check in the headset:

1. Log `preyvr_prompts force_controller_glyphs=1 result=0 detail=verified_readback`
   after VR start.
2. Inventory prompts show controller buttons with the laser on the panel.
3. Laser hover highlights items, and the trigger still drags and drops.
4. Y on a highlighted item opens its menu, and it stays open while the laser
   moves over it.
5. A menu option can be chosen: by laser click, or by stick and A. B closes
   the menu.
6. Pause and main menus still respond to laser clicks.
7. Controller Prompts off returns PC prompts with no restart (`ui.prompts 0`).

**Headset, 2026-10-18 (PreyVR-18):** the inventory works as expected (prompts,
laser, Y item menu). Item 6 fails: the laser does nothing on the main menu or
the pause menu with Controller Prompts on. Earmarked for later. Those movies
ignore the mouse outside scheme 2, so a later fix must give the shell menus
mouse mode while keeping the inventory on the gamepad scheme.

If 3 or 6 fails, the movie ignores the mouse outside scheme 2. The fallback is
to keep mouse mode and give the laser a right-click (event 4/5 through the same
`0x2CFCC0` entry).
