// HandWorld (src/body/HandWorld.h), the one hand placement: bit for bit the
// composition every site wrote by hand before it -- ControllerWorldFromHead about
// the anchor, plus the surface stop's offset for the drawn hand -- for each hand,
// pose, anchor and placement. And the anchors' choices: a frame without a camera
// refuses Anchor::camera, leaving the output alone, and falls back for
// cameraElseReticle.
#include "body/HandWorld.h"
#include "TestSupport.h"

#include <cstring>
#include <iostream>
#include <string>

namespace {
using namespace preyvr;
using namespace preyvr::dll;
using preyvr::test::Require;

bool Same(const Pose& a, const Pose& b) { return std::memcmp(&a, &b, sizeof(Pose)) == 0; }

GameplayPoseFrame Frame()
{
    GameplayPoseFrame f{};
    f.yaw = 0.731f;
    f.cameraCentre = {1.25f, -3.5f, 1.62f};
    f.cameraCentreValid = true;
    f.reticleOrigin = {1.31f, -3.42f, 1.58f};
    f.tracking.head = {Normalize(Quaternion{0.05f, 0.31f, -0.02f, 0.95f}), {0.11f, 1.63f, -0.27f}};
    auto& right = f.tracking.hands[static_cast<unsigned>(Hand::right)];
    auto& left = f.tracking.hands[static_cast<unsigned>(Hand::left)];
    right.gripPose = {Normalize(Quaternion{0.12f, -0.4f, 0.07f, 0.9f}), {0.24f, 1.21f, -0.48f}};
    right.aimPose = {Normalize(Quaternion{0.21f, -0.37f, 0.02f, 0.88f}), {0.26f, 1.24f, -0.55f}};
    left.gripPose = {Normalize(Quaternion{-0.08f, 0.33f, 0.11f, 0.93f}), {-0.21f, 1.18f, -0.41f}};
    left.aimPose = {Normalize(Quaternion{-0.15f, 0.29f, 0.05f, 0.94f}), {-0.23f, 1.2f, -0.49f}};
    f.surface[static_cast<unsigned>(Hand::right)].offset = {-0.031f, 0.012f, 0.004f};
    f.surface[static_cast<unsigned>(Hand::left)].offset = {0.017f, -0.022f, 0.009f};
    return f;
}

// What the sites wrote: the anchor's point, ControllerWorldFromHead, then the
// offset added to the position for the drawn hand.
Pose ByHand(const GameplayPoseFrame& f, Hand hand, ControllerPose pose, const Vec3& anchor, Placement placement)
{
    const auto& c = f.tracking.hands[static_cast<unsigned>(hand)];
    Pose out = animik::ControllerWorldFromHead(f.yaw, anchor, f.tracking.head,
                                               pose == ControllerPose::aim ? c.aimPose : c.gripPose);
    if (placement == Placement::drawn) { out.position = Add(out.position, SurfaceOffset(f, hand)); }
    return out;
}

void TestComposition()
{
    const GameplayPoseFrame f = Frame();
    int cases = 0;
    for (const Hand hand : {Hand::right, Hand::left}) {
        for (const ControllerPose pose : {ControllerPose::grip, ControllerPose::aim}) {
            for (const Anchor anchor : {Anchor::camera, Anchor::cameraElseReticle, Anchor::reticle}) {
                const Vec3 point = anchor == Anchor::reticle ? f.reticleOrigin : f.cameraCentre;
                for (const Placement placement : {Placement::controller, Placement::drawn}) {
                    Pose out{};
                    Require(HandWorld(f, hand, pose, anchor, placement, out), "a frame with a camera places every hand");
                    Require(Same(out, ByHand(f, hand, pose, point, placement)),
                            "bit for bit the composition the sites wrote by hand");
                    ++cases;
                }
            }
        }
    }
    // The two placements differ by the offset, and the anchors by their points.
    Pose controller{}, drawn{}, reticle{};
    HandWorld(f, Hand::right, ControllerPose::grip, Anchor::camera, Placement::controller, controller);
    HandWorld(f, Hand::right, ControllerPose::grip, Anchor::camera, Placement::drawn, drawn);
    HandWorld(f, Hand::right, ControllerPose::grip, Anchor::reticle, Placement::controller, reticle);
    Require(!Same(controller, drawn) && !Same(controller, reticle), "the placements and anchors are not all one");
    std::cout << "hand world: " << cases << " compositions bit-identical\n";
}

void TestAnchors()
{
    GameplayPoseFrame f = Frame();
    Vec3 point{};
    Require(AnchorPoint(f, Anchor::camera, point) && std::memcmp(&point, &f.cameraCentre, sizeof(Vec3)) == 0,
            "camera: the camera centre");
    Require(AnchorPoint(f, Anchor::cameraElseReticle, point) &&
                std::memcmp(&point, &f.cameraCentre, sizeof(Vec3)) == 0,
            "camera else reticle, with a camera: the camera centre");
    Require(AnchorPoint(f, Anchor::reticle, point) && std::memcmp(&point, &f.reticleOrigin, sizeof(Vec3)) == 0,
            "reticle: the reticle origin, camera or not");

    f.cameraCentreValid = false;
    const Pose untouched{Quaternion{0.5f, 0.5f, 0.5f, 0.5f}, Vec3{9, 9, 9}};
    Pose out = untouched;
    Require(!HandWorld(f, Hand::right, ControllerPose::grip, Anchor::camera, Placement::drawn, out) && Same(out, untouched),
            "camera: refused without a camera, the output left alone");
    Require(AnchorPoint(f, Anchor::cameraElseReticle, point) &&
                std::memcmp(&point, &f.reticleOrigin, sizeof(Vec3)) == 0,
            "camera else reticle, without a camera: the reticle origin");
    Require(HandWorld(f, Hand::left, ControllerPose::aim, Anchor::cameraElseReticle, Placement::controller, out) &&
                Same(out, ByHand(f, Hand::left, ControllerPose::aim, f.reticleOrigin, Placement::controller)),
            "and the hand is placed on it");
}

} // namespace

int main()
{
    TestComposition();
    TestAnchors();
    return 0;
}
