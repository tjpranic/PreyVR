#pragma once

namespace preyvr::input {

// **A button down when play resumes waits for its release.** Right B is crouch
// in play and Back in menus, so the B that closes the inventory must not also
// crouch. Watching "held while a menu is open" is not enough: the main thread
// posts the queued Back (the inventory closes), samples the menu state (closed)
// and only then runs the action lanes, so the lanes first see B on a frame that
// already reads as gameplay. The frame the menu closes therefore still counts
// as the menu's.
class PlayResume {
public:
    // Once per frame. True while input still belongs to a menu: the menu is
    // up, or this is the first frame after it went (and the first frame ever).
    bool Update(bool gameplay)
    {
        const bool menu = !gameplay || !was_;
        was_ = gameplay;
        return menu;
    }

private:
    bool was_ = false;
};

// One button through PlayResume: down on a menu frame, it reads as up until it
// has been let go.
class ReleaseGate {
public:
    bool Update(bool down, bool menu)
    {
        if (!down) { blocked_ = false; }
        else if (menu) { blocked_ = true; }
        return down && !blocked_;
    }

private:
    bool blocked_ = false;
};

} // namespace preyvr::input
