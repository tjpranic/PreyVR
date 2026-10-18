# Free-swing wrench: scope

Status: **phase 2 (strikes) worn 2026-10-18 on PreyVR-38: works.** The wearer: "works
fantastically", smacking things, breaking glass and fighting mimics. Phase 3 (polish)
is next. Phase 0 (static contracts) is done. The user's four
decisions are recorded under "Decisions". It replaces
the Physical Wrench ([PHYSICAL-MELEE-2026-09-30.md](PHYSICAL-MELEE-2026-09-30.md))
entirely. It builds on the physical contact lane
([PHYSICAL-CONTACT-SCOPE-2026-10-18.md](PHYSICAL-CONTACT-SCOPE-2026-10-18.md)).

Target: Steam `PreyDll.dll` SHA-256 `7d6e322f...05311a7`. RVAs are from `0x180000000`.
Everything below was read from the Steam binary with the local Ghidra project and
capstone. Neither the EGS symbols nor the CryEngine source is on this machine, so
CryEngine names are matches by signature and layout, not by symbol. The decompiles,
scripts and their output are in
[evidence/free-swing-wrench-2026-10-18/](evidence/free-swing-wrench-2026-10-18/).

## The goal

Swinging the wrench in real life hits whatever the wrench touches, where it touches it:
- enemies take damage;
- glass breaks;
- props are knocked away;
- walls clang, with Prey's own sounds, sparks and decals.

There is no button, no Prey attack animation, and no rest or cooldown gesture.

The user's decisions so far (10-18): discard Prey's melee system for VR and go with free
swings. Physical contact never pushes live NPCs, but the wrench must hurt them.

## Why the Physical Wrench is not that

- **It asks Prey to start a real attack.** That brings the stamina check, Prey's swing
  animation and its recovery state. A swing during recovery is used up and does nothing
  (`weapon_busy`).
- **A gesture gate wraps every swing:** a 150 ms rest, 9 cm of travel and a 350 ms
  cooldown.
- **Every hit is the basic, uncharged one**, however hard the swing.
- **Contacts come from nine point rays** at the corners of the weapon box. They miss thin
  things and report hits in the box's empty corners.
- **Runtime record:**
  - PreyVR-22 (`player-20261018-140113`): 61 contacts, all fixed scenery (`type=1`,
    vtable `0x1D82188`, surface 131). One became a strike (`result=struck state=2`).
  - That strike is the only time the native hit has run from the mod's thread. It
    raised no fault.
  - Glass and enemies have never been logged.

## What the binary shows (static, 2026-10-18)

### Prey's melee chain

| Step | RVA | What it does |
|---|---|---|
| Attack (button) | `0x16B1FD0` | Stamina check `0x12618E0`, then PlayAnim `0x16B2E20`. Weapon `+0x520` is the attack state. |
| Animation events | `0x16B2290` | `Hit` / `DoubleHit` call WeaponHit. `Chain` sets `+0x506`; `Charge` sets `+0x531`. `SetParamClip` sets the component's dodge flag `+0x3F` from the target NPC (`0x13C0160`). |
| WeaponHit | `0x16B2BC0` | Scale = 1 + bonus `+0x52C` × charge fraction (`+0x524` of `+0x528`). Passes the charged flag `+0x531`, calls the component's hit, then plays a recoil animation on its result (1 or 2). |
| **Component hit** | `0x13BF730` | The hit itself (below). The component is weapon `+0x4C8`. |
| GetHits | `0x13BD620` | The native query (below). The component hit calls it. |

**The component hit reads no attack state.** Its only weapon reads are its entity
(`+0x40`), its stats (`+0x1A8`, through `0x1695020`) and one flag (`+0x1A0`). The
component reads its packages, impulse scale, fatigue and the dodge flag. No read of
weapon `+0x500..0x540` occurs. Instruction list:
[onhit_field_reads.txt](evidence/free-swing-wrench-2026-10-18/onhit_field_reads.txt).
So it can be called directly, without the attack or its animation.

**Its call** (from WeaponHit at `0x16B2C21`):
- RCX: the component;
- XMM1: the swing angle, in degrees;
- R8: the weapon;
- XMM3: the damage scale;
- the stack (`[rsp+0x20]`): the charged flag.

It returns 0, 1 or 2. A second weapon class calls the same hit, from `0x13B93E0`, with
the component at `+0x4C0`. That class is not the wrench and is unidentified.

### What the component hit does

1. **The dodge flag.** If `+0x3F` is set, it clears it and returns 0: no hit.
2. **The hits.** It calls GetHits. The mod already replaces its result with its own hit
   records (the 0x50-byte `ray_hit`, through the engine's own vector append `0x13BD250`).
3. **Once, if anything was hit:** a notification to a game manager (`0x141D5F0`,
   unidentified).
4. **For each hit:**
   1. A GLOO ball is skipped (`0x16AF300` names `ArkProjectileGooBall`).
   2. The entity behind the collider is looked up (entity system `+0xE0`; none for
      scenery).
   3. **Impulse** through the wrapper `0x16AE410`. Its strength is a weapon stat times
      `0x1694210`. That reads the object's `ability_CarryRequirement` and can be 0 for
      objects that need the Leverage neuromod.
      - A living character gets knockback through its actor (`0x1537960`).
      - Anything else gets a mass-clamped `pe_action_impulse` (`0x1231940`).
      - The mod already hooks the wrapper's direction.
   4. **The struck material** is resolved for effects (`0x16AEAF0`), for scenery,
      terrain and entities.
   5. **Damage**, when the collider has an entity.
      - It builds a hit record: shooter `0x7777` (the player), target, weapon, point,
        normal, part and angle.
      - It applies the stealth and sneak-attack multiplier (`0x16AF0C0`) and critical
        bookkeeping (`0x16AF980`).
      - It sends the damage package through the signal system (`0x1608860`, which logs
        "Signal System: %llu is not a valid package id."), scaled by damage scale ×
        multiplier.
      - The package is picked from the component: `+0x00` normal, `+0x08` critical,
        `+0x10` charged, `+0x18` charged critical. Critical comes from the weapon's
        `+0x1A0` flag test.
   6. **An artificial collision.**
      - `0x16AEF20` fills a CryEngine `EventPhysCollision` (layout matched field by
        field, down to `fDecalPlacementTestMaxSize` = 1000):
        - the player's physics and the weapon entity;
        - the collider and its foreign data;
        - point and normal;
        - velocity = the swing direction × 100 m/s;
        - mass = a weapon stat and the target's mass;
        - parts, and the weapon's and the hit's surfaces;
        - radius = the weapon's size.
      - It hands the event to the physics world's slot `+0x2E8` (`0xB9F370`, CryEngine's
        `AddDeferredEvent`) as type 2, a collision. That logs it as if the physics had
        produced it, under `pef_log_collisions` (`0x2000000`).
      - This is CryEngine's melee trick for breaking things: the game's collision
        listeners then play the hit sound, particles and decal, and break glass.
   7. **The surface's breakability.** Surface type `+0x38`, CryEngine's
      `GetBreakability`, is checked. A breakable surface (glass) makes the hit return 0,
      so Prey's wrench does not recoil off glass.
5. **Stamina, after the loop, if it returns 1 or 2:**
   - fatigue = damage scale × the component's `fatigueThisHit` (`+0x38`);
   - passed to `0x1260D10`, with the UI preview through `0x1261320`;
   - the stamina object is the player (`0x157C990`, actor `0x7777`) `+0x678`, then
     `+0x88` (`0x1587C20`);
   - `0x12618E0` reports it drained and sends the HUD `staminaDrained`. It is the check
     the button attack makes first.

**Direction.**
- Natively the hit's direction is the player's rotation, turned by the angle about its
  up axis.
- The impulse wrapper negates what it is given, and the event is given that negation.
  So both end up along one direction: the swing's.
- The free swing replaces it in both places with the wrench's real motion.

**Native GetHits, for comparison.**
- **The query:** a 10 cm box swept forward from the eye. It reaches a weapon stat plus
  the forward speed × `+0x30`, at most `+0x34`. The broadphase classes are `0x117`
  (static, sleeping and awake rigid, independent, terrain).
- **Line of sight:** a hit on an entity counts only if a ray from the eye to the point is
  clear (`RayWorldIntersection`, skipping the player and that collider).
- **Choice:** it keeps one target. A living hostile in the box wins (`0x147D2F0`);
  otherwise it takes what one forward ray of `rayRange` (`+0x2C`) hits.

**Glass.**
- Prey has its own: `CArkBreakableGlass`, an `ArkBreakableGlass` render node (type 26),
  the `ArkGlassBreaker` entity, `ArkLooseGlassFrag`, and the cvar
  `e_ArkGlassMinImpactRadius`. List:
  [glass_strings.txt](evidence/free-swing-wrench-2026-10-18/glass_strings.txt).
- Which listener breaks it is not traced. The design does not need it: the component hit
  sends the same event Prey's own wrench sends.

**What dropping the animation gives up:**
- the swing's whoosh;
- the recoil and chain animations;
- the charge hold;
- the native reach: Prey's wrench hits up to `rayRange` ahead of the camera, whatever
  the model touches.

## Design

### Prey's own hit, without its attack

Each strike calls the component hit directly, with the mod's contacts as its hits. One
call gives what Prey's wrench does:
- damage, with every neuromod and difficulty modifier;
- sneak attacks and criticals;
- knockback, and the impulse rules (the Leverage exemption);
- hit sounds, sparks and decals;
- glass;
- stamina.

The alternative is rebuilding the hit from its parts (signal package, impulse, collision
event, stamina). That is six or more new native contracts, and each would have to match
Prey's own bookkeeping. Rejected.

### Finding the strike

- **The query.** It is the contact lane's: the published weapon box, swept from last
  frame's pose to this one (`CollideEntityWithPrimitive`). The wrench adds:
  - the native broadphase classes `0x117`, which add fixed scenery, glass and terrain,
    and likely live characters' skeletons (the independent class: inference, see risks);
  - classification by GetType (`+0x08`) instead of by the two pushable vtables;
  - a 64-entry buffer, since static meshes crowd a room.
  - The player and the carried object are skipped, as now. GLOO is skipped by the native
    hit itself.
- **The speed.** Measured at the contact point, from the tracking-space samples the
  wrench already publishes. Walking and snap turns therefore do not count. A strike
  needs that point to move faster than **Minimum Swing Speed**.
- **One hit per target per pass.** A struck target is locked until the wrench has been
  off it for 150 ms, so a pass through a body is one hit. Several targets in one swing
  each get theirs.
- **No hits through walls.** A ray through fixed scenery only, from the head to the
  contact, must be clear. That covers a wrench pushed through a wall to reach someone
  behind it, the free-swing version of Prey's own eye check. The phase 5 lesson applies:
  only static classes are asked, so the wrench and hands cannot block it.
- **Stamina.** When drained (`0x12618E0`, which also flashes Prey's drained HUD), the
  swing does not strike. It still pushes props through the contact lane. (Decision 2.)

### The strike

1. Clear the dodge flag (`+0x3F`). With no Prey clips playing, it can only be stale.
2. Set the request: the weapon, the hits, and the swing direction and speed.
3. Call the component hit: angle 0, scale and charged from the speed (decision 3).
4. The hooks:
   - **GetHits:** the request's hits, through the engine's append (already built and
     worn).
   - **Impulse wrapper:** the swing's direction (already built).
   - **New, the collision builder `0x16AEF20`:** the swing's direction, so the event's
     velocity follows the wrench.
5. Ignore the return value: there is no recoil animation to play.
6. A haptic pulse, stronger for a harder hit.

### Pushing and striking together

The contact lane keeps pushing what the wrench rests on or slides into. When a swing
strikes a prop or corpse, the lane leaves that object alone for 10 frames. The native
impulse then defines the knock, instead of being overwritten by a velocity match.

### The trigger

Prey's own wrench attack is blocked while the free swing is on (decision 1). It would
otherwise play the animation and hit ahead of the camera.

### Replaced and kept

**Removed:**
- the Swing gesture gate;
- the nine-ray sweep;
- the attack and animation-hit hooks, and the hit-ownership registry;
- the strike through Prey's attack.

**Kept and reused:**
- the weapon box publication (the contact lane uses it);
- the GetHits and impulse hooks;
- the verified rows in `PhysicalNative.h`;
- the `preyvr_physical` log channel;
- the debug overlay's physical layer, redrawn for strikes;
- the contact lane's query, point velocity and haptics.

**The options:**
- Physical Wrench (`physical_melee`) becomes **Free-Swing Wrench**.
- Minimum Swing Speed (`swing_speed`) keeps its key.
- Both move to the Hands tab when the feature is worn.

## Phases

| Phase | What | Proof | Size |
|---|---|---|---|
| 0. Static contracts | **Done 2026-10-18** (below). | Offline bytes. | S |
| 1. Strike detection, diagnostic only | **Worn 2026-10-18 on PreyVR-37: works** (below). The wrench query (`0x117`), speeds, locks and the line-of-sight ray. **No native call.** Log each would-be strike: type, vtable, foreign-data kind, surface name and breakability, speed, decision. Log once per equip the weapon's charge fields (`+0x524..+0x52C`), the component's fatigue and packages, and the impulse and mass stats. Overlay marks. | Headset: swing at a wall, glass, a prop, a corpse, a live enemy and the air. | M |
| 2. Strikes | **Worn 2026-10-18 on PreyVR-38: works** (below). The direct component hit, the third hook, the speed tiers, stamina, the trigger block, the push hand-off. The old lane's gate, rays and attack hooks are removed. | Headset: damage, glass, sounds and decals, stamina use, knockback, one hit per pass, no faults. | M |
| 3. Polish | Options on the Hands tab and their defaults, the player guide and tuning commands, quieter logs. | Headset regression. | S–M |

Not in scope:
- the wrench stopping at walls (no visual clamp);
- a swing whoosh (possible later, through Prey's audio);
- a wrench in the left hand;
- gun melee;
- pushing NPCs.

## Risks and what phase 1 settles

- **Live characters in the query.**
  - Phase 4 never met a live skeleton through the rigid classes (`hosted=0`).
  - The native `0x117` adds the independent class (`0x10`), which in CryEngine holds
    articulated bodies with a host. That is an inference.
  - If they do not show, the living class (`0x08`) is next.
- **What glass is to physics:** its class, foreign data and surface. Phase 1 logs it
  before anything strikes it.
- **Damage from the mod's thread.**
  - The lane runs on the input drain, which is meant to be the engine's main thread
    (`InputPost.cpp` logs its id at start). Animation events are expected there too, but
    that is an inference.
  - Only a strike on scenery has run there so far. Enemy damage is new in phase 2.
- **The 100 m/s event on every strike.** A soft strike still breaks glass and plays a
  full hit. That is Prey's own value; the speed gate is what keeps soft taps out.
- **The native impulse versus the contact push.** If the native knock is too strong or
  too weak for props, the fallback is to let the contact lane alone move props and
  corpses. Then the native impulse only knocks back characters.
- **Fast flicks.** The sweep moves last frame's box along its centre's path, without the
  turn. A wrist flick moves the head farther than the centre: a 10 m/s head travels about
  11 cm a frame. Phase 1 compares the head's speed with the centre's. The fix, if
  needed, is a second sweep for the head.

## Decisions (user, 2026-10-18)

1. **The trigger:** does nothing while the wrench is out. Prey's attack is blocked.
2. **Out of stamina:** Prey's rule. The swing does not strike, but still pushes props
   through the contact lane.
3. **Damage and speed:** two tiers.
   - From the minimum speed, a normal hit scaled 0.5–1 by speed.
   - Above a heavy-swing speed, Prey's charged hit with its charge bonus.
4. **Stamina cost:** as Prey charges it: per hit, scaled with the hit. Misses are free.

## Phase 0 results (2026-10-18)

Static only, from the Steam binary. `python -E -B tools/re/verify_free_swing.py` checks all
of it, 85 checks:
- 6 complete bodies (no base relocations, with mutation controls);
- the rows it shares with the other tables, and none hashed twice;
- 10 vtable slots;
- 62 instruction witnesses;
- 6 cross-checks: call targets, the 100 m/s constant, the deferred-event switch, the
  material-manager chain, and the HUD string.

[Output](evidence/free-swing-wrench-2026-10-18/verification.json). `ctest` 83/83 with the
new `swing_native` test (rows gated and unique across all four native tables).

**New rows** (`SwingNative.h`):
- the collision builder `0x16AEF20`, hooked in phase 2;
- `AddDeferredEvent` `0xB9F370`;
- `SendSignalPackage` `0x1608860`;
- the stamina check `0x12618E0` and charge `0x1260D10`;
- `GetSurfaceType` `0x24E270`.

**Rows kept where they are:**
- `PhysicalNative.h`: the component hit `0x13BF730`, GetHits, the hit append, the
  impulse wrapper, the attack (now to be blocked) and WeaponHit;
- `SceneQueryNative.h`: the player singleton `0x157C990`;
- `ContactNative.h`: the broadphase and the per-entity collision.

**The direct call.**
- **Arguments:** the hit keeps the component from RCX, the weapon from R8, the scale from
  XMM3 and the angle from XMM1.
- **The charged flag** is its 5th argument (entry `[rsp+0x28]`, read at `0x13BFA50`).
  With the critical test, it picks the package at `+0x00` (normal), `+0x08` (critical),
  `+0x10` (charged) or `+0x18` (charged critical).
- **The dodge flag** `+0x3F`: its only writer on this path is the `SetParamClip` handler
  `0x13C0160`.
- **Prey's own call:** WeaponHit's scale reads `+0x524`, `+0x528` and `+0x52C`, and its
  charged flag `+0x531`.

**The collision.**
- **The builder:**
  - keeps its direction from R8, multiplies it by 100.0 and writes it to `vloc[0]`
    (`+0x54`);
  - marks the first entity as an entity (`+0x30` = 2);
  - when its dispatch flag is set, calls the physics world (`0x224D9C8`) at slot
    `+0x2E8` with type 2.
- **The receiver:** that slot is `0xB9F370`. Its switch takes types 0–14; case 2 logs
  the event through `0xB9E160` with `0x2000000`.
- **The hook's arguments** (from the builder's stack reads): `(event, hit material,
  direction, weapon entity, player physics, weapon mass, radius, dispatch, weapon
  surface)`.
- **The builder's callers:** two, OnHit (`0x13BFE4A`) and `0x167E920` (`0x167EFBD`,
  unidentified). The hook passes the latter through.

**Stamina.**
- **The object:** the hit finds it as the player singleton, then `+0x678`, then the
  accessor `0x1587C20` (`mov rax,[rcx+0x88]`).
  - The frame's player is the same singleton (the scene query checks it), so the lane
    reads player `+0x678` → `+0x88`.
- **Drained:** the check compares `+0x14` with `+0x10`; equal means drained. It returns
  false and sends the HUD `"staminaDrained"`.
- **The cost:** the hit's fatigue is scale × component `+0x38`, through `0x1260D10`.

**Surface names, for the phase 1 log.**
- **The material manager:** C3DEngine's GetMaterialManager (`0x1C912A0+0x460` →
  `0x20A540`) returns the global `0x243A6D0`.
  - Its constructor (`0x247080`, stored at `0x2047A7`) installs `CMatMan`'s vtable
    `0x1C9D0B8`.
  - Its `+0xA0` returns the surface type manager at `+0xE0`. Its `+0x98` jumps to the
    manager's `GetSurfaceType` `0x24E270`, which answers ids 1–254, else `mat_default`.
- **The surface type:** the manager's constructor (`0x24D960`, vtable `0x1C9D878`)
  creates `mat_default` as a `CMaterialSurfaceType`, vtable `0x1C9D8B8`. Its slots read:
  - id `+0x18`;
  - name `+0x08` (`+0x18`);
  - flags `+0x1C` (`+0x28`);
  - breakability `+0x68` (`+0x38`, the slot the hit tests).
- The lane will check each of those vtables before reading a field.

**Classes.**
- **Static scenery:** its GetType (`0x1D82188+0x08` → `0x1A5A880`) returns 1.
- **Prey's melee broadphase** asks for `0x117` (`0x13BDCCD`).
- **Live skeletons:** whether they are in the independent class is not visible in the
  articulated SetParams. Phase 1 logs it.

## Phase 1: strike detection, diagnostic only (2026-10-18, package PreyVR-36)

**What runs.** `src/dll/SwingLane.cpp`, once per new tracking sample while the wrench is
drawn, on the input-drain thread, between the old physical lane and the contact lane. It
calls nothing in Prey that acts. It always runs (there is no option yet); `swing log 0`
quiets its event lines.

1. **Motion.** The weapon box now also carries its points in tracking space, the map
   that placed them, the grip and the weapon (`WeaponBox`).
   - Last sample's points go through this sample's map. The motion is the arm's alone:
     walking and snap turns add nothing.
   - Samples 1/240 to 1/20 s apart give speeds; otherwise the overlap test runs alone.
2. **The query.**
   - The broadphase asks for Prey's own melee classes (`0x117`), into a 64-entry buffer.
   - It skips the player, the carried object and anything that is not static, rigid or
     articulated (counted as `other`; living bodies are not in `0x117`).
   - Each object gets three box tests: still at this pose (overlap); last pose's box along
     the centre's path (sweep); and the head's 12 cm (the end away from the grip) along
     its own path (head).
   - Per object it keeps the first surface the motion reached, else the overlap.
3. **The decision**, per object, in this order:
   - `locked` if the object was struck this pass;
   - `too_slow` under Minimum Swing Speed (`swing_speed`, at the contact point);
   - `drained` if stamina is drained (read: the two floats compared, without Prey's HUD
     call);
   - `blocked` if a scenery-only ray from the head to 2 cm short of the contact, skipping
     the target, hits something, or is refused;
   - else `would_strike`.
   - The tier (decision 3): normal at 0.5–1 scale up to 5 m/s; heavy from 5 m/s, adding
     the weapon's own charge bonus by 7.5 m/s.
   - A pass ends 150 ms after the last contact, timed by the tracking samples.
4. **Names, for the log.**
   - The object: class, vtable, foreign-data kind; and if an entity, its id, class and
     name; for a body, whether it has a host.
   - The surface: id, name, breakability and flags (the phase 0 chains, each vtable
     checked first).
   - The wrench, once per equip: charge time, full charge, charge bonus, charged flag,
     fatigue per hit, dodge flag and its four damage packages.

**The log** (`preyvr_swing`):
- once: `gate`, `settings`, and `wrench` per equip;
- per pass: `begin` (what, surface, kind, speeds, decision);
- per would-be strike, and the first `blocked` or `drained` of a pass: `strike`
  (`tier`, `scale`, `stamina`, `sight`, `blocker`);
- per swing: `swing` (`peakHeadCms`, `peakCentreCms`, contacts, strikes);
- the `swing` report every 10 s when it changes.

**The overlay** (physical layer, while Physical Wrench is off):
- the wrench's box;
- the swing's trails while the head moves fast;
- the frame's most notable decision for 3 s, labelled with surface and speed;
- status `swing (diagnostic)`.

**Also changed:**
- The scene query gains a scenery-only segment that also skips a given object
  (`QuerySceneryBetween`; `scene::Build` takes the classes, default unchanged).
- The old lane draws its overlay only when it is on.
- The weapon box is published while this lane wants it.

**Tests** (`ctest` 85/85):
- **`swing_strike`:** the head box, the tiers and scales, approach speed, and passes.
- **`swing_lane`:** the lane with Prey faked at its call boundary:
  - Prey's classes into 64, and the player, carried and living objects skipped;
  - three tests a moving frame;
  - a slow touch is `too_slow`, logged once;
  - 3 m/s strikes at scale 0.74 with a clear sight, the ray skipping the target and
    stopping 2 cm short, and the overlay marking it;
  - one strike per pass, and the next pass strikes again;
  - `blocked` and an unanswered sight refuse;
  - `drained` refuses without a ray;
  - glass by name and breakability, and a heavy 6.25 m/s strike at scale 1.25;
  - a prop's class and name, found by the head's sweep;
  - a body's host;
  - one lookup per surface;
  - the menu, other-weapon, same-sample and overflow gates, and the old lane keeping
    its overlay;
  - quiet logging, and the report.

**Headset checklist** (Debug overlay on, Physical Wrench off; draw the wrench; Minimum Swing
Speed as set, 150 cm/s in your settings):
1. **A wall, a table, a crate:** tap each gently, then swing hard. Expect a grey mark
   (`too_slow`) for a tap and a green one (`would_strike`) for a swing, one per swing.
   Nothing should take damage or break: this build only logs.
2. **Glass:** a window or a glass panel, gently then hard. The log should name its surface
   and show `breakable=1`.
3. **A corpse and a live enemy** (a mimic is ideal): swing through them. Note whether the
   live one shows a mark at all. If it does, its `begin` line names its class and `host=1`.
4. **Through a wall:** reach the wrench through thin scenery at something behind it. It
   should be `blocked`.
5. **Out of stamina:** drain it (sprint, or attack with the trigger: Prey's own wrench
   still works in this build), then swing at a wall. Expect `drained`.
6. **Fast flicks** of the wrist at a thin object, and swings in the air.
7. Afterwards: the `swing` report (`wouldStrike`, `passes`, `headTouches`, `usAvg`/`usMax`).

### PreyVR-36 in the headset (2026-10-18, `player-20261018-194408`)

The wearer saw the overlay but found it hard to tell, mid-swing, whether contacts were
landing. They saw a persistent contact on their own character.

**Their own character.**
- One articulated body was in the query every frame (`articulated=` equalled `frames=`):
  class `ArkPlayer`, name `Dude`, entity id 30583 (`0x7777`, the player's own id), host
  set, surface `mat_Flesh_Human`. That is the player character's own hit skeleton.
- The contact lane never met it (`hosted=0`): it is not in the movable classes. It is in
  Prey's melee classes, so in the **independent** class. Live characters' skeletons
  should be there too, which is the question phase 1 asked. Not tested yet: there were
  no enemies in this run.

**Found as expected:**
- **Glass:** the window is static, entity class `ArkSceneWindow` (`Looking_Glass_Frame_A179`),
  surface `mat_Glass_Looking`, `breakable=1`. Found by sweeps and overlaps; 9 would-be
  strikes.
- **Scenery:** `mat_Metal_Thin_Medium` and `mat_Wood_Medium`, with no entity.
- **Entities:** a workstation and its screen (`ArkWorkstationScreen`, `ArkWorldUI`,
  `mat_Glass_Healing`, not breakable).
- **Props:** `RigidBodyEx` (a couch, a pizza box, a soldering iron, a magnifier, an
  operator part).

**Speeds.**
- **Swings:** heads peaked at 10–19 m/s; slow ones at 1–3 m/s.
- **Would-be strikes:** 55, at 1.5–13 m/s where they touched (median about 6). With heavy
  from 5 m/s, 34 were heavy, most at the full bonus.
- **The head's sweep:** it found 115 touches the others did not.
- **Cost:** 21 µs a frame on average, 230 µs at most.

**The wrench's own fields:**
- charge full 1.5 s, charge bonus 1.0: a fully charged native hit is twice the damage;
- four damage packages;
- **fatigue per hit 0.000**.

**Why fatigue was 0.** Prey sets it per attack. The component's fatigue setter
`0x13BFFF0(component, charged, chargeScale)`:
- reads the player's stamina object (`+8`, a flag) and the player's stat for that kind
  of hit (normal, charged or the flagged variant, through `player+0x7C0`), so neuromods
  change it;
- writes it to `+0x38`;
- previews it on the HUD.

The wrench calls it when an attack starts (`0x16B0590`, `0x16B0630`). A free strike must
call it first (phase 2), or every hit costs no stamina. Witnessed at `0x13C0058`.

**Changed for PreyVR-37:**
- **The player's own skeleton is skipped.** An articulated body is skipped when its
  host is the player's physics or its entity is `0x7777`; it is counted as `self`. A
  `self ... byHost= byEntity=` line logs once which test caught it.
- **A strong pulse on each would-be strike:** a new haptic event, `WeaponStrike`, at
  full amplitude for 45 ms, against the contact pulse's 0.55 and 35 ms. So a strike
  can be felt. Phase 2 strikes on exactly these.
- **Heavy from 7 m/s, the whole charge bonus by 10 m/s,** after this run's speeds.

`ctest` 85/85; the verifier gains the player id and the fatigue setter.

**PreyVR-37 checklist:**
1. Draw the wrench and stand still. Your body should no longer show a mark.
2. Swing at a wall, glass and a prop. Expect one strong buzz per swing that lands, and
   none for a gentle tap.
3. **Enemies, the open question:** swing at a live enemy and a corpse. Does the live one
   buzz?
4. Reach through scenery at something behind it: no buzz (blocked).

### PreyVR-37 in the headset (2026-10-18, `player-20261018-195454`, `player-20261018-195708`)

Two runs: a mimic in the first, ordinary things in the second. The wearer confirmed the
persistent contact on their own character is gone.

**The player's own skeleton** was skipped every frame (`self=` equalled `frames=`). The
`self` line shows both tests caught it: its host is the player's physics, and its
entity is `0x7777`.

**Live enemies are found.**
- The mimic is an articulated body with a host, class `ArkMimic`
  (`SpawnedArkNpcs.Mimics.Mimic1`), surface `mat_Flesh_Mimic`. So live skeletons are in
  the independent class, as the player's own showed, and Prey's melee classes (`0x117`)
  reach them.
- Five passes on it struck different parts (part ids 48, 35, 1, 20, 24) at 1.7–13 m/s,
  three of them heavy. It also stayed in contact a long while (`locked=500` in that run).

**Everything else found:**
- a human corpse: articulated, no host, `ArkHuman`, `mat_Flesh_Human`;
- GLOO: `ArkScalableBreakable` (`Gameplay.Goo_Breakable27`), `mat_Gloo_Hard`;
- glass again: `ArkSceneWindow`, `mat_Glass_Looking`, breakable;
- concrete, metal and wood scenery;
- a prop (`RigidBodyEx`) and a lamp (`ArkLight`).

**Numbers:**
- **Strikes:** 25 would-be strikes in the two runs, 11 heavy.
- **Swings:** head peaks of 1–24 m/s.
- **Cost:** 4–6 µs a frame on average, 94–162 µs at most.
- **Stamina** read `0.0/100.0` throughout (the first float `0`: not drained).

**Not exercised:** `blocked` (a reach through scenery) and `drained`.

**Phase 1 is done:** the query, the decisions and the names are what phase 2 strikes on.

## Phase 2: strikes (2026-10-18, package PreyVR-38)

**What a strike is now.** Each contact the phase 1 rules strike (fast enough, the
first of its pass, a clear line from the head) calls Prey's own hit.
1. **Stamina:** Prey's check (`0x12618E0`). Drained, it flashes Prey's HUD and nothing
   strikes (decision 2). The pass is spent either way: one try per pass.
2. **Fatigue:** the fatigue setter (`0x13BFFF0`), as Prey's attack calls it, with the
   heavy tier as "charged" and the wrench's own charge bonus. So the hit charges
   Prey's own fatigue for that kind of hit, times the strike's scale (decision 4).
3. **The dodge flag** (`+0x3F`) is cleared.
4. **The hit:** the component's hit (`0x13BF730`) with angle 0, the weapon, the
   strike's scale (0.5–1 normal, up to 1 + the bonus heavy) and charged = heavy.
5. **The hooks it reaches:**
   - **GetHits** answers with the one contact, allocated by the engine's own append.
   - **The impulse wrapper** gets the swing's reverse; it negates it, so the push
     follows the swing.
   - **The collision builder** gets the swing's direction, so the 100 m/s collision
     (sounds, sparks, decals, glass) follows the swing too. It is told apart by the
     weapon's entity (`+0x40`).
6. **Afterwards:** a strong pulse (`WeaponStrike`). A struck prop or body is left to
   the hit for 150 ms: physical contact skips it (`struck=` in the contact report),
   so a push cannot overwrite the knock.

**The trigger:** Prey's attack (`0x16B1FD0`) does nothing with the wrench out while
Free-Swing Wrench is on (decision 1); other weapons attack as ever. The four hooks go
in together the first time the wrench is out, before that frame's query. If they fail,
the lane stops and the trigger stays Prey's.

**The option:** Free-Swing Wrench (`free_swing`), Experimental tab, **on by default**.
Minimum Swing Speed (`swing_speed`) stays. `physical_melee` is a retired key.

**Removed:**
- The Physical Wrench: its gesture gate, nine rays, attack and animation-hit hooks, hit
  ownership, options and the `physical` command.
- What remains of `PhysicalInteractions.cpp` is the weapon box, now with its
  tracking-space points, grip and weapon.
- The physical layer of the overlay is the swing lane's alone.

**Logs:**
- `strike ... result=struck`, with `returned` (Prey's hit's return), `fatigue` (what the
  setter put per unit of scale) and `stamina` before->after;
- `result=failed why=not_wrench|no_hooks|no_stamina|fault`;
- `hooks result=0 count=4` once;
- in the report: `struck`, `failed`, `hooks`, `attacksBlocked`, `substituted`,
  `impulses`, `collisions`, `handoffs`.

**New contracts:**
- the fatigue setter's row (7 rows in `SwingNative.h`);
- its witnesses: the wrench's call (RCX the component, DL the charged flag, XMM2 the
  bonus) and its own prologue;
- the weapon's entity at `+0x40`.

The verifier has 107 checks.

**Tests** (`ctest` 85/85):
- **`swing_lane`** drives the strike through fakes of the hit, the setter, the stamina
  check and the hooks' originals:
  - the hit is called once on the component, uncharged, at scale 0.66, after the setter
    (uncharged, bonus 0.5) and with the dodge flag cleared;
  - GetHits answers with the contact and never reaches Prey's;
  - the impulse wrapper gets the reverse, the collision the swing;
  - one strike per pass;
  - outside a strike, all four pass through;
  - the trigger does nothing with the wrench, works with another weapon or the option
    off;
  - drained: one stamina check, no hit;
  - heavy: charged hit and charged fatigue (25), at scale 1.25;
  - props and bodies handed to the hit for 150 ms, scenery not;
  - not the wrench: `failed`;
  - hooks that fail: the lane stops and the trigger is Prey's.
- **`physical_runtime`** now tests the weapon box's publication.
- **`physical_interactions`** tests its geometry and the scenery-only query.
- The options tests cover `free_swing` on by default and `physical_melee` retired.

**Headset checklist** (Experimental: Free-Swing Wrench on, its default; Debug overlay
optional):
1. **The trigger:** with the wrench out, press attack. Nothing should happen. Draw a gun:
   it fires.
2. **Scenery:** hit a wall and a table. You should hear Prey's hit sound and see sparks
   or a decal, and feel a strong buzz. A gentle touch does nothing.
3. **Glass:** a window. It should break.
4. **Props:** a can or box should fly off along your swing, harder for a harder swing.
5. **A mimic or other enemy:** it should take damage, react and die in a few hits. A
   fast swing should hit noticeably harder. Note how many hits it takes.
6. **A corpse:** it should be knocked along the swing.
7. **Stamina:** watch the stamina bar drop with each hit. Drain it and swing: Prey's
   stamina warning should flash, and nothing should strike.
8. **Through a wall:** reach through thin scenery at something behind it. No strike.

Tell me what happened for each. The log has a `strike` line per hit.

### PreyVR-38 in the headset (2026-10-18, `player-20261018-202031`)

The wearer: "The free swinging wrench works fantastically. I was able to smack things,
break glass and fight mimics."

**The log:**
- **Strikes:** 54, 33 of them heavy. None failed, no faults.
- **The hooks:** `hooks result=0 count=4`. Each strike went through all three in-hit
  hooks (`substituted`, `impulses` and `collisions` all 54).
- **The trigger** was pressed and blocked 26 times.
- **What was struck:**
  - mimics 15 times (`ArkMimic`), plus mimic limbs and giblets afterwards;
  - concrete, wood and metal;
  - glass: plain `mat_Glass`, a `GeomEntity` pane and the `ArkSceneWindow`;
  - props, porcelain and GLOO;
  - a workstation and its screen.
- **Refusals:**
  - `blocked`: 7 frames in 2 passes, both at invisible collision (`mat_nodraw`) with
    scenery (surface 140) between it and the head;
  - `drained`: 5 times, with stamina at `100.0/100.0`.
- **Hand-offs:** 31 struck props and bodies were left to the hit. The contact lane
  skipped 69 frames of pushes for them.
- **Cost:** 2 µs a frame on average, 175 µs at most.

**What Prey's hit returned:**
- 1 for most strikes;
- 2 for a living hostile;
- 0 for glass, Prey's breakable-surface rule. Those strikes charged no stamina.

**Stamina:**
- **The pair:** the first float is the fatigue spent, the second its most (100).
- **The cost:** the fatigue setter put 17.5 per hit, normal or charged. Each hit spent
  scale × 17.5: 10 for a gentle normal hit, up to 35 for a full heavy one.
- **Draining:** three full heavy hits drained the player.
- `SwingNative.h` now says what the pair is.

## Evidence

## Evidence

[evidence/free-swing-wrench-2026-10-18/](evidence/free-swing-wrench-2026-10-18/):
- `native_melee_decompile.c`: Ghidra's decompile of the chain above and its callees;
- `onhit_field_reads.txt`: every field the component hit reads, and its calls;
- `constants_and_call_sites.txt`: the floats it uses (100 m/s, the half-degree factor,
  0.05 m) and the WeaponHit call;
- `skip_flag_writers.txt`: the dodge flag's writers;
- `xrefs.txt`, `glass_strings.txt`;
- the scripts that wrote them: `ghidra_decompile.py` (set `GHIDRA_INSTALL_DIR`; run with
  the pyghidra venv) and `w_*.py` (capstone, through the contact evidence's
  `re_disasm.py`);
- phase 0: `p0_rows.py` (the six rows), `p0_bytes.py` (instruction bytes for the
  witnesses), `p0_vtables.py` (vtables that hold a function), and `verification.json`.
