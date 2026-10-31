#include "preyvr/BodyEquipment.h"
#include "preyvr/DebugDraw.h"
#include "preyvr/DebugOverlayScene.h"
#include "preyvr/MedkitSlot.h"
#include "TestGeometry.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>

using namespace preyvr;
using namespace preyvr::debugdraw;

namespace {

using preyvr::test::Require;
bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
using preyvr::test::AboutY;
using preyvr::test::AboutX;
using preyvr::test::kDeg;

EyeView Square()
{
    EyeView eye{};
    eye.left = -1; eye.right = 1; eye.up = 1; eye.down = -1;
    eye.width = 1000; eye.height = 1000;
    return eye;
}

void TestProjection()
{
    const EyeView eye = Square();
    auto p = Project(eye, {0, 0, -1});
    Require(p && Near(p->x, 500) && Near(p->y, 500) && Near(p->depth, 1), "straight ahead lands on the centre");
    p = Project(eye, {.5f, 0, -1});
    Require(p && Near(p->x, 750) && Near(p->y, 500), "+X is right");
    p = Project(eye, {0, .5f, -2});
    Require(p && Near(p->x, 500) && Near(p->y, 375) && Near(p->depth, 2), "+Y is up (smaller pixel y), divided by depth");
    Require(!Project(eye, {0, 0, 1}), "behind the eye is refused");
    Require(!Project(eye, {0, 0, -.01f}), "inside the near plane is refused");
    Require(Near(FocalPixels(eye), 500), "focal length in pixels");

    // The frustum as submitted can be asymmetric: the projection must use it as is.
    EyeView skew = Square();
    skew.left = -1.2f; skew.right = .8f; skew.up = .9f; skew.down = -1.1f;
    p = Project(skew, {0, 0, -1});
    Require(p && Near(p->x, 600) && Near(p->y, 450), "an asymmetric frustum moves the centre");

    // A turned and displaced eye: what it looks at is still the image centre.
    EyeView turned = Square();
    turned.pose = {AboutY(90 * kDeg), {1, 1.6f, 0}};
    p = Project(turned, {0, 1.6f, 0});
    Require(p && Near(p->x, 500) && Near(p->y, 500) && Near(p->depth, 1), "yaw 90 degrees looks along -X");
    turned.pose = {AboutX(-30 * kDeg), {0, 1.6f, 0}};
    p = Project(turned, {0, 1.6f - std::sin(30 * kDeg), -std::cos(30 * kDeg)});
    Require(p && Near(p->x, 500) && Near(p->y, 500), "pitching down looks down");
}

void TestNearClip()
{
    const EyeView eye = Square();
    Vec3 a{0, 0, 1}, b{0, 0, -3};
    Require(ClipToNear(eye, a, b) && Near(a.z, -kNearMetres) && Near(b.z, -3), "a segment through the eye is cut at the near plane");
    a = {0, 0, 1}; b = {0, 0, 2};
    Require(!ClipToNear(eye, a, b), "a segment behind the eye vanishes");
}

void TestLines()
{
    const EyeView eye = Square();
    Scene scene;
    scene.lines.push_back({{-.5f, 0, -1}, {.5f, 0, -1}, {255, 0, 0, 255}, .01f, 1.0f});
    std::vector<Vertex> v;
    auto stats = Tessellate(scene, eye, v, 10000);
    Require(v.size() == 6 && stats.lines == 1 && !stats.truncated, "one segment is one quad");
    // 1 cm at 1 m with a 500 px focal length is 5 px: 2.5 px half width + 1 px feather.
    Require(Near(v[0].extent, 3.5f) && Near(std::fabs(v[0].y - 500), 3.5f), "physical width becomes pixels at depth");
    Require(Near(v[0].x, 250) && Near(v[2].x, 750), "the quad spans the projected ends");
    Require(v[0].kind == 0.0f && Near(std::fabs(v[0].u), 1), "edge vertices carry the across coordinate");

    // A far line never thins below its pixel floor.
    scene.lines[0] = {{-50, 0, -100}, {50, 0, -100}, {255, 0, 0, 255}, .01f, 3.0f};
    v.clear();
    Tessellate(scene, eye, v, 10000);
    Require(v.size() == 6 && Near(v[0].extent, 2.5f), "the pixel floor holds at distance");

    // Dashes are bounded however long the ray.
    // Off the view axis, so every dash projects to a visible segment.
    scene.lines[0] = {{.2f, -.1f, -1}, {.2f, -.1f, -201}, {255, 0, 0, 255}, .005f, 1.0f, .001f};
    v.clear();
    Tessellate(scene, eye, v, 1000000);
    Require(v.size() <= 401 * 6 && v.size() >= 50 * 6, "a dashed 200 m ray stays within 400 dashes");

    // The budget is a hard limit.
    v.clear();
    stats = Tessellate(scene, eye, v, 60);
    Require(v.size() <= 60 && stats.truncated, "the vertex budget truncates instead of overflowing");

    // Lines wholly outside the image cost nothing.
    scene.lines[0] = {{5, 0, -1}, {6, 0, -1}, {255, 0, 0, 255}, .005f, 1.0f};
    v.clear();
    Tessellate(scene, eye, v, 10000);
    Require(v.empty(), "an off-image line is culled");
}

unsigned Alpha(const Vertex& v) { return v.color >> 24; }

void TestFadeIn()
{
    const EyeView eye = Square();
    Scene scene;
    // 1 m across the view at 1 m, the first quarter fading in.
    Line line{{-.5f, 0, -1}, {.5f, 0, -1}, {255, 0, 0, 200}, .01f, 1.0f};
    line.fadeIn = .25f;
    scene.lines.push_back(line);
    std::vector<Vertex> v;
    Tessellate(scene, eye, v, 10000);
    // Quad order per segment: start, start, end, end, start, end.
    Require(v.size() == 6 * 9, "eight fade pieces and the solid rest");
    Require(Alpha(v[0]) == 0 && Alpha(v[1]) == 0 && Near(v[0].x, 250), "the line starts transparent at its start");
    bool rising = true;
    for (int i = 0; i < 8; ++i) {
        const unsigned start = Alpha(v[6 * i]), end = Alpha(v[6 * i + 2]);
        rising = rising && end > start && (i == 0 || start == Alpha(v[6 * (i - 1) + 2]));
        rising = rising && Near(v[6 * i + 2].x, 250 + 125.0f * static_cast<float>(i + 1) / 8);
        rising = rising && Near(static_cast<float>(end), std::round(200 * SmoothStep(static_cast<float>(i + 1) / 8)), 0);
    }
    Require(rising, "each piece rises along the eased curve and meets the next");
    Require(Alpha(v[48]) == 200 && Alpha(v[50]) == 200 && Near(v[48].x, 375) && Near(v[50].x, 750),
            "past the fade the line is solid to its end");

    // Shorter than its fade: it fades over its whole length.
    scene.lines[0].fadeIn = 5;
    v.clear();
    Tessellate(scene, eye, v, 10000);
    Require(v.size() == 6 * 8 && Alpha(v[0]) == 0 && Alpha(v[47]) == 200 && Near(v[47].x, 750),
            "a line shorter than its fade reaches full alpha at its end");

    // No fade: one quad, solid at both ends, as before.
    scene.lines[0].fadeIn = 0;
    v.clear();
    Tessellate(scene, eye, v, 10000);
    Require(v.size() == 6 && Alpha(v[0]) == 200 && Alpha(v[2]) == 200, "without a fade the line is one solid quad");

    // A dashed line ignores the fade.
    scene.lines[0].fadeIn = .25f;
    scene.lines[0].dash = .1f;
    v.clear();
    Tessellate(scene, eye, v, 10000);
    Require(!v.empty() && Alpha(v[0]) == 200, "dashes are not faded");

    // Through the eye: the pieces behind it are dropped, and the one cut at the
    // near plane starts at the fade where it was cut. 2 m long, 0.4 m fade, so
    // 5 cm pieces; the third runs z 0 to -0.05 and is cut 60% along, at -0.03.
    // Just off the axis, so even the cut piece projects inside the image.
    scene.lines[0] = {{.01f, 0, .1f}, {.01f, 0, -1.9f}, {255, 0, 0, 255}, .01f, 1.0f};
    scene.lines[0].fadeIn = .4f;
    v.clear();
    Tessellate(scene, eye, v, 10000);
    const float cut = SmoothStep(.25f) + (SmoothStep(.375f) - SmoothStep(.25f)) * .6f;
    Require(v.size() == 6 * 7, "the two pieces behind the eye are gone");
    Require(Near(static_cast<float>(Alpha(v[0])), 255 * cut, 1) &&
                static_cast<long>(Alpha(v[2])) == std::lround(255 * SmoothStep(.375f)),
            "a piece cut at the near plane keeps the curve");
}

void TestSpheresMarkersLabels()
{
    const EyeView eye = Square();
    Scene scene;
    scene.spheres.push_back({{0, 0, -2}, .14f, {255, 255, 255, 255}, {255, 255, 255, 40}});
    std::vector<Vertex> v;
    Tessellate(scene, eye, v, 100000);
    // The fill fan comes first: hub, then two rim points per triangle.
    Require(v.size() > 3 * 48 && v[0].kind == 0.0f, "a sphere fills its silhouette");
    const float expected = std::tan(std::asin(.14f / 2.0f)) * 500;
    for (int i = 0; i < 48; ++i) {
        const auto& rim = v[3 * i + 1];
        Require(Near(std::hypot(rim.x - 500, rim.y - 500), expected, .5f), "on axis the silhouette is the angular radius");
    }
    // Off axis and close, every rim point is where a line of sight grazes the sphere.
    for (Vec3 c : {Vec3{1.2f, -.4f, -1}, Vec3{.3f, -.6f, -.4f}, Vec3{0, 0, -.2f}}) {
        Vec3 rimCentre{}, normal{};
        float rimRadius = 0;
        Require(SphereSilhouette({0, 0, 0}, c, .14f, rimCentre, rimRadius, normal), "outside the sphere: a silhouette");
        const Vec3 u = Perpendicular(normal), w = Cross(normal, u);
        for (int i = 0; i < 12; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / 12;
            const Vec3 t = Add(rimCentre, Add(Scale(u, rimRadius * std::cos(a)), Scale(w, rimRadius * std::sin(a))));
            Require(Near(Length(Sub(t, c)), .14f, 1e-4f), "rim points lie on the sphere");
            Require(Near(Dot(Sub(t, c), t), 0, 1e-4f), "rim points are tangent to the line of sight");
        }
    }
    Vec3 unusedCentre{}, unusedNormal{};
    float unusedRadius = 0;
    Require(!SphereSilhouette({0, 0, 0}, {0, 0, -.1f}, .14f, unusedCentre, unusedRadius, unusedNormal),
            "inside the sphere there is no silhouette");

    scene = {};
    Marker marker{};
    marker.centre = {0, 0, -100};
    marker.radius = .01f;
    marker.minPixels = 6;
    scene.markers.push_back(marker);
    v.clear();
    Tessellate(scene, eye, v, 1000);
    Require(v.size() == 6 && Near(v[0].extent, 6), "markers keep a pixel floor");

    scene = {};
    Label label{};
    label.anchor = {0, 0, -1};
    label.text = "AB C\nD";
    scene.labels.push_back(label);
    v.clear();
    Tessellate(scene, eye, v, 10000);
    // Plate + shadow and glyph for each of the four visible characters.
    Require(v.size() == 6 * (1 + 2 * 4), "a label is a plate plus two quads per visible glyph");
    Require(v[6].kind == 1.0f && v[6].u >= 0 && v[6].u <= 1, "glyph quads sample the atlas");
    const float h = std::fabs(v[8].y - v[6].y);
    Require(h >= label.minPixels - .01f && h <= label.maxPixels + .01f, "glyph height stays within its clamp");

    // Anchored near the right edge: the block slides in instead of being cut.
    scene.labels[0].anchor = {.98f, 0, -1};
    scene.labels[0].text = "HIP R  SQUEEZE: STORE";
    v.clear();
    Tessellate(scene, eye, v, 10000);
    float right = 0;
    for (const auto& vertex : v) { right = std::max(right, vertex.x); }
    Require(!v.empty() && right <= eye.width, "a label near the edge stays inside the image");
    scene.labels[0].keepInside = false;
    v.clear();
    Tessellate(scene, eye, v, 10000);
    right = 0;
    for (const auto& vertex : v) { right = std::max(right, vertex.x); }
    Require(right > eye.width, "without keepInside the label is where it was anchored");

    // The headset shows less than the image: labels stay inside what it shows.
    EyeView narrow = Square();
    SetShownFrustum(narrow, -.8f, .5f, .9f, -.9f);
    Require(Near(narrow.shownX1, 750) && Near(narrow.shownX0, 100) && Near(narrow.shownY0, 50), "shown frustum in pixels");
    scene.labels[0].keepInside = true;
    scene.labels[0].anchor = {.45f, 0, -1};
    v.clear();
    Tessellate(scene, narrow, v, 10000);
    right = 0;
    for (const auto& vertex : v) { right = std::max(right, vertex.x); }
    Require(right <= 750, "a label stays inside the displayed frustum");

    // An anchor outside what is shown: no fragment at the edge.
    scene.labels[0].anchor = {.7f, 0, -1};
    v.clear();
    const auto hidden = Tessellate(scene, narrow, v, 10000);
    Require(v.empty() && hidden.culled == 1, "a label about something out of view is not drawn");
    scene.labels[0].anchor = {.45f, 0, -1};

    // Two labels at one anchor do not overlap.
    scene.labels.push_back(scene.labels[0]);
    v.clear();
    Tessellate(scene, eye, v, 10000);
    const std::size_t perLabel = v.size() / 2;
    float topA = 1e9f, bottomA = -1e9f, topB = 1e9f, bottomB = -1e9f;
    for (std::size_t i = 0; i < 6; ++i) {
        topA = std::min(topA, v[i].y); bottomA = std::max(bottomA, v[i].y);
        topB = std::min(topB, v[perLabel + i].y); bottomB = std::max(bottomB, v[perLabel + i].y);
    }
    Require(bottomA <= topB || bottomB <= topA, "colliding labels are stacked, not overlapped");
}

void TestGestureTruth()
{
    // The drawn slot centre must be exactly where the gesture detects a hand.
    std::mt19937 random(7);
    std::uniform_real_distribution<float> angle(-3.1f, 3.1f), offset(-.5f, .5f);
    for (int trial = 0; trial < 200; ++trial) {
        equipment::HolsterGesture gesture;
        const float yaw = angle(random);
        const Pose head{AboutY(yaw), {offset(random), 1.6f + offset(random) * .2f, offset(random)}};
        // Establish the torso with the hand away and the grip released.
        gesture.Update(head, {head.position.x + 1, head.position.y, head.position.z}, false, true, .011f);
        Require(gesture.BodyValid() && gesture.Armed(), "a released grip arms the gesture");
        const int zone = trial % 2;
        const Vec3 centre = gesture.ZoneCentre(head.position, zone);
        // Inside by a small margin, then squeeze.
        const Vec3 hand{centre.x + .05f, centre.y, centre.z};
        Require(gesture.Update(head, hand, false, true, .011f) < 0, "no squeeze, no action");
        Require(gesture.Update(head, hand, true, true, .011f) == zone, "squeezing at the drawn centre selects that slot");
        Require(gesture.OwnsGrip(), "the slot owns the grip after selection");
    }
    // Just outside the drawn radius: nothing.
    equipment::HolsterGesture gesture;
    const Pose head{{}, {0, 1.6f, 0}};
    gesture.Update(head, {1, 1.6f, 0}, false, true, .011f);
    const Vec3 c = gesture.ZoneCentre(head.position, 0);
    Require(gesture.Update(head, {c.x + equipment::HolsterGesture::Radius + .005f, c.y, c.z}, true, true, .011f) < 0,
            "a hand outside the drawn sphere does not select");

    equipment::MedkitSlot medkit;
    medkit.Update(head, {1, 1.6f, 0}, false, false, true, .011f);
    const Vec3 m = medkit.Zone().ZoneCentre(head.position, 0);
    Require(Near(m.x, -.25f) && Near(m.y, 1.6f - .65f), "the medkit slot is the left hip");
    medkit.Update(head, m, true, false, true, .011f);
    Require(medkit.OwnsGrip() && medkit.TriggerArmed(), "selected with the trigger released: armed");
}

void TestWristMeasureMatchesVisibility()
{
    // MeasureWrist must reproduce the visibility rule exactly.
    std::mt19937 random(11);
    std::uniform_real_distribution<float> a(-3.1f, 3.1f), o(-.6f, .6f);
    for (int trial = 0; trial < 2000; ++trial) {
        const Pose head{Normalize(Multiply(AboutY(a(random)), AboutX(a(random) * .4f))), {0, 1.6f, 0}};
        const Pose grip{Normalize(Multiply(AboutY(a(random)), AboutX(a(random)))), {o(random), 1.2f + o(random), o(random)}};
        const Pose wrist = equipment::WristPose(grip);
        for (bool was : {false, true}) {
            const auto m = equipment::MeasureWrist(head, wrist);
            const bool expected = m.distance >= .23f && m.distance <= 1.f && m.facing > equipment::WristFacingNeeded(was) &&
                                  m.viewing > equipment::WristViewingNeeded(was);
            Require(expected == equipment::WristVisible(head, wrist, was), "the drawn wrist metrics are the visibility rule");
        }
    }
}

void TestSceneLayers()
{
    OverlayFrame f{};
    f.headValid = true;
    f.head = {{}, {0, 1.6f, 0}};
    f.rays[1] = {true, RayRole::Weapon, {.2f, 1.4f, -.3f}, {0, 0, -1}, {.2f, 1.4f, -3}, true, 2.7f};
    f.rays[0] = {true, RayRole::Psi, {-.2f, 1.4f, -.3f}, {0, 0, -1}, {-.2f, 1.4f, -10}, false, 9.7f};
    f.holstersEnabled = true;
    f.holsters[0] = Slot{true, {.25f, .95f, 0}, .14f, {.25f, .95f, .05f}, false, true, false, false, false, Notice::None, 0, "HIP R"};
    f.holsters[1] = Slot{true, {-.2f, 1.3f, -.13f}, .14f, {.2f, 1.4f, -.3f}, false, true, false, true, false, Notice::None, 0, "CHEST L"};
    Scene scene;
    BuildOverlayScene(f, kRays, scene);
    Require(!scene.lines.empty() && scene.spheres.empty() && scene.labels.empty(), "rays only: no slots, no text");
    Require(scene.markers.size() == 3, "a hit draws a dot and a halo, a miss a ring");
    BuildOverlayScene(f, kBody, scene);
    Require(scene.spheres.size() == 2, "both holster slots are drawn");
    BuildOverlayScene(f, kBody | kLabels | kStatus, scene);
    Require(scene.labels.size() == 3, "a label per slot plus the status panel");
    const auto status = OverlayStatusText(f, kAllLayers);
    Require(status.find("hit 2.70 m") != std::string::npos && status.find("psi") != std::string::npos,
            "the status panel reports both rays");
    f.holstersEnabled = false;
    BuildOverlayScene(f, kBody, scene);
    Require(scene.spheres.empty(), "disabled holsters draw nothing");
    // The aim lasers: the rays ended on the scene, nothing of the debug view.
    f.holstersEnabled = true;
    BuildOverlayScene(f, kLaserLayers, scene);
    Require(!scene.lines.empty() && scene.markers.size() == 3 && scene.spheres.empty() && scene.labels.empty(),
            "the lasers draw both rays, and no slots, labels or status");
    // Each laser line eases in from the hand; a short one over half its length.
    bool fading = true;
    for (const auto& line : scene.lines) { fading = fading && line.fadeIn == kLaserFadeIn; }
    Require(fading, "every laser line fades in");
    f.rays[1].target = {.2f, 1.4f, -.5f};
    BuildOverlayScene(f, kRays, scene);
    Require(Near(scene.lines[0].fadeIn, .1f, 1e-5f), "a 20 cm laser fades over its first half");
    f.rays[0] = {};
    f.rays[1] = {};
    f.rays[0].valid = true;
    f.rays[0].role = RayRole::Use;
    f.rays[0].origin = {-.2f, 1.4f, -.3f};
    f.rays[0].direction = {0, 0, -1};
    f.rays[0].target = {-.1f, 1.4f, -1.3f};
    f.rays[0].selected = true;
    BuildOverlayScene(f, kRays, scene);
    // The faint straight line, the leg to the bend, the bend, the halo.
    Require(scene.lines.size() == 4 && scene.lines[0].fadeIn > 0 && scene.lines[1].fadeIn > 0 &&
                scene.lines[2].fadeIn == 0 && scene.lines[3].fadeIn > 0,
            "the use pointer fades in from the hand, not at its bend");
}

void TestDrawnLayers()
{
    // The debug layers, the lasers and the shot trail are switched apart; no
    // one's off takes another.
    Require(DrawnLayers(0, false, false) == 0, "all off: nothing is drawn");
    Require(DrawnLayers(0, true, false) == (kRays | kHits), "the lasers alone: rays ended on the scene, no shots");
    Require(DrawnLayers(0, false, true) == kShots, "the shot trail alone");
    Require(DrawnLayers(kAllLayers, false, false) == kAllLayers, "the debug overlay alone: every layer, shots included");
    Require(DrawnLayers(kBody, true, false) == (kBody | kRays | kHits), "a debug layer beside the lasers");
    Require(DrawnLayers(kBody, true, true) == (kBody | kRays | kHits | kShots), "and beside the shot trail");
    Require(DrawnLayers(kAllLayers, true, true) == kAllLayers, "all on: every layer, once");
    Require(DrawnLayers(0xFFFu, false, false) == kAllLayers, "no layer beyond the known ones");
}

void TestShotTrail()
{
    OverlayFrame f{};
    f.shot.valid = true;
    f.shot.spawn = {.2f, 1.4f, -.5f};
    f.shot.target = {.2f, 1.4f, -6};
    f.shot.age = .5f;
    Scene scene;
    BuildOverlayScene(f, kLaserLayers, scene);
    Require(scene.lines.empty() && scene.markers.empty(), "the aim lasers draw no shot");
    BuildOverlayScene(f, kShots, scene);
    Require(scene.lines.size() == 1 && scene.markers.size() == 2 && scene.labels.empty(),
            "the shot trail: its line and two ends, no text");
    Require(scene.lines[0].a.z == f.shot.spawn.z && scene.lines[0].b.z == f.shot.target.z, "from the muzzle to the target");
    BuildOverlayScene(f, kAllLayers, scene);
    Require(scene.lines.size() == 1 && scene.labels.size() == 1, "the debug overlay shows it, labelled");
    f.shot.age = kShotSeconds + .1f;
    BuildOverlayScene(f, kShots, scene);
    Require(scene.lines.empty() && scene.markers.empty(), "an old shot is gone");
}

void TestArmFitLayer()
{
    OverlayFrame f{};
    f.headValid = true;
    auto& fit = f.armFit;
    fit.valid = true;
    fit.torso[armpose::kChest] = {{-.04f, 1.3f, -.05f}, {.04f, 1.3f, -.05f}, .12f};
    // The right arm hangs past the chest, its elbow in it; the left is not solved.
    auto& arm = fit.arms[0];
    arm.valid = true;
    arm.shoulder = {.19f, 1.47f, .09f};
    arm.elbow = {.12f, 1.25f, -.05f};
    arm.wrist = {.10f, 1.30f, -.30f};
    arm.clearance = armpose::MeasureArm(arm.shoulder, arm.elbow, arm.wrist, fit.torso.data(), armpose::kTorsoParts,
                                        armpose::ArmThickness{});
    Require(arm.clearance.Worst() < 0, "the fixture's arm is in the chest");
    Scene scene;
    BuildOverlayScene(f, kRays | kBody | kPhysical, scene);
    Require(scene.lines.empty() && scene.markers.empty(), "the arms against the torso are their own layer");
    BuildOverlayScene(f, kArms, scene);
    // A capsule is two rings of 24, four sides and two caps of two half circles of 12.
    Require(scene.lines.size() == 100 + 2, "one capsule with a radius, and the arm's two segments");
    Require(scene.markers.size() == armpose::kArmSamples + 1, "a ring per sample, and a dot on the deepest");
    const Marker& deepest = scene.markers.back();
    Require(deepest.filled && deepest.color.r == kDenied.r && deepest.color.g == kDenied.g, "the deepest is red");
    Require(!scene.markers[0].filled && scene.markers[0].radius == armpose::ArmThickness{}.upper,
            "each ring as thick as the arm there");
    Require(ClearanceColor(-.001f).g == kDenied.g && ClearanceColor(.005f).g == kWarn.g && ClearanceColor(.02f).g == kActive.g,
            "red inside, amber near, green clear");
    BuildOverlayScene(f, kArms | kLabels, scene);
    Require(scene.labels.size() == 1 && scene.labels[0].text.rfind("R ", 0) == 0 &&
                scene.labels[0].text.find(" cm chest") != std::string::npos,
            "the deepest sample is labelled with its arm, depth and torso part");
    const std::string status = OverlayStatusText(f, kArms);
    Require(status.find("\nArms vs torso  R ") != std::string::npos && status.find("  L -") != std::string::npos,
            "the status panel gives each arm's deepest sample");
    Require(OverlayStatusText(f, kRays).find("Arms") == std::string::npos, "only with the layer");
    fit.valid = false;
    BuildOverlayScene(f, kArms, scene);
    Require(scene.lines.empty() && scene.markers.empty(), "no torso this frame: nothing drawn");
    Require(OverlayStatusText(f, kArms).find("\nArms vs torso -") != std::string::npos, "and the panel says so");
}

void TestPhysicalLayer()
{
    OverlayFrame f{};
    f.headValid = true;
    auto& p = f.physical;
    p.valid = true;
    p.open = true;
    p.gate = "open";
    const Vec3 lo{-.06f, 1.2f, -.5f}, hi{.06f, 1.3f, -.1f};
    p.points[0] = Scale(Add(lo, hi), .5f);
    for (unsigned i = 0; i < 8; ++i) { p.points[i + 1] = {i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z}; }
    Scene scene;
    BuildOverlayScene(f, kRays, scene);
    Require(scene.lines.empty() && scene.markers.empty(), "the sweep is its own layer");
    BuildOverlayScene(f, kPhysical, scene);
    Require(scene.lines.size() == 12 && scene.markers.size() == 1, "a box is twelve edges and its centre");
    Require(scene.lines[0].color.r == kRight.r && scene.lines[0].color.g == kRight.g, "open: drawn amber");
    p.open = false;
    p.gate = "rig";
    BuildOverlayScene(f, kPhysical | kLabels, scene);
    Require(scene.lines[0].color.r == kMuted.r && scene.labels.size() == 1 && scene.labels[0].text == "SWEEP rig",
            "gated: grey, labelled with the refusing gate");
    PhysicalSweep sweep{};
    for (unsigned i = 0; i < 9; ++i) { sweep.from[i] = p.points[i]; sweep.to[i] = Add(p.points[i], {.03f, 0, 0}); }
    sweep.age = .2f;
    p.sweeps.push_back(sweep);
    sweep.age = 2.0f;
    p.sweeps.push_back(sweep);
    BuildOverlayScene(f, kPhysical, scene);
    Require(scene.lines.size() == 12 + 9, "a recent sweep draws nine paths; an old one nothing");
    p.contact = true;
    p.acted = false;
    p.point = hi;
    p.normal = {0, 0, 1};
    p.contactAge = .5f;
    p.result = "not_rigid";
    BuildOverlayScene(f, kPhysical | kLabels, scene);
    Require(scene.markers.size() == 2 && scene.markers[1].color.r == kDenied.r && scene.lines.size() == 12 + 9 + 1,
            "a refused contact: a red dot and its normal");
    Require(scene.labels.size() == 2 && scene.labels[1].text == "CONTACT not_rigid", "labelled with its result");
    p.contactAge = kContactSeconds + 1;
    BuildOverlayScene(f, kPhysical, scene);
    Require(scene.markers.size() == 1, "an old contact is gone");
    p.status = "wrench on  gate rig";
    Require(OverlayStatusText(f, kAllLayers).find("\nPhysical wrench on") != std::string::npos, "the status panel reports it");
    Require(OverlayStatusText(f, kRays).find("Physical") == std::string::npos, "only with the layer");
    p.points[3].x = NAN;
    BuildOverlayScene(f, kPhysical, scene);
    Require(scene.lines.size() == 9, "a box with a bad corner is not drawn");
}

void TestSlotColours()
{
    Slot s{};
    s.valid = true;
    s.centre = {0, 1, 0};
    s.hand = {1, 1, 0};
    s.armed = true;
    Require(SlotColor(s).r == kIdle.r && SlotColor(s).b == kIdle.b, "away: idle");
    s.hand = {.05f, 1, 0};
    Require(SlotColor(s).g == kNear.g && SlotColor(s).b == kNear.b, "inside with a released grip: ready to squeeze");
    s.gripPressed = true;
    s.armed = false;
    Require(SlotColor(s).r == kWarn.r && SlotColor(s).g == kWarn.g, "inside with the grip already held: warn, will not act");
    s.owned = true;
    Require(SlotColor(s).g == kActive.g && SlotColor(s).r == kActive.r, "owned: active");
    s.notice = Notice::Denied;
    s.noticeAge = .3f;
    Require(SlotColor(s).r == kDenied.r && SlotColor(s).g == kDenied.g, "a fresh refusal flashes red");
    s.noticeAge = 5;
    Require(SlotColor(s).g == kActive.g, "an old notice no longer colours the slot");
}

}  // namespace

int main()
{
    TestProjection();
    TestNearClip();
    TestLines();
    TestFadeIn();
    TestSpheresMarkersLabels();
    TestGestureTruth();
    TestWristMeasureMatchesVisibility();
    TestSceneLayers();
    TestDrawnLayers();
    TestShotTrail();
    TestPhysicalLayer();
    TestArmFitLayer();
    TestSlotColours();
    std::cout << "debug_draw: all tests passed\n";
    return 0;
}
