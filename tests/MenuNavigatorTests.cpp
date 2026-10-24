#include "preyvr/MenuNavigator.h"
#include "TestSupport.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace preyvr::input;

using preyvr::test::Require;

unsigned int Step(MenuNavigator& nav, const ControllerMenuState& state, float dt,
                  MenuAction* out, unsigned int capacity = 8)
{
    return nav.Update(state, dt, out, capacity);
}

// **The bug this prevents.** A stick reporting 90 times a second, fed straight
// through, scrolls an entire list before a thumb comes back to centre.
void TestOneFlickIsOneAction()
{
    MenuNavigator nav;
    MenuAction out[8];
    ControllerMenuState pushed;
    pushed.stickY = 1.0f;

    Require(Step(nav, pushed, 0.011f, out) == 1, "a deflection produces one action");
    Require(out[0] == MenuAction::Up, "and it is the direction pushed");

    // Held, but not yet past the initial delay: silence.
    unsigned int extra = 0;
    for (int frame = 0; frame < 20; ++frame) {   // 20 * 11ms = 220ms < 450ms
        extra += Step(nav, pushed, 0.011f, out);
    }
    Require(extra == 0, "holding must not repeat before the initial delay");
}

void TestHoldingRepeatsAfterTheDelay()
{
    MenuNavigator nav;
    MenuAction out[8];
    ControllerMenuState pushed;
    pushed.stickY = -1.0f;

    Require(Step(nav, pushed, 0.0f, out) == 1, "the first push acts immediately");
    Require(out[0] == MenuAction::Down, "downward stick means Down");

    // Past the initial delay, repeats begin.
    unsigned int repeats = Step(nav, pushed, 0.5f, out);
    Require(repeats >= 1, "a repeat must fire once the initial delay has passed");
    Require(out[0] == MenuAction::Down, "and repeat the held direction");

    // A long frame must not swallow repeats that came due inside it.
    const unsigned int burst = Step(nav, pushed, 0.5f, out);
    Require(burst >= 2, "a long frame must deliver every repeat that fell inside it");
}

void TestHysteresisStopsChatter()
{
    // A stick resting between the release and engage thresholds must hold its
    // state rather than flickering, which with one threshold emits a stream of
    // actions nobody asked for.
    MenuNavigator nav;
    MenuAction out[8];
    ControllerMenuState pushed;
    pushed.stickY = 1.0f;
    Require(Step(nav, pushed, 0.0f, out) == 1, "engage");

    ControllerMenuState between;
    between.stickY = 0.45f;   // below engage 0.6, above release 0.35
    unsigned int fired = 0;
    for (int frame = 0; frame < 10; ++frame) {
        fired += Step(nav, between, 0.001f, out);
    }
    Require(fired == 0, "sitting between the thresholds must not re-trigger");

    ControllerMenuState centred;
    Require(Step(nav, centred, 0.001f, out) == 0, "releasing emits nothing");
    Require(Step(nav, pushed, 0.001f, out) == 1, "and the next push acts again");
}

void TestDiagonalPicksOneAxis()
{
    MenuNavigator nav;
    MenuAction out[8];
    ControllerMenuState diagonal;
    diagonal.stickX = 0.75f;
    diagonal.stickY = 0.70f;
    const unsigned int count = Step(nav, diagonal, 0.0f, out);
    Require(count == 1, "a diagonal must not fire two directions at once");
    Require(out[0] == MenuAction::Right, "the dominant axis wins");
}

void TestButtonsFireOnTheRisingEdgeOnly()
{
    MenuNavigator nav;
    MenuAction out[8];
    ControllerMenuState held;
    held.accept = true;

    Require(Step(nav, held, 0.016f, out) == 1, "a press acts");
    Require(out[0] == MenuAction::Accept, "and is Accept");
    unsigned int extra = 0;
    for (int frame = 0; frame < 30; ++frame) {
        extra += Step(nav, held, 0.016f, out);
    }
    Require(extra == 0, "a held button must not act once per frame");

    ControllerMenuState released;
    Require(Step(nav, released, 0.016f, out) == 0, "releasing emits nothing");
    Require(Step(nav, held, 0.016f, out) == 1, "and pressing again acts once more");
}

void TestNonFiniteInputIsIgnored()
{
    MenuNavigator nav;
    MenuAction out[8];
    ControllerMenuState bad;
    bad.stickX = std::nanf("");
    Require(Step(nav, bad, 0.016f, out) == 0, "a NaN stick must produce nothing");

    ControllerMenuState pushed;
    pushed.stickY = 1.0f;
    Require(Step(nav, pushed, std::nanf(""), out) == 1,
            "a NaN delta must not stop a real deflection being seen");
}

void TestResetTreatsAHeldButtonAsFresh()
{
    MenuNavigator nav;
    MenuAction out[8];
    ControllerMenuState held;
    held.accept = true;
    Step(nav, held, 0.016f, out);
    Step(nav, held, 0.016f, out);
    nav.Reset();
    // After a menu opens, a button already down is the player's first input.
    Require(Step(nav, held, 0.016f, out) == 1, "a reset makes a held button a fresh press");
}

} // namespace

int main()
{
    {
        ModalMenuRouter router;
        ModalMenuState s{};
        MenuAction out[8]{};
        s.start = true; s.stickY = 1; s.accept = true;
        Require(router.Update(s, false, .016f, out, 8) == 1 && out[0] == MenuAction::Start,
                "Start opens pause from gameplay without D-pad or A leaking");
        Require(router.Update(s, true, .016f, out, 8) == 0,
                "opening a menu with a held gameplay stick/button must not select");
        s = {}; router.Update(s, true, .016f, out, 8);
        s.accept = true; s.previousTab = true; s.tertiary = true;
        Require(router.Update(s, true, .016f, out, 8) == 1 && out[0] == MenuAction::Accept && router.TertiaryHeld(),
                "confirm acts on press and Y is held; a tab waits for release to tell a two-grip hold apart");
        Require(router.Update(s, true, .016f, out, 8) == 0, "held buttons do not repeat");
        s.previousTab=false;
        Require(router.Update(s,true,.016f,out,8)==1 && out[0]==MenuAction::PreviousTab,"single grip release changes tab");
        s.previousTab=true;router.Update(s,true,.016f,out,8);
        s.nextTab=true;
        Require(router.Update(s,true,.016f,out,8)==0,"staggered recenter chord never changes tabs");
        s.previousTab=false;router.Update(s,true,.016f,out,8);
        s.nextTab=false;
        Require(router.Update(s,true,.016f,out,8)==0,"chord releases never change tabs");
        s.previousPage=true;
        Require(router.Update(s,true,.016f,out,8)==0,"a page waits for release, like a tab");
        Require(router.Update(s,true,.016f,out,8)==0,"held page buttons do not repeat");
        s.previousPage=false;
        Require(router.Update(s,true,.016f,out,8)==1&&out[0]==MenuAction::PreviousPage,"and turns on release");
        s.nextPage=true;router.Update(s,true,.016f,out,8);s.nextPage=false;
        Require(router.Update(s,true,.016f,out,8)==1&&out[0]==MenuAction::NextPage,"either page");
        // The Steam Frame's pages are its grips: the recenter chord's two grips
        // must not turn a page, whichever is squeezed first.
        s.nextPage=true;s.bothGrips=false;router.Update(s,true,.016f,out,8);
        s.bothGrips=true;router.Update(s,true,.016f,out,8);
        s.nextPage=false;s.bothGrips=false;
        Require(router.Update(s,true,.016f,out,8)==0,"both grips held spoil a page");
        // And its tabs are the bumpers: both grips still spoil a tab held meanwhile.
        s.nextTab=true;router.Update(s,true,.016f,out,8);
        s.bothGrips=true;router.Update(s,true,.016f,out,8);
        s.nextTab=false;s.bothGrips=false;
        Require(router.Update(s,true,.016f,out,8)==0,"both grips held spoil a tab on another button");
        s.nextTab=true;router.Update(s,true,.016f,out,8);s.nextTab=false;
        Require(router.Update(s,true,.016f,out,8)==1&&out[0]==MenuAction::NextTab,"a lone bumper changes tab");
        // The stick clicks: Prey's LS and RS, on press, once each.
        s.leftStick=true;
        Require(router.Update(s,true,.016f,out,8)==1&&out[0]==MenuAction::LeftStick,"the left stick's click on press");
        Require(router.Update(s,true,.016f,out,8)==0,"once");
        s.leftStick=false;s.rightStick=true;
        Require(router.Update(s,true,.016f,out,8)==1&&out[0]==MenuAction::RightStick,"the right stick's click");
        s.rightStick=false;router.Update(s,true,.016f,out,8);
        Require(MenuActionKeyId(MenuAction::LeftStick)==kThumbL&&MenuActionKeyId(MenuAction::RightStick)==kThumbR,
                "as Prey's xi_thumbl (autosort) and xi_thumbr (drop, split)");
        s.stickX = 1;
        Require(router.Update(s, false, .016f, out, 8) == 0, "closing stops modal input");
        Require(router.Update(s, true, .016f, out, 8) == 0, "reopening requires neutral");
    }
    {
        // **X and Y, Prey's own actions, held** (MenuHold.h): no tap, down for
        // as long as the button is, since Prey's menus bind their holds.
        ModalMenuRouter router;
        ModalMenuState s{};
        MenuAction out[8]{};
        router.Update(s, true, .016f, out, 8);
        router.Update(s, true, .016f, out, 8);
        s.secondary = true;
        Require(router.Update(s, true, .016f, out, 8) == 0 && router.SecondaryHeld() && !router.TertiaryHeld(),
                "X is held, not tapped");
        Require(router.Update(s, true, .016f, out, 8) == 0 && router.SecondaryHeld(), "for as long as it is down");
        s.secondary = false;
        Require(router.Update(s, true, .016f, out, 8) == 0 && !router.SecondaryHeld(), "and let go");
        s.tertiary = true;
        Require(router.Update(s, true, .016f, out, 8) == 0 && router.TertiaryHeld(), "Y likewise");
        Require(router.Update(s, false, .016f, out, 8) == 0 && !router.TertiaryHeld(), "Y held as a menu closes is let go");
        Require(router.Update(s, true, .016f, out, 8) == 0 && !router.TertiaryHeld(),
                "and is no Y in the next menu until let go");
        s.tertiary = false; router.Update(s, true, .016f, out, 8);
        s.tertiary = true;
        Require(router.Update(s, true, .016f, out, 8) == 0 && router.TertiaryHeld(), "a fresh Y is");
        // VR Options resets the router while it has the controllers.
        router.Reset();
        Require(router.Update(s, true, .016f, out, 8) == 0 && !router.TertiaryHeld(),
                "a reset lets Y go until it is let go");
    }
    {
        // **Exit, the Steam Frame's View**: the gamepad's Back, on press. The
        // View that opened the TranScribe in play is still down as it opens,
        // so that press cannot close it again.
        ModalMenuRouter router;
        ModalMenuState s{};
        MenuAction out[8]{};
        s.exit = true;
        router.Update(s, false, .016f, out, 8);
        Require(router.Update(s, true, .016f, out, 8) == 0, "the View that opened the menu does not close it");
        s.exit = false; router.Update(s, true, .016f, out, 8);
        s.exit = true;
        Require(router.Update(s, true, .016f, out, 8) == 1 && out[0] == MenuAction::Exit, "a fresh View closes it");
        Require(router.Update(s, true, .016f, out, 8) == 0, "once");
        Require(MenuActionKeyId(MenuAction::Exit) == kBack, "as the gamepad's Back, Prey's menu_exit");
    }
    TestOneFlickIsOneAction();
    TestHoldingRepeatsAfterTheDelay();
    TestHysteresisStopsChatter();
    TestDiagonalPicksOneAxis();
    TestButtonsFireOnTheRisingEdgeOnly();
    TestNonFiniteInputIsIgnored();
    TestResetTreatsAHeldButtonAsFresh();
    std::cout << "menu navigator tests passed\n";
    return 0;
}
