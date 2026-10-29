# Flashlight follows the head: cause and implementation scope (2026-10-29)

Branch `custom`. TODO: "Flashlight should be aimable". The wearer wants to aim it with the head, like a
head lamp. Found statically with Ghidra over the Steam DLL (SHA `7d6e322f…11a7`) and the `ik.skel` dump in
run `player-20261025-125133`. Addresses are RVAs.

**Headset, 2026-10-29 (PreyVR-102): confirmed working by the wearer.** The run log
(`player-20261029-191912`) gave these readings:
- **The attachment pass came before the frame's view every time:** 4706 placements, all `deferred` and all put
  on the view before the render (`late`); none `placed` at the pass itself.
- **No fallbacks or faults:** `native`, `unread`, `offThread` and `unplaced` were all 0.
- **Prey's mounts on the eye, unrotated:** the flashlight 17.3 mm behind and 34.8 mm below it, the psychoscope's
  light 17.3 mm behind and 126.5 mm below.

**Built 2026-10-29 as PreyVR-102.** The wearer's decisions: both lights follow the head,
always on (no option), and Prey's mount is kept. It follows route A, with one change to the plan
([as built](#as-built)):
- `src/dll/HeadLamp.cpp`: the detour, the filter, and the placement before the render.
- `include/preyvr/HeadLamp.h`: the math.
- `include/preyvr/HeadLampNative.h`: the three body contracts.
- `HeadTrackingHook`'s `LastAppliedView`: the view each frame was given.
- `flashlight.report`, also on the status line.
- `tools/re/verify_head_lamp.py`: 44 checks.
- The `head_lamp` test.

## Why the light ignores the head

**The flashlight is an `ArkLight` entity bound to the `flashlight` bone attachment on `camera_jnt` (98),
Prey's eye joint. In VR nothing moves that joint.** The view seam replaces Prey's view after Prey has
computed it (`HeadTrackingHook.cpp`, `UpdateViewObserved`). The drawn pose keeps Prey's eye joint, because
the body pin poses everything but `camera_jnt`'s subtree
([BODY-FOLLOW.md](BODY-FOLLOW.md), "Do not: write the camera joint"). So the beam starts at Prey's eye and
points the way Prey's eye looks: the body's yaw G and Prey's own pitch, which VR never changes. That is level
and along the torso, whatever the head does.

The dump lists five attachments on joint 98: `#camera`, `vr_headset`, `flashlight`, `helmet` and
`psychoscope_light`.

## The chain

| Step | Where | What |
|---|---|---|
| Component | ArkPlayer `+0x11C0` | Prey's flashlight: `enabled` `+0x88`, `flashlightOn` `+0x8A`, charge `+0x70..+0x7C`. Loaded from the player's `Flashlight` XML node (`0x158AC10`), whose `playerLight` child is a light object at `+0x10`. |
| Update | `0x158ADD0`, called from ArkPlayer::Update (`0x1584FE0`, `LEA RCX,[RDI+0x11C0]` at `0x1585115`) | Battery drain and recharge, then the light's intensity through `0x1814737B0` on the `ArkLight` that `0x13C4BB0` finds by the entity id at **`+0x34`**. The update never touches the light's position. |
| Toggle | `0x158AD80` → `0x1264DC0` → `0x1264E30` → `0x1264920` | Four priority slots of player lights. The highest that wants to be visible is shown. The light object's `+0x18` (`0x1264960`) hides or shows the entity (vt `+0x1A8`). |
| Spawn | light object `+0x10` (`0x12649C0`) | Spawns the `ArkLight` entity once, keeps its id at light `+0x24` (component `+0x34`), then binds it (light `+0x08`). |
| Bind | `0x12647A0` | Player entity (`+0x20`) → character slot 0 (vt `+0x2D8`) → attachment manager (vt `+0x40`) → attachment by the name at light `+0x30` (vt `+0x38`). A new `CEntityAttachment` (vtable **`0x1CB13B0`**, entity id `+0x08`, scale `+0x0C..+0x14`) goes on it by AddBinding (vt `+0xD8`). |
| Every frame | bone execute `0x7A3390`, from manager `0x8297E0` / `0x829940`, from the animation finish `0x8387F0` (called by ProcessAnimations `0x839500`, and by `0x838A40` and `0x839A70`) | Model transform `+0x130` (q) / `+0x140` (t) = pose joint × relative default `+0xF8`, with orientation × K `+0x14C`. Then it **calls the binding's `ProcessAttachment` (vt `+0x10`)**. |
| **Placement** | **`CEntityAttachment::ProcessAttachment`, `0x335010`** (232 bytes) | Reads the attachment's world transform (vt `+0x70`, `0x7A2250` = character location `+0xA90` QuatTS × model `+0x130`). Calls `SetPosRotScale(t, q, m_scale·s, 0x10)` (entity vt `+0x138`) on the entity `gEnv->pEntitySystem` (`0x224DA38`, vt `+0x70`) returns for id `+0x08`. **This is the only place the light gets its transform.** |

The bone-attachment layout (vtable `0x1D212B8`, manager `+0x28` → character `+0x18`, `+0xF8`, `+0x130`,
`+0x14C`, joint `+0x15C`) is from H-005B
([RE-H005B-MODEL-FRAME-ARM-CHAIN-2026-09-05.md](RE-H005B-MODEL-FRAME-ARM-CHAIN-2026-09-05.md#3-attachment-persistence-and-current-pose-compensation)).
The name at `+0x10` and the attachment list at character `+0x38` are what `ArmsLane.cpp`'s
`AttachmentJoint` already reads.

## Routes

| Route | Verdict |
|---|---|
| **A. Detour `0x335010`.** For the player's `flashlight` (and `psychoscope_light`) attachment, place the light at the VR eye instead. | **Recommended.** It is the one producer of the light's transform. It touches only the light entities, and no other attachment, joint or the view. |
| B. Write `camera_jnt` in the IK pass, like the body pin. | No. It also moves `#camera`, `vr_headset` and the `helmet` (the HUD's sway attachment, `0x1327070`), and BODY-FOLLOW's rule forbids it. |
| C. Give Prey's camera the head pitch (`m_rotation`). | No. It pitches Prey's look pose (torso and arms), and it still has no head yaw, only the body's. |
| D. Re-place the light entity after the view. | Depends on frame order. If the attachment pass ran after the view, Prey would overwrite it. Only worth keeping as the snap-frame fix (below). |
| E. Write the attachment's relative default each frame. | No. Reprojection and the redirected update regenerate it (H-005B: "not a dependable controller surface"). |

## The planned design

The plan as recommended before the build. [As built](#as-built) replaces its VR eye and its timing, and there
is no VR Option.

**The light keeps Prey's mount on the eye, carried over to the VR eye.**

- Prey's eye is the `#camera` attachment: `C = L ∘ Mc`, where L is the character location (`+0xA90`) and Mc
  is `#camera`'s model transform.
- The light natively: `F = L ∘ Mf`.
- Both ride joint 98, so the mount `O = Mc⁻¹ ∘ Mf` is constant. Take it from the two relative defaults and Ks
  so it does not depend on the frame's pose, and cross-check it once against `+0x130`.
- **The VR eye V is the view hook's own composition with C in place of the view Prey built.** The yaw is
  `P = yaw(C) − room.yaw`, the orientation is `EyePoseInWorld`, and the position is
  `C.t + Rz(P)·(head − room.origin)`. Factor this out of `UpdateViewObserved` into one function both call.
  No second copy of the view math.
- **The lamp is `V ∘ O`**, written by the same `SetPosRotScale(t, q, m_scale·s, 0x10)` the original makes.

**Invariant for the tests:** with the head at the reference pose, V = C, so the lamp equals Prey's own
placement exactly.

**The filter** runs on every entity attachment, so it stays cheap:
1. The attachment's vtable is the bone one (`0x1D212B8`).
2. Its character (`+0x28` → `+0x18`) is the player's (the IK owner, `AnimIkOwnerCharacter`).
3. Its name is `flashlight` or `psychoscope_light`.

Anything else, or a frame where VR does not drive the view, goes to the original.

**Timing.** The attachment pass is inside the animation finish, which BODY-FOLLOW measured on the main thread,
before the view and the render. The head pose is latest-wins, and L and C are this frame's, so the lamp is the
frame's head with no added lag.

Files, following the keypad and psychoscope pattern:
- `src/dll/HeadLamp.cpp`: the detour and the filter.
- `include/preyvr/HeadLamp.h`: the math.
- `include/preyvr/HeadLampNative.h`: the `0x335010` body contract.
- `tools/re/verify_head_lamp.py`: byte checks of the chain above.
- A `head_lamp` test.
- `flashlight.report`: frames placed, refused and why, and the mount O.
- One VR Option (below).

The entity lookup is already copied in `CarryLane`, `GrenadeLane` and `InteractionLane`. Share it rather than
add a fourth copy. To test without the headset, the cvar `pl_forceFlashlight 1` forces the flashlight on.

## As built

**The lamp uses the view the headset was given, not a second composition of it.** FrameSchedule records the
arm IK callback's place in the frame as varying, and the attachment pass follows it. So the attachment pass can
come before the frame's view or after it. A composition made at the attachment pass would be a frame behind on
stick turns and snaps, because the character's location `+0xA90` is from the animation's start. Instead:
- **The view hook publishes each applied view** (`LastAppliedView`): the rotation it wrote, and the head's
  offset from Prey's eye point (zero without positional tracking).
- **The light is placed at `OnEye(view, C.t + offset, s, O)`.** C is Prey's eye found from the light itself
  (`EyeFromLight`), and the mount O is from the two relative defaults and Ks.
- **When the attachment pass comes before the frame's view,** the light keeps Prey's placement for the moment
  (`deferred`). It is put on the view in `BeforeEngineRender`, before Prey's render (`late`).
- **A view is "this frame's"** while `AfterEngineRender` has not yet passed it.
- **No view that frame** (VR not driving it): Prey's placement stays (`native`).

This also closes the snap-frame question below: the light gets the frame's own view either way.

**The filter:**
1. The bone vtable.
2. The IK owner's character.
3. The name, with its terminator, so `flashlight` matches and `flashlight_fx` does not.

Anything else, or a frame off the main thread, goes to Prey's placement. The entity lookup is now one function,
`EngineAccess.h`'s `EntityById`, shared with CarryLane, GrenadeLane and InteractionLane. `ArmsLane`'s
`FindAttachment` gives the attachment by name.

The gate is the three bodies plus the two vtable slots that reach them: `0x1CB13B0+0x10` must be the placement
and `0x1D212B8+0x70` the world placement. `PREYVR_HEAD_LAMP=0` at launch leaves it unhooked.

## Open until the headset or a run

- **Snap frames.** Closed by design ([as built](#as-built)): the light is put on the frame's own view. Look for
  a one-frame jump of the beam on a snap anyway.
- **The mount O.** The first placement logs it (`preyvr_head_lamp mount light=flashlight mm=x,y,z deg=a`). The
  report carries it as `flashlightMountMm`. Kept as Prey's, by the wearer's choice.
- **Order in the frame.** Answered by PreyVR-102: the attachment pass came before the view in every frame, so the
  placement before the render did all the work. Both orders stay handled.
- **Shadows.** A light at the head lights and shadows the drawn body and hands from above. That is natural for
  a head lamp, but nobody has seen it yet.
- **The psychoscope's light** gets the same treatment. Nobody has checked what it lights.
- **Examination (keypads) and cutscenes.** With Prey's focus camera, C is still the body's eye, so the lamp
  stays on the body's head. Cutscenes fall back to native whenever VR is not driving the view.

## Decisions for the wearer (answered 2026-10-29)

1. **Both lights, or the flashlight alone?** Both.
2. **An option, or always?** Always. There is no VR Option; `PREYVR_HEAD_LAMP=0` at launch is the only way back
   to Prey's.
3. **Keep Prey's mount offset, or put the light on the eye?** Keep it.

A hand-mounted light later would use the same detour, with the off-hand's pose in place of V.
