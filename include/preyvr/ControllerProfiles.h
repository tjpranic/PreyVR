#pragma once
#include <string>
#include <vector>

namespace preyvr::input {
// **What the mod asks of a controller**, device-neutral. Each role is one
// OpenXR action (XrInput); each controller binds the roles to its own
// components below. Gameplay and the mod's panels read the roles, never a
// device's own buttons.
enum class Role : unsigned char {
    gripPose, aimPose, thumbstick, trigger, squeeze, haptic,
    accept, cancel, start, weaponWheel,
    // Navigation for the mod's own panels, beside the right stick: a
    // controller with buttons for it binds them (TrackingFrame::navigate).
    navigateUp, navigateDown, navigateLeft, navigateRight,
    count
};
struct Binding { Role role; const char* path; };
// One complete suggestion. A runtime accepts or refuses a profile's bindings
// whole, and the set it accepts is the profile it uses -- so a set is never
// partial: each names everything the controller is bound for.
struct BindingSet { std::string name; std::vector<Binding> bindings; };
// **One controller's specifics, kept together**: its interaction profile, the
// instance extension it needs (none for core profiles), and its binding sets
// in order of fidelity -- the first the runtime accepts is used.
struct ControllerProfile {
    const char* name;
    const char* path;
    const char* extension;
    std::vector<BindingSet> attempts;
};
// Every controller the mod binds: Meta Touch, Valve Index, the Steam Frame.
const std::vector<ControllerProfile>& ControllerProfiles();
}
