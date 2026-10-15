// Each controller's bindings (ControllerProfiles): one module per device's
// specifics, every binding set complete, device-neutral roles throughout.
#include "preyvr/ControllerProfiles.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <utility>

using namespace preyvr::input;

namespace {
void Check(bool ok, const char* why)
{
    if (!ok) { std::cerr << why << '\n'; std::exit(1); }
}
bool Has(const BindingSet& set, Role role, const std::string& path)
{
    for (const auto& binding : set.bindings) {
        if (binding.role == role && path == binding.path) { return true; }
    }
    return false;
}
bool Binds(const BindingSet& set, Role role)
{
    for (const auto& binding : set.bindings) {
        if (binding.role == role) { return true; }
    }
    return false;
}
const ControllerProfile& Profile(const char* name)
{
    for (const auto& profile : ControllerProfiles()) {
        if (std::strcmp(profile.name, name) == 0) { return profile; }
    }
    Check(false, "a controller profile is missing");
    std::abort();
}
constexpr Role kNavigation[] = {Role::navigateUp, Role::navigateDown, Role::navigateLeft, Role::navigateRight};
} // namespace

int main()
{
    Check(ControllerProfiles().size() == 3, "Touch, Index and the Steam Frame");
    // **Every set is complete**: a runtime takes a profile's accepted set as all
    // it binds, so a set missing a pose would be a controller with no hand.
    for (const auto& profile : ControllerProfiles()) {
        Check(!profile.attempts.empty(), "every profile has a binding set");
        for (const auto& set : profile.attempts) {
            for (const std::string hand : {"left", "right"}) {
                const std::pair<Role, const char*> shared[] = {
                    {Role::gripPose, "grip/pose"}, {Role::aimPose, "aim/pose"}, {Role::thumbstick, "thumbstick"},
                    {Role::trigger, "trigger/value"}, {Role::squeeze, "squeeze/value"}};
                for (const auto& [role, component] : shared) {
                    Check(Has(set, role, "/user/hand/" + hand + "/input/" + component),
                          "every set binds both hands' poses, sticks, triggers and grips");
                }
                Check(Has(set, Role::haptic, "/user/hand/" + hand + "/output/haptic"), "and both hands' haptics");
            }
            for (const Role role : {Role::accept, Role::cancel, Role::start, Role::weaponWheel}) {
                Check(Binds(set, role), "and accept, cancel, start and the weapon wheel");
            }
            Check(Has(set, Role::weaponWheel, "/user/hand/right/input/thumbstick/click"),
                  "the right stick's click is the weapon wheel on every controller");
            std::set<std::pair<int, std::string>> seen;
            for (const auto& binding : set.bindings) {
                Check(std::strncmp(binding.path, "/user/hand/", 11) == 0, "every path is a hand's");
                Check(seen.insert({static_cast<int>(binding.role), binding.path}).second, "no binding twice in a set");
            }
        }
    }

    const auto& touch = Profile("touch");
    Check(std::strcmp(touch.path, "/interaction_profiles/oculus/touch_controller") == 0 && touch.extension == nullptr &&
          touch.attempts.size() == 1, "Touch: a core profile, one set");
    const auto& t = touch.attempts.front();
    Check(Has(t, Role::accept, "/user/hand/right/input/a/click") && Has(t, Role::cancel, "/user/hand/right/input/b/click") &&
          Has(t, Role::accept, "/user/hand/left/input/x/click") && Has(t, Role::cancel, "/user/hand/left/input/y/click") &&
          Has(t, Role::start, "/user/hand/left/input/menu/click"), "Touch: A/B on the right, X/Y and Menu on the left");

    const auto& index = Profile("index");
    Check(std::strcmp(index.path, "/interaction_profiles/valve/index_controller") == 0 && index.extension == nullptr &&
          index.attempts.size() == 1, "Index: a core profile, one set");
    const auto& i = index.attempts.front();
    Check(Has(i, Role::accept, "/user/hand/left/input/a/click") && Has(i, Role::cancel, "/user/hand/left/input/b/click") &&
          Has(i, Role::start, "/user/hand/left/input/thumbstick/click") &&
          !Has(i, Role::start, "/user/hand/left/input/menu/click"),
          "Index: the left A/B stand in for X/Y, and the left stick's click opens pause");

    for (const auto* profile : {&touch, &index}) {
        for (const Role role : kNavigation) {
            Check(!Binds(profile->attempts.front(), role), "a controller without navigation buttons binds none");
        }
    }

    const auto& frame = Profile("frame");
    Check(std::strcmp(frame.path, "/interaction_profiles/valve/frame_controller") == 0 &&
          frame.extension != nullptr && std::strcmp(frame.extension, "XR_VALVE_frame_controller_interaction") == 0,
          "the Steam Frame's profile needs its extension");
    Check(frame.attempts.size() == 8, "four guesses at its Menu button, with and without right X/Y as B");
    Check(frame.attempts.front().name == "menu=view_and_menu,right_xy_as_b" && frame.attempts.back().name == "menu=left_menu",
          "tried in order of fidelity");
    for (const auto& set : frame.attempts) {
        Check(Has(set, Role::navigateUp, "/user/hand/left/input/dpad_up/click") &&
              Has(set, Role::navigateDown, "/user/hand/left/input/dpad_down/click") &&
              Has(set, Role::navigateLeft, "/user/hand/left/input/dpad_left/click") &&
              Has(set, Role::navigateRight, "/user/hand/left/input/dpad_right/click"),
              "the Frame's left D-pad navigates the mod's panels");
        Check(Has(set, Role::accept, "/user/hand/left/input/dpad_down/click") &&
              Has(set, Role::cancel, "/user/hand/left/input/dpad_up/click") &&
              Has(set, Role::cancel, "/user/hand/left/input/dpad_left/click") &&
              Has(set, Role::cancel, "/user/hand/left/input/dpad_right/click"),
              "and is still Touch's X and Y for the game, as SteamVR's emulation placed them");
    }
    Check(Has(frame.attempts.front(), Role::cancel, "/user/hand/right/input/x/click") &&
          !Has(frame.attempts.back(), Role::cancel, "/user/hand/right/input/x/click"),
          "right X/Y as B is tried first, then left out");
    std::cout << "Controller profiles: complete binding sets, device-neutral roles, each controller's own buttons\n";
    return 0;
}
