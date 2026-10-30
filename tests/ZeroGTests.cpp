// Zero-G's rotation (docs/ZERO-G-SCOPE-2026-10-30.md): the bumpers' roll and
// brake, the stick's pitch latch, the steps about the body's own axes, the body
// turned about its own up axis, and the view composed on a tilted body.
#include "preyvr/AnimIk.h"
#include "preyvr/BodyTurn.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/ZeroGControls.h"
#include "TestGeometry.h"
#include "TestSupport.h"

#include <cmath>
#include <iostream>

using namespace preyvr;

namespace {

using preyvr::test::AboutX;
using preyvr::test::AboutY;
using preyvr::test::kDeg;
using preyvr::test::Require;

bool Near(Vec3 a, Vec3 b, float e = 1e-4f) { return Length(Sub(a, b)) < e; }
bool Near(Quaternion a, Quaternion b, float e = 1e-4f)
{
    return std::fabs(std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) - 1.0f) < e;
}
Quaternion AboutZ(float radians) { return stereo::YawQuaternion(radians); }

void TestBumpers()
{
    using zerog::Bumpers;
    constexpr float kFrame = 1.0f / 90.0f;
    {
        Bumpers b;
        const auto out = b.Update(true, false, false, kFrame);
        Require(out.left && !out.right && out.step == 0, "smooth: a bumper is Prey's lean button, held as it is");
        const auto both = b.Update(true, true, false, kFrame);
        Require(both.left && both.right, "smooth: both are both, Prey's brake");
    }
    {
        Bumpers b;
        auto out = b.Update(false, true, true, kFrame);
        Require(!out.left && !out.right && out.step == 0, "snap: a bumper alone waits for the other");
        out = b.Update(false, false, true, kFrame);
        Require(out.step == 1 && !out.right, "let go within the window: one step to the right");
        out = b.Update(false, false, true, kFrame);
        Require(out.step == 0, "once");
    }
    {
        Bumpers b;
        int steps = 0;
        for (int n = 0; n < 30; ++n) { steps += b.Update(true, false, true, kFrame).step; }
        Require(steps == -1, "held past the window: one step to the left, not one a frame");
        b.Update(false, false, true, kFrame);
        steps = 0;
        for (int n = 0; n < 5; ++n) { steps += b.Update(true, false, true, kFrame).step; }
        Require(steps == 0, "pressed again, it waits again");
    }
    {
        Bumpers b;
        b.Update(true, false, true, kFrame);
        const auto out = b.Update(true, true, true, kFrame);
        Require(out.left && out.right && out.step == 0, "the other within the window: Prey's brake, no roll");
        const auto still = b.Update(true, true, true, 1.0f);
        Require(still.left && still.right, "the brake lasts while both are held");
        const auto half = b.Update(true, false, true, kFrame);
        Require(!half.left && !half.right && half.step == 0, "one let go ends the brake without a roll");
        Require(b.Update(false, false, true, kFrame).step == 0 && b.Update(true, false, true, kFrame).step == 0,
                "all let go: ready again, waiting");
    }
    {
        Bumpers b;
        for (int n = 0; n < 20; ++n) { b.Update(false, true, true, kFrame); }   // a step, still held
        const auto out = b.Update(true, true, true, kFrame);
        Require(out.left && out.right, "the other pressed after a step: the brake");
    }
}

void TestPitchLatch()
{
    zerog::PitchLatch latch;
    Require(latch.Update(0.0f, 0.9f, true) == 0, "not armed until the stick has been centred");
    Require(latch.Update(0.0f, 0.0f, true) == 0 && latch.Update(0.1f, 0.9f, true) == 1, "a push up: one step up");
    Require(latch.Update(0.1f, 0.9f, true) == 0, "once a push");
    latch.Update(0.0f, 0.0f, true);
    Require(latch.Update(0.9f, 0.7f, true) == 0, "more sideways than up: the turn's, not a pitch");
    latch.Update(0.0f, 0.0f, true);
    Require(latch.Update(0.0f, -0.8f, false) == 0 && latch.Update(0.0f, -0.8f, true) == 0, "not while refused");
    latch.Update(0.0f, 0.0f, true);
    Require(latch.Update(0.0f, -0.8f, true) == -1, "a push down: one step down");
}

void TestSteps()
{
    using zerog::Axis;
    // Pitch: the nose (forward, +Y) up toward +Z. Roll: the right side (+X) down toward -Z.
    Require(Near(Rotate(zerog::LocalStep(Axis::pitch, 30.0f), {0, 1, 0}), {0, std::cos(30 * kDeg), std::sin(30 * kDeg)}),
            "a pitch step raises the nose");
    Require(Near(Rotate(zerog::LocalStep(Axis::roll, 30.0f), {1, 0, 0}), {std::cos(30 * kDeg), 0, -std::sin(30 * kDeg)}),
            "a roll step to the right lowers the right side");
    // About the body's own axis, whatever the body's facing.
    const Quaternion body = Multiply(AboutZ(70 * kDeg), AboutX(-40 * kDeg));
    const Quaternion stepped = Multiply(body, zerog::LocalStep(Axis::roll, 45.0f));
    Require(Near(Rotate(stepped, {0, 1, 0}), Rotate(body, {0, 1, 0})), "a roll keeps the body's forward");
}

void TestLocalTurn()
{
    // A body rolled 60 degrees and pitched down 20, facing 30 degrees left.
    const Quaternion body = Multiply(Multiply(AboutZ(30 * kDeg), AboutX(-20 * kDeg)), AboutY(60 * kDeg));
    const auto turned = bodyturn::LocalTurn(body, 25 * kDeg);
    Require(turned.has_value(), "a usable body turns");
    Require(Near(Rotate(*turned, {0, 0, 1}), Rotate(body, {0, 0, 1})), "about its own up axis: its up stays");
    const auto between = bodyturn::LocalYawBetween(body, *turned);
    Require(between && std::fabs(*between - 25 * kDeg) < 1e-4f, "and the turn reads back as 25 degrees");
    const auto level = bodyturn::LevelledTurn(body, 25 * kDeg);
    Require(level && std::fabs(Rotate(*level, {0, 0, 1}).z - 1.0f) < 1e-4f &&
                std::fabs(Rotate(*turned, {0, 0, 1}).z - 1.0f) > 0.1f,
            "where the gravity turn levels the body, zero-G's keeps its tilt");
    Require(!bodyturn::LocalTurn(Quaternion{0, 0, 0, 0}, 0.1f) && !bodyturn::LocalTurn(body, NAN),
            "nothing for an unusable body or turn");
    // A pure roll is no yaw about the body's up.
    const auto none = bodyturn::LocalYawBetween(body, Multiply(body, AboutY(30 * kDeg)));
    Require(none && std::fabs(*none) < 1e-4f, "a roll is no turn about the body's up");
}

void TestRotationWatch()
{
    bodyturn::NativeRotationWatch watch;
    const Quaternion body = Multiply(AboutZ(10 * kDeg), AboutY(40 * kDeg));
    Require(!watch.Update(body, 0, 0.02f * kDeg, 150), "the first look sees no turn");
    Require(watch.Update(Multiply(body, AboutX(1 * kDeg)), 10, 0.02f * kDeg, 150),
            "the game pitching the body counts as turning it");
    Require(watch.Update(Multiply(body, AboutX(1 * kDeg)), 100, 0.02f * kDeg, 150), "and for the hold after");
    Require(!watch.Update(Multiply(body, AboutX(1 * kDeg)), 200, 0.02f * kDeg, 150), "then not");
    watch.Left(Multiply(body, AboutY(5 * kDeg)));
    Require(!watch.Update(Multiply(body, AboutY(5 * kDeg)), 400, 0.02f * kDeg, 150),
            "where this lane left the body is not the game's turn");
    Require(watch.Update(Quaternion{0, 0, 0, 0}, 500, 0.02f * kDeg, 150), "an unreadable rotation counts as turning");
}

void TestTiltedView()
{
    // Gravity: the play space turned by the yaw alone, as before.
    stereo::ReferenceFrame level{};
    level.worldPosition = {1, 2, 3};
    level.yawRadians = 40 * kDeg;
    const Pose head{AboutX(10 * kDeg), {0.1f, -0.2f, 0.05f}};
    const Pose before = stereo::EyePoseInWorld(level, head);
    stereo::ReferenceFrame same = level;
    same.tilted = true;
    same.basis = AboutZ(40 * kDeg);
    const Pose sameAgain = stereo::EyePoseInWorld(same, head);
    Require(Near(before.position, sameAgain.position) && Near(before.orientation, sameAgain.orientation),
            "a level basis is the yaw it holds: nothing changes on entering zero-G level");
    // Zero-G: a body rolled 90 degrees right; the head looking straight ahead
    // with the room's forward along the body's sees the world rolled with it.
    stereo::ReferenceFrame rolled{};
    rolled.worldPosition = {0, 0, 0};
    rolled.tilted = true;
    rolled.basis = AboutY(90 * kDeg);
    const Pose ahead = stereo::EyePoseInWorld(rolled, Pose{});
    const Pose ahead0 = stereo::EyePoseInWorld(stereo::ReferenceFrame{}, Pose{});
    Require(Near(ahead.orientation, Multiply(AboutY(90 * kDeg), ahead0.orientation)),
            "the eye is the body's rotation times the head's");
    // And a head offset (leaning) goes along the body's axes, not the world's.
    const Pose leaned = stereo::EyePoseInWorld(rolled, Pose{{}, {0.1f, 0.0f, 0.0f}});
    const Pose leaned0 = stereo::EyePoseInWorld(stereo::ReferenceFrame{}, Pose{{}, {0.1f, 0.0f, 0.0f}});
    Require(Near(leaned.position, Rotate(AboutY(90 * kDeg), leaned0.position)), "a lean moves along the rolled body");
}

void TestSplitHeading()
{
    // A level basis is its heading alone: no tilt, so the yaw-only lanes are right.
    const auto level = stereo::SplitHeading(AboutZ(70 * kDeg));
    Require(std::fabs(level.yaw - 70 * kDeg) < 1e-5f && Near(level.tilt, Quaternion{}), "a level basis has no tilt");
    // A rolled and pitched one splits into a heading and a tilt that put it back together.
    const Quaternion body = Multiply(Multiply(AboutZ(-120 * kDeg), AboutX(35 * kDeg)), AboutY(-50 * kDeg));
    const auto split = stereo::SplitHeading(body);
    Require(Near(stereo::PlaySpaceBasis(split.yaw, split.tilt), body), "tilt * Rz(yaw) is the basis again");
    Require(std::fabs(split.tilt.z) < 1e-5f, "the tilt has no turn about Z left in it");
    // A tilted play space carries the controller with it, as the view's basis does.
    const Pose head{AboutX(5 * kDeg), {0.0f, 1.6f, 0.0f}};
    const Pose controller{AboutY(20 * kDeg), {0.2f, 1.2f, -0.4f}};
    const Vec3 eye{3, 4, 5};
    const Pose hand = animik::ControllerWorldFromHead(body, eye, head, controller);
    stereo::ReferenceFrame frame{};
    frame.tilted = true;
    frame.basis = body;
    frame.worldPosition = eye;
    const Pose expected = stereo::EyePoseInWorld(frame, Pose{controller.orientation, Sub(controller.position, head.position)});
    Require(Near(hand.position, expected.position) && Near(hand.orientation, expected.orientation),
            "the controller is placed on the same basis as the eye");
    const Pose flat = animik::ControllerWorldFromHead(30 * kDeg, eye, head, controller);
    const Pose flatBasis = animik::ControllerWorldFromHead(AboutZ(30 * kDeg), eye, head, controller);
    Require(Near(flat.position, flatBasis.position) && Near(flat.orientation, flatBasis.orientation),
            "the yaw form is the basis form on a level basis");
}

void TestConversions()
{
    const Pose head{AboutX(8 * kDeg), {0.05f, 1.6f, -0.1f}};
    const Vec3 anchor{10, -20, 30};
    const Vec3 tracking{0.3f, 1.1f, -0.5f};
    const Vec3 direction{0.2f, -0.3f, -0.9f};
    // On a level basis the basis forms are the yaw forms, exactly.
    const float yaw = -35 * kDeg;
    const Quaternion level = AboutZ(yaw);
    const Vec3 a = stereo::TrackingToEngine(tracking, anchor, yaw, head), b = stereo::TrackingToEngine(tracking, anchor, level, head);
    Require(a.x == b.x && a.y == b.y && a.z == b.z, "TrackingToEngine: the yaw form is the basis form, bit for bit");
    const Vec3 c = stereo::TrackingDirToEngine(direction, yaw), d = stereo::TrackingDirToEngine(direction, level);
    Require(c.x == d.x && c.y == d.y && c.z == d.z, "TrackingDirToEngine likewise");
    // Tilted: there and back is where it started, and the origin agrees with the point map.
    const Quaternion tilted = Multiply(Multiply(AboutZ(50 * kDeg), AboutX(-30 * kDeg)), AboutY(70 * kDeg));
    const Vec3 world = stereo::TrackingToEngine(tracking, anchor, tilted, head);
    Require(Near(stereo::EngineToTracking(world, anchor, tilted, head), tracking, 1e-4f), "a point there and back, tilted");
    const Vec3 dir = stereo::TrackingDirToEngine(direction, tilted);
    Require(Near(stereo::EngineDirToTracking(dir, tilted), direction), "a direction there and back, tilted");
    const Pose origin = stereo::TrackingOriginInWorld(tilted, anchor, head);
    Require(Near(Add(origin.position, Rotate(origin.orientation, stereo::ToEngineSpace(tracking))), world, 1e-4f),
            "the play space's origin in the world places the point where TrackingToEngine does");
    // How far apart two rotations are.
    Require(std::fabs(stereo::RotationAngle(AboutZ(10 * kDeg), AboutZ(-20 * kDeg)) - 30 * kDeg) < 1e-4f,
            "between two yaws, their difference");
    Require(std::fabs(stereo::RotationAngle(tilted, Multiply(tilted, AboutX(15 * kDeg))) - 15 * kDeg) < 1e-3f,
            "a pitch step on a tilted body is its size, whatever the yaw");
}

} // namespace

int main()
{
    TestBumpers();
    TestPitchLatch();
    TestSteps();
    TestLocalTurn();
    TestRotationWatch();
    TestTiltedView();
    TestSplitHeading();
    TestConversions();
    std::cout << "zero_g: bumpers roll and brake, stick pitches, steps and turns about the body's own axes, a tilted view\n";
}
