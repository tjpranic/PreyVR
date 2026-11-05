# Time stop: hands and body freeze (2026-11-05)

Branch `custom`. TODO: "Hands don't move when aiming a Typhon ability. Or when opening the radial menu. Can't see
the wrist status HUD when using the radial menu either." Found statically, with headless Ghidra and byte reads over
the Steam DLL (SHA `7d6e322f…11a7`). **Option A built as PreyVR-134. Headset, 2026-11-05: confirmed working by
the wearer.** Run `player-20261105-192320`: 13 stops from 0.3 s to 15 s, 2206 stopped frames, every one started
and matched by the IK (`started=2206`, `ikMatched` equal to `frames` in each `stop_end`), with no `declined`,
`onTransform`, `others` or `faulted`. So the player's frame time was 0 in each stop, as found below, and `+0x844` is
0 for the player.

## The defect

While the Favorites Wheel is open, or while a psi power is being aimed, the hands, arms and body stop following
the controllers and the headset, and the wrist status card does not appear.

## Why

**Prey stops the player's clock, and the player is the one character whose animation is then no longer started.**
The mod drives the arms from inside Prey's animation job (`ProcessAnimationDrivenIK`), so no job means no IK.

1. **The wheel and psi targeting ease time through one request stack** (ArkGame `+0x3C0`; push `0x1285410`, pop
   `0x1285390`). Each request has a mask (2 game, 8 player) and a scale; each timer gets the minimum of its live
   requests. A small eased helper (`0x11DE670` set, `0x11DE6E0` step) re-pushes its current value as it moves.
   - **Wheel open** (`0x1383AF0`): player time (mask 8) eases to `FocusMenuModeTimeScale` over
     `…TransitionTimeIn`. Focus exit (`0x124DF60`) eases it back to 1 over `…TransitionTimeOut`.
   - **Psi targeting** (inference from behaviour and the stats read): start (`0x15BAA60`) eases game and player time
     to **0** over `PsiTargetingTimeScaleTransitionTimeIn`; the update (`0x15BC840`) eases them to
     `PsiTargetingMin/Look/MoveTimeScale` by input; the end (`0x15B65A0`) eases them back to 1.
   - The stats' names come from the registration table at `0x11C89A0` (globals `0x2BD6C80`–`C90`,
     `0x2BD6E08`–`E28`). Their values are in the encrypted paks.
2. **`CTimer::SetTimeScale(timer, scale, source)`** (`0xE14BC0`, vtable `0x1DA6D08` `+0x50`). A timer's scale is the
   product of its sources. **Game = min(game, player).** Any positive scale is raised to at least **0.3**
   (`maxss` at `0xE14BE6`), so a slowdown is 0.3 or more and a stop is exactly 0. A zero on the game channel also
   pauses that timer (`0x1285630` -> vtable `+0x80`, `0xE149E0`).
3. **Timers: 1 game, 2 UI, 3 player.** The cvars sit at `0x18 + 0x1C0 * timer` (`t_GameTimeScale` `+0x1D8`,
   `t_UITimeScale` `+0x398`, `t_PlayerScale` `+0x558`). `GetFrameTime(timer)` (`+0x38`, `0xE14650`) returns 0 for
   a paused timer, else the scaled frame time.
4. **The player runs on timer 3.** `EntitySystem::PrePhysicsUpdate` (`0x923BF0`) and
   `CAnimatedCharacter::UpdateTime` (`0x4FDD10`) both take `GetFrameTime(isPlayer ? 3 : 1)`, through the entity
   system's `+0x338`. ArkPlayer's setup (`0x157EDE0`) also puts character slot 0 on timer 3 (setter `0x82EC90`,
   character vtable `+0x1C0`, field `+0xAE0`). The IK's rig is that character, with or without a weapon.
5. **Every other character gets a substitute.** `UpdateTime` gives any non-player whose frame time is 0 or less a
   frame time of 0.0001 s. `test bl,bl; jnz` at `0x4FDD68` skips that for the player, who keeps exactly 0 in
   `+0x1D0`.
6. **`CAnimatedCharacter::PrePhysicsUpdate`** (`0x4F8570`) then returns at once: `comisd 0,[rcx+0x1D0];
   jnc 0x4F8758` (the epilogue). That skips `PrepareAndStartAnimProc` (`0x4F9A80`, called at `0x4F8743`), so
   `StartAnimationProcessing` (`0x82F910`) and the command buffer never run, and neither do
   `ProcessAnimationDrivenIK` (`0x877B50`) and the mod's detour on it.

The rig keeps its last skinned pose. Everything the IK callback publishes stops with it. The wrist card is placed
from the arm frames it publishes (`PublishWristArm`, `AnimIkReports.cpp`), which is why the wrist HUD is missing.

**Ruled out:**
- **The mod's timing.** The arm, torso and room-scale code time themselves on the monotonic clock.
- **The aim seam.** `CArkUIHUD::OnPreRender` (`0x1667440`) calls `UpdateCachedReticleViewPosAndDir` whenever there
  is a local player, with no focus gate.
- **The animation pass itself.** `ProcessAnimations` (`0x839500`) and the command build (`0x8384B0`) do not test the
  delta: at 0 the layers hold still and the job still runs.
- **The entity system.** Its `PrePhysicsUpdate` visits every entity, with no time gate.
- **`ArkPlayer::Update`** (`0x15806E0`). It has no early-out at zero player time, so the torso lane still runs.
  Only its effect on the drawn body is lost.
- **`ArkPlayerInput::SetTimeScale`** (`0x1591340`). That is the debug increase, decrease and normal time dilation.

## Fix options

| Option | What | Effect | Risk |
| --- | --- | --- | --- |
| **A. Start the animation job only** (chosen, built) | Detour `PrePhysicsUpdate` `0x4F8570`. After the original, if `this` is the player's AnimatedCharacter (`player + ArkPlayerLayout::animatedCharacter`), `+0x1D0 <= 0` and `+0x844 == 0`, call `PrepareAndStartAnimProc(this, this + 0x21C)`. | The job runs with the rig's own delta of 0: Prey's authored pose holds still while the IK, body pin and wrist card move. The movement and physics requests (`0x4FCD90`, `0x4FD910`) stay skipped, as Prey intends. | Small. The call is the engine's own, made at the engine's own point in the frame, and it guards itself: once per frame (`+0x840`) and only while `+0x847` is set. The location at `+0x21C` must still be right after a snap turn during the stop: XFORM events rewrite it (`0x4F87A0`, case 0). Check this live. |
| B. Give the player the substitute | Remove the player exception in `UpdateTime` (`jnz` at `0x4FDD6A`). | All of `PrePhysicsUpdate` runs at 0.0001 s, as it does for every NPC. | Also runs the player's movement requests at 0.1 ms. A position change made during the stop (room-scale, a snap turn) could become a very large velocity request. |
| C. Keep player time running | In the stack, send the wheel's and psi targeting's player-time requests (mask 8) to game time (mask 2). | The world stops, and the player's clock runs at 1. | Changes play: the player's animations, and anything else on player time, run while the world is stopped. Not Prey's design. |

A could also be called from the existing `ArkPlayer::Update` seam, with no new hook, but the job would then start
later in the frame than Prey starts it.

## The `report` test, 2026-11-05: no answer

The wearer ran `report` before the wheel and with it open. Between the two, Prey drew 1608 frames, but the OpenXR
session was not focused for 1355 of them (`xrNotFocused` 5610 to 6965): writing the command meant leaving the
headset. `ikMatched` counts only frames with fresh tracking, so it stalls whenever the headset is unfocused, wheel
or not. Its +175 cannot tell the two apart. `ikCalls`, every animated character's IK pass, ran at about 1 a frame
against about 2 before. That is a hint, no more, since it moves with the scene. So the fix logs its own evidence
(below), and needs no command while the wheel is open.

## What was built (PreyVR-134)

Option A, in `src/engine/fixes/TimeStopAnimation.cpp`, installed at VR start and passing through after VR stop:

- **The detour on `PrePhysicsUpdate`** reads the frame time first, as the original tests it (0 or less; a NaN is
  not), then runs the original.
- **When the original skipped the start**, the animated character is the player's (`GetArkPlayerInstance` `+0x1808`)
  and `+0x844` is 0, it calls `PrepareAndStartAnimProc(this, this + 0x21C)`. Anything else keeps Prey's behaviour.
- **Gate.** The bodies of `PrePhysicsUpdate`, `PrepareAndStartAnimProc`, `UpdateTime` and `GetArkPlayerInstance`
  (`TimeStopAnimationNative.h`). `tools/re/verify_time_stop.py` keeps every instruction above; `time_stop_animation`
  tests the detour against fake characters.
- **No switch and no option.** It is a fix.

**What a run shows.** Each stop logs two lines:
`preyvr_time_stop detail=stop_begin`, then
`preyvr_time_stop detail=stop_end reason=clock ms=… frames=… started=… declined=… onTransform=… ikMatched=…`.
The minute status line carries `timeStop={active hooks gate stops stoppedFrames started declined onTransform others
unread faulted}`.

| A wheel opening shows | Means |
| --- | --- |
| No `stop_begin` | The player's frame time was not 0: the cut is elsewhere, and this report is wrong. |
| `started` about `frames`, `ikMatched` rising | The fix works. The hands and the wrist card should move. |
| `onTransform` about `frames` | Prey starts this character from its transform event (`+0x844` set): A misses it. |
| `started` high, `ikMatched` 0 | The job runs but the IK does not match: look at the IK's own gate. |

## In the headset

- **The wheel and a psi power.** Hands, arms, body pin and wrist card follow while the world stays stopped.
- **A snap turn with the wheel open.** The body should turn with the view. The start's location (`+0x21C`) is
  rewritten by the transform event, but that has not been seen running.
- **Time resumes.** No jolt, the world no faster than before.

## Not covered

- **Psi targeting while slowed.** At a scale of 0.3 or more the player's frame time is positive, so the hands
  should already move. Only the stop freezes them.
- **Room-scale during the stop.** Physics stops when game time does (inference), so walking won't move the capsule
  until time resumes. That is a separate question.
- **Aiming psi with the left hand.** A separate TODO.
