#include "preyvr/ArmPose.h"
#include "preyvr/BodyFollow.h"
#include "preyvr/StereoCamera.h"
#include "TestGeometry.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::bodyfollow;

namespace {

using preyvr::test::Require;

using preyvr::test::kDeg;
bool NearAngle(float a, float b, float e = 1e-3f) { return std::fabs(Wrap(a - b)) <= e; }
using preyvr::test::AboutX;
using preyvr::test::AboutY;
// An engine view rotation: yaw about Z, then pitch about the view's right (+X).
Quaternion View(float yaw, float pitch, float roll = 0.0f)
{
    return Normalize(Multiply(Multiply(stereo::YawQuaternion(yaw), AboutX(pitch)), AboutY(roll)));
}

void TestWrap()
{
    Require(NearAngle(Wrap(370.0f * kDeg), 10.0f * kDeg, 1e-5f), "wraps above +pi");
    Require(NearAngle(Wrap(-190.0f * kDeg), 170.0f * kDeg, 1e-5f), "wraps below -pi");
    for (float a = -1000.0f; a < 1000.0f; a += 7.3f) {
        const float w = Wrap(a);
        Require(w > -3.1416f && w <= 3.1416f, "always in (-pi, pi]");
    }
}

void TestEngineYaw()
{
    for (float yaw : {-170.0f, -90.0f, -20.0f, 0.0f, 33.0f, 90.0f, 179.0f}) {
        for (float pitch : {-60.0f, -10.0f, 0.0f, 45.0f}) {
            const Quaternion q = View(yaw * kDeg, pitch * kDeg);
            const auto got = EngineYawOf(q);
            const float camera = stereo::CameraYawOf(stereo::MatrixFromPose(Pose{q, {}}));
            Require(got && NearAngle(*got, yaw * kDeg), "yaw of a pitched view");
            Require(NearAngle(*got, camera), "matches CameraYawOf");
        }
        // Straight down and straight up: the face is still turned somewhere.
        const auto down = EngineYawOf(View(yaw * kDeg, -90.0f * kDeg));
        const auto up = EngineYawOf(View(yaw * kDeg, 90.0f * kDeg));
        Require(down && NearAngle(*down, yaw * kDeg, 1e-2f), "yaw looking straight down");
        Require(up && NearAngle(*up, yaw * kDeg, 1e-2f), "yaw looking straight up");
    }
    Require(!EngineYawOf(Quaternion{0, 0, 0, 0}), "a zero quaternion has no yaw");
    Require(!EngineYawOf(Quaternion{NAN, 0, 0, 1}), "a non-finite quaternion has no yaw");
}

// The lane subtracts the reference yaw (stereo::RecenterYawFromHeadPose) from
// the torso yaw (armpose::HeadYaw): both must name the same direction.
void TestYawConventionsAgree()
{
    for (float yaw : {-160.0f, -45.0f, 0.0f, 30.0f, 120.0f}) {
        for (float pitch : {-50.0f, 0.0f, 40.0f}) {
            const Quaternion head = Normalize(Multiply(AboutY(yaw * kDeg), AboutX(pitch * kDeg)));
            const auto reference = stereo::RecenterYawFromHeadPose(Pose{head, {}});
            Require(reference && NearAngle(*reference, armpose::HeadYaw(head)), "torso and reference yaws agree");
        }
    }
}

void TestTurnedViewRotation()
{
    const Quaternion pitched = View(30.0f * kDeg, -20.0f * kDeg);
    const auto level = TurnedViewRotation(pitched, 15.0f * kDeg, true);
    Require(level && NearAngle(*EngineYawOf(*level), 45.0f * kDeg), "levelled turn adds the delta");
    const Vec3 f = Rotate(*level, Vec3{0, 1, 0});
    const Vec3 u = Rotate(*level, Vec3{0, 0, 1});
    Require(std::fabs(f.z) < 1e-5f && std::fabs(u.z - 1.0f) < 1e-5f, "levelled: no pitch, no roll");

    const auto kept = TurnedViewRotation(pitched, 15.0f * kDeg, false);
    Require(kept && NearAngle(*EngineYawOf(*kept), 45.0f * kDeg), "unlevelled turn adds the delta");
    const float before = Rotate(pitched, Vec3{0, 1, 0}).z;
    Require(std::fabs(Rotate(*kept, Vec3{0, 1, 0}).z - before) < 1e-5f, "unlevelled keeps the pitch");

    Require(!TurnedViewRotation(pitched, NAN, true), "non-finite delta refused");
    Require(!TurnedViewRotation(Quaternion{0, 0, 0, 0}, 0.1f, true), "degenerate rotation refused");
}

void TestFollowStep()
{
    const Step s = FollowStep(10.0f * kDeg, 25.0f * kDeg, 0.01f * kDeg);
    Require(s.apply && NearAngle(s.delta, 15.0f * kDeg, 1e-5f), "turns by torso minus reference");
    const Step across = FollowStep(179.0f * kDeg, -179.0f * kDeg, 0.0f);
    Require(across.apply && NearAngle(across.delta, 2.0f * kDeg, 1e-5f), "the short way across +-180");
    Require(!FollowStep(1.0f, 1.0f + 1e-5f, 1e-4f).apply, "below the minimum is not applied");
    Require(!FollowStep(NAN, 0.0f, 0.0f).apply && !FollowStep(0.0f, INFINITY, 0.0f).apply,
            "non-finite input refused");
}

// The lane, reduced to its arithmetic: a native rotation the body faces, the
// reference yaw, and the play space between them.
struct Model {
    Quaternion native = View(0.0f, 0.0f);
    float reference = 0.0f;
    armpose::TorsoYaw torso;
    float G() const { return *EngineYawOf(native); }
    float P() const { return Wrap(G() - reference); }
    float Follow(float head, float dt)
    {
        const float t = torso.Update(head, dt, 35.0f * kDeg, 0.6f);
        const Step step = FollowStep(reference, t, 0.01f * kDeg);
        if (step.apply) {
            const float before = G();
            native = *TurnedViewRotation(native, step.delta, true);
            // The lane compensates by what the setter actually applied.
            reference = Wrap(reference + Wrap(G() - before));
        }
        return t;
    }
};

void TestPlaySpaceNeverMoves()
{
    Model m;
    m.native = View(40.0f * kDeg, -12.0f * kDeg);   // a save made looking down a little
    const float p0 = m.P();
    const float dt = 1.0f / 90.0f;
    // A physical 180-degree turn over two seconds, then standing still.
    for (int i = 0; i <= 900; ++i) {
        const float head = static_cast<float>(i < 180 ? i : 180) * kDeg;
        m.Follow(head, dt);
        Require(NearAngle(m.P(), p0, 1e-4f), "the play space does not move while the body follows");
        // Within the lane's minimum step, below which it leaves the body be.
        Require(NearAngle(m.reference, m.torso.yaw, 0.0101f * kDeg), "the reference is the torso");
    }
    Require(NearAngle(m.G(), Wrap(p0 + 180.0f * kDeg), 0.5f * kDeg), "after a 180 the body faces the view");
    Require(std::fabs(Rotate(m.native, Vec3{0, 1, 0}).z) < 1e-5f, "native pitch levelled");
}

void TestGlanceBarelyTurnsTheBody()
{
    Model m;
    const float dt = 1.0f / 90.0f;
    for (int i = 0; i < 90; ++i) { m.Follow(0.0f, dt); }
    const float g0 = m.G();
    for (int i = 0; i < 22; ++i) { m.Follow(30.0f * kDeg, dt); }   // a quarter-second glance
    Require(std::fabs(Wrap(m.G() - g0)) < 5.0f * kDeg, "a glance inside the deadzone barely turns the body");
    for (int i = 0; i < 22; ++i) { m.Follow(80.0f * kDeg, dt); }
    Require(std::fabs(Wrap(m.G() - g0)) >= 45.0f * kDeg - 1e-3f, "past the deadzone the body comes round");
}

void TestSnapAndNativeTurnsCompose()
{
    Model m;
    const float dt = 1.0f / 90.0f;
    for (int i = 0; i < 90; ++i) { m.Follow(0.0f, dt); }
    const float p0 = m.P(), g0 = m.G();
    // Snap turn, as SnapTurnHeadTracking does it: the reference only.
    m.reference = Wrap(m.reference - 45.0f * kDeg);
    Require(NearAngle(m.P(), p0 + 45.0f * kDeg, 1e-4f), "the snap turns the view");
    Require(NearAngle(m.G(), g0, 1e-6f), "the snap alone leaves the body behind");
    m.Follow(0.0f, dt);
    Require(NearAngle(m.G(), g0 + 45.0f * kDeg, 1e-4f), "one step later the body has turned with it");
    Require(NearAngle(m.P(), p0 + 45.0f * kDeg, 1e-4f), "and the view did not move again");
    // A native turn the lane did not cause (smooth turn, scripts): kept.
    m.native = Normalize(Multiply(stereo::YawQuaternion(10.0f * kDeg), m.native));
    const float g1 = m.G();
    m.Follow(0.0f, dt);
    Require(NearAngle(m.G(), g1, 0.05f * kDeg), "the follower never cancels a native turn");
    Require(NearAngle(m.P(), p0 + 55.0f * kDeg, 0.05f * kDeg), "a native turn turns the view");
}

} // namespace

int main()
{
    TestWrap();
    TestEngineYaw();
    TestYawConventionsAgree();
    TestTurnedViewRotation();
    TestFollowStep();
    TestPlaySpaceNeverMoves();
    TestGlanceBarelyTurnsTheBody();
    TestSnapAndNativeTurnsCompose();
    std::cout << "body_follow: all tests passed\n";
    return 0;
}
