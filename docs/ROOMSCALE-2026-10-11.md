# The body does not follow the player across the room — investigation, 11 October 2026

Branch `playability-improvements`. Investigation, plan, and implementation in two stages (see
Implementation); the first two stages have run in a headset, the third not yet.
Follows [BODY-YAW-2026-10-11.md](BODY-YAW-2026-10-11.md): the body now turns with the player,
but it does not move with them. Reported in the headset: step back and the player model is in
front of you.

## Cause

The rendered eye is placed at:

    native eye (capsule + local eye offset, about 1.7 m up) + Yaw(P) · (head − origin)

This is in [HeadTrackingHook.cpp](../src/dll/HeadTrackingHook.cpp) (`UpdateViewObserved`). The
origin moves only on a recenter (to the head) or a snap (pivoting about the head). Physical
walking moves only the head, so the offset grows without limit; the only bound is a 10 m sanity
check. The capsule, the body mesh, collision and the native eye stay where they were. In the
headset run the head was 1.8 m from the seated origin (`render_contract … pose=-0.69,1.57,1.68`
at 04:25:08).

**Seeing your own body is the visible part. The rest:**
- **The head walks through walls.** The capsule never follows it.
- **Line-of-sight checks start at the head:** ShotRay's eye-to-hand check, the grenade pull-back
  and the wrist occlusion rays. A head through a wall passes them on the far side.
- **The arms reach from a body up to 1.8 m away.** The IK stretches, then clamps.

## Design: the positional twin of the yaw follower

The yaw follower keeps the reference yaw R equal to the torso yaw and turns the native player by
the same angle, so the play-space yaw P never moves. This keeps the native eye under the head and
moves the native player by the same distance as the origin, so the view never moves.

**Each frame,** in the same lane and at the same call site (`ArkPlayer::Update` entry, game
thread):

1. **Offset.** `offset` = the horizontal part of `Yaw(P) · (head − origin)`, in the world.
   - **Deadzone and glide:** idle until `|offset|` exceeds 3 cm, then glide towards the head
     (time constant 50 ms) until it is under 0.5 cm. The first stage used 12 cm and a
     full-speed catch-up, which read as the body lurching (see the headset result).
   - **Speed cap:** 3 m/s, so a tracking glitch never teleports the body.
   - **Jumps:** no move when the head jumped more than 0.5 m in one frame, which is a
     tracking-space change; the recenter that follows re-baselines.
2. **Collision.** Sweep the capsule's path with the mod's verified `RayWorldIntersection`
   adapter (`QuerySegment`), which skips the player's own physics.
   - Rays at about 0.45 m, 0.9 m and 1.35 m, from the centre and both side edges (± radius),
     each the move's length plus the radius.
   - Clamp the move to the nearest hit, less a small skin.
3. **Move.** `GetPos`, then `SetPos(pos + move)`, then `GetPos` again. The applied displacement
   is the difference, horizontal only.
4. **Compensate.** `origin += Yaw(P)⁻¹ · applied / scale`, in tracking space, under the recenter
   lock.
   - **No generation bump:** P does not change, so the 25 consumers that reset on a generation
     change are left alone. This is the same contract as `TurnHeadTrackingReferenceWithNative`.
   - **A refused lock means no move that frame.**

**Only the follower's own move shifts the origin.** Stick walking, falling, lifts, scripted moves
and level loads move the capsule natively, and the view goes with them as it does today.
Absorbing the capsule's total displacement into the origin would make all of those disappear from
the view, which is why the move is a separate, synchronous step whose displacement the lane
measures itself.

**Vertical stays as it is.** A physical crouch remains view-only and native crouch remains a
button. Mapping a physical crouch to the native stance is a separate item.

### When the capsule cannot follow: the head-in-wall layer

A blocked capsule leaves the head free to walk into geometry. Proposed:

- **Detect.** Each frame, cast the segment from the native eye to the head (`QuerySegment`). The
  head is "out" when the segment hits something, or when the residual horizontal offset exceeds
  30 cm.
- **Fade.** While the head is out, fade the view to black over about 0.1 s with an OpenXR layer
  in front of the eyes. Coming back restores it.
- **Refuse what would cheat.** While the head is out, hold off the trigger and the use button,
  so nothing is shot or used from beyond the wall. Built that way in the second stage, in place
  of re-rooting each line-of-sight check at the native eye. Physical melee swings are held off
  too. Known gap: a grenade already charging when the head goes out is released (thrown), as it
  is when a menu opens.

This is a second step after the follower, but it belongs to the same fix. Without it, room-scale
makes walking into a wall a way to see through it.

## What each consumer does under this design

From a survey of every use of the origin and the head-anchored world positions.

| Consumer | Effect |
|---|---|
| View | Unchanged, if the shift lands in the frame `CView::Update` reads the new entity position. The view's world position is added there, from the entity's transform, after `UpdateView`. A move at `ArkPlayer::Update` entry is in that frame |
| Aim, interaction, scene query, holsters, medkit, psychoscope, wrist, panels, shoulder targets, carry hand | Unchanged: head-relative or tracking-space, so the origin cancels |
| IK | Better: the rig, and its shoulders, now stand under the player |
| Corpse drag (from the feet), grenade pull-back and floor | Better: the feet are under the player |
| Throw velocity (hand velocity + `PlayerVelocity()`) | No double count expected: `SetPos` does not give the living entity a velocity. Verify (grenade report `playerV` while walking physically) |
| PhysicalInteractions world-from-reference transform | Unchanged under an exact same-frame shift. A one-frame mismatch would read as a fake swing: watch for wrench hits while walking |
| Snap, recenter, rebase | Unchanged. They and the follower share the try-locked recenter mutex |

## Native seams (static, Steam hash)

`tools/re/verify_view_rotation.py` now makes 27 checks.

| What | Where | Evidence |
|---|---|---|
| The player's entity | ArkPlayer `+0x38` | Used live by several lanes |
| Concrete `CEntity` vtable | `0x1D30BD0` | It holds the hash-gated `GetPhysics` (`0x9042B0`, SceneQuery) at `+0x228` |
| `GetPos` `+0x110` → `0x10EA250` | `lea rax,[rcx+38h]; ret` | Returns `&m_vPos` |
| `SetPos(pos, whyFlags, recalcPhys, force)` `+0x108` → `0x90AF40` | If the position changed: store `m_vPos`, `flags \|= 2` (POS), tail-call the transform update (`+0x350`) | `SetRotation` (`+0x118`, `0x90B220`) is the same with `\|= 4`, the flag the player's rotation event tests |
| `GetWorldTM` `+0xE8` → `0x10EA260` | The matrix at `+0x60` | Used live by `PlayerFeet` |
| The game's own small move | The stance change (`0x157B980`): `GetPos`, a ceiling `RayWorldIntersection(Game)`, `SetPos(pos, 0, 0, 0)`, camera update | The same pattern this design uses |
| Raycast | `RayWorldIntersection` `0xCCF250` through world `0x224D9C8` | Hash-gated in SceneQuery; game-thread only |

## Risks, and the first live checks

1. **Snap-back is the main risk.** Prey's physics runs asynchronously. A `SetPos` might be
   overwritten by a physics step computed from the old position, or the living entity might push
   itself back out of geometry. Either would make the view judder by the step while walking.
   - **Check:** a diagnostic `body.nudge 20 0` (proposed: 20 cm sideways, the origin compensated)
     with the stick idle. Expect the body to move 20 cm and the view not to move. The entity
     position read at `UpdateView` must equal what was set, for the next 300 frames.
   - **Decision rule:** any snap-back means moving through the physical entity instead (its
     position parameters, or the queued action path CarryLane already validates), and
     compensating when the move is observed rather than when it is requested.
2. **Thread.** `QuerySegment` runs only on the input-drain thread. The yaw run's
   `threadMismatch=0` says the follower and the view share a thread; that this is also the drain
   thread is to be confirmed, and the lane will count refusals.
3. **Stairs and slopes.** The lowest ray at 0.45 m will not see a 0.18 m riser. `SetPos` into a
   step is resolved by the living entity, by stepping up or by pushing back. Test on stairs and a
   ramp. Pushing back would show as snap-back in check 1's numbers.
4. **Zero gravity.** In Prey's zero-G sections the capsule floats, and a world-horizontal move may
   be wrong. Test outside the station and in GUTS; gate if needed.
5. **Capsule radius.** It is not located (`pe_player_dimensions`). Make it configurable, default
   0.35 m, and measure the gap left when walking into a wall.
6. **Gates as for yaw:** camera mode 0, no custom view, the cinematic gate. No move on a tracking
   jump.

## Implementation (11 October 2026)

### First stage

Steps 1 and 2 of the order below, and the option from step 5.

| Part | Where |
|---|---|
| Follower (deadzone, glide, speed cap, jump refusal), sweep rays and clamp, compensation | `include/preyvr/RoomScale.h`, `src/common/RoomScale.cpp` |
| Origin moved with a native move, under the recenter lock, no generation bump; refused unless positional tracking is in the view | `MoveHeadTrackingOriginWithNative`, `src/dll/HeadTrackingHook.{h,cpp}` |
| The position step after the turn, in the same `ArkPlayer::Update` hook | `src/dll/BodyFollow.cpp` |

**How the position step works:**
- **Byte gates.** The complete `SetPos`/`GetPos` bodies and the concrete entity vtable slots are
  compared at install. A mismatch disables room-scale and leaves turning available.
- **Per call.** The player's entity must carry that vtable.
- **Sweep.** It uses `QuerySegment`, and a refused sweep means no move.
- **Play-space yaw.** It is the one the latest view of the same reference generation composed
  with.

**The option.** **Body Follows You**, COMFORT row 4: Off, Turn Only, Turn and Move (the default).
Settings files saved before it existed load as Turn and Move.

### Headset result, first stage

Package `PreyVR-roomscale-wip` (DLL `A1AC8064…`), Steam Frame. Two runs, standing then seated.
The wearer reported **"The body follows the headset as expected."**

**Run `player-20261011-160434` (standing), last status line:**
- `moved=396`, `movedM=11.453`;
- **`viewDrift=0`, `viewDriftMaxMm=0`**;
- `sweepRefused=0`, `roomBusy=0`, `positionOff=0`, `jumps=0`;
- `stepFollowedMaxMdeg=36`;
- `blocked=1815` (frames pressing against something).

**Risk 1 does not occur.** On every one of the 396 moves, the entity was where `SetPos` left it
when the view composed. The multi-frame `body.nudge` check did not run: typed in the tuner, the
game was unfocused and paused, and the first stage dropped requests outside gameplay. The second
stage fixes that.

**What the wearer reported:**
- **The following was very noticeable.** With a 12 cm deadzone caught up at 3 m/s, the body
  visibly lurched after them. They want it to move more often, by less.
- **Leaning into a wall puts the head through it,** as this stage expected.
- **The Steam Frame's own boundary** switched to passthrough ("outside the playable area") when
  leaning far. That is the runtime's play-area guard, not the mod's.
- **27 runtime tracking-space changes in the seated run,** in bursts of up to 11 a second around
  taking the headset off and on. Each forced a recenter. The mod handles such bursts clumsily;
  this is not room-scale's fault, but it is worth an item of its own.

### Second stage

| Change | Detail |
|---|---|
| **Follow more often, by less** | Deadzone 3 cm, stop at 0.5 cm, and a proportional glide (time constant 50 ms, still capped at 3 m/s) in place of the full-speed catch-up. Walking, the body trails by about speed × 50 ms |
| **Diagnostics wait for gameplay** | `body.turn` and `body.nudge` run after a delay (default 5 s) at the first gameplay frame, so they can be typed in the tuner and watched in the headset. Dropped a minute after falling due. Each logs `detail=turn_done`, `nudge_done` or `nudge_refused` |
| **The head-in-wall layer** (step 4) | Each frame with room-scale on, the head is "out" when the segment from the capsule's axis at head height to the head hits geometry, or when the head is more than 30 cm from a body the sweep stopped (back in under 25 cm) |
| **Fade** | While the head is out, a black quad 0.1 m in front of the eyes covers the view. It fades in over 0.1 s and out over 0.2 s, on top of every other layer, and never over the menus |
| **No shots from beyond the wall** | While the head is out, the trigger and the use button are held off. A trigger held when the head comes back must be released first |

### Headset result, second stage

Package `PreyVR-roomscale-2-wip` (DLL `7954E8CF…`), run `player-20261011-164559`. The wearer
started standing, then sat down.

**Risk 1 is closed.** `body.nudge 20 0` ran in gameplay (`detail=nudge_done movedMm=200`). The
body stayed where it was put for the 300 frames watched (`nudgeDriftMaxMm=0`), and `viewDrift`
was 0 throughout.

**The two faults the wearer reported:**
- **"The player seems to get momentarily stuck in place, especially while turning."**
  - With a 3 cm deadzone the follower chased the eyes, which swing several centimetres about the
    neck whenever the head turns or tips.
  - It moved 934–1863 times a minute, about once every two to four frames, through `SetPos`.
  - A `SetPos` while the engine walks the capsule stalls the walk.
- **The fade never appeared.**
  - Detection worked: `headOutEvents=7`, `headHits=814`, the head up to 46 cm from a blocked
    body.
  - The layer never drew: `InventorySwapchain` refuses textures under 640×480, and the fade's
    4×4 texture was refused at creation, silently.

**Not a fault:** a 9° view-yaw step on a frame the body turned (`stepFollowedMaxMdeg=9145`).
It fell in the minute of tuner use. The tabbing run before showed the same size on idle frames;
that is native mouse look on refocus.

### Third stage

| Change | Detail |
|---|---|
| **Follow the neck, not the eyes** | The body follows the head moved by where its neck is now, less where it would be facing the torso, level (`FollowPoint`, neck 7.5 cm below and 8 cm behind the eyes). Turning or tipping the head alone no longer moves the body |
| **Stand aside while the engine moves the capsule** | When the capsule moved more than 0.25 m/s natively since the last frame (stick walking, a jump, a fall, a lift), no room-scale move that frame (`nativeMoving`) |
| **The fade draws** | 640×480, repainted per alpha level (8 levels), 0.2 m ahead and 5 m square. The swapchain logs `detail=fade_swapchain` with its result. `fadeFrames` in `body.report` counts frames it was submitted |

**New `body.report` fields:**
- `fadeFrames` should climb whenever `headOut` is 1. If it does not, read the `fade_swapchain`
  log line.
- `nativeMoving` climbs while stick walking.

### Commands

| Command | Effect |
|---|---|
| `body.roomscale 0\|1` | Room-scale on or off; turning is separate (`body.follow`) |
| `body.zone <startMm> <stopMm> [followMs]` | Deadzone and glide (default 30, 5, 50) |
| `body.radius <mm>` | Capsule radius for the sweep (default 350) |
| `body.nudge <rightCm> <forwardCm> [delayS]` | Diagnostic: one swept, compensated move relative to the body's facing, after the delay (default 5 s) in gameplay. **Use it with `body.roomscale 0`**, or the follower walks the body straight back under the head |
| `body.turn <degrees> [delayS]` | The same for a turn |
| `body.report` | Also reports the room-scale counters below |

### Reading `body.report` (room-scale)

| Field | Good | Otherwise |
|---|---|---|
| `roomAvailable` | 1 | 0: the log names the byte mismatch (`detail=room_scale_...`) |
| `moved`, `movedM` | Climb while walking | Flat: read the refusals below |
| `viewDriftMaxMm`, `viewDrift` | Under 5, and 0 | The entity is not where `SetPos` left it by the time of the view, so the view judders |
| `nudgeDriftMaxMm` | Under 5, standing still with the stick idle | The position snaps back after a move |
| `sweepRefused` | 0 | The raycast refused: wrong thread, or out of gameplay |
| `positionOff` | 0 | Positional head tracking is not in the view, so room-scale cannot work |
| `blocked` | Climbs only against walls and furniture | Climbing in open space: the radius is too large, or the rays hit something unseen |
| `residualMm` | Under 30 while standing in the open | Large while blocked |
| `headOut`, `headOutEvents`, `headOutFrames`, `headHits` | Events only when the head goes into geometry | Events in the open: a false fade |
| `pendingTurnDeg`, `pendingNudge` | 0 once run | A request still waiting for gameplay |
| `jumps` | 0, or one per tracking-space change | |

### Next live checks

1. **Feel:** walk about and lean. The body should stay under you with no visible lurch. If it lags
   or swims, try `body.zone 30 5 25` (or `0` for no glide) and say which reads better.
2. **Walls:** lean or walk into a wall. The view should go black as the head enters it, come back
   when you step back, and the trigger should do nothing while black.
3. **Tables and low furniture:** lean over a desk. A fade there is a false positive: the head is
   above the top but more than 30 cm from a body the desk stopped. Report it if it happens.
4. **Stick walking and turning:** walk and turn with the sticks and your head. Nothing should
   stall, and `nativeMoving` should climb while the stick walks. (The multi-frame snap-back check
   passed in the second stage.)

## Implementation order

1. **A pure module** with tests:
   - the deadzone and speed state machine;
   - the sweep clamp;
   - the compensation arithmetic;
   - invariants: the view is unchanged by a follower move; stick motion is never absorbed; a
     blocked move leaves a residual; a tracking jump is refused.
2. **The lane,** in `BodyFollow`:
   - `GetPos`/`SetPos`, hash-gated like the setter;
   - the `QuerySegment` sweep;
   - an origin-shift sibling of `TurnHeadTrackingReferenceWithNative`;
   - commands: `body.roomscale`, `body.nudge`, `body.zone`;
   - counters: moved, blocked, residual, and the entity-position readback at `UpdateView`.
3. **Seam check** (`body.nudge`), then a wearer test.
4. **The head-in-wall layer:** the fade, and line of sight from the native eye.
5. **A VR option for room-scale.** COMFORT and SETUP are full; ABILITIES has the one free row, or
   the options panel gains a page.
