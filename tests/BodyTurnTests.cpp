#include "preyvr/ArmPose.h"
#include "preyvr/BodyTurn.h"
#include "preyvr/ComfortControls.h"
#include "preyvr/MotionController.h"
#include "preyvr/StereoCamera.h"
#include "TestGeometry.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::bodyturn;

namespace {

using preyvr::test::Require;

using preyvr::test::kDeg;
float Wrap(float a) { return controller::WrapAngle(a); }
bool NearAngle(float a, float b, float e = 1e-3f) { return std::fabs(Wrap(a - b)) <= e; }
using preyvr::test::AboutX;
using preyvr::test::AboutY;
// An engine view rotation: yaw about Z, then pitch about the view's right (+X).
Quaternion View(float yaw, float pitch, float roll = 0.0f)
{
    return Normalize(Multiply(Multiply(stereo::YawQuaternion(yaw), AboutX(pitch)), AboutY(roll)));
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

void TestLevelledTurn()
{
    const Quaternion pitched = View(30.0f * kDeg, -20.0f * kDeg);
    const auto level = LevelledTurn(pitched, 15.0f * kDeg);
    Require(level && NearAngle(*EngineYawOf(*level), 45.0f * kDeg), "the turn adds the delta");
    const Vec3 f = Rotate(*level, Vec3{0, 1, 0});
    const Vec3 u = Rotate(*level, Vec3{0, 0, 1});
    Require(std::fabs(f.z) < 1e-5f && std::fabs(u.z - 1.0f) < 1e-5f, "levelled: no pitch, no roll");
    Require(!LevelledTurn(pitched, NAN), "non-finite delta refused");
    Require(!LevelledTurn(Quaternion{0, 0, 0, 0}, 0.1f), "degenerate rotation refused");
}

void TestTorsoStep()
{
    const Step s = TorsoStep(10.0f * kDeg, 25.0f * kDeg, 0.01f * kDeg);
    Require(s.apply && NearAngle(s.delta, 15.0f * kDeg, 1e-5f), "turns by torso minus reference");
    const Step across = TorsoStep(179.0f * kDeg, -179.0f * kDeg, 0.0f);
    Require(across.apply && NearAngle(across.delta, 2.0f * kDeg, 1e-5f), "the short way across +-180");
    Require(!TorsoStep(1.0f, 1.0f + 1e-5f, 1e-4f).apply, "below the minimum is not applied");
    Require(!TorsoStep(NAN, 0.0f, 0.0f).apply && !TorsoStep(0.0f, INFINITY, 0.0f).apply,
            "non-finite input refused");
}

// The lane, reduced to its arithmetic: a native rotation the body faces, the
// reference yaw, and the play space between them.
struct Model {
    Quaternion native = View(0.0f, 0.0f);
    float reference = 0.0f;
    float G() const { return *EngineYawOf(native); }
    float P() const { return Wrap(G() - reference); }
    // As SnapTurnHeadTracking does it: the reference only.
    void Snap(float degrees) { reference = Wrap(reference - degrees * kDeg); }
    // As TorsoLane does after a snap.
    void TurnToTorso(float torso)
    {
        const Step step = TorsoStep(reference, torso, 0.01f * kDeg);
        if (!step.apply) { return; }
        const float before = G();
        native = *LevelledTurn(native, step.delta);
        // The lane compensates by what the setter actually applied.
        reference = Wrap(reference + Wrap(G() - before));
    }
};

void TestSnapTurnsTheBody()
{
    Model m;
    m.native = View(40.0f * kDeg, -12.0f * kDeg);   // a save made looking down a little
    const float p0 = m.P(), g0 = m.G();
    m.Snap(45.0f);
    Require(NearAngle(m.P(), p0 + 45.0f * kDeg, 1e-4f), "the snap turns the view");
    Require(NearAngle(m.G(), g0, 1e-6f), "the snap alone leaves the body behind");
    m.TurnToTorso(0.0f);   // the torso still faces where it did
    Require(NearAngle(m.G(), g0 + 45.0f * kDeg, 1e-4f), "then the body turns with it");
    Require(NearAngle(m.P(), p0 + 45.0f * kDeg, 1e-4f), "and the view does not move again");
    Require(std::fabs(Rotate(m.native, Vec3{0, 1, 0}).z) < 1e-5f, "native pitch levelled");
    m.Snap(-90.0f);
    m.TurnToTorso(0.0f);
    Require(NearAngle(m.G(), g0 - 45.0f * kDeg, 1e-4f), "a snap the other way turns it back past");
}

void TestSnapFacesTheTorso()
{
    Model m;
    const float p0 = m.P();
    // Turned round in the room by 90 degrees: the view follows the head, the
    // body stays where it was until a snap.
    const float torso = 90.0f * kDeg;
    m.Snap(30.0f);
    m.TurnToTorso(torso);
    Require(NearAngle(m.G(), Wrap(m.P() + torso), 1e-4f), "after a snap the body faces the torso");
    Require(NearAngle(m.P(), p0 + 30.0f * kDeg, 1e-4f), "and the view is where the snap put it");
}

// Where the camera sits, as the view hook composes it (HeadTrackingHook.cpp):
// the game's eye is the capsule plus the #camera offset turned with the body
// (0x1489010), and the head's offset from the origin is turned by the play
// space P = G - R. Seen from the body, the camera is where the player sees the
// model from.
struct Scene : Model {
    Vec3 capsule{2.0f, -1.0f, 0.0f};                  // engine
    Vec3 eyeOffset{0.0f, 0.12f, 1.6f};                // #camera, body-local engine
    Vec3 origin{0.1f, 1.6f, 0.2f};                    // tracking
    Pose head{AboutY(0.3f), {0.45f, 1.7f, -0.25f}};   // tracking: stepped away from the origin
    Vec3 Camera() const
    {
        stereo::ReferenceFrame frame{};
        frame.yawRadians = P();
        frame.worldPosition = Add(capsule, Rotate(stereo::YawQuaternion(G()), eyeOffset));
        return stereo::EyePoseInWorld(frame, Pose{head.orientation, Sub(head.position, origin)}).position;
    }
    Vec3 CameraOnBody() const { return Rotate(Conjugate(stereo::YawQuaternion(G())), Sub(Camera(), capsule)); }
    // As SnapTurnHeadTracking does it, about the head or about the body.
    void SnapAbout(int steps, float degrees, bool aboutHead)
    {
        if (aboutHead) { origin = comfort::TurnOriginAboutHead(origin, head.position, steps * degrees * kDeg); }
        reference = controller::SnapTurn(reference, steps, degrees * kDeg);
    }
};

bool Near(Vec3 a, Vec3 b, float e = 1e-4f) { return Length(Sub(a, b)) <= e; }

void TestSnapKeepsTheCameraOnTheBody()
{
    // Pivoted about the head, as the snap alone always was: the camera stays
    // where it is in the world, and the body turning under it moves the model
    // against the camera -- into it, after a step away from the origin.
    Scene head;
    head.native = View(20.0f * kDeg, 0.0f);
    head.reference = 0.3f;
    const float torso = head.reference;   // not turned round in the room
    const Vec3 world0 = head.Camera(), onBody0 = head.CameraOnBody();
    head.SnapAbout(1, 45.0f, true);
    Require(Near(head.Camera(), world0), "about the head, the snap leaves the camera where it was");
    head.TurnToTorso(torso);
    Require(!Near(head.CameraOnBody(), onBody0, 0.05f), "then the body turns and the model moves against the camera");

    // About the body: the snap and the body's turn together are one rigid turn
    // about the capsule, as a smooth turn's is.
    Scene body = Scene{};
    body.native = View(20.0f * kDeg, 0.0f);
    body.reference = 0.3f;
    const float p0 = body.P();
    body.SnapAbout(1, 45.0f, false);
    body.TurnToTorso(torso);
    Require(Near(body.CameraOnBody(), onBody0), "about the body, the camera keeps its place on the body");
    Require(NearAngle(body.P(), p0 - 45.0f * kDeg, 1e-4f), "and the view turns by the snap");

    Scene smooth = Scene{};
    smooth.native = Normalize(Multiply(stereo::YawQuaternion(-45.0f * kDeg), View(20.0f * kDeg, 0.0f)));
    smooth.reference = 0.3f;
    Require(Near(body.Camera(), smooth.Camera()) && NearAngle(body.G(), smooth.G(), 1e-4f),
            "the same as the game turning the player 45 degrees itself");
}

void TestNativeTurnIsKept()
{
    Model m;
    m.Snap(45.0f);
    m.TurnToTorso(0.0f);
    const float p0 = m.P();
    // A native turn the lane did not cause (smooth turn, scripts): kept.
    m.native = Normalize(Multiply(stereo::YawQuaternion(10.0f * kDeg), m.native));
    const float g1 = m.G();
    m.TurnToTorso(0.0f);
    Require(NearAngle(m.G(), g1, 0.05f * kDeg), "turning to the torso never cancels a native turn");
    Require(NearAngle(m.P(), p0 + 10.0f * kDeg, 0.05f * kDeg), "a native turn turns the view");
}

} // namespace

int main()
{
    TestEngineYaw();
    TestYawConventionsAgree();
    TestLevelledTurn();
    TestTorsoStep();
    TestSnapTurnsTheBody();
    TestSnapFacesTheTorso();
    TestSnapKeepsTheCameraOnTheBody();
    TestNativeTurnIsKept();
    std::cout << "body_turn: all tests passed\n";
    return 0;
}
