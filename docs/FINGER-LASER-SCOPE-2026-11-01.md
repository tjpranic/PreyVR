# Aim lasers out of the index fingers: scope (2026-11-01)

Branch `custom`. TODO (UX/Immersion): "Aim lasers should come out of my pointer fingers. This way it will handle
like the weapon aiming." Read from the source, and measured from headset run logs on the Steam Frame. No new native
seam is needed: every engine write here already exists.

**Final state: PreyVR-128, worn OK 2026-11-01** (`player-20261101-122334`: lean 39.0, `fingerAimDeg` 3.5 and 2.6, `tipErrCm`
0.0 with the finger lifted and up to 5.5 on the trigger). The hand moved instead of the laser. Every hand now leans
39° on its controller, so the index finger lies along the controller's aim, and the lasers leave the fingertip along that
aim. There is no setting. See [what shipped](#what-shipped). The wearer tried that exact state on PreyVR-127 with
`pose.grip 550 35 0 390` first, and chose it over both options below.

PreyVR-127 came first and was worn OK the same day (`player-20261101-113517`: `tipErrCm` 0.0 with the finger lifted and up
to 5.5 on the trigger, `fingerAimDeg` 24.1 and 24.0, both as predicted). It carried A and B on a VR Options setting. See
[as built](#as-built), which also lists what that build found that this scope missed.

**Answer:**
- Every laser that isn't a gun's starts at the controller's OpenXR aim pose, which the drawn hand is not built around.
  It sits 5.7 cm from the index knuckle and 9.6 cm from the pointing fingertip.
- Moving the start is not enough. The drawn finger points **24° below** the aim ray, so a laser moved to the fingertip
  would leave it at a visible angle.
- The empty right hand has a second fault: its laser isn't a ray. It starts at the hand and ends where a ray from the
  *eye* lands, as gun shots did before `aim.shot`.
- The fix is one new pose, the **finger pose**: the tip and axis of the drawn index finger, fixed to the controller's
  grip. The left hand's rays and the empty right hand take it in place of the aim pose. The right hand also follows
  the muzzle path, so the ray really starts at the finger.
- **One decision is left: which way the ray points** ([below](#the-decision-which-way-the-ray-points)).

Evidence: [`data/evidence/finger-laser-2026-11-01/`](../data/evidence/finger-laser-2026-11-01/). To replay it, run
`python -E -B data/evidence/finger-laser-2026-11-01/finger_geometry.py`. `inputs.txt` holds the log lines it uses. It
does not show how any of this feels: the numbers are geometry, and the feel needs a headset.

## How each laser is placed today

| Laser | Starts at | Points along | Source |
|---|---|---|---|
| Right, gun | The muzzle helper, in the world | Aim pose; the IK lays the barrel on it | [`ShotRayLane.cpp:652`](../src/aim/ShotRayLane.cpp#L652), [`DebugOverlay.cpp:635`](../src/diagnostics/overlay/DebugOverlay.cpp#L635) |
| Right, no weapon | Aim pose position | Toward the end of an **eye-origin** ray | [`DebugOverlay.cpp:642`](../src/diagnostics/overlay/DebugOverlay.cpp#L642) |
| Right, wrench | The same, as it has no muzzle helper. Probably grenades too: the rig alignment reports `missing_helper` for them, but no log confirms the laser | The same | [`ShotRayLane.cpp:143`](../src/aim/ShotRayLane.cpp#L143) |
| Left, use pointer (default) | Aim pose + surface stop, filtered | Aim pose | [`InteractionLane.cpp:555`](../src/physics/hands/InteractionLane.cpp#L555) |
| Left, psi (`psi_target` 2) | Aim pose + surface stop | Aim pose | [`PsiMedkit.cpp:78`](../src/equipment/psi/PsiMedkit.cpp#L78) |
| Left, fallback | Aim pose | Aim pose | [`DebugOverlay.cpp:721`](../src/diagnostics/overlay/DebugOverlay.cpp#L721) |
| Menu pointer (not a debug-overlay line) | Aim pose | Aim pose | [`UiPointer.cpp:79`](../src/ui/screens/UiPointer.cpp#L79), [`VrOptionsRuntime.cpp:349`](../src/ui/options/VrOptionsRuntime.cpp#L349), [`XrInput.cpp:656`](../src/controls/input/XrInput.cpp#L656) |

With no weapon the aim sample's origin stays the eye, because `ShotRayFrame` finds no muzzle. So the line is drawn from
the hand to `eye + direction * distance`, and it moves away from the controller's axis as the hand moves from the eye.
This is the convergence measured on the gun before the fix: 14.9° off the barrel at 2 m, from the hip, on the mock
([SHOT-RAY](SHOT-RAY-2026-10-06.md)).

The same sample feeds the world reticle, the scene query and the world UI's cursor
([`AimTakeover.cpp:273-280`](../src/aim/AimTakeover.cpp#L273-L280)). That is why the finger has to enter at the sample,
not only in the overlay.

## Measurements

The script runs the rig's bones through the mod's own hand placement (`FrameToFrame`, `GripPointInWrist`,
`ShapeRelative`). As a control, it reproduces the logged `knuckle` 0.0881, `lenL`, `palmL`, `wristToGrip` 6.0 cm and
`lenInGrip`. Grip-to-aim is 42.8° with the offset (±1.6, -3.5, 7.4) cm in every report on disk (1532, `frame_controller`).

Right hand, grip frame, cm. The left is its mirror, within 0.3 cm.

| Point | x | y | z | From today's start |
|---|---|---|---|---|
| Aim pose, where the laser starts now | -1.6 | -2.5 | -7.8 | |
| Index knuckle | 3.2 | -2.9 | -4.8 | 5.7 |
| Fingertip, finger off the trigger | 2.7 | -11.0 | -8.3 | 9.6 |
| Fingertip, finger resting on the trigger | -2.1 | -9.2 | -6.3 | 6.9 |
| Fingertip, trigger fully pulled | 0.0 | -0.3 | -2.8 | 5.7 |

- **Today's start** is not on the hand: 4.8 cm to the palm side of the index knuckle and 2.3 cm above it.
- **The extended finger** is 23.8° below the aim ray and about 3° inward on both hands. The hand's own axis is 32.2°
  below it, because the index fans toward the thumb.
- **This depends on the hand's seating.** With the fist-tube lean (15°, an estimate) at 5° the gap is 34°, and at
  25° it is 14°.
- **The real fingertip moves with the trigger:** 5.5 cm to resting and 12.4 cm to a full pull, while the line from the
  knuckle to the tip swings from 24° to 50° to 135° off the ray.
- **The rig has no tip joint.** The tip is the existing estimate, 2.2 cm past `index3`
  ([`AnimIkReports.cpp:175`](../src/body/ik/AnimIkReports.cpp#L175)).
- **The drawn hand is usually on the controller.** In one-hand reports `palmErr` is under 0.05 cm in 91% (right) and
  94% (left), and 10 cm or more in 5% and 3%. The logs can't say why for those; menus, cutscenes and full reach are
  candidates.

## The finger pose

**Fixed to the grip, in the pointing shape.** Per hand, one `Pose` in grip space, in the aim pose's convention
(-Z forward): the pointing fingertip, along the line from the knuckle to the tip.

- **Why not the live bones?** A ray on them would swing up to 135° as the trigger is pulled, so aiming would move
  when you fire or use a power. They are also normally a frame old when the aim sample is built, because the IK reads
  the frame the aim lane publishes.
- **Why not hard-coded numbers?** It depends on the rig's bones and on `pose.grip`, so it is computed where the hand
  placement is ([`AnimIkHand.cpp:15-27`](../src/body/ik/AnimIkHand.cpp#L15-L27)).
- **The price** is a start up to 5.5 cm from a finger resting on the trigger. The 20 cm fade-in
  ([`kLaserFadeIn`](../src/diagnostics/overlay/DebugOverlayScene.h#L64)) should hide that, and it starts exactly on a
  finger you lift to point. That needs checking in the headset.

## What has to change

1. **The pose (pure, offline-tested).** `handpose::PointerInGrip` in [`HandPose`](../src/body/HandPose.h), from the
   hand frame, the grip point and the index bones. `BuildHandRig`
   ([`AnimIkRig.cpp:255`](../src/body/ik/AnimIkRig.cpp#L255)) reads the bones and publishes the result. Until a rig
   exists, it falls back to the aim pose.
2. **One way to read it.** A `ControllerPose::pointer` in [`HandWorld.h`](../src/body/HandWorld.h#L45), plus the same
   pose in tracking space for the callers that take a raw pose.
3. **The left hand.** Swap the pose at `InteractionLane.cpp:555`, `PsiMedkit.cpp:78` and `DebugOverlay.cpp:721-735`.
4. **The right hand, no weapon.** In `OverrideAimRay`, the direction comes from the finger pose
   ([`AimTakeover.cpp:176`](../src/aim/AimTakeover.cpp#L176)), and the sample starts at the fingertip after
   `ShotRayFrame`, the way the muzzle does. `DebugOverlayAim` then draws it like the muzzle ray.
5. **Report.** Add the pure tip against `marks.tip` to `pose.report`, so a headset run checks the maths.

## What that breaks if done carelessly

- **The left selector's start.** [`gBackMm`](../src/physics/hands/InteractionLane.cpp#L87) starts its ray 100 mm
  behind the hand so that a prop you touch is still in front of it. From the fingertip it needs about **170 mm** to
  start level with today's spot; otherwise anything at the palm falls behind the ray.
- **The right hand's anchor.** It has to use `Anchor::camera` and `Placement::drawn` and set `cameraAnchored`, like the
  muzzle. The `Anchor::reticle` at [`AimTakeover.cpp:202`](../src/aim/AimTakeover.cpp#L202) would put it in the wrong
  place.
- **Examined screens.** `AimAtScreen` ([`AimTakeover.cpp:108`](../src/aim/AimTakeover.cpp#L108)) uses the aim pose and
  has to follow the same rule, or the ray jumps when a keypad zoom starts with an empty hand.
- **The engine's cached direction** must take the finger too, with no weapon, so that native psi (`psi_target` 0) keeps
  using the direction the laser shows.
- **Holstering** jumps the right laser by the direction change. Instant weapon swaps make that a cut, not a sweep.

## Left as it is

- **Guns, two-handed aim, shots and the IK:** untouched. The finger pose applies only with no weapon.
- **Wrench and grenade:** today's ray. The fist is closed round them, so there is no finger to point with. A wrench
  keeps the eye-origin convergence.
- **Menu pointer:** the aim pose. It has its own renderer, and hold frames locate it with no rig running and no hand
  drawn. Gameplay and menus will differ by the direction change.
- **Carry:** `HandNow::aimDirection` ([`CarryLane.cpp:398`](../src/physics/hands/CarryLane.cpp#L398)) is set but never
  read, and a hand that carries has no laser.

## The decision: which way the ray points

| | Ray direction | Looks | Handles |
|---|---|---|---|
| **A** | Aim pose, moved to the fingertip | 24° kink where it leaves the finger | Same wrist angle as a gun |
| **B** | Along the finger | Straight out of the finger | 24° more wrist lift than a gun to hit the same point |
| **C** | Aim pose, with the drawn hand turned onto it, as a gun's is | Straight | Same as a gun, but the free hand stops matching the real hand |

**Recommended: B.** The ask is a laser out of the finger, and only B gives that with the hand still where the player's
hand is. C undoes the anatomical free hand ([HAND-POSE](HAND-POSE-2026-10-06.md)). A and B share all the plumbing and
differ by one rotation.

**Decided 2026-11-01:** first A and B both, as Hands > Aim Laser Direction (PreyVR-127). Then, having worn them, the
wearer asked for the hands to move instead, and chose that: [what shipped](#what-shipped).

## What shipped

PreyVR-128. Option C, reached by the hand's lean rather than the separate turn this scope proposed: the wearer asked for
`pose.grip 550 35 0 390` as tried, so what shipped is what was worn.

- **`handpose::kTubeLeanDegrees` is 39** ([`HandPose.h`](../src/body/HandPose.h)), for every hand, free or holding a gun.
- **`FingerPose` takes the fingertip's position and the controller's aim** ([`GameplayFrame.h`](../src/aim/GameplayFrame.h)).
  An empty hand now aims exactly where a gun does, so holstering turns nothing.
- **Aim Laser Direction is gone.** `laser_direction` is a retired key: a saved file holding it still loads, and the next
  save leaves it out. Without that, the loader would have refused the wearer's whole file.
- **`aim::EmptyHandLatch` is gone.** It guarded a 24° turn that no longer exists.

Read from the preview run, `player-20261101-120455` (lean 39, Controller):

| Reading | Lean 15 | Lean 39 |
|---|---|---|
| Finger against the controller's aim, right / left | 24.1° / 24.0° | 3.5° / 2.6° |
| Gap to the drawn fingertip, finger lifted | 0.0 cm | 0.0 cm |
| Roll of three guns, the same three readings | 0.6°, -8.1°, 174.4° | 0.6°, -8.1°, 174.1° |

- **Guns:** this scope and the reply before the build expected the lean to move their roll. It didn't, by 0.3° at most.
- **The elbow chooser** reads the same lean, as the run did. It was going to be left on 15° under the separate turn.
- **`lenErrDeg` in `pose.report`** now measures against the 39° hand, so a gun reads about 7°, not 27°.
- **Costs still standing:** the drawn hand is 24° from the real one, the drawn wrist bends that much further toward the
  thumb, the wrist display moves about 2 cm with the drawn wrist, and the push box stays on the controller.

Evidence: [`data/evidence/finger-laser-seating-2026-11-01/`](../data/evidence/finger-laser-seating-2026-11-01/).

## As built

PreyVR-127, the build before [what shipped](#what-shipped). It follows the plan above, with these additions. `ctest`
117/117, worn OK. PreyVR-128 removed the setting and the latch below.

- **The pose:** `handpose::FingerInGrip` ([`HandPose.h`](../src/body/HandPose.h)), published by `PublishFingerPose`
  ([`AnimIkHand.cpp`](../src/body/ik/AnimIkHand.cpp)) on every IK pass, read into the frame by `BuildGameplayFrame`, and
  read by the lanes through `FingerPose` ([`GameplayFrame.h`](../src/aim/GameplayFrame.h)) or `ControllerPose::finger`. The
  setting only changed the orientation `FingerPose` returned, so A and B shared one fingertip.
- **Found while building: a drawn gun could read as empty.** The rig's owner is a try-lock read and two live checks, so one
  refused read would have turned a gun's aim 24° for a frame, perhaps the frame it fires. `aim::EmptyHandLatch`
  ([`WeaponAim.h`](../src/aim/WeaponAim.h)) counts a weapon at once and its absence only after three frames.
- **Found while building: the carry grab point.** It cast from the pointer's origin. From a fingertip already inside an
  object, that gives a point inside it, not its near face. `UsePointer::start` hands it the selector ray's start, behind
  the palm.
- **The raw aim ray** (`dbg.draw` 2) is now drawn for the left hand as well. It no longer lies on that hand's laser.
- **Fingers Follow off** draws the finger relaxed, so the finger pose uses the relaxed curl then.
- **No command:** the setting was menu-only, like Fingers Follow.

`aim_review` replays the real `AimTakeover.cpp`: the fingertip origin with the engine's origin left at the eye, the same
direction with or without a weapon, an examined screen, and no rig. `hand_pose`
reaches the same tip by walking the joints the way the IK does. None of that shows the pose sits on the drawn finger in
the game: `pose.report`'s `tipErrCm` does, in the first headset run.

## Headset check

- Lift each index finger and check that the laser leaves the fingertip along the finger, then rest it on the trigger.
- The empty right hand: move the hand at a fixed wall point and confirm the laser no longer swings from the eye.
- The left hand: select a prop touching the palm, a far one, and one held back by a wall.
- A keypad zoom with an empty hand, holstering a gun, and zero-G.
- A prop picked up with the hand touching it: it should hang from its near side, as before.
- `pose_report` in the run log: `tipErrCmL` and `tipErrCmR` near 0 with a free hand's finger off the trigger, and
  `fingerAimDegR` about 3.5.
