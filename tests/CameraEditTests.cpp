#include "preyvr/CameraEdit.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using namespace preyvr::cameraedit;

using preyvr::test::Require;

bool Near(float left, float right, float epsilon = 1e-5f)
{
    return std::fabs(left - right) <= epsilon;
}

void Set(std::vector<std::uint8_t>& camera, std::size_t index, float value)
{
    std::memcpy(camera.data() + index * sizeof(float), &value, sizeof(float));
}

// Identity rotation with a recognisable translation, plus a marker late in the
// structure so a test can prove the edit does not scribble past the matrix.
std::vector<std::uint8_t> MakeCamera()
{
    std::vector<std::uint8_t> camera(kCameraSize, 0);
    Set(camera, 0, 1.0f);
    Set(camera, 5, 1.0f);
    Set(camera, 10, 1.0f);
    Set(camera, 3, 325.847f);
    Set(camera, 7, 740.602f);
    Set(camera, 11, 482.790f);
    camera[0x200] = 0xAB; // cached derived state lives out here
    camera[0x23F] = 0xCD;
    return camera;
}

// MakeCamera turned about Z (Z-up yaw) by `degrees`.
std::vector<std::uint8_t> MakeYawedCamera(float degrees)
{
    auto camera = MakeCamera();
    const float c = std::cos(degrees * 3.14159265358979323846f / 180.0f);
    const float s = std::sin(degrees * 3.14159265358979323846f / 180.0f);
    Set(camera, 0, c);
    Set(camera, 1, -s);
    Set(camera, 4, s);
    Set(camera, 5, c);
    return camera;
}

void TestPositionOf()
{
    const auto position = PositionOf(MakeCamera());
    Require(Near(position[0], 325.847f), "translation X is read from m[3]");
    Require(Near(position[1], 740.602f), "translation Y is read from m[7]");
    Require(Near(position[2], 482.790f), "translation Z is read from m[11]");
}

void TestRotationStaysOrthonormal()
{
    // The property that matters: UpdateFrustum negates all six frustum plane
    // normals when this check fails, so a denormalising rotation would invert
    // culling rather than merely look slightly wrong.
    auto camera = MakeCamera();
    Require(RotationIsOrthonormal(camera), "the identity rotation is orthonormal");

    for (const float degrees : {-15.0f, -7.5f, -1.0f, 1.0f, 7.5f, 15.0f}) {
        const auto rotated = MakeYawedCamera(degrees);
        Require(RotationIsOrthonormal(rotated),
            "a yawed rotation is still orthonormal, so culling will not invert");
    }
}

void TestNonOrthonormalIsDetected()
{
    auto scaled = MakeCamera();
    Set(scaled, 0, 2.0f); // scale one axis: no longer a rotation
    Require(!RotationIsOrthonormal(scaled), "a scaled basis is rejected");

    auto mirrored = MakeCamera();
    Set(mirrored, 10, -1.0f); // flip handedness
    Require(!RotationIsOrthonormal(mirrored), "a mirrored basis is rejected");

    // A finding worth pinning rather than papering over: the engine's own check
    // **passes** an all-zero matrix, because each of its nine comparisons
    // reduces to |0 - 0|. That is why orthonormality alone is not the gate.
    std::vector<std::uint8_t> zeroed(kCameraSize, 0);
    Require(RotationIsOrthonormal(zeroed),
        "the engine predicate accepts an all-zero matrix -- this is its hole, not ours");
    Require(RotationIsDegenerate(zeroed), "the degeneracy check catches an all-zero matrix");
    Require(!RotationIsSafeToWrite(zeroed), "an all-zero matrix is not safe to write");

    const auto camera = MakeCamera();
    Require(!RotationIsDegenerate(camera), "the identity is not degenerate");
    Require(RotationIsSafeToWrite(camera), "the identity is safe to write");
    Require(RotationIsSafeToWrite(MakeYawedCamera(12.0f)), "a yawed identity is still safe to write");

    auto nonFinite = MakeCamera();
    Set(nonFinite, 5, std::nanf(""));
    Require(RotationIsDegenerate(nonFinite), "a non-finite basis is degenerate");
    Require(!RotationIsSafeToWrite(nonFinite), "a non-finite basis is not safe to write");
}

void TestRestoreIsByteExact()
{
    auto camera = MakeCamera();
    RestorePoint point{};
    Require(Capture(camera, point), "a full camera is captured");
    Require(point.captured, "the restore point records that it captured");
    Require(MatchesRestorePoint(camera, point), "an untouched camera still matches");

    Set(camera, 0, 0.5f);
    Require(!MatchesRestorePoint(camera, point), "an edited camera no longer matches");

    std::memcpy(camera.data(), point.bytes.data(), kCameraSize);
    Require(MatchesRestorePoint(camera, point), "restoring the bytes matches again");

    // A single flipped byte anywhere must fail: a partial restore is the failure
    // mode that would poison every later measurement in the same session.
    camera[0x1FF] ^= 0x01;
    Require(!MatchesRestorePoint(camera, point),
        "one differing byte late in the structure fails the restore check");

    RestorePoint empty{};
    Require(!MatchesRestorePoint(camera, empty),
        "an uncaptured restore point never reports a match");
}

} // namespace

int main()
{
    TestPositionOf();
    TestRotationStaysOrthonormal();
    TestNonOrthonormalIsDetected();
    TestRestoreIsByteExact();
    std::cout << "PreyVR camera edit tests passed\n";
    return 0;
}
