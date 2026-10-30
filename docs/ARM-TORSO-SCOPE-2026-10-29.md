# The arms clip into the drawn body: cause and scope (2026-10-29)

Branch `custom`. TODO: "Arms can clip into the player model". The wearer's report: with the controllers
held close to the chest, the arms go into the drawn body, mostly around the elbow. They asked whether an
IK change, collision spheres on the elbows, or something else would fix it. Investigated offline; step 0
(below) was then built as PreyVR-103, and step 1 as PreyVR-104.

**Headset, 2026-10-29 (PreyVR-104): confirmed working by the wearer.** Run `player-20261029-203533`
recorded no clearance figures: the overlay was off and `arm.report` was not run. After it, `arm.avoid`
lost its on/off switch (below). Step 2 is not needed unless the armpit is seen to clip. The wearer's
`arm.report` on 2026-10-30 agrees ([below](#headset-2026-10-30-preyvr-104-armreport)).

## Why the arm goes into the body

**1. The elbow chooser does not know the torso is there.** With the hand on the controller and the
shoulder placed, the elbow can be anywhere on a circle round the shoulder–wrist line.
`armpose::ChooseElbowPole` ([ARM-POSE-2026-10-06.md](ARM-POSE-2026-10-06.md)) picks a point from three things:
- how the player's real wrist would have to bend;
- the elbow's natural direction (down, out, a little back);
- the last frame's elbow.

Nothing in it stops the arm passing through the torso, so with the hands at the chest it does whenever
the wrist prefers that.

**2. The drawn shoulder sits higher than Morgan's mesh expects.** Measured from the `ik.skel` dump in run
`player-20261025-125133` (PreyVR-84, standing idle, `pre=`: the pose before the IK pass):

| | Out | Behind the eye | Below the eye |
|---|---|---|---|
| Morgan's shoulder (`r_upperArm_jnt`) | 20.1 cm | 11.6 cm | 28.1 cm |
| The mod's drawn shoulder | 18.9 cm | 9.1 cm | 23.0 cm |

How the mod's shoulder is placed:
- The target comes from `bodyrig::ShoulderFromAtlas`.
- `PlaceShoulder` then turns the clavicle to reach it.
- The clavicle hits its 15% shortening limit, leaving the drawn shoulder 1.2 cm short of the target.

The result is about 5 cm higher, 2.5 cm further forward and 1.2 cm narrower than Morgan's own. The top of
the upper arm moves; the mesh of his chest and armpit does not. No elbow choice can fix that part.

Rig lengths for reference: Morgan's clavicle is 19.6 cm, his upper arm 23.3 cm and his forearm 28.4 cm.
The drawn arm at the wearer's Arm Length of 60 cm is 30.5 + 29.5 cm.

## What the simulation showed

**How it was tested:**
- A harness compiled against the mod's real `ArmPose.cpp` and `VrMath.cpp` (not a port); it agreed with
  `ChooseElbowPole` to within 1 cm.
- The torso was an ellipse stack **guessed** from the rig and an adult's build, since there is no mesh to
  measure: the game's archives are encrypted and there are no captures.
- 4000 natural arms with the hands near the chest, arm 60 cm. Each real elbow was kept outside the
  player's own torso.
- The drawn torso tried at 1.0, 1.1 and 1.2 times the player's size.

| Drawn torso | Arm inside it, now | With a torso term in the swivel scan |
|---|---|---|
| Same size | 20% of poses | 0.1% |
| 10% bigger | 49% | 6.6% |
| 20% bigger | 78% | 35% |

What else it found:
- **The elbow joint itself was inside in only 1–3% of poses.** The deepest point was usually the upper
  arm near the armpit (716 of 816 clipping poses at the same size), then the forearm near the wrist.
- **The torso term also made the elbow more accurate** (mean error 6.6 → 5.4 cm), because real elbows are
  never inside the body.

A 60-second stress run moved the hands around the chest, including unnatural wrist angles:
- **Torso term alone:** 13% of frames still clipped. The 60 ms pole smoothing swings the elbow through the
  body when the choice moves from one side of it to the other.
- **Snapping to the nearest clear angle:** no clipping, but 17–37 cm jumps in a single frame, where the
  nearest clear point switched sides.
- **Continuous tracking:** the elbow moves towards the chooser's angle along the clear part of the circle
  only, at most 4° a frame, and waits at the edge while the wanted angle is blocked. No clipping at the
  same and 10%-larger size. Its largest single-frame jump was smaller than the current chooser's in the
  same run (3.9 / 7.4 cm against 5.6 / 8.6); its p99 per-frame step was 1.2 cm against 0.7.

## Why not collision spheres on the elbows

- **The elbow has one free direction** once the shoulder and the hand are placed: round the circle.
  Pushing it out of a sphere moves it off the circle, which either pulls the hand off the controller or
  stretches the arm.
- **Turned back into a move round the circle**, a push is the torso term done after the solve. It fights
  the chooser and its smoothing every frame, so it jitters where it touches.
- **Elbow spheres miss most of the clipping,** which is along the segments.
- **Engine collision is not available:** the player's hit skeleton (the articulated `ArkPlayer` "Dude" the
  contact lane sees) follows Prey's animation, not the pose the mod draws, and the arm is written after
  physics.

So capsules are the right shape to test against, but tested inside the IK rather than pushed afterwards.

## The plan (approved 2026-10-29)

0. **Fit the torso in the headset (built: PreyVR-103).** Draw capsules on the drawn spine and the arm's
   samples by clearance, make them tunable live, and log the clearance. This fits the capsules to Morgan's
   mesh and shows which part of the arm goes in.
1. **A torso term and continuous tracking in the elbow chooser**, with the capsules from step 0, as pure
   code with unit tests. Built: PreyVR-104 ([below](#step-1-as-built-preyvr-104)).
2. **Only if the armpit still clips:** move the drawn shoulder towards Morgan's mesh. Either lower the
   shoulder towards his, or raise his torso under the neck; the second affects the look-down and throat
   tuning ([BODY-FOLLOW.md](BODY-FOLLOW.md#the-body-pin)). Not built.

**Out of reach of the arm:** a hand pressed against the player's real chest is inside the drawn chest if
Morgan's is deeper. Only the body giving way, or the hands-only mode in TODO.md, would change that.

## Step 0 as built (PreyVR-103)

**The torso:** four capsules, each laid across the body on a spine joint as the pose has it after the body
pin. That is the torso that is drawn: it bends, leans and twists with the body.

| Part | Joint | Forward | Up | Half width | Radius |
|---|---|---|---|---|---|
| `chest` | `spine4_jnt` (32) | 30 mm | −40 mm | 42 mm | 118 mm |
| `ribs` | `spine3_jnt` (30) | 15 | −45 | 41 | 116 |
| `waist` | `spine2_jnt` (29) | −15 | −25 | 38 | 111 |
| `hips` | `spine1_jnt` (6) | −15 | −40 | 50 | 117 |

- **Defaults:** from Morgan's standing spine and an adult's torso: 33 cm wide and 24 deep at the chest,
  29 by 22 at the waist. They are a starting point to fit, not a measurement.
- **The joints' frames:** on this rig a spine joint's local x runs up the spine, y forward and z to the
  left. That holds in the standing, armed and crouched dumps alike.

**The arm:** sampled at 30, 55 and 80% of the upper arm, at the elbow, at 20, 45 and 70% of the forearm,
and at the wrist. Each sample is a ball as thick as the arm is there (`arm.limbs`, default radius 50, 45,
40 and 30 mm). Its clearance is the ball's surface from the nearest capsule's, negative inside
(`armpose::MeasureArm`).

**Where to find it:**
- **Code:** `include/preyvr/ArmPose.h` (Capsule, TorsoShape, ArmThickness, MeasureArm);
  `src/dll/AnimIkArms.cpp` (`PlaceTorso`, the measurement after the solve); the overlay's `kArms` layer in
  `DebugOverlayScene`.
- **Not used by the solve in PreyVR-103:** nothing about the arm changed. Step 1 uses them.

**Commands** ([TUNING-COMMANDS.md](TUNING-COMMANDS.md)):
- `dbg.draw 512` draws the capsules (grey wireframes) and each solved arm, and logs `arm_fit` once a
  second while it is on. Each arm is a thin line with a ring at each sample: green clear of the torso,
  amber within 1 cm, red inside. A dot marks the sample nearest the torso.
- `dbg.draw 608` adds the labels (32) and the status panel (64), which give each arm's deepest sample.
- `arm.capsule <part> <fwd> <up> <half> <radius>` moves or resizes one capsule (mm, radius 0 removes it).
  Bare, it reports.
- `arm.limbs <upper> <elbow> <fore> <wrist>` sets the arm's thickness.
- `arm.report` gives the capsules, the thickness and per arm:
  - `fitR/L`: the deepest sample, its torso part and its clearance in cm;
  - `clearCmR/L`: upper arm, elbow, forearm, wrist;
  - since the last `arm.*` command: `fitFramesR/L`, `insidePctR/L` and the worst, with where it was.
- None of it is kept across launches.

**The log line:** `preyvr_anim_ik arm_fit fitR=upper55/ribs:-1.2 clearCmR=-1.2,3.0,0.4,5.0 fitL=...`.

### In the headset

The overlay is drawn over everything (no depth test), so a capsule's outline shows through the body. Fit
by comparing outlines and by where the red appears:

1. `dbg.draw 608`. Stand in good light, look down at your chest.
2. Compare the capsules' outlines with Morgan's torso, from above and in a mirror if one is to hand. The
   chest capsule's outer edge should sit on the side of the chest under the armpit, its front on the
   front of the chest.
3. Hold the controllers close to the chest, as when the clipping showed. Where the mesh visibly goes in,
   the rings there should be red; where the arm is clearly outside, green. If they disagree, move or
   resize the capsule there (`arm.capsule`) or the arm's thickness (`arm.limbs`) and look again.
4. Then hold the poses that clip for a few seconds each. The `arm_fit` lines record what each did.
5. Report the final `arm.capsule` / `arm.limbs` values, or run `arm.report` once at the end. Also say which
   part of the arm went in: the elbow, the upper arm by the armpit, or the forearm.

Typing a command pauses the game (it loses focus), so change values between looks, not while holding a
pose.

### Headset, 2026-10-29 (PreyVR-103), first run

Run `player-20261029-201049`, with a weapon drawn. The capsules and the arm's thickness were left at their
defaults: the readings say where the arm went, not yet whether the capsules match the mesh.
- **`arm.report`:** 6364 frames per arm. Some of the arm was inside the torso in 9.0% of frames (right)
  and 12.8% (left). The deepest were the right `upper80/ribs` at −10.9 cm and the left `elbow/ribs` at
  −15.6 cm.
- **29 `arm_fit` lines, a second apart:** two kinds of clipping.
  - **Deep, at the elbow.** At 13:57.9 and 13:58.9 the left arm's upper arm near the elbow, its elbow and
    its forearm near the elbow were 12–15 cm inside the ribs capsule (`clearCmL=-14.9,-12.2,-9.9,6.5`),
    while its wrist was 4–6 cm in front of the body. Its elbow was about 4 cm from the capsule's axis,
    near the middle of the body. That is far beyond any error in fitting a capsule: the chooser put the
    elbow into the torso (cause 1).
  - **Shallow, at the armpit.** Most of the time the sample nearest the torso was `upper30/chest`, 2–7 cm
    clear. With the hands close it went 1–3 cm in: 4 lines for the right arm and 5 for the left. That is
    the arm's root, where the mesh is joined: cause 2, or nothing.
- **The wearer's reading:** the grey outlines matched Morgan's torso, so the defaults stand. Where the arm
  visibly clipped the rings were red, mostly round the elbow, and most of the arm could be made to clip.

## Step 1 as built (PreyVR-104)

**The elbow is kept out of the drawn torso, with the capsules step 0 fitted.** Two parts, both in
`src/common/ArmPose.cpp`; `SolveArms` runs them each frame with the capsules on the drawn spine:
- **The torso term** (`ChooseElbowPole`'s `obstacle`). An elbow that brings the arm (every sample but the
  wrist, which no elbow moves: `armpose::ArmClear`) within `margin` of the torso costs
  ((margin − clearance) / wall)² more. Default margin and wall: 1 cm each. The term is never negative,
  so the elbows are taken in order of the rest of the cost and only those that could still win are
  measured. Near nothing, that is one.
- **The tracking** (`armpose::TrackElbowPole`). After the pole's 60 ms smoothing, the drawn elbow starts
  where it was drawn last frame:
  - **Free:** with the way to the chosen elbow clear, it goes straight there. That is the chooser's elbow
    exactly, as before.
  - **Round:** with the body in the way and the other way round clear, it goes that way, at most
    `degPerS` (360°/s).
  - **Parked:** with the chosen elbow in the body, it slides along the circle to the body's edge on its
    own side and waits there.
  - **Pushed:** when the body (a lean, a turn) moves onto the drawn elbow, it moves out to the nearest
    clear elbow at once.
  - **Clear** means by the margin; where no elbow on the circle is that clear, within 5 mm of the clearest.
  - The circle is measured coarsely (36 elbows) only when something is in the way; otherwise only along
    the way to the chosen elbow.

**Offline, against the real code** (the default capsules on Morgan's standing spine, arm 60 cm):

| | Now | Step 1 |
|---|---|---|
| Random hand poses near the chest (4000), arm in the torso | 64.2% | 0.3% |
| 60 s of the hands at the chest, frames with the arm in the torso | 72.6% | 0.0% |
| Largest elbow step in a frame | 5.6 cm | 5.1 cm |
| 99th percentile step in a frame | 0.6 cm | 1.8 cm (sliding along the body) |
| Cost per arm and frame, hands at the chest all the time | 7.9 µs | 20.6 µs |

How the 60 s run's frames went: 1833 free, 0 round, 2120 parked, 1447 pushed. Away from the body both
parts take their short way, so the cost is about the chooser's alone.

**Tests:**
- `TestChooserAvoidsTorso`: the sweep's hand pose that put the elbow 12.6 cm into the body.
- `TestTrackElbowPole`: free, parked, pushed and round, against one ball across the circle.
- `TestTrackingKeepsTheArmOut`: the 60 s run. The arm is never inside, and no step is longer than the
  chooser's own.

**Commands:**
- `arm.avoid <margin> <wall> <degPerS>` (default `10 10 360`). PreyVR-104 also had an on/off switch in
  front of them for the headset A/B; it was removed once worn OK, so the elbow is always kept out.
- `arm.report` adds `avoidMm`, `avoidDegPerS`, and per arm `trackR/L` (free, round, parked, pushed frames
  since the last report). Its `insidePctR/L` counts the wrist too, which follows the controller and which
  no elbow moves, so it is not the measure of step 1 (see the PreyVR-104 report below).

### In the headset

What PreyVR-104 was checked for:
1. Hold the controllers close to the chest, as when the arms clipped, and move them round there: the elbows
   should stay outside the body.
2. Watch the elbows as the hands move near the body: no pops, and no elbow stuck out at an odd angle.
3. `dbg.draw 608` shows it: the rings should stay green or amber.
4. `arm.report` at the end: `insidePctR/L` (it was 9.0 and 12.8% on PreyVR-103) and `trackR/L`.

### Headset, 2026-10-30 (PreyVR-104), `arm.report`

Run `player-20261030-131538`, the overlay off, two reports (399 and 3032 frames per arm):

| | PreyVR-103 | PreyVR-104, 1st | PreyVR-104, 2nd |
|---|---|---|---|
| Frames with some sample inside, right / left | 9.0% / 12.8% | 16.3% / 14.3% | 6.2% / 15.6% |
| Deepest, right | −10.9 cm `upper80/ribs` | −2.4 cm `wrist/chest` | −4.9 cm `wrist/chest` |
| Deepest, left | −15.6 cm `elbow/ribs` | −1.2 cm `wrist/chest` | −6.2 cm `upper55/chest` |
| Free / round / parked / pushed, right | — | 346 / 0 / 29 / 24 | 2593 / 0 / 255 / 184 |
| Free / round / parked / pushed, left | — | 399 / 0 / 0 / 0 | 1683 / 0 / 628 / 721 |

- **The deep clipping at the elbow is gone.** The deepest readings are under half of PreyVR-103's, and
  three of the four are the wrist.
- **The hand counts.** The wrist sample is where the controller puts it: a hand held within about 3 cm of
  Morgan's chest reads inside, whatever the elbow does. The tracking keeps the rest of the arm clear by
  the margin. Only where no elbow on the circle (36 samples) is 5 mm clear does it settle for the clearest
  one, and only then can the rest of the arm read inside. The report does not count those frames apart
  from the hand's.
- **The left upper arm's −6.2 cm is one frame in 34 s,** so by that rule a pose where the clearest elbow
  still had the arm at least 5.7 cm in. That can be the forearm through the body at any swivel, or the arm
  straight across it. No `arm_fit` lines say which (the overlay was off).
- **The share inside did not fall,** because it counts the hand and depends on how long the hands were at
  the chest.
- The left arm was parked or pushed in 45% of the second report's frames and the right in 15%. The offline
  60 s at the chest had 66%.

## Open

- **The defaults were guessed,** and the wearer found the drawn outlines matched the body (PreyVR-103).
- **The capsules leave grooves** between neighbours (about 2 cm at the default spacing). If the arm sinks
  into one, step 1 may want a capsule chain or a tapered shape instead.
- **The armpit:** the arm's root always meets the torso, so the samples nearest the shoulder may read
  inside where the mesh is only joined. The fit decides whether that is clipping (cause 2) or the joint.
- **Without the body pin's joints** (`pinReady=0`) no capsule is placed and nothing is measured
  (`torsoPlaced=0`).
