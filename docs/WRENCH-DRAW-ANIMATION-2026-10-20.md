# The wrench draw animation spins the hand

Status, 2026-10-20 (Steam `PreyDll.dll` `7d6e322f...05311a7`):
- **The draw skip: worn OK** (PreyVR-49). The hand no longer spins.
  [WrenchDraw.cpp](../src/dll/WrenchDraw.cpp) skips the wrench's select action and
  finishes the selection a frame later with Prey's own function.
- **The draw sound: worn OK** (PreyVR-51). It was found by a capture (PreyVR-50), and
  [WrenchSound.cpp](../src/dll/WrenchSound.cpp) replays it after each skip.
- **The stow skip: worn OK** (PreyVR-52, see "The stow"). The wrench is put away
  at once, through Prey's own no-animation stow, and its sound replays.
- `wrench.draw 0` brings both animations back; `wrench.sound play 0` turns both sounds off.
- Cause of the spin: inferred from the IK code plus logged joint numbers; not measured
  during a draw.
- Static checks: `python -E -B tools/re/verify_wrench_draw.py` (130 checks; report in the
  evidence folder). Tests `wrench_draw_native`, `wrench_draw` and `wrench_sound` (the lanes
  against fake engine objects); full suite 89/89.

Evidence: [`evidence/wrench-draw-2026-10-20/`](evidence/wrench-draw-2026-10-20/). It holds the
Ghidra headless scripts (`gd.py` d/x/v modes, `gdis.py`, `gscan.py`, `vt.py`), the string scan
`strs.py`, the decompiles they produced, and `verify_wrench_draw.json`.

## The bug

TODO.md (Immersion): "Hand animation plays when taking the wrench out. It causes the
right hand to spin. The animation should not play."

## Why the hand spins

- **The wrench's mount.** The wrench hangs from joint 47, `r_handProp_jnt`, which is a
  child of the wrist, 45 `r_hand_jnt`. Evidence: `ik.align ... basis=wrench_model
  socket=47` in the logs, and the address registry's joint list.
- **The rotation solve.** Every frame, `weaponrig::ReadBasis`
  ([WeaponRigAlignment.cpp](../src/common/WeaponRigAlignment.cpp)) reads the wrench's
  rotation in the hand from the animated pose:
  `weaponInWrist = inverse(wrist) * socket * relativeDefault * extra`.
- **The wrist write.** `SolveWrist` then writes the wrist that puts the wrench on the
  controller: `wrist = inverse(character) * aim * inverse(barrel) * inverse(weaponInWrist)`.
  [AnimIkTakeover.cpp](../src/dll/AnimIkTakeover.cpp) `DriveHand` calls it for the
  right hand.
- **The result.** Prey's draw turns the prop joint inside the hand. The solve keeps the
  wrench still on the controller, so it turns the hand the opposite way by the same
  amount: the hand spins around a still wrench.

Guns are aligned the same way. Their own draws would twist the hand only to the extent
that they move the prop joint.

## Where the draw animation starts

The weapon animations are Mannequin fragments. `0x1637940` fills the weapon fragment-ID
table from the controller definition; slot n is at byte n*4.

| Slot | Offset | Fragment |
|---|---|---|
| 48 | `+0xC0` | `unholster_select` |
| 49 | `+0xC4` | `Wpn_Select` |
| 50 | `+0xC8` | `Wpn_Unselect` |
| 51 | `+0xCC` | `Wpn_Pickup` |
| 73 | `+0x124` | `Wpn_Idle` |

The weapon keeps the table at `weapon+0x470` (`param_1[0x8E]` in OnEquip).

The equip chain, all on the game thread:
1. **SelectItem `0x12748D0`** (player equipment, `player+0x14B8`). If a weapon is out,
   it puts that weapon away first (vfunc `+0x10`), and the equip happens later. With
   none out, it calls the new weapon's **OnEquip (vfunc `+8`) first and only then
   stores its id at `equipment+0x58`**.
2. **OnEquip `0x1699800`**, shared by the wrench (registry R-024 notes).
   - It calls `0x169B450`, which runs AttachToHand `0x16914F0`.
   - Then `0x169C6F0` queues the **idle action**: fragment `Wpn_Idle`, held at
     `weapon+0x2D0`.
3. **The end of OnEquip** (`0x1699C3C`-`0x1699C81`) builds the **select action**:
   - It calls `0x169D6F0`, which allocates 0x90 bytes with vtable `0x1E6D698`.
   - Fields: the item id at `+0x80`, the fragment at `+0x38`, a select flag at `+0x88`
     and the refcount at `+0x50`.
   - Normally the fragment is `Wpn_Select` with the flag set.
   - On a first pickup, it is `Wpn_Pickup` with the flag clear. A first pickup means the
     item is not yet in the list at `equipment+0x80` and the global at `[0x2C16830]+0xE5C`
     is above 0.
   - **PlayAction `0x169A5B0`** `(weapon, IAction**, float)` writes the float to
     `action+0x54` and `1.0` to `+0x58`. It then queues the action on the weapon's
     action controller (vtable `+0x90`), and drops the caller's reference.

`0x169D6F0` has one caller, OnEquip, so vtable `0x1E6D698` is only ever this select or
pickup action. PlayAction has about 37 call sites (fire, reload and so on).

## What the select action does for the weapon

The action's handlers are vtable slots `+0x10` and `+0x20`:
- **Enter `0x169D8D0`.** If the select flag is set, it calls weapon vfunc `+0x100`
  (`0x1692E60`): `weapon+0x48B = 0`.
- **Exit `0x169DBD0`.** It calls weapon vfunc `+0x110` (`0x1692F50`). On a first pickup,
  that hands `weapon+0x360` (and `+0x368`) to a manager reached through `0x119A3A0` and
  `0x119A460`, and adds the item to the list. It is likely a first-pickup notice; this is
  not established.
  It then calls vfunc `+0x108` (`0x1692E70`):
  - If the player's current weapon is this one, `player+0x992 = 0`.
  - `weapon+0x48B = 1`.
  - It calls vfunc `+0xB0`.
  - It releases `weapon+0x2D8`.

The wrench and base weapon vtables (`0x1E92F00`, `0x1E6C640`) agree on `+0x100`, `+0x108`
and `+0x110`.

What those two bytes mean:
- **`player+0x992`: a weapon switch is under way.** The outgoing weapon sets it (`0x169BD80`)
  and the incoming weapon's select clears it. It is read by `0x1262B60`, `0x1262CE0`,
  `0x15806E0`, `0x16A7FA0` (weapon spread), `0x16A80A0` and `0x16AB510`.
- **`weapon+0x48B`: the weapon is free to act.** Reload checks it before it starts
  (`0x169BE80`), and so does `0x169AE60`.

Consequences:
- **Not queuing the action is not enough.** Nothing would then finish the selection, so
  `player+0x992` would stay 1 after a switch. Prey's own path when a fragment is missing
  (`fragment == -1`, which PlayAction skips) has the same gap.
- **Finishing it inside OnEquip is too early.** SelectItem writes the current id after
  OnEquip returns, so `0x1692E70` would not clear `player+0x992` from there. Finish it
  one game-thread frame later, as if the select action had ended at once.

## What was built

[WrenchDraw.cpp](../src/dll/WrenchDraw.cpp) and [WrenchDrawNative.h](../include/preyvr/WrenchDrawNative.h):

1. **The gate.** On the first frame, on the main thread, it checks the nine bodies in
   `WrenchDrawNative.h`:
   - PlayAction, OnEquip, the select action's two constructors, Enter and Exit;
   - the weapon's `0x1692E60`, `0x1692E70` and `0x1692F50`.

   It also checks EquipmentNative.h's rows for SelectItem and the lookup, and five
   vtable slots: the wrench's `+0x100`, `+0x108` and `+0x110`, and the select action's
   Enter and Exit. If they all hold, it hooks **PlayAction `0x169A5B0`** with MinHook.
   A mismatch refuses for good: Prey keeps its draw, and the log says
   `gate result=193 detail=bytes_mismatch`.
2. **The skip.** The detour acts only when all of these hold: the action's vtable is
   `base+0x1E6D698`, the weapon's is the wrench's `base+0x1E92F00`, the action's item
   id matches the weapon's `+0x38`, and the call is on the main thread. Then it:
   - does not queue the action;
   - drops the caller's reference as PlayAction itself does for an action it does not
     queue (`--action[+0x50]`; at <= 0 the action's `+0xD0` releases it);
   - does Enter's part: for a select (`action[+0x88]` set), the weapon's `+0x100`;
   - records the item id, with the frame.

   Everything else goes to Prey's PlayAction unchanged: other weapons, other actions,
   other threads, the switch off, and a fifth draw while four are waiting.
3. **The finish.** CameraEditHook's `CSystem::Render` detour calls
   `ServiceWrenchDrawFrame` on Prey's main thread every frame, menus included. Once a
   whole frame has passed since a skip, it looks the item up (`0x16A5650`). If that is
   still the wrench, it calls the wrench's `+0x110`: Exit's part, Prey's own first-pickup
   bookkeeping and select-finished. A wrench gone by then is counted (`gone`) and left
   alone. Waiting skips are finished even if the switch is turned off meanwhile.
4. **The switch and the report.** `wrench.draw [0|1]` turns the skip on or off (on by
   default). The report gives `hooks`, `gate`, `selects`, `pickups`, `finished`, `gone`,
   `waiting`, `full`, `elsewhere` and `fault`. Each skip and finish is logged:
   - `preyvr_wrench_draw skip id=<item> kind=select|pickup`
   - `preyvr_wrench_draw finish id=<item> result=finished frames=2`

Expected result:
- The arms go straight to what Prey plays once its select ends, which is normally the
  wrench idle. It is queued on equip (`0x169C6F0`, fragment `Wpn_Idle`) unless the
  weapon already holds one at `+0x2D0`. Only the select layer on top is gone.
- No select delay.
- No spin from the draw.

`Wpn_Pickup` (first pickup) goes through the same action, so it is skipped too. Its
first-pickup bookkeeping still runs from `+0x110`.

## What to check in the headset

- **Every way the wrench comes out, with no spin:** the wheel, the grip draw from a
  holster (ArmsLane's native equip), after loading a save, and on a first pickup.
- **Switching still works:** wrench to gun and back, and quickly back and forth.
  Anything gated on `player+0x992` (spread, and the movement checks above) behaves
  normally. In `wrench.draw`, `selects + pickups` equals `finished + gone + waiting`,
  and `gone`, `full`, `elsewhere` and `fault` stay 0.
- **`wrench.draw 0`** brings the draw (and the spin) back, for an A/B.
- **The blend into the idle:** Mannequin's blend from the previous pose may still turn
  the hand a little for a moment.
- **The draw sound:** lost in PreyVR-49 with the fragment. Restored by the replay in
  PreyVR-51 (see "The equip sound" below).
- **Putting the wrench away (`Wpn_Unselect`):** this is not covered. Check whether the
  hand spins then too.

## Headset result, 2026-10-20

PreyVR-49 tested by the user: **the right hand no longer spins.** The draw's sound
went with the draw.

## The equip sound (investigated 2026-10-20, static)

The sound belongs to the skipped fragment; OnEquip plays no audio of its own. Decompiles
are in [`audio_decompile.c`](evidence/wrench-draw-2026-10-20/audio_decompile.c).

**Two ways the fragment can sound.**
- **Mannequin's `PlaySound` procedural clip** (CryEngine's audio clip). Its params,
  serialized at `0x3AE010`:
  - `StartTrigger` `+0x08` and `StopTrigger` `+0x10` (strings);
  - `AttachmentJoint` `+0x18` (a CRC) and AI `Radius` `+0x1C`;
  - `FireAndForget` `+0x20`, `IsVoice` `+0x23`, `PlayFacial` `+0x24`;
  - `SoundFlags` `+0x28` (first- or third-person only).

  OnEnter is `0x3AE460`. It resolves the start trigger by name with `gEnv->pAudioSystem`
  (`0x224DA50`) vtable `+0x40`, GetAudioTriggerID. It then plays it through `0x3AEBE0`:
  the AudioContext's entity audio proxy, vtable `+0xD8` ExecuteTrigger
  `(id, lipSync, proxyId 1, empty callback info)`. OnExit (`0x3AE5B0`) stops the start
  trigger (`+0xE0`) unless the clip is fire-and-forget.
- **A `sound` animation event in the draw animation.** The actor's event names are
  lowercase CRC32s (`0x16D0B00`: `sound`, `PluginTrigger`, `footstep`, `foley`, …),
  handled by `0x17C30C0`.

Which one the wrench's draw uses, and the trigger's name, are in `Animations.pak` and
`Audio.pak`. Those are encrypted: the central directories are scrambled and each pak
carries a 2320-byte encryption comment. So they cannot be read offline.

**Playing it ourselves.** Prey's own Lua bind `ExecuteAudioTrigger` (`0x940B90`) does it:
1. It takes the entity's audio proxy with `IEntity` `+0x210` (type 3, which creates the
   proxy if needed; `+0x200` only gets it).
2. It calls `proxy+0xD8` with `(triggerId, 0, proxyId, callbacks)`.

`StopAudioTrigger` (`0x960F80`) is the `+0xE0` counterpart. The trigger id comes from
`pAudioSystem+0x40`, by name. The player's entity is `IEntitySystem` (`0x224DA38`) `+0x70`
GetEntity(`0x7777`).

**What is missing: the trigger's name (and its time into the draw).** It needs one
capture run.

### The capture (built 2026-10-20, package PreyVR-50)

[WrenchSound.cpp](../src/dll/WrenchSound.cpp) and
[WrenchSoundNative.h](../include/preyvr/WrenchSoundNative.h). It is off by default;
`wrench.sound capture 1` turns it on.

While it is on, WrenchDraw lets the wrench's draw play, and each draw opens a 3 s window
(at most 200 lines). Three hooks log inside the window, timed from the draw's start:
- `0x3AE460` (PlaySound OnEnter): a `clip` line with the start and stop triggers,
  fire-and-forget, sound flags, joint and entity.
- `0xD59F40` (the trigger lookup by name): a `resolve` line with name, id and found.
- `0x911F40` (`CEntityAudioProxy::ExecuteTrigger`): an `execute` line with id, entity
  (`player`, `wrench` or its id) and proxy.

The gate hashes the three bodies and the `GetAudioTriggerID` thunk, and checks two vtable
slots. The verifier also proves the id is the standard CRC32 of the lowercased name, so a
name always gives the same id.

**The run:**
1. Start the game as usual and load a save with the wrench.
2. `Tune Prey VR.cmd wrench.sound capture 1`. The reply should say `hooks=1 gate=ok`.
3. Take the wrench out 3 or 4 times, a few seconds apart, from the wheel and from the
   holster grip. Stand still if you can, so fewer other sounds land in the windows.
4. `Tune Prey VR.cmd wrench.sound` for the report, then quit.
5. Send the run's `PreyVR.log` (`%LOCALAPPDATA%\PreyVR\runs\player-…`).

The draw's sound is the trigger that recurs in every window at about the same `tMs`. The
next build plays it on the player's entity at that time after each skip.

### Capture result (run `player-20261020-203533`, PreyVR-50)

Four draws (windows 2-5). Each opened with the same two PlaySound clips on the player,
11-13 ms after the select was queued:

| Trigger | Id | Clip |
|---|---|---|
| `Play_Player_Arm_Wrench` | `0x446C5BFD` | no joint, sound flags 0, open-ended, stop trigger `null` |
| `Play_switch_weapon` | `0x364DA6FE` | the same |

`null` (`0x25CBFC4F`) is a real trigger that the clip's exit executes, and it stops nothing,
so both sounds are fire-and-forget in effect. All ids are the CRC32 of the lowercased names.

The other lines are not the draw's:
- `Play_Player_Cancel_Wrench_Charge` comes at ~1.46 s in every window, when the select
  ends and the idle begins. It still plays with the skip.
- `Play_Player_Unequip` is the wrench being put away.
- `Play_Env_Sparks` and `Play_Mimic_Glitch` are the level.

Window 1, the equip as the save finished loading, filled its 200 lines within 205 ms with
level-start lookups. So whether the draw's sound plays natively after a load is not known.

### The replay (built 2026-10-20, package PreyVR-51)

The frame after each skip, WrenchDraw calls `WrenchSoundPlayDraw`. It does what the clips
did:
1. Get the player's entity: the entity system (`0x224DA38`) `+0x70` GetEntity(`0x7777`).
2. Get its audio proxy: `+0x200` GetProxy(3), which must have the `CEntityAudioProxy`
   vtable.
3. Play both triggers through its ExecuteTrigger (`+0xD8`, the gated `0x911F40`): proxy 1,
   no lip sync, callback info `{0, 0, 0, 1}`.

The trigger ids come from the audio system's lookup the first time, and must match the
captured ones.

`wrench.sound play 0` turns the replay off. The draw lane reports `sounds=`, and the sound
lane reports `played`, `playFailed` and `lastPlay`, with a `play result=` line each time.
While `wrench.sound capture 1` is on, the draw itself plays, so no replay is added.

What to check:
- **The sound on every draw:** from the wheel, from the holster grip, and on a first pickup
  (the pickup's own sounds were not captured; it plays the draw's).
- **After loading a save with the wrench out:** the draw sound now plays as the load
  ends. Note whether that is welcome.
- **`wrench.draw 0`:** Prey's own draw and its own sound, with no replay on top.

PreyVR-51 worn 2026-10-20: "works as expected".

## The stow (built 2026-10-20, package PreyVR-52)

The user asked for the stow animation to be skipped too, for consistency, with its sound
kept.

**How Prey stows.**
1. SelectItem (`0x12748D0`) and Unequip (`0x12773A0`) call the current weapon's vtable
   `+0x10`, OnUnequip, with `animate` set. The wrench's own override is `0x17392F0`: it
   cancels its charge (`+0x90`), calls the shared `0x169A000` with the same `animate`, and
   clears two fields.
2. The shared OnUnequip needs a fragment table (`+0x470`) and the weapon not lowered
   (`+0x2C9`). Then:
   - **Animating:** it builds the stow action (`0x169D790`, vtable `0x1E6D770`, fragment
     `Wpn_Unselect`) and plays it. That action's Exit (`0x169DCA0`) calls Stow
     (`0x16924E0`) and sets the HUD's "empty".
   - **Not animating:** it calls Stow and "empty" at once, given the hand attachment.
3. **Stow** detaches the weapon from the hand. On the player's equipment it sets current
   to 0, last equipped to this weapon, and clears "unequip in progress". SelectItem then
   equips any pending weapon at once.

So the instant path is Prey's own, and it ends exactly as the animated one does.

**What was built.** WrenchDraw also hooks `0x17392F0` (both hooks install together). Asked
to animate, on the main thread, with the skip on and no capture running, a wrench with a
fragment table that is not lowered is given `animate = 0`. Then `WrenchSoundPlayStow`
plays `Play_Player_Unequip` (`0xE06723D1`) on the player.

That clip is the stow fragment's PlaySound: three stows in the capture, each at the
fragment's start with a 300 ms blend. `Play_Player_Cancel_Wrench_Charge` followed 313 ms
later each time; it is a "cancel" event and is not replayed.

**Left to Prey:** a lowered wrench (EMP, friendly NPCs). Its animated stow plays a lower,
then the stow, and that Exit also calls SelectDone, which ends the switch. The instant path
does not, so it is not used there (`stowsLowered` counts these).

Report: `stows`, `stowSounds`, `stowsLowered`, and a `stow id= sound=` line.

What to check:
- Putting the wrench away from the holster grip, from the weapon wheel (wrench to gun:
  the gun should come up at once), and by picking up an object to carry.
- The unequip sound plays each time.
- The hands still follow, with no weapon, after the stow.

PreyVR-52 worn 2026-10-20: tested and verified working.

## Alternatives

- **Hold the prop joint in the IK lane.** For the wrench, the lane would hold joint 47's
  rotation in the hand at its idle value: use it in `weaponInWrist`, and write the joint
  after the pass.
  - It stops the spin from any wrench animation (draw, pickup, EMP and melee reactions).
  - But the draw still plays: the fingers animate and the wrench comes out late.
  - It could be added later on top of the skip.
- **Speed the action up instead of skipping it.** The detour would pass a large float as
  `action+0x54`, likely the speed. The engine would keep all of its own bookkeeping, but
  at least one frame of the draw would show, and `+0x54`'s meaning is unverified.
- **All weapons.** The same skip works for every weapon (drop the wrench vtable test).
  The TODO asks only for the wrench.
