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
with a Steam controller, as it is set. In the Steam build that is always 5:
see [the scheme guard](#scheme-guard-2026-10-19). Flags 0 means it is not saved.

## Implementation

- `ControllerPrompts.cpp` sets it through the verified console path,
  `QueueVerifiedRendererSetting` (readback `GetIVal`). That path is shared with
  VR start and the focus hold on the command worker.
- VR start sets it to 1. VR Options' Interface > Force Controller Prompts
  (named Controller Prompts before 2026-10-20) saves the
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

## Scheme guard, 2026-10-19

After PreyVR-18 the menus sometimes showed no controller prompts at startup.
Every run had verified the cvar at 1. Turning Steam Input off for Prey in Steam
did not change it.

**Cause (static, Ghidra plus bytes).**

- Input init `0x9D1940` creates Prey's Steam Controller device whenever
  Steamworks starts (`i_useSteamController`), and then creates no XInput
  devices. Game.log here says `Initializing SteamController` with Steam Input
  on and off.
- That device's `+0xD8` is `mov al,1`. The cvar callback asks input vtable
  `+0x1A8` for the first device that answers true, so it always picks scheme
  5 (Steam Controller), never 3.
- In scheme 5, prompt lookup `0x166EB70` asks the Steam device for the
  binding's button (`0x9D0100`). With no Steam Input controller connected
  there is none, and `0x166F0B0` falls back to the text label
  `@input_<key>`.
- The tracker skips mouse events in scheme 5, and with the cvar set it skips
  keyboard events too. Only a posted pad event moves the scheme to 3.
- So the result depends on the scheme at the moment VR start sets the cvar.
  - Scheme 2 (laser, desktop mouse or keyboard) becomes 5: text prompts until
    the first controller button.
  - Scheme 3 (a menu button pressed first) stays 3.
- Toggling the option does nothing. Off leaves 5 alone; on moves only 1 and 2.

**Fix.** `ServiceControllerPromptsFrame` runs once a frame on the main thread,
from the CSystem::Render detour after the input lanes. While the lane's 1 is
applied:

- It reads the scheme `[gGame]+0x1A8 → +0x28` and the cvar
  `[gGameCVars]+0x924`, the fields Prey's callback and tracker read.
- A scheme of 1, 2 or 5 goes back to 3 through Prey's own setter `0x182D7D0`,
  as the tracker does for a pad event. 3 and 4 stay. At most once per 100 ms.
- If the cvar reads 0, the command worker sets it again, at most once a
  second. The scheme is not moved meanwhile, because the laser would only move
  it back.
- It is gated on four byte spans. A mismatch, or a fault in the setter,
  disarms it and logs once.
- Log: `preyvr_prompts scheme_guard corrected=N from=5 to=3`. The minute
  status line shows `prompts={… guard=on corrected=N lastFrom=5 }`.

A posted zero `xi_thumblx` would also have moved the scheme. It was rejected
because it reaches the action maps too: a strafe held at that moment would
stop until the stick moved again.

**Evidence.** `tools/re/verify_controller_prompts.py` passes on the supported
hash. build/headless Release passes 85/85 ctest, including seven new
`controller_prompts` guard cases. No headset run yet.

**Headset check:**

1. Right after VR start, using only the laser: the main menu shows controller
   prompts, and the log has `scheme_guard corrected=1 from=5 to=3`.
2. They stay through loading a save, the pause menu and the inventory.
3. Controller Prompts off brings back PC prompts and the main-menu laser.
   On again brings back controller prompts.
4. The minute status shows `guard=on`. `corrected` stays small: about one per
   VR start or switch to on.

The main and pause menus still ignore the laser in pad mode (item 6 above).
With the guard they are always in pad mode while the option is on, not only
sometimes.
