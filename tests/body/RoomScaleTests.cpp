#include "body/RoomScale.h"
#include "render/camera/StereoCamera.h"
#include "TestGeometry.h"
#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::roomscale;

namespace {

using preyvr::test::Require;
using preyvr::test::kDeg;

bool Near(Vec3 a, Vec3 b, float e = 1e-4f) { return Length(Sub(a, b)) <= e; }
float LevelLength(Vec3 v) { return std::hypot(v.x, v.y); }
// How far a lean of `lead` from hips `trunk` below the eyes lowers the head.
float HingeDrop(float lead, float trunk) { return trunk - std::sqrt(trunk * trunk - lead * lead); }

void TestShiftCancelsTheMove()
{
    for (float yaw : {-150.0f, -20.0f, 0.0f, 75.0f, 180.0f}) {
        for (float scale : {1.0f, 0.8f, 1.5f}) {
            const Vec3 head{0.4f, 1.6f, -1.1f}, origin{0.1f, 1.5f, 0.2f};
            const Vec3 move{0.07f, -0.03f, 0.0f};
            const Vec3 before = HeadOffsetWorld(head, origin, yaw * kDeg, scale);
            const Vec3 after = HeadOffsetWorld(head, Add(origin, OriginShift(move, yaw * kDeg, scale)), yaw * kDeg, scale);
            Require(Near(Sub(before, after), move, 1e-5f), "the origin shift takes exactly the move off the head offset");
        }
    }
    // OpenXR up is engine up: a head 0.2 m above the origin is 0.2 m up.
    Require(Near(HeadOffsetWorld({0, 0.2f, 0}, {}, 33.0f * kDeg, 1.0f), {0, 0, 0.2f}, 1e-5f), "height stays height");
    // OpenXR forward (-Z) at yaw 0 is engine +Y.
    Require(Near(HeadOffsetWorld({0, 0, -1}, {}, 0.0f, 1.0f), {0, 1, 0}, 1e-5f), "forward is engine +Y");
}

// Upright, the head may lead by the lean zone; bent well down, by the lean.
void TestLeanAllowance()
{
    const Settings s;
    Require(LeanAllowance(0.0f, s) == s.leanZone && LeanAllowance(-0.1f, s) == s.leanZone,
            "upright, or taller than calibrated: the lean zone");
    Require(LeanAllowance(s.dropBand, s) == s.leanZone, "a gait's bob is not a deeper lean");
    // The head 10 cm down: past the band, a hinge that takes it about 26 cm forward.
    const float bent = LeanAllowance(0.10f, s);
    Require(bent > s.deadzone + 0.24f && bent < s.deadzone + 0.28f && bent > s.leanZone, "bending down allows more");
    Require(LeanAllowance(0.12f, s) > bent, "a deeper bend allows more still");
    Require(LeanAllowance(0.6f, s) == s.deadzone + s.maxLean, "never more than the most a lean explains");
    Require(LeanAllowance(std::nanf(""), s) == s.leanZone, "no height: upright");
    Require(LeanDrop(0.0f, s) == 0.0f && std::fabs(LeanDrop(0.2f, s) - HingeDrop(0.2f, s.trunk)) < 1e-6f,
            "a lean lowers the head as a hinge of the trunk's length");
    Require(LeanDrop(0.25f, s) > LeanDrop(0.15f, s) && LeanDrop(2.0f, s) == s.trunk && LeanDrop(std::nanf(""), s) == 0.0f,
            "further lowers more, never more than the trunk");
}

// The lane reduced to its arithmetic: the view eye is the native eye plus the
// head offset, the follower moves the capsule and the origin together.
struct World {
    Vec3 capsule{10, 20, 0};
    Vec3 origin{};
    float yaw = 30.0f * kDeg;
    float scale = 1.0f;
    Follower follower;
    Settings settings;
    Vec3 eye(const Vec3& head) const { return Add(Add(capsule, Vec3{0, 0, 1.7f}), HeadOffsetWorld(head, origin, yaw, scale)); }
    // `drop`: how far the head is below its calibrated height.
    Vec3 Step(const Vec3& head, float dt, float drop = 0.0f)
    {
        const Vec3 move = follower.Update(HeadOffsetWorld(head, origin, yaw, scale), drop, dt, settings);
        capsule = Add(capsule, move);
        origin = Add(origin, OriginShift(move, yaw, scale));
        follower.Applied(move);
        return move;
    }
    float Away(const Vec3& head) const { return LevelLength(HeadOffsetWorld(head, origin, yaw, scale)); }
};

void TestWalkingNeverMovesTheView()
{
    World w;
    const float dt = 1.0f / 90.0f;
    const Vec3 start{0.0f, 1.6f, 0.0f};
    const Vec3 eye0 = w.eye(start);
    // Two metres back and half a metre to the side over two seconds, then still.
    for (int i = 0; i <= 360; ++i) {
        const float t = std::fmin(i / 180.0f, 1.0f);
        const Vec3 head{0.5f * t, 1.6f, 2.0f * t};
        const Vec3 expected = Add(eye0, HeadOffsetWorld(head, start, w.yaw, w.scale));
        w.Step(head, dt);
        Require(Near(w.eye(head), expected, 1e-4f), "following never moves the view");
        Require(w.Away(head) <= w.settings.leanZone + 0.012f, "walking, the body trails by the lean zone at most");
    }
    Require(w.Away({0.5f, 1.6f, 2.0f}) <= w.settings.deadzone + w.settings.jitter + 1e-4f,
            "stopped upright, it settles under the head");
    Require(Length(Sub(w.capsule, World{}.capsule)) > 1.9f, "it walked with the player");
    Require(std::fabs(HeadOffsetWorld({0.5f, 1.6f, 2.0f}, w.origin, w.yaw, w.scale).z - 1.6f) < 1e-5f,
            "height is never followed");
}

void TestNativeMovesAreNotAbsorbed()
{
    World w;
    const Vec3 head{0.0f, 1.6f, 0.0f};
    w.Step(head, 1.0f / 90.0f);
    const Vec3 eye0 = w.eye(head);
    // Stick walking, a lift, a scripted move: the capsule moves natively.
    w.capsule = Add(w.capsule, Vec3{1.0f, -2.0f, 0.5f});
    for (int i = 0; i < 90; ++i) {
        Require(Length(w.Step(head, 1.0f / 90.0f)) == 0.0f, "the follower does not chase a native move");
    }
    Require(Near(w.eye(head), Add(eye0, Vec3{1.0f, -2.0f, 0.5f})), "a native move moves the view");
}

void TestBendingDownIsNotAStep()
{
    World w;
    const float dt = 1.0f / 90.0f;
    // Picking something up: the head straight down 50 cm, then 30 cm forward.
    for (int i = 0; i <= 90; ++i) {
        const float down = 0.5f * i / 90.0f;
        Require(Length(w.Step({0.0f, 1.6f - down, 0.0f}, dt, down)) == 0.0f, "going down leaves the capsule");
    }
    for (int i = 0; i <= 90; ++i) {
        Require(Length(w.Step({0.0f, 1.1f, -0.3f * i / 90.0f}, dt, 0.5f)) == 0.0f, "reaching forward bent down too");
    }
    // Back over the feet, then up, and nothing moved.
    for (int i = 0; i <= 90; ++i) { w.Step({0.0f, 1.1f, -0.3f + 0.3f * i / 90.0f}, dt, 0.5f); }
    for (int i = 0; i <= 90; ++i) { w.Step({0.0f, 1.1f + 0.5f * i / 90.0f, 0.0f}, dt, 0.5f - 0.5f * i / 90.0f); }
    Require(Length(Sub(w.capsule, World{}.capsule)) < 1e-6f, "the bend taken back: the capsule never moved");
    // The same 30 cm forward at full height, then standing there, is a step.
    for (int i = 0; i <= 225; ++i) { w.Step({0.0f, 1.6f, -0.3f * std::fmin(i / 45.0f, 1.0f)}, dt); }
    Require(Length(Sub(w.capsule, World{}.capsule)) > 0.27f, "a step at full height is followed");
    // Bent down and further than any lean, the capsule comes after the rest.
    World far;
    far.Step({0.0f, 1.45f, 0.0f}, dt, 0.15f);
    for (int i = 0; i < 90; ++i) { far.Step({0.0f, 1.45f, -0.8f}, dt, 0.15f); }
    Require(std::fabs(far.Away({0.0f, 1.45f, -0.8f}) - LeanAllowance(0.15f, far.settings)) < 0.005f,
            "beyond a lean: followed up to the lean");
}

void TestSpeedAndJumps()
{
    const Settings s;
    const float dt = 1.0f / 90.0f;
    Follower f;
    Require(Length(f.Update({0.2f, 0, 0}, 0.0f, dt, s)) == 0.0f, "inside the lean zone: still");
    const Vec3 m = f.Update({0.27f, 0, 0}, 0.0f, dt, s);
    Require(std::fabs(Length(m) - (0.27f - s.leanZone)) < 1e-6f && m.x > 0.0f, "past it: the excess, at once");
    f.Applied(m);
    // A fresh follower 1.2 m out: never faster than the cap.
    Follower capped;
    Require(std::fabs(Length(capped.Update({1.2f, 0, 0}, 0.0f, dt, s)) - s.maxSpeed * dt) < 1e-5f,
            "never faster than the speed cap");
    // A tracking jump: refused for the frame, then followed normally.
    Follower j;
    j.Update({0.0f, 0, 0}, 0.0f, dt, s);
    Require(Length(j.Update({1.5f, 0, 0}, 0.0f, dt, s)) == 0.0f && j.Jumped(), "a jump is refused");
    Require(Length(j.Update({1.5f, 0, 0}, 0.0f, dt, s)) > 0.0f && !j.Jumped(), "then followed at the speed cap");
    // The move made is no jump: the next offset is less by it.
    Follower k;
    k.Update({0.0f, 0, 0}, 0.0f, dt, s);
    const Vec3 big = k.Update({0.4f, 0, 0}, 0.0f, 0.2f, s);
    k.Applied(big);
    k.Update(Sub(Vec3{0.4f, 0, 0}, big), 0.0f, dt, s);
    Require(!k.Jumped(), "the follower expects its own move");
    Require(Length(f.Update({0.2f, 0, 0}, 0.0f, 0.0f, s)) == 0.0f && Length(f.Update({0.2f, 0, 0}, 0.0f, 0.5f, s)) == 0.0f,
            "no move without a sane frame time");
    Require(Length(f.Update({NAN, 0, 0}, 0.0f, dt, s)) == 0.0f, "non-finite input resets");
}

// **The lean zone.** Leaning 15-25 cm from the hips -- forward, sideways, back
// -- and holding it, the head lowered as a hinge lowers it: the capsule stays,
// and the body pin bends the torso. Hinges of 0.7 to 0.8 m, longer than the
// follower's own 0.6 m, lower the head least: the hardest case.
void TestLeaningIsNotAStep()
{
    const float dt = 1.0f / 90.0f;
    struct Lean { float x, z, trunk; };
    for (const Lean lean : {Lean{0.0f, -0.20f, 0.75f}, Lean{0.15f, 0.0f, 0.7f}, Lean{-0.15f, 0.0f, 0.8f},
                            Lean{0.0f, 0.20f, 0.8f}, Lean{0.17f, -0.17f, 0.75f}}) {
        World w;
        const float lead = std::hypot(lean.x, lean.z);
        // Into the lean over half a second, held for three, back over half a second.
        for (int i = 0; i <= 360; ++i) {
            const float t = i < 45 ? i / 45.0f : i < 315 ? 1.0f : std::fmax(0.0f, (360 - i) / 45.0f);
            const float drop = HingeDrop(lead * t, lean.trunk);
            Require(Length(w.Step({lean.x * t, 1.6f - drop, lean.z * t}, dt, drop)) == 0.0f,
                    "a lean, held, leaves the capsule where it is");
            Require(!w.follower.Settling(), "a lean is never settled under");
        }
        Require(Length(Sub(w.capsule, World{}.capsule)) == 0.0f, "the lean taken back: nothing moved");
    }
    // The borderline 15 cm lean, held with a tracker's noise in its height and
    // a person's sway: still a lean.
    {
        World w;
        unsigned seed = 12345u;
        const auto noise = [&seed](float amplitude) {
            seed = seed * 1664525u + 1013904223u;
            return amplitude * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
        };
        for (int i = 0; i <= 360; ++i) {
            const float t = std::fmin(i / 45.0f, 1.0f);
            const float drop = HingeDrop(0.15f * t, 0.75f) + noise(0.003f);
            const float sway = 0.005f * std::sin(i * 0.05f);
            Require(Length(w.Step({sway, 1.6f - drop, -0.15f * t}, dt, drop)) == 0.0f, "a noisy held lean stays a lean");
        }
    }
    // Leaning further than the zone, the capsule comes after the rest.
    World w;
    for (int i = 0; i <= 90; ++i) {
        const float lead = 0.32f * std::fmin(i / 45.0f, 1.0f);
        w.Step({0.0f, 1.6f, -lead}, dt, HingeDrop(lead, 0.75f));
    }
    Require(std::fabs(w.Away({0.0f, 1.6f, -0.32f}) - w.settings.leanZone) < 0.005f, "past the zone: followed to it");
}

// Lean Zone (VR Options): a wider zone holds a further lean; with none, the
// capsule follows the head past the deadzone, as before there was one, and
// only bending down bends the body.
void TestLeanZoneSetting()
{
    Settings s;
    s.leanZone = 0.40f;
    Require(LeanAllowance(0.0f, s) == 0.40f, "a wider zone allows more");
    s.leanZone = 0.0f;
    Require(LeanAllowance(0.0f, s) == s.deadzone && LeanAllowance(0.10f, s) > s.deadzone + 0.24f,
            "none: the deadzone, and bending down still allows the bend");
    // A 30 cm lean over half a second, held a moment.
    const float dt = 1.0f / 90.0f;
    World wide, none;
    wide.settings.leanZone = 0.40f;
    none.settings.leanZone = 0.0f;
    const auto head = [](float t) { return Vec3{0.0f, 1.6f - HingeDrop(0.30f * t, 0.75f), -0.30f * t}; };
    for (int i = 0; i <= 60; ++i) {
        const float t = std::fmin(i / 45.0f, 1.0f);
        const float drop = HingeDrop(0.30f * t, 0.75f);
        Require(Length(wide.Step(head(t), dt, drop)) == 0.0f, "a 30 cm lean in a 40 cm zone: held");
        none.Step(head(t), dt, drop);
    }
    // Followed 2 cm behind until the drop explained the rest of the lean.
    Require(none.Away(head(1.0f)) < 0.10f && Length(Sub(none.capsule, World{}.capsule)) > 0.2f,
            "with none, the capsule followed the lean");
}

// **Settling.** A step within the zone moves nothing while it is made; standing
// there upright, the capsule glides under the head, never faster than
// settleSpeed, and the view never moves for it.
void TestStepsSettleUnderYou()
{
    World w;
    const float dt = 1.0f / 90.0f;
    const Vec3 start{0.0f, 1.6f, 0.0f};
    const Vec3 eye0 = w.eye(start);
    bool settled = false, glided = false;
    for (int i = 0; i <= 270; ++i) {
        // 22 cm to the right in a third of a second, then standing there, swaying
        // a centimetre.
        const float t = std::fmin(i / 30.0f, 1.0f);
        const float sway = i > 30 ? 0.01f * std::sin(i * 0.2f) : 0.0f;
        const Vec3 head{0.22f * t + sway, 1.6f, 0.0f};
        const Vec3 move = w.Step(head, dt);
        Require(Near(w.eye(head), Add(eye0, HeadOffsetWorld(head, start, w.yaw, w.scale)), 1e-4f),
                "settling never moves the view");
        if (i <= 30) { Require(Length(move) == 0.0f, "a step within the zone moves nothing while it is made"); }
        if (i < 30 + 45) { Require(Length(move) == 0.0f, "nor until the head has been still for the delay"); }
        Require(Length(move) <= w.settings.settleSpeed * dt + 1e-6f, "the glide is gentle");
        glided = glided || w.follower.Settling();
        settled = settled || (glided && !w.follower.Settling());
    }
    Require(glided && settled, "it glided, then stopped");
    Require(w.Away({0.22f, 1.6f, 0.0f}) <= w.settings.deadzone + 0.012f, "and the body is under the head");
}

// **Upright is learnt** standing still over the body, so a stance more relaxed
// than the calibrated one is not taken for a lean.
void TestUprightIsLearnt()
{
    const float dt = 1.0f / 90.0f;
    const float relaxed = 0.03f;   // 3 cm below the calibrated height
    World learnt;
    for (int i = 0; i < 270; ++i) { learnt.Step({0.0f, 1.6f - relaxed, 0.0f}, dt, relaxed); }
    Require(std::fabs(learnt.follower.Upright() - relaxed) < 0.01f, "three seconds over the body: learnt");
    World fresh;
    for (World* w : {&learnt, &fresh}) {
        for (int i = 0; i <= 180; ++i) {
            const float t = std::fmin(i / 30.0f, 1.0f);
            w->Step({0.0f, 1.6f - relaxed, -0.2f * t}, dt, relaxed);
        }
    }
    Require(learnt.Away({0.0f, 1.6f - relaxed, -0.2f}) <= learnt.settings.deadzone + 0.012f,
            "learnt: a step at the relaxed height settles");
    Require(fresh.Away({0.0f, 1.6f - relaxed, -0.2f}) > 0.19f,
            "unlearnt, the same 3 cm reads as a lean and is held");
    // A missed frame forgets the frames, not the stance; a recenter forgets both.
    const float upright = learnt.follower.Upright();
    learnt.follower.Reset();
    Require(learnt.follower.Upright() == upright, "a reset keeps what upright is");
    learnt.follower.ResetUpright();
    Require(learnt.follower.Upright() == 0.0f, "a recenter takes it back to the calibrated height");
}

// A settle stops when the head moves on, and starts again only once it rests.
void TestMovingOnStopsTheSettle()
{
    World w;
    const float dt = 1.0f / 90.0f;
    int i = 0;
    for (; i <= 120 && !w.follower.Settling(); ++i) { w.Step({0.24f * std::fmin(i / 20.0f, 1.0f), 1.6f, 0.0f}, dt); }
    Require(w.follower.Settling(), "standing after a step: settling");
    for (int k = 0; k < 9; ++k) { w.Step({0.24f, 1.6f, 0.0f}, dt); }
    // Leaning on, 10 cm further over a fifth of a second, the head lowered with it.
    const Vec3 before = w.capsule;
    for (int k = 1; k <= 18; ++k) {
        const float x = 0.24f + 0.1f * k / 18.0f;
        w.Step({x, 1.6f - HingeDrop(0.1f * k / 18.0f, 0.75f), 0.0f}, dt, HingeDrop(0.1f * k / 18.0f, 0.75f));
        if (k > 6) { Require(!w.follower.Settling(), "the head moved on: the settle stopped"); }
    }
    Require(LevelLength(Sub(w.capsule, before)) < 0.06f, "it moved little after the head did");
}

// The stance test's verdict along a move: a desk edge 0.37 of the way along
// stops the body short of it, never in it.
void TestFitFraction()
{
    int calls = 0;
    const auto clear = [&](float) { ++calls; return true; };
    Require(FitFraction(clear, 6) == 1.0f && calls == 1, "a clear move is asked once and taken whole");
    float asked = 0.0f;
    const auto desk = [&](float k) { asked = std::max(asked, k <= 0.37f ? k : 0.0f); return k <= 0.37f; };
    const float k = FitFraction(desk, 6);
    Require(k <= 0.37f && k > 0.37f - 1.0f / 64.0f, "bisected to within 1/64 of the contact, on the clear side");
    Require(asked == k, "the fraction taken is one that was found to fit");
    Require(FitFraction([](float k) { return k == 0.0f; }, 6) == 0.0f, "touching already: no move");
}

void TestNoDeeper()
{
    Require(NoDeeper(0.0f, 0.0f) && NoDeeper(0.0f, 0.02f), "clear there: fits");
    Require(!NoDeeper(0.005f, 0.0f), "from clear, any overlap refuses");
    Require(NoDeeper(0.01f, 0.02f) && NoDeeper(0.0205f, 0.02f), "against a wall: no deeper fits");
    Require(!NoDeeper(0.03f, 0.02f), "but further into it does not");
    Require(!NoDeeper(std::nanf(""), 0.0f) && !NoDeeper(0.01f, std::nanf("")), "an unread depth refuses");
}

// A body that is walking, falling or pushed keeps its motion through a move.
void TestBodyMoving()
{
    Require(!BodyMoving({0, 0, 0}, {0, 0, 0}), "at rest");
    Require(!BodyMoving({0.02f, 0, 0}, {0, 0.01f, 0}), "a settling crawl is rest");
    Require(BodyMoving({0, 0, 0}, {0, 2.0f, 0}), "the stick asks to walk: moving, before the body has sped up");
    Require(BodyMoving({1.5f, 0, 0}, {0, 0, 0}), "coasting to a stop: moving");
    Require(BodyMoving({0, 0, -3.0f}, {0, 0, 0}), "falling: moving");
    Require(BodyMoving({NAN, 0, 0}, {0, 0, 0}), "an unreadable velocity keeps the motion");
}

} // namespace

int main()
{
    TestShiftCancelsTheMove();
    TestLeanAllowance();
    TestWalkingNeverMovesTheView();
    TestNativeMovesAreNotAbsorbed();
    TestBendingDownIsNotAStep();
    TestLeaningIsNotAStep();
    TestLeanZoneSetting();
    TestStepsSettleUnderYou();
    TestUprightIsLearnt();
    TestMovingOnStopsTheSettle();
    TestSpeedAndJumps();
    TestFitFraction();
    TestNoDeeper();
    TestBodyMoving();
    std::cout << "room_scale: all tests passed\n";
    return 0;
}
