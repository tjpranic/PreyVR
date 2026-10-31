#include "physics/hands/HandCarry.h"
#include "render/camera/StereoCamera.h"
#include "TestGeometry.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::carry;

namespace {

using preyvr::test::Require;
bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
bool NearV(Vec3 a, Vec3 b, float e = 1e-3f) { return Near(a.x, b.x, e) && Near(a.y, b.y, e) && Near(a.z, b.z, e); }
using preyvr::test::AxisAngle;
constexpr std::int64_t kFrame = 11111111;   // 90 Hz, ns

// --- motion -------------------------------------------------------------------------

void TestConstantVelocity()
{
    MotionHistory h;
    const Vec3 v{1.0f, 2.0f, -0.5f};
    for (int i = 0; i < 30; ++i) {
        MotionSample s{};
        s.time = 1000000000ll + i * kFrame;
        s.position = Scale(v, static_cast<float>(i * kFrame) * 1e-9f);
        h.Push(s);
    }
    const Motion m = h.Estimate();
    Require(m.valid && !m.fromRuntime, "finite differences give a motion");
    Require(NearV(m.velocity, v, 0.02f), "constant velocity recovered");
}

// The hand brakes as it opens: the estimate must be the peak, not the release.
void TestPeakNotRelease()
{
    MotionHistory h;
    float x = 0;
    for (int i = 0; i < 40; ++i) {
        const float t = static_cast<float>(i) / 39.0f;
        // speed ramps up to 6 m/s at 70% of the motion, then down to 1.5 m/s
        const float speed = t < 0.7f ? 6.0f * t / 0.7f : 6.0f - 4.5f * (t - 0.7f) / 0.3f;
        x += speed * static_cast<float>(kFrame) * 1e-9f;
        MotionSample s{};
        s.time = 2000000000ll + i * kFrame;
        s.position = {x, 0, 0};
        h.Push(s);
    }
    const Motion m = h.Estimate();
    Require(m.valid, "valid");
    Require(m.speed > 5.0f, "the peak of the last 0.12 s, not the braking release speed");
    Require(m.velocity.x > 0, "direction kept");
}

void TestRuntimePreferred()
{
    MotionHistory h;
    for (int i = 0; i < 20; ++i) {
        MotionSample s{};
        s.time = 3000000000ll + i * kFrame;
        s.position = {0, 0, static_cast<float>(i) * 0.01f};   // poses say 0.9 m/s
        s.velocityValid = true;
        s.velocity = {0, 3.0f, 0};                            // runtime says 3 m/s up
        s.angular = {0, 0, 2.0f};
        h.Push(s);
    }
    const Motion m = h.Estimate();
    Require(m.valid && m.fromRuntime, "runtime velocity used when reported");
    Require(NearV(m.velocity, {0, 3, 0}, 1e-3f) && NearV(m.angular, {0, 0, 2}, 1e-3f), "runtime values");
}

void TestAngular()
{
    MotionHistory h;
    const float rate = 3.0f;   // rad/s about +Y (tracking)
    for (int i = 0; i < 20; ++i) {
        MotionSample s{};
        s.time = 4000000000ll + i * kFrame;
        s.orientation = AxisAngle({0, 1, 0}, rate * static_cast<float>(i * kFrame) * 1e-9f);
        h.Push(s);
    }
    const Motion m = h.Estimate();
    Require(m.valid && NearV(m.angular, {0, rate, 0}, 0.05f), "angular velocity from orientations");
}

void TestGapResets()
{
    MotionHistory h;
    for (int i = 0; i < 10; ++i) {
        MotionSample s{};
        s.time = 5000000000ll + i * kFrame;
        s.position = {static_cast<float>(i), 0, 0};   // 90 m/s: garbage before the gap
        h.Push(s);
    }
    MotionSample s{};
    s.time = 5000000000ll + 10 * kFrame + 200000000ll;   // a 0.2 s stall
    h.Push(s);
    Require(h.Size() == 1, "a gap restarts the history");
    Require(!h.Estimate().valid, "one sample is no motion");
    // Out of order is ignored.
    MotionSample old{};
    old.time = s.time - 1;
    h.Push(old);
    Require(h.Size() == 1, "an older sample is ignored");
}

// --- frames ---------------------------------------------------------------------------

void TestMatrix()
{
    const Quaternion q = Normalize(Quaternion{0.2f, -0.4f, 0.1f, 0.85f});
    const Vec3 x = Rotate(q, {1, 0, 0}), y = Rotate(q, {0, 1, 0}), z = Rotate(q, {0, 0, 1});
    // Row-major 3x4, columns are the axes, scaled by 2 (scale must not matter).
    const float m[12] = {2 * x.x, 2 * y.x, 2 * z.x, 5, 2 * x.y, 2 * y.y, 2 * z.y, 6, 2 * x.z, 2 * y.z, 2 * z.z, 7};
    const Quaternion r = FromMatrix34(m);
    const Vec3 probe{0.3f, -0.7f, 0.2f};
    Require(NearV(Rotate(r, probe), Rotate(q, probe), 1e-3f), "matrix -> quaternion");
}

// --- kinds ---------------------------------------------------------------------------

void TestClassify()
{
    ClassifyInput in{};
    in.mass = 1.0f;
    in.diagonal = 0.3f;
    Require(Classify(in) == Kind::Light, "a shoe is light");
    in.hold = 0.75f;
    Require(Classify(in) == Kind::Heavy, "a leverage hold is heavy");
    in.hold = 0.33f;
    Require(Classify(in) == Kind::Light, "the generic hold mode alone is not");
    in.hold = 0;
    in.mass = 40;
    Require(Classify(in) == Kind::Heavy, "mass");
    in.mass = 2;
    in.diagonal = 1.5f;
    Require(Classify(in) == Kind::Heavy, "size");
    in.articulated = true;
    Require(Classify(in) == Kind::Corpse, "a ragdoll is a body");
    in.articulated = false;
    in.safeCarry = true;
    Require(Classify(in) == Kind::Native, "turrets keep the native placement");
    in.safeCarry = false;
    in.handValid = false;
    Require(Classify(in) == Kind::Native, "no hand, native");
}

// --- placement -------------------------------------------------------------------------

void TestLightHold()
{
    Pose grip{};
    grip.position = {1, 2, 3};
    const Vec3 palm{1, 0, 0};
    // A thin box (2 cm thick along the palm): its centre in the fist.
    Box thin{{-0.01f, -0.1f, -0.05f}, {0.01f, 0.1f, 0.05f}};
    EntityPose p = LightHold(grip, palm, Quaternion{}, thin);
    Require(NearV(p.position, grip.position), "thin: centre on the grip point");
    // A 20 cm cube: pushed out of the palm until its side rests on it.
    Box cube{{-0.1f, -0.1f, -0.1f}, {0.1f, 0.1f, 0.1f}};
    p = LightHold(grip, palm, Quaternion{}, cube);
    Require(NearV(p.position, {1 + 0.1f - 0.035f, 2, 3}), "thick: near side on the palm");
    // The pivot is not the centre: the centre is what goes in the hand.
    Box offset{{0, 0, 0}, {0.02f, 0.02f, 0.30f}};
    p = LightHold(grip, palm, Quaternion{}, offset);
    const Vec3 centre = Add(p.position, Rotate(p.rotation, Centre(offset)));
    Require(Near(centre.y, 2) && Near(centre.z, 3), "the bounds centre sits at the hand");
    // The object turns with the hand, keeping the grab's relative rotation.
    const Quaternion rel = AxisAngle({0, 0, 1}, 0.5f);
    grip.orientation = AxisAngle({1, 0, 0}, 1.2f);
    p = LightHold(grip, palm, rel, thin);
    const Quaternion expected = Multiply(grip.orientation, rel);
    const Vec3 probe{0.1f, 0.2f, 0.3f};
    Require(NearV(Rotate(p.rotation, probe), Rotate(expected, probe)), "rotation = hand * relative");
}

void TestLightHoldAt()
{
    // A 60 cm long box pointed at end-on: the ray meets its near face, and that
    // point -- not the centre -- goes in the fist; the box extends away along
    // the pointing direction.
    const Box box{{-0.1f, -0.3f, -0.1f}, {0.1f, 0.3f, 0.1f}};
    const float tm[12] = {1, 0, 0, 5,  0, 1, 0, 2,  0, 0, 1, 1};   // at (5, 2, 1), unrotated
    Vec3 local{};
    Require(GrabPointLocal(tm, box, {5, 0, 1}, {0, 1, 0}, local, 0.0f), "ray meets the box");
    Require(NearV(local, {0, -0.3f, 0}), "entry point on the near face, local");
    Vec3 inset{};
    Require(GrabPointLocal(tm, box, {5, 0, 1}, {0, 1, 0}, inset) && NearV(inset, {0, -0.27f, 0}),
            "3 cm inside by default: the fingers close round the edge");
    Pose grip{};
    grip.position = {0, 1, 1.4f};
    // The hand pointing along +Y when it grabbed: object rotation = hand rotation.
    EntityPose p = LightHoldAt(grip, Quaternion{}, local);
    const Vec3 centre = Add(p.position, Rotate(p.rotation, Centre(box)));
    Require(NearV(Add(p.position, Rotate(p.rotation, local)), grip.position), "grab point in the fist");
    Require(NearV(centre, {0, 1.3f, 1.4f}), "the bulk beyond the hand, along its pointing direction");
    // The hand turns 90 degrees about Z: the object swings with it about the fist.
    grip.orientation = AxisAngle({0, 0, 1}, 1.5707963f);
    p = LightHoldAt(grip, Quaternion{}, local);
    const Vec3 turned = Add(p.position, Rotate(p.rotation, Centre(box)));
    Require(NearV(turned, {-0.3f, 1, 1.4f}), "turns with the hand about the grab point");
    // A ray passing beside the box: the nearest bounds point.
    Require(GrabPointLocal(tm, box, {5.5f, 0, 1}, {0, 1, 0}, local, 0.0f), "near miss still gives a point");
    Require(Near(local.x, 0.1f) && local.y >= -0.3f - 1e-4f && local.y <= 0.3f + 1e-4f, "nearest point on the box");
    // A rotated, scaled entity: the local point maps back onto the world ray's entry.
    const float c = std::cos(0.7f) * 2.0f, s = std::sin(0.7f) * 2.0f;   // scale 2
    const float rot[12] = {c, -s, 0, 1,  s, c, 0, -1,  0, 0, 2, 0};
    Require(GrabPointLocal(rot, box, {1, -1, -5}, {0, 0, 1}, local, 0.0f), "vertical ray down the pivot");
    Require(NearV(local, {0, 0, -0.1f}), "enters the bottom face");
    const Box tiny{{-0.02f, -0.02f, -0.02f}, {0.02f, 0.02f, 0.02f}};
    Require(GrabPointLocal(tm, tiny, {5, 0, 1}, {0, 1, 0}, inset, 0.03f) && NearV(inset, {0, -0.01f, 0}),
            "a small object: at most half way to its centre");
    const float singular[12] = {0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0};
    Require(!GrabPointLocal(singular, box, {0, 0, 0}, {0, 1, 0}, local), "singular matrix refused");
}

void TestHeavyShift()
{
    const Vec3 head{10, 20, 1.6f};
    HeavyGrab grab{};
    grab.handOffset = {0, 0.4f, -0.4f};
    grab.yaw = 0;
    // The hand where it was at the grab: the game's own place.
    Require(NearV(HeavyShift(Add(head, grab.handOffset), head, 0, grab), {}), "a resting hand leaves it native");
    // Moved 30 cm left and 10 cm up: the object moves the same.
    Require(NearV(HeavyShift(Add(head, {-0.3f, 0.4f, -0.3f}), head, 0, grab), {-0.3f, 0, 0.1f}), "steered");
    // Limits.
    Vec3 v = HeavyShift(Add(head, {2.0f, 0.4f, -0.4f}), head, 0, grab);
    Require(Near(Length(Horizontal(v)), 0.6f), "max shift");
    v = HeavyShift(Add(head, {0, 0.4f, 1.0f}), head, 0, grab);
    Require(Near(v.z, 0.45f), "max rise");
    v = HeavyShift(Add(head, {0, 0.4f, -2.0f}), head, 0, grab);
    Require(Near(v.z, -0.45f), "max drop");
    // A snap turn of 90 degrees left, the hand turned with the player: still native.
    const Vec3 turned = Rotate(YawRotation(1.5707963f), grab.handOffset);
    Require(NearV(HeavyShift(Add(head, turned), head, 1.5707963f, grab), {}, 1e-3f), "turns with the play space");
}

void TestCorpse()
{
    const CorpseSettings cs{};
    const Vec3 feet{0, 0, 0};
    const Vec3 native{0, 0, 1.25f};   // the game drags at the player's chest
    // In front, inside the limits: the hand itself.
    Require(NearV(CorpsePoint({0.2f, 0.9f, 0.8f}, feet, native), {0.2f, 0.9f, 0.8f}), "the hand");
    // Pulled into the player: pushed out to the minimum distance.
    Vec3 p = CorpsePoint({0, 0.2f, 0.8f}, feet, native);
    Require(Near(std::sqrt(p.x * p.x + p.y * p.y), cs.minDistance, 1e-3f), "min distance");
    // Lifted overhead: no higher than the native height + margin.
    p = CorpsePoint({0, 0.9f, 2.5f}, feet, native);
    Require(Near(p.z, 1.25f + cs.maxAboveNative, 1e-3f), "max height");
    // Into the floor (the reach limit widened, so only the floor limit acts).
    CorpseSettings wide = cs;
    wide.maxFromNative = 5.0f;
    p = CorpsePoint({0, 0.9f, -0.5f}, feet, native, wide);
    Require(Near(p.z, cs.minHeight, 1e-3f), "min height");
    // Far to the side: kept inside the drag's reach.
    p = CorpsePoint({2.5f, 0.5f, 1.0f}, feet, native);
    Require(Near(Length(Sub(p, native)), cs.maxFromNative, 1e-3f), "max from native");
}

void TestFollow()
{
    Follow f;
    Require(NearV(f.Update({1, 0, 0}, 0.01f, 0.1f), {1, 0, 0}), "primes on the first value");
    const Vec3 v = f.Update({2, 0, 0}, 0.1f, 0.1f);
    Require(v.x > 1.5f && v.x < 1.7f, "one time constant: 63%");
}

// --- release ---------------------------------------------------------------------------

void TestRelease()
{
    ThrowSettings s{};
    // Set down: not a throw, not scaled, the player's velocity kept.
    Release r = ComputeRelease({0.5f, 0, 0}, {}, {0, 1, 0}, Kind::Light, s);
    Require(!r.thrown && NearV(r.velocity, {0.5f, 1, 0}), "a gentle release");
    // Thrown: scaled by the gain.
    r = ComputeRelease({0, 5, 0}, {0, 0, 10}, {}, Kind::Light, s);
    Require(r.thrown && NearV(r.velocity, {0, 5 * s.gain, 0}, 1e-3f), "a throw is scaled");
    Require(NearV(r.angular, {0, 0, 10}), "spin kept");
    // Capped.
    r = ComputeRelease({0, 30, 0}, {0, 0, 80}, {}, Kind::Light, s);
    Require(r.capped && Near(Length(r.velocity), s.maxSpeed, 1e-2f), "speed cap");
    Require(Near(Length(r.angular), s.maxSpin, 1e-2f), "spin cap");
    // Heavy: slower, less spin.
    r = ComputeRelease({0, 8, 0}, {0, 0, 10}, {}, Kind::Heavy, s);
    Require(Near(Length(r.velocity), s.heavyMaxSpeed, 1e-2f), "heavy cap");
    Require(NearV(r.angular, {0, 0, 10 * s.heavySpinScale}), "heavy spin");
    // Garbage in, the player's velocity out.
    r = ComputeRelease({NAN, 0, 0}, {}, {1, 0, 0}, Kind::Light, s);
    Require(NearV(r.velocity, {1, 0, 0}) && !r.thrown, "non-finite hand");
}

}  // namespace

int main()
{
    TestConstantVelocity();
    TestPeakNotRelease();
    TestRuntimePreferred();
    TestAngular();
    TestGapResets();
    TestMatrix();
    TestClassify();
    TestLightHold();
    TestLightHoldAt();
    TestHeavyShift();
    TestCorpse();
    TestFollow();
    TestRelease();
    std::cout << "hand_carry: all tests passed\n";
    return 0;
}
