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

// **A menu takes the sticks only once it has been up for `frames` frames.** The
// sticks wait for a centred stick after a menu, so a menu flag that read "open"
// for a single frame used to stop the player until the stick was let go
// (docs/MOVE-STOP-2026-10-24.md). While the flag has been up for fewer frames
// the sticks stay play's, and the lanes hold what they last sent.
class MenuDebounce {
public:
    explicit MenuDebounce(unsigned frames = 2) : frames_(frames) {}

    // Once per frame. True while play keeps the sticks: no menu, or one that
    // has not yet been up for `frames` frames.
    bool Update(bool gameplay)
    {
        flickered_ = gameplay && run_ > 0 && run_ < frames_;
        if (gameplay) { run_ = 0; }
        else if (run_ < frames_) { ++run_; }
        return run_ < frames_;
    }
    // This frame ended a menu flag that was up too briefly to count.
    bool Flickered() const { return flickered_; }

private:
    unsigned frames_;
    unsigned run_ = 0;
    bool flickered_ = false;
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
