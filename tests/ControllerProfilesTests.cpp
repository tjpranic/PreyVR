// The mod's actions and each controller's bindings (ControllerProfiles): named
// actions in three sets, one module per device's specifics, every binding set
// complete, each action on one button, a button with one meaning per set, and
// every path one the controller has, of the kind its action reads.
#include "preyvr/ControllerProfiles.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>

using namespace preyvr::input;

namespace {
void Check(bool ok, const char* why)
{
    if (!ok) { std::cerr << why << '\n'; std::exit(1); }
}
bool Has(const ControllerProfile& profile, Role role, const std::string& path)
{
    for (const auto& binding : profile.bindings) {
        if (binding.role == role && path == binding.path) { return true; }
    }
    return false;
}
unsigned Count(const ControllerProfile& profile, Role role)
{
    unsigned count = 0;
    for (const auto& binding : profile.bindings) { count += binding.role == role ? 1u : 0u; }
    return count;
}
const ControllerProfile& Profile(const char* name)
{
    for (const auto& profile : ControllerProfiles()) {
        if (std::strcmp(profile.name, name) == 0) { return profile; }
    }
    Check(false, "a controller profile is missing");
    std::abort();
}
bool EndsWith(const std::string& text, const std::string& end)
{
    return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
}
// OpenXR's rule for action and action set names.
bool ValidName(const char* name)
{
    if (name == nullptr || *name == '\0') { return false; }
    for (const char* c = name; *c; ++c) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-' || *c == '.')) {
            return false;
        }
    }
    return std::strlen(name) < 64;
}
constexpr Role kHands[] = {Role::gripPose, Role::aimPose, Role::trigger, Role::squeeze, Role::haptic,
                           Role::thumbTouch, Role::indexTouch};
// Bound by every controller, once each.
constexpr Role kEveryController[] = {Role::pause, Role::move, Role::turn, Role::fire, Role::grab, Role::psi,
                                     Role::reload, Role::jump, Role::crouch, Role::inventory, Role::special,
                                     Role::psychoscope, Role::navigate, Role::scroll, Role::confirm, Role::back,
                                     Role::secondary, Role::tertiary};
// Only a controller with the buttons for them: the Steam Frame's D-pad, View
// and bumper; sprint, which Touch and Index give their left stick click to
// (the TranScribe, pause).
constexpr Role kDpadAndView[] = {Role::navigateUp, Role::navigateDown, Role::navigateLeft, Role::navigateRight,
                                 Role::exit, Role::quickSlotLeft, Role::quickSlotRight, Role::quickSlotDown,
                                 Role::torch};
constexpr Role kOptional[] = {Role::navigateUp, Role::navigateDown, Role::navigateLeft, Role::navigateRight,
                              Role::exit, Role::quickSlotLeft, Role::quickSlotRight, Role::quickSlotDown,
                              Role::torch, Role::sprint, Role::weaponWheel};

// **The components each controller has**, per hand, without the system
// button, which belongs to the runtime. Touch and Index from the bundled
// Khronos xr.xml (OpenXR SDK 1.1.60); the Steam Frame, which that registry
// does not have, from Valve's list (partner.steamgames.com/doc/steamhardware/
// steamframe/input, read 2026-10-17).
using Components = std::map<std::string, std::set<std::string>>;
Components Both(std::set<std::string> common, std::set<std::string> left, std::set<std::string> right)
{
    left.insert(common.begin(), common.end());
    right.insert(common.begin(), common.end());
    return {{"left", left}, {"right", right}};
}
Components TouchComponents()
{
    return Both({"squeeze/value", "trigger/value", "trigger/touch", "thumbstick", "thumbstick/x", "thumbstick/y",
                 "thumbstick/click", "thumbstick/touch", "thumbrest/touch", "grip/pose", "aim/pose"},
                {"x/click", "x/touch", "y/click", "y/touch", "menu/click"},
                {"a/click", "a/touch", "b/click", "b/touch"});
}
Components IndexComponents()
{
    return Both({"a/click", "a/touch", "b/click", "b/touch", "squeeze/value", "squeeze/force", "trigger/click",
                 "trigger/value", "trigger/touch", "thumbstick", "thumbstick/x", "thumbstick/y", "thumbstick/click",
                 "thumbstick/touch", "trackpad", "trackpad/x", "trackpad/y", "trackpad/force", "trackpad/touch",
                 "grip/pose", "aim/pose"},
                {}, {});
}
Components FrameComponents()
{
    return Both({"bumper/touch", "bumper/click", "squeeze/touch", "squeeze/click", "squeeze/value", "trigger/touch",
                 "trigger/click", "trigger/value", "thumbstick/touch", "thumbstick/click", "thumbstick/x",
                 "thumbstick/y", "thumbstick", "grip/pose", "aim/pose"},
                {"dpad_up/touch", "dpad_up/click", "dpad_left/touch", "dpad_left/click", "dpad_down/touch",
                 "dpad_down/click", "dpad_right/touch", "dpad_right/click", "view/touch", "view/click"},
                {"a/touch", "a/click", "b/touch", "b/click", "x/touch", "x/click", "y/touch", "y/click", "menu/touch",
                 "menu/click"});
}
// "/user/hand/<hand>/input/<component>" or ".../output/haptic", split.
bool Split(const std::string& path, std::string& hand, std::string& component)
{
    const std::string prefix = "/user/hand/";
    if (path.rfind(prefix, 0) != 0) { return false; }
    const auto slash = path.find('/', prefix.size());
    if (slash == std::string::npos) { return false; }
    hand = path.substr(prefix.size(), slash - prefix.size());
    const std::string rest = path.substr(slash + 1);
    if (rest == "output/haptic") { component = rest; return true; }
    if (rest.rfind("input/", 0) != 0) { return false; }
    component = rest.substr(6);
    return true;
}
// The path a kind of action reads: a pose, a float, a 2D stick, a click.
bool KindFits(Kind kind, const std::string& path)
{
    switch (kind) {
    case Kind::pose: return EndsWith(path, "/pose");
    case Kind::analog: return EndsWith(path, "/value");
    case Kind::stick: return EndsWith(path, "/thumbstick");
    // A click, or a float input pressed (Index's trackpad): the runtime
    // thresholds a boolean action bound to a float.
    case Kind::button: return EndsWith(path, "/click") || EndsWith(path, "/force");
    case Kind::haptic: return EndsWith(path, "/output/haptic");
    case Kind::touch: return EndsWith(path, "/touch");
    }
    return false;
}
} // namespace

int main()
{
    // **The actions**: one per role, in order, each in one set, with names
    // OpenXR accepts and SteamVR can show.
    std::set<std::string> names;
    for (std::size_t i = 0; i < kRoleCount; ++i) {
        const Action& action = Actions()[i];
        Check(static_cast<std::size_t>(action.role) == i && &ActionOf(action.role) == &action, "one action per role, in order");
        Check(ValidName(action.name) && names.insert(action.name).second, "action names valid and unique");
        Check(action.label != nullptr && std::strlen(action.label) > 0 && std::strlen(action.label) < 128,
              "every action has a label for the binding UI");
        Check(action.set < ActionSet::count, "every action is in a set");
    }
    std::set<std::string> sets;
    for (const ActionSet set : {ActionSet::common, ActionSet::gameplay, ActionSet::menu}) {
        Check(ValidName(SetInfo(set).name) && sets.insert(SetInfo(set).name).second && std::strlen(SetInfo(set).label) > 0,
              "set names valid and unique, with labels");
    }
    for (const Role role : kHands) { Check(ActionOf(role).set == ActionSet::common, "the hands themselves are always read"); }
    Check(ActionOf(Role::pause).set == ActionSet::common, "pause, and the recenter chord, work in play and in menus");
    for (const Role role : {Role::move, Role::turn, Role::fire, Role::grab, Role::psi, Role::reload, Role::jump,
                            Role::crouch, Role::inventory, Role::special, Role::weaponWheel, Role::sprint, Role::psychoscope, Role::quickSlotLeft,
                            Role::quickSlotRight, Role::quickSlotDown, Role::torch}) {
        Check(ActionOf(role).set == ActionSet::gameplay, "play's actions in the gameplay set");
    }
    for (const Role role : {Role::navigate, Role::scroll, Role::confirm, Role::back, Role::secondary, Role::tertiary,
                            Role::exit, Role::navigateUp}) {
        Check(ActionOf(role).set == ActionSet::menu, "the menus' actions in the menu set");
    }

    Check(ControllerProfiles().size() == 3, "Touch, Index and the Steam Frame");
    const std::map<std::string, Components> components{
        {"touch", TouchComponents()}, {"index", IndexComponents()}, {"frame", FrameComponents()}};
    for (const auto& profile : ControllerProfiles()) {
        Check(!profile.paths.empty(), "every profile names its interaction profile");
        // **Every set is complete**: a runtime takes a profile's suggestion as
        // all it binds, so a set missing a pose would be a controller with no hand.
        for (const std::string hand : {"left", "right"}) {
            const std::pair<Role, const char*> shared[] = {
                {Role::gripPose, "input/grip/pose"}, {Role::aimPose, "input/aim/pose"},
                {Role::trigger, "input/trigger/value"}, {Role::squeeze, "input/squeeze/value"},
                {Role::haptic, "output/haptic"}};
            for (const auto& [role, component] : shared) {
                Check(Has(profile, role, "/user/hand/" + hand + "/" + component),
                      "every set binds both hands' poses, triggers, grips and haptics");
            }
        }
        // **No duplicated bindings**: each action is one button (or stick).
        // SteamVR's Touch emulation put three Frame buttons on B.
        for (const Role role : kEveryController) { Check(Count(profile, role) == 1, "every action on exactly one button"); }
        for (const Role role : kOptional) { Check(Count(profile, role) <= 1, "an optional action on one button at most"); }
        std::set<std::pair<int, std::string>> seen;
        std::set<std::pair<int, std::string>> meanings;
        for (const auto& binding : profile.bindings) {
            Check(seen.insert({static_cast<int>(binding.role), binding.path}).second, "no binding twice in a set");
            // A button means one thing in each context.
            Check(meanings.insert({static_cast<int>(ActionOf(binding.role).set), binding.path}).second,
                  "a button carries one action per set");
            Check(KindFits(ActionOf(binding.role).kind, binding.path), "every path is of the kind its action reads");
            std::string hand, component;
            Check(Split(binding.path, hand, component), "every path is a hand's input or haptic output");
            Check(component == "output/haptic" || components.at(profile.name).at(hand).count(component) == 1,
                  "every path is a component that hand of that controller has");
        }
        // The fingers' shape: the index finger on each trigger, the thumb on
        // something under it on each hand.
        for (const std::string hand : {"left", "right"}) {
            Check(Has(profile, Role::indexTouch, "/user/hand/" + hand + "/input/trigger/touch"),
                  "every controller reports the index finger on each trigger");
            bool thumb = false;
            for (const auto& binding : profile.bindings) {
                thumb = thumb || (binding.role == Role::thumbTouch &&
                                  std::string(binding.path).rfind("/user/hand/" + hand + "/", 0) == 0);
            }
            Check(thumb, "and the thumb on each hand");
        }
        // What every controller does alike.
        Check(Has(profile, Role::fire, "/user/hand/right/input/trigger/value") &&
              Has(profile, Role::psi, "/user/hand/left/input/trigger/value") &&
              Has(profile, Role::reload, "/user/hand/right/input/squeeze/value"),
              "fire on the right trigger, psi on the left one (Prey's RT and LT), reload on the right grip");
        // Grab is Prey's X and sits where X is, so Prey's X prompts name it.
        for (const auto& binding : profile.bindings) {
            if (binding.role == Role::grab) {
                Check(Has(profile, Role::secondary, binding.path), "grab is on the button that is Prey's X in menus");
            }
            if (binding.role == Role::special) {
                Check(Has(profile, Role::tertiary, binding.path), "Prey's Y in play is Prey's Y in menus");
            }
        }
        Check(Has(profile, Role::move, "/user/hand/left/input/thumbstick") &&
              Has(profile, Role::turn, "/user/hand/right/input/thumbstick") &&
              Has(profile, Role::navigate, "/user/hand/right/input/thumbstick") &&
              Has(profile, Role::scroll, "/user/hand/left/input/thumbstick"),
              "the left stick moves and scrolls, the right one turns and navigates");
        Check(Has(profile, Role::jump, "/user/hand/right/input/a/click") &&
              Has(profile, Role::confirm, "/user/hand/right/input/a/click") &&
              Has(profile, Role::crouch, "/user/hand/right/input/b/click") &&
              Has(profile, Role::back, "/user/hand/right/input/b/click"),
              "A and B on the right: jump and crouch in play, select and back in menus");
        Check(Has(profile, Role::psychoscope, "/user/hand/right/input/thumbstick/click"),
              "the right stick's click is the psychoscope on every controller, as on Prey's gamepad");
    }

    const auto& touch = Profile("touch");
    Check(touch.paths.size() == 1 && std::strcmp(touch.paths[0], "/interaction_profiles/oculus/touch_controller") == 0 &&
          touch.extension == nullptr, "Touch: a core profile");
    Check(Has(touch, Role::grab, "/user/hand/left/input/x/click") &&
          Has(touch, Role::secondary, "/user/hand/left/input/x/click") &&
          Has(touch, Role::special, "/user/hand/left/input/y/click") &&
          Has(touch, Role::tertiary, "/user/hand/left/input/y/click") &&
          Has(touch, Role::inventory, "/user/hand/left/input/thumbstick/click") && Count(touch, Role::sprint) == 0 &&
          Has(touch, Role::pause, "/user/hand/left/input/menu/click"),
          "Touch: X and Y are Prey's; the left stick's click opens the TranScribe, so it does not sprint; Menu on the left");

    const auto& index = Profile("index");
    Check(index.paths.size() == 1 && std::strcmp(index.paths[0], "/interaction_profiles/valve/index_controller") == 0 &&
          index.extension == nullptr, "Index: a core profile");
    Check(Has(index, Role::grab, "/user/hand/left/input/a/click") &&
          Has(index, Role::secondary, "/user/hand/left/input/a/click") &&
          Has(index, Role::special, "/user/hand/left/input/b/click") &&
          Has(index, Role::tertiary, "/user/hand/left/input/b/click") &&
          Has(index, Role::inventory, "/user/hand/left/input/trackpad/force") &&
          Has(index, Role::pause, "/user/hand/left/input/thumbstick/click") && Count(index, Role::sprint) == 0,
          "Index: the left A/B stand in for X/Y, the left trackpad opens the TranScribe, and the left stick's click "
          "opens pause, so it does not sprint");

    for (const auto* profile : {&touch, &index}) {
        for (const Role role : kDpadAndView) { Check(Count(*profile, role) == 0, "no D-pad or View to bind"); }
        Check(Count(*profile, Role::weaponWheel) == 0, "no bumper: Prey's own Y-hold is the wheel");
    }

    const auto& frame = Profile("frame");
    Check(frame.paths.size() == 2 && std::strcmp(frame.paths[0], "/interaction_profiles/valve/frame_controller_valve") == 0 &&
          std::strcmp(frame.paths[1], "/interaction_profiles/valve/frame_controller") == 0,
          "the Steam Frame under its current name, then the one SteamVR used before 2.15.1");
    Check(frame.extension != nullptr && std::strcmp(frame.extension, "XR_VALVE_frame_controller_interaction") == 0,
          "the Steam Frame's profile needs its extension");
    Check(Has(frame, Role::grab, "/user/hand/right/input/x/click") &&
          Has(frame, Role::secondary, "/user/hand/right/input/x/click") &&
          Has(frame, Role::special, "/user/hand/right/input/y/click") &&
          Has(frame, Role::tertiary, "/user/hand/right/input/y/click") &&
          Has(frame, Role::sprint, "/user/hand/left/input/thumbstick/click"),
          "the Frame's own X and Y are Prey's X and Y; its left stick sprints");
    Check(Has(frame, Role::torch, "/user/hand/left/input/dpad_up/click") &&
          Has(frame, Role::quickSlotDown, "/user/hand/left/input/dpad_down/click") &&
          Has(frame, Role::quickSlotLeft, "/user/hand/left/input/dpad_left/click") &&
          Has(frame, Role::quickSlotRight, "/user/hand/left/input/dpad_right/click"),
          "the Frame's D-pad is Prey's in play: quick slots, and up for the torch");
    Check(Has(frame, Role::pause, "/user/hand/right/input/menu/click") &&
          Has(frame, Role::inventory, "/user/hand/left/input/view/click") &&
          Has(frame, Role::exit, "/user/hand/left/input/view/click"),
          "Menu pauses; View opens the TranScribe and closes a menu, as on a gamepad");
    Check(Has(frame, Role::navigateUp, "/user/hand/left/input/dpad_up/click") &&
          Has(frame, Role::navigateDown, "/user/hand/left/input/dpad_down/click") &&
          Has(frame, Role::navigateLeft, "/user/hand/left/input/dpad_left/click") &&
          Has(frame, Role::navigateRight, "/user/hand/left/input/dpad_right/click"),
          "the Frame's left D-pad navigates");
    Check(Has(frame, Role::weaponWheel, "/user/hand/right/input/bumper/click"), "the right bumper holds the PC wheel");
    for (const auto& binding : frame.bindings) {
        Check(std::strstr(binding.path, "bumper") == nullptr ||
                  (binding.role == Role::weaponWheel && std::strstr(binding.path, "/right/") != nullptr),
              "and the bumpers are otherwise left unbound");
    }

    // Hysteresis: down at the press point, up only below the release point.
    Check(!Pressed(false, 0.49f, kTriggerPress) && Pressed(false, 0.5f, kTriggerPress), "a trigger presses at half travel");
    Check(Pressed(true, 0.45f, kTriggerPress) && !Pressed(true, 0.39f, kTriggerPress),
          "and stays down until it is let back past the release point");
    Check(!Pressed(false, 0.54f, kGripPress) && Pressed(false, 0.55f, kGripPress) && Pressed(true, 0.46f, kGripPress) &&
          !Pressed(true, 0.44f, kGripPress), "a grip the same way, at its own points");
    Check(!Pressed(true, std::nanf(""), kTriggerPress) && !Pressed(false, INFINITY, kGripPress),
          "a non-finite value is never a press");
    std::cout << "Controller profiles: named actions in three sets, one button each, each controller's own components\n";
    return 0;
}
