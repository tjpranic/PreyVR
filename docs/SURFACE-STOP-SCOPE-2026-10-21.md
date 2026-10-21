# Surface stop: hands and weapons that stop against walls

Status: **done, worn OK 2026-10-21**: PreyVR-53 (fixed scenery; doors count) and PreyVR-54
(heavy and immovable loose objects, below). TODO item (Immersion):
"Weapons and hands don't stop against surfaces." Investigated the same day; the user's
decisions are at the end.

## Why they pass through

Nothing between the controller and the drawn hand looks at the world.

1. **The IK puts the hand exactly where the controller is.** `DriveHand`
   ([AnimIkTakeover.cpp:1132](../src/dll/AnimIkTakeover.cpp#L1132)) maps the controller's grip
   into the world with `animik::ControllerWorldFromHead` (camera centre + yaw x the
   controller's offset from the head) and makes that the wrist goal. The free-hand branch
   does the same. The goal is changed only by the arm's reach (`SoftReach`, `ClampToReach`)
   and the two-handed support blend. No query runs, so a wall or a desk cannot hold it back.
2. **The weapon has no collider of its own.** It hangs off the right wrist, so it goes
   wherever the wrist goes. The weapon box (`PublishPhysicalWeapon`) only reports where the
   weapon is drawn.
3. **The contact lane pushes things and never stops the hand.** It asks the broadphase for
   movable classes only (`MovableObjects = 0x6`, ContactNative.h) and changes the props'
   velocities. Walls, floors, desks and glass are static and are never asked for. Both
   scope docs left this out on purpose:
   [PHYSICAL-CONTACT-SCOPE](PHYSICAL-CONTACT-SCOPE-2026-10-18.md) ("Not in scope: the hand or
   weapon stopping at walls (no blocking, no visual clamp)") and
   [FREE-SWING-WRENCH-SCOPE](FREE-SWING-WRENCH-SCOPE-2026-10-18.md) (line 262, "the wrench
   stopping at walls (no visual clamp)").
4. **The player's physics is a single capsule** (living entity, radius 0.35 m). It keeps the
   body out of walls. The arms, which reach about 0.7 m past it plus up to 1.25 m of weapon,
   are not part of it.

The real controller can never be stopped, so the fix is a **visual** one. The drawn hand and
weapon stop at the surface while the tracked hand goes on. This is the "ghost hand"
divergence that Half-Life: Alyx and similar games use.

Two guards already stopped the obvious exploits, and the fix keeps them:
- **Shots:** [ShotRay.cpp:704](../src/dll/ShotRay.cpp#L704) spawns from the drawn muzzle
  only if eye-to-grip and grip-to-muzzle are clear, else from the camera.
- **Wrench strikes:** the swing lane's line-of-sight check
  ([SwingLane.cpp:639](../src/dll/SwingLane.cpp#L639)).

Before this work, no such guard existed for the left hand's interaction ray. It started
10 cm behind the real controller ([InteractionLane.cpp:591](../src/dll/InteractionLane.cpp#L591)),
so a hand pushed through glass might select what is behind it. Whether the native selector
checks line of sight from that origin is unverified.

## Prior art in this repo

[RE-VEE-WEAPON-COLLISION-2026-09-09.md](RE-VEE-WEAPON-COLLISION-2026-09-09.md) reviewed a
flat-screen wall pull-back and set the rules this design follows:
- keep the tracked input unchanged;
- produce a constrained visual pose, with the IK following the constrained grip;
- engage fast and release more slowly, but never let smoothing allow penetration;
- reset across equip, recentre, tracking epoch and long gaps;
- the reticle and native spawn use the constrained pose;
- never correct the head.

Lessons from the contact and feet work (the contact scope doc's "What worked" section):
- A per-entity primitive test misses a primitive wholly inside a mesh, as an overlap or as
  the end of a sweep. A hand pushed 20 cm into a thick wall is exactly that case.
- A whole-world ray stops at the first thing it meets. Asking for scenery only (static and
  terrain, `scene::SceneryObjects = 0x101`) cannot meet the hands, the weapon or the player.
- Ray hit normals face back along the ray: PreyVR-32's downward feet rays read
  normal z > 0.7 on top surfaces.
- Decide from geometry, not from engine flags.

## What was built (PreyVR-53)

**Where.** `SurfaceStopLane.cpp` runs on the input-drain thread, in the aim hook, after
`SolveTwoHandedFrame` and **before** `gGameplayFrame.Publish(frame)`
([AimTakeover.cpp:296](../src/dll/AimTakeover.cpp#L296)). It writes each hand's correction
into `GameplayPoseFrame::surface` (by `Hand`). The IK runs on the animation thread, where
physics queries are refused. It reads the correction from the same frame as its tracking,
so the two always match and there is no extra frame of lag.

**The query.** The scene query's scenery-only segment (`QuerySceneryBetween`: static and
terrain, the player skipped, the ray collision class). It is the reticle's own query, so
it needs no new native contract. The ray class means the hand stops at what stops bullets:
player-clip walls do not catch it, and glass does. A segment over 2 m is cast in pieces.

**The probes.** Points rigidly attached to each hand, from this frame's tracking:
- **Free hand:** its grip and the eight corners of `contact::HandBoxAt`, the contact lane's
  palm box (worn 10-18: it wraps the drawn hand). It is now shared with the contact lane.
- **Right hand with a weapon:** its grip, the palm box's four back corners (knuckles and the
  back of the hand), and the weapon box's eight corners, carried to this frame's grip. On a
  weapon longer than 30 cm, the middles of its four long edges are added too.
  - The IK publishes the box with the right grip's full pose (`WeaponBox::gripPose`) and the
    correction it was drawn with (`WeaponBox::correction`).
  - So the box in the hand is `inverse(grip) * (points - correction)`. It is one frame old
    (reload and recoil move it).
- **Left hand fully on the foregrip** (two-hand aim, snapped, blend 1): no probes. It is
  drawn on the weapon, which the right hand carries.

**The solve** (`surface::Solve`, pure, `src/common/SurfaceStop.cpp`).
- **Moved (normal frames):** each probe travels from where it was drawn last frame to where
  the controller puts it, along a scenery ray.
  - The ray starts 2 cm behind the drawn point, so a probe resting on a surface still meets
    it.
  - A surface the ray enters gives that probe a plane: the hit point and its normal.
  - The hand is pushed out of each plane along its normal, to a 5 mm skin: one translation
    for the whole hand. Repeated passes settle corners of two or three planes.
  - Up to three rounds: each push is cast again.
- **Sliding:** pushing out along the normal lets the drawn hand slide along a wall or a desk
  top under the real one. Swept sideways into a pole, a weapon stops beside it rather than
  being pulled back along its length.
- **No tunnelling:** a fast pass through a thin wall in one frame still meets its face.
- **Reaching under a desk** from its side is not held, though the camera's line to the hand
  crosses the top.
- **Fresh (first frame, after any reset):** placed from the camera's view of the real hand.
  The rays go camera to grip, then grip to each probe, with the same push-out. If the push
  cannot be checked, the grip stops along the camera's own ray instead (`fallback`).
- **Catching up:** a held-back hand that is let go moves at most 2 m/s faster than the real
  hand, along a path that is cast again. Coming off a desk edge, the hand glides down
  instead of popping.
  - Pulling the hand straight back out of a wall is never slowed: the real hand moves to
    the drawn one.
- **Letting go:** held back for 30 frames in a row while the camera sees the real grip, and
  the grip each probe, unobstructed (`unstuck`). This frees a hand stuck on the far side of
  something it cannot slide round. A real hand behind a wall is never "in the open", so the
  drawn hand stays at the wall however far it goes (decision 5).
- **Resets:** each hand's memory is forgotten on:
  - a recentre or tracking epoch;
  - a weapon change or the weapon probes switched;
  - a turn over 3 degrees in a frame (snap turn), or the grip jumping 50 cm;
  - a gap over 100 ms;
  - the lane gated off (menu, head in a wall, off, query fault).

  A repeated tracking sample keeps the memory and the last catch-up pace.

**Applying it.** `DriveHand` adds the correction to the controller's world position, in the
weapon branch and the free-hand branch. Everything downstream then follows it:
- the reach compression;
- long-weapon hold-back;
- the support hand on the foregrip (attached to the primary's goal);
- the arm rewrite;
- the published weapon box;
- the shot's muzzle (`ReadMuzzle` reads the drawn weapon), and with it the aim laser and
  reticle origin.

`primaryOffsetWorld`, which the two-hand solver reads, stays free of the correction: the
real hands still meet the real foregrip.

**Other consumers.**
| Consumer | Change |
|---|---|
| `SwingLane` | Strikes from the **real** swing (`TrackedWeaponBox`: the box minus its correction). A wrench stopped a skin short of a wall or glass would never touch it. The real swing crosses the surface once, which gives one strike, while the drawn wrench stops there. |
| `ShotRay` `GripWorld`/`AimWorld` | The corrected right hand. Eye-to-hand is clear when the drawn hand is, so the shot leaves the drawn muzzle (now in front of the wall) instead of falling back to the camera. |
| `InteractionLane` | The left ray starts from the drawn hand: no selecting through glass by reaching through it. |
| `PsiMedkit` | The left-controller psi ray starts from the drawn hand. |
| `ContactLane` `HandProxy` | The hand boxes are where the hands are drawn: they push what they visibly touch. The weapon box was already the drawn one. |
| `GrenadeLane` `RightGrip` | The fist spawn and its "drawn grenade" check use the drawn hand. Its own pull-back from the head stays. |
| `CarryLane` | Unchanged: a carried object is a physics body and stops at walls by itself. |

**Option.** VR Options > Hands > **Hands Stop at Surfaces** (`surface_stop`, on by
default), before Physical Contact. A missing key takes the lane's own value (on).

**Diagnostics.**
- **Lines:** they start `preyvr_surface`.
  - `settings enabled= weapon= log=` at start and on each change.
  - `report`, every 10 s when anything changed, with:
    - `frames`, `held` (frames with a hand held), `holds` (hold starts) and `solves`;
    - the paths `fresh`/`moved`/`unstuck`/`refused`, then `fallback`, `limited`,
      `unconverged`, `backFaces` and `resets`;
    - `weaponFrames`;
    - what held a hand by GetType: `static`, else `other`;
    - `offsetMaxCm`, `raysAvg`, `raysMax`, `usAvg` and `usMax`;
    - the `gate`.
  - With `surface log 1`: `hold hand= path= offsetCm= planes= collider= type= surface= probe=
    point= normal=` when a hand starts being held, and `free hand= frames= maxCm=` when it is
    let go.
- **Commands:**
  - `surface` prints the report.
  - `surface 0|1` is the option.
  - `surface log 0|1` turns the event lines off or on.
  - `surface weapon 0|1` stops the hands alone without the weapon's box, for A/B.
- **Overlay** (debug overlay, physical layer):
  - a held hand's real grip as an orange ring, where it is drawn as a dot, joined by a
    line;
  - each surface met, with its normal;
  - the status line `Surface on gate open held -R rays N us avg/max`.

**Tests** (`ctest` 91/91):
- `surface_stop`, the pure solve against a fake world of closed boxes. A ray starting
  inside a box does not see it. It covers:
  - Resolve;
  - open air (one ray per probe; none at rest);
  - pushed into a wall, then sliding along it, held far behind it, and pulled back out;
  - a 30 cm pass through a 3 cm wall;
  - fresh placement inside a wall, and the fallback;
  - pressing on a desk top, and reaching under it from its side;
  - coming off the edge: a slide, then a capped glide;
  - a pole swept into a long weapon's side;
  - letting go after 30 frames;
  - a query that cannot run, back faces, and a non-finite probe.
- `surface_stop_lane`, the lane against a fake wall. It covers:
  - one ray per probe in the open;
  - a held right hand, with its hold line naming the static wall and its overlay;
  - a repeated sample;
  - the menu and off gates;
  - a 65 cm weapon whose muzzle is in the wall: the hand is held back until it touches,
    with the drawn correction taken out the next frame;
  - `weapon 0`;
  - the foregrip skip;
  - a snap turn's reset;
  - a refused query;
  - the report.
- `physical_runtime`: the surface stop wants the box; the box carries its correction and
  grip pose; `TrackedWeaponBox` moves it back in the world and in tracking space alike.
- `swing_lane`: with the drawn wrench 10 cm back, the query is where the controller holds it.
- `vr_options` and `vr_options_runtime`: the option is on the Hands tab, on by default, and
  reaches the lane.

## Headset checklist (PreyVR-53)

Debug overlay on (physical layer) to see the rings and dots. Add `surface log 1` for the
event lines.
1. **Free hands:** put each hand flat on a wall, then push through it. The drawn hand stops
   at the wall and slides along it as you move. Push far through; it stays at the wall.
   Pull back; it rejoins your hand with no jump.
2. **Desk:** press down on a desk top; the hand rests on it. Slide off the edge with your
   hand below the top: it glides down to your hand. Reach under the desk from its side.
3. **Floor:** reach down to the floor and pick something up.
4. **Pistol and shotgun:** push the muzzle into a wall; the weapon and hand stop. Swing the
   weapon sideways into a door frame. Fire while held back: the shot leaves the drawn
   muzzle. Look at `Shot ... spawn` in the overlay.
5. **Two hands (Q-Beam or similar):** hold the foregrip and push the barrel into a wall.
   Both hands stop with the weapon.
6. **Wrench:** swing at a wall, at glass and at an enemy. Strikes and sounds as before, glass
   breaks, and the drawn wrench stops dead at the wall. Press it slowly into a wall: it
   stops, no strike.
7. **Grab through glass:** reach through a window at an item behind it. It should not be
   selectable.
8. **Snap turn and recentre** with a hand in a wall: no odd jumps afterwards.
9. **Carry** an object into a wall with the left hand: nothing worse than before.
10. **Afterwards:**
    - the report's `usAvg`/`usMax` and `raysAvg` give the cost;
    - `static`/`other` say what held the hands;
    - `backFaces` should be low;
    - `fallback`/`unconverged` should be rare.
    - The hold lines say whether doors and big props stop the hand (static) or not.

## Risks

- **Coverage:** rays between probe points can miss thin features (pipes, railing bars,
  sharp edges), so a sliver of hand may still clip them. The upgrade is swept spheres
  (`CollideEntityWithPrimitive` itype 4, known from the contact lane) or
  `PrimitiveWorldIntersection` (world `+0x1E0`, layout partly known, unproven). Either one
  must start outside the surface.
- **Cost:** in the open, about 9 rays a frame per free hand and up to 17 for a weapon hand,
  and none at rest. Held hands cast two or three rounds. They run on the game thread under
  the world lock the physics step also takes. The report measures it.
- **Head in a wall** (leaning past the capsule): the lane is gated off by `BodyHeadOut`, as
  the contact lane is. The view is blacked out then anyway.
- **Weapon shape is one frame old** (reload and recoil animations move the weapon in the
  hand). Near full arm extension, the reach compression can leave a one-frame error of a
  few centimetres when the correction engages.
- **Holstering:** for up to 100 ms the weapon box is stale and still probed.
- **Divergence:** the drawn hand can end up far from the real one. There are no haptics
  (decision 3), and a ghost hand cannot be drawn.
- **Heavy or immovable props** (PreyVR-53): rigid, not static, so the hand still passed
  into them. PreyVR-54 adds them (below).

## Decisions (user, 2026-10-21)

1. **What stops the hand:** fixed scenery only, for now.
2. **Weapons too**, including the wrench (strikes follow the real swing).
3. **No haptics.**
4. **On by default.**
5. **Keep the hand at the surface** however far the real one goes; no release distance.

### PreyVR-53 in the headset (2026-10-21, `player-20261021-191608`)

The wearer confirmed that hands and weapons stop at fixed scenery, and that doors count as
fixed scenery.
- **Holds:** 45, all of them static scenery (`static=45 other=0`).
- **Paths:** 7 `unstuck`, 0 `fallback`, 9 `unconverged`.
- **Cost:** 37-39 µs per frame on average, 334 µs at most; about 26 rays a frame.
- **The contact lane, same run:** 399 of 472 pushes were force-limited. The hand outran
  heavier props and sank into them, the case the next part takes on.

## Heavy and immovable objects (PreyVR-54)

**Decisions (user, 2026-10-21).**
1. Loose objects **heavier than 10 kg** stop the hands and weapon. The threshold is a VR
   Options setting.
2. A heavy object holding a hand back is still **pushed by pressing** into it.
3. **Corpses stay pushable** and never stop a hand. The exception is a corpse the game gave
   no mass: it cannot be pushed anyway, and is immovable.

**Masses seen** in the contact lane's logs (10-18 to 10-20):
- props of 1, 3, 5, 7 and 15 kg, and one of 10,000 kg;
- bodies of 5 to 110 kg, and one of zero mass.

**What is new.**
- **The rays see loose objects.** The surface stop's rays ask for scenery and loose objects
  (`scene::SceneryAndLooseObjects`: static, sleeping and awake rigid, terrain), through
  `QueryObjectsBetween`. That is the reticle's query asking fewer classes. They skip the
  player and the carried object (`CarriedPhysics`), so a carrying hand is never blocked by
  what it holds.
- **What a ray met, weighed** (`Classify`, once per object per frame):
  - **Scenery:** anything that is not a rigid or articulated body. It holds the hand back,
    as in PreyVR-53.
  - **Rigid prop:** its mass from the dynamics status (`phys::QueryDynamics`). With **no
    mass** it is immovable and holds the hand. **Heavier than Heavy Objects** it is heavy
    and holds the hand. Otherwise it is light, and the ray goes on past it.
  - **Articulated body:** one with a host (a live character's skeleton) is never in the
    way. A corpse is in the way only if it has no mass.
  - **A mass that cannot be read** lets the ray on (`unread`).
  - **The gate:** objects are told apart by the contact lane's own vtables, once its
    loose-body contracts pass (`GateLooseBodies`, ContactLane.h: the hashed rigid and
    articulated GetType/GetStatus behind their slots). Without them the rays ask for
    scenery alone, as before, and the report says `loose=0`.
- **Passing a light object:** the ray is cast again from 1 mm inside it. The engine's faces
  are one-sided, so it meets what lies beyond. At most 4 times per ray; past that, the rest
  of the ray counts as clear.
- **The press** (`surface::Press`, carried in `GameplayPoseFrame::surface`):
  - Each plane now remembers its collider. When a hand is held back by a heavy prop, the
    lane hands the contact lane the deepest one: the prop, the point, its normal, and how
    far past its surface the real hand is.
  - The contact lane adds it as a contact of kind `press`. It ranks before an overlap or a
    sweep on the same object.
  - It is pushed through the usual solver at `min(6, 10 × depth)` m/s into its surface
    (10 cm past: 1 m/s), within the hand's force (400 N × Push Strength). A 15 kg prop
    gains up to 0.3 m/s a frame; 10,000 kg does not move.
  - The prop slides until its face reaches the real hand, and the drawn hand rides on it.
    What the player stands on, and what the wrench just struck, are skipped as for any
    push.
  - No pulse (decision 3 of the first part: no haptics when a hand meets a surface).
- **The option:** VR Options > Hands > **Heavy Objects** (`heavy_mass`, 1-100 kg, default
  10, shown in KG), under Hands Stop at Surfaces. The command is `surface heavy <kg>`.
- **Shared code:**
  - `phys::ArticulatedHosted` is now in PhysicsCalls.h, and the contact lane uses it.
  - The contact lane's body gate is exported as `GateLooseBodies`.
  - `QuerySceneryBetween` is `QueryObjectsBetween` with the scenery classes.
- **Report:**
  - `preyvr_surface` adds `heavyKg`, `loose` (the gate), `heavy` and `immovable` (holds by
    what held them), `presses`, `passedLight`, `passedBodies`, `passedLive` and `unread`.
  - Hold lines say `met=scenery type=` or `met=heavy|immovable mass=`.
  - `preyvr_contact` adds `pressed` (press contacts) and `pressPushes`. Its overlay status
    counts `press`.

**Tests** (`ctest` 91/91):
- `surface_stop_lane`: a fake world of boxes owned by fake engine objects. It covers:
  - the rays' classes and the carried-object skip;
  - a 50 kg crate behind a 2 kg can: held at the crate's face, the can passed through, the
    press 6.5 cm deep, and its hold line;
  - Heavy Objects at 60 kg: the crate pushed through instead;
  - the 1-100 clamp;
  - a massless prop: held, with no press;
  - corpses: a 30 kg one passed, a massless one holding, a hosted one passed;
  - the carried object skipped;
  - without the contracts, scenery alone.
- `contact_lane`: a press on a 15 kg box. It covers:
  - a push along the press at 0.3 m/s (the force limit);
  - its line, and no pulse;
  - the counters;
  - a press beating the weapon's overlap on a can, at 0.5 m/s into its surface;
  - a press on scenery pushing nothing.
- `vr_options` and `vr_options_runtime`: Heavy Objects sits under Hands Stop at Surfaces,
  10 kg by default, 1-100 kg, reads "10 KG", and reaches the lane.

**Headset checklist (PreyVR-54)**, with `surface log 1` and `contact log 1` for the lines,
and the debug overlay on:
1. **Heavy props:** push a hand and the weapon into a big crate or heavy prop. The hand
   stops at its face. Keep pressing: it slides away slowly, and the hand rides on it. Stop
   pressing: it stops. The lines say `met=heavy mass=` and `push ... kind=press`.
2. **Light props:** cans, bottles and small boxes are pushed as before. The hand does not
   stop at them.
3. **Immovable props** (`met=immovable`) stop the hand and never move.
4. **Heavy Objects:** raise it above that prop's mass (its hold line gives it). The prop is
   pushed through again. Set it back.
5. **Corpses** are pushed as before and never stop the hand. A live enemy never stops it
   either.
6. **Carry** a heavy object with the left hand: the hand is not blocked by it. Push it
   against another heavy prop: the carried one stops there by its own physics.
7. **Standing on a heavy crate:** reach down and press on it. The hand stops on it, and it
   never slides out from under you.
8. **The wrench on a heavy crate:** strikes as before. The drawn wrench stops at its face.
9. **Afterwards:**
    - `preyvr_surface report`: `loose=1`, and `heavy`/`immovable`/`presses` above 0;
      compare `usAvg` with PreyVR-53's 37-39.
    - `preyvr_contact report`: `pressPushes`.

### PreyVR-54 in the headset (2026-10-21, `player-20261021-194310`)

The wearer confirmed it works as expected.
- **Loose objects:** `loose=1`, so the contracts passed.
- **Holds:** 40, of which 24 were static scenery and 16 heavy props. There were 666 press
  frames, giving 608 press pushes, and 38 rays passed through light props.
- **Paths:** no fallback, 2 `unstuck`, 3 `unconverged`.
- **Cost:** 22 µs per frame on average (463 µs at most), about 23 rays a frame. That is
  below PreyVR-53's 37-39 µs.
- **The props pressed were heavy:** every contact-lane push was force-limited
  (`limited=667` of 667), and `dvMaxCms=0`.

**Known limits.**
- A heavy object moving *toward* the hand faster than the 2 cm path backoff per frame (one
  knocked loose, or swinging) can swallow the drawn hand. Objects the hand pushes move away
  from it.
- A light object stacked in front of a heavy one still gets pushed into it by the hand, as
  before. It is the TODO item about pushing objects through other objects.
