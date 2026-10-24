#pragma once
#include "preyvr/InputEvent.h"
#include <cstdint>

// **Prey's X and Y in menus are held, not tapped.** Its menus bind actions to
// their hold: the recycler's transfer all is `menu_take_all` on xi_x, which its
// handler (0x161CA60) runs only for activation mode 4, on hold; Y's
// `menu_dismantle` has a hold bit too. A one-frame tap filled the recycler's
// ring a sliver and let go.
//
// So the button is mirrored as a pad's reaches the engine: pressed, then held
// (eIS_Down) every game frame, then released. A posted press never joins the
// engine's own list of held symbols, so without the held events a hold never
// completes -- the reason MoveLane reposts its held buttons.
namespace preyvr::input {

// One button as the XR frame service last read it: down, and the menu its
// latest press was made in. `presses` counts presses, so one shorter than a
// game frame still lands.
struct MenuHoldSample {
    bool held = false;
    std::uint32_t presses = 0;
    std::uint64_t scope = 0;   // HudMenuEpoch of the latest press
    void Note(bool down, std::uint64_t menu)
    {
        if (down && !held) { ++presses; scope = menu; }
        held = down;
    }
};

// Prey's X and Y, read together; `stamp` is when (MonotonicNanoseconds).
struct MenuHolds {
    MenuHoldSample button[2];
    std::uint64_t stamp = 0;
};

// One button's events, on the game's main thread. Scoped to the menu it was
// pressed in: a menu that closes or changes, or samples that stop coming,
// let it go at once, so a hold never leaks into play or the next menu. Each
// press is mirrored once: one let go that way stays let go until pressed again.
class MenuHoldMirror {
public:
    // This game frame's event, kStatePressed, kStateDown or kStateReleased, or
    // 0 for none. `fresh`: the sample is recent enough to trust; `current` and
    // `modal`: the menu now, as the tap dispatch judges it.
    unsigned Next(const MenuHoldSample& sample, bool fresh, std::uint64_t current, bool modal)
    {
        const bool here = fresh && modal && sample.scope == current;
        const bool newPress = sample.presses != mirrored_;
        pending_ = sample.presses;
        if (down_) {
            if (here && sample.held && !newPress) {
                ++frames_;
                return kStateDown;
            }
            // Let go, or let go and pressed again between two game frames:
            // this release, then the new press on the next frame.
            return kStateReleased;
        }
        return here && newPress ? kStatePressed : 0u;
    }
    // After the event was posted.
    void Delivered(unsigned state)
    {
        if (state == kStatePressed) { down_ = true; mirrored_ = pending_; frames_ = 0; }
        if (state == kStateReleased) { down_ = false; }
    }
    bool Down() const { return down_; }
    // Held events posted since the press.
    unsigned Frames() const { return frames_; }
private:
    bool down_ = false;
    std::uint32_t mirrored_ = 0, pending_ = 0;   // the press posted, and the one Next saw
    unsigned frames_ = 0;
};

} // namespace preyvr::input
