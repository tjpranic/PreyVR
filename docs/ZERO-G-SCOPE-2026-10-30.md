# Zero-G rotation and the zero-G helmet: causes and fix scope (2026-10-30)

Branch `custom`. Two TODOs, reported from the GUTS zero-G sections:
- "Zero-G doesn't have rotate controls."
- "Zero-G helmet overlay doesn't render properly. It doesn't follow the camera view."

Found statically with Ghidra over the Steam DLL (SHA `7d6e322f…11a7`). Addresses are RVAs. Nothing is built or worn
yet. `tools/re/verify_zero_g.py` checks the byte contracts below (29 checks), and the registry rows are under
[Zero-G rotation and the helmet](ADDRESS_REGISTRY.md#zero-g-rotation-and-the-helmet--2026-10-30).

## 1. Zero-G rotation

### Why there is none

**Prey's zero-G is full six degrees of freedom, and the mod sends none of its rotation inputs except yaw.** Four
layers stand between the player and a working roll:

1. **No input.** In zero-G Prey rolls with its lean actions (`leanleft`/`leanright`, the gamepad's LB/RB). Holding
   both brakes. The look stick's Y pitches the whole body. The mod sends none of these in play:
   - The Frame's bumpers post `xi_shoulderl/r` only while Prey's weapon wheel is open (`MoveLane`, the
     `wheel_suit_repair`/`wheel_food` actions).
   - Right stick Y posts nothing.
   - Yaw already works: the smooth turn's `xi_thumbrx` turns the body about its own up axis in zero-G.
2. **The view keeps only Prey's yaw.** `UpdateViewObserved` takes the yaw of Prey's view and builds the eye from it
   and the head ([HeadTrackingHook.cpp](../src/dll/HeadTrackingHook.cpp), "Read the game's rotation only to take its
   **yaw**"). A roll or pitch Prey applied to the body would change where thrust goes, but the world would not tilt.
3. **The body turns level the body.** TorsoLane's follow and snap turns write `bodyturn::LevelledTurn` through the
   view-rotation setter (slot 52), which drops pitch and roll. In zero-G that setter's target, `m_rotation`, *is* the
   body orientation the zero-G state rotates. Each follow turn would flatten the player back to level.
4. **Every hand-side lane works in a yaw-only frame.** `GameplayPoseFrame::yaw` and `stereo::ReferenceFrame`
   (`yawRadians` about engine Z) feed about 29 files: aim, shot ray, interaction, scene query, physical contact,
   grenades, carry, wrist and IK. That is about 54 direct yaw uses beyond the two central conversions. With a tilted
   body, the hands and the aim ray would be placed in a level frame while the view is tilted. This is the
   "basis-independent formulation" [VR_MECHANICS_RISK.md](VR_MECHANICS_RISK.md) item 1 asked for.

A fifth layer only matters once the inputs exist:

5. **The stick re-read drops a held roll.** On every filter event, for example the interaction prompt, Prey's re-read
   (`0x158E780`, [MOVE-STOP](MOVE-STOP-2026-10-24.md)) re-reads both lean flags from the physical pad, and pitch too.
   With motion controllers it reads 0. `StickReread` restores move and yaw only.

### The chain

| Step | Where | What |
|---|---|---|
| Zero-G state | ArkPlayer `+0x58` == 9 | The movement FSM's state. The zero-G state object is at ArkPlayer `+0x368`. |
| Lean actions | action table `+0xA00`/`+0xA08` → `0x1590250`/`0x1590290` | A press sets ArkPlayerInput `+0xB5`/`+0xB6`; a release clears it. |
| Lean axis | `0x15910B0` | (`+0xB6` ? 1 : 0) − (`+0xB5` ? 1 : 0) goes to input `+0x80` (ArkPlayer `+0x960`). Both held sets `+0xB1`, the brake. |
| Look rates | `0x158F5B0` | Pitch `+0x44` → `+0x50`, yaw `+0x4C` → `+0x58`. Outside zero-G, yaw is scaled by the camera's `+0x40`. |
| Rotation | ArkPlayerCamera update `0x148A250` → `0x159E1D0` → case 9 `0x126AF50` | `m_rotation = m_rotation × Euler(rates) × roll`. Each turn is about the body's own axes. Roll is a rate: the lean axis with acceleration `+0xB8`, a maximum `+0xB4` and deceleration `+0xBC`. Braking zeroes it. |
| Body | zero-G update `0x126FCC0` | Sets the entity's rotation from the view rotation, with the position moved for the roll pivot (`pl_ZeroGRollMode`, `pl_ZeroGRollPivotOffset`). |
| Eye | `0x1489010` | Entity rotation × a strafe roll (`0x159C4A0`, case 9) × `#camera`. |

### Routes

| Route | Verdict |
|---|---|
| **A. Prey owns the body's orientation; the mod sends its inputs and works in the body's frame.** | **Recommended.** Movement, collision, landing and the drawn body already follow `m_rotation`, so this is the only route where "up" agrees everywhere. |
| B. Tilt only the VR reference (a "roll" like the snap turn's yaw), with Prey's body kept level. | No. Prey's thrust, its landing alignment (`0x126AF50` aligns to surfaces) and the body mesh stay level, so the player's up disagrees with the game's. |
| C. Bind the inputs and nothing else. | No. The view stays level while thrust directions tilt, and the follow levels the body again within a turn. |
| D. Fly where you look: head-directed thrust and no body rotation. | Not a fix. Prey's vertical thrust is the jump and crouch buttons, which are digital, and the player still cannot turn to land on a wall. It could be added later. |

### Route A, in three phases

1. **Inputs** (small).
   - Detect zero-G from ArkPlayer `+0x58 == 9`.
   - While in zero-G, the bumpers post `xi_shoulderl/r`: Prey's roll, with both together as its brake.
   - Pitch is one of the decisions below.
   - Extend `StickReread` to put back the lean flags, calling `0x15910B0` after, and pitch if it is posted.
   - Test: on its own this phase rolls the body while the view stays level, so it is a test step, not a release.
2. **The view and the body turns** (medium).
   - In zero-G, `UpdateViewObserved` builds the eye on `m_rotation × Rz(−room.yaw)` instead of the yaw alone. The
     strafe roll and `#camera` stay out, because the base is `m_rotation` and not Prey's composed view.
   - Outside zero-G nothing changes.
   - TorsoLane turns the body about its own up axis (`q × Rz(δ)`), not `LevelledTurn`.
   - `NativeTurnWatch` compares turns in the body's frame.
   - Snap turns do the same.
   - Room-scale's moves need the basis too, or room-scale pauses in zero-G until phase 3.
3. **The hand-side frame** (large).
   - `ReferenceFrame` and `GameplayPoseFrame` carry a basis quaternion: `Rz(yaw)` in gravity, as now, and the body's
     basis in zero-G.
   - `EyePoseInWorld`, `ControllerPoseInWorld` and `EngineToTracking` use it.
   - The direct yaw uses get the same audit: torso yaw, carry, holster slots and room-scale.
   - Without this phase the hands are wrong whenever the body is tilted, so phases 1 and 2 are not shippable alone.

### Phases 1 and 2, as built (2026-10-30, PreyVR-111, worn OK)

Built together, because phase 1 alone could not be judged in the headset: the view would stay level and the follow
would level the body again.

**Worn OK 2026-10-30** (run `player-20261030-170928`): pitch and roll work as expected, smooth and snap. The report:
`zeroG=1 zeroGSteps=35 zeroGStepsRefused=0`, `rollSteps=22 pitchSteps=13 pitchPosted=299 pitchRefused=0`,
`leansKept=2`.

**Zero-G** is ArkPlayer `+0x58 == 9`, read by TorsoLane at each `ArkPlayer::Update` entry (`PlayerInZeroG`; the log
says `preyvr_torso ... zero_g value=1/0`).

**Inputs** (MoveLane, `preyvr/ZeroGControls.h`):
- Outside Prey's weapon wheel the bumpers are Prey's lean buttons in zero-G: roll left and right, both together the
  brake. In the wheel they are still its LB and RB.
- **Zero-G Roll** (Comfort, `zerog_roll`):
  - Smooth (the default) hands the bumpers to Prey, which rolls while they are held.
  - 15–90° makes a bumper alone one step. It first waits 120 ms for the other bumper, so pressing both brakes without
    rolling first.
- **Zero-G Pitch** (`zerog_pitch`):
  - Smooth posts the right stick's Y as Prey's `xi_thumbry`, with the turn's deadzone and speed.
  - A step is one push past 0.65 that is more up or down than sideways (the snap turn takes the sideways ones).
  - Outside zero-G the stick's Y does nothing, as before.
- **Steps** are queued to TorsoLane and made at the next `ArkPlayer::Update` entry: `m_rotation × step` about the
  body's own axis, through the view-rotation setter.
- **Prey's re-read** keeps them. Pitch goes through `AxisRead` (axis 3). The lean buttons go through a new detour on
  `ButtonRead` (`0x158E540`), which finds a bumper the lane holds down, in zero-G only.

**The view** (HeadTrackingHook):
- In zero-G the play space is `m_rotation × Rz(−room.yaw)` (`ReferenceFrame::tilted`/`basis`, used by
  `EyePoseInWorld`), so a roll or pitch tilts the world around you, and a lean moves along the tilted body.
- Not while Prey's focus camera has the view. On a level body it equals the gravity view, so entering zero-G does not
  jump.

**Body turns** (TorsoLane):
- The follow and snap turns turn the body about its own up axis (`bodyturn::LocalTurn`) instead of levelling it.
- They read back how far it turned with `LocalYawBetween`.
- The "is the game turning the player" watch compares whole rotations (`NativeRotationWatch`), since Prey's stick
  pitch and roll turn it too.
- Room-scale stands in zero-G: its moves assume a level body.

**Known gaps** (phase 3 and the headset):
- The hands, aim, interaction, physics and IK still use a yaw-only frame. While the body is tilted they are placed
  wrong.
- Leaving zero-G with the body tilted, the view goes back to yaw-only at once. Prey's righting (`ZeroGTransitionOut`)
  then happens under a level view.
- Signs to confirm: the right bumper and a smooth roll right should both lower the right side; stick up and a snap up
  should both raise the nose.

**What the run must show:**
- `torso={... zeroG=1 ...}` while floating.
- `viewZeroG` rising.
- `move={... rollSteps/pitchSteps ...}` in snap mode, `pitchPosted` in smooth.
- `reread={... leansKept ...}` rising if you hold a bumper past interaction prompts.

### Phase 3: the hand-side frame, as built (2026-10-30, PreyVR-112, worn OK)

**Worn OK 2026-10-30** (run `player-20261030-172908`): "plays really well", no issues found. The report:
`zeroG=1 zeroGSteps=31 zeroGStepsRefused=0`, no faults.

**One basis, split in two.** The view hook already composes the eye on the play space's whole rotation `B`: the
yaw's alone in gravity, `m_rotation × Rz(−room.yaw)` in zero-G. It now publishes `B` with the view
(`AppliedView::basis`/`tilted`).

In zero-G, `BuildGameplayFrame` splits it into a heading and a tilt, `B = tilt × Rz(yaw)` (`stereo::SplitHeading`):
- `GameplayPoseFrame::yaw` stays the heading every lane already uses.
- The new `tilt` is identity whenever the body is level, so a lane that knows only the yaw is right everywhere but a
  tilted zero-G body.
- `PlaySpaceRotation(frame)` is `tilt × Rz(yaw)`.

**Two central helpers take the basis**, and each yaw form is the basis form on `Rz(yaw)`, operation for operation
(the `zero_g` test pins it bit for bit):
- `animik::ControllerWorldFromHead`: through `HandWorld`, the arms, weapon, contact, swing, grenades, carry, psi and
  wrist.
- The StereoCamera family: `EngineToTracking`, `TrackingToEngine`, `TrackingDirToEngine`, `EngineDirToTracking` and
  `TrackingOriginInWorld`.

**The sites, from a full audit of the yaw-only uses:**

| Site | What it places |
|---|---|
| AimTakeover's aim reference | The ray Prey fires along |
| `AnimIkState.h` XrMap | Every arm and body IK conversion: shoulders, elbows, torso capsules, the body pin |
| `psi::BuildRay` (InteractionLane, PsiMedkit) | The left hand's use pointer, psi aiming |
| ReticleFollow | The crosshair in the world |
| DebugOverlay (`EngineToXr` and its directions) | The Aim Lasers |
| WristLane | The wrist card's occlusion test |
| PhysicalInteractions, SurfaceStopLane | The weapon box, hand stops |
| AnimIkHand, TwoHandHold | The support hand's offset, to play space and back |
| CarryLane, GrenadeLane releases | Throw velocity and spin, now stored as the basis |
| AnimIkReports, InteractionLane | Diagnostics |

ContactLane's and SurfaceStopLane's continuity guards compare whole rotations (`stereo::RotationAngle`) instead of
yaws. That is the same number on a level body, and in zero-G a snap pitch or roll now resets them as a turn does.

**Left as they are:**
- **Heavy carry's grab offset** (`HandCarry` `HeavyShift`) turns with a yaw delta and clamps along world Z. It matches
  Prey's own carry target, so it needs Prey's zero-G carry behaviour checked first.
- **Movement** stays body-relative: Prey moves you along the body's forward, so looking up does not steer you up.
  Fly-where-you-look (route D) remains a possible addition.
- **World-Z assumptions** stand: a grenade's floor height, contact underfoot, room-scale's follow (paused in zero-G).

**What the run must show:**
- `status` → `tiltedFrames` rising while floating tilted.
- Hands, weapon, lasers, crosshair and shots line up with the controllers at any roll or pitch.
- Grabs, throws and the wrench behave as they do level.

**Comfort.** Prey's roll is a smooth, accelerating rate. Roll that the neck did not make is the motion most likely
to make a VR player ill. Snap roll (and snap pitch) can be done the way snap turns are: a local-axis step written
through the setter, which phase 2 needs anyway. Leaving zero-G, Prey rights the player (`ZeroGTransitionOut`). In VR
that is a forced roll, so it needs a look in the headset.

## 2. The zero-G helmet

### Why it follows the body

**The helmet is a bone attachment on Prey's eye joint, and in VR nothing moves that joint.**
- The `helmet` attachment hangs on `camera_jnt` (joint 98) of the player's character (slots 0 and 5). It holds its
  own animated character, the visor: `Breather1P_Close` plays as it deploys (`0x1326690`).
- Every frame Prey's HUD-helmet component (ArkPlayer `+0x1468`, update `0x1327070`) eases a copy toward the live view
  camera. It writes the lag, clamped, as the helmet's relative default: `SetAttRelativeDefault`, vtable `+0x58` =
  `0x828DC0`, rotation only, translation 0.
- The bone pass makes the model transform `+0x130` from joint 98 × that default.
- The view hook replaces Prey's view after Prey computes it, and the body pin leaves `camera_jnt`'s subtree alone
  ([FLASHLIGHT-HEAD-SCOPE](FLASHLIGHT-HEAD-SCOPE-2026-10-29.md), [BODY-FOLLOW](BODY-FOLLOW.md)).

So the helmet stays at Prey's eye, level and facing the torso. Looking around (the body follows only past a 35°
deadzone), looking up or down, or leaning brings its rim into view. The flashlight had the same cause.

One more effect: the sway is measured against the live view camera, which in VR is the head. So head turns also make
the helmet lag.

### Routes

| Route | Verdict |
|---|---|
| **A. Re-place the helmet's model transform before the render.** | **Recommended.** It is HeadLamp's pattern, for an attachment instead of an entity. Details below. |
| B. Write the relative default (vtable `+0x58`) the way Prey's sway does. | Fallback only. The bone pass runs before the view (HeadLamp measured 4706 of 4706 frames), so the helmet would trail the head by a frame. Head-locked geometry would visibly swim. |
| C. Write `camera_jnt`. | No. It also moves `#camera`, `vr_headset` and the lamp's mount, and BODY-FOLLOW forbids it. |
| D. Hide the helmet (vtable `+0x88`). | Trivial, but it loses Prey's helmet. Worth keeping as an option. |

**Route A in detail.** In `BeforeEngineRender`, after the frame's view, write the player's `helmet` model transform
(`+0x130` q, `+0x140` t):

> `Mh' = Mc · Δ · Dc⁻¹`

- `Mc` is `#camera`'s model transform (`+0x130`).
- `Dc` is `#camera`'s relative default (`+0xF8`).
- `Δ = P⁻¹ · V` is the VR eye relative to the eye Prey composed. The view hook has both; `LastAppliedView` would also
  publish `P`.
- The character's render location cancels, so the animation-start location (`+0xA90`) is not used. Using it would
  make the helmet trail the player's movement.
- Multiplying by `Mc⁻¹ · Mh` instead of `Dc⁻¹` keeps Prey's sway.
- `ArmsLane`'s `FindAttachment` finds both attachments by name.
- No hook is needed: Prey's sway write is simply overwritten, and its creak sounds stay.

### Open until a run

- **Which slot draws the visible helmet.** Slot 0 is the first-person character; slot 5 is probably the shadow
  (`0x1326690` unhides its shadow with `+0xA8`).
- **Whether a model transform written before the render survives to the draw.** The lamp proved this for an entity,
  not for an attachment's `+0x130`.
- **The helmet's rest offset from the eye, and its look at the headset's FOV** (about 110°, against Prey's flat FOV).
  The visor rim may show more than it does flat, which may call for a scale.
- **Exterior EVA** is presumably the same FSM state 9. Check it.

### As built (2026-10-30: PreyVR-108, fixed in 109, options in 110, worn OK)

Route A, with the sway dropped, as the wearer chose.
- `src/dll/Helmet.cpp`: the lane. No hook: `FrameSchedule::BeforeEngineRender` calls it after the head lamp, and
  `AfterEngineRender` settles the frame.
- `include/preyvr/Helmet.h`: the math. `include/preyvr/HelmetNative.h`: four body contracts.
- `tools/re/verify_helmet.py` (the contracts) and the `helmet` test.
- `LastAppliedView` now also carries `prey`, the view Prey composed before it was replaced.

**PreyVR-108 drew no helmet** (run `player-20261030-160029`, worn 2026-10-30). The report:
`placed=1845 kept=1845 overwritten=0 mountMm=0,0,0 eyeGapMm=1206313.4 eyeGapDeg=0.0`.
- The render did draw the transform written before it (`kept`, no `overwritten`).
- The angle was right, but the helmet was put about 1.2 km away: the player's distance from the world origin.
- **Cause:** in first person Prey's view position is not a world point.
  - `UpdateView` (`0x148B820`) writes the entity's rotation × `#camera`'s offset, with no position added (its
    debug branch transforms the same value by the entity's world matrix to draw it).
  - `CView::Update` (`0x3D91C0`) adds the entity's world position afterwards (`GetWorldPos`, CEntity `+0x140` =
    `0x904D10`). It does so while slot 0 draws as first person, after giving that slot a camera-space position
    (`+0x2A0`).
  - `ConsumeHeadRenderPose` in HeadTrackingHook.cpp already recorded this ("SViewParams position here is relative
    to the player's world origin"). PreyVR-108 missed it.

**PreyVR-109** uses only the view's rotation and the head's offset from Prey's eye, and anchors the helmet on the
`#camera` the body draws. In the character's model space:
- the headset's eye is `#camera`'s model transform moved by the offset and turned to the view
  (`helmet::EyeInModel`);
- the helmet is that eye ∘ the mount (`helmet::OnEye`). The mount is `#camera`'s relative default and K undone,
  then the helmet's K.

The character's rotation and scale come from the player entity's world matrix (ArkPlayer `+0x38`, CEntity `+0x60`).
Its position is no longer needed. On Prey's own eye this gives exactly Prey's helmet with no sway (the test checks
it).

`eyeGapMm` / `eyeGapDeg` now compare Prey's composed eye, its position taken relative to the entity, with the
`#camera` the body draws. Near 0 confirms the reading.

**Space Helmet** (VR Options > Interface, `helmet`, shown by default). Hidden sets HideAttachment's bits (`0x70000`)
for the render alone and gives Prey's own back after it, so a deploy or stow by Prey is never lost. A helmet Prey has
hidden is left alone either way. `PREYVR_HELMET=0` at launch leaves Prey's helmet untouched.

**PreyVR-109 worn (2026-10-30): the helmet is on the head.** One complaint: you can see out of its bottom edge.
Prey's helmet was made for a flat view, and the headset's view is taller.

**Position and size (PreyVR-110)**, VR Options > Interface, under Space Helmet:
- **Helmet Height** (`helmet_height`) and **Helmet Forward / Back** (`helmet_forward`): ±100 mm in 5 mm steps,
  along the eyes' own axes. They are a shift between the eye and the mount: `helmet::OnEye(eye, mount, shift)`.
- **Helmet Size** (`helmet_size`), 50–200 %.
  - The bone attachment's transform has no scale. The attachment draw (`0x821FD0`) builds parent render matrix ×
    rotation `+0x130` / translation `+0x140`.
  - The helmet is a character held by a character attachment (vtable `0x1CB1328`, its character at `+0x08`). Its
    `RenderAttachment` (`0x334DF0`) calls the character's Render (`0x81BCB0`, CCharInstance `+0xC0`) with an
    identity `QuatTS` offset. Render multiplies the rotation by that offset's scale (`[R14+0x1C]`, `0x81BF8B`).
  - So a detour on Render hands **only the helmet's character** the same offset with the scale × Helmet Size.
    Every other character passes straight through.
  - The scale is about the helmet's origin, which is the eye moved by the shift. Size alone keeps the angles the
    helmet covers and only changes how near it looks. Moved first, it changes its reach too.
  - If the hook cannot be made, the helmet still goes on the head at Prey's size (`sizeHook=0`).

**PreyVR-110 worn OK (2026-10-30, run `player-20261030-163152`).** The wearer settled on 50 mm down, 30 mm back and
120 %. The report: `placed=34396 kept=34396 overwritten=0 sizeHook=1 sizedDraws=32056 eyeGapMm=0.0`. The maxima
(`eyeGapMaxMm=777.4`, `eyeGapMaxDeg=7.3`) are from frames where Prey's view is not first person; the helmet does not
use that position.
- `placed` rising in zero-G and outside.
- `eyeGapMm` near 0.
- Slot 5's helmet (the shadow) is not moved; its shadow may sit at Prey's eye. Look for it.

## Decisions for the wearer (answered 2026-10-30)

1. **Roll:** both snap and smooth, chosen in VR Options; smooth by default.
2. **Pitch:** the same as roll: right stick Y pitches the body in zero-G, snap or smooth, smooth by default.
3. **Brake:** both bumpers, as Prey does it.
4. **Helmet:** locked to the head (sway dropped), with a VR Options setting to hide it.
5. **Order:** the helmet first, then the zero-G phases.
