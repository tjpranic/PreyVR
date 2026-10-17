#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace preyvr::input {
// **The action sets**: what the mod asks of a controller, grouped by when it
// applies. Each is one page of SteamVR's binding UI, which is how a player
// rebinds. All three are synced every frame and the consumers keep their own
// context gates: a set left unsynced would hide buttons held across a menu
// opening or closing from the rules that make them be let go first.
enum class ActionSet : unsigned char {
    common,    // the hands' own poses, trigger and grip, feedback, pause
    gameplay,  // play: moving, turning, firing, grabbing, the game's buttons
    menu,      // the game's menus and the mod's own panels
    count
};
// What an action reads, which decides its OpenXR type and the paths it binds.
enum class Kind : unsigned char { pose, button, analog, stick, haptic, touch };

// **What the mod asks of a controller**, named for what it does: one OpenXR
// action each (XrInput), bound by each controller to its own components below.
// Gameplay and the mod's panels read the roles, never a device's own buttons.
enum class Role : unsigned char {
    // common
    gripPose, aimPose,
    // The hands' trigger and grip as such, for what reads a hand rather than
    // a button: the laser's click, menu pages and tabs, the foregrip, holsters,
    // the medkit slot, the psychoscope gesture and the recenter chord.
    trigger, squeeze,
    haptic,
    // Capacitive touch, for the fingers' shape (HandPose): the thumb on any
    // button or stick under it, the index finger on the trigger.
    thumbTouch, indexTouch,
    // Tap: the game's pause; hold: VR Options; with both grips held: reset
    // the view (the recenter chord; it acts on neither of the others).
    pause,
    // gameplay
    move, turn,
    fire,
    // The left hand's use: what it points at, picked up, carried, dragged,
    // thrown on letting go. With nothing to grab it is Prey's psi button, or
    // aims the equipped power with psychic targeting on.
    grab,
    reload,    // tap: reload; hold: holster; holstered: draw
    jump, crouch,
    inventory, // the TranScribe
    weaponWheel,  // Prey's own Y: tap swaps weapon, hold opens the wheel
    // Prey's own gamepad buttons: the left and right stick clicks, and the
    // D-pad (quick slots, and up for the torch) on a controller that has one.
    sprint, psychoscope,
    quickSlotLeft, quickSlotRight, quickSlotDown, torch,
    // menu
    navigate,  // the stick; the Steam Frame's D-pad below it
    navigateUp, navigateDown, navigateLeft, navigateRight,
    scroll,    // a long list in VR Options
    confirm, back,
    secondary, tertiary,  // Prey's X and Y actions (Equip, Search, Eat...)
    exit,      // closes the menu: the Steam Frame's View, as on a gamepad
    count
};
inline constexpr std::size_t kRoleCount = static_cast<std::size_t>(Role::count);
struct Action {
    Role role;
    ActionSet set;
    Kind kind;
    const char* name;   // the OpenXR action name
    const char* label;  // what SteamVR's binding UI shows
    bool required;      // without it, input is not created at all
};
// Every role's action, in Role order.
const std::array<Action, kRoleCount>& Actions();
const Action& ActionOf(Role role);
struct ActionSetInfo { const char* name; const char* label; };
const ActionSetInfo& SetInfo(ActionSet set);

struct Binding { Role role; const char* path; };
// **One controller's specifics, kept together**: its interaction profile under
// every name a runtime may know it by (each is suggested; a runtime refuses the
// names it does not know), the instance extension it needs (none for core
// profiles), and one complete set of bindings for all three action sets. A
// runtime takes a profile's suggestion whole, so the set names everything the
// controller is bound for.
struct ControllerProfile {
    const char* name;
    std::vector<const char*> paths;
    const char* extension;
    std::vector<Binding> bindings;
};
// Every controller the mod binds: Meta Touch, Valve Index, the Steam Frame.
const std::vector<ControllerProfile>& ControllerProfiles();

// **An analog press, with hysteresis**: down at `press`, up again only below
// `release`, so a trigger or grip resting near its trip point does not chatter
// between pressed and released on sensor noise.
struct PressThreshold { float press, release; };
inline constexpr PressThreshold kTriggerPress{0.5f, 0.4f};
inline constexpr PressThreshold kGripPress{0.55f, 0.45f};
inline bool Pressed(bool was, float value, PressThreshold threshold)
{
    return std::isfinite(value) && value >= (was ? threshold.release : threshold.press);
}
}
