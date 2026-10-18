#include "preyvr/ControllerProfiles.h"

#include <initializer_list>

namespace preyvr::input {
namespace {
using R = Role;
using S = ActionSet;
using K = Kind;

// In Role order (ControllerProfilesTests). The labels are what a player sees in
// SteamVR's binding UI, so they say what the action does, not where it is.
constexpr std::array<Action, kRoleCount> kActions{{
    {R::gripPose, S::common, K::pose, "grip_pose", "Hand Pose (Grip)", true},
    {R::aimPose, S::common, K::pose, "aim_pose", "Hand Pose (Aim)", true},
    {R::trigger, S::common, K::analog, "trigger", "Hand Trigger (Laser Click, Menu Pages, Medkit Slot)", true},
    {R::squeeze, S::common, K::analog, "squeeze", "Hand Grip (Foregrip, Holsters, Tabs, Recenter)", true},
    {R::haptic, S::common, K::haptic, "feedback", "Controller Feedback", false},
    {R::thumbTouch, S::common, K::touch, "thumb_touch", "Thumb Resting (Finger Shape)", false},
    {R::indexTouch, S::common, K::touch, "index_touch", "Index Finger on the Trigger (Finger Shape)", false},
    {R::pause, S::common, K::button, "pause", "Pause (Hold: VR Options; With Both Grips: Recenter)", true},
    {R::move, S::gameplay, K::stick, "move", "Move", true},
    {R::turn, S::gameplay, K::stick, "turn", "Turn", true},
    {R::fire, S::gameplay, K::analog, "fire", "Fire", true},
    {R::grab, S::gameplay, K::button, "grab", "Grab and Use (Prey's X)", true},
    {R::psi, S::gameplay, K::analog, "psi", "Psi Power", true},
    {R::reload, S::gameplay, K::analog, "reload", "Reload (Hold: Holster)", true},
    {R::jump, S::gameplay, K::button, "jump", "Jump", true},
    {R::crouch, S::gameplay, K::button, "crouch", "Crouch", true},
    {R::inventory, S::gameplay, K::button, "inventory", "TranScribe", true},
    {R::special, S::gameplay, K::button, "special", "Prey's Y (Search, Special Use; Tap: Swap Weapon)", true},
    {R::weaponWheel, S::gameplay, K::button, "weapon_wheel", "Weapon Wheel (Hold)", false},
    {R::sprint, S::gameplay, K::button, "sprint", "Sprint", false},
    {R::psychoscope, S::gameplay, K::button, "psychoscope", "Psychoscope", true},
    {R::quickSlotLeft, S::gameplay, K::button, "quick_slot_left", "Quick Slot Left", false},
    {R::quickSlotRight, S::gameplay, K::button, "quick_slot_right", "Quick Slot Right", false},
    {R::quickSlotDown, S::gameplay, K::button, "quick_slot_down", "Quick Slot Down", false},
    {R::torch, S::gameplay, K::button, "torch", "Torch", false},
    {R::wheelSuitRepair, S::gameplay, K::button, "wheel_suit_repair", "Suit Repair Kit, in the Wheel (Prey's LB)", false},
    {R::wheelFood, S::gameplay, K::button, "wheel_food", "Food, in the Wheel (Prey's RB)", false},
    {R::navigate, S::menu, K::stick, "navigate", "Navigate", true},
    {R::navigateUp, S::menu, K::button, "navigate_up", "Navigate Up", false},
    {R::navigateDown, S::menu, K::button, "navigate_down", "Navigate Down", false},
    {R::navigateLeft, S::menu, K::button, "navigate_left", "Navigate Left", false},
    {R::navigateRight, S::menu, K::button, "navigate_right", "Navigate Right", false},
    {R::scroll, S::menu, K::stick, "scroll", "Scroll", true},
    {R::confirm, S::menu, K::button, "confirm", "Select", true},
    {R::back, S::menu, K::button, "back", "Back", true},
    {R::secondary, S::menu, K::button, "secondary", "Prey's X Action (Equip...)", true},
    {R::tertiary, S::menu, K::button, "tertiary", "Prey's Y Action (Search, Eat...)", true},
    {R::exit, S::menu, K::button, "exit", "Close Menu", false},
    {R::previousTab, S::menu, K::button, "previous_tab", "Previous Tab (Prey's LB)", false},
    {R::nextTab, S::menu, K::button, "next_tab", "Next Tab (Prey's RB)", false},
    {R::previousPage, S::menu, K::button, "previous_page", "Previous Page (Prey's LT)", false},
    {R::nextPage, S::menu, K::button, "next_page", "Next Page (Prey's RT)", false},
    {R::leftStickClick, S::menu, K::button, "left_stick_click", "Left Stick Click (Autosort)", false},
    {R::rightStickClick, S::menu, K::button, "right_stick_click", "Right Stick Click (Drop, Split)", false},
}};
constexpr std::array<ActionSetInfo, static_cast<std::size_t>(S::count)> kSets{{
    {"common", "Hands and System"}, {"gameplay", "Gameplay"}, {"menus", "Menus"}}};

// What every profile binds alike: both hands' poses, triggers, grips and
// haptics; the sticks (move and scroll on the left, turn and navigate on the
// right); fire on the right trigger and psi on the left one, as on Prey's
// gamepad; reload on the right grip; A and B on the right; the right stick's
// click for the psychoscope, as on Prey's gamepad. Core profiles' component paths are checked against the bundled
// Khronos xr.xml registry, the Steam Frame's against Valve's list
// (ControllerProfilesTests); runtime acceptance is logged separately (XrInput).
std::vector<Binding> Shared()
{
    return {
        {R::gripPose, "/user/hand/left/input/grip/pose"}, {R::gripPose, "/user/hand/right/input/grip/pose"},
        {R::aimPose, "/user/hand/left/input/aim/pose"}, {R::aimPose, "/user/hand/right/input/aim/pose"},
        {R::trigger, "/user/hand/left/input/trigger/value"}, {R::trigger, "/user/hand/right/input/trigger/value"},
        {R::squeeze, "/user/hand/left/input/squeeze/value"}, {R::squeeze, "/user/hand/right/input/squeeze/value"},
        {R::haptic, "/user/hand/left/output/haptic"}, {R::haptic, "/user/hand/right/output/haptic"},
        {R::indexTouch, "/user/hand/left/input/trigger/touch"}, {R::indexTouch, "/user/hand/right/input/trigger/touch"},
        {R::move, "/user/hand/left/input/thumbstick"}, {R::turn, "/user/hand/right/input/thumbstick"},
        {R::fire, "/user/hand/right/input/trigger/value"}, {R::psi, "/user/hand/left/input/trigger/value"},
        {R::reload, "/user/hand/right/input/squeeze/value"},
        {R::jump, "/user/hand/right/input/a/click"}, {R::crouch, "/user/hand/right/input/b/click"},
        {R::psychoscope, "/user/hand/right/input/thumbstick/click"},
        {R::navigate, "/user/hand/right/input/thumbstick"}, {R::scroll, "/user/hand/left/input/thumbstick"},
        {R::confirm, "/user/hand/right/input/a/click"}, {R::back, "/user/hand/right/input/b/click"},
        // In menus the right stick's click is Prey's RS (drop, split).
        {R::rightStickClick, "/user/hand/right/input/thumbstick/click"},
    };
}

std::vector<Binding> With(std::initializer_list<Binding> more)
{
    auto bindings = Shared();
    bindings.insert(bindings.end(), more);
    return bindings;
}

ControllerProfile Touch()
{
    // X and Y are on the left, and are Prey's X and Y in play and in menus.
    // With no View button, the left stick's click opens the TranScribe, so
    // sprint is left for SteamVR's binding UI; Prey's own Y-hold is the wheel.
    // The menu button is the left one; its right-hand counterpart is reserved
    // by the runtime.
    return {"touch", {"/interaction_profiles/oculus/touch_controller"}, nullptr, With({
        {R::grab, "/user/hand/left/input/x/click"}, {R::secondary, "/user/hand/left/input/x/click"},
        {R::special, "/user/hand/left/input/y/click"}, {R::tertiary, "/user/hand/left/input/y/click"},
        {R::inventory, "/user/hand/left/input/thumbstick/click"},
        // The TranScribe's click is Prey's LS (autosort) in menus; B closes them.
        {R::leftStickClick, "/user/hand/left/input/thumbstick/click"},
        {R::pause, "/user/hand/left/input/menu/click"},
        // The thumb's resting places, for the finger shape.
        {R::thumbTouch, "/user/hand/left/input/x/touch"}, {R::thumbTouch, "/user/hand/left/input/y/touch"},
        {R::thumbTouch, "/user/hand/left/input/thumbstick/touch"}, {R::thumbTouch, "/user/hand/left/input/thumbrest/touch"},
        {R::thumbTouch, "/user/hand/right/input/a/touch"}, {R::thumbTouch, "/user/hand/right/input/b/touch"},
        {R::thumbTouch, "/user/hand/right/input/thumbstick/touch"}, {R::thumbTouch, "/user/hand/right/input/thumbrest/touch"},
    })};
}

ControllerProfile Index()
{
    // Index has A/B on both hands: the left pair stands in for Touch's X/Y.
    // Its system button belongs to the runtime, so the left stick's click
    // opens pause, pressing the left trackpad opens the TranScribe (a boolean
    // on a float input: the runtime applies the threshold), and sprint is left
    // for SteamVR's binding UI.
    return {"index", {"/interaction_profiles/valve/index_controller"}, nullptr, With({
        {R::grab, "/user/hand/left/input/a/click"}, {R::secondary, "/user/hand/left/input/a/click"},
        {R::special, "/user/hand/left/input/b/click"}, {R::tertiary, "/user/hand/left/input/b/click"},
        {R::inventory, "/user/hand/left/input/trackpad/force"},
        // Its left stick's click is pause everywhere, so the trackpad press,
        // the TranScribe in play, is Prey's LS (autosort) in menus.
        {R::leftStickClick, "/user/hand/left/input/trackpad/force"},
        {R::pause, "/user/hand/left/input/thumbstick/click"},
        {R::thumbTouch, "/user/hand/left/input/a/touch"}, {R::thumbTouch, "/user/hand/left/input/b/touch"},
        {R::thumbTouch, "/user/hand/left/input/thumbstick/touch"}, {R::thumbTouch, "/user/hand/left/input/trackpad/touch"},
        {R::thumbTouch, "/user/hand/right/input/a/touch"}, {R::thumbTouch, "/user/hand/right/input/b/touch"},
        {R::thumbTouch, "/user/hand/right/input/thumbstick/touch"}, {R::thumbTouch, "/user/hand/right/input/trackpad/touch"},
    })};
}

ControllerProfile Frame()
{
    // **The Steam Frame controllers' own profile** (XR_VALVE_frame_controller_
    // interaction): a gamepad split in two, so each button does what the
    // gamepad's does in Prey and the game's own prompts name the right one.
    // Right: A, B, X, Y and Menu (pause). Left: the D-pad -- Prey's quick slots
    // and torch in play, navigation in menus -- and View, the TranScribe in
    // play and closing the menu in menus. The left stick's click sprints, as
    // on Prey's gamepad. Without this
    // profile SteamVR presents them as Touch and folds three buttons onto one:
    // right X/Y onto B, and the D-pad onto left X (down) and Y (the rest). In
    // play the bumpers are Prey's LB/RB only inside its weapon wheel (Y held):
    // a suit repair kit and food. Outside it they send nothing -- Prey's
    // bumpers lean, which VR does by itself. In menus the bumpers are Prey's
    // LB/RB (tabs) and the grips its LT/RT (pages), so the tab prompts name the
    // buttons; the stick clicks are its LS/RS.
    //
    // Valve renamed the profile in SteamVR 2.15.1; later runtimes still accept
    // the first name (vrclient_x64.dll knows both), so both are suggested.
    return {"frame", {"/interaction_profiles/valve/frame_controller_valve", "/interaction_profiles/valve/frame_controller"},
            "XR_VALVE_frame_controller_interaction", With({
        {R::grab, "/user/hand/right/input/x/click"}, {R::secondary, "/user/hand/right/input/x/click"},
        {R::special, "/user/hand/right/input/y/click"}, {R::tertiary, "/user/hand/right/input/y/click"},
        {R::wheelSuitRepair, "/user/hand/left/input/bumper/click"},
        {R::wheelFood, "/user/hand/right/input/bumper/click"},
        {R::pause, "/user/hand/right/input/menu/click"},
        {R::inventory, "/user/hand/left/input/view/click"}, {R::exit, "/user/hand/left/input/view/click"},
        {R::sprint, "/user/hand/left/input/thumbstick/click"},
        {R::leftStickClick, "/user/hand/left/input/thumbstick/click"},
        {R::previousTab, "/user/hand/left/input/bumper/click"}, {R::nextTab, "/user/hand/right/input/bumper/click"},
        {R::previousPage, "/user/hand/left/input/squeeze/click"},
        {R::nextPage, "/user/hand/right/input/squeeze/click"},
        {R::torch, "/user/hand/left/input/dpad_up/click"}, {R::navigateUp, "/user/hand/left/input/dpad_up/click"},
        {R::quickSlotDown, "/user/hand/left/input/dpad_down/click"},
        {R::navigateDown, "/user/hand/left/input/dpad_down/click"},
        {R::quickSlotLeft, "/user/hand/left/input/dpad_left/click"},
        {R::navigateLeft, "/user/hand/left/input/dpad_left/click"},
        {R::quickSlotRight, "/user/hand/left/input/dpad_right/click"},
        {R::navigateRight, "/user/hand/left/input/dpad_right/click"},
        {R::thumbTouch, "/user/hand/right/input/a/touch"}, {R::thumbTouch, "/user/hand/right/input/b/touch"},
        {R::thumbTouch, "/user/hand/right/input/x/touch"}, {R::thumbTouch, "/user/hand/right/input/y/touch"},
        {R::thumbTouch, "/user/hand/right/input/menu/touch"}, {R::thumbTouch, "/user/hand/right/input/thumbstick/touch"},
        {R::thumbTouch, "/user/hand/left/input/dpad_up/touch"}, {R::thumbTouch, "/user/hand/left/input/dpad_down/touch"},
        {R::thumbTouch, "/user/hand/left/input/dpad_left/touch"}, {R::thumbTouch, "/user/hand/left/input/dpad_right/touch"},
        {R::thumbTouch, "/user/hand/left/input/view/touch"}, {R::thumbTouch, "/user/hand/left/input/thumbstick/touch"},
    })};
}
} // namespace

const std::array<Action, kRoleCount>& Actions() { return kActions; }
const Action& ActionOf(Role role) { return kActions[static_cast<std::size_t>(role)]; }
const ActionSetInfo& SetInfo(ActionSet set) { return kSets[static_cast<std::size_t>(set)]; }

const std::vector<ControllerProfile>& ControllerProfiles()
{
    static const std::vector<ControllerProfile> profiles{Touch(), Index(), Frame()};
    return profiles;
}
} // namespace preyvr::input
