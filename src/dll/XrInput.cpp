#include "XrInput.h"
#include "CarryLane.h"
#include "HapticsXr.h"
#include "VrOptionsRuntime.h"
#include "UiPointer.h"
#include "preyvr/ControllerProfiles.h"
#include "preyvr/LatestSnapshot.h"

#include "InputPost.h"

#include "preyvr/MenuNavigator.h"

#include "HudBridge.h"

#include "Logger.h"

// Core OpenXR only. This file touches no graphics API, so it deliberately does
// not define XR_USE_GRAPHICS_API_D3D11 -- doing so makes openxr_platform.h
// reference D3D11 types and demand <d3d11.h> for no benefit here.
#include <openxr/openxr.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_xr_input " + line);
}

void LogResult(const char* step, XrResult result)
{
    std::ostringstream line;
    line << "result=failed step=" << step << " code=" << static_cast<int>(result);
    Log(line.str());
}

struct HandActions {
    XrPath subactionPath = XR_NULL_PATH;
    XrSpace gripSpace = XR_NULL_HANDLE;
    XrSpace aimSpace = XR_NULL_HANDLE;
};

using input::ActionSet;
using input::Role;
// **Three action sets** (ControllerProfiles.h): the hands and system, play, and
// menus -- one page each of SteamVR's binding UI. All are attached and all are
// synced every frame; each consumer keeps its own context gate.
constexpr std::size_t kSetCount = static_cast<std::size_t>(ActionSet::count);
std::array<XrActionSet, kSetCount> gSets{};
// **One action per role**: what the mod asks of every controller, named for
// what it does. Each controller's profile binds them. Grip and aim are
// separate on purpose: grip is the palm, which a held weapon hangs off; aim is
// the pointing axis, which a ray follows. Optional actions (haptics, the
// Frame's D-pad and View) may be missing: the session runs without them.
std::array<XrAction, input::kRoleCount> gActions{};
XrAction& Action(Role role) { return gActions[static_cast<std::size_t>(role)]; }
XrActionType TypeOf(input::Kind kind)
{
    switch (kind) {
    case input::Kind::pose: return XR_ACTION_TYPE_POSE_INPUT;
    case input::Kind::button: return XR_ACTION_TYPE_BOOLEAN_INPUT;
    case input::Kind::analog: return XR_ACTION_TYPE_FLOAT_INPUT;
    case input::Kind::stick: return XR_ACTION_TYPE_VECTOR2F_INPUT;
    case input::Kind::haptic: return XR_ACTION_TYPE_VIBRATION_OUTPUT;
    case input::Kind::touch: return XR_ACTION_TYPE_BOOLEAN_INPUT;
    }
    return XR_ACTION_TYPE_BOOLEAN_INPUT;
}
// The buttons under a thumb, whichever hand has them (ControllerState::thumbButton).
constexpr Role kThumbButtons[] = {Role::jump, Role::crouch, Role::inventory, Role::special, Role::confirm,
                                  Role::back, Role::secondary, Role::tertiary, Role::exit};
// The sticks, for each hand's own (ControllerState::thumbstickX/Y).
constexpr Role kSticks[] = {Role::move, Role::turn, Role::navigate, Role::scroll};
// The controller profiles' extensions the instance enabled (SetXrInputEnabledExtensions).
std::mutex gExtensionsMutex;
std::vector<std::string> gEnabledExtensions;
bool ExtensionEnabled(const char* name)
{
    std::lock_guard lock(gExtensionsMutex);
    return std::find(gEnabledExtensions.begin(), gEnabledExtensions.end(), name) != gEnabledExtensions.end();
}
XrSession gHapticSession = XR_NULL_HANDLE;
std::array<HandActions, 2> gHands{};

std::atomic<bool> gCreated{false};
std::atomic<bool> gMenuNavigation{false};
// **Gated on a menu actually being open, and on by default.** The navigator
// posts D-pad taps; in gameplay Prey's D-pad is the weapon quick-select, so an
// ungated navigator switches weapons every time the right stick moves. A wearer
// reported exactly that -- forward torch, back GLOO, left and right the wrench
// and pistol. Switchable so the ungated behaviour can be reproduced rather than
// only remembered.
std::atomic<bool> gMenuNavGate{true};
std::atomic<unsigned long long> gMenuSuppressed{0};
std::atomic<long long> gLastDisplayTime{0};
std::atomic<unsigned long long> gMenuActions{0};
// One navigator, touched only from the frame service that owns UpdateXrInput.
input::ModalMenuRouter gNavigator;
std::atomic<bool> gResetNavigator{false};
std::atomic<unsigned long long> gSyncs{0};
// Counted separately, because "the session is not focused" and "the controllers
// are not tracking" have different fixes -- put the headset on, versus pick the
// controllers up.
std::atomic<unsigned long long> gSyncsNotFocused{0};
std::array<std::atomic<unsigned long long>, 2> gLocated{};
// Each hand's trigger and grip, and fire, grab and reload, as last pressed
// (input::Pressed), for their hysteresis. Touched only by UpdateXrInput's
// frame service.
std::array<bool, 2> gTriggerDown{}, gGripDown{};
bool gFireDown = false, gPsiDown = false, gReloadDown = false;
// What each action is bound to, by the runtime's names (XrInputBoundControls).
std::mutex gBoundMutex;
std::vector<XrBoundControl> gBound;
unsigned gBoundGeneration = 0;

LatestSnapshot<TrackingFrame> gTrackingFrame;
std::atomic<std::uint64_t> gTrackingSequence{0};
std::atomic<std::uint64_t> gTrackingEpoch{1};

Pose FromXrPose(const XrPosef& pose)
{
    return Pose{
        Quaternion{pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w},
        Vec3{pose.position.x, pose.position.y, pose.position.z}};
}

PoseValidity ValidityFrom(XrSpaceLocationFlags flags)
{
    PoseValidity validity{};
    validity.orientationValid = (flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
    validity.positionValid = (flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
    validity.orientationTracked = (flags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
    validity.positionTracked = (flags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
    return validity;
}

XrPath StringToPath(XrInstance instance, const char* text)
{
    XrPath path = XR_NULL_PATH;
    if (XR_FAILED(xrStringToPath(instance, text, &path))) {
        return XR_NULL_PATH;
    }
    return path;
}

XrAction CreateAction(const input::Action& role, const std::array<XrPath, 2>& subactions)
{
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = TypeOf(role.kind);
    std::strncpy(info.actionName, role.name, sizeof(info.actionName) - 1);
    std::strncpy(info.localizedActionName, role.label, sizeof(info.localizedActionName) - 1);
    info.countSubactionPaths = static_cast<std::uint32_t>(subactions.size());
    info.subactionPaths = subactions.data();
    XrAction action = XR_NULL_HANDLE;
    const XrResult result = xrCreateAction(gSets[static_cast<std::size_t>(role.set)], &info, &action);
    if (XR_FAILED(result)) {
        LogResult(role.name, result);
        return XR_NULL_HANDLE;
    }
    return action;
}

// All three sets, every frame (ControllerProfiles.h says why).
XrResult SyncAll(XrSession session)
{
    std::array<XrActiveActionSet, kSetCount> active{};
    for (std::size_t i = 0; i < kSetCount; ++i) { active[i] = {gSets[i], XR_NULL_PATH}; }
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = static_cast<std::uint32_t>(active.size());
    sync.activeActionSets = active.data();
    return xrSyncActions(session, &sync);
}

} // namespace

bool CreateXrInput(void* instanceHandle, void* sessionHandle)
{
    const auto instance = static_cast<XrInstance>(instanceHandle);
    const auto session = static_cast<XrSession>(sessionHandle);
    if (instance == XR_NULL_HANDLE || session == XR_NULL_HANDLE) {
        return false;
    }

    XrResult result = XR_SUCCESS;
    for (std::size_t i = 0; i < kSetCount; ++i) {
        // One priority for all three: a button bound in two sets (A is jump
        // and select) reaches both actions, and its context picks the reader.
        const auto& set = input::SetInfo(static_cast<ActionSet>(i));
        XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
        std::strncpy(setInfo.actionSetName, set.name, sizeof(setInfo.actionSetName) - 1);
        std::strncpy(setInfo.localizedActionSetName, set.label, sizeof(setInfo.localizedActionSetName) - 1);
        setInfo.priority = 0;
        result = xrCreateActionSet(instance, &setInfo, &gSets[i]);
        if (XR_FAILED(result)) {
            LogResult("create_action_set", result);
            return false;
        }
    }

    const std::array<XrPath, 2> subactions = {
        StringToPath(instance, "/user/hand/left"),
        StringToPath(instance, "/user/hand/right"),
    };
    gHands[0].subactionPath = subactions[0];
    gHands[1].subactionPath = subactions[1];
    gHapticSession=session;
    for (const auto& role : input::Actions()) {
        Action(role.role) = CreateAction(role, subactions);
        if (role.required && Action(role.role) == XR_NULL_HANDLE) {
            Log("result=failed step=create_actions");
            return false;
        }
    }

    // **Each controller's bindings, from its own profile** (ControllerProfiles).
    // A profile that needs an instance extension is suggested only when the
    // instance enabled it. A role whose optional action could not be created is
    // left out; the set stays complete for the rest. It is suggested under each
    // of the profile's names, and a runtime refuses the ones it does not know;
    // if it refuses them all, that controller is not bound (for the Steam
    // Frame, SteamVR's Touch emulation then carries on).
    bool haveProfile = false;
    std::string profiles;
    for (const auto& profile : input::ControllerProfiles()) {
        if (profile.extension != nullptr && !ExtensionEnabled(profile.extension)) { continue; }
        std::vector<XrActionSuggestedBinding> bindings;
        for (const auto& binding : profile.bindings) {
            if (Action(binding.role) != XR_NULL_HANDLE) {
                bindings.push_back({Action(binding.role), StringToPath(instance, binding.path)});
            }
        }
        bool bound = false;
        for (const char* path : profile.paths) {
            XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            suggestion.interactionProfile = StringToPath(instance, path);
            suggestion.suggestedBindings = bindings.data();
            suggestion.countSuggestedBindings = static_cast<std::uint32_t>(bindings.size());
            result = xrSuggestInteractionProfileBindings(instance, &suggestion);
            std::ostringstream line;
            if (XR_SUCCEEDED(result)) {
                line << "result=0 detail=bindings profile=" << profile.name << " path=" << path;
                bound = true;
            } else {
                line << "result=failed step=bindings profile=" << profile.name << " path=" << path
                     << " code=" << static_cast<int>(result);
            }
            Log(line.str());
        }
        if (!bound) { continue; }
        haveProfile = true;
        profiles += (profiles.empty() ? "" : ",") + std::string(profile.name);
    }
    if (!haveProfile) { return false; }

    for (std::size_t hand = 0; hand < 2; ++hand) {
        XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.poseInActionSpace.orientation.w = 1.0f;
        spaceInfo.subactionPath = gHands[hand].subactionPath;

        spaceInfo.action = Action(Role::gripPose);
        if (XR_FAILED(xrCreateActionSpace(session, &spaceInfo, &gHands[hand].gripSpace))) {
            Log("result=failed step=create_grip_space");
            return false;
        }
        spaceInfo.action = Action(Role::aimPose);
        if (XR_FAILED(xrCreateActionSpace(session, &spaceInfo, &gHands[hand].aimSpace))) {
            Log("result=failed step=create_aim_space");
            return false;
        }
    }

    // **Once per session, and only here.** OpenXR permits this exactly once, so
    // input cannot be armed later from a console call -- it is created with the
    // session or not at all.
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = static_cast<std::uint32_t>(gSets.size());
    attach.actionSets = gSets.data();
    result = xrAttachSessionActionSets(session, &attach);
    if (XR_FAILED(result)) {
        LogResult("attach_action_sets", result);
        return false;
    }

    gCreated.store(true, std::memory_order_release);
    // Which profiles were bound, not which one the runtime picked: that
    // arrives later as an interaction profile event (LogXrInputProfiles).
    Log("result=0 detail=input_created profiles=" + profiles);
    return true;
}

void LogXrInputProfiles(void* instanceHandle, void* sessionHandle)
{
    const auto instance = static_cast<XrInstance>(instanceHandle);
    const auto session = static_cast<XrSession>(sessionHandle);
    if (instance == XR_NULL_HANDLE || session == XR_NULL_HANDLE) { return; }
    std::string line = "result=0 detail=interaction_profile";
    for (const char* hand : {"left", "right"}) {
        XrInteractionProfileState state{XR_TYPE_INTERACTION_PROFILE_STATE};
        const std::string user = std::string("/user/hand/") + hand;
        line += std::string(" ") + hand + "=";
        if (XR_FAILED(xrGetCurrentInteractionProfile(session, StringToPath(instance, user.c_str()), &state)) ||
            state.interactionProfile == XR_NULL_PATH) {
            line += "none";
            continue;
        }
        char name[XR_MAX_PATH_LENGTH]{};
        std::uint32_t length = 0;
        line += XR_SUCCEEDED(xrPathToString(instance, state.interactionProfile, XR_MAX_PATH_LENGTH, &length, name))
            ? std::string(name) : std::string("unknown");
    }
    Log(line);

    // **What each action is bound to now, by the runtime's own names**: the
    // controller the runtime picked, and any rebinding in SteamVR, decide it,
    // not the mod's suggestions (VR Options' Controls tab).
    std::vector<XrBoundControl> bound;
    bool any = false;
    for (const auto& role : input::Actions()) {
        XrBoundControl control{role.role, {}};
        const XrAction action = Action(role.role);
        XrBoundSourcesForActionEnumerateInfo info{XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO};
        info.action = action;
        std::uint32_t count = 0;
        if (action != XR_NULL_HANDLE && XR_SUCCEEDED(xrEnumerateBoundSourcesForAction(session, &info, 0, &count, nullptr)) &&
            count > 0 && count < 64) {
            std::vector<XrPath> sources(count);
            if (XR_SUCCEEDED(xrEnumerateBoundSourcesForAction(session, &info, count, &count, sources.data()))) {
                sources.resize(count);
                for (const XrPath source : sources) {
                    XrInputSourceLocalizedNameGetInfo nameInfo{XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO};
                    nameInfo.sourcePath = source;
                    nameInfo.whichComponents =
                        XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT | XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT;
                    std::uint32_t size = 0;
                    std::string text;
                    if (XR_SUCCEEDED(xrGetInputSourceLocalizedName(session, &nameInfo, 0, &size, nullptr)) && size > 1 &&
                        size < 256) {
                        text.resize(size);
                        if (XR_SUCCEEDED(xrGetInputSourceLocalizedName(session, &nameInfo, size, &size, text.data()))) {
                            text.resize(std::strlen(text.c_str()));
                        } else {
                            text.clear();
                        }
                    }
                    char path[XR_MAX_PATH_LENGTH]{};
                    std::uint32_t length = 0;
                    if (text.empty() && XR_SUCCEEDED(xrPathToString(instance, source, XR_MAX_PATH_LENGTH, &length, path))) {
                        text = path;
                    }
                    if (!text.empty() &&
                        std::find(control.sources.begin(), control.sources.end(), text) == control.sources.end()) {
                        control.sources.push_back(text);
                    }
                }
            }
        }
        std::string names;
        for (const auto& source : control.sources) { names += (names.empty() ? "" : " | ") + source; }
        Log(std::string("result=0 detail=bound action=") + role.name + " sources=\"" + names + "\"");
        any = any || !control.sources.empty();
        bound.push_back(std::move(control));
    }
    if (!any) { return; }   // no profile yet: keep what was read before
    std::lock_guard lock(gBoundMutex);
    gBound = std::move(bound);
    ++gBoundGeneration;
}

bool XrInputBoundControls(std::vector<XrBoundControl>& out, unsigned& generation)
{
    std::lock_guard lock(gBoundMutex);
    out = gBound;
    generation = gBoundGeneration;
    return !gBound.empty();
}

void UpdateXrInput(void* sessionHandle, void* spaceHandle, long long predictedDisplayTime,
                   const Pose& headPose, const PoseValidity& headValidity)
{
    TrackingFrame frame{};
    frame.head = headPose;
    frame.headValidity = headValidity;
    frame.displayTime = predictedDisplayTime;
    if (!gCreated.load(std::memory_order_acquire)) {
        gTrackingEpoch.fetch_add(1);
        gTrackingFrame.Clear();
        return;
    }
    const auto session = static_cast<XrSession>(sessionHandle);
    const auto space = static_cast<XrSpace>(spaceHandle);

    // **XR_SESSION_NOT_FOCUSED is a SUCCESS code, and that matters here.**
    // Checking only XR_FAILED counts an unfocused sync as an effective one, so
    // the counter reads healthy while every action is inactive and no pose can
    // locate. That is exactly what happened on 2026-09-04: 277 syncs, zero hands
    // located, and the cause was the headset sitting on a desk rather than being
    // worn. The counter said the input layer was working; it was reporting that
    // the call returned, not that it did anything.
    const XrResult syncResult = SyncAll(session);
    if (XR_FAILED(syncResult)) {
        ResetHaptics(session,Action(Role::haptic),{gHands[0].subactionPath,gHands[1].subactionPath});
        gTrackingEpoch.fetch_add(1);
        gTrackingFrame.Clear();
        return;
    }
    if (syncResult == XR_SESSION_NOT_FOCUSED) {
        ResetHaptics(session,Action(Role::haptic),{gHands[0].subactionPath,gHands[1].subactionPath});
        gSyncsNotFocused.fetch_add(1, std::memory_order_relaxed);
        gTrackingEpoch.fetch_add(1);
        gTrackingFrame.Clear();
        return;   // never retain a tracked snapshot after focus loss
    }
    gSyncs.fetch_add(1, std::memory_order_relaxed);

    // An action's state from one hand (its subaction path) or from either
    // (XR_NULL_PATH). Inactive -- unbound there, or optional and missing -- is
    // nothing pressed and nothing moved.
    const auto button = [&](Role role, XrPath subaction) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = Action(role);
        get.subactionPath = subaction;
        XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
        return get.action != XR_NULL_HANDLE && XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &state)) &&
               state.isActive && state.currentState == XR_TRUE;
    };
    const auto analog = [&](Role role, XrPath subaction, bool& active) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = Action(role);
        get.subactionPath = subaction;
        XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
        active = XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &state)) && state.isActive;
        return active && std::isfinite(state.currentState) ? state.currentState : 0.0f;
    };
    const auto stick = [&](Role role, XrPath subaction, bool& active) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = Action(role);
        get.subactionPath = subaction;
        XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
        active = XR_SUCCEEDED(xrGetActionStateVector2f(session, &get, &state)) && state.isActive;
        return active ? Navigation{state.currentState.x, state.currentState.y} : Navigation{};
    };

    for (std::size_t hand = 0; hand < 2; ++hand) {
        ControllerState state{};
        const XrPath subaction = gHands[hand].subactionPath;

        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        XrSpaceVelocity velocity{XR_TYPE_SPACE_VELOCITY};
        location.next = &velocity;
        if (XR_SUCCEEDED(xrLocateSpace(gHands[hand].gripSpace, space,
                                       static_cast<XrTime>(predictedDisplayTime), &location))) {
            state.gripPose = FromXrPose(location.pose);
            state.gripValidity = ValidityFrom(location.locationFlags);
            constexpr XrSpaceVelocityFlags both = XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
            if ((velocity.velocityFlags & both) == both) {
                state.gripVelocityValid = true;
                state.gripLinearVelocity = {velocity.linearVelocity.x, velocity.linearVelocity.y, velocity.linearVelocity.z};
                state.gripAngularVelocity = {velocity.angularVelocity.x, velocity.angularVelocity.y,
                                             velocity.angularVelocity.z};
            }
        }
        XrSpaceLocation aimLocation{XR_TYPE_SPACE_LOCATION};
        if (XR_SUCCEEDED(xrLocateSpace(gHands[hand].aimSpace, space,
                                       static_cast<XrTime>(predictedDisplayTime), &aimLocation))) {
            state.aimPose = FromXrPose(aimLocation.pose);
            state.aimValidity = ValidityFrom(aimLocation.locationFlags);
        }

        // This hand's stick: the first stick action bound on this hand.
        for (const Role role : kSticks) {
            bool active = false;
            const Navigation value = stick(role, subaction, active);
            if (active) { state.thumbstickX = value.x; state.thumbstickY = value.y; break; }
        }
        bool active = false;
        state.triggerValue = analog(Role::trigger, subaction, active);
        // Half travel is the conventional trip point, and it is applied here
        // rather than relied on from the runtime so the threshold is one known
        // number instead of a per-runtime behaviour. It lets go a little lower,
        // so a finger resting at the trip point cannot fire.
        state.triggerPressed = active && input::Pressed(gTriggerDown[hand], state.triggerValue, input::kTriggerPress);
        gTriggerDown[hand] = state.triggerPressed;
        state.squeezeValue = analog(Role::squeeze, subaction, active);
        state.gripPressed = active && input::Pressed(gGripDown[hand], state.squeezeValue, input::kGripPress);
        gGripDown[hand] = state.gripPressed;
        for (const Role role : kThumbButtons) { state.thumbButton = state.thumbButton || button(role, subaction); }
        state.thumbTouch = button(Role::thumbTouch, subaction);
        state.indexTouch = button(Role::indexTouch, subaction);

        if (state.gripValidity.orientationValid || state.aimValidity.orientationValid) {
            gLocated[hand].fetch_add(1, std::memory_order_relaxed);
        }
        frame.hands[hand] = state;
    }

    // **The named actions, from whichever hand binds them.**
    {
        bool active = false;
        auto& play = frame.play;
        play.move = stick(Role::move, XR_NULL_PATH, active);
        play.turn = stick(Role::turn, XR_NULL_PATH, active);
        play.fire = analog(Role::fire, XR_NULL_PATH, active);
        play.firePressed = active && input::Pressed(gFireDown, play.fire, input::kTriggerPress);
        play.psi = analog(Role::psi, XR_NULL_PATH, active);
        play.psiPressed = active && input::Pressed(gPsiDown, play.psi, input::kTriggerPress);
        play.reload = analog(Role::reload, XR_NULL_PATH, active);
        play.reloadPressed = active && input::Pressed(gReloadDown, play.reload, input::kGripPress);
        gFireDown = play.firePressed;
        gPsiDown = play.psiPressed;
        gReloadDown = play.reloadPressed;
        play.grabPressed = button(Role::grab, XR_NULL_PATH);
        play.jump = button(Role::jump, XR_NULL_PATH);
        play.crouch = button(Role::crouch, XR_NULL_PATH);
        play.inventory = button(Role::inventory, XR_NULL_PATH);
        play.special = button(Role::special, XR_NULL_PATH);
        play.weaponWheel = button(Role::weaponWheel, XR_NULL_PATH);
        play.sprint = button(Role::sprint, XR_NULL_PATH);
        play.psychoscope = button(Role::psychoscope, XR_NULL_PATH);
        play.quickSlotLeft = button(Role::quickSlotLeft, XR_NULL_PATH);
        play.quickSlotRight = button(Role::quickSlotRight, XR_NULL_PATH);
        play.quickSlotDown = button(Role::quickSlotDown, XR_NULL_PATH);
        play.torch = button(Role::torch, XR_NULL_PATH);

        // **The mod's panels and the game's menus navigate with one
        // device-neutral input**: the navigate stick, plus a controller's own
        // navigation buttons.
        auto& menu = frame.menu;
        const Navigation navigate = stick(Role::navigate, XR_NULL_PATH, active);
        const float x = navigate.x + (button(Role::navigateRight, XR_NULL_PATH) ? 1.0f : 0.0f) -
                        (button(Role::navigateLeft, XR_NULL_PATH) ? 1.0f : 0.0f);
        const float y = navigate.y + (button(Role::navigateUp, XR_NULL_PATH) ? 1.0f : 0.0f) -
                        (button(Role::navigateDown, XR_NULL_PATH) ? 1.0f : 0.0f);
        menu.navigate = {std::clamp(x, -1.0f, 1.0f), std::clamp(y, -1.0f, 1.0f)};
        menu.scroll = stick(Role::scroll, XR_NULL_PATH, active);
        menu.confirm = button(Role::confirm, XR_NULL_PATH);
        menu.back = button(Role::back, XR_NULL_PATH);
        menu.secondary = button(Role::secondary, XR_NULL_PATH);
        menu.tertiary = button(Role::tertiary, XR_NULL_PATH);
        menu.exit = button(Role::exit, XR_NULL_PATH);

        frame.system.pause = button(Role::pause, XR_NULL_PATH);
    }
    frame.sequence = gTrackingSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    frame.epoch = gTrackingEpoch.load();
    frame.publishedNs = MonotonicNanoseconds();
    gTrackingFrame.Publish(frame);
    RecordCarryMotion(frame);
    ServiceHaptics(session,Action(Role::haptic),{gHands[0].subactionPath,gHands[1].subactionPath},frame,true);
    if((gMenuNavigation.load()||VrOptionsInputOwned())&&VrOptionsReady()&&ProcessVrOptionsInput(frame)) {
        gNavigator.Reset();gLastDisplayTime.store(predictedDisplayTime);
        return;
    }

    // --- controller-driven menus ---------------------------------------------
    //
    // The delta comes from the runtime's own predicted display times rather than
    // a wall clock, so the repeat behaviour follows the frames the player is
    // actually seeing. A first frame, or a backwards or absurd step, contributes
    // nothing rather than a guess.
    if (gMenuNavigation.load(std::memory_order_acquire)) {
        // Read as a value sampled by the main thread; this runs on the frame
        // service and must not enter Scaleform to ask.
        if (gResetNavigator.exchange(false)) { gNavigator.Reset(); }
        const auto menuEpoch=HudMenuEpoch();
        const bool modal = !gMenuNavGate.load(std::memory_order_acquire) ||
                           (HudMenuStateKnown() && HudMenuIsOpen());
        if (!modal) { gMenuSuppressed.fetch_add(1, std::memory_order_relaxed); }
        float delta = 0.0f;
        const long long previous = gLastDisplayTime.exchange(predictedDisplayTime,
                                                             std::memory_order_acq_rel);
        if (previous != 0 && predictedDisplayTime > previous) {
            const long long elapsed = predictedDisplayTime - previous;
            if (elapsed < 1000000000LL) {   // a second is not a frame
                delta = static_cast<float>(elapsed) / 1.0e9f;
            }
        }

        input::ModalMenuState menu;
        menu.stickX = frame.menu.navigate.x;
        menu.stickY = frame.menu.navigate.y;
        menu.accept = frame.menu.confirm;
        menu.cancel = frame.menu.back;
        const auto& left = frame.hands[0];
        const auto& right = frame.hands[1];
        // Short menu taps / long options holds share one tested owner. Menu
        // with both grips held is the recenter chord, not a pause.
        menu.start = !VrOptionsReady() && frame.system.pause && !(left.gripPressed && right.gripPressed);
        // Paired grips suppress tab clicks in ModalMenuRouter: both grips are a
        // two-handed hold, and with Menu the recenter chord.
        menu.previousTab = left.gripPressed;
        menu.nextTab = right.gripPressed;
        menu.secondary = frame.menu.secondary;
        menu.tertiary = frame.menu.tertiary;
        menu.exit = frame.menu.exit;
        menu.previousPage=left.triggerPressed && UiPointerHand()!=0;
        menu.nextPage=right.triggerPressed && UiPointerHand()!=1;

        input::MenuAction actions[12];
        const unsigned int count = gNavigator.Update(menu, modal, delta, actions, 12);
        for (unsigned int i = 0; i < count; ++i) {
            // Enqueued, not posted: the engine's input walk belongs to the main
            // thread and this runs on the frame service.
            PostMenuAction(static_cast<unsigned int>(actions[i]),actions[i]==input::MenuAction::Start?0:menuEpoch);
            gMenuActions.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

bool LocateHeldPointer(void* sessionHandle, void* spaceHandle, long long displayTime, Hand hand,
                       Pose& aim, bool& pressed)
{
    const auto index = static_cast<std::size_t>(hand);
    if (!gCreated.load(std::memory_order_acquire) || index >= gHands.size()) { return false; }
    const auto session = static_cast<XrSession>(sessionHandle);
    // XR_SESSION_NOT_FOCUSED is a success code with every action inactive.
    if (SyncAll(session) != XR_SUCCESS) { return false; }
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(gHands[index].aimSpace, static_cast<XrSpace>(spaceHandle),
                                static_cast<XrTime>(displayTime), &location))) {
        return false;
    }
    const auto validity = ValidityFrom(location.locationFlags);
    if (!validity.orientationValid || !validity.positionValid) { return false; }
    aim = FromXrPose(location.pose);
    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
    get.subactionPath = gHands[index].subactionPath;
    get.action = Action(Role::trigger);
    XrActionStateFloat trigger{XR_TYPE_ACTION_STATE_FLOAT};
    // The live frame's press and release points and state (UpdateXrInput):
    // both run under the session lock, so a trigger resting between the two
    // does not flip when a hold frame takes over.
    pressed = XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &trigger)) && trigger.isActive &&
              input::Pressed(gTriggerDown[index], trigger.currentState, input::kTriggerPress);
    gTriggerDown[index] = pressed;
    return true;
}

std::vector<const char*> XrInputExtensions()
{
    std::vector<const char*> wanted;
    for (const auto& profile : input::ControllerProfiles()) {
        if (profile.extension != nullptr &&
            std::find_if(wanted.begin(), wanted.end(), [&](const char* name) {
                return std::strcmp(name, profile.extension) == 0;
            }) == wanted.end()) {
            wanted.push_back(profile.extension);
        }
    }
    return wanted;
}

void SetXrInputEnabledExtensions(const std::vector<std::string>& enabled)
{
    std::lock_guard lock(gExtensionsMutex);
    gEnabledExtensions = enabled;
}

void SetMenuNavigationGate(unsigned int enabled)
{
    gMenuNavGate.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=menu_nav_gate value=") + (enabled != 0 ? "1" : "0"));
}

unsigned long long MenuNavigationSuppressedCount()
{
    return gMenuSuppressed.load(std::memory_order_relaxed);
}

void SetMenuNavigation(unsigned int enabled)
{
    const bool on = enabled != 0u;
    gMenuNavigation.store(on, std::memory_order_release);
    if(!on)RequestVrOptions(false);
    gResetNavigator.store(true);
    Log(std::string("result=0 detail=menu_navigation value=") + (on ? "1" : "0"));
}

unsigned long long MenuNavigationActionCount()
{
    return gMenuActions.load(std::memory_order_relaxed);
}

void InvalidateTrackingSamples() { gTrackingEpoch.fetch_add(1); gTrackingFrame.Clear(); }

void DestroyXrInput()
{
    ResetHaptics(gHapticSession,Action(Role::haptic),{gHands[0].subactionPath,gHands[1].subactionPath});
    gHapticSession=XR_NULL_HANDLE;
    for (auto& hand : gHands) {
        if (hand.gripSpace != XR_NULL_HANDLE) xrDestroySpace(hand.gripSpace);
        if (hand.aimSpace != XR_NULL_HANDLE) xrDestroySpace(hand.aimSpace);
        hand = HandActions{};
    }
    for (auto& set : gSets) {
        if (set != XR_NULL_HANDLE) { xrDestroyActionSet(set); }
        set = XR_NULL_HANDLE;
    }
    gActions.fill(XR_NULL_HANDLE); // destroyed with their action sets
    gCreated.store(false, std::memory_order_release);
    gSyncs.store(0, std::memory_order_relaxed);
    gSyncsNotFocused.store(0, std::memory_order_relaxed);
    for (auto& count : gLocated) {
        count.store(0, std::memory_order_relaxed);
    }
    gTriggerDown = {};
    gGripDown = {};
    gFireDown = gPsiDown = gReloadDown = false;
    gTrackingEpoch.fetch_add(1);
    gTrackingFrame.Clear();
}

bool TryGetTrackingFrame(TrackingFrame& out)
{
    if (!gTrackingFrame.TryRead(out)) { return false; }
    const auto now = MonotonicNanoseconds();
    if (!FreshSample(now, out.publishedNs)) { return false; }
    const auto age = now - out.publishedNs;
    out.headValidity.ageNanoseconds = age;
    for (auto& state : out.hands) {
        state.gripValidity.ageNanoseconds = age;
        state.aimValidity.ageNanoseconds = age;
    }
    return true;
}

bool TryGetControllerState(Hand hand, ControllerState& out)
{
    TrackingFrame frame{};
    const auto index = static_cast<std::size_t>(hand);
    if (index >= frame.hands.size() || !TryGetTrackingFrame(frame)) { return false; }
    out = frame.hands[index];
    return true;
}

unsigned long long XrInputSyncCount() { return gSyncs.load(std::memory_order_relaxed); }

unsigned long long XrInputNotFocusedCount()
{
    return gSyncsNotFocused.load(std::memory_order_relaxed);
}

unsigned long long XrInputLocatedCount(Hand hand)
{
    return gLocated[static_cast<std::size_t>(hand)].load(std::memory_order_relaxed);
}

DWORD XrInputCreated() { return gCreated.load(std::memory_order_acquire) ? 1u : 0u; }

} // namespace preyvr::dll
