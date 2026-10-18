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
| 1. Contact query, diagnostic only | Query layer for the weapon box; overlay and logs; no pushing. | Headset: a contact at touch distance, held against, inside; object types and normals reported; cost per frame measured. | M |
| 2. Weapon pushes props | Velocity-matching solver on rigid props; options; haptics. | Offline: solver math (idempotent, caps, mass fade, spin). Headset: push, slide, swat, heavy props resist, nothing launches. | M |
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
