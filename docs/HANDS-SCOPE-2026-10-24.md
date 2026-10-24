# Hands batch: scope

Status: **built 2026-10-24 as PreyVR-65 (phases 1-2) and PreyVR-66 (phase 3), and worn OK on both (10-24); not committed.** The one issue seen, the fade when leaning over a desk, is the known 30 cm residual limit (fix 3 in [TURN-LEAN-ARMS-2026-10-19.md](TURN-LEAN-ARMS-2026-10-19.md)); it predates the batch and is left for later. What was built, and where it differs from the plan, is in [Done](#done-2026-10-24) at the end. This is the second T2 batch of the [refactor audit](REFACTOR-AUDIT-2026-10-22.md), after [FrameSchedule](FRAME-SCHEDULE-SCOPE-2026-10-24.md). It covers S2 ("no single *where is my hand in the world* abstraction"), the hand parts of S5 (the AnimIk, AimTakeover and BodyFollow god files), the `nativeEye` rename from S6, and your 10-23 decision on the torso settings (option a). The tables below describe the code before the change.

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

## Decisions
1. **Are the splits (phase 3) part of this batch?** **Yes** (your decision, 10-24), as a second package.
2. **Rename `nativeEye` to `reticleOrigin` now?** **Yes** (your decision, 10-24).
3. **Leave Carry and Grenade for the throwing redo?** **No: convert them too** (your decision, 10-24). Their three calls use HandWorld like the rest.

## Done 2026-10-24

Build passes, 94/94 ctest pass (`hand_world` is new), no new warnings, and the DLL's 75 export names are unchanged. **Worn OK on PreyVR-65 and PreyVR-66 (10-24).** Not yet committed.

**PreyVR-65: phases 1 and 2.**
- **`src/dll/HandWorld.h`:** `Anchor`, `AnchorPoint`, `ControllerPose`, `Placement` and `HandWorld`, as designed.
  - Every `ControllerWorldFromHead` call in the DLL goes through it: 18 calls in 9 files, Carry and Grenade included. Only PsiTargeting.h's call, in a pure header, stays.
  - The two reverse-map sites call `AnchorPoint`.
  - Seven files no longer include preyvr/AnimIk.h, which HandWorld.h now brings in.
- **`hand_world` test:** 24 compositions (2 hands × 2 poses × 3 anchors × 2 placements), each bit-identical to the hand-written composition. It also checks the refusal without a camera, which leaves the output untouched, and the camera-else-reticle fallback.
- **AnimIk:** `IkAnchor()` replaces `ArmAnchor` and the four inline copies. `DriveHand` chooses the anchor once and counts it as before.
- **The rename:** `GameplayPoseFrame::nativeEye` is now `reticleOrigin`, 26 uses in 7 files. Report keys such as `nativeEyeMm` and the `AimNativeEyeMillimetres` getter keep their names.
- **`DriveHand`** takes `OwnedPose` and `PrimaryHand`; its body is unchanged.
- **The torso, option (a):**
  - BodyFollow owns the settings; `BodyTorsoSettings`, `SetTorsoDeadzone` and `TorsoDeadzoneDegrees` live there.
  - AnimIk's fallback filter and BodyFollow's own filter read the same conversion.
  - `arm.report` prints BodyFollow's values, and TUNING-COMMANDS.md says there is one copy.
- **`command_channel` test:** it now pins the one-owner behaviour. `body.torso` and `arm.torso` set what the option reads back, the option keeps the relaxation, and out-of-range values are refused.

**PreyVR-66: phase 3, on top of 65.** Each split was checked line by line against the phase 1-2 file.

| Before | After |
|---|---|
| AimTakeover.cpp (606 lines) | AimTakeover.cpp (348): the hook, its counters, the aim override. GameplayFrame.cpp (203): the frame build, the head-yaw hold, the publish, `TryGetGameplayPoseFrame`, the body-yaw setting. TwoHandHold.cpp (134): `SolveTwoHandedFrame`, its state and setters. |
| BodyFollow.cpp (1,313) | BodyFollow.cpp (498): the hook and gates, the torso, turning, the tick. RoomScaleMove.cpp (489): room-scale moves, the living body's calls, the head-in-wall check, the room-scale settings. BodyFollowDiagnostics.cpp (135): the frame-order probes, `body.report`. BodyFollowState.h (301): the shared constants, state and helpers. |
| AnimIkTakeover.cpp (2,793) | AnimIkTakeover.cpp (620): the hook, install, setters and getters. AnimIkRig.cpp (371). AnimIkHand.cpp (519). AnimIkArms.cpp (278). AnimIkReports.cpp (647). AnimIkState.h (641): the shared constants, types and state. |

- **The detour.** AimTakeover's detour is now the list the scope described: undo the origin edit, call the original, `BuildGameplayFrame`, `SolveTwoHandedFrame`, prepare, `PublishGameplayFrame`, tick the lanes, `OverrideAimRay`.
  - The aim replay compiles all three files in one translation unit, so they keep distinct file-local names.
- **What the line checks show:**
  - **BodyFollow:** only added declarations, and `inline` on the moved state.
  - **AnimIk:**
    - The added declarations and `inline` on the moved state.
    - One default argument (`PropagateFingers`'s `side = 1`) moved to its declaration.
    - The second anonymous namespace's closing brace.
  - **AimTakeover:** the detour's restructuring.
    - The frame build returns `FrameBuild::badNativeRay`/`notPlaceable`, and the detour counts the refusal as before.
    - The override reads the player from `frame.player`, the same value.
    - `SetAimBodyYaw` logs through `lifecycle::Log` with the same prefix.
- **The shared state headers** (`AnimIkState.h` in `preyvr::dll::ik`, `BodyFollowState.h` in `preyvr::dll::body`) hold the moved variables as `inline`. Each is included only by its own files. A function another file calls is declared there; every other function stays file-local.

**Where this differs from the plan:**
- **Carry and Grenade were converted too** (your decision 3).
- **BodyFollow's living-body calls went to RoomScaleMove.cpp, not PhysicsCalls.h.** Room-scale is their only user, and Contact's `ReadFooting` reads the same body in one `__try`, so the two do not share code. PhysicsCalls.h keeps to the calls the lanes share.
- **Contact's `HandProxy` keeps its own offset addition.** It checks the controller's position is finite *before* adding the surface offset, so it uses `Placement::controller` and adds the offset as before. That keeps the order of the check and the add.
- **Two switches are read once where they were read twice or with another ordering.**
  - DriveHand reads `ik.anchor` once per hand (it read it twice), so the aim and the grip can no longer use different anchors in one hand.
  - The aim override reads the body-yaw switch through `AimBodyYawEnabled()`, a relaxed load where it was an acquire. On x86 the instruction is the same.
- **Noticed while renaming, not changed:** the arm lift (`PlanTorsoLift`) takes the game's eye height from `reticleOrigin`, the cached reticle-ray origin. With the new name that reads as what it is. The reticle origin sits a few millimetres from the eye, so the lift is barely affected; using the camera's own height instead would be a behaviour change for a later batch.

**Headset smoke:** the lists under Verification above. PreyVR-65 needs package 1's list, including changing BODY TURN DEADZONE in the options; PreyVR-66 adds package 2's (room-scale, head in a wall, `ik.skel`/`pose.marks`).
