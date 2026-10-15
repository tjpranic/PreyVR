#include "preyvr/ControllerProfiles.h"

#include <initializer_list>
#include <utility>

namespace preyvr::input {
namespace {
using R = Role;

// Poses, sticks, triggers, grips and haptics, which every profile names alike,
// and the right stick's click for the weapon wheel. Component paths are
// checked against the bundled Khronos xr.xml registry; runtime acceptance is
// logged separately (XrInput).
std::vector<Binding> Shared()
{
    return {
        {R::gripPose, "/user/hand/left/input/grip/pose"}, {R::gripPose, "/user/hand/right/input/grip/pose"},
        {R::aimPose, "/user/hand/left/input/aim/pose"}, {R::aimPose, "/user/hand/right/input/aim/pose"},
        {R::thumbstick, "/user/hand/left/input/thumbstick"}, {R::thumbstick, "/user/hand/right/input/thumbstick"},
        {R::trigger, "/user/hand/left/input/trigger/value"}, {R::trigger, "/user/hand/right/input/trigger/value"},
        {R::squeeze, "/user/hand/left/input/squeeze/value"}, {R::squeeze, "/user/hand/right/input/squeeze/value"},
        {R::haptic, "/user/hand/left/output/haptic"}, {R::haptic, "/user/hand/right/output/haptic"},
        {R::weaponWheel, "/user/hand/right/input/thumbstick/click"},
    };
}

std::vector<Binding> With(std::vector<Binding> bindings, std::initializer_list<Binding> more)
{
    bindings.insert(bindings.end(), more);
    return bindings;
}

ControllerProfile Touch()
{
    // A/B are right-hand only on Touch and X/Y left-hand only, so accept and
    // cancel bind to one controller each. The menu button is the left one;
    // its right-hand counterpart is reserved by the runtime.
    return {"touch", "/interaction_profiles/oculus/touch_controller", nullptr,
            {{"all", With(Shared(), {
                {R::accept, "/user/hand/right/input/a/click"}, {R::cancel, "/user/hand/right/input/b/click"},
                {R::accept, "/user/hand/left/input/x/click"}, {R::cancel, "/user/hand/left/input/y/click"},
                {R::start, "/user/hand/left/input/menu/click"},
            })}}};
}

ControllerProfile Index()
{
    // Index has A/B on both hands: the left pair stands in for Touch's X/Y.
    // Its system button belongs to the runtime, so the left stick's click
    // opens pause.
    return {"index", "/interaction_profiles/valve/index_controller", nullptr,
            {{"all", With(Shared(), {
                {R::accept, "/user/hand/right/input/a/click"}, {R::cancel, "/user/hand/right/input/b/click"},
                {R::accept, "/user/hand/left/input/a/click"}, {R::cancel, "/user/hand/left/input/b/click"},
                {R::start, "/user/hand/left/input/thumbstick/click"},
            })}}};
}

ControllerProfile Frame()
{
    // **The Steam Frame controllers' own profile** (XR_VALVE_frame_controller_
    // interaction). Without it SteamVR presents them as Touch and folds the
    // left D-pad into X/Y -- down as X, left/right/up as Y -- so its left and
    // right cannot be told apart. These keep exactly what that emulation gave
    // every role (SteamVR's own Frame-to-Touch remapping), so the game behaves
    // as before, and add the D-pad as itself: the mod's panels navigate with it.
    const auto core = With(Shared(), {
        {R::accept, "/user/hand/right/input/a/click"}, {R::cancel, "/user/hand/right/input/b/click"},
        // Touch's left X and Y, where the emulation put them on the D-pad.
        {R::accept, "/user/hand/left/input/dpad_down/click"}, {R::cancel, "/user/hand/left/input/dpad_up/click"},
        {R::cancel, "/user/hand/left/input/dpad_left/click"}, {R::cancel, "/user/hand/left/input/dpad_right/click"},
        {R::navigateUp, "/user/hand/left/input/dpad_up/click"},
        {R::navigateDown, "/user/hand/left/input/dpad_down/click"},
        {R::navigateLeft, "/user/hand/left/input/dpad_left/click"},
        {R::navigateRight, "/user/hand/left/input/dpad_right/click"},
    });
    // The less certain parts, tried in order of fidelity: which button is Menu,
    // and X/Y on the right acting as B, as the emulation does.
    const std::pair<const char*, std::vector<Binding>> starts[] = {
        {"view_and_menu", {{R::start, "/user/hand/left/input/view/click"}, {R::start, "/user/hand/right/input/menu/click"}}},
        {"menu", {{R::start, "/user/hand/right/input/menu/click"}}},
        {"view", {{R::start, "/user/hand/left/input/view/click"}}},
        {"left_menu", {{R::start, "/user/hand/left/input/menu/click"}}},
    };
    ControllerProfile profile{"frame", "/interaction_profiles/valve/frame_controller",
                              "XR_VALVE_frame_controller_interaction", {}};
    for (const bool rightXy : {true, false}) {
        for (const auto& [menu, start] : starts) {
            auto bindings = core;
            if (rightXy) {
                bindings.push_back({R::cancel, "/user/hand/right/input/x/click"});
                bindings.push_back({R::cancel, "/user/hand/right/input/y/click"});
            }
            bindings.insert(bindings.end(), start.begin(), start.end());
            profile.attempts.push_back({std::string("menu=") + menu + (rightXy ? ",right_xy_as_b" : ""),
                                        std::move(bindings)});
        }
    }
    return profile;
}
} // namespace

const std::vector<ControllerProfile>& ControllerProfiles()
{
    static const std::vector<ControllerProfile> profiles{Touch(), Index(), Frame()};
    return profiles;
}
} // namespace preyvr::input
