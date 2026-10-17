#pragma once

#include "preyvr/ControllerProfiles.h"
#include "preyvr/VrMath.h"
#include <array>
#include <string>
#include <vector>

#include <windows.h>

// OpenXR head, grip and aim samples are published together at one predicted
// display time. Grip anchors the wrist; aim supplies the pointing ray. Neither
// is an authored weapon muzzle. Invalid/focus-lost publications are refused.
namespace preyvr::dll {

enum class Hand { left = 0, right = 1 };

// **One hand, as such**: where it is and what its trigger, grip and stick are
// doing, whatever action they are bound to. What the mod reads a hand for --
// gestures, the laser, the foregrip -- rather than a button's meaning, which is
// in the frame's named actions (GameplayInput, MenuInput, SystemInput).
struct ControllerState {
    Pose gripPose{};
    Pose aimPose{};
    PoseValidity gripValidity{};
    PoseValidity aimValidity{};
    // The grip's velocity as the runtime reports it (XrSpaceVelocity), in the
    // tracking space; for throwing (HandCarry.h). Valid only when the runtime
    // set both VALID bits.
    bool gripVelocityValid = false;
    Vec3 gripLinearVelocity{};
    Vec3 gripAngularVelocity{};
    // This hand's stick, from whichever action reads it here (move, turn,
    // navigate, scroll): for diagnostics and the laser hand's push and pull.
    float thumbstickX = 0.0f;
    float thumbstickY = 0.0f;
    // **The trigger is read as a FLOAT, and the press is derived from it.**
    // Binding a boolean action to `/input/trigger/value` leans on the runtime
    // performing the spec's bool-from-float conversion, and it throws away the
    // travel that `xi_triggerr` -- an analog axis -- actually wants. Reading the
    // float and thresholding here keeps both: consumers that want a button get
    // one, and the fire lane can post real analog values.
    float triggerValue = 0.0f;
    bool triggerPressed = false;
    bool gripPressed = false;
    float squeezeValue = 0.0f;
    // A button under this hand's thumb is held (A, B, X, Y, View, whichever it
    // has, whatever it is bound to): a squeeze or trigger meanwhile is
    // incidental, not a gesture.
    bool thumbButton = false;
    // Capacitive touch, for the finger shape (HandPose): the thumb resting on
    // anything under it, the index finger on the trigger.
    bool thumbTouch = false;
    bool indexTouch = false;
};
inline bool FaceButtonHeld(const ControllerState& state) { return state.thumbButton; }

// **Navigation for the mod's own panels and the game's menus**, -1..1 each way:
// the navigate stick, plus whatever a controller binds to navigation
// (input::Role::navigate*: the Steam Frame's left D-pad).
struct Navigation { float x = 0, y = 0; };

// **The named actions** (input::Role), hand-neutral: each is true or moved
// whichever button a controller -- or the player, in SteamVR -- binds it to.
// Every set is read every frame (ControllerProfiles.h); each consumer keeps
// its own context gate, as before.
struct GameplayInput {
    Navigation move{}, turn{};
    float fire = 0.0f, psi = 0.0f, reload = 0.0f;   // analog, with their presses
    bool firePressed = false, psiPressed = false, reloadPressed = false;
    bool grabPressed = false;   // Prey's X
    bool jump = false, crouch = false, inventory = false;
    bool special = false, weaponWheel = false;   // Prey's Y; the PC wheel
    bool sprint = false, psychoscope = false;
    bool quickSlotLeft = false, quickSlotRight = false, quickSlotDown = false, torch = false;
};
struct MenuInput {
    Navigation navigate{};   // the stick and navigation buttons, combined
    Navigation scroll{};
    bool confirm = false, back = false;
    bool secondary = false, tertiary = false;   // Prey's X and Y actions
    bool exit = false;
};
struct SystemInput {
    bool pause = false;  // tap: pause; hold: VR Options; with both grips: reset the view
};

// Head and BOTH hands located at one predicted display time, with the actions
// read at the same sync. This is the publication unit for gameplay;
// independently reading latest poses mixes times.
struct TrackingFrame {
    Pose head{};
    PoseValidity headValidity{};
    std::array<ControllerState, 2> hands{}; // indexed by Hand (left, right)
    GameplayInput play{};
    MenuInput menu{};
    SystemInput system{};
    long long displayTime = 0;
    std::uint64_t sequence = 0;
    std::uint64_t epoch = 0;
    std::uint64_t publishedNs = 0;
};
bool TryGetTrackingFrame(TrackingFrame& out);

// Builds the action sets, suggests bindings and creates the pose spaces. Must run
// **before** the session is attached -- OpenXR permits xrAttachSessionActionSets
// exactly once per session, so this cannot be armed later from a console call.
//
// Returns false and logs rather than throwing; the session then runs without
// input, which is a mod with no hands rather than a mod that fails to start.
bool CreateXrInput(void* instanceHandle, void* sessionHandle);
// The instance extensions the controller profiles need (ControllerProfiles.h),
// for the instance to enable where the runtime offers them; then which ones
// it did, before CreateXrInput, which suggests a profile only when its own is.
std::vector<const char*> XrInputExtensions();
void SetXrInputEnabledExtensions(const std::vector<std::string>& enabled);
// Logs the profile the runtime picked for each hand, and what each action is
// bound to now (XrInputBoundControls). Call on an interaction profile changed
// event; before it, the runtime may report none.
void LogXrInputProfiles(void* instanceHandle, void* sessionHandle);
// **What each action is bound to now**, by the runtime's own names
// (xrGetInputSourceLocalizedName: "Right Hand A Button" and the like) -- the
// controller the runtime picked and any rebinding in SteamVR, not the mod's
// suggestions. In input::Actions() order; empty sources for an unbound action.
// False until a profile has been read. `generation` counts the readings.
struct XrBoundControl {
    input::Role role;
    std::vector<std::string> sources;
};
bool XrInputBoundControls(std::vector<XrBoundControl>& out, unsigned& generation);

// Syncs actions and locates both controllers. Called once per frame from the
// session's own frame service, after xrWaitFrame so the predicted display time is
// the one the poses are located against.
void UpdateXrInput(void* sessionHandle, void* spaceHandle, long long predictedDisplayTime,
                   const Pose& head, const PoseValidity& headValidity);

// **Hold frames only** (XrSessionHost): one hand's aim and whether its trigger
// is pressed, for the laser while Prey renders no frames. Syncs the actions as
// UpdateXrInput does, then publishes no tracking frame and runs no menu
// navigation, haptics or carry record: nothing the stalled game would act on
// later. False when unfocused or the aim is not located.
bool LocateHeldPointer(void* sessionHandle, void* spaceHandle, long long displayTime, Hand hand,
                       Pose& aim, bool& pressed);

// Arms controller-driven menu navigation. Off by default: it posts synthesised
// input into the engine, so it is enabled deliberately like every other write
// this mod performs.
void SetMenuNavigation(unsigned int enabled);
unsigned long long MenuNavigationActionCount();
// Suppresses navigator output unless a menu is actually open. On by default:
// without it the right stick changes weapons during play, because the taps the
// navigator posts are the D-pad and Prey binds the D-pad to quick-select.
// Turn off to reproduce the ungated behaviour deliberately.
void SetMenuNavigationGate(unsigned int enabled);
unsigned long long MenuNavigationSuppressedCount();

void InvalidateTrackingSamples();
void DestroyXrInput();

// Latest state for one hand. Returns false before anything has been published.
bool TryGetControllerState(Hand hand, ControllerState& out);

// Diagnostics, so a session with no hands says which half is missing rather than
// silently doing nothing.
unsigned long long XrInputSyncCount();

// Syncs that returned XR_SESSION_NOT_FOCUSED -- a *success* code, so a plain
// XR_FAILED check counts them as working syncs while every action is inactive.
// A high count here means the session does not have focus, which in practice
// means the headset is not being worn; it is a different problem from controllers
// that are not tracking, and it needs a different fix.
unsigned long long XrInputNotFocusedCount();
unsigned long long XrInputLocatedCount(Hand hand);
DWORD XrInputCreated();

} // namespace preyvr::dll
