# Hands batch: scope

Status: **scoped 2026-10-24, not started.** This is the second T2 batch of the [refactor audit](REFACTOR-AUDIT-2026-10-22.md), after [FrameSchedule](FRAME-SCHEDULE-SCOPE-2026-10-24.md). It covers S2 ("no single *where is my hand in the world* abstraction"), the hand parts of S5 (the AnimIk, AimTakeover and BodyFollow god files), the `nativeEye` rename from S6, and your 10-23 decision on the torso settings (option a). Your decisions are at the end.

## What is there today

### Placing a controller in the world: 19 calls, 4 anchor policies, nothing names them
Every site calls `animik::ControllerWorldFromHead(frame.yaw, anchor, frame.tracking.head, pose)` and picks its own anchor. Some then add the surface stop's offset, to get where the hand is *drawn*.

| Site | Hand, pose | Anchor | Drawn (adds `SurfaceOffset`) |
|---|---|---|---|
| AimTakeover.cpp:100-104 `SolveTwoHandedFrame` | right aim, right grip, left grip | camera, else reticle | no |
| AimTakeover.cpp:330, :360 (aim sample, `aim.origin`) | right aim, right grip | reticle | no |
| AnimIkTakeover.cpp:1109-1128 `DriveHand` | the driven hand's grip | `ik.anchor` (camera, refused if unread; or reticle), with counters | yes |
| AnimIkTakeover.cpp:1217-1221 | right aim | `ik.anchor`, camera else reticle | no |
| AnimIkTakeover.cpp:1266 (free hand) | the driven hand's grip | `ik.anchor` | adds it by hand at :1269 |
| ShotRay.cpp:199, :207 `AimWorld`/`GripWorld` | right aim, right grip | camera | yes |
| ContactLane.cpp:794-800 `HandProxy` | either grip | camera (the lane's gate checked it) | yes |
| SurfaceStopLane.cpp:293 `BuildHand` | either grip | camera (gate checked) | no: it computes the offset |
| DebugOverlay.cpp:690-691 (pointer ray) | left aim | camera, else reticle | no |
| WeaponAttachment.cpp:119-120 (muzzle sample) | right aim, right grip | reticle | no |
| CarryLane.cpp:422, :427 | left grip, left aim | camera | no |
| GrenadeLane.cpp:446-448 `RightGrip` | right grip | camera | yes |
| include/preyvr/PsiTargeting.h:21 | a pose passed in | camera, passed in | no |

**The anchor policies:**
- **camera:** `cameraCentre`, the view camera's centre. The default since the IK crosstalk fix.
- **camera, else reticle:** `cameraCentreValid ? cameraCentre : nativeEye`.
- **reticle:** `nativeEye`. That field is the engine's cached *reticle-ray origin*, not an eye; GameplayFrame.h calls it a "historical name".
- **`ik.anchor`:** AnimIk's A/B switch between camera and reticle.

**The same choice made in reverse (tracking ← engine):**
- DebugOverlay.cpp:716 (foregrip) uses camera, else reticle.
- ReticleFollow.cpp:489 uses camera-else-reticle when the sample starts at the muzzle, and reticle otherwise.
- AnimIk's `XrMap` and `CaptureMarks` use the `ik.anchor` policy.

**Inside AnimIk, the `ik.anchor` choice is written 5 times:**
- `ArmAnchor` (:782);
- inline at :1109, :1217, :1372 (the trace) and :1690.

All five give the same anchor wherever the camera has been checked first.

### DriveHand: 430 lines, 15 parameters
`DriveHand` (AnimIkTakeover.cpp:1035) takes the hand, the two pose arrays, the joint count, the character's location, the frame, the owner and the pose settings. It then takes **7 optional out-pointers**, which hand 0 and hand 1 use differently (:1969-1978):
- hand 0 writes the primary goal and rotation;
- hand 1 reads them, to stay on the weapon.

### The torso settings are stored twice
| Store | Who writes it | Who reads it |
|---|---|---|
| BodyFollow `gDeadzoneMilliDeg`/`gRelaxPerMille` (BodyFollow.cpp:127) | `body.torso`, and `arm.torso` | the body's torso filter, which the arms also use |
| AnimIk `PoseSettings::torsoDeadzone/torsoRelax` (AnimIkTakeover.cpp:478) | `arm.torso` and the Body Turn Deadzone option (through `SetTorsoDeadzone`) | the arms' **fallback** filter (:855), used only while BodyFollow has no fresh torso |

VR start always activates BodyFollow (VrMode.cpp:238), so in play the fallback runs only if that lane failed to install.

### The god files
| File | Lines | What it holds |
|---|---|---|
| AnimIkTakeover.cpp | 2,792 | The hook (:1806), rig reading and identification (:197-672), `DriveHand` and the fingers (:1035-1556), the arms and torso lift (:691-889, :1557-1686), the trace, marks and dumps (:891-1033, :1687-1805, :2222-2690), and about 75 setters and getters. **No offline test.** |
| BodyFollow.cpp | 1,299 | The hook and gates, the torso and turning, room-scale moves (:547-750), living-body physics calls (:285-432), and frame-order diagnostics (:1187-1299). **No offline test.** |
| AimTakeover.cpp | 605 | The detour (:165), which builds the gameplay frame, solves the two-hand hold (:96) and overrides the aim ray (:300-405), plus about 35 getters and setters. **The aim replay exercises it.** |

## Goal and non-goals

**Goals:**
- One function that says where a hand is in the world, with the anchor and "drawn or not" visible at the call site.
- `ik.anchor` chosen in one place.
- `DriveHand` taking a struct, not 7 optional pointers.
- One owner for the torso settings.
- The three files split along the seams above.

Everything except the torso change is **behaviour-identical**: the same float operations in the same order, and the same log lines, verbs, replies and export names.

**Not in this batch:**
- **Carry and Grenade.** You are redoing throwing, so their four sites stay as they are, and the redo can use the new function from the start.
- **PsiTargeting.h.** It is a pure header taking plain values, not a frame.
- **Rewriting `DriveHand`'s body.** It has 15 early returns and no offline test. Splitting its branches waits until AnimIk has a test seam, the way the newer lanes have `Natives`/`gResolve`.
- **Changing any anchor.** Each site keeps its policy; the batch only makes it visible. Whether the aim origin should move off the reticle anchor is a separate question.
- **Sharing BodyFollow's living-body reader with Contact's `ReadFooting`.** They fault differently (one `__try` versus four); that belongs to the Physics batch.

## Design

**`src/dll/HandWorld.h`**, header-only so the DLL-source tests keep linking:
```cpp
namespace preyvr::dll {
// The point a head-relative offset is rotated about (GameplayPoseFrame).
enum class Anchor {
    camera,             // the view camera's centre; refused when it was not read
    cameraElseReticle,  // the camera centre, else the cached reticle origin
    reticle,            // the cached reticle-ray origin (the aim origin; ik.anchor 0)
};
bool AnchorPoint(const GameplayPoseFrame& frame, Anchor anchor, Vec3& out);

enum class ControllerPose { grip, aim };
enum class Placement { controller, drawn };   // drawn: plus the surface stop's offset
// Where `hand`'s controller is in engine world this frame. False only when the
// anchor is unavailable: each site keeps its own freshness and finiteness checks,
// because they differ (validity fields, ages, `why` strings).
bool HandWorld(const GameplayPoseFrame& frame, Hand hand, ControllerPose pose,
               Anchor anchor, Placement placement, Pose& out);
}
```
- **Bit-identical by construction.** It is `ControllerWorldFromHead`, then `Add(position, SurfaceOffset(frame, hand))` for `drawn`, which is what each site writes today.
- **A new test, `hand_world`,** checks it bit for bit against those two calls for every anchor and placement, and checks that `camera` refuses a frame without a camera.
- **AnimIk:** one `IkAnchor()` returns `camera` or `reticle` from `ik.anchor`. `DriveHand`, the aim at :1217, the trace, `CaptureMarks` and `ArmAnchor` all ask it. The `gAnchorCamera/Reticle/Missing` counters stay in `DriveHand`, and so do their counts.

**`DriveHand`'s parameters:**
```cpp
struct OwnedPose { std::uint8_t* relative; std::uint8_t* absolute; unsigned count; animik::Location location; };
struct PrimaryHand { Vec3 goal{}; Quaternion rotation{}; bool written = false; };  // hand 0 out, hand 1 in
void DriveHand(unsigned hand, const OwnedPose& pose, const GameplayPoseFrame& frame, const EquippedRig& owner,
               const PoseSettings& settings, PrimaryHand* primaryOut, const PrimaryHand* primaryIn,
               FingerPlan* fingers, ArmPlan* arm);
```
Hand 1 reads the primary only when `written` is set, which is what the two `primaryWritten ? … : nullptr` arguments do today.

**The torso settings, option (a):**
- **BodyFollow owns them.** `BodyTorsoSettings(float& deadzoneRadians, float& relax)` returns them with the conversion BodyFollow's own filter uses.
- **AnimIk stops storing them.** `PoseSettings::torsoDeadzone/torsoRelax` go, and the fallback filter (:855) reads `BodyTorsoSettings`.
- **The setters:**
  - `arm.torso` keeps its range check and its reply, and writes BodyFollow only.
  - `SetTorsoDeadzone` and `TorsoDeadzoneDegrees` (the Body Turn Deadzone option) move to BodyFollow with the same names. The option wiring and its test stub need no change.
- **`arm.report`** keeps its `torsoDeadzoneDeg=` and `torsoRelax=` fields, read from BodyFollow.

## Phases and packages

1. **HandWorld and the anchor** (package 1):
   - `HandWorld.h` and its test.
   - The 15 calls outside Carry, Grenade and PsiTargeting.h.
   - `IkAnchor()`.
   - The two reverse-map sites (DebugOverlay:716, ReticleFollow:489) call `AnchorPoint`.
   - **Rename `nativeEye` to `reticleOrigin`:** 26 uses in 7 files. C++ only; report keys such as `nativeEyeMm` stay.
2. **`DriveHand`'s struct and the torso owner** (package 1). The torso change is the batch's only behaviour change (below).
3. **The splits** (package 2, on top of 1). Moves only, cut at declarations and checked line by line against the old file, as CameraEditHook's split was.
   - **AnimIkTakeover.cpp** becomes:
     - `AnimIkTakeover.cpp`: the hook, its install and the setters and getters;
     - `AnimIkRig.cpp`: raw reads, identification and the hand rig;
     - `AnimIkHand.cpp`: `DriveHand`, the wrist and grip point, and the fingers;
     - `AnimIkArms.cpp`: the shoulders, torso lift and arm solve;
     - `AnimIkReports.cpp`: the trace, marks, dumps and reports.

     The shared state goes in a private `AnimIkState.h` (namespace `preyvr::dll::ik`, not `animik`, which would hide `preyvr::animik`).
   - **AimTakeover.cpp** becomes:
     - `GameplayFrame.cpp`: the frame build, the head-yaw hold and the publish, beside the existing GameplayFrame.h;
     - `TwoHandHold.cpp`: `SolveTwoHandedFrame` and its state;
     - `AimTakeover.cpp`: the detour and the aim override.

     The detour becomes a short list:
     1. undo the last origin edit;
     2. call the original;
     3. build the frame;
     4. solve the two-hand hold;
     5. prepare, publish, then tick the lanes;
     6. override the aim.

     The aim replay compiles all three files.
   - **BodyFollow.cpp** becomes:
     - `BodyFollow.cpp`: the hook, gates, torso and turning;
     - `RoomScaleMove.cpp`: the fit, the moves and the head-in-wall check;
     - `BodyFollowDiagnostics.cpp`: the frame-order probes and the report.

     The living-body calls (`ReadLiving`, `StanceFits`, `SetPositionKeepingMotion`, `ReadDimensions`, `PhysicsIdle`) move into PhysicsCalls.h as `phys::` functions. The shared state goes in a private `BodyFollowState.h`.

**Size:**
- Phases 1-2: about +200/−120 across 12 files.
- Phase 3: about 4,700 lines moved and a few hundred added, mostly includes and the two state headers.

## The one behaviour change (phase 2)
- **`body.torso` now moves the arms' fallback filter too.** Before, only `arm.torso` and the menu option did. In play the fallback does not run (above), so this is visible only if BodyFollow is unavailable.
- **The fallback's deadzone in radians may differ in the last bit.** It is converted the way BodyFollow converts it (`milli-degrees × 1e-3 × kRadiansPerDegree`), not AnimIk's way (`degrees × 3.14159265 / 180`).
- **The `arm.report` text may print `35` where it printed `35.0000`.**

## Verification
- **Each phase:**
  - build, and the full ctest with `hand_world` added;
  - the four VR scheme replays, since aim_review covers AimTakeover's sites and its split;
  - `command_channel`, which pins the `arm.*`, `ik.*`, `body.*` and `aim.*` replies;
  - the export table unchanged (75 names).
- **Phase 3:** a line-multiset comparison of each old file against its new files; only includes, namespace braces and the declared cross-file calls may differ.
- **Headset smoke, package 1:**
  - both hands on their controllers, holstered and drawn;
  - the free hands' fingers;
  - a pistol, then a two-handed weapon (foregrip latch, then a long weapon's hold-back and roll);
  - the wrench and surface stop: hand against a wall, pushing a box, a strike;
  - the aim laser and reticle, the shot origin, and the pointer ray;
  - **turning:** look away past the deadzone, then change BODY TURN DEADZONE in the options and check that body and arms still turn together;
  - snap turn and recenter.
- **Headset smoke, package 2:** the same, plus:
  - room-scale: step around, lean over a desk, walk with the stick;
  - head in a wall (the fade);
  - `ik.skel`/`pose.marks` still dump.

## Risks
| Risk | Guard |
|---|---|
| A site gets a different anchor | Each site's policy is in the table above and passed explicitly. A site whose lane gate already checked the camera gets `camera`, whose refusal cannot run. |
| A drawn/undrawn mix-up moves a hand by its surface offset | Only the 4 sites marked "yes" (and AnimIk's :1269 by hand) pass `drawn`. The test pins both placements. |
| AnimIk has no offline test | Phases 1-2 only replace expressions with calls that compute the same floats, and phase 3 is a checked move. A real AnimIk test seam is a later item. |
| Hidden ordering in the AimTakeover detour | The new list keeps the order the code has now; aim_review checks the published ray, the reticle context and the refusals. |
| Static state shared across new files | The state headers hold the moved variables as `inline` in a named namespace, so there is still one instance of each. |

## Decisions for you
1. **Are the splits (phase 3) part of this batch?** I'd recommend yes, as a second package after 1-2, like FrameSchedule's split. They are pure moves, but AnimIk and BodyFollow have no offline test, so they need their own smoke run.
2. **Rename `nativeEye` to `reticleOrigin` now?** I'd recommend yes, in phase 1: with `Anchor::reticle` at every call site, the old name would contradict the code around it.
3. **Leave Carry and Grenade for the throwing redo?** I'd recommend yes. Converting their four sites is mechanical, but it would touch code you plan to replace.
