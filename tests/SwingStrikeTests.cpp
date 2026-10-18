// The free-swing wrench's strike rules (phase 1): the head's box, how hard a
// strike is by speed, and one hit per target per pass.
#include "preyvr/SwingStrike.h"
#include "TestSupport.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
using namespace preyvr;
using namespace preyvr::swing;
using preyvr::test::Check;
namespace {
bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }
}
int main()
{
    // A wrench box along z: the handle 10 cm behind the grip, the head 34 cm ahead.
    Box box{};
    box.centre = {0, 0, .12f};
    box.axis = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
    box.half = {.06f, .06f, .22f};
    const Box head = HeadBox(box, {0, 0, 0});
    Check(Near(head.centre.z, .28f) && Near(head.half.z, .06f) && Near(head.half.x, .06f) && Near(head.centre.x, 0),
          "the head is the 12 cm of the long axis away from the grip");
    const Box other = HeadBox(box, {0, 0, .30f});
    Check(Near(other.centre.z, -.04f), "a grip at the other end puts the head at this one");
    const Box whole = HeadBox(box, {}, 1.0f);
    Check(Near(whole.half.z, .22f) && Near(whole.centre.z, .12f), "a head longer than the box is the box");

    StrikeSettings s{};
    Check(s.heavySpeed == 7.0f && s.fullSpeed == 10.0f, "heavy from 7 m/s, the whole bonus by 10 (PreyVR-36's strikes)");
    s.minSpeed = 1.2f;
    s.heavySpeed = 5.0f;
    s.fullSpeed = 7.5f;
    Check(!Classify(1.19f, .5f, s).strike, "below the minimum speed: a touch, not a strike");
    auto t = Classify(1.2f, .5f, s);
    Check(t.strike && !t.heavy && Near(t.scale, .5f), "at the minimum: a normal hit at half damage");
    t = Classify(3.1f, .5f, s);
    Check(t.strike && !t.heavy && Near(t.scale, .75f), "halfway to a heavy swing: three quarters");
    t = Classify(5.0f, .5f, s);
    Check(t.heavy && Near(t.scale, 1), "a heavy swing starts at Prey's charged hit, uncharged");
    t = Classify(6.25f, .5f, s);
    Check(t.heavy && Near(t.scale, 1.25f), "half the charge bonus halfway to the full speed");
    t = Classify(20.0f, .5f, s);
    Check(t.heavy && Near(t.scale, 1.5f), "never more than the whole charge bonus");
    t = Classify(7.5f, std::numeric_limits<float>::quiet_NaN(), s);
    Check(t.heavy && Near(t.scale, 1), "an unread bonus adds nothing");
    Check(!Classify(std::numeric_limits<float>::quiet_NaN(), .5f, s).strike, "an unknown speed never strikes");

    Check(Near(Approach({2, 0, 0}, {-1, 0, 0}), 2) && Near(Approach({0, 3, 0}, {-1, 0, 0}), 0) &&
          Approach({-1, 0, 0}, {-1, 0, 0}) < 0, "approach is the speed into the surface");

    Passes passes;
    int a = 0, b = 0;
    bool begun = false;
    constexpr std::uint64_t ms = 1000000ull;
    auto* p = passes.Touch(&a, 1000 * ms, .15f, begun);
    Check(p && begun && !p->struck, "a first touch begins a pass");
    p->struck = true;
    p = passes.Touch(&a, 1100 * ms, .15f, begun);
    Check(p && !begun && p->struck, "still touching within the lock: the same pass, still struck");
    p = passes.Touch(&a, 1240 * ms, .15f, begun);
    Check(p && !begun && p->struck, "the lock counts from the last touch");
    p = passes.Touch(&a, 1400 * ms, .15f, begun);
    Check(p && begun && !p->struck, "off it for longer than the lock: a new pass");
    passes.Touch(&b, 1400 * ms, .15f, begun);
    Check(begun && passes.Active() == 2, "each target its own pass");
    passes.Expire(1500 * ms, .15f);
    Check(passes.Active() == 2, "passes touched within the lock stay");
    passes.Expire(1600 * ms, .15f);
    Check(passes.Active() == 0, "and end once the wrench has been off them for the lock");
    int many[Passes::kCapacity + 1]{};
    for (unsigned i = 0; i < Passes::kCapacity; ++i) { Check(passes.Touch(&many[i], 2000 * ms, .15f, begun) != nullptr, "room"); }
    Check(passes.Touch(&many[Passes::kCapacity], 2000 * ms, .15f, begun) == nullptr, "a full table refuses a new pass");
    Check(passes.Touch(&many[Passes::kCapacity], 2200 * ms, .15f, begun) != nullptr && begun,
          "an ended pass makes room");
    Check(std::string(DecisionName(Decision::strike)) == "would_strike", "phase 1 names its strikes as such");
    std::cout << "swing_strike: head, tiers and passes\n";
}
