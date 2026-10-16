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

using input::Role;
XrActionSet gActionSet = XR_NULL_HANDLE;
// **One action per role** (ControllerProfiles.h): what the mod asks of every
// controller, named for what it does. Each controller's profile binds them.
std::array<XrAction, static_cast<std::size_t>(Role::count)> gActions{};
XrAction& Action(Role role) { return gActions[static_cast<std::size_t>(role)]; }
// Grip and aim are separate on purpose: grip is the palm, which a held weapon
// hangs off; aim is the pointing axis, which a ray follows. Haptics and
// navigation are optional: without them the session runs as before.
struct RoleAction { Role role; XrActionType type; const char* name; const char* label; bool required; };
constexpr RoleAction kRoleActions[] = {
    {Role::gripPose, XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip Pose", true},
    {Role::aimPose, XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim Pose", true},
    {Role::thumbstick, XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick", true},
    {Role::trigger, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", true},
    {Role::squeeze, XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Squeeze", true},
    {Role::haptic, XR_ACTION_TYPE_VIBRATION_OUTPUT, "feedback", "Controller Feedback", false},
    {Role::accept, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu_accept", "Menu Accept", true},
    {Role::cancel, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu_cancel", "Menu Cancel", true},
    {Role::start, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu_start", "Menu Start", true},
    {Role::weaponWheel, XR_ACTION_TYPE_BOOLEAN_INPUT, "weapon_wheel", "Weapon Wheel", true},
    {Role::navigateUp, XR_ACTION_TYPE_BOOLEAN_INPUT, "navigate_up", "Navigate Up", false},
    {Role::navigateDown, XR_ACTION_TYPE_BOOLEAN_INPUT, "navigate_down", "Navigate Down", false},
    {Role::navigateLeft, XR_ACTION_TYPE_BOOLEAN_INPUT, "navigate_left", "Navigate Left", false},
    {Role::navigateRight, XR_ACTION_TYPE_BOOLEAN_INPUT, "navigate_right", "Navigate Right", false},
};
static_assert(std::size(kRoleActions) == static_cast<std::size_t>(Role::count), "one action per role");
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

XrAction CreateAction(XrActionType type, const char* name, const char* localised,
                      const std::array<XrPath, 2>& subactions)
{
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = type;
    std::strncpy(info.actionName, name, sizeof(info.actionName) - 1);
    std::strncpy(info.localizedActionName, localised, sizeof(info.localizedActionName) - 1);
    info.countSubactionPaths = static_cast<std::uint32_t>(subactions.size());
    info.subactionPaths = subactions.data();
    XrAction action = XR_NULL_HANDLE;
    if (XR_FAILED(xrCreateAction(gActionSet, &info, &action))) {
        LogResult(name, XR_ERROR_RUNTIME_FAILURE);
        return XR_NULL_HANDLE;
    }
    return action;
}

} // namespace

bool CreateXrInput(void* instanceHandle, void* sessionHandle)
{
    const auto instance = static_cast<XrInstance>(instanceHandle);
    const auto session = static_cast<XrSession>(sessionHandle);
    if (instance == XR_NULL_HANDLE || session == XR_NULL_HANDLE) {
        return false;
    }

    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strncpy(setInfo.actionSetName, "preyvr", sizeof(setInfo.actionSetName) - 1);
    std::strncpy(setInfo.localizedActionSetName, "PreyVR",
                 sizeof(setInfo.localizedActionSetName) - 1);
    setInfo.priority = 0;
    XrResult result = xrCreateActionSet(instance, &setInfo, &gActionSet);
    if (XR_FAILED(result)) {
        LogResult("create_action_set", result);
        return false;
    }

    const std::array<XrPath, 2> subactions = {
        StringToPath(instance, "/user/hand/left"),
        StringToPath(instance, "/user/hand/right"),
    };
    gHands[0].subactionPath = subactions[0];
    gHands[1].subactionPath = subactions[1];
    gHapticSession=session;
    for (const auto& role : kRoleActions) {
        Action(role.role) = CreateAction(role.type, role.name, role.label, subactions);
        if (role.required && Action(role.role) == XR_NULL_HANDLE) {
            Log("result=failed step=create_actions");
            return false;
        }
    }

    // **Each controller's bindings, from its own profile** (ControllerProfiles).
    // A profile that needs an instance extension is suggested only when the
    // instance enabled it. A role whose optional action could not be created is
    // left out of every set; each set stays complete for the rest. The first set
    // a runtime accepts is the profile's; if none is, that controller is not
    // bound (for the Steam Frame, SteamVR's Touch emulation then carries on).
    bool haveProfile = false;
    std::string profiles;
    for (const auto& profile : input::ControllerProfiles()) {
        if (profile.extension != nullptr && !ExtensionEnabled(profile.extension)) { continue; }
        bool bound = false;
        for (const auto& set : profile.attempts) {
            std::vector<XrActionSuggestedBinding> bindings;
            for (const auto& binding : set.bindings) {
                if (Action(binding.role) != XR_NULL_HANDLE) {
                    bindings.push_back({Action(binding.role), StringToPath(instance, binding.path)});
                }
            }
            XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            suggestion.interactionProfile = StringToPath(instance, profile.path);
            suggestion.suggestedBindings = bindings.data();
            suggestion.countSuggestedBindings = static_cast<std::uint32_t>(bindings.size());
            result = xrSuggestInteractionProfileBindings(instance, &suggestion);
            if (XR_SUCCEEDED(result)) {
                Log(std::string("result=0 detail=bindings profile=") + profile.name + " set=" + set.name);
                bound = true;
                break;
            }
        }
        if (!bound) {
            LogResult((std::string(profile.name) + "_bindings").c_str(), result);
            continue;
        }
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
    attach.countActionSets = 1;
    attach.actionSets = &gActionSet;
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
}

void UpdateXrInput(void* sessionHandle, void* spaceHandle, long long predictedDisplayTime,
                   const Pose& headPose, const PoseValidity& headValidity)
{
    TrackingFrame frame{};
    frame.head = headPose;
    frame.headValidity = headValidity;
    frame.displayTime = predictedDisplayTime;
    ControllerState rightState{};
    bool leftStart = false;
    if (!gCreated.load(std::memory_order_acquire)) {
        gTrackingEpoch.fetch_add(1);
        gTrackingFrame.Clear();
        return;
    }
    const auto session = static_cast<XrSession>(sessionHandle);
    const auto space = static_cast<XrSpace>(spaceHandle);

    XrActiveActionSet active{gActionSet, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    // **XR_SESSION_NOT_FOCUSED is a SUCCESS code, and that matters here.**
    // Checking only XR_FAILED counts an unfocused sync as an effective one, so
    // the counter reads healthy while every action is inactive and no pose can
    // locate. That is exactly what happened on 2026-09-04: 277 syncs, zero hands
    // located, and the cause was the headset sitting on a desk rather than being
    // worn. The counter said the input layer was working; it was reporting that
    // the call returned, not that it did anything.
    const XrResult syncResult = xrSyncActions(session, &sync);
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

        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.subactionPath = subaction;

        get.action = Action(Role::thumbstick);
        XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (XR_SUCCEEDED(xrGetActionStateVector2f(session, &get, &stick)) && stick.isActive) {
            state.thumbstickX = stick.currentState.x;
            state.thumbstickY = stick.currentState.y;
        }
        get.action = Action(Role::trigger);
        XrActionStateFloat analog{XR_TYPE_ACTION_STATE_FLOAT};
        if (XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &analog)) && analog.isActive) {
            state.triggerValue = analog.currentState;
            // Half travel is the conventional trip point, and it is applied here
            // rather than relied on from the runtime so the threshold is one
            // known number instead of a per-runtime behaviour.
            state.triggerPressed = analog.currentState >= 0.5f;
        }
        XrActionStateBoolean button{XR_TYPE_ACTION_STATE_BOOLEAN};
        get.action = Action(Role::squeeze);
        XrActionStateFloat squeeze{XR_TYPE_ACTION_STATE_FLOAT};
        if (XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &squeeze)) && squeeze.isActive) {
            state.gripPressed = std::isfinite(squeeze.currentState) && squeeze.currentState >= .55f;
            state.squeezeValue = std::isfinite(squeeze.currentState) ? squeeze.currentState : 0.0f;
        }
        get.action = Action(Role::accept);
        if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &button)) && button.isActive) {
            state.menuAccept = button.currentState == XR_TRUE;
        }
        get.action = Action(Role::cancel);
        if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &button)) && button.isActive) {
            state.menuCancel = button.currentState == XR_TRUE;
        }
        get.action = Action(Role::start);
        if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &button)) && button.isActive) {
            state.menuStart = button.currentState == XR_TRUE;
        }
        get.action = Action(Role::weaponWheel);
        if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &button)) && button.isActive) {
            state.weaponWheelPressed = button.currentState == XR_TRUE;
        }

        if (state.gripValidity.orientationValid || state.aimValidity.orientationValid) {
            gLocated[hand].fetch_add(1, std::memory_order_relaxed);
        }
        frame.hands[hand] = state;
        if (hand == static_cast<int>(Hand::right)) {
            rightState = state;
        } else {
            leftStart = state.menuStart;
        }
    }

    // **The mod's panels navigate with one device-neutral input**: the right
    // stick, plus a controller's own navigation buttons from either hand.
    {
        XrActionStateGetInfo any{XR_TYPE_ACTION_STATE_GET_INFO};
        const auto held = [&](Role role) {
            XrActionStateBoolean pressed{XR_TYPE_ACTION_STATE_BOOLEAN};
            any.action = Action(role);
            return any.action != XR_NULL_HANDLE && XR_SUCCEEDED(xrGetActionStateBoolean(session, &any, &pressed)) &&
                   pressed.isActive && pressed.currentState == XR_TRUE;
        };
        const float x = rightState.thumbstickX + (held(Role::navigateRight) ? 1.0f : 0.0f) -
                        (held(Role::navigateLeft) ? 1.0f : 0.0f);
        const float y = rightState.thumbstickY + (held(Role::navigateUp) ? 1.0f : 0.0f) -
                        (held(Role::navigateDown) ? 1.0f : 0.0f);
        frame.navigate = {std::clamp(x, -1.0f, 1.0f), std::clamp(y, -1.0f, 1.0f)};
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
        menu.stickX = rightState.thumbstickX;
        menu.stickY = rightState.thumbstickY;
        menu.accept = rightState.menuAccept;
        menu.cancel = rightState.menuCancel;
        // Short menu taps / long options holds share one tested owner.
        menu.start = !VrOptionsReady()&&(rightState.menuStart||leftStart);
        const auto& left = frame.hands[0];
        // Paired grips suppress tab clicks in ModalMenuNavigator. When left Y
        // completes recenter, consume it until release as well.
        menu.previousTab = left.gripPressed;
        menu.nextTab = rightState.gripPressed;
        menu.secondary = left.menuAccept;
        static bool recenterYConsumed=false;
        if(left.gripPressed&&rightState.gripPressed&&left.menuCancel) recenterYConsumed=true;
        if(!left.menuCancel) recenterYConsumed=false;
        menu.tertiary = left.menuCancel&&!recenterYConsumed;
        menu.previousPage=left.triggerPressed && UiPointerHand()!=0;
        menu.nextPage=rightState.triggerPressed && UiPointerHand()!=1;

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
    XrActiveActionSet active{gActionSet, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    // XR_SESSION_NOT_FOCUSED is a success code with every action inactive.
    if (xrSyncActions(session, &sync) != XR_SUCCESS) { return false; }
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
    pressed = XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &trigger)) && trigger.isActive &&
              trigger.currentState >= 0.5f;
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
    if (gActionSet != XR_NULL_HANDLE) {
        xrDestroyActionSet(gActionSet);
        gActionSet = XR_NULL_HANDLE;
    }
    gActions.fill(XR_NULL_HANDLE); // destroyed with the action set
    gCreated.store(false, std::memory_order_release);
    gSyncs.store(0, std::memory_order_relaxed);
    gSyncsNotFocused.store(0, std::memory_order_relaxed);
    for (auto& count : gLocated) {
        count.store(0, std::memory_order_relaxed);
    }
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
