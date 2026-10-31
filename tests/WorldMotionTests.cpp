// World motion vectors in the second eye (preyvr/WorldMotion.h): objects compile
// once, against the first eye's previous view. With the second eye's previous
// projection moved, an object at rest has no motion in that eye and a moving one
// has its true motion, whichever way the head went; without it, every pixel's
// motion is the stereo disparity. The pixels are the headset's, 2644 across.
#include "preyvr/WorldMotion.h"
#include "TestSupport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

using namespace preyvr::worldmotion;
using preyvr::test::Require;

namespace {

using Matrix = std::array<float, 16>;   // row-vector, translation in row 3, as Prey's
using Point = std::array<float, 3>;
constexpr double kHalfWidth = 1322.0;
constexpr float kEyes = 0.065f;

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

// Where a point lands on screen, in pixels from the centre.
std::array<double, 2> Screen(const Point& p, const Matrix& m)
{
    double clip[4]{};
    for (int j = 0; j < 4; ++j) {
        clip[j] = static_cast<double>(p[0]) * m[j] + static_cast<double>(p[1]) * m[4 + j] +
                  static_cast<double>(p[2]) * m[8 + j] + m[12 + j];
    }
    return {clip[0] / clip[3] * kHalfWidth, clip[1] / clip[3] * kHalfWidth};
}
double Apart(const std::array<double, 2>& a, const std::array<double, 2>& b)
{
    return std::hypot(a[0] - b[0], a[1] - b[1]);
}

// A view from a camera at `eye`, with rows the world axes in the camera's right,
// up and back. Doubles down to the last step, as the engine's camera is float.
Matrix View(float yaw, float pitch, const Point& eye)
{
    const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const double right[3] = {cy, sy, 0.0};
    const double forward[3] = {-sy * cp, cy * cp, sp};
    const double up[3] = {right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2],
                          right[0] * forward[1] - right[1] * forward[0]};
    Matrix v{};
    double translation[3]{};
    for (int i = 0; i < 3; ++i) {
        v[4 * i + 0] = static_cast<float>(right[i]);
        v[4 * i + 1] = static_cast<float>(up[i]);
        v[4 * i + 2] = static_cast<float>(-forward[i]);
        translation[0] -= eye[i] * right[i];
        translation[1] -= eye[i] * up[i];
        translation[2] += eye[i] * forward[i];
    }
    for (int i = 0; i < 3; ++i) { v[12 + i] = static_cast<float>(translation[i]); }
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
// An object's world transform, row-vector, placed at `at` and turned about up.
Matrix World(float turn, const Point& at)
{
    Matrix w{};
    w[0] = std::cos(turn);
    w[1] = std::sin(turn);
    w[4] = -w[1];
    w[5] = w[0];
    w[10] = 1.0f;
    w[12] = at[0];
    w[13] = at[1];
    w[14] = at[2];
    w[15] = 1.0f;
    return w;
}
// A camera `distance` ahead of `head`, the view's right offset by half the eyes.
Point Eye(const Point& head, float yaw, float side)
{
    return {head[0] + side * std::cos(yaw), head[1] + side * std::sin(yaw), head[2]};
}

// One object's previous position for the second eye, in pixels: as compiled, and
// through the moved projection, and the truth, from its own previous view.
struct Previous {
    std::array<double, 2> compiled, corrected, truth;
};
Previous Second(const Matrix& previousWorld, const Matrix& firstPrevious, const Matrix& secondPrevious, const Point& local)
{
    const Matrix projection = Projection();
    const Matrix constants = Multiply(previousWorld, firstPrevious);      // 0xFD4570, compiled once
    Matrix relative{}, moved{};
    Require(Between(firstPrevious.data(), secondPrevious.data(), relative.data()), "two views have a matrix between them");
    Follow(relative.data(), projection.data(), moved.data());
    return {Screen(local, Multiply(constants, projection)), Screen(local, Multiply(constants, moved)),
            Screen(local, Multiply(Multiply(previousWorld, secondPrevious), projection))};
}

void TestObjectAtRest()
{
    const Point head{4.0f, -2.0f, 1.6f};
    const float yaw = 0.3f, pitch = -0.05f;
    const Matrix first = View(yaw, pitch, Eye(head, yaw, -kEyes / 2.0f));
    const Matrix second = View(yaw, pitch, Eye(head, yaw, kEyes / 2.0f));
    // Two metres ahead of the head.
    const Point at{head[0] - 2.0f * std::sin(yaw), head[1] + 2.0f * std::cos(yaw), 1.4f};
    const Matrix world = World(0.7f, at);
    const Point local{0.1f, -0.05f, 0.2f};
    const auto current = Screen(local, Multiply(Multiply(world, second), Projection()));
    const Previous previous = Second(world, first, second, local);
    Require(Apart(previous.compiled, current) > 25.0, "as compiled: 2 m away, the second eye's motion is the disparity");
    Require(Apart(previous.corrected, current) < 0.01, "moved: an object at rest has no motion in the second eye");
}

void TestHeadAndObjectMove()
{
    const Point head{4.0f, -2.0f, 1.6f}, headBefore{3.9f, -2.1f, 1.62f};
    const float yawBefore = 0.35f, pitchBefore = -0.02f;
    const Matrix first = View(yawBefore, pitchBefore, Eye(headBefore, yawBefore, -kEyes / 2.0f));
    const Matrix second = View(yawBefore, pitchBefore, Eye(headBefore, yawBefore, kEyes / 2.0f));
    const Matrix worldBefore = World(0.65f, {3.0f, 0.2f, 1.2f});
    const Point local{0.3f, 0.1f, -0.4f};
    const Previous previous = Second(worldBefore, first, second, local);
    Require(Apart(previous.corrected, previous.truth) < 0.01, "a moving object, a moved head: the second eye's true previous position");
    Require(Apart(previous.compiled, previous.truth) > 15.0, "as compiled it is a disparity away from it");
}

void TestCantedEyes()
{
    const Point head{1.0f, 2.0f, 1.5f};
    const float yaw = -0.4f;
    const Matrix first = View(yaw + 0.03f, 0.1f, Eye(head, yaw, -kEyes / 2.0f));
    const Matrix second = View(yaw - 0.03f, 0.1f, Eye(head, yaw, kEyes / 2.0f));
    const Previous previous = Second(World(1.1f, {0.0f, 4.0f, 1.0f}), first, second, {0.2f, 0.2f, 0.2f});
    Require(Apart(previous.corrected, previous.truth) < 0.01, "eyes that are not parallel: still the true position");
}

void TestFarFromTheOrigin()
{
    // Station coordinates: the views' translations are kilometres in floats. Only
    // V1's true inverse cancels its float rotation exactly; its transpose leaves
    // the kilometres times that rounding, up to half a pixel 1 m away.
    const Point head{2047.3f, -1533.7f, 612.4f};
    double worst = 0.0;
    for (int turn = 0; turn < 48; ++turn) {
        for (int tilt = -4; tilt <= 4; ++tilt) {
            const float yaw = 0.13f * static_cast<float>(turn), pitch = 0.17f * static_cast<float>(tilt);
            const Matrix first = View(yaw, pitch, Eye(head, yaw, -kEyes / 2.0f));
            const Matrix second = View(yaw, pitch, Eye(head, yaw, kEyes / 2.0f));
            const Point at{head[0] - std::sin(yaw), head[1] + std::cos(yaw), head[2]};
            const Previous previous = Second(World(0.2f, at), first, second, {0.05f, 0.05f, 0.05f});
            worst = std::max(worst, Apart(previous.corrected, previous.truth));
            Require(Apart(previous.compiled, previous.truth) > 50.0, "2 km out, 1 m away: as compiled, the disparity");
        }
    }
    Require(worst < 0.02, "2 km out, 1 m away: the second eye's own view, whichever way it looks");
}

void TestSeparation()
{
    const Point head{4.0f, -2.0f, 1.6f};
    const Matrix first = View(0.3f, 0.0f, Eye(head, 0.3f, -kEyes / 2.0f));
    const Matrix second = View(0.3f, 0.0f, Eye(head, 0.3f, kEyes / 2.0f));
    Matrix relative{};
    Require(Between(first.data(), second.data(), relative.data()), "two views");
    Require(std::fabs(Separation(relative.data()) - kEyes) < 1e-5, "the separation is the eyes'");
    Require(std::fabs(relative[0] - 1.0f) < 1e-6f && std::fabs(relative[5] - 1.0f) < 1e-6f, "same head: no rotation");
    Require(relative[3] == 0.0f && relative[7] == 0.0f && relative[11] == 0.0f && relative[15] == 1.0f, "column 3 stays");
}

void TestRefusals()
{
    const Matrix view = View(0.3f, 0.1f, {1.0f, 2.0f, 3.0f});
    Require(LooksLikeView(view.data()), "a view is a view");
    Matrix scaled = view;
    for (int i = 0; i < 12; ++i) { scaled[i] *= 2.0f; }
    Matrix relative{};
    Require(!Between(scaled.data(), view.data(), relative.data()), "a scaled first view is refused");
    Require(!Between(view.data(), scaled.data(), relative.data()), "a scaled second view is refused");
    Matrix bad = view;
    bad[6] = std::numeric_limits<float>::quiet_NaN();
    Require(!LooksLikeView(bad.data()), "not finite is refused");
    Require(!LooksLikeView(Projection().data()), "a projection is not a view");
    const Matrix empty{};
    Require(!LooksLikeView(empty.data()), "nor is nothing");
}

} // namespace

int main()
{
    TestObjectAtRest();
    TestHeadAndObjectMove();
    TestCantedEyes();
    TestFarFromTheOrigin();
    TestSeparation();
    TestRefusals();
    std::cout << "world_motion: the second eye's previous positions are its own\n";
}
