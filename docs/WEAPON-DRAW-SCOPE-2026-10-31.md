# Taking every weapon out without its animation: scope (2026-10-31)

Branch `custom`. TODO (UX/Immersion): "Remove Q-Beam equip animation (?) ... Add an option to toggle all of
them?" The investigation is static, against the Steam DLL (SHA `7d6e322f…11a7`); [the run](#the-run) is measured.
Addresses are RVAs. It builds on [the wrench's skip](WRENCH-DRAW-ANIMATION-2026-10-20.md), worn OK on PreyVR-49 to 52.

**Worn OK on PreyVR-126, 2026-10-31.** The wearer tested and verified it working. PreyVR-125 (the skip and the capture)
ran first in `player-20261031-211309`, and PreyVR-126 adds each weapon's sounds and takes the capture out. See
[the run](#the-run) and [as built](#as-built).

**Answer:** the wrench's skip extends to every weapon. The mechanism needs no new engine work:
- All nine weapon classes draw through the same select action and stow through the same shared function.
- Every per-class difference sits behind a vtable slot that the skip already calls through the vtable.

Two things are needed beyond dropping the wrench test:
- **The stow hook has to move** to the shared OnUnequip.
- **Each weapon's draw sound has to be captured.** Only the wrench's is known, and the capture code had been
  cut, in ba6599c and f9cc865.

Evidence: [`evidence/weapon-draw-2026-10-31/`](evidence/weapon-draw-2026-10-31/). To replay it, run
`python -E -B docs/evidence/weapon-draw-2026-10-31/verify_weapon_classes.py` (106 checks, all passing). It is
the investigation's own check; its rows have since moved into `tools/re/verify_weapon_draw.py` (169 checks), which
ctest runs. The decompiles are alongside, and `gmix.py` reproduces them.

## The weapon classes

Registered by name in `0x17327F0`. Each row was followed from name to factory to create to constructor to
vtable. Pistol, Shotgun and ToyGun (the Boltcaster) share one C++ class, as do the four grenades.

| Registered as | Weapon | vtable | Its OnEquip (`+8`) adds, after the shared one | `+0x100` / `+0x108` / `+0x110` |
|---|---|---|---|---|
| `ArkWeapon` | base | `0x1E6C640` | nothing | shared |
| `ArkWeaponWrench` | wrench | `0x1E92F00` | nothing | shared |
| `ArkWeaponDoubleWrench` | not seen in play | `0x1E92A08` | nothing | shared |
| `ArkWeaponPistol`, `ArkWeaponShotgun`, `ArkWeaponToyGun` | guns | `0x1E6F780` | `0x16AA8C0`: spread, `ammoOpen` | shared |
| `ArkWeaponGooGun` | GLOO | `0x1E6E390` | `0x169F3F0`: its ammo material | shared |
| `ArkWeaponInstalaser` | Q-Beam | `0x1E6E9F0` | `0x16A29A0`: ammo material, **`+0x584 = 1`** | **own `+0x110`** `0x16A07A0` |
| `ArkWeaponStunGun` | Disruptor | `0x1E31290` | `0x13BB1D0`: its ammo material | **`+0x100` no-op**, **own `+0x108`** `0x13B9E80` |
| `ArkWeaponEMPGrenade`, `…LureGrenade`, `…NullwaveTransmitter`, `…RecyclerGrenade` | grenades | `0x1E6F148` | `0x16A5DE0`: shows the grenade, `ammoOpen`, `+0x48B = 1` | **own `+0x108`** `0x16A5300` |
| `ArkWeaponDiscRifle` | not seen in play | `0x1E308D0` | `0x13B7A10`: `onWeaponEquipped` metric | shared |

Why the differences don't matter to the skip:
- **All six OnEquip wrappers call the shared OnEquip `0x1699800` first**, and only OnEquip builds the select
  action (`0x169D6F0` has one caller). OnEquip returns before that for any owner but the player (`0x7777`).
- **The Q-Beam must finish through its vtable.** OnEquip's `+0x584 = 1` blocks firing and reloading
  (`0x16A02B0`, `0x16A3B40`, `0x16A3C40`). Only its own `+0x110` clears it, then jumps to the shared finish.
  The lane already calls `+0x110` through the vtable; a direct call to `0x1692F50` would leave the Q-Beam dead.
- **The Disruptor's** `+0x100` is a no-op, so it never goes busy, and its `+0x108` clears two more bytes.
- **The grenade's** `+0x108` shows the grenade, or unequips it if none are left. It is reached through the
  shared finish, so it still runs. Native order sets busy after OnEquip's `+0x48B = 1`; the skip sets it before.
  Either way it ends free. Throws don't go through the select action.
- **An empty gun still reloads on its own.** `SelectDone` calls `+0xB0`, which starts the reload, so that now
  begins two frames after the gun appears instead of at the end of the draw.
- **No weapon event is lost.** Throw, Deploy, Reload, InterruptFire, GlowEnd and MuzzleFlash are handled at slot
  `+0x98` of two other actions (`0x1E6D9F8`, `0x1E6E268`). In the select and stow actions that slot is a no-op.

The mod side is clear too. Nothing outside the wrench lane reads `+0x48B` or `player+0x992`, and the IK's
re-take waits a fixed 90 frames. The two-hand latch only waits for the hand to rest, so it settles sooner.

## What has to change

1. **The skip** (`PlayActionHook`). Drop the wrench test in `ReadSelect`: the select action's vtable, plus the
   item id on both sides, identifies it. `FinishSelect`'s wrench test becomes "one of the nine vtables".
2. **The stow.** Move the hook from `WrenchUnequip 0x17392F0` to the shared OnUnequip `0x169A000`:
   - Every class reaches it with `animate` intact: by call or tail jump, DL untouched (checked in the
     wrappers). MinHook can take its entry, a plain `mov [rsp+18h],rbx`.
   - The shared function does nothing unless the weapon is the current one, so the hook has to test that
     itself (id == `equipment+0x58`), or the stow sound plays for nothing.
   - The instant path only stows given the hand attachment (`+0x2B0`). Without one it would leave the
     unequip hanging, so the hook passes those on to Prey's animated path.
   - `SelectItem` lets OnUnequip return, wrapper tail included, before it calls the next OnEquip
     (`0x12748D0`). So the guns' `ammoClose` still comes before the next weapon's `ammoOpen`.
3. **The gate.** Add the nine vtables' `+0x100`, `+0x108` and `+0x110` slots, and bodies for `0x16A07A0`,
   `0x13B9E80` and `0x16A5300`. Drop the `WrenchUnequip` hook row.
4. **The sound** (below): per-class tables instead of one list. `Pending` records which, since the weapon
   may be gone by the next frame.
5. **The option** (below).
6. **The name.** `WrenchDraw` and `WrenchSound` become `WeaponDraw` and `WeaponSound`, under
   `src/equipment/weapon/`, with their tests, verifier and `CMakeLists.txt` rows; `wrench.draw` and
   `wrench.sound` become `weapon.draw` and `weapon.sound`. Update `TUNING-COMMANDS.md`, the registry and
   `CommandChannelTests.cpp`. Nothing else lived in `src/equipment/wrench/`, so that folder is gone.

## The sound

The skip removes the fragment, so it removes the fragment's sound. `Play_switch_weapon` and
`Play_Player_Unequip` look generic. The arm sound won't be: the wrench's is `Play_Player_Arm_Wrench`. Those
three are the only draw or stow triggers in all 218 stored runs, so the rest have to be captured. The audio
and animation paks are encrypted.

- **The capture** was brought back for one build, opening a window on any weapon's draw or stow, then taken out
  again. That version is kept as `WeaponSound-with-capture.*` in the evidence folder.
- **The class name is the key.** Pistol, Shotgun and Boltcaster share a vtable, so only the name tells them
  apart. It comes from `weapon+0x40` (the entity) `+0x18` then `+0x10`, the route `0x13B7DC0` takes. The run read a
  real name in all 78 windows.
- **A weapon nobody captured** gets `Play_switch_weapon` on a draw and `Play_Player_Unequip` on a stow.
- **Getting a weapon for a capture.** `grenade.give` supplies the four grenades. Guns have to come from a save:
  Prey's `i_giveitem` is not on the console allowlist, and the guns' archetype names are in the encrypted paks.

## The option

- **Label and place:** `WEAPON DRAW ANIMATIONS` on EQUIPMENT, after Body Holsters. Key `weapon_draw_anims`.
- **Default off**, so every weapon is skipped, as the wrench is today.
- **One switch for all weapons, the wrench included**, as the TODO asks. On plays Prey's draws and stows and
  brings the wrench's hand spin back, so the help text should say the hand turns with the weapon.
- **Wiring:** the `Setting` enum, one `Spec`, `equipment[]`, a row in `VrOptionsRuntime.cpp`'s table, and stubs
  in `VrOptionsRuntimeTests`. The setter only stores an atomic, so neither `kStartOff` nor `kNeedsPrey` applies.

## Effects to expect

- **Weapon swaps become instant, and that changes the game's balance.** [The run](#what-a-swap-costs) measured what
  Prey charges for a swap: about 0.5-0.7 s to stow and 0.45-1.46 s to draw. With the skip, both are gone and a
  weapon is ready two frames after it appears. Each weapon's own fire-rate timer is untouched.
- **Stowing while attacking keeps Prey's animation**, as it does for the wrench today (see Corrections).

## What static work can't answer

These are in the encrypted paks, or only visible in the headset:
- **Clips other than sound in a draw**, such as a particle or a glow. The Q-Beam and Disruptor are the ones to
  watch. Nothing in the weapon code depends on them, but they could be visibly missing.
- **Mannequin's blend into the idle** may still turn the hand for a moment, as the wrench doc noted.
- **Whether `DiscRifle` and `DoubleWrench` can be reached in play.** They are covered either way.

Check in the headset, per weapon type:
- From the wheel, the holster grip, a save load, and a first pickup (`Wpn_Pickup`).
- Swapping back and forth quickly.
- The Q-Beam fires and reloads after a skip.
- An empty gun reloads when drawn.
- A grenade appears and throws.
- The Disruptor's charge works.
- In the report, `selects + pickups == finished + gone + waiting`, with `gone`, `full`, `elsewhere` and `fault` at 0.

## Order of work

1. **Offline (done):** steps 1-6, plus the capture restored for now. The tests and verifier cover this.
2. **One session (done):** the capture pass (draws play), then the skip pass on the same build.
3. **Next build (done, PreyVR-126):** the trigger tables go in and the capture comes out again.
4. **Worn check (done):** PreyVR-126, worn OK 2026-10-31.

## Corrections to the wrench doc

Found while reading the same code. None change what the wrench does, but the names mislead.
- **`+0x2C9` is not "lowered".** `StartAttack` (`0x169BD80`, base `+0xE8`, from the press path `0x16995B0`) sets it
  and `StopAttack` (`0x169BFA0`, `+0xF8`) clears it, so it means an attack is under way. The "lowered wrench" stow
  is really a stow during an attack: `0x169D5C0` plays a closing fragment, then its Exit `0x169D9F0` calls `+0x108`
  and queues the stow. Rename `WeaponLowered` and `stowsLowered`. The EMP route is `+0x32A`, a different path.
- **`player+0x992` is not "a weapon switch".** `StartAttack` sets it too, and its readers are sprint and spread.
- **`+0x108` is not select-only.** The Exits of other weapon actions call it too (`0x169D920`, `0x169D960`).
- **The PlayAction float is a playback rate.** OnEquip passes 1.0 and reload passes a weapon stat (`0x169BE80`).

## The run

`player-20261031-211309`, PreyVR-125, about six minutes. Steam recorded exit code 0. The lines are in
`capture_lines-player-20261031-211309.log`, and `parse_capture.py` makes `capture_summary.txt` from them.

**The capture pass:** 78 windows over eleven classes, none truncated (`dropped=0`). `DiscRifle`, `DoubleWrench` and
plain `ArkWeapon` never came up. The pistol came out once, so its row rests on one draw and one stow.

| Class | Weapon | Its own draw trigger | Id | Extra `Play_switch_weapon` |
|---|---|---|---|---|
| `ArkWeaponWrench` | wrench | `Play_Player_Arm_Wrench` | `0x446C5BFD` | |
| `ArkWeaponPistol` | pistol | `Play_WPN_Pistol_Equip` | `0xEAA21D0A` | draw |
| `ArkWeaponShotgun` | shotgun | `Play_Pickup_Shotgun` | `0x258CF3AD` | stow |
| `ArkWeaponToyGun` | Boltcaster | `Play_WPN_NarfGun_Equip` | `0x2FE7D01F` | stow |
| `ArkWeaponGooGun` | GLOO | `Play_Pickup_GooGun` | `0x2ADD4571` | |
| `ArkWeaponInstalaser` | Q-Beam | `Play_Player_Arm_Instalaser` | `0x4C554451` | stow |
| `ArkWeaponStunGun` | Disruptor | none | | draw |
| `ArkWeaponEMPGrenade` | EMP charge | `Play_Grenade_EMP_Select` | `0xC29E38B3` | |
| `ArkWeaponLureGrenade` | lure | `Play_WPN_Grenade_Lure_Unholster` | `0xB1D6200F` | |
| `ArkWeaponNullwaveTransmitter` | nullwave | `Play_Nullwave_Select` | `0xA5C9FDBE` | |
| `ArkWeaponRecyclerGrenade` | recycler | `Play_Recycler_Grenade_Equip` | `0x66DC62FD` | |

- **Every draw** is one or two PlaySound clips on the player, 10-16 ms in: the trigger above, and `Play_switch_weapon`.
  No clip lands later, so nothing needs a delayed replay.
- **Every stow** is one clip, `Play_Player_Unequip`, as the fragment starts (300 ms blend).
- **The extra `Play_switch_weapon`** is resolved by name and played on the player with no clip behind it, in every
  window of those weapons and no others, mostly 12-60 ms after the clips. That reads as an animation event in those
  animations, but it is not proven. The replay plays it with the rest.
- **The rest of each window** is not the weapon's: phantoms, a turret and footsteps nearby. `Play_Player_Cancel_Wrench_Charge`
  is the wrench's idle, as before.

All twelve ids are the CRC32 of the lowercased names, and each matched the id the game resolved.

### What a swap costs

From the same pass, with the animations playing:
- **Stow:** 0.48-0.68 s from the stow to the next weapon's select.
- **Draw:** the clip's stop trigger (`null`) fires as the fragment ends, which gives grenades 0.45 s, shotgun and
  Boltcaster 0.52 s, pistol 0.72 s, GLOO 0.8 s, Q-Beam 1.33 s and wrench 1.46 s. The Disruptor's clip has no stop trigger,
  so it has no reading. This is an inference from when `null` fires.

### The skip pass

18 draws skipped, all 18 finished at exactly `frames=2`, 17 instant stows, every `play` line `result=played`, and no
fault or `gone` line. Each swap went from the stow to the next weapon's skip within a millisecond. Ten classes went
through it; the pistol did not. The log can't show how it looked: missing effects, the Q-Beam firing and reloading,
and grenades throwing are still for the wearer to say.

### The worn check

`player-20261031-212949`, PreyVR-126, exit code 0, seven classes: 13 draws skipped and finished at `frames=2`, 12 instant
stows, every `play` line `result=played`, no fault. The pistol went through the skip three times, with its three-trigger
draw each time. This run didn't include a first pickup, so `Wpn_Pickup` is still only tested offline.

## As built

PreyVR-126. `src/equipment/weapon/`: `WeaponDraw`, `WeaponSound` and their `*Native.h`.
- **The skip** acts on any of the nine vtables in `WeaponClasses`. An unknown class keeps Prey's draw, and a
  mismatch in any class's slots refuses the whole lane at the gate.
- **The stow hook** is on `0x169A000`, and the wrench's old hook on `0x17392F0` is gone.
- **The sound** comes from `ClassSounds`, keyed by entity class name, with a draw list and a stow list for each of
  the eleven classes. Any other class takes `DefaultDraw` and `DefaultStow`.
- **The option** is `WEAPON DRAW ANIMATIONS` on EQUIPMENT, off by default. `weapon.draw` only reports now, since
  the menu is its switch; `SetWeaponDrawAnimations` takes "play", so it reads the opposite way from the old
  `wrench.draw`.
- **`+0x2C9`, `+0x992` and `+0x108`** are renamed and described as the corrections above say (`WeaponAttacking`,
  `stowsAttacking`). The dated wrench doc keeps its old words, with a note at its top.

Checks: 117 of 117 ctest suites, including `weapon_draw` (fake engine objects), `weapon_sound`, `weapon_draw_native`
and `re_verifiers`.

## Rejected: speeding the draw up instead

Passing a large rate through PlayAction would keep Mannequin's own Enter, Exit and sounds, with no capture and
no tables, and the wrench doc's doubt about that float is now settled. But at least one frame of the draw would
still show, and because the IK follows the prop joint, that is one frame of spin. You asked for the animation
not to play. The skip is also worn OK already on the wrench.
