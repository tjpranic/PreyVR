// A button that leaves a menu must not act in play (ReleaseGate): right B is
// Back in the inventory and crouch in play.
#include "preyvr/ReleaseGate.h"

#include <cstdlib>
#include <iostream>

using preyvr::input::PlayResume;
using preyvr::input::ReleaseGate;

namespace {
void Check(bool ok, const char* why)
{
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
} // namespace

int main()
{
    {
        // The race: B is first seen on the frame the menu reads as closed.
        PlayResume resume;
        ReleaseGate b;
        Check(!b.Update(false, resume.Update(false)), "menu up, B up");
        Check(!b.Update(true, resume.Update(true)), "B first seen as the menu closes: no crouch");
        for (int i = 0; i < 20; ++i) { Check(!b.Update(true, resume.Update(true)), "still held: still no crouch"); }
        Check(!b.Update(false, resume.Update(true)), "let go");
        Check(b.Update(true, resume.Update(true)), "the next press crouches");
    }
    {
        // B held while the menu is still up (the close lands a frame later).
        PlayResume resume;
        ReleaseGate b;
        resume.Update(false);
        Check(!b.Update(true, resume.Update(false)), "held in the menu");
        Check(!b.Update(true, resume.Update(true)), "held as play resumes");
        Check(!b.Update(true, resume.Update(true)), "held in play");
        b.Update(false, resume.Update(true));
        Check(b.Update(true, resume.Update(true)), "a fresh press acts");
    }
    {
        // Play that never left a menu is untouched.
        PlayResume resume;
        ReleaseGate b;
        resume.Update(true);
        Check(b.Update(true, resume.Update(true)), "a press in play acts at once");
        Check(b.Update(true, resume.Update(true)), "and holds");
        Check(!b.Update(false, resume.Update(true)), "and lets go");
    }
    {
        // A press released on the closing frame leaves nothing behind.
        PlayResume resume;
        ReleaseGate b;
        resume.Update(false);
        Check(!b.Update(false, resume.Update(true)), "up as the menu closes");
        Check(b.Update(true, resume.Update(true)), "the first press in play acts");
    }
    {
        PlayResume resume;
        Check(resume.Update(true), "the first frame ever is the menu's");
        Check(!resume.Update(true), "then play");
        Check(resume.Update(false) && resume.Update(true) && !resume.Update(true), "one frame of grace per return");
    }
    std::cout << "release gate: a button that closes a menu waits for its release\n";
    return 0;
}
