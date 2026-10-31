#include "controls/menus/MenuHold.h"
#include "TestSupport.h"

#include <iostream>

namespace {

using namespace preyvr::input;
using preyvr::test::Require;

constexpr std::uint64_t kMenu = 6;

// One game frame: the mirror's event, posted.
unsigned Frame(MenuHoldMirror& mirror, const MenuHoldSample& sample, bool fresh = true,
               std::uint64_t menu = kMenu, bool modal = true)
{
    const unsigned state = mirror.Next(sample, fresh, menu, modal);
    mirror.Delivered(state);
    return state;
}

// **The recycler's transfer all.** Prey runs menu_take_all only on hold: the
// press, then a held event every frame, then the release when let go.
void TestAHoldIsPressedHeldAndReleased()
{
    MenuHoldMirror mirror;
    MenuHoldSample x;
    Require(Frame(mirror, x) == 0, "nothing down, nothing posted");
    x.Note(true, kMenu);
    Require(Frame(mirror, x) == kStatePressed, "the press");
    for (int frame = 0; frame < 90; ++frame) {
        Require(Frame(mirror, x) == kStateDown, "held every frame, as a pad's is");
    }
    Require(mirror.Frames() == 90, "counted");
    x.Note(false, kMenu);
    Require(Frame(mirror, x) == kStateReleased && !mirror.Down(), "released when let go");
    Require(Frame(mirror, x) == 0, "once");
}

// A tap shorter than a game frame: the frame service saw it, the drain did
// not. It still lands, as a press and a release on separate frames.
void TestATapBetweenGameFramesLands()
{
    MenuHoldMirror mirror;
    MenuHoldSample x;
    x.Note(true, kMenu);
    x.Note(false, kMenu);
    Require(Frame(mirror, x) == kStatePressed, "the press is not lost");
    Require(Frame(mirror, x) == kStateReleased, "and is released on the next frame");
    Require(Frame(mirror, x) == 0, "once");
}

void TestLetGoAndPressedAgainBetweenGameFramesIsTwoPresses()
{
    MenuHoldMirror mirror;
    MenuHoldSample x;
    x.Note(true, kMenu);
    Frame(mirror, x);
    Frame(mirror, x);
    x.Note(false, kMenu);
    x.Note(true, kMenu);
    Require(Frame(mirror, x) == kStateReleased, "the first press ends");
    Require(Frame(mirror, x) == kStatePressed, "and the second begins");
    Require(Frame(mirror, x) == kStateDown, "and is held");
}

// A hold never leaks into play or the next menu.
void TestTheMenuLetsGo()
{
    MenuHoldMirror mirror;
    MenuHoldSample x;
    x.Note(true, kMenu);
    Frame(mirror, x);
    Require(Frame(mirror, x, true, kMenu, false) == kStateReleased, "a menu closing lets go");
    Require(Frame(mirror, x, true, kMenu + 1, true) == 0, "still held in the next menu: not pressed");
    x.Note(false, kMenu + 1);
    x.Note(true, kMenu + 1);
    Require(Frame(mirror, x, true, kMenu + 1, true) == kStatePressed, "a fresh press in it is");
    Require(Frame(mirror, x, true, kMenu + 2, true) == kStateReleased, "a menu changing under it lets go");
    Require(Frame(mirror, x, true, kMenu + 2, true) == 0, "and does not press again");

    MenuHoldMirror play;
    MenuHoldSample y;
    y.Note(true, kMenu);
    Require(Frame(play, y, true, kMenu, false) == 0, "no press without a modal menu");
}

void TestStaleSamplesLetGo()
{
    MenuHoldMirror mirror;
    MenuHoldSample x;
    x.Note(true, kMenu);
    Require(Frame(mirror, x, false) == 0, "a stale sample presses nothing");
    Require(Frame(mirror, x) == kStatePressed, "a fresh one does");
    Require(Frame(mirror, x, false) == kStateReleased, "and the XR input stopping lets go");
    Require(Frame(mirror, x, true) == 0, "the same press is not pressed again");
}

void TestAPressFromAnotherMenuIsIgnored()
{
    MenuHoldMirror mirror;
    MenuHoldSample x;
    x.Note(true, kMenu - 1);
    Require(Frame(mirror, x) == 0, "held from an older menu");
    x.Note(false, kMenu - 1);
    x.Note(true, kMenu - 1);
    x.Note(false, kMenu - 1);
    Require(Frame(mirror, x) == 0, "or pressed in one");
}

// The post refused: nothing changed in the engine, so try again.
void TestARefusedPostIsRetried()
{
    MenuHoldMirror mirror;
    MenuHoldSample x;
    x.Note(true, kMenu);
    Require(mirror.Next(x, true, kMenu, true) == kStatePressed && !mirror.Down(), "a press not delivered");
    Require(Frame(mirror, x) == kStatePressed, "is made on the next frame");
    x.Note(false, kMenu);
    Require(mirror.Next(x, true, kMenu, true) == kStateReleased && mirror.Down(), "a release not delivered");
    Require(Frame(mirror, x) == kStateReleased && !mirror.Down(), "is made on the next");
}

void TestTheSampleCountsPressesInTheirMenu()
{
    MenuHoldSample x;
    x.Note(false, kMenu);
    Require(x.presses == 0 && x.scope == 0, "nothing pressed");
    x.Note(true, kMenu);
    x.Note(true, kMenu + 1);
    Require(x.presses == 1 && x.scope == kMenu && x.held, "one press, in the menu it was made in");
    x.Note(false, kMenu + 1);
    x.Note(true, kMenu + 1);
    Require(x.presses == 2 && x.scope == kMenu + 1, "a second press, in its own");
}

} // namespace

int main()
{
    TestAHoldIsPressedHeldAndReleased();
    TestATapBetweenGameFramesLands();
    TestLetGoAndPressedAgainBetweenGameFramesIsTwoPresses();
    TestTheMenuLetsGo();
    TestStaleSamplesLetGo();
    TestAPressFromAnotherMenuIsIgnored();
    TestARefusedPostIsRetried();
    TestTheSampleCountsPressesInTheirMenu();
    std::cout << "menu hold tests passed\n";
    return 0;
}
