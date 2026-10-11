# The body does not follow the player across the room — investigation, 11 October 2026

Branch `playability-improvements`. Investigation and plan only; nothing here is implemented.
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
   - **Deadzone:** idle until `|offset|` exceeds 12 cm, then follow until it is under 3 cm.
     Leaning over a table does not step the feet.
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
- **Refuse what would cheat.** While the head is out, refuse shots and interaction from beyond
  the wall: the ShotRay, grenade, wrist and interaction checks start from the native eye instead
  of the head.

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
