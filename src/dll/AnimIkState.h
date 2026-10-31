#pragma once

// AnimIkTakeover's private state, shared by its six files:
//  - AnimIkTakeover.cpp: the ProcessAnimationDrivenIK hook, its install, and the
//    setters and getters (AnimIkTakeover.h);
//  - AnimIkRig.cpp: SEH-guarded raw reads and writes, rig identification, and the
//    hand rig;
//  - AnimIkHand.cpp: DriveHand, the wrist and grip point, and the fingers;
//  - AnimIkArms.cpp: the torso the shoulders face, the player's shoulders and
//    the arm solve;
//  - AnimIkBody.cpp: the body pin;
//  - AnimIkReports.cpp: the trace, marks, dumps and reports.
// The constants, types and state, moved from AnimIkTakeover.cpp as they were;
// one instance of each (`inline`). Include it from those six files only.
#include "AnimIkTakeover.h"
#include "SafeMemory.h"
#include "HookInstall.h"
#include "EngineAccess.h"

#include "preyvr/FrameTiming.h"

#include "AimTakeover.h"
#include "HandWorld.h"
#include "ArmsLane.h"
#include "TorsoLane.h"
#include "WeaponAttachment.h"
#include "CarryLane.h"
#include "GrenadeLane.h"
#include "PhysicalInteractions.h"
#include "WristLane.h"
#include <mutex>
#include "HeadTrackingHook.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "XrInput.h"
#include "preyvr/AnimIk.h"
#include "preyvr/ArmPose.h"
#include "preyvr/BodyRig.h"
#include "preyvr/HandPose.h"
#include "preyvr/TwoHandedAim.h"
#include "preyvr/WeaponRigAlignment.h"
#include "preyvr/EngineMap.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/VrMath.h"

#include <MinHook.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <sstream>
#include <atomic>
#include <cmath>
#include <cstring>
#include <span>
#include <string>

namespace preyvr::dll::ik {

inline void Log(const std::string& line) { lifecycle::Log("preyvr_anim_ik " + line); }

// R-102. CSkeletonAnim::ProcessAnimationDrivenIK(CCharInstance*, SAnimationPoseModifierParams*).
using ProcessAdikFn = void(__fastcall*)(void*, void*);

constexpr std::uintptr_t kProcessAdikRva = 0x877B50;

// The gate is the `anim.process_adik` landmark's exact bytes, fail closed.

// Layout, all from decompiled reads (H-021 report sections 1, 2 and 7).
constexpr std::size_t kCharSkeleton = 0x10;     // CCharInstance -> CDefaultSkeleton*

// CCharInstance+0x610 aliases CSkeletonAnim+0x4D0 (the animation object is at
// character+0x140). Its writers (RE-H021-STATIC-VERIFICATION) make it a
// nonempty-command-buffer predicate -- CryEngine's m_IsAnimPlaying -- and the
// ADIK pass tests it. It says nothing about whether the rig has IK targets;
// that is the skeleton's table at +0x70, read separately below.
constexpr std::size_t kCharAnimPlaying = 0x610;

constexpr std::size_t kSkelJoints = 0x08;       // DynArray<joint>, stride 0xA8, name ptr at +0

constexpr std::size_t kJointStride = 0xA8;

constexpr std::size_t kSkelLimbs = 0x68;        // DynArray<IKLimb>, stride 0x30

constexpr std::size_t kLimbStride = 0x30;

constexpr std::size_t kLimbTag = 0x08;

constexpr std::size_t kLimbChain = 0x18;        // -> records of 0x10, int32 index at +0

constexpr std::size_t kSkelAdik = 0x70;         // DynArray<ADIKTarget>, stride 0x28

constexpr std::size_t kAdikStride = 0x28;

constexpr std::size_t kAdikTarget = 0x08;

constexpr std::size_t kAdikTargetName = 0x10;

constexpr std::size_t kAdikWeight = 0x18;

constexpr std::size_t kParamsPose = 0x08;

constexpr std::size_t kParamsLocQ = 0x14;

constexpr std::size_t kParamsLocT = 0x24;

constexpr std::size_t kParamsLocS = 0x30;

constexpr std::size_t kPoseRelative = 0x10;

constexpr std::size_t kPoseAbsolute = 0x18;

constexpr std::size_t kQuatTStride = 0x1C;

constexpr std::uintptr_t kUseAdikCvarRva = 0x225780C;   // DAT_18225780c, tested != 0 by the pass

constexpr unsigned int kMaxJoints = 768;

inline std::atomic<bool> gInstalled{false};

inline std::atomic<ProcessAdikFn> gOriginal{nullptr};

inline void* gTarget = nullptr;

inline std::atomic<unsigned int> gMode{0};

inline std::atomic<int> gTestMm[3]{0, 0, 0};

inline std::atomic<bool> gDrive{false};

inline std::mutex gIkMutex;

// Opt-in until authored-basis alignment has been checked across live assets.
inline bool gAlignWeapon = false; // all alignment state is protected by gIkMutex

inline weaponrig::Status gAlignStatus = weaponrig::Status::disabled;

inline weaponrig::Basis gAlignBasis{};

inline std::uint64_t gAlignGeneration = 0, gAlignSequence = 0, gAlignApplied = 0, gAlignRefused = 0;

inline animik::CalibrationState gCalibration;

inline std::atomic<std::uint64_t> gOwnerGeneration{0}, gOwnerCharacter{0}, gUsedSequence{0};

inline std::atomic<unsigned long long> gNoOwner{0}, gBusy{0};

inline std::atomic<unsigned int> gSignatureJoints{101};

inline std::atomic<unsigned int> gHands{1};

// The free arms: with no weapon drawn (holstered, or carrying something) the game
// lowers the first-person arms out of view; the same rig is still skinned and
// drawn, so it is driven too. Its owner is the player's own character
// (ArkPlayer::GetAnimatedCharacter 0x157C6D0 = [player+0x1808]; the animated
// character caches its ICharacterInstance at +0x710, live == the weapon rig's
// owning character, 2026-10-07).
inline std::atomic<bool> gFreeBound{false};

inline std::atomic<unsigned long long> gFreeFrames{0}, gFreeGateZero{0};

inline std::atomic<int> gFreeFistMilli[2]{0, 0};

inline float gFreeFist[2]{};               // eased, under gIkMutex

inline std::uint64_t gFreeFistNs = 0;

// **The fingers follow the player's** (hands.fingers, VR Options "Fingers
// follow the controller"): each free hand's shape from its controller's trigger,
// grip and touch (handpose::ShapeFromController), eased, per side (1 = left).
// Off, every finger takes the one relaxed pose, as before. Under gIkMutex.
inline std::atomic<unsigned> gFingerCurl{1};

inline handpose::FingerShape gFingerShape[2]{};

inline bool gFingerShapeValid[2]{};

inline std::uint64_t gFreeSinceNs = 0;     // when the free owner bound (fingers ease in), under gIkMutex

constexpr std::uint64_t kFreeGenerationBit = 1ull << 62;

constexpr std::size_t kPlayerAnimatedCharacter = engine::ArkPlayerLayout::animatedCharacter;

constexpr std::size_t kAnimatedCharacterInstance = engine::AnimatedCharacterLayout::characterInstance;

constexpr std::uintptr_t kArmsCharacterVtableRva = engine::CharacterInstanceLayout::vtableRva;

// Not during a cinematic: ArkPlayerInput (player+0x8E0, vtable 0x1E580E0) +0x94,
// the gate Funk's body holsters use (BodyEquipment.cpp).
constexpr std::size_t kPlayerInput = engine::ArkPlayerLayout::input, kInputCinematic = engine::ArkPlayerInputLayout::cinematic;

constexpr std::uintptr_t kPlayerInputVtableRva = engine::ArkPlayerInputLayout::vtableRva;

inline std::atomic<int> gReachPercent{100};

inline std::atomic<unsigned long long> gCalls{0};

inline std::atomic<unsigned long long> gMatched{0};

inline std::atomic<unsigned long long> gRigSkeleton{0};

inline std::atomic<unsigned int> gRigJoints{0};

inline std::atomic<unsigned int> gGate{0};

inline std::atomic<int> gCvar{-1};

inline std::atomic<int> gTargetJoint[2]{-1, -1};

inline std::atomic<int> gWeightJoint[2]{-1, -1};

inline std::atomic<int> gHandJoint[2]{-1, -1};

inline std::atomic<int> gLimbUpper[2]{-1, -1};

inline std::atomic<int> gLimbMid[2]{-1, -1};

inline std::atomic<int> gLimbEnd[2]{-1, -1};

inline std::atomic<unsigned int> gLimbTagValue[2]{0, 0};

inline std::atomic<unsigned long long> gWritten[2]{0, 0};

inline std::atomic<unsigned long long> gNoPose{0};

inline std::atomic<unsigned long long> gClamped{0};

inline std::atomic<bool> gCalibrated[2]{false, false};

inline std::atomic<int> gLocMm[3]{0, 0, 0};

inline std::atomic<int> gLocYawMilli{0};

// **One coherent per-hand record from inside the IK callback.**
//
// Separate latest-value counters cannot reconstruct a transformation chain: by
// the time three of them are read, they may describe three different frames, and
// the whole question here is which term in ONE frame's arithmetic moved. The
// static audit named two candidate paths -- a reticle-derived shared anchor, and
// reach compression retaining animated-shoulder motion -- and no existing counter
// can tell them apart, because `ikGoalMm` publishes only the right hand's final
// model-space goal.
//
// Captured under the lock that already serialises the write, published per hand.
struct IkTraceRecord {
    std::uint64_t stamp = 0;
    std::uint64_t trackingSequence = 0;
    std::uint64_t publishedNs = 0;
    std::uintptr_t owner = 0;
    // The shared anchor the hands were placed with: the camera centre (kIkAnchor
    // below). Recorded so the crosstalk fix stays measured rather than argued.
    Vec3 anchor{};
    float yaw = 0.0f;
    Pose head{}, grip{};
    Vec3 controllerWorld{};
    Vec3 characterLocation{};
    Vec3 shoulderModel{};
    // The three stages the audit asks to see separated. Raw is the controller's
    // goal before compression; scaled is after ScaleReach, which mixes in
    // (1-k) * shoulder; clamped is after the reach sphere.
    Vec3 goalRaw{}, goalScaled{}, goalClamped{};
    float reachLimit = 0.0f;
    unsigned int reachPercent = 0;
    bool clamped = false;
    bool calibrated = false;
    bool wroteRotation = false;
    bool valid = false;
};

inline IkTraceRecord gTrace[2]{};

inline std::atomic<bool> gTraceEnabled{false};

inline std::atomic<int> gLastGoalMm[3]{0, 0, 0};

// --- the write ------------------------------------------------------------------

struct QuatT {
    float q[4];
    float t[3];
};

// --- VR hand pose (pose.*) ------------------------------------------------------
//
// **The hand is where the player's hand is, shaped like a hand that holds
// nothing unless it holds the weapon.** Changes over the original lane, each
// switchable for A/B (pose.mode 0 = all of them off; docs/HAND-POSE-2026-10-06.md):
//
//  1. Orientation of the free (left) hand is ANATOMICAL: the game hand's own
//     frame (from its knuckles) is mapped onto the player's real hand frame,
//     which the OpenXR grip pose defines. The original captured "controller ->
//     animated wrist" at one instant and kept it, so the hand's angle to the
//     controller depended on what the weapon animation was doing then.
//  2. The PALM, not the wrist, goes to the controller: the IK target is the
//     wrist joint, and the grip pose's origin is in the middle of the fist, so
//     the wrist goal is the controller minus the palm offset (both hands).
//  3. The free hand's fingers take one relaxed open pose (handpose::Relaxed*),
//     and while the left grip holds a two-handed weapon the hand takes the
//     NATIVE two-handed hold instead -- the animation's own left hand on the
//     weapon, position and rotation carried rigidly with the right wrist.
//  4. A held weapon keeps its barrel on the aim ray but is ROLLED about it
//     until the hand holding it lies like the real hand (pose.roll): the
//     muzzle helper's authored roll held the Boltcaster upside down.
//  5. Reach: exact up to a knee, then a smooth approach (pose.reach), to an
//     arm the native solve may lengthen by pose.stretch -- instead of the
//     original 65% linear compression, which moved every hand off its
//     controller even at rest.
struct PoseSettings {
    unsigned mode = 1;          // 0 original, 1 hand pose
    float curl = 1.0f;          // relaxed flexion scale
    float tubeLean = 15.0f * 3.14159265f / 180.0f;
    handpose::GripPoint point{};
    unsigned reachMode = 1;     // 0 linear percent (original), 1 exact to the knee then soft
    float knee = 0.85f;
    bool weaponRoll = true;     // roll a held weapon about its barrel to fit the real hand
    float stretch = 1.20f;      // reach limit over the rig's arm length (native solve allows 1.25)
    // The arm (arm.*, docs/ARM-POSE-2026-10-06.md): from the player's shoulder.
    unsigned armMode = 1;       // 0: the native arm, hung from the animated shoulder
    armpose::Body body{};
    // The torso filter's deadzone and relaxation are TorsoLane's
    // (BodyTorsoSettings): one torso for the arms and the body slots.
    float armKnee = 0.92f;      // exact reach up to this fraction of the stretched arm
    float poleTau = 0.06f;      // elbow pole smoothing, seconds
    armpose::WristLimits wrist{};
    // The drawn torso as capsules on its spine and how thick the arm is
    // (arm.capsule, arm.limbs; docs/ARM-TORSO-SCOPE-2026-10-29.md): the elbow
    // is kept out of it, and the arm measured against it.
    armpose::TorsoShape torsoShape{};
    armpose::ArmThickness thickness{};
    // Keeping the elbow out of the torso (arm.avoid): the clearance wanted and
    // how steeply the chooser's cost rises inside it (m); how fast the drawn
    // elbow may go round the body (radians a second).
    float avoidMargin = 0.01f, avoidWall = 0.01f;
    float avoidRate = 2.0f * 3.14159265f;
};

inline std::mutex gPoseSettingsMutex;

inline PoseSettings gPoseSettings;

inline PoseSettings ReadPoseSettings() { std::lock_guard lock(gPoseSettingsMutex); return gPoseSettings; }

// The most joints the body pin poses itself (the player's rig: 40).
constexpr int kMaxBodyJoints = 64;

// In `HandRig::arm`: the joint rides the chest with the right (1) or left (2)
// arm, and is in that hand (kArmHand added).
constexpr unsigned char kArmHand = 4;

// The hand's joints and frames on the identified rig. Rebuilt when the rig
// changes; read and written under gIkMutex.
struct HandRig {
    std::uintptr_t skeleton = 0;
    bool valid[2]{};
    int wrist[2]{-1, -1};
    int joint[2][handpose::kJointCount]{};
    handpose::Frame frame[2]{};
    float knuckle[2]{};
    // For pose.marks only: the arm and head the IK does not drive (-1 if absent).
    int clavicle[2]{-1, -1}, upperArm[2]{-1, -1}, lowerArm[2]{-1, -1};
    int neck = -1, head = -1, headEnd = -1;
    int handProp[2]{-1, -1};
    // Carried rigidly with the upper arm (top, twist, muscle) and the forearm (twist).
    int upperChild[2][3]{{-1, -1, -1}, {-1, -1, -1}};
    int lowerTwist[2]{-1, -1};
    // The body pin (BuildBodyRig): its joints by bodyrig::Pinned, the joint
    // the game's #camera rides, the throat, the hands, and the joints it poses
    // -- the pelvis and all that hangs from it, less the camera joint and what
    // hangs from it, the arms below the collarbones and the hands' IK targets --
    // each after its parent. A slot's role is its bodyrig::Pinned (-1: carried
    // with its parent, the slot `posedParent`). The arms ride the chest (`arm`).
    bool bodyReady = false;
    int pinned[bodyrig::kPinnedCount]{}, pinnedSlot[bodyrig::kPinnedCount]{};
    int camera = -1, throat = -1, bodyHand[2]{-1, -1};
    int posedCount = 0;
    std::array<short, kMaxBodyJoints> posed{};
    std::array<signed char, kMaxBodyJoints> posedRole{}, posedParent{};
    std::array<unsigned char, kMaxJoints> arm{};
    // The waist is straightened only on the rig its rest was measured on.
    bool straighten = false;
};

inline HandRig gHandRig;

// --- the body pin (AnimIkBody.cpp) ------------------------------------------------
//
// Planned before the native pass, so the arms are placed on the posed body
// (PlanBody), and written into the pose after it (ApplyBody).
struct BodyPlan {
    bool apply = false;
    int count = 0;
    std::array<bodyrig::Joint, kMaxBodyJoints> pose{};   // by the rig's posed slots
    // The chest (spine4) as the game left it and as posed, and the rig's
    // atlas as the game left it: the arms' collarbones and shoulders ride the
    // chest. `chestTurn` is how far the game had turned the chest from the
    // model's facing (radians, positive left): during a smooth turn Prey turns
    // the upper body towards the view while the body lags.
    bodyrig::Joint chestOld{}, chestNew{};
    Vec3 atlas{};
    float chestTurn = 0.0f;
    Vec3 Chest(const Vec3& native) const { return bodyrig::Carry(chestOld, chestNew, bodyrig::Joint{{}, native}).t; }
};

// For body.report: frames posed and refused, what the last one did
// (mm, tenths of a degree), and the most since the last report.
inline std::atomic<unsigned long long> gBodyFrames{0}, gBodyRefused{0}, gBodyFolded{0};

inline std::atomic<int> gBodyDropMm{0}, gBodyLeanDeci{0}, gBodyTwistDeci{0}, gBodyWaistMm{0};

inline std::atomic<int> gBodyPushMm{0}, gBodySinkMm{0}, gBodyShortMm{0};

inline std::atomic<int> gBodyPushMaxMm{0}, gBodySinkMaxMm{0}, gBodyShortMaxMm{0};

// The game's eye under the view against the camera joint in this pose (mm):
// whether the hands' anchor and the pose are one frame's.
inline std::atomic<int> gBodyEyeGapMm{0}, gBodyEyeGapMaxMm{0};

inline std::atomic<bool> gBodyReady{false}, gBodyStraighten{false};

// The skeleton whose spine once matched the one the waist's rest was measured
// on: matched once, it stays matched (a rebuild mid-animation can read the
// offsets a few millimetres off). And what the last body_rig line said, so a
// rebuild (every snap turn re-binds the rig) logs only a change. Under gIkMutex.
inline std::uintptr_t gWaistSkeleton = 0;

inline std::string gBodyRigLast;

inline std::atomic<int> gBodyPosed{0};

// Per callback: what the drive decided for the fingers, consumed around the
// native pass.
struct FingerPlan {
    bool apply = false;
    float weight = 0.0f;        // 1 relaxed, 0 native
    float support = 0.0f;       // the native two-handed hold's blend
};

inline std::atomic<int> gWeaponRollMilliDeg{0};   // last roll correction, for the report

// What the last owned frame looked like, for pose.marks / pose.report. All
// positions in the app's OpenXR tracking space, so they compare directly with
// what the runtime reports and what the mock draws.
struct HandMarks {
    bool valid = false;
    Vec3 joint[2][handpose::kJointCount]{};
    Vec3 tip[2][handpose::kJointCount]{};   // only for the distal joints
    Vec3 wrist[2]{};
    Vec3 gripPoint[2]{};                    // the game palm's grip point
    Vec3 length[2]{}, palm[2]{};            // the game hand's frame
    Pose grip[2]{}, aim[2]{};               // what the runtime reported
    bool posed[2]{};                        // pose.mode applied to this hand
    // The arm as drawn, and what the drive asked of it: the wrist goal before
    // any reach shaping (palm on the controller) and the one written.
    Vec3 clavicle[2]{}, shoulder[2]{}, elbow[2]{};
    bool arm[2]{};
    Vec3 goalRaw[2]{}, goalWritten[2]{};
    bool goal[2]{};
    Vec3 neck{}, headJoint{}, headEnd{};
    bool headValid = false;
    Pose head{};                            // the HMD, same space
    // The hand's prop joint (r/l_handProp_jnt): what a held grenade is bound
    // to (the arms' "weapon" attachment), and its axes (directions).
    Vec3 prop[2]{}, propAxis[2][3]{};
    bool propValid[2]{};
    float support = 0.0f;
    std::uint64_t stamp = 0;
};

inline std::mutex gMarksMutex;

inline HandMarks gMarks;

// DriveHand -> CaptureMarks, same callback and thread: the wrist goals in
// model space (before reach shaping, and as written).
struct MarkGoal { bool set = false; Vec3 raw{}, written{}; };

inline MarkGoal gMarkGoal[2];

// --- VR arm (arm.*) ---------------------------------------------------------------
//
// **The arm hangs from the player's shoulder, not the weapon animation's.**
// The native two-bone pass solves from the animated upper-arm joint, which the
// weapon's idle puts 4-16 cm from where the player's shoulder is (the GLOO
// protracts one clavicle and retracts the other; the pistol pulls the right
// shoulder back and in). Measured on the mock with an anatomical reference arm:
// hands short of the controller at full extension, elbows 10-30 cm off.
//
// Before the pass, the player's shoulder is placed (armpose: torso yaw from the
// HMD, an adult's glenohumeral centre, reached by rotating the rig's clavicle)
// and the hand's reach is measured from there. After the pass -- which keeps
// recomputing the animated shoulder from the clavicle, so it cannot be moved
// before -- the arm is rewritten in the absolute pose: clavicle, upper arm,
// elbow (the most likely one for the player's REAL hand, from the controller's
// grip pose: armpose::ChooseElbowPole), forearm, and the hand on its goal with
// everything it carries. Measured: the skinned mesh and the held weapon follow
// absolute writes made after the pass. On the mock, against arms whose elbow is
// known: shoulders on target, palms 0 cm, elbows 0.2-6.7 cm with every weapon
// (the native arm: shoulders 4-16 cm, elbows 15-21 cm with the pistol).
struct ArmPlan {
    bool valid = false;
    armpose::TorsoFrame torso{};
    Vec3 clavicleXr{}, targetXr{}, shoulderXr{};
    Vec3 shoulderModel{};
    float reachModel = 0.0f;    // the stretched arm's reach, model units
    float reach = -1.0f;        // the player's wrist from this shoulder (m), before reach shaping
    bool goalSet = false;       // DriveHand wrote this wrist goal (model)
    Vec3 goal{};
    // The free arms: the wrist's rotation is set after the pass as well, with what
    // the hand carries: the lowered no-weapon pose may run no native IK at all.
    bool forceRotation = false;
    bool rotationSet = false;
    Quaternion rotation{};
};

// What the last solve did, for arm.report and pose.marks.
struct ArmStats {
    bool solved[2]{};
    Vec3 target[2]{}, shoulder[2]{}, elbow[2]{};   // OpenXR
    float handMoveCm[2]{};      // how far the native pass left the hand from its goal
    float scale[2]{};           // segment stretch applied
    bool straight[2]{};
    float reachCm[2]{};         // the player's wrist from the shoulder, before reach shaping
    float torsoYawDeg = 0.0f, headYawDeg = 0.0f;
    // The drawn arm against the drawn torso (armpose::MeasureArm), OpenXR.
    bool torsoPlaced = false;
    armpose::ArmClearance clearance[2]{};
    int track[2]{-1, -1};       // how the drawn elbow was tracked (armpose::Track), -1 not kept out
};

// The arms against the torso since the last arm.report, per arm: frames
// measured, frames with any sample inside, and the deepest any sample went.
struct ArmFitTally {
    unsigned long long frames[2]{}, inside[2]{};
    float worst[2]{1e9f, 1e9f};
    int worstSample[2]{}, worstPart[2]{-1, -1};
    // How the drawn elbow was tracked (armpose::Track's order), frames.
    unsigned long long track[2][4]{};
};

inline ArmFitTally gArmFitTally;   // under gArmMutex

inline std::uint64_t gArmFitLogNs = 0;   // the last arm_fit log line, under gArmMutex

inline std::mutex gArmMutex;           // the torso filter, pole smoothing and stats

// The longest reach since the last arm.report, per arm: hold the arms out
// straight between two reports and it reads the player's arm length.
inline float gReachMaxCm[2]{};

// arm.length and the Arm Length option (VrOptions' arm_length): shoulder to wrist.
constexpr int kArmLengthMinCm = 40, kArmLengthMaxCm = 70;

inline armpose::TorsoYaw gTorsoYaw;

inline std::uint64_t gTorsoNs = 0, gPoleNs[2]{};

inline Vec3 gPole[2]{};

// The elbow as drawn last frame (the chosen one, kept out of the torso): where
// the next frame's tracking starts.
inline Vec3 gDrawnPole[2]{};

inline ArmStats gArmStats;

// Long weapons held further back (twohand::HoldBack; ik.longreach), mm.
inline std::atomic<int> gHoldBackMm{0};

inline std::atomic<std::uint64_t> gLongGeneration{0};

// Model <-> the app's OpenXR space, through the same anchor the hands use.
struct XrMap {
    const GameplayPoseFrame* frame = nullptr;
    Vec3 anchor{};
    animik::Location location{};
    Vec3 ToXr(const Vec3& model) const
    {
        return stereo::EngineToTracking(animik::ModelToWorld(location, model), anchor, PlaySpaceRotation(*frame),
                                        frame->tracking.head);
    }
    Vec3 ToModel(const Vec3& xr) const
    {
        return animik::WorldToModel(location,
                                    stereo::TrackingToEngine(xr, anchor, PlaySpaceRotation(*frame), frame->tracking.head));
    }
};

// **The hands' anchor, and the whole point of the IK crosstalk fix**
// (RE-IK-CROSSTALK-2026-09-09): the view camera's centre. The reticle origin,
// the old anchor, is the native cached reticle ray origin: our reticle lane
// moves the reticle, the engine unprojects it, and the result lands there -- so
// the right controller's aim reached BOTH hands through this one term. The camera
// centre does not move with the reticle. Anchor::camera refuses a frame without a
// camera rather than fall back to the reticle origin, which would bring the
// defect back on exactly the frames where the camera is unreadable.
constexpr Anchor kIkAnchor = Anchor::camera;

inline float Dist3(Vec3 a, Vec3 b) { return Length(Sub(a, b)); }

// --- skeleton dump (ik.skel) ------------------------------------------------------
//
// One-shot research record of the owning rig: every joint's name, the joint
// table's candidate parent fields, the parent DERIVED from the pose itself
// (the p whose abs * rel[j] lands on abs[j]), and the relative and absolute
// pose before and after the engine's ADIK pass. It is what the hand-pose work
// is designed from; the derived parent does not trust any table offset.
inline std::atomic<int> gSkelDump{0};   // 1 = requested, consumed by the next matched frame

// The request's label, for the dump's header.
inline std::mutex gSkelLabelMutex;

inline std::string gSkelLabel;

inline std::atomic<bool> gHelperDump{false};

// The native left palm in the barrel frame (mm), last alignment pass: where the
// two-handed support region would be, accepted or not.
inline std::atomic<int> gSupportSocketMm[3]{};

inline std::atomic<bool> gWideSupport{true};

// One settled support socket per weapon (ik.supportlatch, default 1).
inline std::atomic<bool> gSupportLatchOn{true};

inline std::mutex gSupportLatchMutex;

inline preyvr::twohand::RegionLatch gSupportLatch;

// Long weapons held further back (twohand::HoldBack; ik.longreach): the applied
// hold-back is eased in per weapon (no jump at the latch).
inline float gHoldBackEased = 0.0f;

inline std::uint64_t gHoldBackOwner = 0, gHoldBackNs = 0;

// The held weapon's concrete vtable (RVA), for identifying weapons by class.
inline std::atomic<std::uint32_t> gWeaponVtableRva{0};

// The owning rig's pose arrays in this callback, and where its character is.
struct OwnedPose {
    std::uint8_t* relative = nullptr;
    std::uint8_t* absolute = nullptr;
    unsigned count = 0;
    animik::Location location{};
};

// The right wrist as DriveHand wrote it, for the left hand on a two-handed
// weapon to follow: hand 0 fills it, hand 1 reads it. `written` only when the
// wrist was aligned to the weapon; the rotation is kept whenever it was written.
struct PrimaryHand {
    Vec3 goal{};
    Quaternion rotation{};
    bool written = false;
};

// Called across the six files.
// AnimIkRig.cpp
unsigned int DynArrayCount(const void* data);
bool ReadPointer(const void* at, void** out);
bool ReadInt(const void* at, int* out);
bool ReadFloats(const void* at, float* out, unsigned int count);
void FormatAdikLine(char* line, std::size_t capacity, unsigned int index, int target,
                    const char* targetName, int weight, const char* weightName);
const char* JointName(const std::uint8_t* skeleton, int index);
bool TryGetFreeArms(std::uintptr_t player, EquippedRig& out);
bool IdentifyRig(std::uint8_t* character);
bool ReadQuatT(std::uint8_t* array, unsigned count, int index, QuatT& out);
bool WriteGoal(std::uint8_t* target, float* weight, const Vec3& position,
               const Quaternion& rotation, bool writeRotation);
bool ReadAlignmentMemory(void*, std::uintptr_t address, void* output, std::size_t size);
void BuildHandRig(std::uint8_t* skeleton, unsigned jointCount, std::uint8_t* relative, unsigned poseCount,
                  std::uintptr_t character);
bool WriteFloatsGuarded(std::uint8_t* at, const float* values, unsigned count);
bool WriteQuatT(std::uint8_t* absolute, unsigned count, int index, const Quaternion& q, const Vec3& t);
// The joint named `name` (-1: none), and every joint's parent (-1: none) from
// the joint table; false when a parent cannot be read or lies outside the rig.
int RigJointByName(const std::uint8_t* skeleton, unsigned count, const char* name);
bool RigParents(const std::uint8_t* skeleton, unsigned count, int* parent);
// AnimIkBody.cpp
void BuildBodyRig(HandRig& rig, const std::uint8_t* skeleton, unsigned jointCount, std::uint8_t* relative,
                  unsigned poseCount, std::uintptr_t character);
void PlanBody(const GameplayPoseFrame& frame, const animik::Location& location, std::uint8_t* relative,
              std::uint8_t* absolute, unsigned count, float shoulderYaw, BodyPlan& plan);
void ApplyBody(std::uint8_t* absolute, unsigned count, const HandRig& rig, const BodyPlan& plan);
// AnimIkHand.cpp
void DriveHand(unsigned int hand, const OwnedPose& owned, const GameplayPoseFrame& frame,
               const EquippedRig& owner, const PoseSettings& pose, PrimaryHand* primaryOut,
               const PrimaryHand* primaryIn, FingerPlan* fingers, ArmPlan* arm);
bool ApplyRelaxedFingers(std::uint8_t* relative, unsigned count, float weight, const handpose::FingerShape& shape,
                         int side, float carryFist);
void PropagateFingers(std::uint8_t* relative, std::uint8_t* absolute, unsigned count, const HandRig& rig, int side = 1);
// AnimIkArms.cpp
float ShoulderYaw(const GameplayPoseFrame& frame);
void PrepareArms(const GameplayPoseFrame& frame, const animik::Location& location, std::uint8_t* absolute,
                 unsigned count, const PoseSettings& pose, float shoulderYaw, const BodyPlan& body, ArmPlan plan[2]);
void SolveArms(const GameplayPoseFrame& frame, std::uint8_t* absolute, unsigned count,
               const animik::Location& location, const HandRig& rig, const ArmPlan plan[2],
               const PoseSettings& pose);
// The drawn torso's capsules (PoseSettings::torsoShape) on the spine as `absolute`
// has it, OpenXR; false (none placed) without the body pin's joints.
bool PlaceTorso(const XrMap& map, std::uint8_t* absolute, unsigned count, const HandRig& rig,
                const armpose::TorsoShape& shape, armpose::Capsule torso[armpose::kTorsoParts]);
// AnimIkReports.cpp
void DumpWeaponFrames(const RigIdentity& owner);
void DumpSkeletonPose(std::uint8_t* character, std::uint8_t* relative, const std::uint8_t* before,
                      std::uint8_t* absolute, unsigned count, const animik::Location* location,
                      const std::string& label);
void CaptureMarks(const GameplayPoseFrame& frame, std::uint8_t* absolute, unsigned count,
                  const animik::Location& location, const HandRig& rig, const FingerPlan& fingers);
// " fit<side>=<deepest sample>/<torso part>:<cm> clearCm<side>=<upper>,<elbow>,<fore>,<wrist>",
// each part's surface from the torso's (negative inside); " fit<side>=-" unmeasured.
std::string ArmFitText(const armpose::ArmClearance& clearance, const char* side);

} // namespace preyvr::dll::ik
