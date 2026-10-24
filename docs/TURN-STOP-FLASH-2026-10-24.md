# One bad frame when a smooth turn stops (2026-10-24)

## Symptom

With smooth turning (`turn=0`), the moment a stick turn stops, the headset shows one bad
frame, "like the camera is rendering an old frame". It happens with the weapon holstered.
`body.follow 0` removes it (the user's A/B), so it comes from the physical-turn body follow
added in 68bfc1d ("Physical turning", PreyVR-76). The user keeps that feature: the flash has
to go, not the follow.

## What the follow does at that moment

`TorsoLane` pauses the follow while Prey turns the player, and for 150 ms after
(`bodyturn::NativeTurnWatch`). Meanwhile the torso estimate keeps relaxing toward the head,
so when the hold ends the follow catches up in one or a few frames, at most 360°/s, with
the slot-52 setter (`0x1584CC0`) and `TurnHeadTrackingReferenceWithNative` (native yaw G and
reference R together, play-space yaw P = G − R unchanged). Physical following makes the same
call every frame, in smaller steps.

## Ruled out, statically

| Candidate | Why not | Where |
|---|---|---|
| The view's rotation | Prey's `UpdateView` (`0x148B820`), camera mode 0, takes the eye from `0x1489010`: the **live** entity rotation (IEntity vt `+0x150`) × the movement roll (player `+0x58`, `0x159C4A0`, no yaw) × the `#camera` attachment's model-relative transform × lean. No smoothing between frames. TorsoLane turns G and R before it in the same frame, so the view hook's P is unchanged. | decompile |
| View effects | `0x117B4D0`, called at the end of `UpdateView`: time-based sine sway from active effects, independent of turning. | decompile |
| Prey's turn tail | `0x158F5B0` sets the stick's turn rate to zero below the deadzone: Prey stops instantly. | decompile |
| The hands | `GameplayPoseFrame` yaw is the previous camera minus the head yaw (`gBodyYaw` on), independent of R. Not the weapon (user: holstered too). | `GameplayFrame.cpp` |
| Motion blur, temporal AA | `r_MotionBlur 0`, `r_AntialiasingMode 1`, both read back in the log. | `VrMode.cpp` |
| The old cull lag | No `detail=lag depth` in 20 runs; the guard's kicks recover. | logs |

Frame order, for reference: `CSystem::Update` (`0xDF5640`) calls the entity system's
`PrePhysicsUpdate` (vtable `0x1D34798` slot `+0x18`, `0x923BF0`), then physics, then
`EntitySystem::Update` (slot `+0x20`, `0x926520`), where `ArkPlayer::Update` runs and TorsoLane
turns the body at its entry. Inside the player's update (`0x15806E0`): stick rate
(`0x158F5B0`), the helmet component (`0x1327070`), then `ArkPlayerCamera` (`0x148A250`).

## A lead ruled out: the helmet's own copy

The setter hands the rotation to the component at player `+0x1468`, which keeps a copy at
`+0x08` (`0x1327050`). Its update (`0x1327070`) eases that copy toward the live view camera and
rotates the character's `helmet` attachment (slots 0 and 5) by the difference, the HUD's
sway, with creak sounds from the same angle. Round 2 below found the error in the `#camera`
attachment instead; the helmet only sways with the body.

## The instrumentation (removed after the fix)

Three diagnostic builds (PreyVR-78, 79, 81) added read-only frame-probe steps (`FrameProbe`),
each catch-up of 0.2° or more opening a window from 8 frames before it to 12 after: the body
turn (applied, native and reference yaw, the helmet copy), Prey's composed view yaw and eye
point against the view hook's, the camera rendered, the eye's terms read with Prey's own calls
(entity rotation, `#camera` and `helmet` attachments, `m_rotation`), and a hook on
`CCharInstance::StartAnimationProcessing` (`0x82F910`) for the location the player's animation
poses for. All angles in millidegrees. The results below are what they measured; all of it,
with its `body.report` counters, was removed once PreyVR-81 was worn.

## Result, round 1 (PreyVR-78, run player-20261024-205536)

Seven catch-up windows, all alike: **Prey's own composed view yaw is wrong for exactly
the catch-up frame**, then equal to the native yaw again to the millidegree.

| Window | Catch-up | Prey's view, that frame | Prey's stick turn before it |
|---|---|---|---|
| 3 | +0.83° | +21.5° | ? |
| 4 | +0.45° | +16.2° | ≈ +124° |
| 5 | −0.51° | −6.9° | ≈ −98° |
| 6 | −0.58° | −9.5° | ≈ −86° |
| 7 | −0.43° | +17.0° | ≈ +103° |
| 8 | −0.62° | −16.3° | ≈ −143° |
| 10 | +0.31° | +49.3° | ≈ +61° (a menu between) |

- The error's sign follows the stick turn before it, not the catch-up (window 7), and it is
  10 to 150 times the catch-up.
- The follow turns on the frames after it, through the same setter, cause nothing.
- Prey's eye point (0,0,1700 mm, relative) does not move; the rendered camera follows the
  composed view.

So the first setter call after Prey itself turned the player makes Prey's eye (`0x1489010`)
produce a wrong yaw for one frame. Body Follows You called the setter during stick turns, every
frame: the same error every frame is a good fit for the jitter that removed it.

## Result, round 2 (PreyVR-79, run player-20261024-210526): the cause

Before each catch-up, while Prey's stick turn has just ended:

| Window | `m_rotation` − entity (body lag) | `#camera` attachment yaw | Prey's view error, catch-up frame | next frame |
|---|---|---|---|---|
| 3 | 30.067° | 30.067° | 30.067° | 0 |
| 4 | 45.638° | 45.638° | 45.638° | 0 |
| 5 | −32.318° | −32.317° | −32.317° | 0 |
| 6 | −10.605° | −10.605° | −9.877° | 0 |
| 7 | −18.823° | −18.824° | −17.564° | 0 |

- **During a stick turn Prey lets the body (the entity) lag the view**, and its animation turns
  the head, where `#camera` sits, by the lag: the view, entity × `#camera`, stays on `m_rotation`.
- The setter takes the entity all the way to `m_rotation` at once. The frame's animation ran
  before it (the character's animation starts in the pre-physics pass, before
  `ArkPlayer::Update`), so that frame's head still turns by the old lag: **the view gets the lag
  twice for one frame**. On the next frame the head is posed for no lag and the view is right.
- Why snap turns are fine: they never use Prey's turn, so the body never lags.
- Why Body Follows You jittered: it called the setter during stick turns, every frame, with
  the body lagging every frame.

## The fix (PreyVR-80)

Body follow keeps the setter. Before it, `TorsoLane::TurnToTorso` reads the entity's rotation
as Prey's eye does; when the body lagged the view by more than 0.05°, it calls
`UseNativeViewYawOnce` (`HeadTrackingHook.h`), and **that frame's view takes Prey's look yaw
from `m_rotation`** (the camera's +0x150) instead of the composed view, unless Prey's focus
camera (player +0x9B0, state +0x98) supplies the view. `m_rotation` is exactly what the
composed view equals on every other frame. The body still turns to the torso at once (the
entity jumps by the lag, as before).

**Worn on PreyVR-80 (2026-10-24):** the world no longer flashes; the player's own body still
shows one bad frame at the stop. Everything else worked.

## The body (PreyVR-81)

The same frame, seen from the character. Its animation for the catch-up frame started before
the turn (round 2: the head was still posed for the lag until the next frame's animation), so
the whole pose was made for the old facing: Prey's look pose turning the head and upper body
by the lag, and the arm IK (`AnimIkTakeover`), which maps the hands into the model with the
location the animation started with (`kParamsLocQ`). The render then draws that pose with
the turned entity, for one frame.

**The fix: the follow turns the body after the frame's render**, before the next frame's
animation starts. `TorsoLane::Tick` still decides at `ArkPlayer::Update`'s entry; it leaves the
turn to `ServiceBodyFollow`, which `FrameSchedule::AfterEngineRender` calls once the world
render has taken the body's transform. The next frame's animation then starts with the body
already turned, and the next frame's view sees the turned body and reference together. The
native-yaw view (above) stays as the guard for the world. `body.followafter 0` puts the turn
back at the frame's start, for an A/B. Snap turns still turn the body at the frame's start.

This relies on the player's animation starting after the previous frame's render, in the
pre-physics pass (AnimatedCharacter's CryEngine pattern); the worn run below measured it.

**Worn on PreyVR-81 (2026-10-24, run player-20261024-213107): fixed**, world and body. All 16
catch-up windows: the next frame's animation starts 7-8 ms after the turn, on the main thread,
before the view and the render, posed for exactly the turned body; Prey's view yaw equals the
native yaw on that frame (no lag left in the head). So the player's animation does
start after the previous frame's render, as assumed, and with the turn made there the native-yaw
view is no longer needed for the follow; it still covers `body.followafter 0` and snap turns.
