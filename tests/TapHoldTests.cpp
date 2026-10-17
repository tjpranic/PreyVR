// One button, two meanings by duration (TapHold): the mod's Y, which swaps
// weapon on a tap and opens the weapon wheel on a hold.
#include "preyvr/TapHold.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using preyvr::input::TapHold;

namespace {
void Check(bool ok, const char* why)
{
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
} // namespace

int main()
{
    constexpr float frame = 0.011f;
    {
        TapHold y;
        Check(!y.Update(true, frame).tap, "nothing on the press");
        for (int i = 0; i < 10; ++i) { Check(!y.Update(true, frame).hold, "a short press is not a hold"); }
        const auto out = y.Update(false, frame);
        Check(out.tap && !out.hold, "let go in time: a tap");
        Check(!y.Update(false, frame).tap, "one frame");
    }
    {
        TapHold y;
        bool held = false;
        for (int i = 0; i < 40; ++i) { held = y.Update(true, frame).hold; }
        Check(held, "held past the threshold: a hold");
        const auto out = y.Update(false, frame);
        Check(!out.tap && !out.hold, "a hold let go is no tap, and ends");
    }
    {
        TapHold y;
        y.Update(true, frame);
        y.Reset();
        for (int i = 0; i < 40; ++i) { Check(!y.Update(true, frame).hold, "a press held through a menu is ignored"); }
        Check(!y.Update(false, frame).tap, "and its release is no tap");
        y.Update(true, frame);
        Check(y.Update(false, frame).tap, "the next press is a press again");
    }
    {
        TapHold y;
        y.Update(true, frame);
        Check(!y.Update(true, std::nanf("")).hold && !y.Update(true, -1.0f).hold, "bad time steps add nothing");
        Check(y.Update(false, frame).tap, "so it is still a tap");
    }
    std::cout << "tap/hold: taps on a short release, holds past the threshold\n";
    return 0;
}
