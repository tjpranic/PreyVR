# Leaning push-back and bent arms: investigation, 19 October 2026

**The turning stall this investigation also covered now lives with the rest of that bug, in
[MOVE-STOP-2026-10-24.md](MOVE-STOP-2026-10-24.md) (cause A).** Section 1 and fix 1 are kept here
as pointers, so the section and fix numbers others cite still hold.

Status, 2026-10-19:
- **Fix 5 (the arms):** worn OK, and committed as `079e55f`.
- **Fix 2 (the push-back):** worn OK in PreyVR-42, with fix 1 (the stall). See "Fixes 1 and 2 as
  built".
- **Fixes 3 and 4 (leaning past furniture):** waiting on a decision.
  - **2026-10-24:** seen again in PreyVR-65/66 as a fade when leaning over a desk (run `player-20261024-113604`: 3 fades, 86 frames, 0 head hits, with `fitShortened` climbing). It is the 30 cm residual rule fix 3 replaces. You chose to leave it for now.

It covered three TODO items:
- The movement stop on turn still exists. It is not as bad as before, but it still needs fixing.
  **Now in [MOVE-STOP-2026-10-24.md](MOVE-STOP-2026-10-24.md); fixed 2026-10-24.**
- Physically leaning is wonky. The game's collision seems to act on the player's character
  model and moves the player back to resolve the contact.
- Arm IK bends the arms inward. There may not be enough room between shoulder and wrist.

Evidence used:
- The code on `custom` at `fa0ce02`.
- The `body={...}` status counters from every headset run on 10-17 and 10-18
  (`%LOCALAPPDATA%\PreyVR\runs`).
- Two simulations that copy the mod's own formulas.
- A static read of the Steam `PreyDll.dll` (SHA-256 `7d6e322f...05311a7`; RVAs from
  `0x180000000`). The key instructions were re-checked from the file's bytes
  (`verify_teleport.py`).

Scripts, dumps and outputs are in
[evidence/turn-lean-arms-2026-10-19/](evidence/turn-lean-arms-2026-10-19/). Nothing here has
been run in a headset since the TODO items were written.

## Summary

| Symptom | Cause | Confidence |
|---|---|---|
| Walking stalls on a turn | Room-scale's `SetPos` is an engine teleport: [MOVE-STOP-2026-10-24.md](MOVE-STOP-2026-10-24.md), cause A | |
| Leaning pushes you back | Room-scale moves the 35 cm-radius capsule with your neck. Its 9-ray sweep misses table tops and edges, so `SetPos` puts the capsule inside furniture. The engine's next step pushes it out, and the mod does not cancel that push in the view | Every link **proved** from bytes or code. The chain as a whole is **inferred** |
| Arms bend inward | The arm hangs from a shoulder fixed to the *eyes*. Looking down drops it 5–11 cm towards the hands. The drawn arm is also a fixed 56 cm, whatever the player's arm length | **Proved** from code; sizes from simulation |

**The character model is not what collides.** The body mesh's physics is a separate entity
(simulation class 4), and the player's step skips it. What collides is the physics capsule,
which room-scale moves under the head.

## What the three have in common

All three (the stall included) come from the `BodyFollow` / arm code estimating the player's body
from the headset alone, with three separate approximations:
- **Room-scale** follows a point 8 cm in front of the neck, along the lagging torso yaw.
- **The arm solver** hangs the shoulders off the eyes.
- **Neither** has a notion of leaning.

Room-scale then moves the capsule the one way the engine reads as a teleport.

## 1. Movement stops on a turn

Moved to [MOVE-STOP-2026-10-24.md](MOVE-STOP-2026-10-24.md), cause A: what runs on a turn, the
engine's teleport (proved from bytes), why turning made room-scale move (`follow_sim.py`), and why
the stand-aside guard did not keep the moves out of a walk. The smaller items listed here (the
stick released on a refused tracking read, or with the head untracked) are its cause C.

## 2. Leaning pushes the player back

### What happens on a lean

1. **Room-scale moves the capsule with the neck.** Leaning moves the follow point forward, and
   room-scale glides the capsule after it.
2. **The capsule is much wider than a person.**
   - It is a capsule of radius `r` (0.35 m measured in the contact work), spanning `O` to `H`
     above the feet. Those come from the stance's Lua data, through `0x157D3A0`.
   - A torso is about 12–15 cm from its centre to its front, so the capsule meets furniture
     20 cm or more before the player would.
3. **The sweep cannot see much of what the capsule hits.**
   - Room-scale checks a move with 9 horizontal rays (`roomscale::SweepRays`): three heights,
     **0.5, 1.0 and 1.5 m** (the room-scale doc says 0.45/0.9/1.35; the code uses these), from the
     capsule's centre line and both sides.
   - A table top or desk edge at 0.7–0.8 m lies between two heights.
   - A corner that meets the round front of the capsule between the three lines is missed too.
   - `SetPos` then puts the capsule inside the furniture.
4. **The engine pushes it out.**
   - The move marks the body airborne (the teleport, MOVE-STOP A.2), so the next physics step runs.
   - The step first tests for overlap at the current position (`0xB4ED4A`). Because the player
     is `pushable_by_players` (`m_flags` `0x643` has bit `0x200`; test at `0xB4F421`), it moves
     the capsule out by the depth plus a small gap.
   - The push is along the contact direction in full 3D, up to 2.5·r per overlap
     (`0xB4F41D`–`0xB4F548`).
   - A low edge can lift the player instead, through the same push or the ground probe's
     step-up.
5. **The view goes back with it.**
   - The physics position is synced to the entity after the step (`0x96C010`). The view is the
     entity position, plus the `#camera` attachment of the player's character (`0x1489010`),
     plus the head offset.
   - The mod moved the origin only by its own `SetPos`. The push-out is not cancelled, so the
     view moves back.
6. **Then it repeats.**
   - The head is now ahead of the capsule again, so the follower moves again and is pushed out
     again.
   - That is the wonky feel. `blocked` (frames the sweep clamped a move) reached 963 in one 10-18
     run. There is no counter for the push-out itself yet.

### What the character model does

**Nothing.** The step collides with the living entity's own collider (part 0) and with any part
whose `flagsCollider & 0x1FFB`; there is exactly one part (`0xB441D0`). The character's skeleton
gets its own physics entity, with the living entity as host (`0x96D930`), set to simulation
class 4, which the step skips. There is no head sphere either.

### A related limit, not the push

The head-in-wall fade also triggers when the head is 30 cm from a capsule the sweep stopped. A
35 cm capsule stopped at a desk therefore limits how far you can lean over the desk before the
view goes black. The 10-18 runs show no fades (`headOutEvents=0`), so this was not what the
wearer saw.

## 3. Arms bend inward

### Where the drawn arm comes from

`PrepareArms` / `SolveArms` in `AnimIkTakeover.cpp`, with `armpose`:
- **The shoulder** is `ShoulderTarget(eye, torso)`: the HMD centre eye plus (−9 cm forward,
  ±18.5 cm out, −23 cm up) in the torso frame. It is reached by turning the rig's clavicle, which
  may stretch by 15%.
- **The arm** is a fixed 28.5 + 27.5 cm. It may stretch by up to 10%, and past 92% of that the
  wrist goal is pulled in softly (`SoftReach`).
- **The wrist goal** is the player's real wrist: the controller grip, less the palm offset.

### Two errors, both shortening the shoulder-to-wrist distance

1. **The shoulder moves with the eyes.**
   - When the head pitches about the neck (the mod's own `kNeckFromEye`: 7.5 cm down, 8 cm
     back), the eyes go forward and down, and the drawn shoulders go with them.
   - Looking down 15/30/45/60° moves them by +1.7/+2.7/+2.9/+2.5 cm forward and
     −2.3/−5.0/−7.9/−10.7 cm down.
   - That is towards the hands, which is where you look to see your arms. A real neck flexing
     moves the eyes further still.
   - Turning the head moves them sideways by the same mechanism.
2. **The arm length is the average adult's, not the player's.** A player with shorter arms gets a
   bent elbow even looking straight ahead.

`arm_sim.py`: the elbow angle (180° = straight) the mod would draw, against the player's real
one, for the same real arm. The real arm's shoulder is where the model puts it with the head
level.

| Pose | Player's arm | Head pitch | Real | Drawn |
|---|---|---|---|---|
| Arm straight forward | 56 cm | 0° | 169° | 169° |
| Arm straight forward | 56 cm | 30° | 169° | 146° |
| Arm straight, 30° down | 56 cm | 45° | 169° | 126° |
| Arm straight forward | 50 cm | 0° | 169° | 126° |
| Arm straight, 30° down | 50 cm | 45° | 169° | 103° |
| Hands at the belly | 56 cm | 45° | 89° | 69° |

Once the arm has to fold that much, the elbow chooser places the extra bend round the
shoulder-to-wrist axis. That is the "inward" bend.

**Why the mock never showed it.** The October mock validation built its "true" arms with exactly
the model's shoulder offsets and arm length, and with the head level
(`evidence/arm-pose-2026-10-06/truth-gloo.txt`: `shoulder 0.0cm` and `upper/fore 28.5/27.5`
everywhere). Neither error could appear there.

**On scaling the player to the character instead:** a position scale changes how big the world
feels and moves the hands with it. The drawn arm is already the mod's choice, not the rig's (Prey's
arm is 51.6 cm and the mod stretches it), so the cheaper fix is to make it the player's.

## What can be done

**The wearer's requirements (10-19):**
- **The body keeps moving with the player, as a real body does.** No fix may hold it back:
  - no deferring room-scale while the stick is in use;
  - no large step deadzone;
  - no turning room-scale off.
- **The player can lean over desks and railings without being pushed back.** The body bends at
  the waist to allow it.

The plan below is revised to meet them. Under "Dropped" is what the first draft proposed and why
it went.

### 0. Live checks (confirmation only)

These confirm the causes. **Turn Only and a 200 mm step are diagnostics, not fixes.** Each check
is written as hypothesis, control, variable and decision rule. Read `body.report` after each.

1. **Turning stall.** Moved to [MOVE-STOP-2026-10-24.md](MOVE-STOP-2026-10-24.md) ("If it comes
   back").
2. **Lean push-back.**
   - Hypothesis: the push is the capsule placed into furniture by room-scale.
   - Control: Body Step Distance 30 mm.
   - Variable: 200 mm.
   - Test: lean up to 20 cm over a desk.
   - Decision: if the push goes, it is room-scale, and fixes 2–4 are the cure.
3. **Arms.**
   - With both arms straight out, run `arm.report`, looking ahead and then down at the hands.
   - Expect `shoulderToTargetCm` near 0 both times, and the elbow bending when looking down.

### 1. Move the capsule without the teleport branch: the stall fix (small–medium)

Moved to [MOVE-STOP-2026-10-24.md](MOVE-STOP-2026-10-24.md), A.5: `pe_params_pos{pos}` with
`bRecalcBounds 0x21` before the entity's `SetPos`, while the body is moving and the physics idle.

**Ship it with fix 2.** Without the teleport's airborne flag, an overlap is no longer pushed out
at once; it would pop when the body next moves.

It may also explain the `bFlying` flicker the contact work found while the player stood on a light
object: every room-scale move set that flag.

### 2. Ask the engine whether the capsule fits before moving it (medium)

Replace the 9-ray sweep with the living entity's own stance check:
- Call `GetStatus` with type `0x15`. Layout:
  - pos `+0x04`, q `+0x10`, sizeCollider `+0x20`, heightCollider `+0x2C`, bUseCapsule `+0x40`;
  - dirUnproj in/out `+0x30`; unproj out `+0x3C`.
- It runs the step's own part filter (`0xB56900`) and returns 1 when there is no overlap.
- Bisect a refused move to the largest fraction that fits.

**Effect:**
- The capsule is never placed in furniture, so the engine never pushes it out.
- The body stops at a desk or railing as a real one does, and follows everywhere else.

The alternative is a real-capsule sweep with the per-entity primitive test (`0xBA1D80`), but it
needs the broadphase and the same filters by hand.

### Fixes 1 and 2 as built (2026-10-19)

Fix 1 (the move that keeps its motion, its moving test, `PhysicsIdle` and its counters) is described
in [MOVE-STOP-2026-10-24.md](MOVE-STOP-2026-10-24.md), A.5 and A.6. Fix 2 is here.

**Static results** (both fixes).
- `tools/re/verify_room_scale_physics.py` passes 41 checks (52 after PreyVR-43 hashed the change
  request).
- `include/preyvr/RoomScaleNative.h` hashes the bodies:
  - the living `SetParams` (`0xB49610`);
  - the base `SetParams` (`0xB88AA0`);
  - the stance test (`0xB56900`);
  - the physics proxy's XForm (`0x96BE00`);
  - the change request (`0xB1BCD0`, PreyVR-43).
- ContactNative.h's living and base `GetStatus`/`GetParams` rows are gated as well.
- The scripts and dumps are in the evidence folder (`scan_stance.py`, `rows.py`, `fit*.txt`).

**The stance test is `pe_status_check_stance` (type `0x15`).**
- The base `GetStatus` does not handle that type (it returns 0 for it), so the living one does.
- The test returns 1 when nothing overlaps. Otherwise it writes the depth to `+0x3C` and, with a
  zero `dirUnproj`, the contact's normal to `+0x30`.
- It skips the body itself, anything with the same owner, and what ignores collisions with it.
- It runs on the game thread's query slot (4), the one the scene query uses.
- Game code never calls it: only the living entity does, around stance changes.

**What it does, each room-scale move** (`RoomScaleMove.cpp`). Reading the body, waiting out a
step-up and the move itself are fix 1's (MOVE-STOP A.5). Between them, fix 2 tests the fit:
- First, does the body fit where it stands?
  - If yes: if the move's end fits, take it all. Otherwise bisect 6 times and take the largest
    part that fits, within 1/64 of the first contact.
  - If no (something the test counts but the step ignores, perhaps): the 9 rays decide, as
    before (`fitStartBlocked`).
- A refusal by a contact above or below rather than a wall is counted (`fitVertical`). A staircase
  being refused would show there.

**Unchanged:** the head-in-wall fade on the 30 cm residual. Deciding that is fix 3.

**Diagnostics:**
- `pushOut` and `pushOutMaxMm`: frames the capsule moved sideways by more than 5 mm while at rest,
  with no walk asked for and none of our own moves. That is the engine pushing it out of
  something, which is the lean push-back measured directly.
- The first frame logs `room_scale_dimensions`: radius, half-height, heightCollider, pivot,
  capsule, and `bottomMm`. `bottomMm` is how high a step can be before the stance test refuses it.

**Switch:** `body.physics 0` restores the rays and the plain `SetPos`, for A/B.

**Tests.** `room_scale` covers the bisection: a clear move is taken whole; a desk 0.37 of the way
stops the body within 1/64, on the clear side; touching already means no move.

**Headset checklist (`body.report` after each):**
1. **Leaning.** Lean over a desk and towards a wall, as before.
   - Expect no push back.
   - `pushOut` stays near 0 and `fitShortened` climbs.
   - Compare with `body.physics 0`: `pushOut` should climb there.
   - The fade may still come on 30 cm past the desk. That is fix 3's to change.
2. **Stairs and ramps.** Step about physically on stairs and a ramp, at rest.
   - The body should follow.
   - If it sticks, read `fitVertical` and the `room_scale_dimensions` line.
3. **Walls in the open.** Walk physically into a wall.
   - The body stops at it and the fade comes on as before.
   - `fitStartBlocked` stays low. If it climbs in open space, something the test counts is
     overlapping the body, such as a carried object; note what you were holding.

### Fix 2 worn (PreyVR-42, 2026-10-19)

Runs `player-20261019-194513` and `-195350`. The wearer reported **the push-back is gone**. (The
walk no longer stalled either, and the body lagging mid-walk led to PreyVR-43: MOVE-STOP A.6.)

**Counters:**
- `pushOut` 25 in four minutes, at most 73 mm. PreyVR-43: 10, at most 50 mm.
- The collider: radius 350 mm, half-height 335 mm, `bottomMm=530`, so a physical step onto a stair
  is never refused.
- `fitShortened` climbed fastest while the fade fired: the lean test, as intended.
- `fitStartBlocked` climbed while the wearer carried bins and buckets. The stance test counts a
  carried object overlapping the body, which the step ignores, so those moves fell back to the
  rays. PreyVR-43: 2.

### 3. Lean past a blocked body (medium)

**The idea.** When fix 2 stops the capsule at a desk or railing, the head is allowed to lead it.
That distance becomes the upper body's lean instead of being treated as a head in a wall.

**What changes:**
- **The fade no longer fires on the 30 cm residual.**
- **It fires on a body line instead:** a segment from a hip point on the capsule's axis, to the
  neck, to the head. The fade comes on when that line passes through geometry, or when the head
  is further than a bent torso can reach (about 55 cm; to be tuned).
  - Over a desk or a railing the line is clear.
  - Into a wall it is not.
  - Folding down over a railing until your belly would be on it is not.
- **Trigger and use stay blocked while faded,** as now.
- **The engine's eye stays on the capsule.** The view is still the head, as it is today.

**Optional, later: a pelvis estimate for leans in open space.**
- The capsule would follow a hip point rather than the neck when the head drops and moves
  forward together, as a lean does.
- Walking keeps the head at standing height, so stepping still moves the body at once.
- Fix 3 alone covers desks and railings. This only matters for how the legs look when leaning in
  the open.

### 4. Bend the body at the waist (medium–large; research first)

**The idea.** After the native animation pass (the same place the arm is written), rotate the
rig's spine so the torso runs from the capsule's hips to the player's neck. The visible torso
then leans with the player. The clavicles, and so the arms' shoulders, ride the bent torso.
- Fix 5 needs this during a lean: a clavicle can reach only 15% past its length, so beyond about
  15 cm of lean the shoulders cannot get there from an upright torso.
- With the bend placing the upper body, the follow point's 8 cm term along the torso (the cause of
  the turn-orbit) may no longer be needed. Revisit it then.

**To settle first, live:**
1. **The spine joints.** Name them on the 101-joint rig; `ik.skel` dumps every joint.
2. **Which instance to write.** The near-pass instance draws the first-person body and the world
   instance casts the shadow (R-085). Find which shows the torso and legs, and write both if
   needed.
3. **Whether the bend moves the engine's eye.** The eye is the `#camera` attachment of the slot-0
   character. If that rides the head joint, a spine bend would move the view. Measure the eye with
   and without a test bend (`pose.marks`, and `posOffsetMm` in `report`), and compensate if it moves.
4. **Whether the native pass fights it.** The arm writes are made after the pass and hold, so the
   spine is expected to hold too.

### 5. Arms: shoulders from the neck, and the player's arm length (small; unchanged)

- **Shoulders.** Use `ShoulderTarget(roomscale::FollowPoint(head, torso), torso, ...)`: the eye
  as it would be with the head level and facing the torso. Head pitch and yaw then no longer
  move the shoulders, while a real lean still does.
  - Build the shoulder frame so it can take the torso's lean pitch from fix 4 later.
  - The `arm.shoulder` offsets keep their meaning.
- **Arm length.**
  - Add an Arm Length option, with the 28.5/27.5 split kept.
  - Or add a one-press calibration: arms straight forward, take the shoulder-to-wrist distance.
  - Standing eye height (LOCAL_FLOOR) could seed a default.

**Expected:** a straight real arm draws straight at any head pitch. It does not depend on fixes
1–4, so it can go first.

**Implemented 2026-10-19** (details in
[ARM-POSE-2026-10-06.md](ARM-POSE-2026-10-06.md#2026-10-19-shoulders-from-the-neck-and-the-players-arm-length)):
- `armpose::ShoulderFromHead`, with `arm.anchor 0|1` for A/B.
- The Arm Length option (`arm_length`, `arm.length <cm>`).
- `reachCm` and `reachMaxCm` in `arm.report`.
- `ctest` 85/85.

The lean-pitch hook for fix 4 is not built yet.

**Headset checklist:**
1. **Look down at straight arms.** Hold both arms out straight, look ahead, then look down at the
   hands. The drawn elbows should stay straight. Compare with `arm.anchor 0`.
2. **Read your arm length.** Send `arm.report`, hold both arms out straight for a few seconds,
   then send `arm.report` again. `reachMaxCmR/L` is your arm length; set Arm Length to it
   (Hands tab).
3. **Check it fits.** With your length set, a straight arm draws straight, and at full reach the
   hand still meets the controller.
4. **Check nothing else moved.** Turning the head alone does not move the shoulders, and
   two-handed long weapons still reach the foregrip.

### 5b. The torso follows the neck up and down (2026-10-19)

**Reported by the wearer.** Calibrate seated, then stand: the shoulders rose with the neck (fix 5)
while the torso stayed at the calibrated height. Room-scale follows only horizontally.

**Live facts** (tuner, 2026-10-19):
- `arms.joints spine` names the spine:
  - `spine1_jnt` (6) at 1.044 m;
  - `spine2_jnt` (29) at 1.166 m;
  - `spine3_jnt` (30) at 1.309 m;
  - `spine4_jnt` (32) at 1.423 m;
  - `neck_jnt` (34) at 1.530 m, 10.6 cm behind the model's origin.
  
  Room-scale puts that origin about 8 cm in front of the player's neck, so the rig's neck already
  lands within about 3 cm of the player's horizontally.
- `arms.att` puts `#camera` on joint 98, with `vr_headset`, `flashlight` and `psychoscope_light`:
  the head. The game's eye comes from that attachment (`0x1489010`), so moving joint 98 would move
  the game's eye, and the view would count the player's height twice.

**What was built (`AnimIkTakeover.cpp`, `armpose`):**
- **The lift.** The lift is the neck's rise since calibration: the view's vertical head offset
  (`cameraCentre − nativeEye`), less what tipping the head moved the eye (`LevelEyeHeight`).
  It was limited to 40 cm up and 15 cm down at first; since 2026-10-20 it is unlimited by default
  (worn OK in PreyVR-44, then uncapped at the wearer's request). `arm.lift 1 <upMm> <downMm>` can still
  set limits (`TorsoLift`).
- **Which joints move, read once per rig.**
  - The four spine joints, by name.
  - The joint `#camera` rides, read from the character's attachments (`AttachmentJoint`, shared
    with `arms.att`).
  - Every joint's parent, from the joint table.
  - From those, each joint takes the share of its nearest spine ancestor (`LiftClasses`), and the
    camera's joint and everything under it take none.
  - The rig logs `detail=torso_lift spine= base= camera= lifted=`.
- **Shares.** Each spine joint's share follows its height above spine1's parent (`LiftShares`):
  spine1 about 20%, rising to spine4's whole. So the stretch spreads over the waist and lower back
  rather than one joint.
- **Order.**
  - Planned before the native pass, so the collarbones are placed from the lifted torso.
  - Applied to the absolute pose after the pass, before the fingers and the arm solve, so the
    arms are solved from the raised shoulders to the same hand goals.
  - The hands stay on the controllers.
- **Switch.** `arm.lift 0|1 [upMm [downMm]]`; on by default.

**Tests (`arm_pose`):**
- Tipping the head does not change the lift.
- The limits hold.
- A Prey-shaped hierarchy gives the right share per joint: the camera's subtree stays, a helper
  under spine2 takes spine2's share, and a leg takes none.
- A cyclic parent table lifts nothing.
- Prey's measured heights share upwards.

`ctest` 85/85. Worn OK in PreyVR-44 (committed as `7088229`).

**Headset checklist:**
1. **The case reported.** Calibrate seated (Setup → Calibrate Posture), then stand up and look
   down. The chest and shoulders rise together, the waist stretches, and the hands stay on the
   controllers.
2. **The view does not move with it.** Nothing jumps or drifts when standing up. `arm.report`
   shows `liftReady=1` and `cameraJoint=98`.
3. **Compare.** `arm.lift 0` brings back the shrug.
4. **Everyday play is unchanged.** Calibrated standing, `liftMm` stays within a few centimetres,
   and a weapon held in both hands still meets the foregrip.

### Order

| Step | What | Fixes |
|---|---|---|
| 1 | The stall and the push | 1 and 2 together |
| 2 | Independent of the rest; can go first | 5 |
| 3 | Lean over desks and railings | 3 |
| 4 | The visible bend | 4: research, then the bend |
| 5 | Optional | The pelvis estimate |

### Dropped from the first draft

- **Gating room-scale on stick intent** (a stall fix): see MOVE-STOP A.5.
- **A larger Body Step Distance as the lean fix.** The body would lag small steps. It stays only as
  diagnostic 0.2.
- **A narrower capsule in VR.** It is not needed for leaning once fixes 3 and 4 exist, and it
  changes gameplay collision. One side effect remains: standing upright at a desk, the real body is
  15–20 cm closer than the 35 cm capsule can get. With fixes 3 and 4 that shows as a slight forward
  bend. Revisit it only if that looks wrong.

### Lower priority

- **Stick hold.** Done 2026-10-24 as `move.hold` (MOVE-STOP, cause C).
- **Turn threshold.** Skip body turns under about 0.05°. That saves the XFORM fan-out on most
  frames, including the animated character's re-sync.
- **Push-out counter.** Done in fixes 1 and 2 as built (`pushOut`).

## Engine facts recorded here (Steam hash)

The teleport's chain (the transform update, the physics component's XForm, the living
`SetParams`), the ArkPlayer and animated-character XFORM handlers and the movement path are in
[MOVE-STOP-2026-10-24.md](MOVE-STOP-2026-10-24.md), with the rest of the stall.

| What | Where | Evidence |
|---|---|---|
| Living Step; overlap push-out (`pushable_by_players`, up to 2.5·r) | `0xB4BF60` (slot `+0x100`); `0xB4F41D`–`0xB4F548`; constant `0x1C85B60` = 2.5 | bytes re-checked |
| Living constructor: one part, `flagsCollider` 2, `m_flags` `0x643` | `0xB441D0` (`0xB4472D`, `0xB4473B`, `0xB44819`) | bytes re-checked |
| Player physicalize (capsule, per-stance table `ArkPlayer+0x1610`) | `0x157EDE0`, stance table `0x157D3A0` | decompile |
| Skeleton physics is a separate entity (sim class 4) | `0x96D930` | decompile |
| Native eye = entity rotation × `#camera` attachment, plus entity position | `0x1489010`, `CView::Update` `0x3D9B9D` | decompile |

The CRYENGINE GitHub source was not reachable (404) and the CryEngine trees named in `AGENTS.md`
are not on this machine. CryEngine names above are by signature, not by symbol.
