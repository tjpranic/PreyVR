// The ray a screen is pointed at with (aim/ExamineRay.h): on the hand's line,
// started level with the eye, so it meets the screen where the controller's line
// does -- not where a ray from the eyes in the same direction would.
#include "aim/ExamineRay.h"
#include "TestSupport.h"

#include <cmath>
#include <iostream>

using namespace preyvr;
using namespace preyvr::examine;
using preyvr::test::Require;

namespace {

bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }
bool Near(Vec3 a, Vec3 b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }

// Where the ray from `origin` along `direction` meets the plane y = `screenY`.
Vec3 OnScreen(Vec3 origin, Vec3 direction, float screenY)
{
    return Add(origin, Scale(direction, (screenY - origin.y) / direction.y));
}

// The eye at the origin, a screen 42 cm ahead (y = 0.42: Prey's camera 12 cm off
// it, moved 30 cm back), the hand 20 cm right, 30 cm ahead and 25 cm below.
constexpr Vec3 kEye{0.0f, 0.0f, 0.0f};
constexpr Vec3 kHand{0.2f, 0.3f, -0.25f};
constexpr float kScreen = 0.42f;

void TestStraightAhead()
{
    const Vec3 ahead{0.0f, 1.0f, 0.0f};
    const Vec3 origin = ScreenRayOrigin(kHand, kEye, ahead);
    Require(Near(origin, Vec3{0.2f, 0.0f, -0.25f}), "on the hand's line, level with the eye");
    Require(Near(OnScreen(origin, ahead, kScreen), Vec3{0.2f, kScreen, -0.25f}),
            "meets the screen straight ahead of the controller");
    // What the eye-origin ray did: the cursor straight ahead of the eyes, 32 cm off.
    const Vec3 fromEye = OnScreen(kEye, ahead, kScreen);
    Require(std::fabs(Length(Sub(fromEye, OnScreen(origin, ahead, kScreen))) - 0.3201562f) < 1e-4f,
            "the eye's ray lands 32 cm from the controller's line");
}

void TestAngled()
{
    // Pointing down and to the left at the screen.
    const Vec3 direction = Scale(Vec3{-0.3f, 1.0f, 0.2f}, 1.0f / Length(Vec3{-0.3f, 1.0f, 0.2f}));
    const Vec3 origin = ScreenRayOrigin(kHand, kEye, direction);
    Require(Near(Dot(Sub(origin, kEye), direction), 0.0f), "level with the eye along the ray");
    const Vec3 fromHand = OnScreen(kHand, direction, kScreen);
    Require(Near(OnScreen(origin, direction, kScreen), fromHand), "the hand's own line, wherever it points");
}

void TestPastTheScreen()
{
    // The arm reached through the screen: the ray still starts on the near side.
    const Vec3 ahead{0.0f, 1.0f, 0.0f};
    const Vec3 hand{0.1f, 0.55f, -0.1f};
    const Vec3 origin = ScreenRayOrigin(hand, kEye, ahead);
    Require(origin.y < kScreen && Near(origin, Vec3{0.1f, 0.0f, -0.1f}), "a hand past the screen still points at it");
}

void TestNotAhead()
{
    // The hand level with the eyes, or behind them, along the ray: started there.
    const Vec3 ahead{0.0f, 1.0f, 0.0f};
    Require(Near(ScreenRayOrigin(Vec3{0.2f, 0.0f, -0.3f}, kEye, ahead), Vec3{0.2f, 0.0f, -0.3f}),
            "a hand level with the eye starts the ray");
    Require(Near(ScreenRayOrigin(Vec3{0.2f, -0.1f, -0.3f}, kEye, ahead), Vec3{0.2f, -0.1f, -0.3f}),
            "a hand behind the eye is not moved forward");
}

} // namespace

int main()
{
    TestStraightAhead();
    TestAngled();
    TestPastTheScreen();
    TestNotAhead();
    std::cout << "examine_ray: a screen pointed at along the hand's line, started level with the eye\n";
    return 0;
}
