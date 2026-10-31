#pragma once

#include "base/VrMath.h"

// The pure half of the VR arm (jordi/polishing): **the arm comes from where
// the player's shoulder is.**
//
// The game draws its first-person arms from the shoulders its weapon
// animation authored: measured on the mock (docs/ARM-POSE-2026-10-06.md),
// the GLOO's idle protracts the left clavicle 14 cm and retracts the right
// 7 cm, the pistol's pulls the right shoulder 12 cm back and 10 cm inwards,
// and the shoulders never move with the player's body. The native two-bone
// solve then hangs the arm from there, so the hand falls short of the
// controller at full extension and the elbow lands 10-30 cm from where the
// player's is.
//
// What is defined here, as numbers:
//
//  * **The player's torso** -- a yaw-only frame from the HMD, which follows the
//    head's yaw with a deadzone (turning the head alone does not turn the
//    shoulders) and relaxes towards it;
//  * **the player's shoulder** -- an adult's glenohumeral centre relative to
//    the centre eye in that frame (the anthropometry Dishonored VR uses),
//    reached by rotating the rig's clavicle about its root;
//  * **the elbow** -- of the circle of elbows that reach the wrist, the one
//    whose wrist bends the way wrists can (little side deviation, flexion
//    within its range), the anatomical direction (down, out, a little back)
//    breaking ties: the forearm follows the hand's twist.
//
// All in one right-handed frame with +Y up and -Z forward (OpenXR), metres.
namespace preyvr::armpose {

// +1 right arm, -1 left arm.
using Side = int;

struct Body {
    // The shoulder joint from the centre eye, in the torso frame (m):
    // forward, outwards (to this arm's side) and up.
    float shoulderForward = -0.09f;
    float shoulderOut = 0.185f;
    float shoulderUp = -0.23f;
    // The drawn arm's segments (m): shoulder -> elbow, elbow -> wrist.
    float upper = 0.285f;
    float fore = 0.275f;
    // How far the clavicle may lengthen or shorten (fraction) to put the
    // shoulder on its target, and the arm's segments to reach the wrist.
    float clavicleStretch = 0.15f;
    float armStretch = 1.10f;
};

struct TorsoFrame {
    Vec3 forward{0, 0, -1};
    Vec3 right{1, 0, 0};
    Vec3 up{0, 1, 0};
};

// The yaw of a horizontal forward (0 = -Z, positive turning left, about +Y).
float YawOf(const Vec3& forward);
TorsoFrame TorsoFromYaw(float yaw);
// The head's yaw, robust looking straight down or up: the head's up vector
// stands in for its forward as the pitch approaches +-90 degrees.
float HeadYaw(const Quaternion& head);

// The torso follows the head's yaw: never further than `deadzone` from it,
// and relaxing towards it at `relax` (fraction per second) while within.
struct TorsoYaw {
    bool valid = false;
    float yaw = 0.0f;
    float Update(float headYaw, float dtSeconds, float deadzone, float relax);
    void Reset() { valid = false; }
};

// Where the player's shoulder joint is, from the centre eye with the head level
// and facing the torso.
Vec3 ShoulderTarget(const Vec3& eye, const TorsoFrame& torso, Side side, const Body& body);

// The neck's pivot from the eyes, head-local OpenXR (+Y up, +Z back), metres.
// Turning or tipping the head swings the eyes about it by several centimetres.
inline constexpr Vec3 kNeckFromEye{0.0f, -0.075f, 0.0805f};

// The eye as it would be with the head level and facing the torso, tracking
// space: the head moved by where its neck is now, less where the neck would be
// level and facing `torsoYaw`. Rotating the head alone leaves it where it was;
// facing the torso it is the head itself.
Vec3 LevelEye(const Pose& head, float torsoYaw);

// **The shoulder from the headset's pose.** Turning or tipping the head swings
// the eyes about the neck by several centimetres, and the shoulders do not
// move with them: looking down at the hands dropped an eye-anchored shoulder
// 5-11 cm towards them and bent a straight arm by 20-45 degrees
// (docs/BODY-FOLLOW.md, the shoulders). So the shoulder is placed from the eye
// as it would be with the head level and facing the torso (LevelEye). A lean
// still moves it.
Vec3 ShoulderFromHead(const Pose& head, float torsoYaw, Side side, const Body& body);

// The drawn arm's length, shoulder joint to wrist (m), and the same body with
// its segments set to `metres`, split as the defaults are (28.5 : 27.5).
float ArmLength(const Body& body);
Body WithArmLength(Body body, float metres);

// The clavicle's end: from its root towards `target`, at the clavicle's own
// length stretched by at most `stretch` either way.
Vec3 PlaceShoulder(const Vec3& clavicleRoot, float clavicleLength, const Vec3& target, float stretch);

// The anatomical elbow direction for this arm: down, out and a little back.
Vec3 BodyPole(const TorsoFrame& torso, Side side);

// The wrist's angles for a forearm direction `forearm` (elbow -> wrist, unit)
// and the hand's anatomical frame (`length` wrist -> knuckles, `palm` out of
// the palm, both unit). Flexion bends the hand towards its palm (positive) or
// its back (negative). Degrees.
float WristFlexionDeg(const Vec3& forearm, const Vec3& length, const Vec3& palm);
// Deviation, signed: towards the thumb (radial) positive, towards the little
// finger (ulnar) negative. The thumb is on length x palm for the right hand.
float WristRadialDeg(const Vec3& forearm, const Vec3& length, const Vec3& palm, Side side);

// How likely a wrist and an elbow are, as a MAP estimate's priors (degrees).
// A wrist bends freely towards the palm or its back, little sideways, and a
// hand round a controller sits a little ulnar; beyond its range of motion it
// cannot go at all. The elbow swivels about the shoulder-wrist axis around its
// natural place (BodyPole).
struct WristLimits {
    float flexionSigmaDeg = 45.0f;
    float deviationRestDeg = -8.0f;   // ulnar
    float deviationSigmaDeg = 12.0f;
    float swivelSigmaDeg = 25.0f;
    // Range of motion, and how steep its wall is.
    float flexionMaxDeg = 75.0f, extensionMaxDeg = 65.0f;
    float radialMaxDeg = 20.0f, ulnarMaxDeg = 35.0f;
    float wallDeg = 5.0f;
    // The previous frame's elbow, as one more prior: no flips between
    // near-equal choices from frame to frame.
    float holdSigmaDeg = 45.0f;
};
// Tuned offline against known arms (build/jordi-mock/elbowsim.py, 400 each):
// natural arms (swivel, flexion, deviation spread as players make them) mean
// elbow error 4.5 cm, p90 9.7, against 6.9 / 15.1 for the anatomical direction
// alone; controllers held at the edge of the wrist's range with the elbow in
// its natural place 4.6 / 8.7 (the anatomical direction, right by
// construction there, 3.2 / 6.9) -- an impossible wrist does not raise it.

// **The elbow the hand allows.** Around the circle of elbows that reach the
// wrist, the most likely one: the wrist bending as wrists do, within its
// range, and the elbow near its natural place -- so the forearm follows the
// hand's twist, a hand that says nothing leaves the elbow hanging, and a hand
// that would need an impossible wrist raises the elbow as a person's would.
// Returns the pole that selects it (unit, from the circle's centre towards the
// elbow). With `obstacle` (the drawn torso, below) an elbow that would bring
// the arm into it costs more, the steeper the deeper.
struct ArmObstacle;
Vec3 ChooseElbowPole(const Vec3& shoulder, const Vec3& wrist, float upper, float fore, float maxStretch,
                     const Vec3& handLength, const Vec3& handPalm, Side side, const Vec3& bodyPole,
                     const WristLimits& limits, const Vec3* previousPole = nullptr,
                     const ArmObstacle* obstacle = nullptr);

struct ArmSolution {
    Vec3 elbow{};
    float scale = 1.0f;     // applied to both segments to reach
    bool straight = false;  // the wrist is at (or beyond) the stretched reach
};
// Two-bone solve: the elbow on the circle where both segments meet, towards
// `pole`. Beyond the reach the segments lengthen (up to `maxStretch`); beyond
// that the arm points straight at the wrist and the caller decides.
ArmSolution SolveElbow(const Vec3& shoulder, const Vec3& wrist, float upper, float fore,
                       const Vec3& pole, float maxStretch);

// The shortest rotation taking unit `from` onto unit `to`.
Quaternion FromTo(const Vec3& from, const Vec3& to);

// Exponential smoothing of a direction (unit), time constant `tau` seconds.
Vec3 SmoothDirection(const Vec3& previous, const Vec3& current, float dtSeconds, float tau);

// --- the arm against the drawn torso (docs/ARM-TORSO-SCOPE-2026-10-29.md) ----------
//
// **What the arm must not pass through.** With the hands at the chest the
// circle of elbows that reach the wrist passes by the drawn torso, and
// ChooseElbowPole knows nothing of it. The torso is a few capsules laid across
// the body on its spine joints; the arm is sampled along both segments, each
// sample a ball as thick as the arm is there. The elbow is kept out of it
// (below), and the drawn arm measured against it (arm.capsule, arm.limbs,
// dbg.draw 512); the defaults were fitted to the mesh by eye (PreyVR-103).

// Every point within `radius` of the segment a-b; none without a radius.
struct Capsule {
    Vec3 a{}, b{};
    float radius = 0.0f;
};
// How far `p` lies outside the capsule's surface, metres (negative inside).
// A capsule without a radius is never near.
float CapsuleDistance(const Capsule& capsule, const Vec3& p);

// A capsule laid across the body on a spine joint, in the joint's frame
// (metres): centred `forward` and `up` from the joint, its segment `halfWidth`
// either way along the joint's right, `radius` round it. No radius: none.
struct CapsuleSpec {
    float forward = 0.0f, up = 0.0f, halfWidth = 0.0f, radius = 0.0f;
};
// On a joint at `joint` whose right, forward and up are the unit vectors given.
Capsule PlaceCapsule(const Vec3& joint, const Vec3& right, const Vec3& forward, const Vec3& up, const CapsuleSpec& spec);

// The drawn torso, one capsule on each spine joint: the chest on spine4, the
// ribs on spine3, the waist on spine2, the hips on spine1.
enum TorsoPart : int { kChest, kRibs, kWaist, kHips, kTorsoParts };
const char* TorsoPartName(int part);
// Defaults from Morgan's standing rig (PreyVR-84) and an adult's torso -- 33 cm
// wide and 24 cm deep at the chest, 29 by 22 at the waist -- with no mesh to
// measure; drawn over the body in the headset they matched it (PreyVR-103).
struct TorsoShape {
    CapsuleSpec part[kTorsoParts]{
        {0.030f, -0.040f, 0.042f, 0.118f},
        {0.015f, -0.045f, 0.041f, 0.116f},
        {-0.015f, -0.025f, 0.038f, 0.111f},
        {-0.015f, -0.040f, 0.050f, 0.117f},
    };
};

// How thick the drawn arm is (radius, metres) at the upper arm, the elbow, the
// forearm and the wrist.
struct ArmThickness {
    float upper = 0.050f, elbow = 0.045f, fore = 0.040f, wrist = 0.030f;
};

// Where the arm is measured: the upper arm 30, 55 and 80% of the way from the
// shoulder, the elbow, the forearm 20, 45 and 70% of the way from the elbow,
// and the wrist.
enum ArmSample : int { kUpper30, kUpper55, kUpper80, kElbowAt, kFore20, kFore45, kFore70, kWristAt, kArmSamples };
const char* ArmSampleName(int sample);

struct ArmClearance {
    bool valid = false;               // measured against at least one capsule
    Vec3 point[kArmSamples]{};
    float radius[kArmSamples]{};      // the arm's thickness there
    float clear[kArmSamples]{};       // the arm's surface from the torso's, m (negative: inside)
    int part[kArmSamples]{};          // the torso part nearest it
    int deepest = 0;                  // the sample nearest the torso, or deepest in it
    float Worst() const { return clear[deepest]; }
};
// The arm shoulder -> elbow -> wrist against `count` capsules.
ArmClearance MeasureArm(const Vec3& shoulder, const Vec3& elbow, const Vec3& wrist, const Capsule* torso, int count,
                        const ArmThickness& thickness);

// --- keeping the elbow out of the torso ------------------------------------------
//
// **The elbow goes round the body, never through it.** The 2026-10-29 headset
// run (PreyVR-103) had the left elbow 12-15 cm inside the drawn ribs with the
// wrist in front of the body: the chooser had put it there. A torso term in
// ChooseElbowPole prefers elbows outside the body, but alone it is not enough
// (offline: 13% of frames still in): the pole's smoothing swings the elbow
// through the body when the choice moves from one side of it to the other, and
// snapping to the nearest clear elbow instead jumps across the body. So the
// drawn elbow is tracked: from where it was drawn, it moves towards the chosen
// one round the circle of elbows that reach the wrist, through clear elbows
// only, and waits at the body while the chosen one is in it.

// What the arm stays out of: the torso's capsules (`count`, none without), the
// arm's thickness, the clearance wanted (m), and how steeply ChooseElbowPole's
// cost rises inside it: ((margin - clearance) / wall)^2.
struct ArmObstacle {
    const Capsule* torso = nullptr;
    int count = 0;
    ArmThickness thickness{};
    float margin = 0.01f;
    float wall = 0.01f;
};
// The arm's least clearance with its elbow at `elbow` (m; negative inside):
// every sample but the wrist, which no elbow moves. Far without a capsule.
float ArmClear(const Vec3& shoulder, const Vec3& elbow, const Vec3& wrist, const ArmObstacle& obstacle);

// How the tracked elbow moved this frame.
enum class Track : int {
    kFree,      // straight to the chosen elbow: nothing in the way (or no torso)
    kAround,    // round the other way, the body between: at most the step
    kParked,    // the chosen elbow is in the body: at the body's edge nearest it
    kPushed,    // the body moved onto the drawn elbow: out to the nearest clear one
};
struct TrackedPole {
    Vec3 pole{};
    Track how = Track::kFree;
};
// The elbow to draw: from `drawn` (last frame's pole; none starts at `wanted`)
// towards `wanted` round the circle of elbows that reach the wrist, through
// elbows clear of the torso only -- clear meaning at least the margin, or where
// none is, within 5 mm of the clearest. Going round the other way, or along
// the body to its edge, it moves at most `maxStep` radians. With nothing in
// the way it is `wanted` itself.
TrackedPole TrackElbowPole(const Vec3& shoulder, const Vec3& wrist, float upper, float fore, float maxStretch,
                           const Vec3& wanted, const Vec3* drawn, float maxStep, const ArmObstacle& obstacle);

} // namespace preyvr::armpose
