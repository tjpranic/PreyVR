// Whether Prey's fullscreen video draw clears and letterboxes
// (render/stereo/VideoLetterbox.h), against its own test at 0xEFAA2C..0xEFAA6B: the
// copy into the held first eye is made only when it does.
#include "render/stereo/FullscreenVideoNative.h"
#include "engine/NativeContract.h"
#include "render/stereo/VideoLetterbox.h"
#include "TestSupport.h"

#include <cstdint>
#include <iostream>

using preyvr::test::Require;
using preyvr::video::Letterboxed;
namespace vn = preyvr::video::native;

int main()
{
    // Every Prey video is 1920x1080 but the Psychotronics screens'.
    Require(Letterboxed(2644, 2644, 1920, 1080), "a headset's square eye letterboxes the 16:9 video");
    Require(Letterboxed(2016, 2240, 1920, 1080), "and a taller-than-wide one");
    Require(Letterboxed(1440, 900, 1920, 1080), "16:10 letterboxes");
    Require(Letterboxed(1366, 768, 1920, 1080), "and an aspect only near 16:9");

    Require(!Letterboxed(1920, 1080, 1920, 1080), "the video's own size does not");
    Require(!Letterboxed(3840, 2160, 1920, 1080), "another size of the same aspect does not");
    Require(!Letterboxed(1280, 720, 1920, 1080), "nor a smaller one");
    Require(!Letterboxed(1920, 1080, 960, 540), "nor a 960x540 video on a 16:9 screen");

    // The draw's UCOMISS/JZ: unordered counts as the same aspect, an infinite one
    // does not. Through a volatile, so the zero is not a constant divisor.
    volatile std::int32_t zero = 0;
    Require(!Letterboxed(zero, zero, 1920, 1080), "a zero-size viewport (0/0) is not letterboxed");
    Require(Letterboxed(2644, zero, 1920, 1080), "a zero-height one (infinite aspect) is");
    Require(!Letterboxed(1920, 1080, zero, zero), "nor a zero-size video");

    // The hook calls both functions it names, so the gate checks both.
    static_assert(preyvr::contract::Gated(vn::Contracts, vn::FullscreenDraw));
    static_assert(preyvr::contract::Gated(vn::Contracts, vn::GetViewport));
    Require(sizeof(vn::Contracts) / sizeof(vn::Contracts[0]) == 2, "two whole-body rows");

    std::cout << "video_letterbox: letterboxed exactly when Prey's draw clears and letterboxes\n";
}
