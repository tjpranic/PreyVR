#include "body/BodyRig.h"
#include "TestGeometry.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::bodyrig;

namespace {

using preyvr::test::Require;
using preyvr::test::kDeg;
using preyvr::test::AxisAngle;

bool Near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) <= e; }
float Dist(Vec3 a, Vec3 b) { return Length(Sub(a, b)); }
bool NearVec(Vec3 a, Vec3 b, float e = 1e-3f) { return Dist(a, b) <= e; }
float AngleBetween(Quaternion a, Quaternion b)
{
    const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.0f * std::acos(std::fmin(1.0f, d));
}

void TestSubtree()
{
    // Shaped like the player's rig: root 0 -> pelvis 1 -> spine 2 -> neck 3 ->
    // head 4 (the camera) -> headEnd 5; neck -> clavicle 6; pelvis -> thigh 7 ->
    // calf 8 -> foot 9; root -> a prop 10.
    const int parent[] = {-1, 0, 1, 2, 3, 4, 3, 1, 7, 8, 0};
    bool in[11]{};
    Require(Subtree(parent, 11, 7, 4, in), "a leg is a subtree");
    const bool leg[] = {false, false, false, false, false, false, false, true, true, true, false};
    for (int j = 0; j < 11; ++j) { Require(in[j] == leg[j], "the thigh carries the calf and the foot, nothing else"); }

    Require(Subtree(parent, 11, 1, 4, in), "the pelvis is a subtree");
    const bool body[] = {false, true, true, true, false, false, true, true, true, true, false};
    for (int j = 0; j < 11; ++j) { Require(in[j] == body[j], "the pelvis carries the body, but not the camera's head"); }

    Require(Subtree(parent, 11, 1, -1, in) && in[4] && in[5], "with no joint excluded the head goes too");
    Require(!Subtree(parent, 11, 4, 4, in) && !in[4], "the excluded joint itself is refused");
    Require(!Subtree(parent, 11, 11, -1, in) && !Subtree(parent, 11, -1, -1, in), "a root outside the rig is refused");

    const int looped[] = {1, 0, 0};
    Require(Subtree(looped, 3, 2, -1, in) && in[2] && !in[0] && !in[1], "a cyclic table carries nothing past the cycle");
}

void TestCarry()
{
    const Joint parentOld{AxisAngle({0, 0, 1}, 0.3f), {0.1f, 0.2f, 1.0f}};
    const Joint child{AxisAngle({1, 0, 0}, 0.5f), {0.1f, 0.5f, 1.2f}};
    const Joint parentNew{AxisAngle({0, 1, 0}, 0.4f), {-0.3f, 0.0f, 0.6f}};
    const Joint carried = Carry(parentOld, parentNew, child);
    // The child's transform relative to its parent is kept.
    const Vec3 before = Rotate(Conjugate(parentOld.q), Sub(child.t, parentOld.t));
    const Vec3 after = Rotate(Conjugate(parentNew.q), Sub(carried.t, parentNew.t));
    Require(NearVec(before, after, 1e-5f), "a carried child keeps its offset in its parent's frame");
    Require(AngleBetween(Multiply(Conjugate(parentOld.q), child.q), Multiply(Conjugate(parentNew.q), carried.q)) < 1e-4f,
            "and its rotation relative to it");
    const Joint same = Carry(parentOld, parentOld, child);
    Require(NearVec(same.t, child.t, 1e-6f) && AngleBetween(same.q, child.q) < 1e-4f, "a parent that stays leaves it");
}

// The player's rig as the research build dumped it (PreyVR-84, run
// player-20261025-125133): the pinned joints, the camera joint and the throat,
// before the native pass, standing and in Prey's crouch.
struct Measured {
    Joint pinned[kPinnedCount];
    Joint camera, throat;
    Vec3 spine2Offset;
};

const Measured kStand{
    {
        {{0.0874f, -0.7014f, 0.1219f, 0.6968f}, {0.0015f, 0.0004f, 0.9302f}},     // hip_jnt
        {{0.0168f, -0.7069f, -0.0168f, 0.7069f}, {0.0000f, 0.0056f, 1.0402f}},    // spine1_jnt
        {{0.0600f, -0.7046f, -0.0600f, 0.7045f}, {0.0000f, -0.0398f, 1.1657f}},   // spine2_jnt
        {{0.0449f, -0.7057f, -0.0449f, 0.7057f}, {0.0000f, -0.0644f, 1.3092f}},   // spine3_jnt
        {{0.0000f, -0.7071f, 0.0000f, 0.7071f}, {0.0000f, -0.0789f, 1.4231f}},    // spine4_jnt
        {{-0.0842f, -0.7021f, 0.0842f, 0.7021f}, {0.0000f, -0.1063f, 1.5301f}},   // neck_jnt
        {{0.1468f, 0.7090f, 0.0620f, 0.6869f}, {0.0876f, 0.0241f, 0.9605f}},      // r_upperLeg_jnt
        {{0.0012f, 0.7208f, -0.0695f, 0.6897f}, {0.0820f, 0.1483f, 0.5560f}},     // r_lowerLeg_jnt
        {{-0.0271f, 0.7126f, 0.0388f, 0.7000f}, {0.0615f, 0.1084f, 0.1352f}},     // r_foot_jnt
        {{-0.1642f, 0.7470f, 0.0873f, 0.6382f}, {-0.0831f, -0.0282f, 0.9606f}},   // l_upperLeg_jnt
        {{-0.3111f, 0.7003f, -0.0520f, 0.6404f}, {-0.1387f, -0.0848f, 0.5450f}},  // l_lowerLeg_jnt
        {{-0.2224f, 0.6731f, -0.0039f, 0.7053f}, {-0.1329f, -0.2973f, 0.1791f}},  // l_foot_jnt
    },
    {{0.0f, 0.0001f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.7000f}},                         // camera_jnt
    {{0.0f, 0.7709f, 0.6370f, 0.0f}, {0.0004f, -0.0343f, 1.5517f}},               // throat_jnt
    {0.1276f, -0.0393f, 0.0f},
};

const Measured kCrouch{
    {
        {{-0.1405f, -0.6930f, -0.1050f, 0.6993f}, {-0.0019f, -0.1997f, 0.5089f}},
        {{0.0168f, -0.7069f, -0.0168f, 0.7069f}, {0.0000f, -0.1944f, 0.6189f}},
        {{0.0600f, -0.7046f, -0.0600f, 0.7046f}, {0.0000f, -0.0397f, 0.4657f}},
        {{0.0449f, -0.7057f, -0.0449f, 0.7057f}, {0.0000f, -0.0644f, 0.6092f}},
        {{0.0000f, -0.7071f, 0.0000f, 0.7071f}, {0.0000f, -0.0789f, 0.7231f}},
        {{-0.0841f, -0.7021f, 0.0841f, 0.7021f}, {0.0000f, -0.1063f, 0.8300f}},
        {{0.3250f, 0.5286f, 0.0140f, 0.7841f}, {0.0811f, -0.2326f, 0.5393f}},
        {{-0.3352f, 0.5337f, -0.6747f, 0.3842f}, {0.2676f, -0.0778f, 0.1924f}},
        {{-0.0861f, 0.7018f, -0.3851f, 0.5931f}, {0.0645f, -0.4487f, 0.2103f}},
        {{0.2518f, 0.6766f, 0.3458f, 0.5993f}, {-0.0866f, -0.1715f, 0.5393f}},
        {{-0.4613f, 0.5509f, -0.3785f, 0.5835f}, {-0.1522f, 0.1481f, 0.2698f}},
        {{-0.0456f, 0.7056f, 0.0456f, 0.7056f}, {-0.1071f, -0.2539f, 0.1455f}},
    },
    {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0000f}},
    {{0.0f, 0.7708f, 0.6370f, 0.0f}, {0.0004f, -0.0343f, 0.8517f}},
    {-0.1604f, 0.1472f, 0.0f},
};

// The rig's atlas: the game's eye less kNeckFromEye, in the eye's frame.
Vec3 RigAtlas(const Joint& camera)
{
    const Vec3& k = armpose::kNeckFromEye;
    return Add(camera.t, Rotate(camera.q, Vec3{k.x, -k.z, k.y}));
}

// A pitch down of `pitch` radians, the head facing +y.
Quaternion HeadDown(float pitch) { return AxisAngle({1, 0, 0}, -pitch); }

// The input for a player whose atlas is `move` from the rig's, the head
// pitched down `pitch` (the eye swung about the atlas), the waist straightened.
PinInput InputFor(const Measured& m, Vec3 move, float pitch)
{
    PinInput in{};
    for (int k = 0; k < kPinnedCount; ++k) { in.native[k] = {Normalize(m.pinned[k].q), m.pinned[k].t}; }
    in.atlas = RigAtlas(m.camera);
    in.throat = m.throat.t;
    const Vec3 shift = StraightenShift(m.pinned[kSpine1].q, m.spine2Offset, Vec3{0.1255f, 0.0f, 0.0f}, 0.05f);
    for (int k = kSpine2; k <= kNeck; ++k) { in.native[k].t = Add(in.native[k].t, shift); }
    in.atlas = Add(in.atlas, shift);
    in.throat = Add(in.throat, shift);
    in.realAtlas = Add(RigAtlas(m.camera), move);
    const Vec3 eyeFromAtlas = Sub(m.camera.t, RigAtlas(m.camera));
    in.eye = Add(in.realAtlas, Rotate(HeadDown(pitch), eyeFromAtlas));
    in.facing = {0, 1, 0};
    in.pitch = pitch;
    return in;
}

// Every bone keeps its length, the spine's segments and both legs', within the
// half percent a leg's solve may stretch near straight.
bool BonesKept(const PinInput& in, const PinResult& r)
{
    const int chain[][2] = {{kPelvis, kSpine1}, {kSpine1, kSpine2}, {kSpine2, kSpine3}, {kSpine3, kSpine4},
                            {kSpine4, kNeck}, {kUpperLegR, kLowerLegR}, {kLowerLegR, kFootR},
                            {kUpperLegL, kLowerLegL}, {kLowerLegL, kFootL}};
    for (const auto& b : chain) {
        const float before = Dist(in.native[b[0]].t, in.native[b[1]].t);
        const float after = Dist(r.pose[b[0]].t, r.pose[b[1]].t);
        if (!(std::fabs(before - after) <= 1e-3f + 0.006f * before)) { return false; }
    }
    return true;
}

// Where the throat goes with the neck (it hangs from neckTwist, which hangs
// from the neck unturned).
Vec3 ThroatAfter(const PinInput& in, const PinResult& r)
{
    return Carry(in.native[kNeck], r.pose[kNeck], Joint{{}, in.throat}).t;
}

void TestStandingIsTheGame()
{
    const PinInput in = InputFor(kStand, {}, 0.0f);
    const PinResult r = SolvePin(in, PinSettings{});
    Require(r.valid, "a standing rig is posed");
    for (int k = 0; k < kPinnedCount; ++k) {
        Require(NearVec(r.pose[k].t, in.native[k].t, 2e-3f) && AngleBetween(r.pose[k].q, in.native[k].q) < 0.01f,
                "standing at the calibrated height, level, the body is the game's");
    }
    Require(r.drop < 1e-3f && r.throatPush == 0.0f && r.sink == 0.0f && !r.folded, "and nothing was needed");
}

void TestSquat()
{
    const PinInput in = InputFor(kStand, {0.0f, 0.0f, -0.30f}, 0.0f);
    const PinResult r = SolvePin(in, PinSettings{});
    Require(r.valid && r.shortfall < 5e-3f, "a 30 cm squat: the neck reaches its goal");
    Require(Near(r.pose[kNeck].t.z, in.native[kNeck].t.z - 0.30f, 5e-3f), "the neck drops with the player's");
    Require(r.pose[kPelvis].t.z < in.native[kPelvis].t.z - 0.25f, "the pelvis drops too");
    Require(r.pose[kPelvis].t.y < in.native[kPelvis].t.y - 0.10f, "and goes back");
    Require(BonesKept(in, r), "no bone stretches or shrinks");
    Require(NearVec(r.pose[kFootR].t, in.native[kFootR].t, 1e-4f) && NearVec(r.pose[kFootL].t, in.native[kFootL].t, 1e-4f),
            "the feet stay where the game planted them");
    for (const int knee : {kLowerLegR, kLowerLegL}) {
        const Vec3 hip = r.pose[knee - 1].t, ankle = r.pose[knee + 1].t, k = r.pose[knee].t;
        const Vec3 axis = Scale(Sub(ankle, hip), 1.0f / Dist(ankle, hip));
        const Vec3 off = Sub(Sub(k, hip), Scale(axis, Dot(Sub(k, hip), axis)));
        Require(off.y > 0.10f, "the knees bend forward");
    }
    Require(r.leanDeg < 30.0f, "the back stays fairly upright");
    // The spine bends; no joint goes below the one under it.
    for (int k = kSpine1; k <= kNeck; ++k) { Require(r.pose[k].t.z > r.pose[k - 1].t.z, "the spine still rises"); }
}

// The deepest crouch the research build measured (2026-10-25): the eye 70 cm
// down and 11 cm forward.
void TestDeepCrouch()
{
    const PinInput in = InputFor(kStand, {0.0f, 0.107f, -0.70f}, 0.0f);
    const PinResult r = SolvePin(in, PinSettings{});
    Require(r.valid && r.shortfall < 0.02f, "a 70 cm crouch: the neck reaches its goal");
    Require(BonesKept(in, r), "no bone stretches");
    const PinSettings s{};
    for (const int hip : {kUpperLegR, kUpperLegL}) {
        Require(Dist(r.pose[hip].t, r.pose[hip + 2].t) >= s.legFold - 0.01f, "the legs fold no further than a squat's");
    }
    Require(r.leanDeg > 25.0f && r.leanDeg < 70.0f, "the back leans into the crouch");
    for (int k = kSpine1; k <= kNeck; ++k) { Require(r.pose[k].t.z > r.pose[k - 1].t.z - 0.02f, "the spine does not fold under"); }
}

// Looking straight down put the eyes on the throat joint (PreyVR-84).
void TestLookingDown()
{
    const PinSettings s{};
    // As people look down: the neck bends half the way and the head the rest,
    // so the atlas comes forward and down. The body stays; the neck bends.
    const Vec3 toAtlas = Sub(RigAtlas(kStand.camera), kStand.pinned[kNeck].t);
    const Vec3 flexed = Sub(Rotate(AxisAngle({1, 0, 0}, -45.0f * kDeg), toAtlas), toAtlas);
    const PinInput in = InputFor(kStand, flexed, 90.0f * kDeg);
    const PinResult r = SolvePin(in, s);
    Require(r.valid && r.shortfall < 2e-3f, "looking straight down is posed");
    for (int k = kPelvis; k <= kSpine4; ++k) { Require(NearVec(r.pose[k].t, in.native[k].t, 3e-3f), "the body stays"); }
    Require(Near(AngleBetween(r.pose[kNeck].q, in.native[kNeck].q), 45.0f * kDeg, 1.0f * kDeg), "the neck bends half the pitch");
    const Vec3 gap = Sub(in.eye, ThroatAfter(in, r));
    Require(gap.y > 0.07f && r.throatPush == 0.0f, "the throat is well behind the eyes");
    // Tipping only the head: the neck's base would rise, as far as the legs let it.
    const PinInput tipped = InputFor(kStand, {}, 90.0f * kDeg);
    const PinResult t = SolvePin(tipped, s);
    Require(t.valid && Sub(tipped.eye, ThroatAfter(tipped, t)).y > 0.06f, "the throat stays behind the eyes");
    Require(t.pose[kPelvis].t.z - tipped.native[kPelvis].t.z < 0.03f, "the body does not climb");
    // A neck that does not bend: the eye sits 3 cm in front of the throat, and
    // the clearance pushes the neck back to keep it behind.
    PinSettings stiff{};
    stiff.neckFlex = 0.0f;
    const PinResult held = SolvePin(tipped, stiff);
    const Vec3 heldGap = Sub(tipped.eye, ThroatAfter(tipped, held));
    const float need = stiff.throatBack * std::fmin(1.0f, std::fmax(0.0f, 1.0f - heldGap.z / stiff.throatDown));
    Require(held.valid && held.throatPush > 0.03f, "a stiff neck is pushed back");
    Require(heldGap.y >= need - 5e-3f, "until the throat is behind the eyes");
}

void TestLean()
{
    const PinInput in = InputFor(kStand, {0.0f, 0.30f, -0.08f}, 30.0f * kDeg);
    const PinResult r = SolvePin(in, PinSettings{});
    Require(r.valid && r.shortfall < 0.01f, "leaning 30 cm over a desk: the neck follows");
    Require(r.leanDeg > 15.0f && r.leanDeg < 45.0f, "the back leans");
    Require(r.pose[kPelvis].t.y < in.native[kPelvis].t.y + 0.02f, "the hips do not follow the head forward");
    Require(BonesKept(in, r), "no bone stretches");
    // Sideways.
    const PinInput side = InputFor(kStand, {0.20f, 0.0f, -0.03f}, 0.0f);
    const PinResult s = SolvePin(side, PinSettings{});
    Require(s.valid && s.shortfall < 0.01f && Near(s.pose[kNeck].t.x, 0.20f, 0.01f) && s.pose[kSpine4].t.x > 0.10f,
            "a lean to the side bends the spine sideways");
    // Stepped away: beyond a lean, the body stays upright where it is.
    const PinInput away = InputFor(kStand, {0.0f, 1.0f, 0.0f}, 0.0f);
    const PinResult a = SolvePin(away, PinSettings{});
    Require(a.valid && NearVec(a.pose[kNeck].t, away.native[kNeck].t, 0.01f), "a metre away, the body stays as it was");
}

// Hips and Legs Back. Prey's standing idle has the hips under the eyes and the
// neck 10.6 cm behind them: the back leans 10 degrees back. Moved back the 8 cm
// from the eyes to the head's pivot, the pelvis and the feet go together, the
// legs as they were, and the back stands near upright under the same neck.
void TestHipsBack()
{
    const PinInput in = InputFor(kStand, {}, 0.0f);
    PinSettings s{};
    s.hipsBack = 0.08f;
    const PinResult r = SolvePin(in, s);
    const PinResult game = SolvePin(in, PinSettings{});
    Require(r.valid && r.shortfall < 2e-3f, "hips back: the neck stays under the head");
    const auto level = [](Vec3 v) { return Vec3{v.x, v.y, 0.0f}; };
    const Vec3 back{0.0f, -0.08f, 0.0f};
    Require(NearVec(level(r.pose[kPelvis].t), level(Add(in.native[kPelvis].t, back)), 3e-3f) &&
                Near(r.pose[kPelvis].t.z, in.native[kPelvis].t.z, 0.02f),
            "the pelvis stands 8 cm back");
    for (const int hip : {kUpperLegR, kUpperLegL}) {
        Require(NearVec(r.pose[hip + 2].t, Add(in.native[hip + 2].t, back), 1e-4f), "the feet go back with it");
        Require(NearVec(level(Sub(r.pose[hip + 2].t, r.pose[hip].t)), level(Sub(in.native[hip + 2].t, in.native[hip].t)), 0.01f),
                "each foot stays where it was from its hip");
    }
    Require(BonesKept(in, r), "no bone stretches");
    Require(game.leanDeg > 9.0f && r.leanDeg < 4.0f, "the back no longer leans back");
    Require(r.drop < 1e-3f && r.sink == 0.0f && !r.folded, "and nothing else was needed");
}

void TestTwist()
{
    PinInput in = InputFor(kStand, {}, 0.0f);
    in.twist = -30.0f * kDeg;
    const PinResult r = SolvePin(in, PinSettings{});
    Require(r.valid, "a turned chest is posed");
    const Vec3 chest = Rotate(Multiply(r.pose[kSpine4].q, Conjugate(in.native[kSpine4].q)), Vec3{0, 1, 0});
    Require(Near(TurnAbout(Vec3{0, 1, 0}, chest), -30.0f * kDeg, 2.0f * kDeg), "the chest turns right with the stance");
    // Turned, the spine's backward curve swings the neck a few centimetres
    // aside, and the bend that brings it back tips the pelvis a little.
    Require(AngleBetween(r.pose[kPelvis].q, in.native[kPelvis].q) < 5.0f * kDeg, "the pelvis stays on the legs");
    Require(r.shortfall < 5e-3f, "the neck stays under the head");
    const Vec3 neckFacing = Rotate(Multiply(r.pose[kNeck].q, Conjugate(in.native[kNeck].q)), Vec3{0, 1, 0});
    Require(std::fabs(TurnAbout(Vec3{0, 1, 0}, neckFacing)) < 5.0f * kDeg, "the neck turns back to the head");
}

// Prey's crouch kinks the waist: spine2 0.15 m below spine1.
void TestPreyCrouch()
{
    Require(kCrouch.pinned[kSpine2].t.z < kCrouch.pinned[kSpine1].t.z - 0.1f, "the game's crouch kinks the waist");
    const PinInput in = InputFor(kCrouch, {}, 0.0f);
    const PinResult r = SolvePin(in, PinSettings{});
    Require(r.valid && r.shortfall < 0.02f, "standing still in Prey's crouch: the neck stays under the head");
    Require(NearVec(r.pose[kNeck].t, kCrouch.pinned[kNeck].t, 0.02f), "where the game's crouch put it");
    for (int k = kSpine1; k <= kNeck; ++k) { Require(r.pose[k].t.z > r.pose[k - 1].t.z, "the waist is no longer kinked"); }
    Require(BonesKept(in, r), "no bone stretches");
}

void TestStraighten()
{
    const Quaternion spine1 = kStand.pinned[kSpine1].q;
    const Vec3 rest{0.1255f, 0.0f, 0.0f};
    Require(Length(StraightenShift(spine1, kStand.spine2Offset, rest, 0.05f)) == 0.0f, "the standing idle's waist stays");
    const Vec3 shift = StraightenShift(spine1, kCrouch.spine2Offset, rest, 0.05f);
    // The crouch's offset is 0.32 from the rest: all but 5 cm of it undone, up the spine.
    Require(Near(Length(shift), Length(Sub(kCrouch.spine2Offset, rest)) - 0.05f, 1e-3f), "the crouch's kink is undone");
    Require(shift.z > 0.2f, "which lifts the upper body");
}

void TestHeadAngles()
{
    Require(NearVec(FacingOf(Vec3{0, 1, 0}, Vec3{0, 0, 1}), Vec3{0, 1, 0}, 1e-5f), "level: the facing is forward");
    Require(NearVec(FacingOf(Vec3{0, 0, -1}, Vec3{0, 1, 0}), Vec3{0, 1, 0}, 1e-5f), "straight down: the up says forward");
    Require(NearVec(FacingOf(Vec3{0, 0, 1}, Vec3{0, -1, 0}), Vec3{0, 1, 0}, 1e-5f), "straight up: so does the up");
    Require(Near(PitchOf(Vec3{0, std::cos(0.5f), -std::sin(0.5f)}), 0.5f, 1e-5f), "pitch is positive looking down");
    Require(Near(TurnAbout(Vec3{0, 1, 0}, Vec3{-1, 0, 0}), 90.0f * kDeg, 1e-5f), "a turn left is positive");
    Require(Near(TurnAbout(Vec3{0, 1, 0.5f}, Vec3{1, 0, -2}), -90.0f * kDeg, 1e-5f), "measured level");
}

// The arm's shoulder from the atlas is where armpose's hangs from the eye.
void TestShoulderFromAtlas()
{
    const armpose::Body body{};
    const Pose head{};   // level, facing -Z, at the origin
    const Vec3 atlasXr = Rotate(head.orientation, armpose::kNeckFromEye);
    for (const armpose::Side side : {+1, -1}) {
        const Vec3 xr = Sub(armpose::ShoulderFromHead(head, 0.0f, side, body), atlasXr);
        const Vec3 model{xr.x, -xr.z, xr.y};
        Require(NearVec(ShoulderFromAtlas(body, side), model, 1e-5f), "the shoulder hangs from the atlas as from the eye");
    }
    Require(ShoulderFromAtlas(body, +1).x > 0.15f && ShoulderFromAtlas(body, -1).x < -0.15f, "out to its own side");
}

} // namespace

int main()
{
    TestSubtree();
    TestCarry();
    TestStandingIsTheGame();
    TestSquat();
    TestDeepCrouch();
    TestLookingDown();
    TestLean();
    TestHipsBack();
    TestTwist();
    TestPreyCrouch();
    TestStraighten();
    TestHeadAngles();
    TestShoulderFromAtlas();
    std::cout << "body rig: ok\n";
    return 0;
}
