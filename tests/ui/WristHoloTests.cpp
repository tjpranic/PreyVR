#include "ui/hud/WristHolo.h"
#include "ui/hud/WristFrame.h"
#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::wrist;

namespace {

using preyvr::test::Require;
bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
bool NearV(Vec3 a, Vec3 b, float e = 1e-3f) { return Near(a.x, b.x, e) && Near(a.y, b.y, e) && Near(a.z, b.z, e); }

// The watch check: left forearm across the chest at 1.25 m, hand to the right,
// back of the forearm up; eye 35 cm above and a little behind.
Arm WatchArm()
{
    Arm arm{};
    arm.elbow = {-0.20f, 1.25f, -0.30f};
    arm.wrist = {0.05f, 1.25f, -0.30f};
    arm.palm = {0.0f, -1.0f, 0.0f};   // palm down = back of the forearm up
    return arm;
}
Pose EyeAbove(Vec3 target, Vec3 eye)
{
    // A head at `eye` looking straight at `target` (no roll).
    const float dx = target.x - eye.x, dy = target.y - eye.y, dz = target.z - eye.z;
    const float yaw = std::atan2(-dx, -dz);
    const float pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
    const Quaternion qy{0.0f, std::sin(yaw / 2), 0.0f, std::cos(yaw / 2)};
    const Quaternion qx{std::sin(pitch / 2), 0.0f, 0.0f, std::cos(pitch / 2)};
    return Pose{Multiply(qy, qx), eye};
}

void TestPlacementOnTheBackOfTheForearm()
{
    Placement p{};
    p.maxTiltDeg = 0.0f;
    const Card c = PlaceCard(WatchArm(), Pose{{}, {0.0f, 1.60f, -0.20f}}, p);
    Require(c.valid, "a forearm gives a card");
    Require(NearV(c.axis, {1, 0, 0}), "the card runs along the forearm towards the hand");
    Require(NearV(c.dorsal, {0, 1, 0}), "the back of the forearm is opposite the palm");
    Require(NearV(c.pose.position, {0.05f - p.along, 1.25f + p.hover, -0.30f}), "the card sits behind the wrist, over the forearm");
    Require(NearV(Rotate(c.pose.orientation, {0, 0, 1}), {0, 1, 0}), "untilted, the face is the back of the forearm");
    Require(NearV(Rotate(c.pose.orientation, {1, 0, 0}), {1, 0, 0}), "the text runs elbow -> hand");
    Require(NearV(Rotate(c.pose.orientation, {0, 1, 0}), {0, 0, -1}), "the text's up points away from the body");
}

void TestTiltTowardsTheEye()
{
    Placement p{};
    p.maxTiltDeg = 40.0f;
    // Eye 30 cm towards the body and 20 cm above: 56 deg off the back -> clamps at 40.
    const Arm arm = WatchArm();
    const Card c = PlaceCard(arm, Pose{{}, {-0.015f, 1.49f, 0.0f}}, p);
    Require(c.valid, "valid");
    Require(Near(std::fabs(c.tiltDeg), 40.0f, 0.05f), "tilt clamps at the limit");
    const Vec3 face = Rotate(c.pose.orientation, {0, 0, 1});
    Require(face.z > 0.5f && face.y > 0.5f, "the face turns towards the body, still up");
    Require(Near(Dot(face, c.axis), 0.0f), "the tilt is about the forearm only");
    // Eye almost straight above: the tilt is small and exact.
    const Card d = PlaceCard(arm, Pose{{}, {-0.015f, 1.70f, -0.28f}}, p);
    Require(std::fabs(d.tiltDeg) < 5.0f, "straight above, almost no tilt");
}

void TestTwistFollowsTheHand()
{
    Arm arm = WatchArm();
    arm.palm = {0.0f, 0.0f, 1.0f};   // palm towards the body: back faces away
    Placement p{};
    p.maxTiltDeg = 0.0f;
    const Card c = PlaceCard(arm, Pose{{}, {0.0f, 1.6f, 0.0f}}, p);
    Require(NearV(c.dorsal, {0, 0, -1}), "the back turns with the hand");
    // A palm with a component along the forearm is made perpendicular.
    arm.palm = {0.5f, -1.0f, 0.0f};
    const Card d = PlaceCard(arm, Pose{{}, {0.0f, 1.6f, 0.0f}}, p);
    Require(NearV(d.dorsal, {0, 1, 0}), "the forearm's own back, without its length");
}

// An arm held out in front, back of the wrist up, looked at from above and
// behind: along the forearm the text would run away from the viewer.
void TestTextLevelsWithTheView()
{
    Arm arm{};
    arm.elbow = {0.0f, 1.25f, -0.10f};
    arm.wrist = {0.0f, 1.25f, -0.35f};   // forearm straight ahead
    arm.palm = {0.0f, -1.0f, 0.0f};
    Placement p{};
    p.maxTiltDeg = 0.0f;
    const Pose head{{}, {0.0f, 1.60f, 0.0f}};
    const Card level = PlaceCard(arm, head, p);
    Require(NearV(Rotate(level.pose.orientation, {1, 0, 0}), {1, 0, 0}), "90 deg off: the text reads level");
    Require(NearV(Rotate(level.pose.orientation, {0, 0, 1}), {0, 1, 0}), "the face does not change");
    Require(Near(std::fabs(level.rollDeg), 90.0f, 0.1f), "rolled 90 in the card's plane");
    p.upright = 0;
    const Card along = PlaceCard(arm, head, p);
    Require(NearV(Rotate(along.pose.orientation, {1, 0, 0}), {0, 0, -1}), "upright 0: along the forearm");
    // Within uprightFrom of the horizontal it stays exactly on the forearm.
    p.upright = 1;
    arm.wrist = {0.25f * std::cos(0.35f), 1.25f, -0.10f - 0.25f * std::sin(0.35f)};   // 20 deg off level
    const Card near = PlaceCard(arm, head, p);
    Require(Near(near.rollDeg, 0.0f, 0.01f), "near the horizontal: on the forearm");
    // A rolled head levels with the head.
    arm = WatchArm();
    const float r = 0.4f;
    const Pose rolled{{0.0f, 0.0f, std::sin(r / 2), std::cos(r / 2)}, {0.0f, 1.6f, -0.2f}};
    p.upright = 2;
    const Card tilted = PlaceCard(arm, rolled, p);
    const Vec3 x = Rotate(tilted.pose.orientation, {1, 0, 0});
    Require(Near(x.y, std::sin(r), 0.02f) || Near(x.z, 0.0f, 0.02f), "upright 2 follows the head's horizontal");
}

void TestDegenerateArms()
{
    Arm arm = WatchArm();
    arm.elbow = arm.wrist;
    Require(!PlaceCard(arm, Pose{{}, {0, 1.6f, 0}}, {}).valid, "no forearm, no card");
    arm = WatchArm();
    arm.palm = {1, 0, 0};   // the palm along the forearm says nothing
    Require(!PlaceCard(arm, Pose{{}, {0, 1.6f, 0}}, {}).valid, "a palm along the forearm is rejected");
    arm = WatchArm();
    arm.wrist.x = NAN;
    Require(!PlaceCard(arm, Pose{{}, {0, 1.6f, 0}}, {}).valid, "NaN is rejected");
}

void TestMeasure()
{
    Placement p{};
    p.maxTiltDeg = 0.0f;
    const Card c = PlaceCard(WatchArm(), Pose{{}, {0.0f, 1.6f, -0.2f}}, p);
    const Vec3 eye{c.pose.position.x, c.pose.position.y + 0.35f, c.pose.position.z};
    const View v = Measure(c, EyeAbove(c.pose.position, eye));
    Require(Near(v.distance, 0.35f), "distance");
    Require(Near(v.facing, 1.0f), "straight above: facing 1");
    Require(Near(v.viewing, 1.0f, 2e-3f), "looked at: viewing 1");
    // Looking 60 deg away.
    Pose away = EyeAbove(c.pose.position, eye);
    away.orientation = Multiply(away.orientation, Quaternion{std::sin(0.5236f), 0, 0, std::cos(0.5236f)});
    Require(Near(Measure(c, away).viewing, 0.5f, 0.01f), "60 deg off the gaze: 0.5");
}

View Looked() { return {0.35f, 0.9f, 0.95f}; }

void TestDwellFadeAndHysteresis()
{
    Presenter p;
    Conditions c{};
    c.eligible = true;
    const float dt = 1.0f / 90.0f;
    p.Update(Looked(), c, dt);
    Require(!p.Showing() && p.Alpha() == 0.0f, "a glance shorter than the dwell shows nothing");
    for (int i = 0; i < 7; ++i) { p.Update(Looked(), c, dt); }
    Require(p.Showing(), "held past the dwell, it shows");
    for (int i = 0; i < 20; ++i) { p.Update(Looked(), c, dt); }
    Require(Near(p.Alpha(), 1.0f), "fully faded in after fadeIn");
    // Between the exit and entry thresholds it stays.
    p.Update({0.35f, 0.35f, 0.7f}, c, dt);
    Require(p.Showing(), "hysteresis keeps it");
    // Past the exit threshold it fades out over fadeOut.
    p.Update({0.35f, 0.1f, 0.7f}, c, dt);
    Require(!p.Showing() && p.Alpha() < 1.0f && p.Alpha() > 0.8f, "fades, does not snap");
    for (int i = 0; i < 30; ++i) { p.Update({0.35f, 0.1f, 0.7f}, c, dt); }
    Require(p.Alpha() == 0.0f, "gone after fadeOut");
}

// Where a quadrant_SW point lands in the capture, through what the replay writes,
// with the HUD canvas as Prey renders it (R-118, R-122): scaled uniformly to
// cover the capture by the larger axis ratio, and centred.
void Pixel(const Layout& l, const Stage& s, double u, double v, double& x, double& y)
{
    const double cover = std::max(s.width / (2.0 * s.halfWidth), s.height / (2.0 * s.halfHeight));
    const double stageX = l.safeX + (l.quadrantX + u * l.quadrantScaleX * 0.01) * s.safeScaleX * 0.01;
    const double stageY = l.safeY + (l.quadrantY + v * l.quadrantScaleY * 0.01) * s.safeScaleY * 0.01;
    x = s.width * 0.5 + (stageX - s.halfWidth) * cover;
    y = s.height * 0.5 + (stageY - s.halfHeight) * cover;
}
// A square in quadrant units stays a square in the capture: nothing stretched.
bool Square(const Layout& l, const Stage& s)
{
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    Pixel(l, s, 100.0, -100.0, x0, y0);
    Pixel(l, s, 200.0, 0.0, x1, y1);
    return std::fabs((x1 - x0) - (y1 - y0)) < 1e-6 && std::fabs((x1 - x0) - 100.0 * l.pixelsPerUnit) < 1e-6;
}
bool LandsOnItsRect(const Layout& l, const Stage& s)
{
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    Pixel(l, s, kFrame.left, kFrame.top, x0, y0);
    Pixel(l, s, kFrame.right, kFrame.bottom, x1, y1);
    return std::fabs(x0 - l.rect[0]) < 0.01 && std::fabs(y0 - l.rect[1]) < 0.01 &&
           std::fabs(x1 - l.rect[2]) < 0.01 && std::fabs(y1 - l.rect[3]) < 0.01;
}

void TestFrameLayout()
{
    // The headset: Prey's 1920 x 1080 canvas covering a square 2644 eye target,
    // 2644 / 1080 pixels per stage unit in both axes. PreyVR-150 and 151 took
    // 2644 / 1920 across and drew the status 1.78x too wide.
    const Stage canvas{960.0, 540.0, 100.0, 100.0, 2644, 2644};
    const Layout c = Place(canvas);
    Require(c.valid && LandsOnItsRect(c, canvas) && Square(c, canvas), "the canvas as Prey covers the eye: nothing stretched");
    Require(Near(static_cast<float>(c.quadrantScaleX), static_cast<float>(c.quadrantScaleY)) &&
            Near(static_cast<float>(c.quadrantScaleX), static_cast<float>(200.0 * 1080.0 / 2644.0)),
            "one scale for both axes, from the axis that covers");
    Require(Near(c.rect[0], 222.0f) && Near(c.rect[1], 842.0f) && Near(c.rect[2], 2422.0f) && Near(c.rect[3], 1802.0f),
            "and the same centred rectangle");
    // A stage in pixels (no scaling): the same rectangle, scale 200.
    const Stage headset{1322.0, 1322.0, 100.0, 100.0, 2644, 2644};
    const Layout l = Place(headset);
    Require(l.valid && LandsOnItsRect(l, headset) && Square(l, headset), "the frame lands where its rectangle says");
    Require(Near(l.rect[0], 222.0f) && Near(l.rect[1], 842.0f) && Near(l.rect[2], 2422.0f) && Near(l.rect[3], 1802.0f),
            "centred in the capture, one source pixel per texture pixel");
    Require(Near(static_cast<float>(l.quadrantScaleX), 200.0f) && Near(static_cast<float>(l.quadrantScaleY), 200.0f) &&
            Near(static_cast<float>(l.pixelsPerUnit), 2.0f) && Near(l.rect[2] - l.rect[0], float(kTextureWidth)) &&
            Near(l.rect[3] - l.rect[1], float(kTextureHeight)), "2 pixels per unit, the texture's own");
    Require(Near(static_cast<float>(l.safeX), 1322.0f) && Near(static_cast<float>(l.safeY), 1322.0f),
            "safe at the stage centre: no HUD sway");
    // A flat 16:9 screen (the canvas fits exactly) and a wide one (width covers).
    const Stage flat{960.0, 540.0, 100.0, 100.0, 1920, 1080};
    const Layout f = Place(flat);
    Require(f.valid && LandsOnItsRect(f, flat) && Square(f, flat), "16:9: the canvas is the screen");
    const Stage wide{960.0, 540.0, 100.0, 100.0, 3440, 1440};
    const Layout k = Place(wide);
    Require(k.valid && LandsOnItsRect(k, wide) && Square(k, wide), "a wide screen covers by its width");
    // safe scaled by Prey: the quadrant makes up for it.
    const Stage half{1322.0, 1322.0, 50.0, 50.0, 2644, 2644};
    const Layout h = Place(half);
    Require(h.valid && LandsOnItsRect(h, half) && Near(static_cast<float>(h.quadrantScaleX), 400.0f),
            "a half-scale safe doubles the quadrant's scale");
    // A capture too small for the density: the frame shrinks into it, never past its edge.
    const Stage small{400.0, 300.0, 100.0, 100.0, 800, 600};
    const Layout m = Place(small);
    Require(m.valid && LandsOnItsRect(m, small) && m.rect[0] >= 0.0f && m.rect[2] <= 800.0f &&
            m.rect[1] >= 0.0f && m.rect[3] <= 600.0f, "a small capture holds the whole frame");
    Require(Near(m.rect[2] - m.rect[0], 760.0f, 0.01f), "at 95% of its width");
    // Nothing usable, nothing written.
    Require(!Place({0.0, 1322.0, 100.0, 100.0, 2644, 2644}).valid, "no stage width");
    Require(!Place({NAN, 1322.0, 100.0, 100.0, 2644, 2644}).valid, "a stage that is not a number");
    Require(!Place({1322.0, 1322.0, 0.0, 100.0, 2644, 2644}).valid, "a collapsed safe");
    Require(!Place({1322.0, 1322.0, 100.0, 100.0, 8, 8}).valid, "no capture");
}

void TestFrameUnitsAndSize()
{
    const Layout l = Place({1322.0, 1322.0, 100.0, 100.0, 2644, 2644});
    const Box all = ToFrameUnits(l.rect, l.rect);
    Require(Near(static_cast<float>(all.left), -200.0f) && Near(static_cast<float>(all.bottom), 100.0f),
            "the rectangle is the frame");
    Require(!Outside(all) && !Outside({0.0, -200.0, 600.0, 20.0}), "the frame holds what is inside it");
    Require(Outside({-250.0, -100.0, 300.0, 0.0}) && Outside({0.0, -400.0, 300.0, 0.0}) &&
            Outside({0.0, -100.0, 950.0, 0.0}) && Outside({0.0, -100.0, 300.0, 120.0}), "and not what reaches past it");
    // What overran the first frame (-40,-220 to 640,30, PreyVR-150) by a margin on every side fits now.
    Require(!Outside({-120.0, -300.0, 760.0, 60.0}), "the wide margins hold what the first frame cut off");
    // The meters, read back from pixels: units in, units out.
    const double pp = l.pixelsPerUnit;
    const float meters[4]{static_cast<float>(l.rect[0] + (89.0 - kFrame.left) * pp), static_cast<float>(l.rect[1] + (-112.0 - kFrame.top) * pp),
                          static_cast<float>(l.rect[0] + (338.0 - kFrame.left) * pp), static_cast<float>(l.rect[1] + (0.0 - kFrame.top) * pp)};
    const Box b = ToFrameUnits(l.rect, meters);
    Require(Near(static_cast<float>(b.left), 89.0f, 0.01f) && Near(static_cast<float>(b.top), -112.0f, 0.01f) &&
            Near(static_cast<float>(b.right), 338.0f, 0.01f) && Near(static_cast<float>(b.bottom), 0.0f, 0.01f),
            "a content box comes back in quadrant units");
    // The card: the frame at Wrist Display Size, and only that.
    const CardSize one = Size(100.0), two = Size(200.0);
    Require(Near(one.width, 0.352f) && Near(one.height, 0.1536f), "100%: the frame at 0.32 mm a unit");
    Require(Near(two.width, 2.0f * one.width) && Near(two.height, 2.0f * one.height), "200%: twice as large");
    Require(Near(one.offsetX, 0.048f) && Near(one.offsetY, 0.02688f),
            "the frame's centre sits ahead of and above the meters' middle");
    Require(Near(Size(NAN).width, one.width), "a size that is not a number is 100%");
    Require(Near(MetersHalfSpan(100.0), 0.0416f) && Near(MetersHalfSpan(50.0), 0.0208f), "the meters' half length");
    Require(kTextureWidth >= 640 && kTextureHeight >= 480, "a layer swapchain's floor");
}

void TestFastSweepDoesNotOpen()
{
    Presenter p;
    Conditions c{};
    c.eligible = true;
    c.handSpeed = 2.5f;
    for (int i = 0; i < 30; ++i) { p.Update(Looked(), c, 1.0f / 90.0f); }
    Require(!p.Showing(), "a fast hand passing the gaze does not open it");
}

void TestOcclusionAndGateHideFast()
{
    Presenter p;
    Conditions c{};
    c.eligible = true;
    const float dt = 1.0f / 90.0f;
    for (int i = 0; i < 40; ++i) { p.Update(Looked(), c, dt); }
    Require(Near(p.Alpha(), 1.0f), "shown");
    c.occluded = true;
    p.Update(Looked(), c, dt);
    Require(!p.Showing(), "occluded: hides");
    for (int i = 0; i < 6; ++i) { p.Update(Looked(), c, dt); }
    Require(p.Alpha() == 0.0f, "occlusion fades out within fastOut");
    for (int i = 0; i < 30; ++i) { p.Update(Looked(), c, dt); }
    Require(!p.Showing(), "does not come back while occluded");
    c.occluded = false;
    for (int i = 0; i < 40; ++i) { p.Update(Looked(), c, dt); }
    Require(p.Showing(), "back once clear");
    c.eligible = false;   // two-hand aim, a menu
    p.Update(Looked(), c, dt);
    Require(p.Alpha() == 0.0f, "the gate closing removes it at once");
    Require(!p.Candidate(), "and it is no longer a candidate");
}

void TestOcclusionShapes()
{
    const Vec3 eye{0, 1.6f, 0}, card{0, 1.25f, -0.3f};
    Require(SegmentNearPoint(eye, card, {0.0f, 1.42f, -0.15f}, 0.05f), "a hand on the line of sight");
    Require(!SegmentNearPoint(eye, card, {0.2f, 1.42f, -0.15f}, 0.05f), "a hand beside it");
    Require(!SegmentNearPoint(eye, card, {0.0f, 1.10f, -0.43f}, 0.05f), "a hand beyond the card");
    // A gun barrel crossing the line of sight.
    Require(SegmentNearSegment(eye, card, {-0.2f, 1.45f, -0.13f}, {0.2f, 1.45f, -0.13f}, 0.04f), "barrel across");
    Require(!SegmentNearSegment(eye, card, {-0.2f, 1.45f, 0.2f}, {0.2f, 1.45f, 0.2f}, 0.04f), "barrel behind the eye");
    Require(SegmentNearSegment(eye, card, eye, eye, 0.01f), "degenerate segment at the eye");
}

void TestGateBits()
{
    // The bits wrist.report gate= prints, one per condition, in XrSessionHost's order.
    Require(Gate{}.Bits() == 0, "nothing open, no bits");
    bool Gate::* const fields[] = {&Gate::display, &Gate::views, &Gate::rendered, &Gate::noMenu, &Gate::noOptions,
                                  &Gate::gameplay, &Gate::oneHanded, &Gate::settled, &Gate::tracking};
    Gate all{};
    for (unsigned i = 0; i < 9; ++i) {
        Gate one{};
        one.*fields[i] = true;
        Require(one.Bits() == 1u << i, "each condition is its own bit");
        all.*fields[i] = true;
    }
    Require(all.Bits() == Gate::kOpen && Gate::kOpen == 511u, "all nine are open, 511");
    all.tracking = false;
    Require(all.Bits() == 255u, "a stale frame closes it on the top bit");
}

}  // namespace

int main()
{
    TestPlacementOnTheBackOfTheForearm();
    TestTiltTowardsTheEye();
    TestTwistFollowsTheHand();
    TestTextLevelsWithTheView();
    TestDegenerateArms();
    TestMeasure();
    TestDwellFadeAndHysteresis();
    TestFrameLayout();
    TestFrameUnitsAndSize();
    TestFastSweepDoesNotOpen();
    TestOcclusionAndGateHideFast();
    TestOcclusionShapes();
    TestGateBits();
    std::cout << "wrist_holo tests passed\n";
    return 0;
}
