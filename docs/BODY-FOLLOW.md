# The player's body

The one document for the player's body in VR: how it turns with you, follows you across the room
and is posed under your head, how it is drawn, where the shoulders the arms hang from come from,
the engine facts all of it rests on, what was tried and removed, and the history. Merged on 2026-10-25, in two steps:
- from `BODY-YAW-2026-10-11.md` (turning), `ROOMSCALE-2026-10-11.md` (moving) and
  `TURN-STOP-FLASH-2026-10-24.md` (the flash);
- then, at the end of the body overhaul, from `TURN-LEAN-ARMS-2026-10-19.md` (leaning push-back,
  bent arms, the torso lift), `POSTURE-AND-BODY-SLOT-FEEDBACK-2026-10-04.md` (posture calibration,
  the body-slot cards), the room-scale cause of [MOVE-STOP-2026-10-24.md](MOVE-STOP-2026-10-24.md)
  (its cause A) and the torso and shoulder parts of [ARM-POSE-2026-10-06.md](ARM-POSE-2026-10-06.md).

The removed documents are in git history. Their evidence stays in
[evidence/turn-lean-arms-2026-10-19/](evidence/turn-lean-arms-2026-10-19/) (the follow, arm and
teleport simulations, the stance and living dumps, `verify_teleport.py`),
[evidence/move-stop-2026-10-24/](evidence/move-stop-2026-10-24/) and
[evidence/posture-slot-feedback-2026-10-04/](evidence/posture-slot-feedback-2026-10-04/).

Kept separate, because their main subject is something else:
- [MOVE-STOP-2026-10-24.md](MOVE-STOP-2026-10-24.md): movement stops from Prey re-reading the stick
  (causes B and C).
- [ARM-POSE-2026-10-06.md](ARM-POSE-2026-10-06.md): the arm solver: the shoulder joint's place on
  the torso, the reach, the elbow, the Arm Length option.
- [COMFORT-CONTROLS-2026-09-19.md](COMFORT-CONTROLS-2026-09-19.md): snap and smooth turning,
  recenter and the calibrated height. Where it says the snap pivots about the head, it is
  superseded by [rule 1](#when-the-body-is-turned-and-why). A receipt pins its bytes, so it is not
  edited.
- [BODY-EQUIPMENT-2026-09-27.md](BODY-EQUIPMENT-2026-09-27.md) and
  [PSI-MEDKIT-2026-09-30.md](PSI-MEDKIT-2026-09-30.md): the holster and medkit slots. Both describe
  a torso of their own (a 45° deadband, at most 90°/s); since 2026-10-24 that is only the fallback
  for [the shared torso](#the-four-yaws). PSI-MEDKIT's bytes are pinned too.
- [PHYSICAL-CONTACT-SCOPE-2026-10-18.md](PHYSICAL-CONTACT-SCOPE-2026-10-18.md): the capsule against
  props: the feet, `heightPivot` and the stance's smoothing offset.

All engine addresses are RVAs in the Steam `PreyDll.dll` (SHA-256 `7D6E322F...05311A7`), from
`0x180000000`. RTTI is stripped, so the names are by shape, not by symbol.

## Where things stand (2026-10-25)

**The body is the player's: it turns to face your torso, its capsule follows you across the room,
and the drawn body is posed under your head.** The overhaul was worn OK on PreyVR-87; PreyVR-88 is
its cleanup, and the lean zone (PreyVR-89) was worn OK. PreyVR-90 puts the lean zone and where the
hips stand in VR Options (Setup → **Lean Zone**, **Hips and Legs Back**).

| When you turn | What turns the body | When |
|---|---|---|
| With the stick (smooth turn) | Prey, as always | Prey's own update |
| By a snap turn | The mod, once, to the torso | The frame the snap lands, at `ArkPlayer::Update`'s entry |
| Physically, in the room | The mod, every frame, at most 360°/s | After the frame's render; never while Prey is turning the player, nor for 150 ms after |

- **Walking in the room moves the capsule under you** ([room-scale](#room-scale)), after the
  frame's render, with the turn. **Leaning bends the torso instead:** the capsule follows only once
  your head leads it by more than the lean zone (25 cm by default), or once you come to rest
  upright after a step. Where the
  capsule cannot follow, at a desk or a wall, the torso bends to your head too; a head through
  geometry fades the view.
- **The drawn body is posed under your head** ([the body pin](#the-body-pin)): the neck under
  yours, the spine bent to it, the pelvis and the knees down as you crouch, the hips and legs 8 cm
  further back than Prey stands them, under your head rather than your eyes. The arms hang from
  its collarbones ([the shoulders](#the-shoulders)).
- **The drawn body is in the world** ([how the body is drawn](#how-the-body-is-drawn)): what is in
  front of it hides it, a hand reached behind a crate included, where Prey drew it over everything.
  The view's near plane is 3 cm (Setup → **Near Clip Distance**), so looking down does not cut
  into the neck. Worn OK on PreyVR-92.
- **Code:**
  - Turning: `src/dll/TorsoLane.{h,cpp}` (the lane and the shared torso), `include/preyvr/BodyTurn.h`
    and `src/common/BodyTurn.cpp` (the arithmetic and `NativeTurnWatch`); in `HeadTrackingHook`,
    `TurnHeadTrackingReferenceWithNative`, `SetSnapTurnsAboutBody` and `UseNativeViewYawOnce`.
  - Room-scale: `src/dll/RoomScaleLane.{h,cpp}`, `include/preyvr/RoomScale.h` and
    `src/common/RoomScale.cpp` (the follower, the fit), `include/preyvr/RoomScaleNative.h` (the
    living body's calls); in `HeadTrackingHook`, `MoveHeadTrackingOriginWithNative` and
    `HeadDropBelowCalibrated`; the fade layer, `BuildFadeLayer` in `XrOverlayLayers.cpp`.
    `FrameSchedule::AfterEngineRender` calls `ServiceBodyFollow`, which turns, then moves.
  - The body pin: `include/preyvr/BodyRig.h` and `src/common/BodyRig.cpp` (`bodyrig::SolvePin`), and
    `src/dll/AnimIkBody.cpp` (`BuildBodyRig`, `PlanBody`, `ApplyBody`).
  - The shoulders: `armpose::ShoulderFromHead` and `armpose::LevelEye` (`include/preyvr/ArmPose.h`)
    and `bodyrig::ShoulderFromAtlas`, placed by `PrepareArms` (`src/dll/AnimIkArms.cpp`).
  - How it is drawn: `src/dll/BodyDepthHook.{h,cpp}` with `include/preyvr/BodyDepth.h` and
    `BodyDepthNative.h` (the slot render); the near plane in `HeadTrackingHook.cpp`'s
    `ApplyNearClip`, with `include/preyvr/NearClip.h`.
- **Tests:** `body_turn` (`tests/BodyTurnTests.cpp`, with `TestSnapKeepsTheCameraOnTheBody` and
  `TestSmoothTurnPausesTheBody`), `room_scale`, `body_rig` (against the rig PreyVR-84 measured) and
  `arm_pose`; `scheme_review` drives `MoveLane.cpp`'s walk start; `body_depth`, `body_depth_hook`
  and `near_clip` for how it is drawn. **Static contracts**, run by `ctest re_verifiers`:
  `tools/re/verify_view_rotation.py` (27 checks: the setter, the entity's transform),
  `tools/re/verify_room_scale_physics.py` (52: the living body's calls) and
  `tools/re/verify_body_depth.py` (57: the slot render, the near pass's depth slice).
- **Commands** ([TUNING-COMMANDS.md](TUNING-COMMANDS.md#body)): `body.report` (the torso and the
  turns, the body pin, room-scale) and `body.torso` / `arm.torso` (the torso filter: 35° and
  600 ‰/s, one copy); the overhaul's switches and research commands went in PreyVR-88
  ([history](#history)). `body.depth` (Stereo and rendering) reports how the body is drawn and the
  near plane, and is the A/B for drawing it in the world (1, the default) or Prey's near pass (0).

**Do not:**
- **Turn the body while Prey turns the player** (a smooth turn). Every such frame shows the view
  wrong by Prey's body lag ([below](#preys-body-lag-and-the-one-frame-flash-2026-10-24)): the
  jitter that removed Body Follows You.
- **Turn or move the body before the frame's render** in the follow or room-scale. That frame's
  pose was made for the old facing and place.
- **Move a walking body with the plain `SetPos`.** It is the engine's teleport
  ([the capsule's physics](#the-capsules-physics)): room-scale keeps the motion while the body
  moves, and uses it only at rest.
- **Move the capsule without the stance test.** Put into a desk, it is pushed back out by physics,
  and the view with it.
- **Write the camera joint** (`camera_jnt`, 98) or what hangs from it in the drawn pose. `#camera`,
  the flashlight and the psychoscope's light ride it; the view ignores the drawn pose (PreyVR-85),
  so leaving it alone costs nothing.
- **Bump the reference generation for a turn or a move that keeps P** ([why](#the-four-yaws)).

## The four yaws

| | Meaning | Owner |
|---|---|---|
| **G** | native player yaw: what the body mesh, capsule and arms rig face | engine |
| **R** | reference yaw (`room.yaw` / `gReferenceYaw`), tracking space | mod |
| **H** | head yaw, tracking space | headset |
| **P = G − R** | world yaw of the tracking space | derived each frame |

The view is `P + H` (`HeadTrackingHook.cpp`, `UpdateViewObserved`); the body faces `G = P + R`.
Physical turning changes only H, a snap turn only R, a recenter sets R = H once, and Prey's stick
turn changes G. So without help the body faces wherever you faced at the last recenter. That
was the original report (2026-10-11): turn round physically or snap 180°, look down, and you see
the character's back.

**The design: R follows the torso yaw T, and G moves with it.** Each turn computes
`d = wrap(T − R)` and turns G and R by d together, so **P, and with it the view, the aim, the
hands and the panels, does not move**. The rules that follow from it:

- **Relative, not absolute.** The turn is `level(Rz(d) · current G)`, on top of whatever the game
  did; an absolute `G = P + T` would fight every native rotation.
- **No generation bump.** About 25 consumers reset on a reference-generation change (IK
  calibration, the two-hand hold, gestures, panel fits, the render contract). The generation
  announces a change of P; this never changes P. Room-scale's move keeps P too. Snaps and
  recenters keep their bumps.
- **Level.** The written rotation is yaw only: native pitch bends the torso mesh, and nothing in
  VR reads it.
- **One torso.** `armpose::TorsoYaw` (the head's yaw behind a 35° deadzone, relaxing towards it at
  0.6/s), estimated once a frame by TorsoLane, is what the body, the arm solver's shoulders and
  the holster and medkit slots all read. A recenter resets it. While TorsoLane has no torso from
  the last 100 ms, two fallbacks stand in: the arms' own filter, with the same settings, and the
  slots' head-following estimate (a 45° deadband, at most 90°/s).

**What each consumer does** (from a survey of every yaw-coupled site, 2026-10-11):

| Consumer | Frame it uses | Effect of a body turn |
|---|---|---|
| View, aim, shot ray, interaction, scene query, psi, grenades, physical, wrist, debug overlay | P | None |
| IK world goals; the rig's model space from the ADIK `location` | P; G | Re-expressed in the turned rig; the shoulders sit on the visible body |
| Head-relative movement | `H − R` | Relative to the torso the body faces |
| Heavy carry | native target (G) plus a P-based shift | The shift's neutral offset turns with `P + R` (= G), the frame the native target turns in |
| Holster and medkit slots | the shared torso | They follow the torso while the hand is outside them, so a turn cannot drag a reached slot away |

## How the body is turned: the engine's own setter

No code writes G directly. **ArkPlayer vtable slot 52** (`+0x1A0`, `0x1584CC0`) has the
`IActor::SetViewRotation(const Quat&)` shape and does three things:
- copies q to ArkPlayer `+0x13F0`, which is `ArkPlayerCamera::m_rotation` (camera `+0x150`,
  the camera being ArkPlayer `+0x12A0`), the rotation save games store as `"rotation"`;
- calls the player entity's (`+0x38`) virtual `+0x118` with `(q, 0)`, `SetRotation`;
- hands q to a component at `+0x1468`, which **keeps its own copy** at `+0x08` (`0x1327050`).

Slot 53 (`0x157CE80`) is the matching getter. An entity rotation event (`0x1580AE4`) copies the
entity rotation back into `+0x13F0`. **Use the setter, not a field write:** writing `+0x13F0`
alone leaves the component's copy and the entity stale. At install TorsoLane compares the
setter's, the getter's and the component copy's complete bodies, and slots 52 and 53.

The component at `+0x1468` is the HUD helmet's: its update (`0x1327070`) eases its copy toward the
live view camera and rotates the character's `helmet` attachment (slots 0 and 5) by the
difference, the HUD's sway, with creak sounds from the same angle.

**Other routes, and why not:** posting look input (`xi_thumbrx`) gives a rate scaled by an
unmeasured turn speed, not an angle; hiding the body mesh removes the body too.

## When the body is turned, and why

Each rule below was paid for in a headset.

1. **A snap turns the body at once, and the view about the body.** The snap is queued where it is
   decided and applied at `ArkPlayer::Update`'s entry (`0x1584FE0`, vtable slot 19), TorsoLane's
   hook; TorsoLane then turns the body to the torso all the way. While it can,
   `SetSnapTurnsAboutBody(true)` makes the snap skip the pivot about the head, so the snap and
   the body's turn are one turn about the capsule, as a smooth turn's is. Pivoting about the head
   (COMFORT-CONTROLS) kept the camera fixed in the world while the body turned under it, and after
   a step away from the origin you could see into the model (PreyVR-74). With room-scale keeping
   the capsule under you, a turn about the body is a turn about you. Only if the body cannot be
   turned (`snapBodyTurn=0`) does a snap pivot about the head.
2. **The physical follow never turns the body while Prey is turning the player**, nor for 150 ms
   after (`bodyturn::NativeTurnWatch`: the native yaw moved more than 0.02° since the lane last
   left it). A physical turn made during a stick turn is caught up after it, at most 360°/s.
3. **The physical follow turns the body after the frame's render** (`ServiceBodyFollow`, from
   `FrameSchedule::AfterEngineRender`). The decision is still made at `ArkPlayer::Update`'s entry;
   the turn waits until the world render has taken the body's transform, before the next frame's
   animation starts.
4. **On a frame where the body lagged the view when it was turned, the view takes Prey's look yaw
   from `m_rotation`** (`UseNativeViewYawOnce`), not from the view Prey composed; never while
   Prey's focus camera has the view. With rule 3 the follow no longer needs this; it covers snap
   turns.

Rules 2 to 4 all come from one engine behaviour, measured in the next section.

## Prey's body lag, and the one-frame flash (2026-10-24)

**The report.** With smooth turning, the moment a stick turn stopped, the headset showed one bad
frame, "like the camera is rendering an old frame". Snap turns did not. Turning the follow off
(`body.follow 0`, a switch since removed) removed it. After the first fix the world was clean but
the player's own body still showed it.

### What Prey does during a stick turn

Prey's eye (`0x1489010`, from `UpdateView` `0x148B820`, camera mode 0) is composed every frame,
with no smoothing between frames, from:
- the **entity's rotation**, read live (IEntity vt `+0x150`);
- a roll offset from movement (player `+0x58`, `0x159C4A0`; no yaw);
- the **`#camera` attachment's model-relative transform**: character slot 0 (vt `+0x2D8`), its
  attachment manager (`+0x48`), the attachment by name (`+0x38`), its transform (`+0x68`). `#camera`
  sits on head joint 98;
- lean, then view effects (`0x117B4D0`: time-based sway, independent of turning).

**During a stick turn Prey lets the body (the entity) lag the view** (`m_rotation`), and its
animation turns the head by the lag, so entity × `#camera` stays on `m_rotation`. Measured just
before a catch-up:

| `m_rotation` | Entity | `#camera` | Prey's view |
|---|---|---|---|
| 56.755° | 26.688° | +30.067° | 56.755° |

### Why the turn made a bad frame

The character's animation for a frame starts in the pre-physics pass, before `ArkPlayer::Update`.
The setter then takes the entity all the way to `m_rotation` at once. So on that frame:
- **the view** was the entity (now on `m_rotation`) plus a head still turned by the old lag: the
  lag counted twice;
- **the body** was drawn turned, with a pose made for the old facing: Prey's look pose turning the
  head and upper body by the lag, and the arm IK, which maps the hands into the model with the
  location the animation started with (ADIK params `+0x14`, `kParamsLocQ`).

On the next frame the animation is posed for no lag and everything is right. Every catch-up
measured (PreyVR-79, run `player-20261024-210526`):

| Body lag | `#camera` yaw | View error, that frame | Next frame |
|---|---|---|---|
| 30.067° | 30.067° | 30.067° | 0 |
| 45.638° | 45.638° | 45.638° | 0 |
| −32.318° | −32.317° | −32.317° | 0 |
| −10.605° | −10.605° | −9.877° | 0 |
| −18.823° | −18.824° | −17.564° | 0 |

The catch-up turns themselves were 0.3° to 2°; the error's sign followed the stick turn before it
(PreyVR-78, run `player-20261024-205536`, 7 of 7). This explains the rest:
- **Snap turns were fine:** they never use Prey's turn, so the body never lags.
- **Ordinary follow turns were fine:** the body is already on the view.
- **Body Follows You jittered** (inferred, not measured with it): it called the setter during
  stick turns, every frame, so every frame carried the lag twice.

### The frame order it rests on

`CSystem::Update` (`0xDF5640`) calls the entity system's `PrePhysicsUpdate` (vtable `0x1D34798`,
slot `+0x18`, `0x923BF0`), then physics, then `EntitySystem::Update` (slot `+0x20`, `0x926520`),
where `ArkPlayer::Update` runs. Inside the player's update (`0x15806E0`): the stick's turn rate
(`0x158F5B0`, zero at once below the deadzone, then aim assist `0x12DFC20`), the helmet component
(`0x1327070`), then `ArkPlayerCamera`'s update (`0x148A250`). The view (`UpdateView`) and the
render come after.

`CCharInstance::StartAnimationProcessing` (`0x82F910`, character vtable `0x1D22200` slot `+0x90`)
copies the caller's location to `+0xA90` for the job (`CSkeletonAnim::ProcessAnimations`
`0x839500`). The player uses CryAction's AnimatedCharacter. With the turn made after the render
(PreyVR-81, run `player-20261024-213107`, 16 of 16), the next frame's animation started 7-8 ms
later, on the main thread, before the view and the render, posed for exactly the turned body,
and Prey's view equalled `m_rotation`.

### The two fixes

1. **The world (PreyVR-80):** `TorsoLane::TurnToTorso` reads the entity's rotation before the
   setter. When the body lagged by more than 0.05°, that frame's view takes the yaw from
   `m_rotation` (camera `+0x150`), unless Prey's focus camera supplies the view: player `+0x9B0`
   with its state at `+0x98` nonzero (`0x15ABD20` returns at once when it is 0). Worn: the world
   was clean; the body was not.
2. **The body (PreyVR-81):** the follow turns the body after the frame's render (rule 3). Worn:
   fixed, world and body. The cleaned build, PreyVR-82, passed a sanity check.

### Ruled out on the way

| Candidate | Why not |
|---|---|
| Compositor frame synthesis (SteamVR at 120 Hz, Prey at about 63 fps) | Turning the follow off removed the flash |
| The old cull lag ([CULL-CAMERA-CRASH-2026-10-17.md](CULL-CAMERA-CRASH-2026-10-17.md)) | No lag in 20 runs |
| The stick turn lane (MoveLane) | Posts on change, always posts the zero |
| The hands' frame | Previous camera minus the head yaw: independent of R |
| Motion blur, temporal AA | `r_MotionBlur 0`, `r_AntialiasingMode 1`, both read back |
| View effects (`0x117B4D0`) | Time-based, independent of turning |
| The helmet's copy | The error was in `#camera`; the helmet only sways with the body |
| The weapon | The flash showed with it holstered |

The investigation used read-only frame-probe steps and a hook on `0x82F910` in three diagnostic
builds (PreyVR-78, 79 and 81); all of it was removed once the fix was worn.

## Room-scale

**Why it is needed.** The rendered eye is the native eye plus `Yaw(P) · (head − origin)`, and
physical walking grows that offset without bound: the head walks through walls, the arms reach
from a body up to 1.8 m away, line-of-sight checks start beyond the wall, and looking down after a
step shows the body somewhere else. Turned round in place after a step, the camera ends up behind
the body's head (a 0.3 m step and a 180° turn put it 0.3 m behind). Room-scale is the positional
twin of the turn: it moves the capsule under the player and the origin by exactly the move it
applied, so the view never moves.

### How it works (2026-10-25)

`RoomScaleLane.cpp`, with the pure half in `preyvr/RoomScale.h`:
- **When:** after the frame's render, right after the follow's turn (`ServiceBodyFollow`). The
  render has taken the body's transform and the next frame's animation starts posed for the moved
  body. Physics is idle there on 99.97% of frames, against 94.5% at `ArkPlayer::Update`'s entry,
  where it used to run (PreyVR-84), so a move applies at once instead of waiting.
- **Why the stale frame is still right:** the move keeps the world camera where it was, so the
  gameplay frame the next animation reads (the last one published, built before the move)
  still places the hands and the body pin correctly in the moved body.
- **What it follows:** your eyes as they would be with the head level and facing the torso
  (`armpose::LevelEye`), at most 3 m/s, with a jump of more than 50 cm between frames refused as a
  tracking glitch.
- **The lean zone** (PreyVR-89): your head may lead the capsule by up to 25 cm before it follows,
  and over that the body pin bends the torso to your head, as a lean from the hips does. Past
  the zone the capsule follows the rest at once, so walking, it trails your head by at most that.
  Setup → **Lean Zone** (`lean_zone`, PreyVR-90) sets it, off to 50 cm in 5 cm steps; off follows
  beyond the 2 cm deadzone, as PreyVR-87 and 88 did.
  Bent well down, more than 4 cm below the calibrated height, your head may lead by the lean a hip
  hinge of 60 cm explains for that drop, if that is more, at most 37 cm
  (`roomscale::LeanAllowance`): bending to the floor bends the body. PreyVR-87 and 88 followed
  everything beyond 2 cm at standing height, so a lean slid the body under you instead of bending
  it.
- **Settling: a lean held, a step caught up.** While your head moves, a lean cannot be told from
  a step: 20 cm about the hips drops the head less than 4 cm, as much as a walk's bob. At rest it
  can. A head that stays within 2 cm of where it rests for half a second (that point trails the
  head by 0.2 s, so a head slower than 10 cm/s is at rest) was a step if it is lowered by less than
  half what a lean that far out lowers it (`roomscale::LeanDrop`, a 60 cm hinge, plus 3 mm): the
  capsule glides under it at 0.4 m/s, to within 2 cm. Lowered more, it is a lean, and held. The
  height is smoothed over the same 0.2 s. **Upright** is learnt while you stand still over the
  body (over about a second), so a stance more relaxed than the calibrated one is not taken for a
  lean; a recenter or a calibration takes it back to the calibrated height. Moving on stops a
  glide.
- **Fitted, never pushed back:** the living body's own stance test bisects how much of each move
  fits ([the capsule's physics](#the-capsules-physics)), so the capsule stops at walls and
  furniture instead of being placed in them and pushed back out. A body already touching
  something may still move if the overlap gets no deeper.
- **Where the capsule stops short, the torso bends:** the body pin leans the spine out to its
  limits towards a head the capsule could not follow. The capsule is wider than you, so it stops
  about 20 cm short of a desk.
- **Keeps the motion:** a walking body is moved with `pe_params_pos` (`bRecalcBounds = 0x21`),
  from its physics position, only while physics is idle; at rest the entity's `SetPos` alone. A
  walk's first 150 ms count as walking.
- **Level only:** height stays the view's and the engine's. **Stairs** need nothing of it: a step
  lower than the capsule's bottom (0.53 m above the feet) never overlaps it, so the move fits and
  the living step climbs the stair. (The PreyVR-87 notes said the fit test would refuse a physical
  step up a stair; it cannot, and stairs raised nothing in that build's headset check.)
- **The head in a wall:** the line from the capsule's axis, 45 cm below the eyes (kept within 0.3
  to 1.25 m above the feet), to the head's pivot and on to the eyes is tested against the scene each
  frame. Through something, the view fades to black (in 0.1 s, out 0.2 s) and firing, using and psi
  wait (`BodyHeadOut`). Over a desk the line passes above the top, so a lean there does not fade.
  This replaced the old rule that faded whenever the head was 30 cm from the capsule.
- **Snaps:** with the capsule under you, a snap's turn about the body is a turn about you again.
  The snap still lands at the frame's start: moved after the render, the next frame's IK would map
  the hands with the old play-space yaw and draw them 45° off for a frame.
- **A fault** in any engine call turns room-scale off until Prey is restarted (`roomFault`),
  leaving the capsule where the game puts it. The calls are compared against their bytes before
  the first one (`roomAvailable`).

### The capsule's physics

- **The capsule.** The living entity's collider: radius 0.35 m, from 0.53 to 1.90 m above the feet
  standing and 0.40 to 1.50 m crouched, pivot 0; the model's origin is the physics feet exactly
  (PreyVR-84). `pe_player_dimensions` (type 1, as the living `GetParams` `0xB46AA0` copies it) has
  `heightPivot` `+0x04` and `sizeCollider` `+0x0C` (x the radius, z the half-height), as
  ContactNative.h has them, then `heightCollider` `+0x18` (from `+0x1B8`) and `bUseCapsule` `+0x34`
  (flags `+0x2C0` bit 4). The sizes come from the stance's Lua data through the player's
  physicalize (`0x157EDE0`; the per-stance table at `ArkPlayer+0x1610`, stance data `0x157D3A0`).
- **Only the capsule collides.** The living entity has one part (its constructor `0xB441D0`:
  `flagsCollider` 2, `m_flags` `0x643`). The character's skeleton has a physics entity of its own,
  hosted by the living one and set to simulation class 4 (`0x96D930`), which the living step skips.
  There is no head sphere. A torso is 12–15 cm from its centre to its front, so the capsule meets
  furniture 20 cm or more before you would.
- **`SetPos` is the engine's teleport.** `CEntity::SetPos` (`0x90AF40`) runs the transform update
  (`0x905480`), which sends the XFORM event unless `whyFlags` has bit 18 (`0x40000`, NO_EVENT). The
  physics component's handler (`0x96BE00`) builds `pe_params_pos` with only `pos` and
  `bRecalcBounds = 1`, and calls the living entity's `SetParams` (`0xB49610`, slot `+0x20` of
  `0x1D81580`). There, when the position moved by more than 1% of the collider's half-height
  (`+0x1CC`) and `bRecalcBounds & 0x20` is clear:

  ```
  B49A52  call 0xB49390            ; release the ground colliders (+0x2C8, +0x2D0)
  B49A57  or   [rbx+2C0h],1        ; airborne
  B49A76  mov  [rbx+16Ch],rsi      ; velocity = 0 (skipped only for entity 0x7777 with dynamics flag 8)
  B49A7D  mov  [rbx+168h],esi
  B49A83  mov  [rbx+1D0h],esi      ; step-smoothing offset = 0
  ```

  The requested velocity (`+0x174`) is kept, so a walk does not stop dead: it accelerates again from
  zero under air control until it lands. A yaw-only `SetRotation` sends only `q` and skips the
  branch, so turning the body is not a teleport. ArkPlayer's XFORM handler (`0x15806E0` case 0)
  copies the rotation and checks the level bounds; the animated character's (`ArkPlayer+0x1808`,
  `0x4F87A0`) re-syncs its location and zeroes its prediction on every XFORM whose `whyFlags` lack
  `0x1002000`. Neither resets movement.
- **The move that keeps the motion.** First `pe_params_pos{pos}` with `bRecalcBounds = 0x21`: bit
  `0x20` skips the living branch, and the base `SetParams` (`0xB88AA0`) still recomputes the bounds.
  The entity's `SetPos` that follows finds the position already there, so it skips the branch too.
  Only while the body is moving: its own velocity (`pe_status_living` `+0x30`, from `+0x168`) or the
  movement's request (`+0x3C`, from `+0x174`; the living `GetStatus` `0xB475D0`) above 5 cm/s
  (`roomscale::BodyMoving`), or the stick pushed within 150 ms (`MoveLaneStickStarted`: a walk's
  first frames read as rest, because both velocities are last frame's). **At rest the teleport has
  a job:** the living step runs only on a body that is moving, airborne or climbing a step, and the
  airborne flag is what makes the next step settle a body moved while standing onto the floor,
  ramp or stair under it. So at rest the entity's `SetPos` alone.
- **A move must not undo a physics step.** The change request (`0xB1BCD0`), which both `SetParams`
  run first, applies a call at once, from a thread other than physics, only while the world's step
  lock (`+0x576C`), its `+0x57B0` and its lock (`+0x5784`) are clear and the entity has nothing
  queued (`+0x40` below `0x20000`). Otherwise it is queued and replayed after the step, where an
  absolute position computed before the step puts the body back by that step's movement. So a
  moving body is moved only while all four are clear (`PhysicsIdle`), from its physics position
  (`+0xA8`), and otherwise waits a frame (`busy`). If the entity was behind its physics, catching
  that up is the walk's own movement, which the view shows rather than cancels. A step that begins
  between the two calls queues the entity's own `SetParams` behind it (`raced`; none seen).
- **The stance test.** `pe_status_check_stance` (`GetStatus` type `0x15`; the base `GetStatus`
  returns 0 for it, so the living one answers) tests the collider at `pos` `+0x04` (the body's own
  position when unused), with `q` `+0x10`, `sizeCollider` `+0x20`, `heightCollider` `+0x2C` and
  `bUseCapsule` `+0x40`, each its own when unused, through the living step's own part filter
  (`0xB56900`): every entity around but itself, its own parts, and what ignores collisions with it.
  It returns 1 when nothing overlaps; otherwise the depth goes to `+0x3C` and, with a zero
  `dirUnproj` (`+0x30`), the contact's normal there. It runs on the game thread's query slot (4),
  the scene query's; game code never calls it, only the living entity around stance changes.
  Room-scale bisects a refused move 6 times (`roomscale::FitFraction`, within 1/64 of the first
  contact) and takes an end whose overlap is no deeper than where the body stands
  (`roomscale::NoDeeper`). The alternative, a real capsule sweep with the per-entity primitive test
  (`0xBA1D80`), would need the broadphase and the same filters by hand.
- **Why a capsule put into furniture comes back out.** The living step (`0xB4BF60`, slot `+0x100`)
  first tests for overlap where the body stands (`0xB4ED4A`). The player is `pushable_by_players`
  (`m_flags` `0x643` has bit `0x200`, tested at `0xB4F421`), so it is moved out along the contact
  direction, in full 3D, by the depth plus a small gap, up to 2.5·r per overlap
  (`0xB4F41D`–`0xB4F548`; the constant at `0x1C85B60` is 2.5). A low edge can lift the player
  instead, through the same push or the ground probe's step-up. The physics position is synced to
  the entity after the step (`0x96C010`), and the view is the entity position plus the `#camera`
  attachment plus the head offset, so the view goes back with it.
- **A step-up being smoothed** (camOffset, `pe_status_living` `+0x0C`) holds the move until it
  settles (`stepSettling`).
- **Carried objects** count in the stance test, though the living step ignores them, so a body
  carrying one can start a test already touching something (`startOverlap`); the no-deeper rule
  still lets it move.
- **The entity's seams**, pinned by `verify_view_rotation.py`: the concrete `CEntity` vtable
  `0x1D30BD0` (the hash-gated `GetPhysics` `0x9042B0` at `+0x228`); `GetPos` `+0x110` → `0x10EA250`
  (`&m_vPos`); `SetPos(pos, whyFlags, recalcPhys, force)` `+0x108` → `0x90AF40` (stores `m_vPos`,
  `flags |= 2`, tail-calls the transform update `+0x350`); `SetRotation` `+0x118` → `0x90B220` (the
  same with `|= 4`); `GetWorldTM` `+0xE8` → `0x10EA260`. The game's own small move, the stance
  change (`0x157B980`), uses the same `GetPos`, ceiling ray, `SetPos` pattern.

### Before: room-scale until 2026-10-24

**What it did.** Each frame at `ArkPlayer::Update`'s entry, in `BodyFollow.cpp`, it moved the
capsule toward the head with the entity's `SetPos`, swept first with nine
`RayWorldIntersection` rays (`roomscale::SweepRays`: 0.5, 1.0 and 1.5 m up, from the capsule's
centre line and both sides), and moved the origin by the applied displacement. It had a
head-in-wall layer: a fade to black, with the trigger and use button held off, while the segment
from the body to the head hit geometry or the head was 30 cm from a capsule the sweep had stopped.
It was the Comfort option **Body Follows You**: Off, Turn Only, Turn and Move.

**The stall on turns** (MOVE-STOP's cause A, 10-11 to 10-19). Every `SetPos` was the teleport
above. And turning alone made room-scale move: its follow point then was the neck plus 8.05 cm
along the *torso's* facing, and the torso lags the head (35° deadzone) and then relaxes towards
it, so after a physical turn the follow point swung round the neck though the body had not moved
(`follow_sim.py`, 90 fps, the mod's settings):

| Turn | `SetPos` frames | Capsule moved | Over |
|---|---|---|---|
| 45° in 0.4 s | 16 | 5.3 cm | 1.1–4.0 s |
| 90° in 0.5 s | 29 | 10.0 cm | 0.3–1.9 s |
| 180° in 1 s | 74 | 22.5 cm | 0.3–2.4 s |
| 60° glance held 1 s | 17 | 5.9 cm | 0.2–5.8 s |
| 30° glance held 2 s | 0 | 0 | |

A snap pivoting about the eyes with the head turned from the torso shifted the follow point by up
to 3.7 cm, which could start a glide too. The guard meant to stand aside during a walk measured the
capsule's speed after the fact, so the stall fed itself: a pending glide, a slow first frame, a
`SetPos` that zeroed the velocity, and the guard stayed open. The fix (PreyVR-42, 10-19) was the
move that keeps the motion. PreyVR-42 then left the body behind mid-walk, because room-scale stood
aside entirely while the engine moved the capsule (85% of one run); PreyVR-43 followed during
walks too, from the physics position and only while physics was idle, and the wearer confirmed
the body kept up with no stall (run `player-20261019-200651`: `keptMotion` 2959, `deferredBusy`
220, the entity up to 7 cm behind its physics at a move). PreyVR-72 (10-24) added the walk start
(`startKept` 6) and the race counter (`raced` 0).

**The lean push-back** (10-19). Leaning moved the follow point forward and room-scale glided the
capsule after it. The sweep missed desk tops at 0.7–0.8 m, between two ray heights, and corners
that met the capsule's round front between the lines, so `SetPos` put the capsule inside the
furniture, and the living step pushed it back out, with the view (above); then the head was ahead
again, and it repeated. `blocked` reached 963 in one 10-18 run. The stance test (fix 2, PreyVR-42)
ended it: the wearer reported the push-back gone, with `pushOut` (frames the engine moved the body
at rest) 25 in four minutes, at most 73 mm (PreyVR-43: 10, at most 50 mm).

**The fade over a desk** (PreyVR-65/66, 10-24): leaning over a desk faded the view (run
`player-20261024-113604`: 3 fades, 86 frames, no head hits): the 30 cm rule, left for later then,
and replaced by the head-in-wall line in PreyVR-87.

**What it taught:**
- **Snap-back does not happen:** a 20 cm `SetPos` stayed put for 300 frames (`nudgeDriftMaxMm=0`).
- **Follow the neck, not the eyes.** With a 12 cm deadzone the body lurched after you; with 3 cm
  it chased the eyes, which swing several centimetres whenever the head turns or tips (934–1863
  `SetPos` a minute). Following a point 7.5 cm below and 8 cm behind the eyes, levelled to the
  torso, fixed both.
- **The body keeps moving with you, as a real one does** (your requirement, 10-19): no deferring
  room-scale while the stick is in use, no large step deadzone, no turning it off. A narrower
  capsule was dropped too: it changes gameplay collision. (The lean zone of PreyVR-89 is a 25 cm
  deadzone, chosen on 2026-10-25 once the body pin bends the drawn body to the head: the body no
  longer visibly lags, only the collision does, and a step is caught up once you stand still.)
- **A fade layer needs a 640×480 texture or more:** `InventorySwapchain` silently refuses smaller
  ones (FAILURE_REGISTRY F-018).
- **Runtime tracking-space changes come in bursts** (up to 11 a second around taking the headset
  off), each forcing a recenter.

**Why it went.** With Body Follows You on, smooth turning jittered when spinning; with it off it
did not (your A/B, run `player-20261024-180831`, Turn Only and Turn and Move both jittered). It
was removed whole: the follower, room-scale, the fade layer, four options and nine commands. The
shared torso and the snap seam stayed in TorsoLane. Both of its modes turned the body every frame
during stick turns, which the flash investigation (above) later showed was the jitter; room-scale
itself had been worn OK in PreyVR-42, 43 and 72. It came back on 2026-10-25, rebuilt after the
render.

## The body pin

**After Prey animates the body, the mod poses it again under your head.** It works in the IK pass
(`ProcessAnimationDrivenIK`, `0x877B50`) and writes only the drawn rig's absolute pose, as the arms
do. The view, the capsule and the shadow keep the game's.

- **The neck under yours.** Your atlas, the head's pivot, is `armpose::kNeckFromEye` from your eyes:
  7.5 cm below, 8 cm behind. Turning or tipping your head leaves it where it is. On the rig the same
  point is the game's eye (`camera_jnt`) less that offset, and the neck is placed to put it on yours.
- **The neck bends with your head:** half your head's pitch below level and a quarter above, as a
  neck does when people look down.
- **The throat stays behind your eyes.** Looking straight down with a stiff neck put the eyes 3 cm in
  front of the throat joint (PreyVR-84); with the bend it is about 9 cm. If the throat comes closer
  than 8 cm behind them, along your view, the neck is pushed back by the difference. The required
  gap shrinks as the throat lies lower, and is zero 12 cm below the eyes.
- **The pelvis hangs the spine's length below the neck.** It goes back 0.45 cm for each cm the neck
  drops; Prey's own crouch takes it back 0.48 cm per cm. If that would fold a leg tighter than 30 cm
  hip to ankle, the back leans further forward. Lower than the deepest squat, the feet sink into the
  floor, which hides them.
- **The hips and legs stand back, under your head** (PreyVR-90). Prey's standing idle has the hips
  on the model's axis, under its eye, and the neck 10.6 cm behind it. With the neck under yours,
  the back leaned 10° back from the hips, and looking down your legs seemed ahead of you. The pelvis
  and both feet now stand back together by **Hips and Legs Back** (Setup, `hips_back`, 0–20 cm,
  default 8 cm): 8 cm is your eyes from your head's pivot, which a person's hips stand under. The
  legs keep their shape, the pelvis drops about 1 cm as the back straightens, and the back stands
  within 3° of upright. At 0, Prey's own stance.
- **The spine bends instead of stretching.** 60% of a lean is at the hips and the rest is spread up
  `spine1` to `spine4`. The spine turns towards the torso the shoulders face, for example in a long
  weapon's stance, at most 45°; the pelvis does not turn.
- **The legs keep Prey's feet.** Each leg is solved from the hip to the planted ankle
  (`armpose::SolveElbow`). The knee bends the way Prey's does, or forward when Prey's leg is straight.
- **Prey's crouch had a kinked waist.** It lowers the upper body by moving `spine2_jnt` 32 cm off
  `spine1_jnt`, and the hips came through the belly. On the rig this was measured on, the pin first
  moves `spine2_jnt` back to within 5 cm of its rest. A rebuild of the rig mid-animation (every snap
  turn re-binds it) can read the offsets a few millimetres off, so once matched, a skeleton stays
  matched (PreyVR-86 saw the straightening switch off on snaps without it).
- **The arms ride the drawn chest** ([the shoulders](#the-shoulders)). The hands move with it but
  keep their rotation, which is the controller's.
- **How far it leans to your head.** With room-scale moving the capsule, as far as the spine leans,
  fully out to 1 m and given up only at 1.5 m: a head far from the capsule is one the capsule
  could not follow.
  Without room-scale (its engine calls refused, or a fault), up to 35 cm away, less from there and
  not at all from 70 cm, so a step away leaves the body standing.
- **When it cannot pose a frame** (the rig lacks a joint it needs, or a joint does not read), the
  game's pose is drawn and the shoulders come from the neck (`pinRefused`).

**What it rests on** (the research builds PreyVR-84 and 85, 2026-10-25):
- **The rig.** `hip_jnt` (2) carries `spine1` to `spine4` (6, 29, 30, 32), `neck_jnt` (34) and both
  legs: `upperLeg` 7/8, `lowerLeg` 19/9 and `foot` 22/12, with thigh and shin both 42.3 cm. The
  neck carries `head_jnt` (95), and that `camera_jnt` (98, the game's eye and `#camera`), with
  `vr_headset`, `flashlight` and `psychoscope_light` on it; `neckTwist` (94) carries `throat_jnt`
  (100). `camera_jnt` sits on the model's axis, 1.70 m up standing and 1.00 m crouched: Prey's
  crouch drops the eye 0.700 m. Standing, `spine1` to the neck sit at 1.044, 1.166, 1.309, 1.423 and
  1.530 m, the neck 10.6 cm behind the model's origin. `hip_jnt` sits on the axis, 0.93 m up, so
  the spine leans back 10° from the hips to the neck; the idle's feet are staggered, the right
  11 cm forward and the left 30 cm back.
- **Prey's crouch** moves the pelvis 0.42 m down and 0.20 m back, and moves `spine2` and everything
  above it straight down by exactly 0.700 m, which is the kink.
- **What Prey keeps of a write.** Joint writes made after the native pass hold, the pelvis's and
  the feet's included. Feet pushed down into the floor come back up to it (Prey plants them after
  the pass; where is unconfirmed); feet moved along it stay.
- **What the view ignores.** It does not read `camera_jnt` from the pose the mod writes: the pelvis
  and the camera joint raised 10 cm together did not move it.
- **Looking down** put the eyes about 15 cm lower (the head pitches about the neck), right at the
  throat joint: the camera was inside the throat.
- **Nothing in the world hid the body then.** It was drawn in Prey's near pass, in front of
  everything; this line once read "a desk hides the legs", which was wrong. Since PreyVR-91 the
  world hides it ([how the body is drawn](#how-the-body-is-drawn)).

**It replaced the torso lift** (`arm.lift`, 2026-10-19 to 10-25). Calibrated seated then standing,
the shoulders had risen off a torso left at the calibrated height. The lift stretched the spine by
the neck's rise since calibration, shared up `spine1` to `spine4` by their heights (about 20% to
all of it), and never moved the camera joint. Worn OK on PreyVR-44 (committed as `7088229`), its
40 cm / 15 cm limits removed on 10-20; but a stretch squashed the spine at −45 cm and turned it
inside out below the hips at −60 cm. `arm.lift`, `TorsoLift`, `LiftClasses` and `LiftShares` are
gone.

**What an earlier plan proposed** (10-19): a hip–neck–head body line for the fade (built, above), a
pelvis estimate the capsule would follow instead of the neck, and a waist bend from the capsule's
hips to the neck. The body pin is that bend. The pelvis estimate was dropped: at standing height
the head cannot tell a lean from a step while it moves, so room-scale's lean zone holds the
capsule while it moves and decides once it rests.

## The shoulders

**The arm solver ([ARM-POSE-2026-10-06.md](ARM-POSE-2026-10-06.md)) reaches each hand from a
shoulder joint on the torso: −9 cm forward, ±18.5 cm out and −23 cm up from a centre eye in the
torso frame (`arm.shoulder`).** Which eye that is, and which torso, is the body's business.

- **From the neck, not the eyes** (2026-10-19, PreyVR-41, committed as `079e55f`).
  `armpose::ShoulderFromHead` lays the offset out from `armpose::LevelEye`: the eye as it would be
  with the head level and facing the torso, from the atlas (`kNeckFromEye`). Turning or tipping your
  head alone leaves the shoulders where they are; a lean still moves them.
- **On the drawn chest** (2026-10-25). With the body pin, the collarbones and the upper arms ride
  the posed chest, and the shoulder target is where `ShoulderFromHead` would put it for the body as
  the game left it (`bodyrig::ShoulderFromAtlas`, from the rig's atlas), carried with the chest.
  It is laid out from the chest as Prey turned it (`BodyPlan::chestTurn`): during a smooth turn
  Prey turns the upper body towards the view while the body lags, and laid out from the body's
  facing, the shoulders trailed the chest and the arms stretched (PreyVR-86, fixed in PreyVR-87).
- **Without the pin** (a frame it refused), the shoulders come from the neck, as on 10-19.

**Why the eyes were wrong** (10-19). The wearer reported the arms bending inward, as if there were
too little room between shoulder and wrist. Two errors shortened that distance:
1. **The shoulder moved with the eyes.** The head pitches about the neck, so looking down 15, 30,
   45 and 60° moved the eyes, and the shoulders with them, by +1.7, +2.7, +2.9 and +2.5 cm forward
   and −2.3, −5.0, −7.9 and −10.7 cm down: towards the hands, where you look to see your arms.
   Turning the head moved them sideways the same way.
2. **The arm was the average adult's 56 cm**, whatever yours is. Its fix, the Arm Length option, is
   the arm solver's.

`arm_sim.py`: the elbow angle (180° is straight) the mod drew, against the player's real one, for
the same real arm:

| Pose | Player's arm | Head pitch | Real | Drawn |
|---|---|---|---|---|
| Arm straight forward | 56 cm | 0° | 169° | 169° |
| Arm straight forward | 56 cm | 30° | 169° | 146° |
| Arm straight, 30° down | 56 cm | 45° | 169° | 126° |
| Arm straight forward | 50 cm | 0° | 169° | 126° |
| Arm straight, 30° down | 50 cm | 45° | 169° | 103° |
| Hands at the belly | 56 cm | 45° | 89° | 69° |

Folded that much, the elbow chooser put the extra bend round the shoulder-to-wrist axis: the
"inward" bend. The October mock never showed it: its known arms used exactly the model's shoulder
offsets and arm length, with the head level (`evidence/arm-pose-2026-10-06/truth-gloo.txt`).
Scaling the player to the character instead was rejected: it changes how big the world feels, and
the drawn arm is already the mod's, not the rig's (Prey's is 51.6 cm, stretched).

## How the body is drawn

**The body is drawn in the world, at the world's depth: what is in front of it hides it.** Prey
drew it in front of everything, so a hand reached behind a crate showed through the crate (TODO,
reported on PreyVR-86). Fixed in PreyVR-91 and 92, worn OK 2026-10-25.

- **Why Prey drew it over everything.** Its first-person character (the rig above: arms, body,
  legs, and the weapon, one of its attachments) is drawn in CryEngine's near pass. The entity slot
  chooses it: `ENTITY_SLOT_RENDER_NEAREST`, slot `+0xAC` bit 1, puts `FOB_NEAREST` (bit 23) on the
  slot's object flags (`+0xA8`) and so on each render object. The near pass draws into its own slice
  at the front of the depth buffer, viewport depth `[0, r_DrawNearZRange]`, `[0.9, 1]` in reverse
  depth with the default 0.1, through its own projection: near plane 0.03 m, far
  `r_DrawNearFarPlane` (40 m). The world's near plane is 0.1 m, so only a surface within about
  11 cm of the eyes ever reaches that slice. Every mesh of a near character is near (the skin
  draw tests the master character's flag and ORs the render params'), and nothing lets one out.
- **What changed (`body.depth`, default 1).** The slot render, `CEntityObject::Render`, is hooked.
  For the player's slot (its character is the player's first-person one, published by the gameplay
  lanes) in the main view's pass (the rendering flags `CSystem::Render` builds it with), the near
  bits are clear for the call: the slot's own and `FOB_NEAREST`, on the slot and in the render
  params, and the character's `CS_FLAG_DRAW_NEAR` (never seen set). They are put back after it.
  Without the bit the slot is drawn at its world transform instead of camera-relative, and the
  character, its meshes and the weapon are all drawn inside that call. Each eye's own view gives
  the body its parallax.
- **Every other pass is Prey's.** The shadow passes draw the slot about 4.7 times for each main
  view (`kept`, `keptFlags=0xae5de`), still in the near pass, so the shadows are as before. Other
  characters' near draws are Prey's too (`others`).
- **The near plane (Setup → Near Clip Distance, `near_clip`, 1–10 cm, default 3).** Drawn in the
  world, the body is cut at the view's near plane, 10 cm, where the near pass cut it at 3 cm:
  looking down, PreyVR-91's view cut through the neck. `CView::Update` builds the camera with the
  view's own near plane (`SViewParams` `+0x2C`) when it is at least 1 cm, else with the view
  system's 10 cm default, and Prey's player view never sets one. So the view hook sets it, after
  Prey's `UpdateView`, while VR drives the view; it keeps a near plane Prey set itself, and gives
  the default back when VR stops. Everything built from the camera follows: the eyes' frusta,
  culling, the near pass's scaling. `pl_cameraNearZ` (0.24 m) has no reader that reaches the view.
- **What a smaller near plane costs: depth precision far away.** `r_DepthBits` is 24 by default
  (D24S8, reverse depth), and no config changes it, so depth steps are about 3.3 times coarser at
  3 cm than at 10: about 2 cm at 100 m against 6 mm. Raise the setting if distant surfaces flicker.
- **Measured** (PreyVR-92, run `player-20261025-183819`): `world=7024 kept=30303
  keptFlags=0xae5de others=105 characterNear=0 paramsNear=0 unread=0`.

## Height, posture and the body slots

- **The body stands at the height you calibrate at.** VR start calibrates, and Setup → **Calibrate
  Current Posture** (`view.calibrate`) retakes it: it records the head's height as the baseline
  and resets the view and controller reference together. **Reset VR View**, like an ordinary
  recenter, keeps that height. The panel reports success, a busy reference, missing tracking, or
  the need to look straight ahead. A request keeps its action, session and one-second deadline
  together; a newer click replaces a pending one, and teardown or a session change discards it.
- **No seated mode** (decided 2026-10-25): the body takes the calibrated height as standing, and
  the runtime's floor does not move it. Seated players compensate themselves.
- **How far below it your head is** is what the body pin crouches by, and what room-scale's lean
  allowance reads (`HeadDropBelowCalibrated`).
- **Physical crouching does not change Prey's stance:** the capsule stays standing until you
  crouch with B. That is planned for after the overhaul.
- **The body slots.** The holster and medkit slots sit on the shared torso, relative to the head,
  so they follow the head down when you crouch (DEBUG-OVERLAY). **Body Slot Feedback**
  (Abilities, `belt_hints`, on by default) shows brief cards low in the view, only while the
  matching slot feature is on: medkit selection, confirmed use, no stock or refusal; holster
  assignment and the stow or draw request accepted. They are messages, not a claim that the equip
  animation finished. They have their own sRGB swapchain, framed against both eyes' frusta with a
  margin, expire after 1.5 s, and hide across menus and tracking, session and reference changes; an
  unused medkit selection hides when the grip is let go. (Built 2026-10-04 with 57/57 offline
  checks; not checked in a headset then.)

## Engine facts (Steam hash)

Collected from the sections above, with how each was established.

| What | Where | Evidence |
|---|---|---|
| `ArkPlayer::Update` (vtable slot 19) | `0x1584FE0` | bytes (TorsoLane's hook) |
| View-rotation setter and getter (slots 52, 53); the helmet component's copy and update | `0x1584CC0`, `0x157CE80`; `0x1327050`, `0x1327070` | bytes (`verify_view_rotation.py`) |
| Prey's eye: entity rotation × `#camera`, plus the entity position; `UpdateView`; `CView::Update` | `0x1489010`; `0x148B820`; `0x3D9B9D` | decompile, measured (PreyVR-79) |
| Frame order: `CSystem::Update`, `PrePhysicsUpdate`, `EntitySystem::Update`; the player's update | `0xDF5640`, `0x923BF0`, `0x926520`; `0x15806E0` | decompile, measured (PreyVR-81) |
| `StartAnimationProcessing` copies the location to `+0xA90`; ADIK pass | `0x82F910`; `0x877B50` | decompile, measured |
| Entity transform: `SetPos`, `GetPos`, `SetRotation`, `GetWorldTM`, `GetPhysics` | `0x90AF40`, `0x10EA250`, `0x90B220`, `0x10EA260`, `0x9042B0` | bytes (`verify_view_rotation.py`) |
| Transform update; skips the event on `whyFlags` bit 18 | `0x905480` (`0x9059A0 bt r14d,12h`) | listing |
| Physics component XForm → `pe_params_pos` (pos or q, `bRecalcBounds = 1`); physics → entity sync | `0x96BE00` (call `0x96BFA3`); `0x96C010` | listing |
| Living `SetParams`; its teleport branch, skipped when `bRecalcBounds & 0x20` | `0xB49610`; `0xB499CE`–`0xB49A8E` | bytes (`verify_room_scale_physics.py`) |
| Base `SetParams` (pos, q, sim class, bounds) | `0xB88AA0` | bytes |
| Change request: at once only while the step lock, `+0x57B0` and the world lock are clear and nothing is queued | `0xB1BCD0` | bytes |
| `pe_status_living`: velocity `+0x30`, request `+0x3C`; `pe_player_dimensions` | living `GetStatus` `0xB475D0`; `GetParams` `0xB46AA0` | bytes |
| Stance test `pe_status_check_stance` (type `0x15`) and its part filter | `0xB56900` | bytes |
| Living step; overlap test; push-out (`pushable_by_players`, up to 2.5·r) | `0xB4BF60` (slot `+0x100`); `0xB4ED4A`; `0xB4F41D`–`0xB4F548`, constant `0x1C85B60` | bytes |
| Living constructor: one part, `flagsCollider` 2, `m_flags` `0x643` | `0xB441D0` (`0xB4472D`, `0xB4473B`, `0xB44819`) | bytes |
| Player physicalize; the per-stance capsule | `0x157EDE0`; `ArkPlayer+0x1610`, `0x157D3A0` | decompile |
| Skeleton physics: a separate entity, sim class 4 | `0x96D930` | decompile |
| ArkPlayer and animated-character XFORM handlers | `0x15806E0`, `0x1580AD6`; `0x4F87A0`, `0x4F886C` | listing |
| Movement: AddMovement → `pe_action_move` | `0x4F5D00` → `0x4FC070` → living Action `0xB44E60` | decompile |
| The stance change (`GetPos`, ceiling ray, `SetPos`) | `0x157B980` | decompile |
| The rig, the capsule's span, the idle physics after the render | the body pin; the capsule | measured (PreyVR-84, 85) |
| Slot render `CEntityObject::Render`: slot RCX, params R8, pass info stack `+0x30`; the near bit (`0x9734AF`), object flags into the params (`0x973471`), put back (`0x9748A7`); the character drawn at `0x9747F6`; one caller | `0x973400`; `0x97CF2F` | bytes (`verify_body_depth.py`) |
| Slot updates set `+0xA8` bit 23 from `+0xAC` bit 1; character `SetFlags` (vtable `0x1D22200` `+0xC8`) | `0x9763F0`, `0x97D050`; `0x82EC20` | bytes |
| Near in `RenderCHR` (params or `CS_FLAG_DRAW_NEAR`); skins and skeleton attachments | `0x81D13A`–`0x81D155`; `0x7AAE06`, `0x7C768A` | bytes |
| The near pass's depth slice and projection: `UpdateNearestChange`, the viewport setter, `r_DrawNearZRange` | `0xF43D70`, `0xF90390`; `0x2B1C718` | bytes, decompile |
| The view's near plane: `SViewParams+0x2C` if at least 0.01 (`0x1C7F774`), else `GetDefaultZNear` (vtable `+0x78`, `CViewSystem+0x90` = 0.1, constructor `0x3DAB30`); `SetFrustum` | `CView::Update` `0x3D977A`–`0x3D97FD`; `0x2167E0` | listing |
| `r_DepthBits` default 24; `r_ReverseDepth` default 1 | `0xE0B651`; `0xECC72B` | listing |

## History

| Date | Package | What changed | Result |
|---|---|---|---|
| 09-19 | | Snap turns pivot about the head; the body's yaw is not rewritten (COMFORT-CONTROLS) | Superseded on 10-11 and 10-24 |
| 10-04 | | Calibrate Current Posture and Body Slot Feedback | 57/57 offline; not worn then |
| 10-06 | | The arm hangs from the player's shoulder, on a torso from the headset (ARM-POSE) | On the mock |
| 10-11 | `PreyVR-body-follow-wip` | **Body Follows Head:** the body follows the torso every frame through the setter | "A definite improvement"; `readbackMaxMdeg=0`, the view never moved for a body turn |
| 10-11 | `PreyVR-roomscale-wip` to `-3-wip` | **Room-scale** in three stages: follow, then smaller steps and the fade, then the neck model and standing aside | "Everything works as expected" |
| 10-19 | PreyVR-41 | Shoulders from the neck; Arm Length | Worn OK (`079e55f`) |
| 10-19 | PreyVR-42, 43 | Room-scale moves keep the motion; the stance test; following during walks | Worn OK: no stall, no push-back |
| 10-19 | PreyVR-44 | The torso lift (`arm.lift`) | Worn OK (`7088229`); uncapped 10-20 |
| 10-24 | PreyVR-65, 66 | (the hands refactor) | A fade leaning over a desk: the 30 cm rule, left for later |
| 10-24 | PreyVR-72 | Room-scale's walk start and race counter, with the stick fix (MOVE-STOP) | Worn OK |
| 10-24 | PreyVR-73 | Smooth turning jittered with Body Follows You on: **all of it removed** | Worn OK, but snaps no longer turned the body |
| 10-24 | PreyVR-74 | One body turn to the torso after each snap | The camera moved against the model |
| 10-24 | PreyVR-75 | Snaps turn about the body | Worn OK |
| 10-24 | PreyVR-76 (`68bfc1d`) | Physical turns turn the body again, paused while Prey turns the player | Worn OK; one bad frame when a stick turn stopped |
| 10-24 | PreyVR-78, 79 | Diagnostics: the view error, then its terms | The cause, above |
| 10-24 | PreyVR-80 | The view takes the yaw from `m_rotation` on a lagged turn | World fixed; the body still flickered |
| 10-24 | PreyVR-81 | The follow turns the body after the render | Fixed |
| 10-24 | PreyVR-82 | Diagnostics removed | Sanity check passed |
| 10-25 | PreyVR-84, 85 | Research: the rig, the capsule, and which joint writes hold (`body.capsule`, `body.posetest`) | The body pin's facts, above |
| 10-25 | PreyVR-86 | The body pin replaces the torso lift (`arm.lift`, `arm.anchor` removed) | "Very promising"; the shoulders lagged the chest in smooth turns |
| 10-25 | PreyVR-87 | The shoulders on the chest as Prey turned it; room-scale back, after the render, with the fit test and the head-in-wall line | "Tested and verified working. Everything checks out." |
| 10-25 | PreyVR-88 | Cleanup: the research commands (`body.capsule`, `body.posetest`, the automatic skeleton dump) and the switches (`body.follow`, `body.followafter`, `body.roomscale`, `body.pin`, `body.neck`, `body.pelvis`, `body.throat`, `ik.anchor`, `aim.bodyyaw`) removed, their defaults kept; one `body.report`; this document | The torso did not bend in a lean (in 87 too): the body slid under the headset |
| 10-25 | PreyVR-89 | The lean zone: the capsule follows past 25 cm, or settles under a head that came to rest upright; upright learnt over the body | Worn OK (leaning); one crash with no dump in the first run, cause unknown |
| 10-25 | PreyVR-90 | Two Setup options: Lean Zone (off to 50 cm, default 25) and Hips and Legs Back (0–20 cm, default 8: the hips under the head's pivot, not the eyes) | "Tested and verified working as expected." |
| 10-25 | PreyVR-91 | The body drawn in the world, not Prey's near pass (`body.depth`) | A hand behind an object is hidden; looking down, the view cut into the body (the 10 cm near plane) |
| 10-25 | PreyVR-92 | Setup → Near Clip Distance (1–10 cm, default 3): the view's near plane | "Tested and verified working." |

## Open

- **Snap turns still turn the body at the frame's start** (rule 1). If Prey had turned the player
  just before (aim assist, a script), the snap's body turn could show the same one-frame pose as
  the flash; the snap's own view jump would hide most of it. Not seen. Not moved after the render:
  the snap changes P, and the next frame's IK, which reads the frame published before the snap,
  would draw the hands 45° off for a frame.
- **On a catch-up the body still turns by Prey's lag in one frame**, up to about 45°. Only visible
  looking down at it.
- **A torso from the controllers** (the hands' average yaw) rather than the head alone, for
  looking sideways while holding a weapon forward. Not tried.
- **What the lean zone costs** (PreyVR-89): walking, the capsule trails your head by up to the zone,
  so collision lags it and the drawn body leans into the walk until it settles; rounding a tight
  corner, the head-in-wall line from a trailing capsule could cut the corner and fade the view for
  a moment. A lean of 10 cm or less lowers the head too little to tell from a step, so held still,
  it settles. A stance relaxed below the calibrated height reads as a lean until you have stood
  over the body once (after a recenter, at once).
- **What is still drawn over everything:** Prey's DrawNear particles (muzzle flashes, the Q-Beam,
  charge glows: NEAR-PARTICLES), other characters' near draws (`others`), and the aim lasers,
  which the mod draws with depth off (TODO).
- **Hands within 3 cm of the eyes are cut**, as in the near pass. Distant flicker from the smaller
  near plane was not reported on PreyVR-92; if it is, Near Clip Distance trades it back.
- **After the overhaul:** physical crouching into Prey's crouch stance; the holster and medkit
  slots on the drawn body (today they follow the head); the shadow, which keeps Prey's pose; the
  optional no-body mode in TODO.md.
