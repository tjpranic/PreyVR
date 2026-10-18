# Physical contact: scope for pushing objects with hands and weapons

Status: **phase 0 (static contracts) done 2026-10-18**: `include/preyvr/ContactNative.h`,
`tools/re/verify_physical_contact.py`, `contact_native` test (see "Phase 0 results"). No
runtime code calls them yet. It replaces
Object Nudges, removed the same day (see the end of
[PHYSICAL-MELEE-2026-09-30.md](PHYSICAL-MELEE-2026-09-30.md)). The deferred wrench rebuild
will reuse the query layer described here.

Target: Steam `PreyDll.dll` SHA-256 `7d6e322f...05311a7`, RVAs from `0x180000000`.
Neither the CryEngine source nor the EGS symbol build is on this machine, so everything
below was read from the Steam binary. CryEngine names are matches by signature, not
proof by symbol. Scripts and output are in
[evidence/physical-contact-2026-10-18/](evidence/physical-contact-2026-10-18/); they need
capstone, which this session installed in a scratch venv only.

## The two requirements

1. **Continuous contact.** On every frame a weapon or hand touches a movable object,
   move the object so its surface keeps up with the hand along the contact, scaled by
   the object's mass. No swing gesture, no rest, no cooldown, no one-shot tap.
2. **Overlap detection.** Find contacts with a volume, not with nine sampled point paths:
   catch a weapon or hand touching an object, held against it, or already inside it, and
   report the object, point, normal and depth.

## What the binary already shows (static, 2026-10-18)

| World slot | Target | What it is | Evidence |
|---|---|---|---|
| `+0x110` | `0xBA8560` | **GetEntitiesInBox** (broadphase). Arguments `(world, Vec3* min, Vec3* max, IPhysicalEntity*** list, int objtypes, int prealloc)`, returns the count. Takes the world lock at `world+0x5784`, then calls `0xBA6830` with caller index 4, the same index as our ray query. The list is engine-owned; the native caller copies it at once. | The native melee `GetHits` call at `0x13BDD4D`; the body. 38 calls through the world global. |
| `+0x1D0` | `0xBA1D80` | **Per-entity primitive collision** (CryEngine's `CollideEntityWithPrimitive` by shape). Arguments `(world, entity, int itype, primitive*, Vec3* dir, ray_hit* out, intersection_params*)`, returns 1 on a hit; `out` is the 0x50-byte `ray_hit` the mod already uses. The body refuses an entity whose `+0x44` top byte is 5. | Native melee at `0x13BDE06`: one call per broadphase entity, reading the hit point at `out+0x24`; the body (Phase 0 results). |
| `+0x1E0` | `0xBAA8B0` | **PrimitiveWorldIntersection** (one call for the whole world). 37 consumers of its name tags, 49 `PrimitiveWorldIntersection(Game)` call sites. | Name tags; a game helper at `0x1259C40`, beside the carry code. |
| `+0x118` | `0xCCF250` | RayWorldIntersection, already used (`SceneQuery.cpp`). | R-127. |

**The native wrench already sweeps a volume.** `GetHits` builds an oriented box
(`itype 0`): `Matrix33 Basis +0x00`, `bOriented +0x24 = 1`, `centre +0x28`, half-sizes
`+0x34` (5 cm, so about 10 cm across, by the reach). It broadphases with `objtypes 0x117`, then
collides that box, swept along the swing, with each entity. That is the pattern
requirement 2 needs, with the entity known per call.

**PrimitiveWorldIntersection layout, from the game helper `0x1259C40`.**
`SPWIParams` is 0x78 bytes, zeroed. It opens with the same 0x18-byte base as the ray
query's parameters: `itype +0x18`, `pprim +0x20`, `sweepDir +0x28`, `entTypes +0x34`,
`ppcontact +0x38`, `geomFlagsAny +0x44`, an int `+0x50` (1 here), a pointer `+0x58`,
collision class `+0x60/+0x64` (bit `0x400000` set, as in the ray query), and a pointer
`+0x70`. The call is `(world, &params, lock = 0, nameTag)` and returns a float (> 0 on a
hit). `geom_contact` starts `double t +0x00`, `pt +0x08`, `n +0x14`, matching CryEngine's.
Unresolved: how a contact names its entity, and what the return value counts.

Already proven by earlier work and reusable as is: `pe_status_dynamics` (v, w, mass) and
`pe_action_set_velocity` (type 10, 40 bytes, queued) through the generic physics vtable
(`CarryLane.cpp`, worn in throws); rigid body Action / impulse fields (`PhysicalNative.h`);
skipping the player's physics in a query (`SceneQuery.cpp`); the weapon box and its pose
(worn 10-18: it sits on the weapon).

## Design

**Query layer (requirement 2).** Per frame, on the input-drain thread, for each active
proxy:
1. Broadphase `GetEntitiesInBox` over the proxy's box at the last and current poses, plus a
   margin, asking only for `ent_rigid | ent_sleeping_rigid` (no static, terrain or living).
   Copy the list at once and skip the player and the carried object.
2. For each entity, by physics type: rigid (2) always; articulated (6, corpses) from phase 4;
   anything else skipped. Collide the proxy box with it (`+0x1D0`): once swept along this
   frame's motion, so a fast swat cannot pass through, and once still, at the current
   pose, so a hand held against or inside an object still reports it.
3. Output per entity: collider, part, point, normal, depth or sweep distance.

The broadphase plus per-entity route is preferred over `+0x1E0`. It is the game's own
melee pattern, its output layout is the known `ray_hit`, and the entity comes from our own
loop. `+0x1E0` stays the fallback if per-entity calls turn out too slow.

**Proxies.**
- **Weapon:** the existing box, any weapon, right hand.
- **Right hand:** a palm box (about 9 x 3 x 10 cm) from the grip pose, when no weapon is drawn.
- **Left hand:** the same palm box, when it is not carrying, aiming psi, on the medkit
  slot or on the foregrip.

Hand velocity is the runtime's controller velocity (the throw's `MotionHistory`), turned
to world space and added to the player's own velocity: walking into a can with your hand
out pushes it, and a snap turn alone does not.

**Response (requirement 1): velocity matching, not impulses.**
- **Approach speed:** for each contact, take the hand's speed into the object along the
  normal, minus the object's own speed at that point. Velocities and the centre of mass
  (`+0x3C`, proved in phase 0) come from `pe_status_dynamics`.
- **The change:** when that approach speed is positive, change the object's velocity so its
  surface keeps pace. The object spins about its centre of mass, using the inertia fields
  `PhysicalNative.h` already reads, and gets a small separation speed when the proxy is
  inside it by more than about 5 mm.
- **Applying it:** with `pe_action_set_velocity`, one per object per frame (the deepest
  contact wins).
- **Why set the velocity:** queued impulses accumulate if two frames run before one
  physics step; setting the same velocity twice is harmless. This seam is already worn in
  throws.
- **Realistic mass (decision 1):** a hand pushes with a limited force, not a set speed.
  The velocity change one frame may give is at most that force times the frame time,
  divided by the object's effective mass at the contact. A can keeps up with the hand at
  once; a crate gains speed slowly and stops when the push stops. The strength setting
  scales that force. A maximum hand speed (about 6 m/s) also applies.
- **Friction from the engine's own data (decision 2):**
  - The sideways speed difference at the contact is matched too, within the Coulomb
    limit (no more than μ times the normal change). That lets a hand pressing down on an
    object drag it.
  - μ is the engine's own rule, `max(0, μ_object + μ_proxy) × 0.5`. μ_object is read from
    the world friction table at the contact's surface index; it is dynamic friction (×2/3)
    while the contact slides.
  - The hand or weapon itself has no surface. Phase 2 takes μ_proxy from a fixed surface
    or the engine default (0.7) and reports which.
- **One switch (decision 3):** "Physical contact" on/off, plus a strength setting.

**Diagnostics.**
- The overlay's physical layer draws each proxy, its contacts (point, normal, depth) and
  the velocity change given.
- `preyvr_physical` lines summarise pushes, rate-limited.
- The `physical` report gains query, contact and push counters per proxy.

## Phase 0 results (2026-10-18)

Static only, from the Steam binary. `python -E -B tools/re/verify_physical_contact.py`
checks all of it: 11 complete bodies (split chunks included; no base relocations;
mutation controls), 10 vtable slots, 41 instruction witnesses, one string and three constants.
[Output](evidence/physical-contact-2026-10-18/verification.json); the disassembly it was
read from is in the same folder.

**Per-entity primitive collision, `0xBA1D80` (4002 bytes, 7 chunks).**
- **Arguments:**
  - The 5th argument is the sweep vector.
  - The 6th is the `ray_hit` output; its `dist` is set to 1e10 first.
  - The 7th, `intersection_params*`, may be null. Then the function builds its own,
    with sweep mode on only when |sweep|² > 0. **A zero sweep is a static overlap test.**
- **Types:** `itype` 0 box (centre `+0x28`), 4 sphere, 5 cylinder, 6 capsule (centre
  `+0x00`); others are refused.
- **The primitive is modified:** the function zeroes the primitive's centre while it
  builds the geometry, then writes it back. The primitive must be the caller's own,
  per call.
- **Locks:** the caller lock (`world+0x5774 + caller×4`), then the entity's read lock.
- **Contacts:** it loops over the entity's parts (count `+0x128`, array `+0x120`, stride
  0x88), calls `IGeometry::Intersect` on each, and keeps the smallest contact `t`.
- **Output:** `dist` = t, collider = the entity, part index `+0x10`, part id `+0x14`,
  surface `+0x18`, point `+0x24`, normal `+0x30` (reversed, so out of the object). It
  returns 1 when `dist < 1e9`.
- **Still open for phase 1:** what `t` means in an overlap (depth, or 0).

**Broadphase, `0xBA8560` + `0xBA6830` (7470 bytes).**
- Takes the world lock and passes caller index 4.
- **Results:** when the caller passes its own buffer with `prealloc ≥ count`, the results
  are copied into it under the lock. Otherwise `*list` points at a shared per-caller
  buffer that the next query from any thread can overwrite.
- **Never set:** objtypes `0x40000` (the engine allocates a list) or `0x100000` (it
  AddRefs each entry).
- **Policy:** use our own buffer, and treat count > capacity as a refusal.

**Body state.**
- **Dynamics status:** rigid `GetStatus` type 8 writes `v +0x0C`, `w +0x18`, centre of
  mass `+0x3C` (from body `+0x298`, the centre the rigid Action uses for torque), and
  mass `+0x4C`. A sleeping body reports zero velocities.
- **Set velocity on a rigid body:** writes the body's velocity and momentum. Either field
  is left alone when it holds the "unused" sentinel. It wakes a sleeping body (vtable
  `+0x1C0`).

**Corpses: `CArticulatedEntity`, vtable `0x1D803E8`.**
- **Identity:** GetType returns 6. Its Action names itself in
  `"CArticulatedEntity:Action(action_impulse)"`, and it shares five inherited slots with
  the rigid class.
- **Action `0xB1CF30`:**
  - It queues like the rigid one.
  - It handles impulses per part: `partid +0x28` when `ipart +0x2C` is unused.
  - It handles set velocity for the whole body or one part.
- **GetStatus:** `0xB2C660`.

**Friction.**
- **Tables:** world slot `0xD0` is `GetSurfaceParameters(surface, &bounciness, &friction,
  &flags)`, for a surface index below 512. Slot `0xC0` sets them. Bounciness is at
  `world+0x10F0`, friction `+0x18F0`, dynamic friction `+0x20F0` (friction × 2/3), flags
  `+0x48F0`. `SurfaceTypes.xml` supplies the values (default friction 0.7).
- **The combining rule:** the rigid contact solver (`0xBE5FD0`) combines two surfaces as
  `(|a + b| + (a + b)) × 0.25`, that is `max(0, a + b) × 0.5`.

**Physics classes.**
- Static `0x1D82188`: the scenery the PreyVR-22 run kept hitting.
- Rigid `0x1D83870`.
- Living, by GetType 4, is `0x1D842C0`. Its slots are not physics-module code, but living
  bodies are left out by the broadphase classes anyway.

## Remaining work

| Phase | Work | Proof | Size |
|---|---|---|---|
| 0. Static contracts | **Done 2026-10-18** (above). | Offline bytes. | — |
| 1. Contact query, diagnostic only | **Worn 2026-10-18 on PreyVR-24**: overlaps work for movable props (below). | Headset: a contact at touch distance, held against, inside; object types and normals reported; cost per frame measured. | M |
| 2. Weapon pushes props | **Built 2026-10-18, package PreyVR-25, awaiting headset** (below). | Offline: solver math. Headset: push, slide, swat, heavy props resist, nothing launches. | M |
| 3. Hands | Palm proxies, hand arbitration (carry, psi, medkit, foregrip, two-hand), hand haptics. | Headset. | S–M |
| 4. Corpses | Articulated bodies, one part per contact, by impulse if per-part velocity is unsafe. | Phase 0 articulated contract, then headset. | M |
| 5. Polish | Friction/drag, skip what the player stands on (`pe_status_living` ground collider: Prey's struct is not the SDK's), performance budget, logging cleanup. | Headset. | S–M |

Not in scope:
- The hand or weapon stopping at walls (no blocking, no visual clamp).
- NPCs (user decision 10-18: no).
- Mod-owned physics bodies for the hands; kept as an option if velocity matching feels wrong.
- The wrench damage rebuild (deferred; it will reuse phases 0–1).

## Risks

- The broadphase takes the world lock exclusively. If the input-drain thread ever runs
  beside the physics step, it waits; measure in phase 1.
- A zero sweep is an overlap test (phase 0), but what `dist` holds in an overlap is
  unproven. Phase 1 settles it. The fallback is `+0x1E0`, or a slightly enlarged box.
- Setting a velocity overrides whatever the physics did to that object since our status
  read. One frame of that is invisible on a pushed prop, but must be kept off objects in a
  stack the hand is not touching (one object per contact only).
- Pushing an object the player stands on can throw the player; skipped from phase 5,
  watched for before that.

## Decisions (user, 2026-10-18)

1. **Mass:** heavy props resist realistically.
2. **Friction:** included, reusing the engine's own physics where possible: its friction
   tables and its combining rule.
3. **Options:** one switch with a strength setting.

## Phase 1: contact query, diagnostic only (2026-10-18, package PreyVR-24)

**What runs.** VR Options > Experimental > **Physical Contact** (`physical_contact`, off by
default). The one switch the scope settled on; its strength setting arrives with pushing
in phase 2. While it is on, `ContactLane.cpp` runs once per gameplay frame on the
input-drain thread, after the physical lane, and pushes nothing:

1. **The box:** the weapon box (`TryGetWeaponBox`), rebuilt as an oriented box
   (`contact::BoxFromPoints`). The physical lane now publishes it for either consumer
   (`PhysicalGeometryWanted`), so the wrench option no longer has to be on.
2. **Broadphase:** `GetEntitiesInBox` over the box at this pose and the last, plus 2 cm,
   asking for movable classes only, into our own 32-entry buffer (`prealloc` 32).
   More objects than that, or the engine's shared buffer handed back, is a refusal.
3. **Filtering:** skips the player's physics (shared with the scene query:
   `SceneQueryWorld`) and the carried object (`CarriedPhysics`). Only the rigid and
   articulated vtables go on; everything else is counted as `other`.
4. **The box tests:** per object, `CollideEntityWithPrimitive` with the box still (overlap)
   and, when the pose moved, from last frame's pose along the motion (sweep). Each call
   gets its own primitive. No sweep across a recentre, a weapon change, a jump over
   30 cm or a turn of more than 3 degrees: a snap turn swings the whole box around the
   player.
5. **The gate:** the first use checks the 11 `ContactNative.h` bodies and 7 vtable slots.
   A bad contract or a fault latches the lane off.

**What it reports.** Lines start `preyvr_contact`.
- **`settings enabled=`:** at start and on each change.
- **`gate result=0`:** once, when the contracts pass.
- **`fault error= detail=`:** when something latches the lane off.
- **`begin entity= type=rigid|articulated kind=overlap|sweep[+sweep] dist= partId= surface= friction= bounciness= mass= point= normal= sweepCm=`:**
  when an object starts touching. Mass comes from the hashed `GetStatus`; friction and
  bounciness from the engine's surface tables. These are the inputs phase 2 will use.
- **`end entity= frames= [why=]`:** after 3 frames untouched, or at once for a menu, a
  weapon change or a fault.
- **`report`:** every 10 s when anything changed. It holds queries, refusals, overflow,
  objects per query, contacts by kind, begins and ends, cost per frame in microseconds
  (`usAvg`, `usMax`) and the refusing gate. The same as the `contact` command.

Event lines are capped at 40 a second; the rest are counted (`droppedLines`). The debug
overlay's physical layer draws the queried box (amber, or grey with its gate) and each
contact this frame: an overlap as a filled cyan dot, a sweep as a yellow ring, a body
violet. Each has its normal and a label (`OVERLAP 2 mm`). The status panel line is `Contact`.

Tests:
- `contact_query`: box rebuild and native layout, bounds over both poses, record
  validation, begin/end tracking.
- `contact_lane`: the production lane against mock engine calls. It covers our buffer
  and its overflow, the skipped player, carried object and other classes, a fresh
  primitive per call, overlap-still and sweep-along, the begin line with mass and
  friction, the ends, a menu, and a fault latch.

`ctest` 82/82. Not run in the game.

**Open questions this run answers.**
- What does `dist` hold in an overlap: depth, or 0?
- Do contacts register at touch distance, held against an object, and inside it?
- Which objects come back, with which class, mass and friction?
- What does the broadphase plus box tests cost per frame, given that they take the
  world lock?

**Headset checklist** (VR Options: Physical Contact on, Debug overlay on; Physical
Wrench off):
1. Draw the wrench, then a gun: the amber box on the weapon; the `Contact` status line
   says `gate open`.
2. Touch a can or bottle lightly; hold the weapon against it; push the weapon into it.
   A cyan dot should appear on contact and stay while held. Note the `OVERLAP` mm
   values: touching, then deeper.
3. Swipe quickly past a can: a yellow `SWEEP` ring, even if the box passes through it.
4. Touch a corpse: violet. Touch a wall or a desk: nothing (not a movable class).
5. Stand among many props (a cluttered desk) and watch `us avg` / `max`.
6. Open a menu while touching: contact ends. Change weapons: the box follows the new
   weapon.
7. Afterwards, the log's `preyvr_contact begin` lines say what each object was (class,
   mass, friction), and `report` gives the cost.

### Phase 1 in the headset (2026-10-18, PreyVR-24, `player-20261018-151130`)

- **Movable props register.** The wearer confirmed it, and the log has 21 begins and
  1,104 contacts over 6,439 queries.
- **Static props do not, by design.** Fixed scenery, glass and the table are not asked
  for (`MovableObjects`), and the wearer accepted that.
- **Cost:** 2–4 µs per frame on average, 74 µs at most, with at most 2 objects near the
  box.
- **An overlap's `dist` is penetration depth:** 0.3 mm on touching, up to about 10 cm
  pushed in.
- **Masses** 1, 3, 7 and 10,000 kg; **friction** 0.5–1.0 from the surface tables.
- **31 sweeps** caught passes the overlap test would have missed.
- **Deep inside, contact can drop out briefly:** twice, at 7–10 cm into a 7 kg prop, for
  a few frames. Most likely the box was wholly inside the object.

## Phase 2: the weapon pushes props (2026-10-18, package PreyVR-25)

**What is new.**
- **The push step:** each touched rigid object gets one push per frame, from its deepest
  overlap or else its sweep, in `ContactLane.cpp` `PushContacts`.
- **The solver,** `contact::Solve` (pure, tested offline):
  - **Velocity matching:** the object's surface at the contact keeps up with the
    weapon's own velocity there (`contact::PointVelocity`, from its last two poses:
    hand motion, wrist turn and the player's walking).
  - **Never pulls:** moving away pushes nothing.
  - **Easing out:** an overlap past 5 mm is eased out at 10 cm/s per cm, at most
    0.5 m/s.
  - **Friction:** the object's surface friction from the engine table, combined with the
    weapon's own (the engine default, 0.7) by the engine's rule. It drags sideways up to
    that times the normal push, and 2/3 of it once sliding.
  - **Realistic mass:** the whole impulse is at most force × dt, with 400 N at the
    default strength. At 90 Hz a 1 kg can keeps up at once; a 7 kg prop gains about
    0.6 m/s a frame; a 100 kg crate about 0.04 m/s a frame, less than floor friction
    takes away; 10,000 kg does not budge.
  - **Speed cap:** hand speed is counted at most 6 m/s.
- **Applying it:** the new velocity and spin go through `pe_action_set_velocity`.
  Applying the same result twice before a physics step is harmless, so a queued set
  cannot stack. The rigid inverse mass and world inverse inertia come from the rigid
  Action's own fields (`ContactNative.h` `RigidInverseMass`, `RigidInverseInertia`). A
  body whose inverse mass disagrees with its reported mass by 5% or more is not pushed.
- **The gate:**
  - It adds the rigid Action and QueueAction bodies (`PhysicalNative.h`), the rigid
    Action slot, and the queue's set-velocity entry (`phys::VelocityQueueReady`). `push=`
    in the gate line and the report says whether they passed.
  - Without them the lane still detects but pushes nothing.
- **Shared code:** `PhysicsCalls.h` now holds the dynamics status, set velocity and queue
  check that `CarryLane.cpp` had to itself. The carry lane uses it too: the same calls,
  now reading the centre of mass as well.
- **Options:** VR Options > Experimental > **Physical Contact** (one switch) and **Push
  Strength** (`push_strength`, 25–200% of 400 N, default 100%; the retired
  `contact_strength` key is not reused).
- **Feedback:** a weapon-hand pulse and a `push` log line when an object starts being
  pushed. A push lasts until the object goes 30 frames without one. The overlay draws
  each push as a green line along the velocity change, 10 cm per m/s.
- **Scope:** rigid props only. Bodies (articulated) are found but not pushed (phase 4);
  hands are phase 3.

**Log.**
- **`push entity= kind= mass= friction= approachCms= handCms= dvCms= depthMm= limited= sliding= dtMs=`:**
  when a push starts.
- **The report adds:** `push= strength= pushes= pushBegins= limited= sliding= pushFailed= dvMaxCms=`.

Tests:
- `contact_query`: the solver. Covers matching and idempotence, never pulling, force
  limits for 100 kg and 10,000 kg, the speed cap, off-centre spin, stick and slide, zero
  grip, easing out and the slop, refusals, and the weapon's point velocity in
  translation and turn.
- `contact_lane`: the production lane. Covers pressing down setting the can's velocity
  through the entity's Action (type 10, thread-safe), a push starting once with a line
  and a pulse, the overlay arrow, bodies left alone, the 10,000 kg crate limited, and
  the strength clamp.

`ctest` 82/82. Not run in the game.

**Headset checklist** (VR Options: Physical Contact on, Push Strength 100%, Debug overlay
on to see the arrows; Physical Wrench off):
1. Press a can down onto a table, then slide the weapon sideways while pressing: it
   should be dragged along. A light touch from the side should push it away at the
   weapon's speed, and it should stop when the weapon stops (no pull back).
2. Swat a can or bottle quickly: it should fly at about the swing's speed, not faster.
3. Push a heavier prop (a 7 kg one, a chair, a box): it should move, but slower than
   your hand. Push something heavy: it should not move. Note anything that launches,
   spins wildly or jitters.
4. Hold the weapon still just inside an object: it should drift gently out of the
   weapon, not jump.
5. Push a can into a wall and hold it there: note any jitter or the can passing through.
6. Try Push Strength 25% and 200% on the same prop.
7. Bodies should not move yet; the carried object should not be pushed by your weapon.
8. Afterwards, `preyvr_contact push` lines give mass, approach and velocity change for each
   push, and `report` gives the counts.
