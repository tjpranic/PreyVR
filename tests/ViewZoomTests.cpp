// Prey's zooms in the headset (preyvr/ViewZoom.h): a narrowed view read as a
// magnification of the FOV slider's, a wider view left as it is, and a point in
// the world moved to where a zoomed frame shows it.
#include "preyvr/ViewZoom.h"
#include "TestSupport.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace preyvr;
using namespace preyvr::viewzoom;
using preyvr::test::Require;

namespace {

constexpr float kDegree = 3.14159265358979f / 180.0f;

bool Near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) < tolerance; }
bool Near(Vec3 a, Vec3 b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }
float Tangent(float degrees) { return std::tan(degrees * 0.5f * kDegree); }

// UpdateView's vertical FOV for this hfov, on an eye H/W tall.
float ViewFov(float hfov, float aspect = 1.0f) { return 2.0f * std::atan(Tangent(hfov) * aspect); }

void TestMagnification()
{
    Require(Magnification(0.5f, 1.5f) == 3.0f, "the base over the view");
    Require(Magnification(1.5f, 1.5f) == 1.0f, "at the base: none");
    Require(Magnification(2.0f, 1.5f) == 1.0f, "wider than the base, a sprint: none");
    Require(Magnification(0.5f, 0.0f) == 1.0f, "no base: none");
    Require(Magnification(0.0f, 1.5f) == 1.0f && Magnification(-1.0f, 1.5f) == 1.0f, "no view: none");
    Require(Magnification(std::numeric_limits<float>::quiet_NaN(), 1.5f) == 1.0f, "NaN: none");
    // The camera's copy of the FOV goes through a tan and back.
    Require(Magnification(1.4281f, 1.4281f * 1.0005f) == 1.0f, "rounding is no zoom: the frame stays exact");
}

void TestBaseTangent()
{
    float base = 0;
    // No zoom: the view's own.
    Require(BaseTangent(ViewFov(110.0f), 110.0f, 110.0f, base) && Near(base, Tangent(110.0f)), "unzoomed: the view's own");
    // A zoom: the base stays the slider's, and the frame's magnification is the
    // ratio of the tangents, as deep as Prey goes.
    for (const float hfov : {104.4f, 60.0f, 49.4f, 24.8f}) {
        Require(BaseTangent(ViewFov(hfov), hfov, 110.0f, base) && Near(base, Tangent(110.0f)),
                "the base stays the slider's through a zoom");
        Require(Near(Magnification(Tangent(hfov), base), Tangent(110.0f) / Tangent(hfov), 1e-3f), "the magnification");
    }
    Require(Near(Tangent(110.0f) / Tangent(49.4f), 3.10f, 0.01f), "the psychoscope's own stop, measured at 3.1x");
    Require(Near(Tangent(110.0f) / Tangent(24.8f), 6.5f, 0.01f), "the deepest scan lock measured, 6.5x");
    // Wider than the slider: the frame is declared as it is.
    Require(BaseTangent(ViewFov(115.4f), 115.4f, 110.0f, base) && Magnification(Tangent(115.4f), base) == 1.0f,
            "a sprint's view is no zoom");
    // Any eye shape: the 2688x2880 Quest's as the Frame's square one.
    const float aspect = 2880.0f / 2688.0f;
    Require(BaseTangent(ViewFov(50.0f, aspect), 50.0f, 110.0f, base) && Near(base, Tangent(110.0f) * aspect),
            "the base in the eye's own shape");
}

void TestRefused()
{
    float base = 0;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float fov = ViewFov(60.0f);
    Require(!BaseTangent(nan, 60.0f, 110.0f, base) && !BaseTangent(fov, nan, 110.0f, base) &&
                !BaseTangent(fov, 60.0f, nan, base),
            "NaN");
    Require(!BaseTangent(0.0f, 60.0f, 110.0f, base) && !BaseTangent(3.2f, 60.0f, 110.0f, base), "no view's FOV");
    Require(!BaseTangent(fov, 0.0f, 110.0f, base) && !BaseTangent(fov, 180.0f, 110.0f, base), "no hfov");
    Require(!BaseTangent(fov, 60.0f, 0.0f, base) && !BaseTangent(fov, 60.0f, 180.0f, base), "no slider");
}

void TestShownPoint()
{
    // Facing -Z, at the origin. A point 1 m right of the axis at 10 m: a tangent of
    // 0.1, shown at 0.3 by a 3x zoom, so 3 m right at the same depth.
    const Pose ahead{};
    Require(Near(ShownPoint({1, 0, -10}, ahead, 3.0f), Vec3{3, 0, -10}), "out from the view's axis by the zoom");
    Require(Near(ShownPoint({0, -2, -10}, ahead, 3.0f), Vec3{0, -6, -10}), "up and down as well");
    Require(Near(ShownPoint({0, 0, -10}, ahead, 6.5f), Vec3{0, 0, -10}), "on the axis it stays");

    // What the head sees: the angle's tangent, times the zoom.
    const Vec3 shown = ShownPoint({1, 0.5f, -4}, ahead, 3.0f);
    Require(Near(shown.x / -shown.z, 3.0f * (1.0f / 4.0f)) && Near(shown.y / -shown.z, 3.0f * (0.5f / 4.0f)),
            "at the tangents the magnified image has it at");
    Require(Near(shown.z, -4.0f), "at the depth it has, which its stereo in the image keeps");

    // No zoom: exactly the point, not a round trip through the head.
    const Vec3 far{123.456f, -7.89f, 0.001f};
    const Pose head{Quaternion{0, 0.7071068f, 0, 0.7071068f}, Vec3{1, 2, 3}};
    const Vec3 same = ShownPoint(far, head, 1.0f);
    Require(same.x == far.x && same.y == far.y && same.z == far.z, "unzoomed: exactly where it was");

    // Turned 90 degrees left (+Y), standing elsewhere: forward is -X, right is -Z.
    // 1 m to its right at 10 m is (1-10, 2, 3-1); 3x puts it 3 m right.
    Require(Near(ShownPoint({-9, 2, 2}, head, 3.0f), Vec3{-9, 2, 0}), "about the head it was rendered from, in its axes");
    // And the head's own tangent agrees.
    const Vec3 local = Rotate(Quaternion{0, -0.7071068f, 0, 0.7071068f}, Sub(ShownPoint({-9, 2, 2}, head, 3.0f), head.position));
    Require(Near(local.x / -local.z, 0.3f), "a tangent of 0.1 shown at 0.3");
}

} // namespace

int main()
{
    TestMagnification();
    TestBaseTangent();
    TestRefused();
    TestShownPoint();
    std::cout << "view_zoom: a narrowed view read against the slider's, and world points placed where it shows them\n";
    return 0;
}
