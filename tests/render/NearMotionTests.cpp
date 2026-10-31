// First-person motion vectors under the near stereo (render/taa/NearMotion.h): with
// the previous near projection moved as the near VP is, a near object that has
// not moved against the head has no motion in either eye, whichever way the
// head turned since; without it, every pixel's motion is the eye's parallax.
#include "render/taa/NearMotion.h"
#include "TestSupport.h"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace preyvr::nearmotion;
using preyvr::test::Require;

namespace {

using Matrix = std::array<float, 16>;   // row-vector, translation in row 3, as Prey's
using Vec4 = std::array<float, 4>;

Matrix Multiply(const Matrix& a, const Matrix& b)
{
    Matrix m{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            for (int k = 0; k < 4; ++k) { m[4 * i + j] += a[4 * i + k] * b[4 * k + j]; }
        }
    }
    return m;
}
Vec4 Transform(const Vec4& v, const Matrix& m)
{
    Vec4 out{};
    for (int j = 0; j < 4; ++j) {
        for (int k = 0; k < 4; ++k) { out[j] += v[k] * m[4 * k + j]; }
    }
    return out;
}
// Where a clip-space point lands on screen.
std::array<float, 2> Screen(const Vec4& clip) { return {clip[0] / clip[3], clip[1] / clip[3]}; }

// A translation-free view: rows are the world axes, columns the camera's right,
// up and back, from a yaw and a pitch (radians).
Matrix View(float yaw, float pitch)
{
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const float right[3] = {cy, sy, 0.0f};
    const float forward[3] = {-sy * cp, cy * cp, sp};
    const float up[3] = {right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2],
                         right[0] * forward[1] - right[1] * forward[0]};
    Matrix v{};
    for (int i = 0; i < 3; ++i) {
        v[4 * i + 0] = right[i];
        v[4 * i + 1] = up[i];
        v[4 * i + 2] = -forward[i];
    }
    v[15] = 1.0f;
    return v;
}
// An off-centre perspective with a sub-pixel jitter, w the negated view depth.
Matrix Projection()
{
    Matrix p{};
    p[0] = 0.71f;
    p[5] = 0.70f;
    p[8] = 0.0123f;     // off-centre plus jitter
    p[9] = -0.0087f;
    p[10] = -0.000750f;
    p[11] = -1.0f;
    p[14] = 0.0300f;
    return p;
}
// The camera's right axis in the world: the view's column 0.
std::array<float, 3> Right(const Matrix& v) { return {v[0], v[4], v[8]}; }

// Translation(-d) x m, as NearViewStereo moves the near VP.
Matrix Shifted(const Matrix& m, const Matrix& axes, const std::array<float, 3>& d)
{
    Matrix out = m;
    ShiftTranslationRow(out.data() + 12, axes.data(), d[0], d[1], d[2]);
    return out;
}

bool Same(const std::array<float, 2>& a, const std::array<float, 2>& b, float tolerance = 1e-5f)
{
    return std::fabs(a[0] - b[0]) < tolerance && std::fabs(a[1] - b[1]) < tolerance;
}

void TestStillObject()
{
    const Matrix projection = Projection();
    const float half = 0.032f;
    // A pistol at arm's length, camera-relative (world axes, as Prey hands near objects).
    const std::array<Vec4, 3> points{{{0.18f, 0.35f, -0.12f, 1.0f}, {0.05f, 0.42f, -0.20f, 1.0f},
                                      {0.25f, 0.30f, -0.05f, 1.0f}}};
    for (int eye = 0; eye < 2; ++eye) {
        const float sign = eye == 0 ? -1.0f : 1.0f;
        // This frame's head and the last's, turned between them.
        const Matrix now = View(0.40f, -0.15f), before = View(0.43f, -0.13f);
        const Matrix nearVp = Multiply(now, projection);
        const auto d = Right(now);
        const std::array<float, 3> eyeNow{d[0] * sign * half, d[1] * sign * half, d[2] * sign * half};
        const Matrix current = Shifted(nearVp, nearVp, eyeNow);
        // The previous near projection, moved against the UNEDITED near VP as NearViewStereo does.
        const Matrix previous = Shifted(projection, nearVp, eyeNow);
        for (const Vec4& p : points) {
            // The object has not moved against the head: the same camera-relative point both frames.
            const Vec4 viewNow = Transform(p, now), viewBefore = Transform(p, before);
            const auto here = Screen(Transform(p, current));
            Require(Same(here, Screen(Transform(viewNow, previous))),
                    "head still: the previous position is this one, so no motion");
            // Where this eye really saw it last frame: the same point from the eye then.
            const auto d2 = Right(before);
            const Vec4 eyeBefore{p[0] - d2[0] * sign * half, p[1] - d2[1] * sign * half,
                                 p[2] - d2[2] * sign * half, 1.0f};
            Require(Same(Screen(Transform(viewBefore, previous)), Screen(Transform(Transform(eyeBefore, before),
                                                                                   projection))),
                    "head turned: the previous position is where this eye saw it last frame");
            // Prey's own previous projection: the centre's view, not this eye's.
            const auto unshifted = Screen(Transform(viewNow, projection));
            Require(std::fabs(unshifted[0] - here[0]) > 0.02f,
                    "without the shift, a still pistol's motion is the eye's parallax");
        }
    }
}

void TestShape()
{
    const Matrix projection = Projection();
    const Matrix nearVp = Multiply(View(0.4f, -0.15f), projection);
    Require(LooksPerspective(projection.data()), "a near projection is perspective-shaped");
    Require(!LooksPerspective(nearVp.data()), "a view-projection is not: its column 3 is the view's back axis");
    Matrix identity{};
    identity[0] = identity[5] = identity[10] = identity[15] = 1.0f;
    Require(!LooksPerspective(identity.data()), "nor is an identity");
    Matrix broken = projection;
    broken[5] = std::nanf("");
    Require(!LooksPerspective(broken.data()), "nor anything not finite");
    Matrix same = nearVp;
    ShiftTranslationRow(same.data() + 12, nearVp.data(), 0.0f, 0.0f, 0.0f);
    Require(std::memcmp(same.data(), nearVp.data(), sizeof(same)) == 0, "no displacement: the very same bits");
}

} // namespace

int main()
{
    TestStillObject();
    TestShape();
    std::cout << "near_motion: the previous near projection moved with each eye, so a still pistol has no motion\n";
    return 0;
}
