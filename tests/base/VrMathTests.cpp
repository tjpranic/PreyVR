#include "base/VrMath.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

using preyvr::test::Require;

bool Near(float left, float right, float epsilon = 0.0001f)
{
    return std::fabs(left - right) <= epsilon;
}

void TestPoseComposition()
{
    constexpr float halfSqrtTwo = 0.70710678118f;
    const preyvr::Pose parent{
        {0.0f, 0.0f, halfSqrtTwo, halfSqrtTwo},
        {10.0f, 20.0f, 30.0f},
    };
    const preyvr::Pose child{{}, {2.0f, 0.0f, 0.0f}};
    const preyvr::Pose result = preyvr::Compose(parent, child);

    Require(Near(result.position.x, 10.0f), "parent rotation owns child X");
    Require(Near(result.position.y, 22.0f), "child offset rotates into parent Y");
    Require(Near(result.position.z, 30.0f), "pose composition preserves Z");
}

void TestQuaternionFailClosed()
{
    const preyvr::Quaternion normalized = preyvr::Normalize({0.0f, 0.0f, 0.0f, 0.0f});
    Require(Near(normalized.x, 0.0f) && Near(normalized.y, 0.0f) &&
            Near(normalized.z, 0.0f) && Near(normalized.w, 1.0f),
        "zero quaternion falls back to identity");
}

void TestPoseValidity()
{
    preyvr::Pose pose{};
    preyvr::PoseValidity validity{true, true, true, true, 5'000'000};
    Require(preyvr::IsPoseUsable(pose, validity, 10'000'000), "fresh tracked pose is usable");

    validity.ageNanoseconds = 15'000'000;
    Require(!preyvr::IsPoseUsable(pose, validity, 10'000'000), "stale pose fails closed");

    validity.ageNanoseconds = 0;
    pose.orientation = {0.0f, 0.0f, 0.0f, 0.0f};
    Require(!preyvr::IsPoseUsable(pose, validity, 10'000'000),
        "zero-length tracked orientation fails closed");

    pose.orientation = {};
    pose.position.x = std::numeric_limits<float>::quiet_NaN();
    Require(!preyvr::IsPoseUsable(pose, validity, 10'000'000), "non-finite pose fails closed");
}

} // namespace

int main()
{
    TestPoseComposition();
    TestQuaternionFailClosed();
    TestPoseValidity();
    std::cout << "PreyVR math tests passed\n";
    return 0;
}
