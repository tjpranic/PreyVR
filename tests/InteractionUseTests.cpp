#include "preyvr/InteractionUse.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::use;

namespace {

using preyvr::test::Require;
bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

ButtonInput Base()
{
    ButtonInput in{};
    in.gameplay = true;
    in.leftHand = true;
    return in;
}

// A hand acts only on a press after it has been seen released, so every test
// starts with one frame of both grips open.
UseButton Ready()
{
    UseButton b;
    b.Update(Base());
    return b;
}

void TestLeftUse()
{
    UseButton b = Ready();
    auto in = Base();
    in.target = true;
    Require(!b.Update(in).down, "idle");
    in.leftPress = true;
    auto out = b.Update(in);
    Require(out.down && out.pressed && out.owner == Owner::Use, "left press on a target holds use");
    Require(!out.suppressTarget, "use never suppresses the selection");
    // The target goes away (an item picked up, a carry started): still held.
    in.target = false;
    for (int i = 0; i < 30; ++i) { Require(b.Update(in).down, "held while the grip is"); }
    in.leftPress = false;
    out = b.Update(in);
    Require(!out.down && out.released && out.owner == Owner::None, "released with the grip");
}

void TestLeftNoTarget()
{
    // Prey's own X: with nothing selected the press still goes to the game,
    // which reloads.
    UseButton b = Ready();
    auto in = Base();
    in.leftPress = true;
    auto out = b.Update(in);
    Require(out.down && out.pressed && !out.noTarget && out.owner == Owner::Use,
            "left press on nothing is Prey's X (a reload)");
    Require(!out.suppressTarget, "and leaves the selection alone");
    for (int i = 0; i < 10; ++i) { Require(b.Update(in).down, "held while the button is"); }
    in.leftPress = false;
    out = b.Update(in);
    Require(!out.down && out.released && out.owner == Owner::None, "let go with the button");
}

void TestLeftBusy()
{
    UseButton b = Ready();
    auto in = Base();
    in.target = true;
    in.leftBusy = true;
    in.leftPress = true;
    auto out = b.Update(in);
    Require(!out.down && out.busy, "a medkit or foregrip squeeze is not a use");
    in.leftBusy = false;
    Require(!b.Update(in).down, "still the same squeeze");
    // Busy arriving mid-hold (the foregrip latched) releases.
    in.leftPress = false;
    b.Update(in);
    in.leftPress = true;
    Require(b.Update(in).down, "fresh press");
    in.leftBusy = true;
    out = b.Update(in);
    Require(!out.down && out.released, "busy mid-hold releases");
}

// Y held, then the grips: the recentre chord, which neither uses nor reloads,
// and which lets nothing go that a hand already holds.
void TestChord()
{
    UseButton b = Ready();
    auto in = Base();
    in.target = true;
    in.chord = true;
    in.leftPress = true;
    auto out = b.Update(in);
    Require(!out.down && out.busy, "a left squeeze with Y held is the chord's, not a use");
    in.rightPress = true;
    out = b.Update(in);
    Require(!out.down && out.owner == Owner::None, "nor is the right one a reload");
    in = Base();
    b.Update(in);
    in.rightPress = true;
    in.chord = true;
    for (int i = 0; i < 10; ++i) { Require(!b.Update(in).down, "a right squeeze with Y held never reloads"); }
    in = Base();
    b.Update(in);
    in.carrying = true;
    in.leftPress = true;
    in.chord = true;
    Require(!b.Update(in).releaseCarry, "a squeeze with Y held does not drop what is carried");
    // A hold already under way stays held when Y goes down.
    in = Base();
    b.Update(in);
    in.target = true;
    in.leftPress = true;
    Require(b.Update(in).down, "a fresh press uses");
    in.chord = true;
    Require(b.Update(in).down, "Y does not let a held use go");
    in = Base();
    b.Update(in);
    in.leftHand = false;
    in.rightPress = true;
    in.chord = true;
    Require(!b.Update(in).down, "nor does the original layout's right grip press with Y held");
}

// Just after a reload the selection is held empty for a few frames; a left
// press then sends nothing, so it cannot land on whatever comes back.
void TestReloadGuard()
{
    UseButton b = Ready();
    auto in = Base();
    in.rightPress = true;
    ButtonOutput out{};
    for (int i = 0; i <= UseButton::kClearFrames; ++i) { out = b.Update(in); }
    Require(out.down && out.owner == Owner::Reload, "a reload");
    in.rightPress = false;
    b.Update(in);
    in.target = true;
    in.leftPress = true;
    out = b.Update(in);
    Require(!out.down && out.noTarget, "a press under the reload's guard sends nothing");
}

void TestRightReload()
{
    UseButton b = Ready();
    auto in = Base();
    in.target = true;   // the left hand points at a can
    in.rightPress = true;
    auto out = b.Update(in);
    Require(!out.down && out.suppressTarget && out.owner == Owner::Reload, "reload first clears the selection");
    // The selector still reports the old pick this frame.
    out = b.Update(in);
    Require(!out.down && out.suppressTarget, "waits while a target is still selected");
    in.target = false;
    out = b.Update(in);
    Require(!out.down, "one clear frame is not enough");
    out = b.Update(in);
    Require(out.down && out.pressed && out.suppressTarget, "X goes down only on a clear selection");
    for (int i = 0; i < 10; ++i) { Require(b.Update(in).down, "held"); }
    in.rightPress = false;
    out = b.Update(in);
    Require(!out.down && out.released, "released");
    // The guard keeps the selection empty while the release lands.
    int suppressed = 0;
    for (int i = 0; i < 6; ++i) { suppressed += b.Update(in).suppressTarget ? 1 : 0; }
    Require(suppressed == UseButton::kGuardFrames, "guard frames after a reload");
}

void TestRightDrawsWhenHolstered()
{
    UseButton b = Ready();
    ButtonInput in = Base();
    in.holstered = true;
    in.rightPress = true;
    ButtonOutput out = b.Update(in);
    Require(out.drawWeapon && !out.down && out.owner == Owner::Draw, "holstered: the right grip draws");
    Require(!out.suppressTarget, "a draw leaves the selection alone");
    in.holstered = false;   // drawn
    for (int i = 0; i < 40; ++i) {
        out = b.Update(in);
        Require(!out.down && !out.drawWeapon, "held on after the draw: no X (it would holster again)");
    }
    in.rightPress = false;
    Require(b.Update(in).owner == Owner::None, "the grip opened");
    in.rightPress = true;
    out = b.Update(in);
    Require(out.owner == Owner::Reload && !out.drawWeapon, "drawn: the next press reloads");
    // Carrying wins: the grip is not a draw while something is held.
    UseButton c = Ready();
    ButtonInput carry = Base();
    carry.holstered = true;
    carry.carrying = true;
    carry.rightPress = true;
    out = c.Update(carry);
    Require(!out.drawWeapon, "no draw while carrying");
}

void TestReloadGiveUp()
{
    UseButton b = Ready();
    auto in = Base();
    in.target = true;
    in.rightPress = true;
    bool gaveUp = false, down = false;
    for (int i = 0; i < 40; ++i) {
        const auto out = b.Update(in);
        gaveUp = gaveUp || out.gaveUp;
        down = down || out.down;
    }
    Require(gaveUp && !down, "a target that never clears is never used by the right grip");
}

void TestRightBusyAndPairs()
{
    UseButton b = Ready();
    auto in = Base();
    in.rightBusy = true;
    in.rightPress = true;
    Require(b.Update(in).busy, "holster squeeze is not a reload");
    in.rightPress = false;
    in.rightBusy = false;
    b.Update(in);
    // Both grips at once (recentre, two-hand): neither acts.
    in.target = true;
    in.leftPress = in.rightPress = true;
    auto out = b.Update(in);
    Require(!out.down && out.owner == Owner::None, "paired squeeze does nothing");
}

void TestOtherHandIgnored()
{
    UseButton b = Ready();
    auto in = Base();
    in.target = true;
    in.leftPress = true;
    b.Update(in);
    in.rightPress = true;
    Require(b.Update(in).owner == Owner::Use, "right grip during a use is ignored");
    in.leftPress = false;
    auto out = b.Update(in);
    Require(out.released && !out.down, "use ends");
    Require(b.Update(in).owner == Owner::None, "the right grip is not re-armed until released");
}

void TestMenuReleases()
{
    UseButton b = Ready();
    auto in = Base();
    in.target = true;
    in.leftPress = true;
    Require(b.Update(in).down, "held");
    in.gameplay = false;
    auto out = b.Update(in);
    Require(!out.down && out.released, "a menu opening releases");
    in.gameplay = true;
    Require(!b.Update(in).down, "the grip held across the menu does not act");
}

void TestLegacy()
{
    UseButton b = Ready();
    auto in = Base();
    in.leftHand = false;
    in.leftPress = true;
    in.target = true;
    Require(!b.Update(in).down, "legacy: the left grip does nothing");
    in.leftPress = false;
    in.rightPress = true;
    auto out = b.Update(in);
    Require(out.down && out.owner == Owner::Legacy && !out.suppressTarget, "legacy: right grip is X");
    in.rightBusy = true;
    Require(!b.Update(in).down, "legacy: busy releases");
}

void TestStartsDisarmed()
{
    UseButton b;
    auto in = Base();
    in.target = true;
    in.leftPress = true;
    Require(!b.Update(in).down, "a grip already held when the lane starts does not act");
}

void TestOneEuro()
{
    // A still hand with tremor: the filtered value moves far less than the input.
    RayFilter f;
    FilterSettings s{};
    float lo = 1e9f, hi = -1e9f, worst = 0;
    for (int i = 0; i < 400; ++i) {
        const float jitter = (i % 2 ? 1.0f : -1.0f) * .007f;   // 0.4 degrees
        Ray raw{{0, 0, 0}, {jitter, 1, 0}};
        const float len = std::sqrt(1 + jitter * jitter);
        raw.direction = {jitter / len, 1 / len, 0};
        const auto out = f.Update(raw, 1.0f / 90.0f, s);
        if (i > 50) {
            const float deg = std::atan2(out.direction.x, out.direction.y) * 57.29578f;
            lo = std::fmin(lo, deg);
            hi = std::fmax(hi, deg);
            worst = std::fmax(worst, std::fabs(deg));
        }
    }
    Require(hi - lo < .05f, "a 0.4-degree tremor does not move the pointer");
    Require(worst <= s.backlashDegrees + .41f, "and it stays within the dead band of the hand");
    // A real sweep is followed: after a 10-degree turn at 100 deg/s the lag is small.
    f.Reset();
    Ray last{};
    for (int i = 0; i <= 90; ++i) {
        const float a = std::fmin(i * (100.0f / 90.0f), 10.0f) * 3.14159265f / 180.0f;
        last = f.Update({{0, 0, 0}, {std::sin(a), std::cos(a), 0}}, 1.0f / 90.0f, s);
    }
    Require(AngleDegrees(last.direction, {std::sin(.17453f), std::cos(.17453f), 0}) < s.backlashDegrees + .25f,
            "settles on a sweep, behind it by at most the dead band");
    // A jump restarts the filter instead of sliding.
    const auto jumped = f.Update({{0, 0, 0}, {1, 0, 0}}, 1.0f / 90.0f, s);
    Require(Near(jumped.direction.x, 1), "jump resets");
    s.enabled = false;
    const auto raw = f.Update({{1, 2, 3}, {0, 0, 1}}, 1.0f / 90.0f, s);
    Require(Near(raw.origin.y, 2) && Near(raw.direction.z, 1), "disabled passes through");
}

// A tremor aimed exactly at the edge between two targets (the mock case of
// 2026-10-06): the side of the edge the output is on must not change.
void TestBoundaryTremor()
{
    RayFilter f;
    FilterSettings s{};
    int flips = 0;
    bool side = false, primed = false;
    for (int i = 0; i < 900; ++i) {
        const float t = static_cast<float>(i) / 90.0f;
        // 0.4 degrees peak, physiological 9 Hz plus a slow 1.3 Hz drift.
        const float deg = .3f * std::sin(t * 56.5f) + .1f * std::sin(t * 8.2f);
        const float a = deg * 3.14159265f / 180.0f;
        const auto out = f.Update({{0, 0, 0}, {std::sin(a), std::cos(a), 0}}, 1.0f / 90.0f, s);
        const bool now = out.direction.x > 0;
        if (primed && now != side) { ++flips; }
        side = now;
        primed = true;
    }
    Require(flips <= 1, "no flips across an edge under a tremor inside the dead band");
}

void TestGeometry()
{
    Require(Near(AngleDegrees({0, 1, 0}, {1, 0, 0}), 90), "perpendicular");
    Require(Near(AngleDegrees({0, 10, 0}, {.001f, 10, 0}), .0057296f, 2e-5f), "small angle exact");
    const auto t = RayBoxEntry({0, 0, 0}, {0, 1, 0}, {-1, 2, -1}, {1, 4, 1});
    Require(t && Near(*t, 2), "entry distance");
    Require(!RayBoxEntry({0, 0, 0}, {0, 1, 0}, {2, 2, 2}, {3, 3, 3}), "miss");
    Require(!RayBoxEntry({0, 0, 0}, {0, -1, 0}, {-1, 2, -1}, {1, 4, 1}), "box behind");
    const auto inside = RayBoxEntry({0, 3, 0}, {0, 1, 0}, {-1, 2, -1}, {1, 4, 1});
    Require(inside && Near(*inside, 0), "origin inside");
    bool on = false;
    auto p = PointerEnd({0, 0, 0}, {0, 1, 0}, {-1, 2, -1}, {1, 4, 1}, &on);
    Require(on && Near(p.y, 2), "pointer ends on the face it enters");
    p = PointerEnd({0, 0, 0}, {0, 1, 0}, {.5f, 2, -.2f}, {.9f, 3, .2f}, &on);
    Require(!on && Near(p.x, .5f) && Near(p.y, 2.5f), "a cone pick bends to the nearest bounds point");
}

}  // namespace

int main()
{
    TestLeftUse();
    TestLeftNoTarget();
    TestLeftBusy();
    TestChord();
    TestReloadGuard();
    TestRightReload();
    TestRightDrawsWhenHolstered();
    TestReloadGiveUp();
    TestRightBusyAndPairs();
    TestOtherHandIgnored();
    TestMenuReleases();
    TestLegacy();
    TestStartsDisarmed();
    TestOneEuro();
    TestBoundaryTremor();
    TestGeometry();
    std::cout << "interaction use tests passed\n";
    return 0;
}
