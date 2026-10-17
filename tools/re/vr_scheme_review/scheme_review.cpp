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
int rejectedKey = 0;
int failures = 0;

namespace preyvr::lifecycle { void Log(std::string_view) {} }
namespace preyvr::dll {
// The input boundary: one tracking publication in, posted keys out.
bool TryGetTrackingFrame(TrackingFrame& out) {
    if (!focused) { return false; }
    out = tracked;
    out.publishedNs = MonotonicNanoseconds();
    return true;
}
bool HudGameplayInputAllowed() { return true; }
bool InputPostDrivingThisThread() { return true; }
DWORD SetInputPostEnabled(unsigned int) { return 0; }
DWORD PostRawInputImmediate(int key, unsigned int, int value) {
    if (key == rejectedKey) { return 4; }
    posted.push_back({key, value});
    return 0;
}
// No hook is installed; Reset marks the lane installed instead.
bool EnsureMinHook() { return false; }
// The rest of the game at rest: no recentre or snap, no other lane holding a
// hand, nothing to use or carry.
DWORD RecenterHeadTracking(bool) { return 0; }
unsigned long long HeadTrackingReferenceGeneration() { return 0; }
float HeadTrackingReferenceYaw() { return 0; }
DWORD QueueSnapTurn(int, unsigned) { return 0; }
void ApplyQueuedSnapTurn(bool) {}
bool BodyHeadOut() { return false; }
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
}

void Check(bool ok, const char* name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    failures += !ok;
}
void Reset() {
    using namespace preyvr::dll;
    focused = true;
    rejectedKey = 0;
    tracked = {};
    posted.clear();
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
bool ZeroFor(int key) {
    for (const auto& e : posted) { if (e.key == key && e.value == 0) return true; }
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
