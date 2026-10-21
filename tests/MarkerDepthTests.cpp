#include "preyvr/MarkerDepth.h"
#include "TestSupport.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace preyvr;
using namespace preyvr::markers;

namespace {

using preyvr::test::Require;
bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

// A camera as CArkUIHUD::OnPreRender builds its matrix: right/up/forward axes,
// symmetric perspective scales, column-major, w = forward depth.
struct Camera {
    Vec3 position, right, up, forward;
    float p00, p11;
};

HudMatrix MatrixOf(const Camera& c)
{
    HudMatrix m{};
    const Vec3 rows[4] = {Scale(c.right, c.p00), Scale(c.up, c.p11), c.forward, c.forward};
    for (int r = 0; r < 4; ++r) {
        m[0 + r] = rows[r].x;
        m[4 + r] = rows[r].y;
        m[8 + r] = rows[r].z;
        m[12 + r] = -Dot(rows[r], c.position);
    }
    return m;
}

// 0x16659F0 without the clamp: normalized x right, y down.
void Project(const HudMatrix& m, Vec3 p, float& nx, float& ny, float& w)
{
    const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    const float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    nx = (x / std::fabs(w) + 1.0f) * 0.5f;
    ny = 0.5f - y / std::fabs(w) * 0.5f;
}

// Prey's edge clamp, as a direction from the screen centre onto the 0.05..0.95 box.
void Clamp(float& nx, float& ny, bool behind)
{
    float dx = nx - 0.5f, dy = ny - 0.5f;
    if (behind) { dx = -dx; dy = -dy; }
    const float s = 0.45f / std::fmax(std::fabs(dx), std::fabs(dy));
    if (s < 1.0f || behind) { nx = 0.5f + dx * s; ny = 0.5f + dy * s; }
}

Camera Head()
{
    // CryEngine axes: x right, y forward, z up. Turned a little, off the origin.
    const float yaw = 0.4f;
    const Vec3 forward{-std::sin(yaw), std::cos(yaw), 0.f};
    const Vec3 right{std::cos(yaw), std::sin(yaw), 0.f};
    // Prey's native frame: 1.732 = tan(60 deg) half width at 16:9.
    return {{12.f, -3.f, 1.7f}, right, {0, 0, 1}, forward, 1.f / 1.732f, 1.f / 0.974f};
}

Camera Moved(const Camera& c, float offset)
{
    Camera e = c;
    e.position = Add(c.position, Scale(c.right, offset));
    return e;
}

void TestScale()
{
    const Camera c = Head();
    HudScale s{};
    Require(ScaleOf(MatrixOf(c), s) && Near(s.p00, c.p00, 1e-6f) && Near(s.p11, c.p11, 1e-6f), "scales read back");
    HudMatrix bad = MatrixOf(c);
    bad[5] = std::numeric_limits<float>::quiet_NaN();
    Require(!ScaleOf(bad, s), "a NaN matrix is refused");
    HudMatrix zero{};
    Require(!ScaleOf(zero, s), "an empty matrix is refused");
    HudMatrix noDepth = MatrixOf(c);
    noDepth[3] = noDepth[7] = noDepth[11] = 0;
    Require(!ScaleOf(noDepth, s), "no forward axis is refused");
}

// On screen: the depth is the view depth, and the shift is exactly what the
// eye's own projection gives.
void TestOnScreenShift()
{
    const Camera head = Head();
    const HudMatrix m = MatrixOf(head);
    HudScale s{};
    Require(ScaleOf(m, s), "scale");
    const float canvasPerUnit = 2133.3f;  // a cover-fitted 1920 canvas wider than the screen
    const float half = 0.0345f;
    const Vec3 targets[] = {
        Add(head.position, Add(Scale(head.forward, 8.f), Scale(head.right, 2.f))),   // an objective ahead-right
        Add(head.position, Add(Scale(head.forward, 1.2f), Scale(head.up, -0.4f))),  // a repair icon close by
        Add(head.position, Add(Scale(head.forward, 30.f), Scale(head.right, -9.f))), // far left
    };
    for (const Vec3& target : targets) {
        float nx = 0, ny = 0, w = 0;
        Project(m, target, nx, ny, w);
        const Vec3 view = ViewPoint(m, s, target);
        Require(Near(view.z, w, 1e-4f), "view depth is the projection's w");
        float depth = 0;
        Require(DisplayedDepth(s, view, nx, ny, depth) && Near(depth, w, 1e-3f), "on screen: shown at its view depth");
        const float perMetre = CanvasShiftPerMetre(s, depth, canvasPerUnit);
        for (float offset : {-half, half}) {
            float ex = 0, ey = 0, ew = 0;
            Project(MatrixOf(Moved(head, offset)), target, ex, ey, ew);
            Require(Near((ex - nx) * canvasPerUnit, perMetre * offset, 2e-2f), "the shift is the eye's own projection");
            Require(Near(ey, ny, 1e-5f), "no vertical disparity");
        }
        Require(perMetre * -half > 0, "the left eye sees it further right");
    }
    // ~9 canvas px per eye at 2 m with a 69 mm IPD and Prey's native frustum.
    float depth = 0;
    Require(DisplayedDepth(s, {0, 0, 2.f}, 0.5f, 0.5f, depth), "straight ahead");
    Require(Near(CanvasShiftPerMetre(s, depth, 1920.f) * half, -9.56f, 0.05f), "magnitude at 2 m");
}

// Off screen: pinned near the edge, shown at the target's distance along the
// direction it is drawn in.
void TestOffScreenDepth()
{
    const Camera head = Head();
    const HudMatrix m = MatrixOf(head);
    HudScale s{};
    Require(ScaleOf(m, s), "scale");
    const Vec3 cases[] = {
        Add(head.position, Add(Scale(head.forward, 3.f), Scale(head.right, 12.f))),   // far to the right
        Add(head.position, Add(Scale(head.forward, -6.f), Scale(head.right, 1.f))),   // behind
    };
    for (const Vec3& target : cases) {
        float nx = 0, ny = 0, w = 0;
        Project(m, target, nx, ny, w);
        Clamp(nx, ny, w < 0);
        const Vec3 view = ViewPoint(m, s, target);
        float depth = 0;
        Require(DisplayedDepth(s, view, nx, ny, depth), "clamped marker resolves");
        // A point at the target's distance, in the drawn direction, has this depth
        // and projects where the marker is drawn.
        const float tx = (2 * nx - 1) / s.p00, ty = (1 - 2 * ny) / s.p11;
        const Vec3 drawn = Add(head.position, Add(Scale(head.forward, depth),
                                                  Add(Scale(head.right, tx * depth), Scale(head.up, ty * depth))));
        Require(Near(Length(Sub(drawn, head.position)), Length(Sub(target, head.position)), 1e-3f),
                "shown at the target's distance");
        float px = 0, py = 0, pw = 0;
        Project(m, drawn, px, py, pw);
        Require(Near(px, nx, 1e-5f) && Near(py, ny, 1e-5f) && Near(pw, depth, 1e-3f), "in the drawn direction");
        Require(depth > 0, "a target behind still gets a depth in front");
    }
}

void TestNearest()
{
    const Camera head = Head();
    const HudMatrix m = MatrixOf(head);
    HudScale s{};
    Require(ScaleOf(m, s), "scale");
    float depth = 0;
    const Vec3 touching = Add(head.position, Scale(head.forward, 0.05f));
    Require(DisplayedDepth(s, ViewPoint(m, s, touching), 0.5f, 0.5f, depth) && Near(depth, kNearestMetres),
            "nothing nearer than the reticle's floor");
    Require(!DisplayedDepth(s, {std::nanf(""), 0, 1}, 0.5f, 0.5f, depth), "NaN refused");
    Require(!DisplayedDepth(s, {0, 0, 1}, std::nanf(""), 0.5f, depth), "NaN position refused");
}

} // namespace

int main()
{
    TestScale();
    TestOnScreenShift();
    TestOffScreenDepth();
    TestNearest();
    std::cout << "marker_depth: all tests passed\n";
    return 0;
}
