#include "body/HandPose.h"
#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace preyvr;
using namespace preyvr::handpose;

namespace {

using preyvr::test::Require;

bool NearVec(Vec3 a, Vec3 b, float e = 1e-3f)
{
    return std::fabs(a.x - b.x) <= e && std::fabs(a.y - b.y) <= e && std::fabs(a.z - b.z) <= e;
}
bool Orthonormal(const Frame& f)
{
    return std::fabs(Length(f.length) - 1) < 1e-4f && std::fabs(Length(f.thumb) - 1) < 1e-4f &&
           std::fabs(Length(f.palm) - 1) < 1e-4f && std::fabs(Dot(f.length, f.thumb)) < 1e-4f &&
           std::fabs(Dot(f.length, f.palm)) < 1e-4f && std::fabs(Dot(f.thumb, f.palm)) < 1e-4f;
}

// The first-person rig's own offsets (left hand, metres, measured 2026-10-06):
// metacarpal offsets from the wrist and first-phalanx offsets from them.
const Vec3 kLeftBase[4] = {{0.0351f, -0.0015f, 0.0259f}, {0.0345f, 0.0032f, 0.0027f},
                           {0.0318f, 0.0056f, -0.0211f}, {0.0279f, -0.0024f, -0.0361f}};
const Vec3 kLeftFirst[4] = {{0.0578f, -0.0034f, 0.0002f}, {0.0595f, -0.0032f, 0.0001f},
                            {0.0542f, -0.0027f, -0.0006f}, {0.0499f, -0.0022f, -0.0006f}};

Frame MeasuredLeft(float& knuckle)
{
    Quaternion rotation[4];
    const int bases[4] = {kIndexBase, kMiddleBase, kRingBase, kPinkyBase};
    for (int f = 0; f < 4; ++f) { rotation[f] = RelaxedRelative(Side::left, bases[f], 1.0f); }
    Frame frame{};
    Require(MeasureHandFrame(Side::left, kLeftBase, rotation, kLeftFirst, frame, knuckle), "left frame measured");
    return frame;
}

void TestNames()
{
    Require(std::strcmp(JointName(Side::left, kIndex1), "l_index1_jnt") == 0, "left index1 name");
    Require(std::strcmp(JointName(Side::right, kPinkyBase), "r_pinkyBase_jnt") == 0, "right pinky base name");
    Require(std::strcmp(JointName(Side::left, kThumb3), "l_thumb3_jnt") == 0, "thumb3 name");
    Require(std::strcmp(WristName(Side::right), "r_hand_jnt") == 0, "wrist name");
    // Parents come before children: one pass in role order propagates a hand.
    for (int j = 0; j < kJointCount; ++j) { Require(ParentRole(j) < j, "parent role precedes child"); }
    Require(ParentRole(kThumb1) == -1 && ParentRole(kRingBase) == -1, "roots hang off the wrist");
    Require(ParentRole(kMiddle3) == kMiddle2, "phalanx chain");
}

// The real hand, by the OpenXR definition of the grip pose's axes.
void TestGripHandFrame()
{
    for (int s = 0; s < 2; ++s) {
        const Side side = s ? Side::left : Side::right;
        const Frame straight = GripHandFrame(side, 0.0f);
        Require(Orthonormal(straight), "grip frame orthonormal");
        // -Z runs through the fist, pinky -> thumb: the thumb's side.
        Require(NearVec(straight.thumb, {0, 0, -1}), "thumb side is -Z");
        // +X points out of the palm on the left hand and into it on the right.
        Require(NearVec(straight.palm, side == Side::left ? Vec3{1, 0, 0} : Vec3{-1, 0, 0}), "palm normal is +-X");
        // Wrist -> knuckles: -Y for a fist at right angles to its tube.
        Require(NearVec(straight.length, {0, -1, 0}), "length is -Y");
        const Frame leaning = GripHandFrame(side, 0.26f);
        Require(Orthonormal(leaning), "leaning grip frame orthonormal");
        // The tube leans towards the fingers' direction.
        Require(Dot(leaning.thumb, leaning.length) < 1e-4f && Dot(Vec3{0, 0, -1}, leaning.length) > 0.2f,
                "tube leans towards the fingers");
    }
    // The handedness rule both hand frames share.
    const Frame l = GripHandFrame(Side::left, 0.2f), r = GripHandFrame(Side::right, 0.2f);
    Require(NearVec(Cross(l.length, l.thumb), l.palm), "left: palm = length x thumb");
    Require(NearVec(Cross(r.thumb, r.length), r.palm), "right: palm = thumb x length");
}

// The game hand, from its knuckles: along +X, thumb on +Z, palm towards -Y
// on the left hand (the rig's measured layout).
void TestMeasuredHandFrame()
{
    float knuckle = 0;
    const Frame left = MeasuredLeft(knuckle);
    Require(Orthonormal(left), "measured frame orthonormal");
    Require(left.length.x > 0.98f, "left hand runs along +X");
    Require(left.thumb.z > 0.95f, "left thumb on +Z");
    Require(left.palm.y < -0.95f, "left palm towards -Y");
    Require(knuckle > 0.07f && knuckle < 0.11f, "knuckles 7-11 cm from the wrist");
    // The right hand is the mirror image through local XY.
    Vec3 rb[4], rf[4];
    Quaternion rq[4];
    const int bases[4] = {kIndexBase, kMiddleBase, kRingBase, kPinkyBase};
    for (int f = 0; f < 4; ++f) {
        rb[f] = {kLeftBase[f].x, kLeftBase[f].y, -kLeftBase[f].z};
        rf[f] = {kLeftFirst[f].x, kLeftFirst[f].y, -kLeftFirst[f].z};
        rq[f] = RelaxedRelative(Side::right, bases[f], 1.0f);
    }
    Frame right{};
    float rk = 0;
    Require(MeasureHandFrame(Side::right, rb, rq, rf, right, rk), "right frame measured");
    Require(right.length.x > 0.98f && right.thumb.z < -0.95f && right.palm.y < -0.95f, "right hand mirrors left");
    Require(std::fabs(rk - knuckle) < 1e-4f, "mirror keeps the knuckle distance");
    // Degenerate geometry is refused, not turned into a frame.
    const Vec3 zero[4]{};
    Require(!MeasureHandFrame(Side::left, zero, rq, zero, right, rk), "degenerate hand refused");
}

// The whole point: the wrist rotation that puts the game hand on the real one.
void TestAnatomicalMapping()
{
    float knuckle = 0;
    const Frame game = MeasuredLeft(knuckle);
    for (float lean : {0.0f, 0.26f, -0.3f}) {
        const Frame real = GripHandFrame(Side::left, lean);
        const Quaternion q = FrameToFrame(game, real);
        Require(NearVec(Rotate(q, game.length), real.length), "length lands on the real hand's");
        Require(NearVec(Rotate(q, game.thumb), real.thumb), "thumb side lands on the real hand's");
        Require(NearVec(Rotate(q, game.palm), real.palm), "palm lands on the real palm");
    }
    // An arbitrary pair of frames, too.
    Frame a{{0.6f, 0.8f, 0}, {-0.8f, 0.6f, 0}, {0, 0, 1}};
    Frame b{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}};
    const Quaternion q = FrameToFrame(a, b);
    Require(NearVec(Rotate(q, a.length), b.length) && NearVec(Rotate(q, a.palm), b.palm), "general frame map");
}

void TestRelaxedPose()
{
    // Phalanges flex towards the palm (-Y) about local Z; the little finger most.
    const Vec3 bone{1, 0, 0};
    const Vec3 index2 = Rotate(RelaxedRelative(Side::left, kIndex2, 1.0f), bone);
    const Vec3 pinky2 = Rotate(RelaxedRelative(Side::left, kPinky2, 1.0f), bone);
    Require(index2.y < 0 && pinky2.y < index2.y, "flexion towards the palm, cascading to the pinky");
    Require(std::fabs(index2.z) < 1e-5f, "a phalanx flexes in its own plane");
    // Curl scales the flexion; 0 leaves only the rig's palm arch.
    const Vec3 straight = Rotate(RelaxedRelative(Side::left, kMiddle2, 0.0f), bone);
    Require(NearVec(straight, bone, 1e-5f), "curl 0 straightens the phalanges");
    const float half = AngleBetween(RelaxedRelative(Side::left, kRing2, 0.5f), Quaternion{});
    const float full = AngleBetween(RelaxedRelative(Side::left, kRing2, 1.0f), Quaternion{});
    Require(std::fabs(full - 2 * half) < 1e-3f, "curl is linear in angle");
    // The right hand's pose is the mirror: same flexion about Z, reflected bases.
    const Quaternion l = RelaxedRelative(Side::left, kIndexBase, 1.0f);
    const Quaternion r = RelaxedRelative(Side::right, kIndexBase, 1.0f);
    Require(std::fabs(l.x + r.x) < 1e-6f && std::fabs(l.y + r.y) < 1e-6f && std::fabs(l.z - r.z) < 1e-6f,
            "right relaxed pose mirrors the left");
    const Vec3 rIndex2 = Rotate(RelaxedRelative(Side::right, kIndex2, 1.0f), bone);
    Require(NearVec(rIndex2, index2, 1e-5f), "both hands flex the same way in their own frames");
}

void TestGripPoint()
{
    float knuckle = 0;
    const Frame f = MeasuredLeft(knuckle);
    GripPoint point{};
    const Vec3 p = GripPointInWrist(f, knuckle, point);
    Require(std::fabs(Dot(p, f.length) - point.alongHand * knuckle) < 1e-5f, "grip point along the hand");
    Require(std::fabs(Dot(p, f.palm) - point.outOfPalm) < 1e-5f, "grip point out of the palm");
    Require(std::fabs(Dot(p, f.thumb)) < 1e-5f, "no thumb offset by default");
}

void TestSoftReach()
{
    const Vec3 shoulder{0.1f, 0.2f, 1.4f};
    const float reach = 0.51f, knee = 0.85f;
    // Exact inside the knee.
    const Vec3 inside{shoulder.x + 0.3f, shoulder.y, shoulder.z};
    Require(NearVec(SoftReach(shoulder, inside, reach, knee), inside, 1e-6f), "1:1 inside the knee");
    // Beyond it: same direction, never past the reach, monotonic, continuous.
    float previous = 0;
    for (float d = 0.30f; d < 1.5f; d += 0.01f) {
        const Vec3 goal{shoulder.x, shoulder.y + d * 0.6f, shoulder.z - d * 0.8f};
        const Vec3 out = SoftReach(shoulder, goal, reach, knee);
        const Vec3 v{out.x - shoulder.x, out.y - shoulder.y, out.z - shoulder.z};
        const float l = Length(v);
        Require(l <= reach + 1e-5f, "never past the arm's reach");
        if (previous > 0) {
            Require(l >= previous - 1e-6f, "monotonic");
            Require(l - previous < 0.0101f, "no jump");
        }
        Require(std::fabs(v.y / l - 0.6f) < 1e-4f && std::fabs(v.z / l + 0.8f) < 1e-4f, "direction kept");
        previous = l;
    }
    // Slope 1 at the knee: just past it, the hand still moves as the controller does.
    const Vec3 a{shoulder.x + knee * reach + 0.001f, shoulder.y, shoulder.z};
    const Vec3 b{shoulder.x + knee * reach + 0.002f, shoulder.y, shoulder.z};
    const float da = SoftReach(shoulder, a, reach, knee).x, db = SoftReach(shoulder, b, reach, knee).x;
    Require(std::fabs((db - da) - 0.001f) < 5e-5f, "slope 1 at the knee");
}

// A weapon rolled about its barrel by any angle is rolled back exactly, and
// the barrel itself does not move.
void TestRollToMatch()
{
    Vec3 axis{0.3f, 0.9f, -0.2f};
    const float n = Length(axis);
    axis = {axis.x / n, axis.y / n, axis.z / n};
    const Vec3 palm{0.8f, -0.1f, 0.59f}, thumb{-0.2f, 0.3f, 0.93f};
    for (float truth : {0.0f, 0.7f, -2.0f, 3.1f}) {
        const Quaternion r = AxisAngle(axis, truth);
        const Vec3 from[2] = {palm, thumb};
        const Vec3 to[2] = {Rotate(r, palm), Rotate(r, thumb)};
        const float roll = RollToMatch(axis, from, to, 2);
        Require(AngleBetween(AxisAngle(axis, roll), r) < 1e-3f, "roll recovered");
        Require(NearVec(Rotate(AxisAngle(axis, roll), axis), axis, 1e-5f), "the barrel stays put");
    }
    // Off-axis disagreement: the least-squares roll, not an arbitrary one.
    const Vec3 from[1] = {{1, 0, 0}};
    const Vec3 to[1] = {{0, 0.5f, 1}};
    const float roll = RollToMatch({0, 1, 0}, from, to, 1);
    Require(NearVec(Rotate(AxisAngle({0, 1, 0}, roll), {1, 0, 0}), {0, 0, 1}, 1e-4f), "best roll in the plane");
}

void TestSlerp()
{
    const Quaternion a{}, b{0, 0, std::sin(0.5f), std::cos(0.5f)};
    Require(AngleBetween(Slerp(a, b, 0), a) < 1e-4f && AngleBetween(Slerp(a, b, 1), b) < 1e-4f, "slerp endpoints");
    Require(std::fabs(AngleBetween(Slerp(a, b, 0.5f), a) - 0.5f) < 1e-3f, "slerp halfway");
    const Quaternion nb{-b.x, -b.y, -b.z, -b.w};
    Require(std::fabs(AngleBetween(Slerp(a, nb, 0.5f), a) - 0.5f) < 1e-3f, "slerp takes the short arc");
}

} // namespace

void TestHandShape()
{
    const Vec3 bone{1, 0, 0};
    // fist 0 is the relaxed hand, for every joint, both hands, any curl.
    for (int s = 0; s < 2; ++s) {
        const Side side = s ? Side::left : Side::right;
        for (int j = 0; j < kJointCount; ++j) {
            const Quaternion a = ShapeRelative(side, j, 0.7f, 0.0f), b = RelaxedRelative(side, j, 0.7f);
            Require(std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z) + std::fabs(a.w - b.w) < 1e-5f,
                    "fist 0 == relaxed");
        }
    }
    // Closing flexes every phalanx further towards the palm, in its own plane.
    for (int j : {kIndex1, kIndex2, kMiddle1, kRing2, kPinky1}) {
        const Vec3 open = Rotate(ShapeRelative(Side::left, j, 1.0f, 0.0f), bone);
        const Vec3 half = Rotate(ShapeRelative(Side::left, j, 1.0f, 0.5f), bone);
        const Vec3 shut = Rotate(ShapeRelative(Side::left, j, 1.0f, 1.0f), bone);
        Require(shut.y < half.y && half.y < open.y, "the fist closes towards the palm");
        Require(std::fabs(shut.z) < 1e-5f, "in the phalanx's own plane");
    }
    // A closed knuckle bends past 60 degrees; the metacarpals keep the palm's arch.
    Require(AngleBetween(ShapeRelative(Side::left, kMiddle1, 1.0f, 1.0f), Quaternion{}) > 60.0f * 3.14159f / 180.0f,
            "a real fist");
    Require(AngleBetween(ShapeRelative(Side::left, kRingBase, 1.0f, 1.0f),
                         ShapeRelative(Side::left, kRingBase, 1.0f, 0.0f)) < 2e-3f, "metacarpals unchanged");
    // Clamped, and mirrored for the right hand like the relaxed pose.
    Require(AngleBetween(ShapeRelative(Side::left, kIndex2, 1.0f, 3.0f), ShapeRelative(Side::left, kIndex2, 1.0f, 1.0f)) < 2e-3f,
            "fist clamped to 1");
    const Vec3 l = Rotate(ShapeRelative(Side::left, kIndex2, 1.0f, 1.0f), bone);
    const Vec3 r = Rotate(ShapeRelative(Side::right, kIndex2, 1.0f, 1.0f), bone);
    Require(NearVec(l, r, 1e-5f), "both fists close the same way in their own frames");
}

// The fingers from the controller (ShapeFromController): each finger its own.
void TestFingerShape()
{
    Require(FingerOf(kThumb1) == kFingerThumb && FingerOf(kThumb3) == kFingerThumb && FingerOf(kIndexBase) == kFingerIndex &&
            FingerOf(kIndex3) == kFingerIndex && FingerOf(kMiddle2) == kFingerMiddle && FingerOf(kRingBase) == kFingerRing &&
            FingerOf(kPinky3) == kFingerPinky, "every joint belongs to its finger");
    const FingerShape open = ShapeFromController(0.0f, false, 0.0f, false, 1.0f);
    Require(open.curl[kFingerIndex] < 0.5f && open.fist[kFingerIndex] == 0.0f, "off the trigger, the index points");
    Require(open.curl[kFingerThumb] < 0.5f && open.fist[kFingerThumb] == 0.0f, "a lifted thumb stands up");
    Require(open.curl[kFingerMiddle] == 1.0f && open.fist[kFingerMiddle] == 0.0f, "the rest relaxed, open");
    const FingerShape resting = ShapeFromController(0.0f, true, 0.0f, true, 1.0f);
    Require(resting.curl[kFingerIndex] == 1.0f && resting.fist[kFingerIndex] > 0.0f && resting.fist[kFingerIndex] < 0.5f,
            "on the trigger, the index rests curled");
    Require(resting.fist[kFingerThumb] > 0.0f, "a thumb on a button curls down");
    const FingerShape pulled = ShapeFromController(1.0f, true, 1.0f, true, 1.0f);
    Require(pulled.fist[kFingerIndex] == 1.0f && pulled.fist[kFingerMiddle] == 1.0f && pulled.fist[kFingerRing] == 1.0f &&
            pulled.fist[kFingerPinky] == 1.0f, "the trigger closes the index, the grip the other three");
    const FingerShape half = ShapeFromController(0.5f, true, 0.5f, false, 1.0f);
    Require(half.fist[kFingerIndex] > resting.fist[kFingerIndex] && half.fist[kFingerIndex] < 1.0f &&
            half.fist[kFingerPinky] == 0.5f, "each with its own travel");
    const FingerShape bad = ShapeFromController(std::nanf(""), false, 7.0f, false, 1.0f);
    Require(bad.fist[kFingerIndex] == 0.0f && bad.fist[kFingerMiddle] == 1.0f, "non-finite is nothing, past full is full");
    Require(ShapeFromController(0, false, 0, false, 0.5f).curl[kFingerMiddle] == 0.5f, "the relaxed scale carries through");
}

// The index finger's bones on the rig (ik.skel, run player-20261025-161415).
const IndexBones kLeftIndex{kLeftBase[0], kLeftFirst[0], {0.0389f, -0.0023f, 0.0002f}, {0.0275f, -0.0016f, -0.0001f}};
const Vec3 kRightBase[4] = {{0.0351f, 0.0024f, -0.0259f}, {0.0343f, 0.0054f, -0.0024f},
                            {0.0315f, 0.0059f, 0.0215f}, {0.0281f, -0.0034f, 0.0359f}};
const Vec3 kRightFirst[4] = {{0.0579f, 0, 0}, {0.0596f, 0, 0}, {0.0543f, 0, 0}, {0.0500f, 0, 0}};
const IndexBones kRightIndex{kRightBase[0], kRightFirst[0], {0.0390f, 0, 0}, {0.0276f, 0, 0}};
constexpr float kLean = 15.0f * kRadiansPerDegree;

// The Frame's aim direction in its grip frame: gripInAim, logged 1532 times as 42.8
// degrees about X (docs/FINGER-LASER-SCOPE-2026-11-01.md).
Vec3 AimInGrip() { return Rotate(Conjugate(Normalize(Quaternion{0.365f, 0, 0, 0.931f})), Vec3{0, 0, -1}); }

float DegreesBetween(Vec3 a, Vec3 b)
{
    return std::acos(std::clamp(Dot(a, b) / (Length(a) * Length(b)), -1.0f, 1.0f)) / kRadiansPerDegree;
}

void TestFingerInGrip()
{
    float knuckle = 0.0f;
    const Frame left = MeasuredLeft(knuckle);
    const GripPoint point{};
    const float pointing = PointingCurl(1.0f);
    Require(std::fabs(pointing - 0.25f) < 1e-6f, "a finger off the trigger keeps a quarter of its flexion");
    Pose pose{};
    Require(FingerInGrip(Side::left, left, knuckle, point, kLean, kLeftIndex, pointing, 1.0f, pose), "left finger");
    // finger_geometry.py, the numbers the scope was written from.
    Require(NearVec(pose.position, {-0.028f, -0.110f, -0.083f}, 0.001f), "the left tip, measured");
    const Vec3 along = Rotate(pose.orientation, Vec3{0, 0, -1});
    Require(NearVec(along, {0.045f, -0.917f, -0.396f}, 0.002f), "-Z along the finger, as an aim pose's ray");
    Require(std::fabs(DegreesBetween(along, AimInGrip()) - 24.0f) < 0.3f, "24 degrees from the controller's aim");

    Frame right{};
    float rightKnuckle = 0.0f;
    Quaternion rotation[4];
    const int bases[4] = {kIndexBase, kMiddleBase, kRingBase, kPinkyBase};
    for (int f = 0; f < 4; ++f) { rotation[f] = RelaxedRelative(Side::right, bases[f], 1.0f); }
    Require(MeasureHandFrame(Side::right, kRightBase, rotation, kRightFirst, right, rightKnuckle), "right frame");
    Pose mirrored{};
    Require(FingerInGrip(Side::right, right, rightKnuckle, point, kLean, kRightIndex, pointing, 1.0f, mirrored), "right finger");
    Require(NearVec(mirrored.position, {0.027f, -0.110f, -0.083f}, 0.001f), "the right tip, across the grip's X");
    Require(DegreesBetween(Rotate(mirrored.orientation, Vec3{0, 0, -1}), AimInGrip()) < 25.0f, "the same way on that hand");

    // **Seated as the hands are**, the finger lies along the controller's aim on
    // both hands, which is why the lasers can take that aim and a weapon's wrist
    // angle. The wearer's reading on PreyVR-127 with pose.grip: 3.5 and 2.6 degrees.
    constexpr float kSeated = kTubeLeanDegrees * kRadiansPerDegree;
    Pose seatedLeft{}, seatedRight{};
    Require(FingerInGrip(Side::left, left, knuckle, point, kSeated, kLeftIndex, pointing, 1.0f, seatedLeft) &&
                FingerInGrip(Side::right, right, rightKnuckle, point, kSeated, kRightIndex, pointing, 1.0f, seatedRight),
            "both fingers, seated");
    Require(DegreesBetween(Rotate(seatedLeft.orientation, Vec3{0, 0, -1}), AimInGrip()) < 3.0f &&
                DegreesBetween(Rotate(seatedRight.orientation, Vec3{0, 0, -1}), AimInGrip()) < 4.0f,
            "the finger on the aim, within the logged angle");
    Require(NearVec(seatedRight.position, {0.027f, -0.067f, -0.121f}, 0.001f), "the tip, seated");

    // The thumb side is +Y, as it is up the handle of an aim pose.
    const Vec3 up = Rotate(pose.orientation, Vec3{0, 1, 0});
    const Vec3 thumb = Rotate(FrameToFrame(left, GripHandFrame(Side::left, kLean)), left.thumb);
    Require(Dot(up, thumb) > 0.9f && std::fabs(Dot(up, along)) < 1e-4f, "+Y to the thumb, across the finger");

    // By another road: place the wrist on the grip, then walk the joints down as
    // the IK propagates them. It has to land on the same tip.
    const Quaternion handInGrip = FrameToFrame(left, GripHandFrame(Side::left, kLean));
    Pose joint{handInGrip, Scale(Rotate(handInGrip, GripPointInWrist(left, knuckle, point)), -1.0f)};
    const Vec3 offsets[4] = {kLeftIndex.base, kLeftIndex.first, kLeftIndex.second, kLeftIndex.third};
    for (int j = 0; j < 4; ++j) {
        joint = Compose(joint, Pose{ShapeRelative(Side::left, kIndexBase + j, pointing, 0.0f), offsets[j]});
    }
    joint = Compose(joint, Pose{Quaternion{}, Vec3{kFingertipLength, 0, 0}});
    Require(NearVec(joint.position, pose.position, 1e-5f), "the tip the drawn joints reach");

    // What moves it, and what does not.
    Pose twice{};
    Require(FingerInGrip(Side::left, left, knuckle, point, kLean, kLeftIndex, pointing, 2.0f, twice) &&
                NearVec(twice.position, Scale(pose.position, 2.0f), 1e-5f),
            "scale takes model units to metres");
    Pose relaxed{};
    Require(FingerInGrip(Side::left, left, knuckle, point, kLean, kLeftIndex, 1.0f, 1.0f, relaxed) &&
                !NearVec(relaxed.position, pose.position, 0.01f),
            "Fingers Follow off: the finger as it is drawn then, relaxed");
    GripPoint moved{};
    moved.outOfPalm = 0.05f;
    Pose seated{};
    // The grip point further out of the palm puts the whole hand further behind it.
    Require(FingerInGrip(Side::left, left, knuckle, moved, kLean, kLeftIndex, pointing, 1.0f, seated) &&
                std::fabs(seated.position.x - (pose.position.x - 0.015f)) < 0.001f,
            "it follows how the hand sits on the controller (pose.grip)");

    // No length between knuckle and tip: no direction to point in.
    Require(!FingerInGrip(Side::left, left, knuckle, point, kLean, IndexBones{{}, {}, {}, {-kFingertipLength, 0, 0}}, 0.0f, 1.0f, seated),
            "a degenerate finger is refused");
}

int main()
{
    TestNames();
    TestFingerInGrip();
    TestFingerShape();
    TestGripHandFrame();
    TestMeasuredHandFrame();
    TestAnatomicalMapping();
    TestRelaxedPose();
    TestHandShape();
    TestGripPoint();
    TestSoftReach();
    TestRollToMatch();
    TestSlerp();
    std::cout << "hand_pose: all tests passed\n";
    return 0;
}
