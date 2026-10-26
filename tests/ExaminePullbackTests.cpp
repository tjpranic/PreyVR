// Screen Distance (preyvr/ExaminePullback.h): the examination camera moved back
// along its view, never further than the eye was, and the VR Options setting's
// range the one the camera takes.
#include "preyvr/ExaminePullback.h"
#include "preyvr/VrOptions.h"
#include "TestSupport.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace preyvr;
using namespace preyvr::examine;
using preyvr::test::Require;

namespace {

bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }
bool Near(Vec3 a, Vec3 b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }

// What Prey's fit faces for an entity with no rotation: its axes turned 180
// degrees about Z, so the camera on the entity's +Y side looks along -Y.
constexpr Quaternion kFacingScreen{0.0f, 0.0f, 1.0f, 0.0f};
const float kNaN = std::numeric_limits<float>::quiet_NaN();

void TestRange()
{
    Require(ClampCm(0) == 0 && ClampCm(30) == 30 && ClampCm(100) == 100 && ClampCm(101) == 100 && ClampCm(5000) == 100,
            "0 to 100 cm, more taken as 100");
    Require(Near(Metres(30), 0.3f) && Near(Metres(100), 1.0f) && Metres(0) == 0.0f, "in metres, as the camera moves");
    const auto& spec = options::Describe(options::ScreenPullback);
    Require(spec.min == static_cast<int>(kMinCm) && spec.max == static_cast<int>(kMaxCm) &&
                spec.initial == static_cast<int>(kDefaultCm),
            "VR Options' Screen Distance is the range and default the camera takes");
}

void TestForward()
{
    Require(Near(Forward(Quaternion{}), Vec3{0, 1, 0}), "no rotation looks along +Y, CryEngine's forward");
    Require(Near(Forward(kFacingScreen), Vec3{0, -1, 0}), "Prey's facing for an unrotated screen looks along -Y");
    const float h = std::sqrt(0.5f);
    Require(Near(Forward(Quaternion{0, 0, h, h}), Vec3{-1, 0, 0}), "a quarter turn left about Z looks along -X");
    Require(Near(Forward(Quaternion{h, 0, 0, h}), Vec3{0, 0, 1}), "a quarter turn up about X looks up");
    Require(Near(Forward(Quaternion{0, 0, 1.1f, 0}), Vec3{0, -1, 0}), "a slightly long rotation is still a rotation");
    Require(Length(Forward(Quaternion{0, 0, 0, 0})) == 0.0f && Length(Forward(Quaternion{0, 0, 2, 0})) == 0.0f &&
                Length(Forward(Quaternion{kNaN, 0, 0, 1})) == 0.0f,
            "a zero, far too long or non-finite quaternion is no rotation");
}

void TestPlace()
{
    // The screen in the plane y = 0, the fit 10 cm in front of it, facing it.
    const Vec3 fit{0.2f, 0.1f, 1.5f};
    Require(Near(EyeDepth(fit, Vec3{0, -1, 0}, Vec3{0.5f, 0.9f, 1.6f}), 0.8f), "an eye 90 cm out is 80 cm behind the fit");
    Require(Near(EyeDepth(fit, Vec3{0, -1, 0}, Vec3{0, 0.05f, 1.5f}), -0.05f), "an eye nearer the screen is ahead of it");

    Placement p = Place(fit, kFacingScreen, 30, 0.8f);
    Require(Near(p.pull, 0.3f) && Near(p.position, Vec3{0.2f, 0.4f, 1.5f}), "30 cm back along the view, centred");
    p = Place(fit, kFacingScreen, 100, 0.8f);
    Require(Near(p.pull, 0.8f) && Near(p.position, Vec3{0.2f, 0.9f, 1.5f}), "never further back than the eye was");
    p = Place(fit, kFacingScreen, 30, -0.05f);
    Require(p.pull == 0.0f && Near(p.position, fit), "an eye nearer than the fit: Prey's own camera");
    p = Place(fit, kFacingScreen, 0, 0.8f);
    Require(p.pull == 0.0f && Near(p.position, fit), "0: Prey's own camera");
    p = Place(fit, kFacingScreen, 300, 5.0f);
    Require(Near(p.pull, 1.0f), "more than 100 cm is taken as 100");
    p = Place(fit, kFacingScreen, 30, kNaN);
    Require(p.pull == 0.0f && Near(p.position, fit), "no eye known: not moved");
    p = Place(fit, Quaternion{0, 0, 0, 0}, 30, 0.8f);
    Require(p.pull == 0.0f && Near(p.position, fit), "no facing: not moved");
    p = Place(Vec3{kNaN, 0, 0}, kFacingScreen, 30, 0.8f);
    Require(p.pull == 0.0f, "a non-finite fit is not moved");

    // A tilted screen, the view 30 degrees up: back along that view, still centred.
    const float half = 15.0f * kRadiansPerDegree;
    const Quaternion tilted{std::sin(half), 0.0f, 0.0f, std::cos(half)};
    Require(Near(Forward(tilted), Vec3{0, std::cos(2 * half), std::sin(2 * half)}), "the tilted view");
    p = Place(fit, tilted, 50, 2.0f);
    Require(Near(Sub(fit, p.position), Scale(Forward(tilted), 0.5f)), "a tilted screen: back along its own view");
}

} // namespace

int main()
{
    TestRange();
    TestForward();
    TestPlace();
    std::cout << "examine_pullback: the camera moved back along its view, never past the eye\n";
    return 0;
}
