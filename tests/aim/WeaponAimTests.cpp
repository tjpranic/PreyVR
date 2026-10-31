#include "aim/WeaponAim.h"
#include "TestGeometry.h"
#include "TestSupport.h"

#include <cmath>
#include <utility>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::aim;

namespace {

using preyvr::test::Require;

bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

using preyvr::test::AxisAngle;

Sample FreshSample(std::uint64_t equip, std::uint64_t reference, std::uint64_t published)
{
    Sample s;
    s.origin = Vec3{1.0f, 2.0f, 3.0f};
    s.direction = Vec3{0.0f, 1.0f, 0.0f};
    s.confidence = Confidence::barrel;
    s.equipGeneration = equip;
    s.referenceGeneration = reference;
    s.publishedNs = published;
    return s;
}

// Every reason a sample must be refused, each on its own, so a future change
// that drops one of the checks fails here rather than in a headset.
void TestUsableRefusesStaleSamples()
{
    const std::uint64_t now = 1'000'000'000ull;
    Require(Usable(FreshSample(7, 3, now - 1000), 7, 3, now), "a fresh matching sample is usable");

    Require(!Usable(FreshSample(6, 3, now - 1000), 7, 3, now),
            "a weapon change invalidates the whole sample, not just its rotation");
    Require(!Usable(FreshSample(7, 2, now - 1000), 7, 3, now),
            "a recentre invalidates a world direction computed in the old frame");
    Require(!Usable(FreshSample(7, 3, now - 500'000'000ull), 7, 3, now),
            "an old sample is refused");
    Require(!Usable(FreshSample(7, 3, now + 1000), 7, 3, now),
            "a sample from the future is refused rather than treated as fresh");
    Require(!Usable(FreshSample(7, 3, 0), 7, 3, now), "an unpublished sample is refused");

    Sample noPose = FreshSample(7, 3, now - 1000);
    noPose.confidence = Confidence::none;
    Require(!Usable(noPose, 7, 3, now), "a sample with no pose is refused");

    Sample nan = FreshSample(7, 3, now - 1000);
    nan.direction.x = std::nanf("");
    Require(!Usable(nan, 7, 3, now), "a non-finite direction is refused");
}

// Without a calibration the sample is honest about being the pointing axis
// rather than the barrel, so a consumer cannot silently treat them as the same.
void TestComposeReportsWhatItKnows()
{
    Pose grip;
    grip.position = Vec3{1.0f, 0.0f, 2.0f};
    grip.orientation = Quaternion{};

    const Sample raw = Compose(grip, Quaternion{}, false);
    Require(raw.confidence == Confidence::origin,
            "without a calibration this is an origin and a pointing axis, not a barrel");
    Require(Near(raw.direction.y, 1.0f), "identity grip points along engine forward");
    Require(Near(raw.origin.x, 1.0f) && Near(raw.origin.z, 2.0f), "the origin is the grip");

    // A 90 degree yaw in the grip-to-barrel offset must rotate the direction.
    const Sample turned = Compose(grip, AxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 3.14159265f * 0.5f), true);
    Require(turned.confidence == Confidence::barrel, "a calibrated sample says so");
    Require(Near(turned.direction.x, -1.0f, 1e-3f) && Near(turned.direction.y, 0.0f, 1e-3f),
            "the grip-to-barrel rotation composes on the right");
}

// **The HUD canvas conversion, pinned to numbers measured in the game.** R-118:
// at 2688x2880 the reticle sprite was photographed at three head angles and its
// pixel centre read off the captured frame. Those measurements, not a derivation,
// are what this test defends.
void TestHudCanvasMatchesTheMeasuredSprite()
{
    // Frame pixels the sprite was actually drawn at, against the viewport
    // fraction the mod had computed for it.
    struct Sample { float viewport; float measuredPx; };
    const Sample samples[] = {
        {0.39486f,  800.0f},
        {0.50090f, 1340.0f},
        {0.60514f, 1898.0f},
    };
    const float frameW = 2688.0f;
    const float frameH = 2880.0f;
    // Cover-fitted 16:9 on this frame: height sets the scale, width overflows.
    const float canvasW = frameH * 16.0f / 9.0f;   // 5120
    const float offset = (canvasW - frameW) * 0.5f; // 1216

    for (const Sample& s : samples) {
        // What the movie DID with the uncorrected fraction, under the model.
        const float drawn = s.viewport * canvasW - offset;
        Require(std::fabs(drawn - s.measuredPx) < 25.0f,
                "the cover-canvas model reproduces where the sprite was measured");

        // And the correction sends it where it was meant to go.
        const CanvasPoint fixed = ViewportToHudCanvas(s.viewport, 0.5f, frameW, frameH);
        const float corrected = fixed.x * canvasW - offset;
        Require(std::fabs(corrected - s.viewport * frameW) < 1.0f,
                "the corrected fraction lands on the intended viewport pixel");
    }

    // The uncorrected error is large enough to matter: hundreds of pixels at the
    // edges. A test that only checked the centre would pass on broken code.
    const CanvasPoint edge = ViewportToHudCanvas(0.39486f, 0.5f, frameW, frameH);
    Require(std::fabs(edge.x - 0.39486f) > 0.04f,
            "the correction is a real change away from centre, not a rounding tweak");
}

// At 16:9 the canvas and the frame coincide, so the conversion must be the
// identity -- measured to be true, since the same three angles landed on the
// naive prediction exactly at 2560x1440.
void TestHudCanvasIsIdentityAtSixteenNine()
{
    for (const float f : {0.0f, 0.25f, 0.39486f, 0.5f, 0.60514f, 1.0f}) {
        const CanvasPoint p = ViewportToHudCanvas(f, f, 2560.0f, 1440.0f);
        Require(Near(p.x, f, 1e-5f) && Near(p.y, f, 1e-5f),
                "16:9 needs no correction in either axis");
    }
    // Any 16:9 size, not just that one.
    const CanvasPoint q = ViewportToHudCanvas(0.2f, 0.8f, 1920.0f, 1080.0f);
    Require(Near(q.x, 0.2f, 1e-5f) && Near(q.y, 0.8f, 1e-5f), "1920x1080 likewise");
}

// A frame WIDER than 16:9 overflows the other way, so the correction must move
// to Y and leave X alone. Untested in game -- recorded here as the symmetry the
// implementation claims, so a future change cannot quietly break it.
void TestHudCanvasCorrectsTheOverflowingAxisOnly()
{
    const CanvasPoint tall = ViewportToHudCanvas(0.2f, 0.2f, 2688.0f, 2880.0f);
    Require(!Near(tall.x, 0.2f, 1e-4f), "a tall frame corrects X");
    Require(Near(tall.y, 0.2f, 1e-5f), "and leaves Y alone");

    const CanvasPoint wide = ViewportToHudCanvas(0.2f, 0.2f, 3840.0f, 1440.0f);
    Require(Near(wide.x, 0.2f, 1e-5f), "a wide frame leaves X alone");
    Require(!Near(wide.y, 0.2f, 1e-4f), "and corrects Y");

    // Centre is a fixed point at every aspect, which is why centre agreement
    // could never have discriminated this bug.
    for (const auto wh : {std::pair<float, float>{2688.0f, 2880.0f},
                          std::pair<float, float>{3840.0f, 1440.0f},
                          std::pair<float, float>{2560.0f, 1440.0f}}) {
        const CanvasPoint c = ViewportToHudCanvas(0.5f, 0.5f, wh.first, wh.second);
        Require(Near(c.x, 0.5f, 1e-6f) && Near(c.y, 0.5f, 1e-6f),
                "the centre never moves, at any aspect");
    }
}

// A degenerate frame size must pass the input through rather than invent one.
void TestHudCanvasRefusesBadSizes()
{
    for (const auto wh : {std::pair<float, float>{0.0f, 1440.0f},
                          std::pair<float, float>{2560.0f, 0.0f},
                          std::pair<float, float>{-1.0f, 1440.0f}}) {
        const CanvasPoint p = ViewportToHudCanvas(0.3f, 0.7f, wh.first, wh.second);
        Require(Near(p.x, 0.3f, 1e-6f) && Near(p.y, 0.7f, 1e-6f),
                "a bad frame size returns the input unchanged");
    }
}

} // namespace

int main()
{
    TestUsableRefusesStaleSamples();
    TestComposeReportsWhatItKnows();
    TestHudCanvasMatchesTheMeasuredSprite();
    TestHudCanvasIsIdentityAtSixteenNine();
    TestHudCanvasCorrectsTheOverflowingAxisOnly();
    TestHudCanvasRefusesBadSizes();
    std::cout << "PreyVR weapon aim tests passed\n";
    return 0;
}
