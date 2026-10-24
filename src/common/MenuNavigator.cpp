#include "preyvr/MenuNavigator.h"

#include <cmath>

namespace preyvr::input {

// A diagonal resolves to the dominant axis rather than firing both: a slightly
// off-axis flick would otherwise move the highlight down *and* across and land
// somewhere the player did not aim.
MenuNavigator::Direction MenuNavigator::DirectionOf(float x, float y, float threshold)
{
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return Direction::None;
    }
    const float absX = std::fabs(x);
    const float absY = std::fabs(y);
    if (absX < threshold && absY < threshold) {
        return Direction::None;
    }
    if (absX >= absY) {
        return x > 0.0f ? Direction::Right : Direction::Left;
    }
    return y > 0.0f ? Direction::Up : Direction::Down;
}

unsigned int MenuNavigator::Update(const ControllerMenuState& state, float deltaSeconds,
                                   MenuAction* out, unsigned int capacity)
{
    if (out == nullptr || capacity == 0) {
        return 0;
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) {
        deltaSeconds = 0.0f;
    }
    unsigned int count = 0;

    const auto emit = [&](MenuAction action) {
        if (count < capacity) {
            out[count] = action;
            ++count;
        }
    };

    // --- stick, as a d-pad with repeat ---------------------------------------
    //
    // Engage and release use different thresholds. With one threshold a stick
    // resting near it chatters on noise and emits a stream of actions.
    const float threshold = held_ == Direction::None ? policy_.engage : policy_.release;
    const Direction now = DirectionOf(state.stickX, state.stickY, threshold);

    if (now != held_) {
        held_ = now;
        heldFor_ = 0.0f;
        untilRepeat_ = policy_.initialDelay;
        if (now != Direction::None) {
            switch (now) {
                case Direction::Up: emit(MenuAction::Up); break;
                case Direction::Down: emit(MenuAction::Down); break;
                case Direction::Left: emit(MenuAction::Left); break;
                case Direction::Right: emit(MenuAction::Right); break;
                case Direction::None: break;
            }
        }
    } else if (held_ != Direction::None) {
        heldFor_ += deltaSeconds;
        untilRepeat_ -= deltaSeconds;
        // A loop rather than a single fire: a long frame must not swallow a
        // repeat that was due inside it, or the highlight stutters exactly when
        // the game is struggling.
        while (untilRepeat_ <= 0.0f && count < capacity) {
            switch (held_) {
                case Direction::Up: emit(MenuAction::Up); break;
                case Direction::Down: emit(MenuAction::Down); break;
                case Direction::Left: emit(MenuAction::Left); break;
                case Direction::Right: emit(MenuAction::Right); break;
                case Direction::None: break;
            }
            untilRepeat_ += policy_.repeatInterval;
        }
    }

    // --- buttons, rising edge only -------------------------------------------
    if (state.accept && !acceptWas_) {
        emit(MenuAction::Accept);
    }
    if (state.cancel && !cancelWas_) {
        emit(MenuAction::Cancel);
    }
    if (state.start && !startWas_) {
        emit(MenuAction::Start);
    }
    acceptWas_ = state.accept;
    cancelWas_ = state.cancel;
    startWas_ = state.start;

    return count;
}

void MenuNavigator::Reset()
{
    held_ = Direction::None;
    heldFor_ = 0.0f;
    untilRepeat_ = 0.0f;
    // Buttons are cleared too. After a reset the next frame's held button is a
    // fresh press: treating it as still-held would swallow the player's first
    // input after a menu opens.
    acceptWas_ = false;
    cancelWas_ = false;
    startWas_ = false;
}

unsigned int ModalMenuRouter::Update(const ModalMenuState& state, bool modal, float seconds,
                                     MenuAction* out, unsigned int capacity)
{
    if (!out || !capacity) { return 0; }
    unsigned int count = 0;
    if (state.start && !startWas_) { out[count++] = MenuAction::Start; }
    startWas_ = state.start;
    const bool buttons[] = {state.accept, state.cancel, state.previousTab,
                            state.nextTab, state.secondary, state.tertiary,state.previousPage,state.nextPage,
                            state.exit, state.leftStick, state.rightStick};
    static_assert(sizeof(buttons) / sizeof(buttons[0]) == sizeof(blocked_) / sizeof(blocked_[0]),
                  "one blocked/was slot per button");
    constexpr unsigned kButtons = sizeof(buttons) / sizeof(buttons[0]);
    if (!modal || !modal_) {
        navigator_.Reset();
        blockStick_ = std::fabs(state.stickX) > .35f || std::fabs(state.stickY) > .35f;
        for (unsigned i = 0; i < kButtons; ++i) { blocked_[i] = buttons[i]; }
        for (bool& held : was_) { held = false; }
        previousTab_.Reset(); nextTab_.Reset(); previousPage_.Reset(); nextPage_.Reset();
    }
    modal_ = modal;
    if (!modal) { secondaryHeld_ = tertiaryHeld_ = false; return count; }
    if (std::fabs(state.stickX) <= .35f && std::fabs(state.stickY) <= .35f) { blockStick_ = false; }
    for (unsigned i = 0; i < kButtons; ++i) { if (!buttons[i]) { blocked_[i] = false; } }
    ControllerMenuState nav{};
    nav.stickX = blockStick_ ? 0 : state.stickX;
    nav.stickY = blockStick_ ? 0 : state.stickY;
    nav.accept = state.accept && !blocked_[0];
    nav.cancel = state.cancel && !blocked_[1];
    if (count < capacity) { count += navigator_.Update(nav, seconds, out+count, capacity-count); }
    const auto emit = [&](MenuAction action) { if (count < capacity) { out[count++] = action; } };
    const auto down = [&](unsigned i) { return buttons[i] && !blocked_[i]; };
    // The tabs and pages act on release, and not at all when both grips, or
    // both of the pair, were held meanwhile (ReleaseTap).
    const bool tabs = state.bothGrips || (state.previousTab && state.nextTab);
    if (previousTab_.Update(down(2), tabs)) { emit(MenuAction::PreviousTab); }
    if (nextTab_.Update(down(3), tabs)) { emit(MenuAction::NextTab); }
    const bool pages = state.bothGrips || (state.previousPage && state.nextPage);
    if (previousPage_.Update(down(6), pages)) { emit(MenuAction::PreviousPage); }
    if (nextPage_.Update(down(7), pages)) { emit(MenuAction::NextPage); }
    // Exit and the stick clicks act on press.
    constexpr struct { unsigned button; MenuAction action; } pressed[] = {
        {8, MenuAction::Exit}, {9, MenuAction::LeftStick}, {10, MenuAction::RightStick}};
    for (const auto& [i, action] : pressed) {
        if (down(i) && !was_[i]) { emit(action); }
        was_[i] = down(i);
    }
    // X and Y are held for as long as they are down (MenuHold.h).
    secondaryHeld_ = down(4);
    tertiaryHeld_ = down(5);
    return count;
}

void ModalMenuRouter::Reset()
{
    *this = ModalMenuRouter{};
}

} // namespace preyvr::input
