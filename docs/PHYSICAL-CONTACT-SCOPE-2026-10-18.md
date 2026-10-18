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
- Living: `0x1D81580` (corrected in phase 5, below). `0x1D842C0`, named here at first,
  only shares the GetType-4 stub and is not a physics class. Living bodies are left out by
  the broadphase classes anyway.

## Remaining work

| Phase | Work | Proof | Size |
|---|---|---|---|
| 0. Static contracts | **Done 2026-10-18** (above). | Offline bytes. | — |
| 1. Contact query, diagnostic only | **Worn 2026-10-18 on PreyVR-24**: overlaps work for movable props (below). | Headset: a contact at touch distance, held against, inside; object types and normals reported; cost per frame measured. | M |
| 2. Weapon pushes props | **Worn 2026-10-18 on PreyVR-25: works** (below). | Offline: solver math. Headset: push, slide, swat, heavy props resist, nothing launches. | M |
| 3. Hands | **Worn 2026-10-18 on PreyVR-26: works** (below). | Headset. | S–M |
| 4. Corpses | **Worn 2026-10-18 on PreyVR-27: works** (below): one part per contact, by per-part set velocity. | Phase 4 articulated contract (below), then headset. | M |
| 5. Polish | **Worn 2026-10-18 on PreyVR-34: works** (the standing-on skip took six tries; what worked is summarised at the end): skip what the player stands on, quiet logging, immovable bodies counted apart, the option on by default on the Hands tab. Friction was done in phase 2; performance needed no change. | Headset. | S–M |

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

### Phase 2 in the headset (2026-10-18, PreyVR-25, `player-20261018-153543`)

The wearer confirmed it works.
- **Activity:** 733 pushes in 31 push starts, with no failure or refusal and `push=1`.
- **Cost:** 2 µs per frame on average, 141 µs at most.
- **Heavy props:** 15 kg props were force-limited at 0.3 m/s per frame (400 N / 15 kg at
  90 Hz).
- **Light props:** 1 kg cans kept up, up to 3.8 m/s on a swat.
- **Friction:** about half the pushes slid.
- **Carry:** releases still go through the mod (`ours=1`) after the shared
  `PhysicsCalls.h` change.

## Phase 3: the hands push (2026-10-18, package PreyVR-26)

**What is new.**
- **Proxies:** the lane queries up to three boxes a frame (`Contact::proxy`), each with
  its own last pose, so each moves at its own measured speed:
  - **The weapon:** the existing box.
  - **The right hand:** while no weapon is drawn, since a drawn weapon is the right
    hand's box.
  - **The left hand:** unless it carries (`UseCarrying`, `CarryHoldingInHand`), holds
    the foregrip (`TwoHandedAimHeld`), works the medkit slot (`MedkitZoneHasLeftHand`)
    or aims psi (`PsiButtonHeld`).
- **The hand box,** `contact::HandBox`: palm-sized, from the controller's grip pose,
  mapped into the world by `animik::ControllerWorldFromHead`, the carry lane's own
  route.
  - It runs from the back of the hand (6.5 cm behind the grip centre along the palm
    normal) to the fingers round the handle (3 cm in front), 10 cm up the hand and 12 cm
    along the handle.
  - The palm faces grip +x on the left hand and -x on the right; the handle sits 3.5 cm
    out of the palm (`HandPose.h` `GripPoint`).
- **One push per object:** each object gets one push per frame, from the deepest overlap
  across all boxes, else a sweep, so two boxes on one object never fight. The pulse goes
  to the hand that pushed: the right for the weapon or the right hand, the left for the
  left hand.
- **Deadband:** an approach under 1 cm/s pushes nothing (`PushSettings::deadband`). The
  lane test found that a resting hand's measured speed (float noise, and in the headset
  tracking jitter) pushed and woke the object every frame.
- **Logging and overlay:** log lines name the box (`proxy=weapon|right_hand|left_hand`).
  The overlay draws each pushing hand's box in cyan, and the status line lists the
  active `boxes`. The gate is `idle` when no box is active.
- **Options:** no new options. Physical Contact and Push Strength cover the hands, as
  decided.

Tests:
- `contact_query`: the hand box's size and lean to the back of each hand, the
  `BoxPoints`/`BoxFromPoints` round trip, and the jitter deadband.
- `contact_lane`:
  - with no weapon, the right hand pushes the can with its own pulse, and the overlay
    shows its box alone;
  - the free left hand pushes, with a left pulse;
  - carrying, foregrip, medkit or psi each keep the left hand out;
  - weapon and left hand on one object give one push;
  - a resting hand pushes nothing.

`ctest` 82/82. Not run in the game.

**Headset checklist** (Physical Contact on, Debug overlay on):
1. **Box placement:** holster the weapon and look at both hands. The cyan boxes should
   wrap each visible hand. Note any offset; the box is placed from the controller, and
   the drawn hand may sit elsewhere.
2. **Swatting:** swat a can off a table with an empty right hand, then with the left;
   each pulses its own hand.
3. **Pressing and dragging:** press a box down and drag it with an open hand; push a
   heavy prop (it should resist).
4. **Resting:** rest a hand on an object and hold still. It should stay put and not
   buzz.
5. **Drawing a weapon:** the right hand's box goes and the weapon's box takes over. The
   left hand still pushes until it grabs the foregrip.
6. **Left-hand jobs:** carry with the left hand (its box goes away: the carried object
   is not pushed). Aim psi or reach for the medkit slot: no left box.
7. **Reaching to grab:** reach in to pick something up with X. Note whether the hand
   pushing it away first gets in the way of grabbing.

### Phase 3 in the headset (2026-10-18, PreyVR-26, `player-20261018-155944`)

The wearer confirmed it works over two runs.
- **Activity:** 765 pushes in 26 push starts: 22 by the right hand, 3 by the left and 1
  by the weapon, with no failure or fault.
- **Props:** 1 kg, 3 kg and 15 kg.
- **Cost:** 2 µs per frame on average, 116 µs at most.
- **The second run** (`player-20261018-160240`) touched nothing.

## Phase 4: corpses (2026-10-18, package PreyVR-27)

**Static results.** `tools/re/verify_physical_contact.py` passes with 15 rows and the phase 4
witnesses. The dumps (`artic_*.asm.txt`) and scripts (`p4_*.py`) are in
`docs/evidence/physical-contact-2026-10-18/`.
- **Set velocity on one part:** the articulated Action (type 10) takes `ipart` at +4 or
  `partid` at +8.
  - **Both unused:** the whole body. The engine turns it into one impulse at the centre of
    mass, on the root part, using `v` only.
  - **One part:** it finds the part (parts `+0x120`, count `+0x128`, stride 0x88, id
    `+0x48`) and the part's joint (infos `+0x458`, stride 0x94, joint `+0x54`; joints
    `+0x460`, stride 0x330).
  - **What it writes:** the joint body (`+0x1B0`) gets `v` and momentum `P = v·M`. A given
    `w` is written too, with `L` rebuilt from `q` and `Ibody`.
  - **Then:** it clamps to the world's speed limit and the entity's spin limit and marks
    the joint moving (`+0x1A0`). Unless the entity is awake (`+0x1A4`), it wakes it
    (vtable `+0x1C0`).
- **The step keeps it:**
  - The step (slot `+0x100`, `0xB37960`) runs a recursive joint pass (`0xB3C000`). The
    pass first copies each joint body's `v` and `w` into the joint's own `+0x88`/`+0x94`.
    After that it only adds to the body's velocity: gravity, external impulses and joint
    constraints, as `v += dv` (`0xB3ED30`).
  - No pass rebuilds a child's velocity from its parent's: there is no featherstone path.
  - StepBack (slot `+0xD8`) restores body `v` from that copy.
  - So a part's set velocity is the state the next step starts from, and the joints pull
    the rest of the body along.
- **Why not impulses:**
  - The articulated impulse branch adds into per-joint accumulators (`+0x2B8`/`+0x2C4`),
    applied at the next step. It ignores small impulses (below the joint's mass times a
    constant, for `iSource` 0).
  - Two frames between steps would apply an impulse twice. Set velocity is absolute, so
    the lane uses that instead.
- **Reading a part:**
  - Per-part GetStatus takes `partid` at +4 and `ipart` at +8; with neither it reports
    the whole body.
  - For a part it reports the joint's `+0x88`/`+0x94` copy, the velocity the last step
    started from. Its centre is the body's position (`+0x1B0`) and its mass the body's
    `M` (`+0x1FC`).
  - It uses the body's inverse mass (`+0x200`) and world inverse inertia (`+0x240`).
  - A joint body is laid out like the rigid entity's (`+0x298`): `v +0x40`, `M +0x4C`,
    `Minv +0x50`, `Iinv +0x90`. These are the same `0x2E8`/`0x328` offsets phase 2
    reads.
- **Host:**
  - GetParams (articulated body, type 8) returns `+0x4F8` as `pHost`: the entity a live
    character's skeleton rides on. The step drops it when that entity is deleted.
  - A ragdoll should have none. The headset run will show it: begin lines log `host=`.
- **Part ids:** the collide writes ray_hit `partid` (`+0x14`) from the part's id
  (`+0x48`). The PreyVR-25 run touched one 80 kg body at parts 50, 12 and 46.

**What is new.**
- **One part at a time:** a corpse is pushed through the part the box touched (ray_hit
  `partid`), by per-part set velocity.
  - It uses the props' solver, force limit (400 N × strength), friction and deadband.
  - A 4 kg arm keeps up with a hand. A 30 kg torso gains at most 0.15 m/s per frame. The
    joints drag the rest.
- **Reading the part:**
  - The lane reads its joint body live: `v`, `w`, centre, mass, inverse mass and world
    inverse inertia.
  - It does not use the status's copy. That copy lags a step, so a push read from it
    would land only every other frame.
  - Each read is cross-checked against per-part GetStatus: the same mass (to 0.1%), the
    same centre (within 10 cm), and inverse mass × mass ≈ 1. On a mismatch nothing is
    pushed (`pushFailed`).
- **Never a live character:**
  - An articulated body with a host is refused (`hosted`).
  - The broadphase classes (sleeping and awake rigid) already leave living bodies out.
- **One push per part:** two boxes on two parts push both. Two on one part push it once,
  from the deeper contact.
- **Gate:**
  - The four new rows (GetParams, the step, the joint pass, StepBack) are hashed with the
    rest.
  - Corpses are pushed only if the articulated Action, step, StepBack and GetParams slots
    route to the hashed bodies, on top of the props' push gate.
  - Otherwise props are still pushed, and the overlay shows `push N (props only)`.
- **Log:**
  - Begin lines for bodies add `host=`.
  - Push lines add `type=` and, for bodies, `part=`. For bodies, `mass` is the part's.
  - The gate line and the report add `bodies=`. The report also adds `partPushes=` and
    `hosted=`.
- **Option text:** Physical Contact now reads "loose objects and bodies".

Tests (`contact_lane`) use a fake two-part ragdoll in the engine's layout: an arm of 4 kg
on joint 1 and a torso of 30 kg on joint 0, so the lookup goes through the infos.
- Pressing on the corpse sets the arm's velocity alone, by `partid`. Its push line names
  the part and its 4 kg.
- An arm already moving with the weapon is left alone: the lane reads the live velocity.
- A host, a status that disagrees, or a failed body gate pushes nothing. The failed gate
  shows `props only`.
- Two boxes on two parts give two pushes, with the torso force-limited. Two boxes on one
  part give one push.

`ctest` 82/82. Not run in the game.

**Headset checklist** (Physical Contact on, Debug overlay on):
1. **A limb:** push a corpse's arm or head with an open hand. That limb should move with
   your hand, and the rest should follow through the joints. Note anything that jitters,
   stretches or launches.
2. **The torso:** press on it. It should resist: slower than a limb.
3. **A swat:** swat a hand or foot quickly. It should swing, not fly off.
4. **Dragging:** drag a limb along the floor with your hand on it.
5. **A still corpse:** one that has lain still for a while should wake when pushed.
6. **A live NPC:** touch one. Nothing should move, and no violet marks are expected. If a
   live character shows violet marks, note it; the begin line's `host=` says whether the
   host check caught it.
7. **Carrying:** if a corpse can be carried, the carried body is never pushed.
8. **Afterwards:** the `contact` report should show `bodies=1`, plus `partPushes`,
   `hosted` and `pushFailed`.

### Phase 4 in the headset (2026-10-18, PreyVR-27, `player-20261018-163121`)

The wearer confirmed it works on a mimic corpse.
- **Activity:** 404 pushes in 7 push starts, 350 of them on parts of five bodies (5 kg,
  5 kg, 30 kg, 80 kg and 110 kg in all). `bodies=1`, `hosted=0`: no ragdoll had a host.
- **Cost:** 1 µs per frame on average, 168 µs at most.
- **Immovable bodies:**
  - One articulated body (`0x23b481963e0`, surface 137) has zero mass: the game made it
    immovable. Its part read refuses a zero mass, and that accounts for all 20
    `pushFailed`.
  - Other corpses never showed up in the query at all. They are static entities, which
    the broadphase classes leave out.

## Phase 5: polish (2026-10-18, package PreyVR-28)

**Static results.** `tools/re/verify_physical_contact.py` passes with 17 rows and the phase 5
witnesses. The scripts (`p5_*.py`) are in the evidence folder.
- **The living class:** `CLivingEntity` has its vtable at `0x1D81580`.
  - Its Action (`0xB44E60`, slot `+0x38`) names itself
    `"CLivingEntity:Action(action_move)"`, and SetParams (`+0x20`) cites
    `"CLivingEntity:SetParams(player_dimensions)"`. GetType (`+0x08`) is the shared
    `mov eax, 4` stub `0x335000`.
  - Phase 0's `0x1D842C0` shares only that stub; its other slots are not living code.
- **What the player stands on:**
  - GetStatus (`0xB475D0`) first asks the base GetStatus (`0xB83040`, the static
    class's). Then, for `pe_status_living` (type 2), it copies fields out under the
    entity's read lock (`+0x348`).
  - `bFlying` goes to `+0x04` (flags `+0x2C0`, bit 7). The ground collider goes to `+0x70`,
    from `+0x2C8`. That is the same pointer that, when set, adds the ground's velocity to
    the reported `vel`.
  - Prey's struct is not the SDK's: it ends by `+0xA0`, and the lane passes a 256-byte
    buffer.

**What is new.**
- **Never what you stand on:**
  - Once a frame, when something is to be pushed, the lane asks the player's physics for
    `pe_status_living`. It does so only if the player's vtable is the living one; the
    first read logs `ground player=living|other`.
  - The ground collider is skipped (`standing=`), unless the player is airborne. A
    corpse underfoot is skipped whole.
- **Immovable bodies:** a prop or body part the game gave no mass (mass and inverse mass
  0) counts as `immovable`, not `pushFailed`. The PreyVR-27 run's 20 failures were one
  such body.
- **Quiet logging:**
  - The per-event `begin`, `end` and `push` lines are off by default; `contact log 1`
    turns them on (`contact log 0` off). With them off, the begin lines' mass and friction
    reads are skipped as well.
  - The counters, the 10 s report and the settings, gate, ground and fault lines always
    run.
  - The report adds `immovable=`, `standing=`, `ground=` and `log=`.
- **The option:**
  - Physical Contact and Push Strength move from Experimental to the Hands tab, after the
    throw strengths.
  - Physical Contact is now on by default. The lane starts on too, because a key missing
    from the settings file takes the lane's own value. A saved `physical_contact=0`
    still keeps it off.
- **Performance:** no change. The worn runs measured 1–2 µs per frame on average and at
  most 170 µs.

Tests:
- `contact_lane`:
  - The lane starts on, with its event lines off.
  - Pressing on the can the player stands on pushes nothing, and the player's class is
    logged once. Airborne, the can is pushed again.
  - Standing on a corpse, none of its parts are pushed.
  - A massless prop counts as immovable, not as a failure.
  - With the event lines off, contacts and pushes are counted but not logged.
- `vr_options`: the option and its strength are on the Hands tab, and the option is on
  by default.
- `vr_options_runtime`: on by default; off and on reach the lane.

`ctest` 82/82. Not run in the game.

**Headset checklist:**
1. **Default on:** in VR Options > Hands, Physical Contact and Push Strength sit after
   the throw strengths, and Experimental no longer lists them. Your saved setting
   carries over.
2. **Standing on a prop:** stand on a crate or box and press down on it with a hand, or
   touch it with the weapon. It should not slide out from under you. Jump off it, or
   step down, and it pushes as before.
3. **Everything else as before:** props and corpses still push.
4. **Afterwards:** the `contact` report should show `ground=living`, and `standing` above
   0 from step 2. The log should hold no per-event lines unless `contact log 1` was sent.

### PreyVR-28 in the headset (2026-10-18, `player-20261018-165507`)

The wearer stood on an object and could still push it with both hands and the weapon.
- **The ground read worked:** `ground player=living`. The object underfoot was matched
  and skipped on 5 frames when contact began (`standing=5`).
- **Then the field went empty:** the next 43 pushes went through, with no further skips.
- **The cause:** the engine does not keep its ground collider while the player stands
  still.
  - `bFlying` is reported as flags bit 0 AND bit 7, so an inactive body reads as not
    flying.
  - The living code clears `+0x2C8` in several places. They include the step, when the
    ground is static or the player is off it (`0xB5381D`, `0xB5390C`), and Awake (`+0x1C0`),
    when the ground was deleted. The step that would refresh it does not keep it for a
    body at rest.
  - The engine's bookkeeping alone cannot say what a still player stands on.

## Phase 5 fix: a probe under the feet (2026-10-18, package PreyVR-29)

**The feet.**
- Living GetStatus for `pe_status_pos` (type 1) reports the entity's position as the
  body's `+0xA8` shifted by `q·(0, 0, −pivot)`, with the pivot height at `+0x1D0` (plus a
  smoothing term). `pe_status_living` gives that same shift as `camOffset` (`+0x0C`).
- So the ground point under the player is `+0xA8 + camOffset`.
- It is used only if it sits 0.2–2.6 m below the head (the camera centre). The report
  gives `headAboveFeetCm=`, and the first ground line logs `headAboveFeet=` and `probe=`.

**The probe.**
- A level box 50 cm square, from 2 to 8 cm below the feet, goes through the same
  broadphase and per-entity overlap test as the hands, for movable objects only.
- What it overlaps reaches below the floor the player stands on, so it is what they
  stand on.
- A can on the floor beside the feet does not reach below that floor, so it still
  pushes.
- Underfoot is that set plus the engine's collider when it reports one. Nothing is
  underfoot while airborne.
- The probe runs once a frame, only when something is to be pushed.
- **The overlay** draws the probe while something is touched: orange over what you stand
  on, grey over nothing.

Tests (`contact_lane`):
- With no ground collider, the probe keeps the can underfoot from being pushed. It sits
  under the feet, 2 to 8 cm down, and the report shows a head height of 170 cm.
- A can the probe does not reach still pushes, and the probe shows grey.
- Feet implausibly far below the head mean no probe.

`ctest` 82/82. Not run in the game.

**Headset checklist** (Debug overlay on, to see the probe):
1. **Standing still on a crate or box:** press down on it with a hand, or touch it with
   the weapon. It should not move. The orange box should sit just under your feet,
   inside the crate's top.
2. **Stepping off:** the crate pushes again, and the box goes grey or away.
3. **Something on the floor at your feet:** a can, say. It should still push, and the
   box should stay grey.
4. **Afterwards:** the `contact` report gives `headAboveFeetCm`, which should be about
   your eye height in the game (150–180), and `standing` above 0.

### PreyVR-29 in the headset (2026-10-18, `player-20261018-171059`)

The wearer stood on a box, crouched, and pushed it with the wrench.
- **The feet were found:** `headAboveFeetCm=168`, a plausible height, so the probe ran.
- **But it rarely caught the box:** it matched on only 2 frames (`standing=2`), while
  27 pushes went through.
- **The cause:** the overlap probe sat wholly inside the box, 2 to 8 cm below its top.
  The per-entity test reports where a primitive crosses an object's surface. A box
  wholly inside a mesh crosses none of its triangles, so it finds nothing. This is the
  same deep-overlap dropout phase 1 saw at 7–10 cm.
- **The first ground line** logged `headAboveFeet=nan probe=0`: on that frame the
  player was airborne or the read failed.

## Phase 5 fix 2: a swept probe (2026-10-18, package PreyVR-30)

**The probe.**
- A thin, level box 40 cm square is swept from 10 cm above the feet to 5 cm below them.
  It goes through the same per-entity sweep the weapon and hands use, with the overlap
  test skipped (`Query`'s new `overlap` flag).
- What it lands on is underfoot when the surface faces up (normal z > 0.7) and sits at
  the feet: from 6 cm below them to 4 cm above.
- **What it skips:**
  - A can lying beside the feet is struck 7 cm or more above them, so it still pushes.
  - Something the probe starts inside is caught at its start or on a side face, so it
    is not underfoot.
- The engine's collider is still added when it reports one. The overlay draws the
  swept volume.

Tests (`contact_lane`):
- A can whose top is at the feet is not pushed. The probe sweeps from above the feet to
  below them.
- A can struck 7 cm above the feet still pushes, and so does one the probe misses.
- Feet implausibly far below the head mean no probe.

`ctest` 82/82. Not run in the game. The headset checklist is phase 5's: stand still or
crouch on a box and push it with a hand or the wrench (it should not move); step off
(it pushes); a can at your feet still pushes.

### PreyVR-30 in the headset (2026-10-18, `player-20261018-172441`, event lines on)

The wearer confirmed it works, but said the box needs to be bigger: a part of the
object outside the box could still be hit, and the object moved.
- **The object:** one 3 kg prop (surface 137), a large flat object. Its upward-facing
  contacts spanned about 0.8 m by 1.3 m.
- **The probe:** it held the object back on 292 frames, but 46 pushes went through.
  The head was 84–90 cm above the feet: crouched.
- **Why:** crouched near the object's edge, the player's centre can sit past the
  probe's 20 cm half-width while their body still rests on the object.

## Phase 5 fix 3: the collider's width, and a hold (2026-10-18, package PreyVR-31)

- **The player's real size:**
  - Living GetParams (`0xB46AA0`, slot `+0x28`) for `pe_player_dimensions` (type 1)
    first asks the base GetParams (`0xB81A20`).
  - Then, under the read lock, it copies `heightPivot` (`+0x04`, from `+0x1C0`),
    `sizeCollider` (`+0x0C`, from `+0x1C4`; x is the radius), `heightCollider` (`+0x18`)
    and `bUseCapsule` (`+0x34`, flags bit 4). Both functions are hashed (19 rows).
- **What that corrects:** `camOffset`'s `+0x1D0` is not the pivot but a stance-smoothing
  offset, zero at rest. The feet are now `+0xA8 − heightPivot + camOffset`. The measured
  168 cm standing and 85 cm crouched show a pivot near 0 either way.
- **The probe's size:** its half-width is the collider radius plus 5 cm, clamped to
  20–80 cm (40 cm when unread). The report gives `footHalfCm=`, and the first ground line
  gives `radius=`.
- **The surface window:** the landing surface's top limit drops from 4 cm to 2.5 cm
  above the feet, so a wider probe still lets flat things beside the feet push.
- **The hold:** what was underfoot stays underfoot for 20 frames (about 0.2 s) after it
  was last found, even airborne, so a missed frame cannot slide it out. Stepping off,
  it pushes a moment later.
- **Event lines** (`contact log 1`): `stand entity= via=probe|engine feetZ= surfaceZ=
  radius=` when an object starts being underfoot, and `stand_end entity= frames=` when
  the hold lapses.

Tests (`contact_lane`):
- The probe is 40 cm each way for a 35 cm collider (`footHalfCm=40`).
- What was underfoot is held for 20 frames after the player leaves it, then pushes, with
  `stand` and `stand_end` lines.
- A can landed on 3 cm above the feet still pushes.

`ctest` 82/82. Not run in the game.

**Headset checklist:**
1. **Edges:** crouch or stand near the edge of a large object, then push it at its far
   end with a hand and with the wrench. It should not move. The orange box should reach
   about as far as your body does.
2. **Stepping off:** step off it. It should push again after a moment.
3. **Things beside you:** a can or a book at your feet should still push.
4. **Afterwards:** the `contact` report gives `footHalfCm`, the player's collider
   radius plus 5 cm.

### PreyVR-31 in the headset (2026-10-18, `player-20261018-173439`, event lines on)

The wearer could still move the object they stood on, by reaching round the drawn box
with the wrench or a hand.
- **No probe detection:** every `stand` line says `via=engine`. The probe never found
  the object, not in this build and, it now appears, not in PreyVR-29 or -30 either.
  All 384 skipped frames came from the engine's collider, reported now and then.
- **The pushes:** each lands right after a `stand_end`, when the 20-frame hold has lapsed
  between two engine reports. There were 113 pushes.
- **The likely cause:** the per-entity sweep resolves where the swept box ends, as the
  overlap test does, and this probe ended 5 cm inside the object. A box wholly inside a
  mesh touches none of its triangles. The weapon's sweeps work because the weapon ends
  only just inside what it hits.
- **The sizes were right:** radius 0.35 m, so a 40 cm probe half-width; feet at the
  object's top (z 7.22).

## Phase 5 fix 4: rays under the feet, and a hold while standing (2026-10-18, package PreyVR-32)

- **Rays:**
  - 13 rays go straight down, from 10 cm above the feet to 10 cm below: one at the
    centre, 4 at half the collider radius and 8 at the radius (35 cm for the player).
  - Each is the scene reticle's own query (`QuerySegment`): static, rigid, living and
    terrain, with the player skipped. It stops on the first solid surface, which is the
    top of whatever is under that point, mesh or box.
  - A hit is underfoot when the object is movable, the surface faces up (normal z > 0.7)
    and it sits 6 cm below to 2.5 cm above the feet.
  - A can beside the feet is not: the ray starts inside it (one-sided triangles) or
    meets its top too high. On the static floor, the floor is hit and nothing is
    underfoot.
- **The hold:**
  - What was underfoot stays underfoot while the feet stay within 10 cm across and 5 cm
    up or down of where it was last found, however long that is.
  - Once they move further, or leave the ground, it stays underfoot 20 frames more.
  - Standing still on it, it can no longer lapse between detections.
- **The sweep probe and its `overlap` switch are gone.**
- **The report** gives `footRadiusCm`, `footRays` and `footRayHits`. `stand` lines say
  `via=ray|engine` and how many rays hit.

Tests (`contact_lane`; a fake `QuerySegment` answers each ray):
- 13 rays, out to the 35 cm radius, keep the can underfoot (`via=ray`).
- Standing still with every ray missing, the can stays underfoot for 60 frames.
- Stepping 15 cm away, it is held 20 frames, then pushes.
- On the object's edge, where only the outer rays reach it, it is still underfoot.
- A can 3 cm above the feet, the static floor, or a miss all leave the can pushable.
- With implausible feet, no rays are cast.

`ctest` 82/82. Not run in the game.

**Headset checklist** (`contact log 1`):
1. **Standing and crouching:** stand or crouch on the big object. Push it anywhere,
   including round the drawn footprint, with hands and wrench. It should not move. The
   log's `stand` line should say `via=ray`.
2. **Stepping off:** step off. It should push again after a moment.
3. **Things beside you:** a can at your feet should still push.

### PreyVR-32 in the headset (2026-10-18, `player-20261018-175115`, event lines on)

The object could still be pushed.
- **The rays did hit it:** each `stand` began with 8–9 of the 13 rays on it, and 370
  frames had ray hits. The `stand` line said `via=engine` only because the engine's
  collider is listed first.
- **When the pushes came:** every push lands on the first frame of a new contact (`begin`)
  after a gap with no contact. That is the frame with a `stand_end`, where that frame's
  rays had found nothing.
- **The likely cause:** the world's ray stops at the first thing it meets. The hand,
  the held wrench, or what they press against sits between the feet's ray starts and
  the object top in exactly the frames a push begins: 5 of the 8 pushes were the
  wrench's.
- **A second cause:** the hold's "feet stay put" check only ran on frames with a contact,
  so across a gap it compared against stale feet.

## Phase 5 fix 5: test the object itself (2026-10-18, package PreyVR-33)

**The test.** For each object about to be pushed, once a frame, 13 samples check
whether its top is under the feet.
- **The samples:** one at the centre, 4 at half the collider radius, and 8 at the
  radius.
- **Each sample:** a 3 cm sphere centred 0.5 cm below the feet, through the
  per-entity test (`CollideEntityWithPrimitive`, itype 4, sphere `{centre +0x00, radius
  +0x0C}`) against that object alone. The collide's itype switch takes box 0, sphere
  4, cylinder 5 and capsule 6; there is no ray.
- **What counts:**
  - Where the object's top runs through a sphere, the contact faces up, and the player
    stands on it there.
  - Where the object is not under that point, there is no contact.
  - A can beside the feet meets the sphere with its bottom or side: facing down or
    sideways, so not underfoot.
- **Why this avoids the earlier failures:**
  - The sphere straddles the surface it looks for, so it is never wholly inside a mesh,
    which defeated the overlap box and the sweep.
  - Nothing else in the world is tested, so nothing over the object can block it, as
    something blocked the rays.
- **The rays are gone.** The engine's collider still counts.
- **The hold** keeps its rule: underfoot while the feet stay within 10 cm across and 5 cm
  up or down of where the object was last found, then 20 frames.
- **Diagnostics:**
  - `stand` lines say `via=feet|engine`, with `samples=` (found its top / met it
    otherwise).
  - Each `push` line ends with the same `feet=`, plus `feetZ` and the contact `point`.
    A push through an object the player stands on now shows exactly what the samples
    saw.
  - The report gives `feetTests` and `feetFound`.

Tests (`contact_lane`; the fake collide answers the feet's spheres):
- 13 samples, out to the 35 cm radius, find the can's top (`via=feet`).
- Standing still with every sample missing, the can stays underfoot for 60 frames.
- After a 15 cm step it is held 20 frames, then pushes.
- On its edge, the outer samples find it.
- A top 3 cm above the feet, a bottom at the feet (a push line with `feet=0/13`), or a
  miss all leave the can pushable.
- With implausible feet, no samples are taken.

`ctest` 82/82. Not run in the game.

**Headset checklist** (`contact log 1`): stand and crouch on the object, and push it
everywhere with hands and wrench, including pressing down near your feet. It should
not move. `stand` lines should say `via=feet`. If anything does move it, its `push` line
says `feet=hits/others feetZ= point=`.

### PreyVR-33 in the headset (2026-10-18, `player-20261018-180109`, event lines on)

The box could still be moved from on top of it.
- **No feet were read:** every push line says `feet=- feetZ=nan`. On those frames the
  feet were not read and the samples were not taken, because the engine called the
  player airborne. The first ground line (`headAboveFeet=nan probe=0`) is the same.
- **The flying flag flickers:** standing on this light 3 kg box, the engine reports
  the player flying in bursts. Since PreyVR-28 an airborne frame had cleared
  everything underfoot, and the hold read airborne as having left.
- **Likely the common cause:** this is probably why every build failed the same way,
  whatever the detector.
- **Still open:** the sphere test ran 29 times and found nothing (`feetFound=0`). Those
  frames may have been ones where the player was off the box; the next run's `feet` lines
  will say.

## Phase 5 fix 6: the feet decide, not the airborne flag (2026-10-18, package PreyVR-34)

- **Always read:** the feet are read and the samples taken whether or not the engine
  says airborne.
- **The hold:** something stays underfoot while the feet stay within 10 cm across and
  5 cm up or down of where it was found. Jumping or stepping off moves them, and the
  20-frame hold then runs.
- **The `airborne` flag** is now only logged: push lines say `air=`.
- **A `feet` line** (with `contact log 1`) for each object the samples did not find
  underfoot: `samples=0/others feetZ= air= firstZ= firstNz=`, the first contact any
  sample made, to show why.

Tests (`contact_lane`):
- Standing still while the engine calls the player airborne, what is underfoot stays
  underfoot.
- On the object's edge, airborne flag set, the outer samples find it.
- Jumping 10 cm up, it is held 20 frames, then pushes.
- A bottom at the feet logs `samples=0/13 ... firstNz=-1.00`.

`ctest` 82/82. Not run in the game.

### PreyVR-34 in the headset (2026-10-18, `player-20261018-180738` with event lines, `player-20261018-181030`)

The wearer confirmed it is fixed over two runs: the box did not move while they stood on
it.
- **Who found the box underfoot:** the feet's samples (`via=feet`, 7–9 of the 13 samples
  on its top) and the engine's collider (`via=engine`) each did, three times.
- **The only pushes** came with the feet on the floor (z 6.70, the box's top at 7.22).
  There the samples met the box's bottom face (normal z −1) and rightly left it
  pushable.
- **Cost:** 2–3 µs per frame on average, 38–77 µs at most.

## What worked: never pushing what the player stands on

**What decides it.** An object about to be pushed is skipped if the player stands on
it. It is underfoot when either of these holds:
- **The engine's ground collider** names it: `pe_status_living` `+0x70`. The engine lets
  go of it while the player stands still, so it only adds.
- **The feet's samples** find its top. Thirteen 3 cm spheres sit at the feet: one at
  the centre, 4 at half the collider radius and 8 at the radius
  (`pe_player_dimensions`, 35 cm for the player). Each goes through the per-entity
  test against that object alone. A contact facing up (normal z > 0.7), between 6 cm
  below and 2.5 cm above the feet, is its top.

**The feet** are the living body's position `+0xA8`, less `heightPivot`, plus
`camOffset`. They are used only if they sit 0.2–2.6 m below the camera.

**The hold.** It stays underfoot while the feet stay within 10 cm across and 5 cm up or
down of where it was found, and for 20 frames after they move.

**What did not work, and why:**
1. **The engine's ground collider alone (PreyVR-28):** it is released while the player
   stands still.
2. **An overlap box under the feet (-29):** the per-entity test reports surface
   crossings only, and a box wholly inside a mesh crosses none.
3. **A downward sweep (-30, -31):** it resolves where the sweep ends, which was inside
   the object again.
4. **The world's rays (-32):** they stop at the first thing they meet, such as the hand,
   the wrench or what they press on.
5. **Trusting the engine's airborne flag (-28 to -33):** standing on a light object it
   flickers on in bursts. Every build cleared what was underfoot on those frames, which
   hid whether each detector worked.

**Lessons for the wrench rebuild and later work:**
- A per-entity primitive must straddle the surface it looks for.
- A query of the whole world can be blocked by the hands themselves.
- Read the player's state from geometry, not from the engine's flags.

**Tidy-up after the fix.** The investigation's instrumentation is removed:
- the `feet` miss line;
- the push line's `feet`/`feetZ`/`air`/`point`;
- the report's `headAboveFeetCm`, `footRadiusCm`, `feetTests` and `feetFound`;
- the airborne read.

Kept:
- the one-time `ground player= radius=` line;
- the `standing` and `ground` report fields;
- `stand entity= via=feet|engine` and `stand_end` (with `contact log 1`);
- the overlay's orange or grey footprint.

`ctest` 82/82, package PreyVR-35.

### PreyVR-35 regression (2026-10-18, `player-20261018-181715`)

The wearer confirmed the tidied build works. Logged: `ground player=living radius=0.35`, 11 pushes in
2 push starts, 254 frames held back as underfoot, no fault or failed push, 3 µs per frame on average
(35 µs at most), event lines off by default. Physical contact phases 0-5 are complete.
