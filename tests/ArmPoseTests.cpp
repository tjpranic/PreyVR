#include "preyvr/ArmPose.h"
#include "TestGeometry.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace preyvr;
using namespace preyvr::armpose;

namespace {

using preyvr::test::Require;

using preyvr::test::kDeg;
float Dist(Vec3 a, Vec3 b) { return Length(Sub(a, b)); }
bool NearVec(Vec3 a, Vec3 b, float e = 1e-3f) { return Dist(a, b) <= e; }
using preyvr::test::AboutX;
using preyvr::test::AboutY;

void TestTorsoFrame()
{
    const TorsoFrame t = TorsoFromYaw(0.0f);
    Require(NearVec(t.forward, {0, 0, -1}) && NearVec(t.right, {1, 0, 0}), "yaw 0 faces -Z, right is +X");
    const TorsoFrame l = TorsoFromYaw(90.0f * kDeg);
    Require(NearVec(l.forward, {-1, 0, 0}) && NearVec(l.right, {0, 0, -1}), "positive yaw turns left");
    Require(std::fabs(YawOf(l.forward) - 90.0f * kDeg) < 1e-4f, "YawOf inverts TorsoFromYaw");
}

void TestHeadYaw()
{
    for (float yaw : {-150.0f, -30.0f, 0.0f, 45.0f, 170.0f}) {
        for (float pitch : {-89.9f, -60.0f, 0.0f, 50.0f, 89.9f}) {
            const Quaternion q = Multiply(AboutY(yaw * kDeg), AboutX(pitch * kDeg));
            const float got = HeadYaw(q);
            float diff = std::fabs(got - yaw * kDeg);
            if (diff > 3.14159265f) { diff = 2 * 3.14159265f - diff; }
            Require(diff < 1e-3f, "head yaw survives any pitch, even straight down or up");
        }
    }
}

void TestTorsoYaw()
{
    TorsoYaw t;
    Require(std::fabs(t.Update(0.3f, 0.01f, 0.5f, 1.0f) - 0.3f) < 1e-6f, "the first sample sets the torso");
    // A quick head turn inside the deadzone barely moves the torso.
    const float after = t.Update(0.6f, 0.011f, 0.5f, 1.0f);
    Require(after > 0.3f && after < 0.31f, "inside the deadzone the torso only relaxes");
    // Past the deadzone it is dragged along, never further than the deadzone.
    const float dragged = t.Update(1.5f, 0.011f, 0.5f, 1.0f);
    Require(1.5f - dragged <= 0.5f + 1e-4f && 1.5f - dragged > 0.48f, "past the deadzone the torso follows");
    // Held still, it relaxes onto the head.
    for (int i = 0; i < 1000; ++i) { t.Update(1.5f, 0.011f, 0.5f, 1.0f); }
    Require(std::fabs(t.yaw - 1.5f) < 1e-3f, "the torso relaxes onto a still head");
    // Wrap-around: a head at -179 degrees from a torso at +179 is 2 degrees away.
    TorsoYaw w;
    w.Update(179.0f * kDeg, 0.01f, 0.5f, 0.0f);
    w.Update(-179.0f * kDeg, 0.01f, 0.5f, 0.0f);
    Require(std::fabs(w.yaw - 179.0f * kDeg) < 1e-4f, "yaw differences wrap");
}

void TestShoulder()
{
    const Body body;
    const TorsoFrame t = TorsoFromYaw(0.0f);
    const Vec3 eye{0, 1.6f, 0};
    const Vec3 r = ShoulderTarget(eye, t, +1, body), l = ShoulderTarget(eye, t, -1, body);
    Require(NearVec(r, {0.185f, 1.37f, 0.09f}), "right shoulder: out, below and behind the eye");
    Require(NearVec(l, {-0.185f, 1.37f, 0.09f}), "left shoulder mirrors");
    // The clavicle reaches its target within its stretch, and stops short beyond it.
    const Vec3 root{0.03f, 1.38f, 0.025f};
    const Vec3 within = PlaceShoulder(root, 0.17f, r, 0.15f);
    Require(NearVec(within, r), "a target within the clavicle's stretch is reached");
    const Vec3 far{0.5f, 1.38f, 0.025f};
    const Vec3 limited = PlaceShoulder(root, 0.17f, far, 0.15f);
    Require(std::fabs(Dist(limited, root) - 0.17f * 1.15f) < 1e-4f, "beyond its stretch the clavicle stops");
    Require(std::fabs(limited.y - root.y) < 1e-4f && limited.x > root.x, "towards the target");
}

void TestLevelEyeIgnoresHeadRotation()
{
    const float torso = 25.0f * kDeg;
    const Vec3 neck{0.3f, 1.5f, -0.4f};
    // Facing the torso, level: the eyes themselves.
    const Pose level{AboutY(torso), Sub(neck, Rotate(AboutY(torso), kNeckFromEye))};
    Require(NearVec(LevelEye(level, torso), level.position, 1e-6f), "facing the torso: the head itself");
    // Turn and tip the head about a fixed neck: the eyes swing, the point does not.
    for (float yaw : {-70.0f, -30.0f, 0.0f, 45.0f, 80.0f}) {
        for (float pitch : {-50.0f, -10.0f, 0.0f, 30.0f}) {
            const Quaternion q = Normalize(Multiply(AboutY(torso + yaw * kDeg), AboutX(pitch * kDeg)));
            const Pose head{q, Sub(neck, Rotate(q, kNeckFromEye))};
            Require(NearVec(LevelEye(head, torso), level.position, 1e-5f), "a glance does not move it");
        }
    }
    // Walking moves it one for one.
    const Pose walked{level.orientation, Add(level.position, Vec3{0.4f, 0.0f, -0.2f})};
    Require(NearVec(LevelEye(walked, torso), Add(level.position, Vec3{0.4f, 0.0f, -0.2f}), 1e-6f), "a step does");
    Require(NearVec(LevelEye(Pose{Quaternion{NAN, 0, 0, 1}, {1, 2, 3}}, torso), {1, 2, 3}), "no orientation: the head");
}

// The shoulder hangs from the neck: turning or tipping the head swings the eyes
// about the neck by centimetres, and the shoulders not at all.
void TestShoulderFromHead()
{
    const Body body;
    const float torso = 20.0f * kDeg;
    // Level and facing the torso, the head puts the shoulder where its eye does.
    const Pose level{AboutY(torso), Vec3{0.1f, 1.6f, -0.2f}};
    for (const Side side : {+1, -1}) {
        const Vec3 fromEye = ShoulderTarget(level.position, TorsoFromYaw(torso), side, body);
        Require(NearVec(ShoulderFromHead(level, torso, side, body), fromEye, 1e-4f),
                "head level and facing the torso: the shoulder is where the eye puts it");
    }
    const Vec3 neck = Add(level.position, Rotate(level.orientation, kNeckFromEye));
    const Vec3 right = ShoulderFromHead(level, torso, +1, body);
    const auto about = [&](const Quaternion& local) {
        const Quaternion q = Multiply(level.orientation, local);
        return Pose{q, Sub(neck, Rotate(q, kNeckFromEye))};
    };
    for (const Quaternion local : {AboutX(-60.0f * kDeg), AboutX(-30.0f * kDeg), AboutX(25.0f * kDeg),
                                   AboutY(30.0f * kDeg), AboutY(-45.0f * kDeg),
                                   Multiply(AboutY(30.0f * kDeg), AboutX(-45.0f * kDeg))}) {
        const Pose turned = about(local);
        Require(Dist(turned.position, level.position) > 0.02f, "turning the head about the neck moves the eyes");
        Require(NearVec(ShoulderFromHead(turned, torso, +1, body), right, 1e-4f),
                "and leaves the shoulder where it was");
    }
    // What the arm did before: looking down 45 degrees dropped an eye-anchored
    // shoulder about 8 cm, towards the hands.
    const Pose down = about(AboutX(-45.0f * kDeg));
    const Vec3 fromEye = ShoulderTarget(down.position, TorsoFromYaw(torso), +1, body);
    Require(right.y - fromEye.y > 0.07f, "the eye-anchored shoulder dropped with the gaze");
    // A lean moves the neck, and the shoulder goes with it.
    const Vec3 lean{0.0f, -0.05f, -0.2f};
    const Pose leaned{level.orientation, Add(level.position, lean)};
    Require(NearVec(Sub(ShoulderFromHead(leaned, torso, +1, body), right), lean, 1e-4f), "a lean moves the shoulder");
}

void TestArmLength()
{
    const Body body;
    Require(std::fabs(ArmLength(body) - 0.56f) < 1e-6f, "the default arm is 56 cm, shoulder to wrist");
    const Body shorter = WithArmLength(body, 0.50f);
    Require(std::fabs(ArmLength(shorter) - 0.50f) < 1e-6f &&
                std::fabs(shorter.upper / shorter.fore - body.upper / body.fore) < 1e-5f,
            "a new length keeps the segments' split");
    Require(shorter.shoulderOut == body.shoulderOut && shorter.armStretch == body.armStretch,
            "only the segments change");
    Require(ArmLength(WithArmLength(body, -1.0f)) == ArmLength(body) &&
                ArmLength(WithArmLength(body, std::nanf(""))) == ArmLength(body),
            "a length that is no length changes nothing");
    // A player's straight 50 cm arm: the default arm folds to fit it, an arm
    // of the player's length lies straight.
    const Vec3 shoulder{0.185f, 1.37f, 0.09f};
    const Vec3 wrist{shoulder.x, shoulder.y, shoulder.z - 0.50f};
    const auto offAxis = [&](const Body& b) {
        const Vec3 elbow = SolveElbow(shoulder, wrist, b.upper, b.fore, Vec3{0, -1, 0}, b.armStretch).elbow;
        return std::hypot(elbow.x - shoulder.x, elbow.y - shoulder.y);
    };
    // The solve keeps 0.5% slack, so a straight arm sits about 2.5 cm off the
    // line (169 degrees); the default arm folds to 12.6 cm off it.
    Require(offAxis(body) > 0.10f, "the default arm bends a 50 cm player's straight arm");
    Require(offAxis(shorter) < 0.03f, "the player's own length draws it straight");
}

// The torso follows the neck up and down; the camera's joint never moves.
void TestTorsoLift()
{
    // Tipping the head about the neck leaves the level eye's height alone.
    const Pose level{AboutY(0.3f), Vec3{0, 1.2f, 0}};
    const Vec3 neck = Add(level.position, Rotate(level.orientation, kNeckFromEye));
    for (const float pitch : {-60.0f, -20.0f, 30.0f}) {
        const Quaternion q = Multiply(level.orientation, AboutX(pitch * kDeg));
        const Pose tipped{q, Sub(neck, Rotate(q, kNeckFromEye))};
        Require(std::fabs(LevelEyeHeight(tipped) - 1.2f) < 1e-4f, "tipping the head does not lift the torso");
    }
    Require(std::fabs(LevelEyeHeight(Pose{Quaternion{}, Vec3{0, 1.65f, 0}}) - 1.65f) < 1e-5f, "level: the eye's own height");

    Require(TorsoLift(0.45f, 0.40f, 0.15f) == 0.40f && TorsoLift(-0.4f, 0.40f, 0.15f) == -0.15f &&
                TorsoLift(0.1f, 0.40f, 0.15f) == 0.1f && TorsoLift(std::nanf(""), 0.4f, 0.15f) == 0.0f,
            "the lift is the neck's rise, within its limits");
    const float none = std::numeric_limits<float>::infinity();
    Require(TorsoLift(0.6f, none, none) == 0.6f && TorsoLift(-0.5f, none, none) == -0.5f,
            "with no limits the torso follows the neck all the way");

    // A rig shaped like Prey's 101-joint one: root 0 -> hips 1 -> spine1 2 ->
    // spine2 3 -> spine3 4 -> spine4 5 -> neck 6 -> head 7 (the camera) -> headEnd 8;
    // spine4 -> clavicle 9 -> arm 10; spine2 -> a helper 11; root -> a leg 12.
    const int parent[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 5, 9, 3, 0};
    const int spine[kSpineJoints] = {2, 3, 4, 5};
    signed char classes[13]{};
    LiftClasses(parent, 13, spine, 7, classes);
    const signed char expected[] = {0, 0, 1, 2, 3, 4, 4, 0, 0, 4, 4, 2, 0};
    for (int j = 0; j < 13; ++j) { Require(classes[j] == expected[j], "each joint takes its spine joint's share"); }
    // No camera joint: the head rides the neck.
    LiftClasses(parent, 13, spine, -1, classes);
    Require(classes[7] == 4 && classes[8] == 4, "with no camera joint the head is lifted too");
    // A parent table with a cycle does not hang.
    const int looped[] = {1, 0};
    LiftClasses(looped, 2, spine, -1, classes);
    Require(classes[0] == 0 && classes[1] == 0, "a cyclic table lifts nothing");

    // Prey's spine heights (arms.joints, 2026-10-19), from a base at 0.95 m.
    const float heights[kSpineJoints] = {1.044f, 1.166f, 1.309f, 1.423f};
    float shares[kSpineJoints]{};
    Require(LiftShares(0.95f, heights, shares), "a rising spine shares the lift");
    Require(shares[0] > 0.15f && shares[0] < shares[1] && shares[1] < shares[2] && shares[2] < shares[3] &&
                shares[3] == 1.0f, "the share grows up the spine, the chest takes it all");
    const float flat[kSpineJoints] = {1.0f, 1.0f, 1.0f, 1.0f};
    Require(!LiftShares(1.0f, flat, shares), "a spine that does not rise refuses");
}

void TestElbow()
{
    const Vec3 s{0.185f, 1.37f, 0.09f};
    const TorsoFrame t = TorsoFromYaw(0.0f);
    const Vec3 pole = BodyPole(t, +1);
    Require(pole.y < -0.7f && pole.x > 0.3f && pole.z > 0.1f, "the anatomical elbow points down, out and back");
    // A reachable wrist: both segments keep their length, the elbow towards the pole.
    const Vec3 w{0.2f, 1.15f, -0.25f};
    const ArmSolution a = SolveElbow(s, w, 0.285f, 0.275f, pole, 1.1f);
    Require(std::fabs(Dist(a.elbow, s) - 0.285f) < 1e-4f && std::fabs(Dist(a.elbow, w) - 0.275f) < 1e-4f,
            "the segments meet at the elbow");
    Require(a.scale == 1.0f && !a.straight, "a reachable wrist needs no stretch");
    const Vec3 mid{(s.x + w.x) / 2, (s.y + w.y) / 2, (s.z + w.z) / 2};
    Require(a.elbow.y < mid.y, "the elbow hangs below the arm's axis");
    // Beyond reach the segments lengthen and stay connected.
    const Vec3 far{0.2f, 1.37f, -0.50f};
    const ArmSolution b = SolveElbow(s, far, 0.285f, 0.275f, pole, 1.1f);
    Require(b.scale > 1.0f && b.scale < 1.1f && !b.straight, "a little beyond reach: stretched, not straight");
    Require(std::fabs(Dist(b.elbow, far) - 0.275f * b.scale) < 1e-4f, "stretched segments still meet the wrist");
    const ArmSolution c = SolveElbow(s, Vec3{0.2f, 1.37f, -0.9f}, 0.285f, 0.275f, pole, 1.1f);
    Require(c.straight, "far beyond reach is reported");
}

void TestWristAngles()
{
    const Vec3 length{0, 0, -1}, palm{0, -1, 0};
    Require(std::fabs(WristFlexionDeg(length, length, palm)) < 1e-3f, "a straight wrist does not bend");
    const float c = std::cos(30 * kDeg), s = std::sin(30 * kDeg);
    // Flexed 30 degrees: the forearm, seen from the hand, rises out of the palm side.
    Require(std::fabs(WristFlexionDeg(Vec3{0, s, -c}, length, palm) - 30.0f) < 1e-2f, "flexion towards the palm");
    Require(std::fabs(WristFlexionDeg(Vec3{0, -s, -c}, length, palm) + 30.0f) < 1e-2f, "extension is negative");
    // Right hand, palm down: the thumb is on length x palm = -X... (0,0,-1) x (0,-1,0) = (-1,0,0).
    // Deviated 20 degrees towards the thumb, the forearm leans away from it, to +X.
    const float c2 = std::cos(20 * kDeg), s2 = std::sin(20 * kDeg);
    Require(std::fabs(WristRadialDeg(Vec3{s2, 0, -c2}, length, palm, +1) - 20.0f) < 1e-2f, "radial is positive");
    Require(std::fabs(WristRadialDeg(Vec3{-s2, 0, -c2}, length, palm, +1) + 20.0f) < 1e-2f, "ulnar is negative");
    Require(std::fabs(WristRadialDeg(Vec3{-s2, 0, -c2}, length, palm, -1) - 20.0f) < 1e-2f, "the left mirrors");
}

Vec3 ElbowFor(const Vec3& s, const Vec3& w, const Vec3& length, const Vec3& palm)
{
    const Vec3 pole = ChooseElbowPole(s, w, 0.285f, 0.275f, 1.1f, length, palm, +1, BodyPole(TorsoFromYaw(0), +1),
                                      WristLimits{});
    return SolveElbow(s, w, 0.285f, 0.275f, pole, 1.1f).elbow;
}

void TestElbowFollowsHand()
{
    const Vec3 s{0.185f, 1.37f, 0.09f};
    const Vec3 w{0.2f, 1.15f, -0.25f};   // in front of the belly
    // Thumb up, palm facing in: the forearm runs level behind the hand, the
    // elbow at the body's side, not inside it.
    const Vec3 thumbUp = ElbowFor(s, w, Vec3{0, 0, -1}, Vec3{-1, 0, 0});
    Require(std::fabs(thumbUp.y - w.y) < 0.06f, "thumb up: a level forearm");
    Require(thumbUp.x > 0.15f, "thumb up: the elbow at the side, outside the body");
    // Palm down: the forearm close to the vertical plane of the hand -- the
    // elbow behind the wrist, out only by the wrist's resting ulnar deviation.
    const Vec3 palmDown = ElbowFor(s, w, Vec3{0, 0, -1}, Vec3{0, -1, 0});
    Require(std::fabs(palmDown.x - w.x) < 0.07f, "palm down: the elbow behind the wrist");
    const Vec3 f2{(w.x - palmDown.x) / 0.275f, (w.y - palmDown.y) / 0.275f, (w.z - palmDown.z) / 0.275f};
    const float r2 = WristRadialDeg(f2, Vec3{0, 0, -1}, Vec3{0, -1, 0}, +1);
    Require(r2 < 0.0f && r2 > -15.0f, "palm down: a little ulnar");
    for (const Vec3& e : {thumbUp, palmDown}) {
        Require(std::fabs(Dist(e, s) - 0.285f) < 1e-3f, "the elbow is on the arm");
    }
    const Vec3 f1{(w.x - thumbUp.x) / 0.275f, (w.y - thumbUp.y) / 0.275f, (w.z - thumbUp.z) / 0.275f};
    const float radial = WristRadialDeg(f1, Vec3{0, 0, -1}, Vec3{-1, 0, 0}, +1);
    Require(radial < 0.0f && radial > -15.0f, "thumb up: a little ulnar, as round a controller");
    // Continuity: a small turn of the hand moves the elbow a little.
    const float a = 0.05f;
    const Vec3 turned = ElbowFor(s, w, Vec3{0, 0, -1}, Vec3{-std::cos(a), -std::sin(a), 0});
    Require(Dist(turned, thumbUp) < 0.03f, "the elbow is continuous in the hand");
    // A hand that cannot decide (pointing along the arm) leaves the anatomical elbow.
    const Vec3 axis{w.x - s.x, w.y - s.y, w.z - s.z};
    const float l = Length(axis);
    const Vec3 along = ElbowFor(s, w, Vec3{axis.x / l, axis.y / l, axis.z / l}, Vec3{-1, 0, 0});
    Require(along.y < (s.y + w.y) / 2, "no information: the elbow hangs");
}

void TestFromTo()
{
    const Vec3 a{1, 0, 0}, b{0, 0.6f, 0.8f};
    Require(NearVec(Rotate(FromTo(a, b), a), b, 1e-5f), "FromTo turns from onto to");
    Require(NearVec(Rotate(FromTo(a, Vec3{-1, 0, 0}), a), Vec3{-1, 0, 0}, 1e-5f), "and the half turn");
    const Vec3 s = SmoothDirection(Vec3{1, 0, 0}, Vec3{0, 1, 0}, 0.01f, 0.05f);
    Require(s.x > s.y && s.y > 0.1f && std::fabs(Length(s) - 1) < 1e-5f, "smoothing moves part of the way");
}

} // namespace

int main()
{
    TestTorsoFrame();
    TestHeadYaw();
    TestTorsoYaw();
    TestShoulder();
    TestLevelEyeIgnoresHeadRotation();
    TestShoulderFromHead();
    TestArmLength();
    TestTorsoLift();
    TestElbow();
    TestWristAngles();
    TestElbowFollowsHand();
    TestFromTo();
    std::cout << "arm_pose: all tests passed\n";
    return 0;
}
