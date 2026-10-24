// Offline integration review: compile the REAL lane, replace only its external
// input, logger and hook-install boundaries. No game, injection or native calls.
// Exit 1 reports requirements violated by the current production implementation.
#include "../../../src/dll/MoveLane.cpp"
#include "preyvr/XrFrameContract.h"
#include <iostream>
#include <type_traits>
#include <vector>

struct Posted { int key; int value; };
std::vector<Posted> posted;
preyvr::dll::TrackingFrame tracked;
bool focused = true;
bool gameplay = true;     // no menu, and VR Options does not own the controllers
bool wheelOpen = false;   // Prey's weapon wheel (its focusmode map)
int rejectedKey = 0;
int failures = 0;
// ArkPlayerInput as the handlers leave it: a posted axis is stored, unless a
// filter blocks that action (filteredKey) or the gate says otherwise.
struct PlayerInput {
    preyvr::dll::StickAxes axes;
    int filteredKey = 0;
} playerInput;

namespace preyvr::lifecycle { void Log(std::string_view) {} }
namespace preyvr::dll {
// The input boundary: one tracking publication in, posted keys out.
bool TryGetTrackingFrame(TrackingFrame& out) {
    if (!focused) { return false; }
    out = tracked;
    out.publishedNs = MonotonicNanoseconds();
    return true;
}
bool HudGameplayInputAllowed() { return gameplay; }
bool InputPostDrivingThisThread() { return true; }
DWORD SetInputPostEnabled(unsigned int) { return 0; }
DWORD PostRawInputImmediate(int key, unsigned int, int value) {
    if (key == rejectedKey) { return 4; }
    posted.push_back({key, value});
    const float v = locomotion::HeldAxisValue(value);
    if (key == playerInput.filteredKey) {}
    else if (key == locomotion::kKeyThumbLX) { playerInput.axes.moveX = playerInput.axes.gate ? 0.0f : v; }
    else if (key == locomotion::kKeyThumbLY) { playerInput.axes.moveY = playerInput.axes.gate ? 0.0f : v; }
    else if (key == locomotion::kKeyThumbRX && !playerInput.axes.yawBlocked) { playerInput.axes.yaw = -v; }
    return 0;
}
// The read-back's view of the engine (StickReread.h).
bool ReadStickAxes(StickAxes& out) { out = playerInput.axes; return true; }
// No hook is installed; Reset marks the lane installed instead.
bool EnsureMinHook() { return false; }
// The rest of the game at rest: no recentre or snap, no other lane holding a
// hand, nothing to use or carry.
DWORD RecenterHeadTracking(bool) { return 0; }
unsigned long long HeadTrackingReferenceGeneration() { return 0; }
float HeadTrackingReferenceYaw() { return 0; }
DWORD QueueSnapTurn(int, unsigned) { return 0; }
void ApplyQueuedSnapTurn(bool) {}
bool ArmsHolstered() { return false; }
bool HolsterOwnsGrip() { return false; }
void UpdateHolsterInput(const TrackingFrame&, bool) {}
bool MedkitZoneHasLeftHand() { return false; }
bool PsychoscopeZoneHasLeftHand() { return false; }
bool SupportGripNear() { return false; }
bool TwoHandedAimHeld() { return false; }
unsigned PsiTargetMode() { return 0; }
bool UseCarrying() { return false; }
bool UseTargetPresent() { return false; }
bool UseTargetTapOnly() { return false; }
std::uint32_t UseTargetId() { return 0; }
unsigned UseHandLeft() { return 1; }
unsigned UseHoldEnabled() { return 0; }
unsigned CarryHoldToHold() { return 0; }
void NoteGrenadeTrigger(bool) {}
void NoteUseButton(bool, unsigned, bool, bool, unsigned) {}
void RequestCarryRelease() {}
void RequestDrawWeapon() {}
void RequestUseTargetSuppression(bool) {}
bool PreyWheelOpen() { return wheelOpen; }
}

void Check(bool ok, const char* name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    failures += !ok;
}
void Reset() {
    using namespace preyvr::dll;
    focused = true;
    gameplay = true;
    wheelOpen = false;
    rejectedKey = 0;
    tracked = {};
    posted.clear();
    playerInput = {};
    // A refused sample releases at once unless a test asks for the hold.
    SetStickSampleHoldMs(0);
    for (auto& held : gHeldMilli) { held = 0; }
    for (auto& after : gReassertAfterNs) { after = 0; }
    for (unsigned axis = 0; axis < 3; ++axis) { gReasserted[axis] = 0; gReassertRefused[axis] = 0; }
    gMoveSamples.Reset();
    gInstalled = true; // Skip hook installation: this executable has no target.
    SetMoveLaneMode(2);
    SetTurnLaneEnabled(1);
    SetFireLaneEnabled(1);
    // Smooth turn and stick-relative movement: each stick axis is one posted key.
    SetSnapTurnDegrees(0);
    SetHeadRelativeMovement(0);
    gStick = preyvr::locomotion::StickAxis{};
    gStickPolicyHundredths = 15;
    gLastTurnSent = gLastFireSent = 0;
    gTurnPrimed = gFirePrimed = gFireHeld = false;
    gMoveNeutralize = gTurnNeutralize = gFireNeutralize = false;
    gMoveMayBeHeld = false;
}
void Tick() {
    preyvr::dll::UpdateMoveLane();
    preyvr::dll::UpdateTurnAndFireLanes();
}
bool Sent(int key, int value) {
    for (const auto& e : posted) { if (e.key == key && e.value == value) return true; }
    return false;
}
bool ZeroFor(int key) {
    for (const auto& e : posted) { if (e.key == key && e.value == 0) return true; }
    return false;
}
// Anything posted on the stick axes (the fire lane posts its hold every frame).
bool StickSent() {
    for (const auto& e : posted) {
        if (e.key == preyvr::locomotion::kKeyThumbLX || e.key == preyvr::locomotion::kKeyThumbLY ||
            e.key == preyvr::locomotion::kKeyThumbRX) return true;
    }
    return false;
}
void Held() {
    tracked.play.move = {0.8f, 0.6f};
    tracked.play.turn = {0.8f, 0.0f};
    tracked.play.fire = 0.8f;
    tracked.play.firePressed = true;
    Tick();
}
bool forwarded = false;
bool __fastcall AnalogTarget(void* receiver, unsigned int entity, const void* action,
                             int mode, float value) {
    forwarded = entity == 0x1234 && action == &forwarded && mode == 8 && value == 0.625f;
    std::memcpy(static_cast<std::uint8_t*>(receiver) + 0x5C, &value, sizeof(value));
    return true;
}
int main() {
    using namespace preyvr;
    using namespace preyvr::dll;
    using NativeAnalog = bool(__fastcall*)(void*, unsigned int, const void*, int, float);
    Check((std::is_same_v<AnalogHandlerFn, NativeAnalog>),
          "movement hook preserves native five-argument ABI and return");
    std::array<std::uint8_t, 0xB0> inputReceiver{};
    gOriginalX = &AnalogTarget;
    const bool accepted = AnalogXObserved(inputReceiver.data(), 0x1234, &forwarded, 8, 0.625f);
    Check(accepted && forwarded && MoveLaneAxisMilli(0) == 625,
          "hook forwards all arguments and return; telemetry reads AFTER the native handler");

    // The first frame ever is a menu's (ReleaseGate.h PlayResume): a trigger
    // held on it waits for release. One neutral frame first, as in play.
    Reset(); Tick();

    Reset(); Held();
    Check(posted.size() == 5, "positive control: four held axes and digital trigger reach the posting boundary");
    posted.clear(); focused = false; Tick();
    Check(ZeroFor(locomotion::kKeyThumbLX) && ZeroFor(locomotion::kKeyThumbLY) &&
          ZeroFor(locomotion::kKeyThumbRX) && ZeroFor(input::kTriggerR) && ZeroFor(input::kTriggerRButton),
          "focus loss neutralizes all previously held axes");

    Reset(); Held(); posted.clear();
    SetMoveLaneMode(0); SetTurnLaneEnabled(0); SetFireLaneEnabled(0); Tick();
    Check(ZeroFor(locomotion::kKeyThumbLX) && ZeroFor(locomotion::kKeyThumbLY) &&
          ZeroFor(locomotion::kKeyThumbRX) && ZeroFor(input::kTriggerR) && ZeroFor(input::kTriggerRButton),
          "disabling lanes neutralizes all previously held axes");

    Reset();
    tracked.play.move.x = 0.151f;
    tracked.play.turn.x = 0.151f;
    tracked.play.fire = 0.01f;
    Tick();
    posted.clear(); tracked = {}; Tick();
    Check(ZeroFor(locomotion::kKeyThumbLX) && ZeroFor(locomotion::kKeyThumbRX) &&
          ZeroFor(input::kTriggerR), "return to exact zero bypasses change epsilon");

    Reset(); rejectedKey = locomotion::kKeyThumbLY; Held();
    posted.clear(); rejectedKey = 0; focused = false; Tick();
    Check(ZeroFor(locomotion::kKeyThumbLX) && ZeroFor(locomotion::kKeyThumbLY),
          "partially posted movement pair is still neutralized on focus loss");

    Reset(); Held(); SetMoveLaneMode(0); SetTurnLaneEnabled(0); SetFireLaneEnabled(0);
    rejectedKey = locomotion::kKeyThumbLX; Tick();
    posted.clear(); rejectedKey = 0; Tick();
    Check(ZeroFor(locomotion::kKeyThumbLX), "refused neutralization is retried on the frame thread");

    // **The Steam Frame's bumpers in play: Prey's LB/RB inside its wheel only.**
    // Outside it Prey's bumpers lean, so they send nothing; a bumper held as
    // the wheel opens waits for its release; the wheel closing lets it go.
    Reset(); SetInteractionEnabled(1); Tick(); Tick();
    posted.clear(); tracked.play.wheelSuitRepair = true; Tick();
    Check(!Sent(input::kShoulderL, 1000), "the left bumper sends nothing outside Prey's wheel: no lean");
    wheelOpen = true; posted.clear(); Tick();
    Check(!Sent(input::kShoulderL, 1000), "held as the wheel opens, it waits for its release");
    tracked.play.wheelSuitRepair = false; Tick(); posted.clear();
    tracked.play.wheelSuitRepair = true; Tick();
    Check(Sent(input::kShoulderL, 1000), "pressed in the wheel it is Prey's LB: a suit repair kit");
    posted.clear(); wheelOpen = false; Tick();
    Check(ZeroFor(input::kShoulderL), "the wheel closing with it held lets LB go");
    tracked.play.wheelSuitRepair = false; Tick();
    wheelOpen = true; Tick(); posted.clear();
    tracked.play.wheelFood = true; Tick();
    Check(Sent(input::kShoulderR, 1000) && !Sent(input::kMouse3, 1000),
          "the right bumper in the wheel is Prey's RB (food), not the PC wheel");
    tracked.play.wheelFood = false; posted.clear(); Tick();
    Check(ZeroFor(input::kShoulderR), "and is let go");
    SetInteractionEnabled(0); Tick();

    // **The stick the engine keeps** (docs/MOVE-STOP-2026-10-24.md).
    // Held(): move (0.8, 0.6) at full deflection, turn 0.8 past a 0.15 deadzone.
    const int turnMilli = static_cast<int>((0.8f - 0.15f) / 0.85f * 1000.0f);
    Reset(); Held();
    Check(MoveLaneHeldMilli(0) == 800 && MoveLaneHeldMilli(1) == 600 && MoveLaneHeldMilli(2) == turnMilli,
          "the held values are what each lane delivered");
    focused = false; Tick();
    Check(MoveLaneHeldMilli(0) == 0 && MoveLaneHeldMilli(1) == 0 && MoveLaneHeldMilli(2) == 0,
          "released, nothing is held");

    // Fix 2, the read-back: Prey's re-read (or anything else) zeroes the axes.
    Reset(); Held();
    playerInput.axes.moveX = playerInput.axes.moveY = playerInput.axes.yaw = 0.0f;
    posted.clear(); Tick();
    Check(Sent(locomotion::kKeyThumbLX, 800) && Sent(locomotion::kKeyThumbLY, 600) &&
          Sent(locomotion::kKeyThumbRX, turnMilli), "the read-back posts again every axis the engine lost");
    Check(playerInput.axes.moveX == 0.8f && playerInput.axes.moveY == 0.6f &&
          gReasserted[0] == 1 && gReasserted[1] == 1 && gReasserted[2] == 1, "and the engine holds them again");
    posted.clear(); Tick();
    Check(!StickSent(), "a stick the engine holds is not posted again");

    Reset(); Held();
    playerInput.filteredKey = locomotion::kKeyThumbLX; playerInput.axes.moveX = 0.0f;
    posted.clear(); Tick();
    Check(Sent(locomotion::kKeyThumbLX, 800) && gReassertRefused[0] == 1, "an axis a filter blocks is tried once");
    posted.clear(); Tick();
    Check(!Sent(locomotion::kKeyThumbLX, 800), "and not posted again every frame");

    Reset(); Held();
    playerInput.axes.gate = 1; playerInput.axes.moveX = playerInput.axes.moveY = 0.0f;
    playerInput.axes.yawBlocked = true; playerInput.axes.yaw = 0.0f;
    posted.clear(); Tick();
    Check(!StickSent(), "a cinematic gate's zero is the engine's decision");

    // A gap in the controller samples within move.hold keeps walk and turn.
    Reset(); SetStickSampleHoldMs(100); Held();
    posted.clear(); focused = false; Tick();
    Check(!ZeroFor(locomotion::kKeyThumbLX) && !ZeroFor(locomotion::kKeyThumbLY) &&
          !ZeroFor(locomotion::kKeyThumbRX), "a sample gap within move.hold releases nothing");
    focused = true; Tick();
    Check(!StickSent() && MoveLaneHeldMilli(0) == 800, "and the stick carries on with nothing posted");
    Reset(); SetStickSampleHoldMs(1); Held();
    posted.clear(); focused = false; Sleep(10); Tick();
    Check(ZeroFor(locomotion::kKeyThumbLX) && ZeroFor(locomotion::kKeyThumbLY) && ZeroFor(locomotion::kKeyThumbRX),
          "past move.hold the sticks are released");

    // The menu flag for one frame neither releases the sticks nor latches the
    // wait for a centred stick; for two it does both.
    Reset(); Held();
    posted.clear(); gameplay = false; Tick(); gameplay = true; Tick();
    Check(!ZeroFor(locomotion::kKeyThumbLX) && !ZeroFor(locomotion::kKeyThumbRX),
          "a one-frame menu flag releases nothing");
    tracked.play.move = {0.6f, 0.8f}; posted.clear(); Tick();
    Check(Sent(locomotion::kKeyThumbLX, 600), "and the stick still moves the player");
    Reset(); Held();
    posted.clear(); gameplay = false; Tick(); Tick();
    Check(ZeroFor(locomotion::kKeyThumbLX) && ZeroFor(locomotion::kKeyThumbLY) && ZeroFor(locomotion::kKeyThumbRX),
          "a menu up for two frames releases the sticks");
    gameplay = true; posted.clear(); Held();
    Check(!Sent(locomotion::kKeyThumbLX, 800) && !Sent(locomotion::kKeyThumbRX, turnMilli),
          "after it, a stick still pushed waits for centre");
    tracked = {}; Tick(); posted.clear(); Held();
    Check(Sent(locomotion::kKeyThumbLX, 800) && Sent(locomotion::kKeyThumbRX, turnMilli), "centred, it moves again");
    tracked = {}; Tick();

    xrframe::FrameContract contract;
    contract.AllowSingleThreaded(true);
    contract.SetSessionRunning(true);
    contract.OnWaited(100, true, 1);
    contract.OnViewsLocated(100, true, true);
    contract.OnBegun();
    // A resize return in XrSessionHost's frame loop skips OnSubmitted.
    Check(contract.OnWaited(200, true, 1) == xrframe::Result::outOfOrder,
          "positive control: abandoned resize frame rejects the next frame");
    contract.OnSubmitted(1);
    Check(contract.OnWaited(300, true, 1) == xrframe::Result::ok,
          "ending a skipped-copy frame restores frame progression");
    return failures ? 1 : 0;
}
