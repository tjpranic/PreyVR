# Hands and helmet stop after a level load (2026-11-07)

Branch `custom`. TODO: "Going through loading screens can cause the hands to not be out. Affects the space
helmet too." The wearer: occasionally, after a level transition past a loading screen, the body IK and motion
controls stop and the space helmet displays wrong, until a weapon is drawn. **Investigated from source and 240
run logs. The fix below is built as PreyVR-138 (2026-11-07), all 117 tests passing. Headset, 2026-11-07: confirmed
working by the wearer.**

Run `player-20261107-153032` (PreyVR-138):

- **Helmet:** `noHelmet=0` for the whole run, against about 100 per load before, even with a weapon drawn. The
  helmet never read a destroyed character.
- **Save reload, 04:54:53:** the arms character was replaced with the skeleton unchanged (`0x21792c1d450` ->
  `0x2177bd7a9f0`, skeleton `0x2177c4dfeb0` both sides), so a reload replaces it too. Logged
  `owner_character_changed` 1.4 s later.
- **Level transition, 05:03:28:** rebound 0.2 s after the load.
- **Snap turns:** none went by without a rebind (scan verdicts `ok`).

All three loads in that log had a weapon drawn (`free=0` on both rebinds), and those already recovered before the
fix. So the empty-handed case rests on the wearer's report, not on this log.

## The defect

After a level load with **no weapon in hand**, the arms and body stop following the controllers and headset, the
space helmet is no longer placed on the head, and the head lamp falls back to Prey's mount. Drawing any weapon
brings everything back at once. A load with a weapon drawn is not affected. That is the "occasionally".

## Why

**The IK hook keeps the player's old arms character as its owner, and lets every other character, including the
new arms, straight through until the weapon equip count changes.**

1. **A level load rebuilds the player's arms.** The rig's skeleton pointer changes across the load: run
   `player-20261107-131731` `0x16dfc66a7d0` -> `0x16d0fd41bf0`; `player-20261101-164609` `0x1b2038da130` ->
   `0x1b1ecc9a540` (`rig_identified` lines). The character the IK cached (`gOwnerCharacter`) is the destroyed one.
2. **The fast pass-through only checks the equip count.**
   [AnimIkTakeover.cpp:27](../src/body/ik/AnimIkTakeover.cpp#L27): a character that is not the cached owner is
   passed to Prey unchanged when `WeaponEquipGeneration() == gOwnerGeneration`. With free arms, `gOwnerGeneration`
   follows `WeaponEquipGeneration()` every frame ([:64](../src/body/ik/AnimIkTakeover.cpp#L64)), and only a
   successful `CArkWeapon::AttachToHand` changes that count
   ([WeaponAttachment.cpp:212](../src/aim/WeaponAttachment.cpp#L212)). So the new arms never reach the code that
   would find them (`TryGetFreeArms`, which reads the live player and would return the new character).
3. **The rebind does not look at the character either.** `gOwnerCharacter` is written only when
   `CalibrationState::Bind` returns true ([AnimIkTakeover.cpp:55-60](../src/body/ik/AnimIkTakeover.cpp#L55-L60)),
   and `Bind` ([AnimIk.h:63](../src/body/ik/AnimIk.h#L63)) keys on equip generation, head reference and tracking
   epoch. A new character with the same free-arms generation would not be recorded even if it got through.
4. **Why a weapon fixes it.** The draw's `AttachToHand` bumps the count, every character takes the slow path, the
   owner rebinds to the new arms, and a later holster stays bound (free arms keep the new character). With a weapon
   drawn during the load, Prey re-equips it as the load ends, which does the same thing automatically.
5. **Why the helmet and head lamp go too.** Both take "the player's character" from the IK cache
   (`gPlayerCharacter = AnimIkOwnerCharacter`, [HelmetLane.cpp:130](../src/equipment/head/HelmetLane.cpp#L130),
   [HeadLampLane.cpp:117](../src/equipment/head/HeadLampLane.cpp#L117)). They look for `helmet`, `#camera` and the
   lights on the destroyed character, find nothing, and leave Prey's placement, which does not follow the headset.

## Evidence (observed runtime)

`tools/scan_load_ik_owner.py` over every run since weapon draws were logged (PreyVR-126, 2026-10-31). A healthy IK
logs `rig_identified` after every snap turn (the head reference changes and `Bind` rebinds), so snaps with no
rebind after a load mean the owner is stale.

| Weapon when the load ended | Loads | First rebind |
| --- | --- | --- |
| drawn | 28 | 0.1-4.1 s after the load, on its own |
| holstered | 12 | **11 only at the next draw** (within 0.1 s of it, 6.6-98 s after the load); 1 on its own after 25 s |

A run's first load from the main menu also rebinds on its own (~3 s), because nothing is cached yet.

- **`player-20261107-131731`, load ended 02:19:52, pistol holstered at 02:18:47.** Six snap turns at ~02:20:11 with
  no rebind. Helmet `stowed` froze at 5832 while `noHelmet` climbed 0 -> 1348 -> 3951 -> 4912. Head lamp counters
  froze at 5575 from 02:20:10 to 02:21:10. Pistol drawn at 02:21:30.490 (item id 65000 before the load, 64484 after),
  `rig_identified` 10 ms later, then helmet `placed` climbed and `noHelmet` stopped. The 02:31:48 load repeated it
  until the draw at 02:32:28.
- **Holster, then free arms after the fix-by-draw:** the same run holstered at 02:25:25 and the helmet kept being
  placed until 02:27:34. So `TryGetFreeArms` works after a load. Only the cached owner is wrong.
- **The exception, `player-20261101-164609` 05:48:23:** five snaps with no rebind, then a rebind at 05:48:48.7 with
  no draw logged. The log does not show why. The code allows one route: a call into the slow path, either from a
  character created at the old character's freed address or from an equip count change that wrote no
  `weapon_draw` line (inference).

## Hazard alongside

The helmet and head lamp dereference the destroyed character under SEH. In these runs the reads failed (`noHelmet`).
If the freed block still held the old attachment list, `WriteModel`/`WriteHideBits` would write into freed heap
memory (inference). About 100 frames per load miss the helmet even with a weapon drawn; where in the load those
frames fall is not logged.

## Fix (built, PreyVR-138)

1. **One reader for the player's arms character.** `PlayerArmsCharacter(base, player)`
   ([EngineAccess.h](../src/engine/EngineAccess.h)): player `+0x1808` -> `+0x710`, vtable
   `CharacterInstanceLayout::vtableRva`, SEH-guarded. It replaces the two copies of that chain: `TryGetFreeArms`
   (which keeps its cinematic check) and body depth's `ReadPlayerCharacter`. The weapon rig's character is the same
   instance ([ARMS-NO-WEAPON-2026-10-07.md](ARMS-NO-WEAPON-2026-10-07.md#why-the-arms-vanish-measured)).
2. **IK hook.**
   - `PublishAnimIkPlayer(frame.player)` stores `gPlayerArms` once per gameplay frame (main thread,
     `TickGameplayLanes`, next to `PublishBodyDepthPlayer`).
   - The fast path passes through only characters that are **neither** the cached owner **nor** `gPlayerArms`. NPCs
     keep the fast path even while the cache is stale, so the 2026-09-07 try-lock contention cannot come back. With
     `gPlayerArms` still 0, the rule reduces to the old one.
   - `CalibrationState::Bind` takes the character as a fourth key, so a replaced character rebinds exactly like a
     weapon switch: joints re-identified, completed calibrations re-taken after the settle frames, no stale offset.
   - One log line per change of bound character:
     `preyvr_anim_ik result=0 detail=owner_character_changed old=0x… new=0x… free=0|1`. It also appears on a load
     with a weapon drawn, where the re-equip rebinds onto the new arms.
   - VrMode's automatic calibration request also fires on a character change, as it does on a weapon change.
3. **Helmet and head lamp read the player's character live** (`PlayerArmsCharacter` of `PlayerInstance`), not the
   IK cache. They never touch a destroyed character, and they no longer depend on the IK binding. The head lamp
   matches the light by name first, so the actor-system lookup runs only for its two lights, about twice a frame.
   Its gate now also pins the player getter (`BodyAtMatchesGuarded` on StickReread's `PlayerSingleton` row), as the
   helmet's does. The helmet now calls the getter every frame, menus included. That is safe (observed static, Steam
   bytes): `0x157C990` loads a game global (`[rip+0xCD108D]`), calls its vtable `+0x70` (framework), then that one's
   `+0xC0` (actor system), and tail-calls `GetActor(0x7777)` (`+0x18`). With no player it returns null. It faults
   only on a null game global, which the nullable `0x157C9C0` tests (`test rcx,rcx`) and which exists before VR
   starts.
4. **Tests.** `anim_ik`: a character change alone rebinds, drops the old offset and re-queues the calibrated hand.
   `head_lamp`: an attachment that is not a light never looks the player up. `body_depth_hook` already drives the
   shared reader (player, wrong vtable, none).

### Rejected

- **Reset the owner when a loading screen starts.** This only covers loads that pass through the hooked present,
  and it still needs the `Bind` change. The identity check covers any replacement (save load, death reload).
- **Bump the equip count when the character changes, as the draw does.** The published rig keeps its old
  generation, so with a weapon drawn the fast path stays off until the next equip (the contention again). Clearing
  the rig instead races Prey's own re-equip at load end.

## Headset check

Go through a level load empty-handed, and again with a weapon drawn; once in zero-G for the helmet. Expect:

- the hands follow at once;
- `owner_character_changed` at each load (the scan's `character_changed=` column);
- the scan's verdicts all `ok`;
- helmet `noHelmet` no longer climbing after the load, and head lamp `flashlight=` still counting.

## Evidence chain

```text
Question / decision: why do the IK, helmet and head lamp stop after a load until a draw?
Producer and receiver: gOwnerCharacter <- AnimIkTakeover.cpp:60, only inside Bind (AnimIk.h:63; generation,
  reference, epoch); gEquipGeneration <- WeaponAttachment.cpp:212, AttachToHand success only.
Consumer and receiver: fast pass-through AnimIkTakeover.cpp:27 (character != owner && generation equal);
  HelmetLane.cpp:130 / HeadLampLane.cpp:117 gPlayerCharacter = AnimIkOwnerCharacter.
Discriminating observations: skeleton pointer changes across loads; 11/12 empty-handed loads rebind within 0.1 s
  after the next draw, 28/28 drawn loads rebind on their own; free arms rebind after a holster post-draw.
Counterexample or rejected alternative: TryGetFreeArms failing after a load (refuted: the holster at 02:25:25 stayed
  bound); one empty-handed load recovered without a draw (route unlogged).
Result and remaining runtime condition: cause established from source plus logs; fix built as PreyVR-138, worn OK
  2026-11-07 (empty-handed load on the wearer's report; the run log's loads all had a weapon drawn).
Replay command or evidence path: py -3 -I tools/scan_load_ik_owner.py "%LOCALAPPDATA%\PreyVR\runs"
```
