#include "preyvr/ArmPose.h"
#include "TestGeometry.h"
#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

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

void TestCapsuleDistance()
{
    const Capsule c{{-0.05f, 0, 0}, {0.05f, 0, 0}, 0.1f};
    Require(std::fabs(CapsuleDistance(c, {0, 0, 0}) + 0.1f) < 1e-6f, "on its segment: a radius deep");
    Require(std::fabs(CapsuleDistance(c, {0.02f, 0.3f, 0}) - 0.2f) < 1e-6f, "beside the segment: from the axis");
    Require(std::fabs(CapsuleDistance(c, {0.35f, 0, 0}) - 0.2f) < 1e-6f, "past an end: from that end's ball");
    Require(std::fabs(CapsuleDistance(Capsule{{1, 1, 1}, {1, 1, 1}, 0.1f}, {1, 1.3f, 1}) - 0.2f) < 1e-6f,
            "no length: a ball");
    Require(CapsuleDistance(Capsule{c.a, c.b, 0.0f}, {0, 0, 0}) > 1e30f, "no radius: never near");
}

void TestPlaceCapsule()
{
    // A joint turned so its right is -Z, forward +X and up +Y.
    const Capsule c = PlaceCapsule({1, 2, 3}, {0, 0, -1}, {1, 0, 0}, {0, 1, 0}, CapsuleSpec{0.03f, -0.04f, 0.05f, 0.12f});
    Require(NearVec(c.a, {1.03f, 1.96f, 3.05f}, 1e-5f) && NearVec(c.b, {1.03f, 1.96f, 2.95f}, 1e-5f),
            "centred forward and up from the joint, laid along its right");
    Require(c.radius == 0.12f, "with its radius");
    // The defaults on Morgan's standing chest (spine4, model x right, y forward, z up):
    // the shoulder the arm hangs from stays outside it.
    const TorsoShape shape{};
    const Capsule chest = PlaceCapsule({0, -0.0789f, 1.4231f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, shape.part[kChest]);
    Require(CapsuleDistance(chest, {0.189f, -0.091f, 1.470f}) > 0.05f, "the drawn shoulder is clear of the chest");
    Require(std::string(TorsoPartName(kHips)) == "hips" && std::string(TorsoPartName(9)) == "none", "parts by name");
}

void TestMeasureArm()
{
    // An arm hanging straight down from (0.2, 0, 0) past a capsule at the origin.
    const Capsule torso[2] = {{{-0.05f, -0.30f, 0}, {0.05f, -0.30f, 0}, 0.12f}, {{0, 0, 0}, {0, 0, 0}, 0.0f}};
    const ArmThickness thick{};
    const Vec3 shoulder{0.2f, 0, 0}, elbow{0.2f, -0.30f, 0}, wrist{0.2f, -0.58f, 0};
    ArmClearance a = MeasureArm(shoulder, elbow, wrist, torso, 2, thick);
    Require(a.valid && a.deepest == kElbowAt, "the elbow is the nearest the capsule");
    Require(std::fabs(a.clear[kElbowAt] - (0.15f - 0.12f - thick.elbow)) < 1e-5f, "its surface from the capsule's");
    Require(a.part[kElbowAt] == 0 && a.part[kWristAt] == 0, "measured against the capsule with a radius");
    Require(NearVec(a.point[kUpper30], {0.2f, -0.09f, 0}, 1e-5f) && NearVec(a.point[kFore70], {0.2f, -0.496f, 0}, 1e-5f),
            "sampled along the upper arm from the shoulder and the forearm from the elbow");
    Require(a.radius[kUpper55] == thick.upper && a.radius[kFore20] == thick.fore && a.radius[kWristAt] == thick.wrist,
            "each as thick as its part");
    // The elbow pushed in.
    a = MeasureArm(shoulder, Vec3{0.1f, -0.30f, 0}, wrist, torso, 2, thick);
    Require(a.deepest == kElbowAt && a.Worst() < -0.1f, "an elbow in the torso is the deepest sample");
    Require(!MeasureArm(shoulder, elbow, wrist, torso + 1, 1, thick).valid, "no capsule with a radius: not measured");
    Require(std::string(ArmSampleName(kFore45)) == "fore45", "samples by name");
}

// The default capsules on Morgan's standing spine, in OpenXR from the centre
// eye (x right, y up, z back; the ik.skel stand dump, PreyVR-84).
struct StandingTorso {
    Capsule part[kTorsoParts]{};
    StandingTorso()
    {
        const Vec3 joints[kTorsoParts] = {{0, -0.277f, 0.0789f}, {0, -0.391f, 0.0644f}, {0, -0.534f, 0.0398f},
                                          {0, -0.660f, -0.0056f}};
        const TorsoShape shape{};
        for (int k = 0; k < kTorsoParts; ++k) { part[k] = PlaceCapsule(joints[k], {1, 0, 0}, {0, 0, -1}, {0, 1, 0}, shape.part[k]); }
    }
};

void TestArmClear()
{
    const Capsule ball{{0, -0.3f, 0}, {0, -0.3f, 0}, 0.1f};
    ArmObstacle o{&ball, 1, ArmThickness{}, 0.01f, 0.01f};
    // A forearm ending in the ball: only the wrist is in it, and no elbow moves the wrist.
    const Vec3 s{0.45f, 0.0f, 0}, e{0.45f, -0.3f, 0}, w{0.05f, -0.3f, 0};
    Require(MeasureArm(s, e, w, &ball, 1, o.thickness).Worst() < 0.0f, "the wrist is in the ball");
    const float least = ArmClear(s, e, w, o);
    Require(least > 0.0f && least < 0.1f, "the arm's clearance leaves the wrist out");
    o.count = 0;
    Require(ArmClear(s, e, w, o) > 1e30f, "no capsule: far");
}

// The torso term moves an elbow the chooser put in the body out of it. The
// hand pose is from an offline sweep (arm 60 cm, hand forward and down, palm
// turned in): without the torso the elbow lands 5 cm from the midline at the
// belly.
void TestChooserAvoidsTorso()
{
    const StandingTorso torso;
    const Body body = WithArmLength(Body{}, 0.60f);
    const Vec3 s = ShoulderFromHead(Pose{}, 0.0f, 1, body);
    const Vec3 w{0.1408f, -0.3914f, -0.2277f}, length{0.0405f, -0.4513f, -0.8915f}, palm{-0.7596f, -0.5935f, 0.2660f};
    const ArmObstacle o{torso.part, kTorsoParts, ArmThickness{}, 0.01f, 0.01f};
    const Vec3 bodyPole = BodyPole(TorsoFromYaw(0.0f), 1);
    const WristLimits limits{};
    const Vec3 plain = ChooseElbowPole(s, w, body.upper, body.fore, body.armStretch, length, palm, 1, bodyPole, limits);
    const Vec3 kept = ChooseElbowPole(s, w, body.upper, body.fore, body.armStretch, length, palm, 1, bodyPole, limits,
                                      nullptr, &o);
    const auto clear = [&](Vec3 pole) {
        return ArmClear(s, SolveElbow(s, w, body.upper, body.fore, pole, body.armStretch).elbow, w, o);
    };
    Require(clear(plain) < -0.05f, "without the torso the elbow is in the body");
    Require(clear(kept) >= 0.0f, "with it, out of it");
    const ArmObstacle none{torso.part, 0, ArmThickness{}, 0.01f, 0.01f};
    Require(NearVec(ChooseElbowPole(s, w, body.upper, body.fore, body.armStretch, length, palm, 1, bodyPole, limits,
                                    nullptr, &none), plain, 1e-6f),
            "no capsule: the chooser as it was");
}

// One ball across the circle of elbows: the shoulder at the origin, the wrist
// 30 cm ahead, both segments 30 cm, so the elbows are on a circle of radius
// 26 cm round -Z; the ball sits on the one towards +X.
void TestTrackElbowPole()
{
    const Vec3 s{0, 0, 0}, w{0, 0, -0.3f};
    const float upper = 0.3f, fore = 0.3f, stretch = 1.1f;
    const Capsule ball{{0.26f, 0, -0.15f}, {0.26f, 0, -0.15f}, 0.08f};
    const ArmObstacle o{&ball, 1, ArmThickness{0.01f, 0.01f, 0.01f, 0.01f}, 0.01f, 0.01f};
    const auto at = [](float deg) { return Vec3{std::cos(deg * kDeg), std::sin(deg * kDeg), 0}; };   // 0 = +X, towards the ball
    const auto angleOf = [](Vec3 pole) { return std::atan2(pole.y, pole.x) / kDeg; };
    const auto clear = [&](Vec3 pole) { return ArmClear(s, SolveElbow(s, w, upper, fore, pole, stretch).elbow, w, o); };
    Require(clear(at(0)) < 0.0f && clear(at(90)) > 0.05f, "the fixture: the elbow towards +X is in the ball");

    TrackedPole t = TrackElbowPole(s, w, upper, fore, stretch, at(120), nullptr, 0.1f,
                                   ArmObstacle{&ball, 0, o.thickness, o.margin, o.wall});
    Require(t.how == Track::kFree && NearVec(t.pole, at(120), 1e-6f), "no torso: the wanted elbow");
    const Vec3 drawn = at(90);
    t = TrackElbowPole(s, w, upper, fore, stretch, at(120), &drawn, 0.1f, o);
    Require(t.how == Track::kFree && NearVec(t.pole, at(120), 1e-6f), "nothing in the way: the wanted elbow, however far");

    // Wanted in the ball: along the circle to its edge, at most a step at a time.
    t = TrackElbowPole(s, w, upper, fore, stretch, at(0), &drawn, 0.2f, o);
    Require(t.how == Track::kParked && std::fabs(angleOf(t.pole) - (90.0f - 0.2f / kDeg)) < 0.05f,
            "the wanted elbow in the body: towards it by one step");
    t = TrackElbowPole(s, w, upper, fore, stretch, at(0), &drawn, 3.0f, o);
    Require(t.how == Track::kParked && clear(t.pole) >= o.margin && clear(t.pole) < o.margin + 0.01f &&
                angleOf(t.pole) > 0.0f,
            "with room to go: at the ball's edge on this side, clear by the margin");

    // The body moved onto the drawn elbow: out to the nearest clear one.
    const Vec3 inside = at(10);
    t = TrackElbowPole(s, w, upper, fore, stretch, at(10), &inside, 0.0f, o);
    Require(t.how == Track::kPushed && clear(t.pole) >= o.margin && angleOf(t.pole) > 10.0f,
            "pushed out of the body, on the near side, however small the step");

    // Wanted on the far side of the ball: round the other way, a step at a time.
    const Vec3 above = at(60);
    t = TrackElbowPole(s, w, upper, fore, stretch, at(-60), &above, 0.2f, o);
    Require(t.how == Track::kAround && std::fabs(angleOf(t.pole) - (60.0f + 0.2f / kDeg)) < 0.05f,
            "never through the body: round the other way");
}

// Sixty seconds of the hands wandering at the chest, with the wrist turning
// through more than it can: frame by frame as the DLL runs it (the chooser with
// the last pole, its 60 ms smoothing, then the tracking). The drawn arm is never
// in the torso, and the elbow moves no further in a frame than it does without
// the torso (offline, 2026-10-29: 5.1 cm against 5.6).
void TestTrackingKeepsTheArmOut()
{
    const StandingTorso torso;
    const Body body = WithArmLength(Body{}, 0.60f);
    const Vec3 s = ShoulderFromHead(Pose{}, 0.0f, 1, body);
    const ArmObstacle o{torso.part, kTorsoParts, ArmThickness{}, 0.01f, 0.01f};
    const Vec3 bodyPole = BodyPole(TorsoFromYaw(0.0f), 1);
    const WristLimits limits{};
    const float dt = 1.0f / 90.0f;
    const auto turn = [](Vec3 v, Vec3 axis, float a) {
        return Rotate(Quaternion{axis.x * std::sin(a / 2), axis.y * std::sin(a / 2), axis.z * std::sin(a / 2), std::cos(a / 2)}, v);
    };
    Vec3 pole{}, drawn{}, last{};
    bool have = false;
    int inside = 0;
    float most = 0.0f;
    for (int i = 0; i < 90 * 60; ++i) {
        const float t = static_cast<float>(i) * dt;
        const Vec3 w{0.11f + 0.06f * std::sin(0.9f * t) + 0.02f * std::sin(2.3f * t + 1),
                     -0.40f + 0.09f * std::sin(0.7f * t + 0.5f) + 0.02f * std::sin(3.1f * t),
                     -0.17f + 0.07f * std::sin(1.1f * t + 2) + 0.02f * std::sin(2.7f * t + 0.3f)};
        Vec3 length{0, 0, -1}, palm{-1, 0, 0};
        const float yaw = 0.6f * std::sin(0.5f * t), pitch = 0.7f * std::sin(0.37f * t + 1), roll = 0.8f * std::sin(0.29f * t + 2);
        length = turn(length, {0, 1, 0}, yaw);
        palm = turn(palm, {0, 1, 0}, yaw);
        const Vec3 side = Cross(length, Vec3{0, 1, 0});
        const Vec3 across = Scale(side, 1.0f / Length(side));
        length = turn(length, across, pitch);
        palm = turn(palm, across, pitch);
        palm = turn(palm, length, roll);
        Vec3 chosen = ChooseElbowPole(s, w, body.upper, body.fore, body.armStretch, length, palm, 1, bodyPole, limits,
                                      have ? &pole : nullptr, &o);
        pole = have ? SmoothDirection(pole, chosen, dt, 0.06f) : chosen;
        drawn = TrackElbowPole(s, w, body.upper, body.fore, body.armStretch, pole, have ? &drawn : nullptr,
                               2.0f * kPi * dt, o).pole;
        const Vec3 e = SolveElbow(s, w, body.upper, body.fore, drawn, body.armStretch).elbow;
        if (ArmClear(s, e, w, o) < 0.0f) { ++inside; }
        if (have) { most = std::max(most, Dist(e, last)); }
        last = e;
        have = true;
    }
    Require(inside == 0, "the drawn arm is never in the torso");
    Require(most < 0.056f, "and no elbow jumps further than the chooser's own");
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
    TestElbow();
    TestWristAngles();
    TestElbowFollowsHand();
    TestFromTo();
    TestCapsuleDistance();
    TestPlaceCapsule();
    TestMeasureArm();
    TestArmClear();
    TestChooserAvoidsTorso();
    TestTrackElbowPole();
    TestTrackingKeepsTheArmOut();
    std::cout << "arm_pose: all tests passed\n";
    return 0;
}
