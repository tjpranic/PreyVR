#include "MoveLane.h"
#include "SafeMemory.h"
#include "EngineAccess.h"
#include "HookInstall.h"
#include "ArmsLane.h"
#include "AimTakeover.h"
#include "BodyEquipment.h"
#include "ActionMaps.h"
#include "InteractionLane.h"
#include "RoomScaleLane.h"
#include "CarryLane.h"
#include "GrenadeLane.h"
#include "PsiMedkit.h"
#include "Psychoscope.h"

#include "InputPost.h"
#include "HeadTrackingHook.h"
#include "StickReread.h"
#include "UiMode.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "XrInput.h"
#include "preyvr/InputEvent.h"
#include "preyvr/InteractionUse.h"
#include "preyvr/ReleaseGate.h"
#include "preyvr/Locomotion.h"
#include "preyvr/ComfortControls.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/LatestSnapshot.h"

#include <MinHook.h>

#include <sstream>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>

namespace preyvr::dll {
namespace {
std::atomic<unsigned> gSnapDegrees{45};
std::atomic<bool> gHeadRelative{true};
std::atomic<float> gStrafeScale{1.f},gBackwardScale{1.f};

void Log(const std::string& line) { lifecycle::Log("preyvr_move " + line); }

// R-089. The player-side analog handlers. RCX is the input object: the X
// handler stores to `+0x5C`, the Y handler to `+0x60`, and both discard the
// value when the cinematic gate at `+0x94` is set.
//
//   83 b9 94 00 00 00 00   cmp dword ptr [rcx+0x94], 0
//   f3 0f 10 44 24 28      movss xmm0, [rsp+0x28]
//   74 03                  je  +3
//   0f 57 c0               xorps xmm0, xmm0          <- gated: value becomes 0
//   f3 0f 11 41 5c         movss [rcx+0x5C], xmm0
//
// Hooking these rather than reading a guessed player->input pointer chain means
// the engine hands us the object, so nothing is inferred about where it lives.
// The action callback has FIVE arguments including this. The target loads
// value from [rsp+28h] before changing rsp, and returns acceptance in AL.
// Forwarding only this/event would make its value come from unrelated stack data.
using AnalogHandlerFn = bool(__fastcall*)(void*, unsigned int, const void*, int, float);
constexpr std::uintptr_t kAnalogXRva = 0x158FD20;
constexpr std::uintptr_t kAnalogYRva = 0x158FD80;
constexpr std::array<std::uint8_t, 23> kAnalogXPrologue{
    0x83, 0xB9, 0x94, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x44, 0x24,
    0x28, 0x74, 0x03, 0x0F, 0x57, 0xC0, 0xF3, 0x0F, 0x11, 0x41, 0x5C};
constexpr std::array<std::uint8_t, 23> kAnalogYPrologue{
    0x83, 0xB9, 0x94, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x4C, 0x24,
    0x28, 0x74, 0x03, 0x0F, 0x57, 0xC9, 0xF3, 0x0F, 0x10, 0x41, 0x5C};
constexpr std::size_t kMoveX = 0x5C;
constexpr std::size_t kMoveY = 0x60;
constexpr std::size_t kCinematicGate = 0x94;

std::atomic<bool> gInstalled{false};
std::atomic<AnalogHandlerFn> gOriginalX{nullptr}, gOriginalY{nullptr};
void* gTargetX = nullptr;
void* gTargetY = nullptr;

// --- interaction bindings ----------------------------------------------------
//
// **Which XInput button Prey binds to "use" is NOT established here.** The
// binding table lives in the shipped GameData PAKs, which H-018 records as
// unreadable as ordinary ZIPs, so the defaults below are the conventional
// gamepad layout and nothing stronger. They are remappable at runtime precisely
// because of that: `move.bind <slot> <keyId>` and one session in a headset
// settles empirically what could not be read statically -- exactly how the
// trigger was resolved after R-115.
//
// The lesson from that episode is built in: a valid key id that nothing is bound
// to is accepted, posts cleanly, increments every counter and does nothing at
// all. So these counters name presses SENT, never presses that acted.
enum ActionSlot {
    kActionInteract = 0,
    kActionInventory = 1,
    kActionJump = 2,
    kActionCrouch = 3,
    kActionWeaponWheel = 4,
    kActionSpecial = 5,
    kActionSprint = 6,
    kActionPsychoscope = 7,
    kActionQuickSlotLeft = 8,
    kActionQuickSlotRight = 9,
    kActionQuickSlotDown = 10,
    kActionTorch = 11,
    kActionPsi = 12,
    kActionWheelLeft = 13,
    kActionWheelRight = 14,
    kActionCount = 15,
};

struct ActionBinding {
    std::atomic<int> keyId;
    std::atomic<unsigned long long> pressed;
    std::atomic<unsigned long long> released;
    std::atomic<unsigned long long> refused;
    bool held;
};

ActionBinding gActions[kActionCount] = {
    {{input::kButtonX}, {0}, {0}, {0}, false},   // use/reload <- grab, reload (preyvr/InteractionUse.h)
    {{input::kBack}, {0}, {0}, {0}, false},      // inventory <- Touch X, Frame View
    {{input::kButtonA}, {0}, {0}, {0}, false},   // jump      <- right A
    {{input::kButtonB}, {0}, {0}, {0}, false},   // crouch    <- right B
    // Native PC Favorites Wheel shortcut (Bethesda manual); Steam CMouse::Init
    // proves mouse3/0x102. The wheel the mod has always opened, now on its own
    // button, so Y is free to be Prey's.
    {{input::kMouse3}, {0}, {0}, {0}, false},    // wheel     <- unbound (Prey's own wheel is Y held)
    // Prey's own gamepad buttons (the key ids read from this build; which
    // action each drives is logged by ActionMaps in play).
    {{input::kButtonY}, {0}, {0}, {0}, false},   // Prey's Y  <- Y
    {{input::kThumbL}, {0}, {0}, {0}, false},    // sprint    <- left stick click
    {{input::kThumbR}, {0}, {0}, {0}, false},    // psychoscope <- right stick click
    {{input::kDPadLeft}, {0}, {0}, {0}, false},  // quick slot <- Frame D-pad left
    {{input::kDPadRight}, {0}, {0}, {0}, false}, // quick slot <- Frame D-pad right
    {{input::kDPadDown}, {0}, {0}, {0}, false},  // quick slot <- Frame D-pad down
    {{input::kDPadUp}, {0}, {0}, {0}, false},    // torch     <- Frame D-pad up
    {{input::kTriggerLButton}, {0}, {0}, {0}, false}, // psi power <- left trigger
    // Prey's LB/RB inside its weapon wheel (focusmode: use_armor_item,
    // use_food_item). Outside it Prey's bumpers lean, so nothing is sent.
    {{input::kShoulderL}, {0}, {0}, {0}, false}, // suit repair kit <- Frame left bumper, in the wheel
    {{input::kShoulderR}, {0}, {0}, {0}, false}, // food      <- Frame right bumper, in the wheel
};
std::atomic<bool> gActionsEnabled{false};
std::atomic<bool> gActionsNeutralize{false};
// Use/reload: which hand presses the shared button, and whether the selection
// is cleared first. Owned by the frame thread.
use::UseButton gUseButton;
std::atomic<unsigned long long> gUseHoldPosted{0};
// The psi button held, as the psi slot posts it (PsiButtonHeld).
std::atomic<bool> gPsiHeld{false};

std::atomic<unsigned int> gMode{0};
std::atomic<unsigned int> gDeadzoneHundredths{15};
std::atomic<unsigned long long> gOursX{0}, gOursY{0}, gNative{0};
std::atomic<unsigned long long> gPosted{0}, gDropped{0};
std::atomic<unsigned long long> gInputObject{0};
std::atomic<int> gAxisMilli[2]{0, 0};
std::atomic<int> gCinematic{0};

// Owned by the frame thread that calls UpdateMoveLane. Rebuilt only when the
// policy changes: its state is what makes the release case correct, so
// reconstructing it per frame would forget the last value sent and the player
// would keep walking after letting go.
locomotion::StickAxis gStick;
unsigned int gStickPolicyHundredths = 15;
std::atomic<bool> gMoveNeutralize{false};
bool gMoveMayBeHeld = false;

// --- turn and fire, same thread and frame as the move lane ------------------
std::atomic<bool> gTurnEnabled{false};
std::atomic<unsigned int> gTurnDeadzoneHundredths{15};
std::atomic<unsigned int> gTurnScalePercent{100};
std::atomic<unsigned long long> gTurnPosted{0}, gTurnRefused{0};
float gLastTurnSent = 0.0f;
bool gTurnPrimed = false;
std::atomic<bool> gTurnNeutralize{false};

std::atomic<bool> gFireEnabled{false};
std::atomic<unsigned long long> gFirePressed{0}, gFireReleased{0}, gFireRefused{0};
bool gFireHeld = false;
float gLastFireSent = 0.0f;
bool gFirePrimed = false;
std::atomic<bool> gFireNeutralize{false};
// Hold events for the trigger button while it stays pressed (move.firehold).
std::atomic<bool> gFireHold{true};
std::atomic<unsigned long long> gFireHoldPosted{0};

// Recentre-on-both-grips, edge triggered so a held pair fires once.
bool gRecenterHeld = false;
std::atomic<unsigned long long> gRecenters{0};

// --- the stick the engine keeps (docs/MOVE-STOP-2026-10-24.md) ---------------
//
// What the lanes delivered and the engine should hold, in thousandths: move X,
// move Y, yaw. Written by the frame thread, read by StickReread's hook wherever
// Prey re-reads the sticks.
std::atomic<int> gHeldMilli[3]{};
std::atomic<std::uint64_t> gStickPushedNs{0};
std::atomic<unsigned> gSampleHoldMs{100};
locomotion::SampleHold gMoveSamples;   // frame thread
// Once a frame, for both lanes: a menu takes the sticks only after two frames.
// On the flag's first frame the sticks are still play's and the lanes hold.
input::MenuDebounce gMenuDebounce;
bool gSticksPlay = false;
bool gMenuFlagFrame = false;
// Why the move lane let go of a held stick -- each one a stop the player sees.
enum MoveRelease : unsigned { kReleaseOff, kReleaseMenu, kReleaseRead, kReleaseHead, kReleaseRecentre,
                              kReleaseCentre, kReleaseCount };
std::atomic<unsigned long long> gReleases[kReleaseCount]{};
std::atomic<unsigned long long> gBridged{0}, gTurnBridged{0}, gMenuFlickers{0}, gMenuTook{0};
// The read-back's re-posts, per axis: restored, or still refused (filtered),
// which waits a quarter second before the next try.
std::atomic<unsigned long long> gReasserted[3]{}, gReassertRefused[3]{};
std::uint64_t gReassertAfterNs[3]{};   // frame thread
constexpr std::uint64_t kReassertBackoffNs = 250000000ull;
constexpr unsigned long long kReassertLogBudget = 20;
std::atomic<unsigned long long> gReassertLogged{0};
constexpr float kAxisTolerance = 1e-4f;
constexpr int kAxisKeys[3] = {locomotion::kKeyThumbLX, locomotion::kKeyThumbLY, locomotion::kKeyThumbRX};
constexpr const char* kAxisNames[3] = {"x", "y", "yaw"};

std::uint64_t SampleHoldNs() { return gSampleHoldMs.load(std::memory_order_relaxed) * 1000000ull; }

// The move stick's two axes delivered as held: notes the moment it went from
// released to pushed, for room-scale's walk start.
void HoldMove(unsigned axis, int milli)
{
    const bool wasPushed = gHeldMilli[0].load(std::memory_order_relaxed) != 0 ||
                           gHeldMilli[1].load(std::memory_order_relaxed) != 0;
    gHeldMilli[axis].store(milli, std::memory_order_relaxed);
    if (!wasPushed && milli != 0) { gStickPushedNs.store(MonotonicNanoseconds(), std::memory_order_relaxed); }
}

float StoredAxis(const StickAxes& axes, unsigned axis)
{
    return axis == 0 ? axes.moveX : axis == 1 ? axes.moveY : axes.yaw;
}

// What an axis holds when the lane delivered `milli`: the yaw handler stores it
// negated (StickRereadNative.h).
float ExpectedAxis(unsigned axis, int milli)
{
    const float value = locomotion::HeldAxisValue(milli);
    return axis == 2 ? -value : value;
}

// **The read-back** (fix 2): the engine still holds what the lanes delivered.
// A held axis it stores otherwise -- overwritten by a path StickReread does not
// cover -- is posted again, within the frame it was found. One the engine
// still refuses (a filter blocks it) is tried again a quarter second later.
// The cinematic gate, and yaw's own block, are the engine's to decide.
void ReassertHeld(unsigned first, unsigned last)
{
    bool anyHeld = false;
    for (unsigned axis = first; axis <= last; ++axis) {
        anyHeld = anyHeld || gHeldMilli[axis].load(std::memory_order_relaxed) != 0;
    }
    StickAxes stored{};
    if (!anyHeld || !ReadStickAxes(stored)) { return; }
    const std::uint64_t now = MonotonicNanoseconds();
    for (unsigned axis = first; axis <= last; ++axis) {
        const int milli = gHeldMilli[axis].load(std::memory_order_relaxed);
        if (milli == 0 || now < gReassertAfterNs[axis] || (axis < 2 ? stored.gate != 0 : stored.yawBlocked)) {
            continue;
        }
        const float before = StoredAxis(stored, axis);
        const float expected = ExpectedAxis(axis, milli);
        if (std::fabs(before - expected) <= kAxisTolerance) { continue; }
        if (PostRawInputImmediate(kAxisKeys[axis], static_cast<unsigned int>(locomotion::kStateChanged), milli) != 0) {
            continue;
        }
        StickAxes after{};
        const bool restored = ReadStickAxes(after) && std::fabs(StoredAxis(after, axis) - expected) <= kAxisTolerance;
        (restored ? gReasserted[axis] : gReassertRefused[axis]).fetch_add(1, std::memory_order_relaxed);
        if (!restored) { gReassertAfterNs[axis] = now + kReassertBackoffNs; }
        if (gReassertLogged.fetch_add(1, std::memory_order_relaxed) < kReassertLogBudget) {
            Log(std::string("result=0 detail=reassert axis=") + kAxisNames[axis] +
                " storedMilli=" + std::to_string(static_cast<int>(before * 1000.0f)) +
                " heldMilli=" + std::to_string(milli) + " restored=" + (restored ? "1" : "0"));
        }
    }
}

bool ReadFloat(const void* at, float* out)
{
    return SafeRead(at, *out);
}

bool ReadInt(const void* at, int* out)
{
    return SafeRead(at, *out);
}

// Runs inside the engine's own input walk, on whichever thread posted. Reads
// only; the original does the work.
void ObserveHandler(void* inputObject, bool isX)
{
    if (inputObject == nullptr) { return; }
    // Attribution first, because it is the whole point of the hook. Our posts
    // run inside PostInputEvent on the drain thread with the flag set; anything
    // else is the player's physical hardware reaching the same handler.
    if (InputPostDrivingThisThread()) {
        (isX ? gOursX : gOursY).fetch_add(1, std::memory_order_relaxed);
    } else {
        gNative.fetch_add(1, std::memory_order_relaxed);
    }
    gInputObject.store(reinterpret_cast<unsigned long long>(inputObject),
                       std::memory_order_relaxed);
    auto* const bytes = static_cast<const std::uint8_t*>(inputObject);
    float value = 0.0f;
    // Read back what the engine actually stored, rather than trusting that a
    // posted value arrived. R-089 names these offsets; this is where that claim
    // gets checked every frame instead of once.
    if (ReadFloat(bytes + kMoveX, &value)) {
        gAxisMilli[0].store(static_cast<int>(value * 1000.0f), std::memory_order_relaxed);
    }
    if (ReadFloat(bytes + kMoveY, &value)) {
        gAxisMilli[1].store(static_cast<int>(value * 1000.0f), std::memory_order_relaxed);
    }
    int gate = 0;
    if (ReadInt(bytes + kCinematicGate, &gate)) {
        gCinematic.store(gate, std::memory_order_relaxed);
    }
}

bool __fastcall AnalogXObserved(void* inputObject, unsigned int entity,
                               const void* action, int mode, float value)
{
    const AnalogHandlerFn original = gOriginalX.load(std::memory_order_acquire);
    const bool accepted = original != nullptr && original(inputObject, entity, action, mode, value);
    ObserveHandler(inputObject, true);
    return accepted;
}

bool __fastcall AnalogYObserved(void* inputObject, unsigned int entity,
                               const void* action, int mode, float value)
{
    const AnalogHandlerFn original = gOriginalY.load(std::memory_order_acquire);
    const bool accepted = original != nullptr && original(inputObject, entity, action, mode, value);
    ObserveHandler(inputObject, false);
    return accepted;
}

bool InstallOne(std::uintptr_t rva, const std::uint8_t* prologue, std::size_t size,
                void* detour, std::atomic<AnalogHandlerFn>& original, void*& target,
                const char* name)
{
    // Already in place from an attempt whose other hook failed.
    if (original.load(std::memory_order_acquire) != nullptr) { return true; }
    const auto base = PreyBase();
    if (base == 0) { return false; }
    target = reinterpret_cast<void*>(base + rva);
    return hook::HookAt(target, {prologue, size}, detour, original, name, &Log);
}

bool Install()
{
    if (gInstalled.load(std::memory_order_acquire)) { return true; }
    if (!InstallOne(kAnalogXRva, kAnalogXPrologue.data(), kAnalogXPrologue.size(),
                    reinterpret_cast<void*>(&AnalogXObserved), gOriginalX, gTargetX,
                    "analog_x")) {
        return false;
    }
    if (!InstallOne(kAnalogYRva, kAnalogYPrologue.data(), kAnalogYPrologue.size(),
                    reinterpret_cast<void*>(&AnalogYObserved), gOriginalY, gTargetY,
                    "analog_y")) {
        // Leave X installed rather than half-removing: it is passive, and a
        // partial install is visible in the counters.
        Log("result=partial detail=x_installed_y_failed");
        return false;
    }
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=hooks_installed x=0x158FD20 y=0x158FD80");
    return true;
}

} // namespace

DWORD SetMoveLaneMode(unsigned int mode)
{
    if (mode > 2u) { return 1; }
    if (mode != 0u && !Install()) { return 2; }
    if (mode == 2u) {
        // Posting requires the input lane to be armed; without it every event
        // would be queued and silently dropped.
        if (SetInputPostEnabled(1) != 0) {
            Log("result=refused detail=input_post_unavailable");
            return 3;
        }
    }
    if (mode != 2u) { gMoveNeutralize.store(true, std::memory_order_release); }
    gMode.store(mode, std::memory_order_release);
    Log("result=0 detail=mode value=" + std::to_string(mode));
    return 0;
}

DWORD SetMoveLaneDeadzone(unsigned int hundredths)
{
    if (hundredths > 60u) { return 1; }
    gDeadzoneHundredths.store(hundredths, std::memory_order_relaxed);
    Log("result=0 detail=deadzone value=" + std::to_string(hundredths));
    return 0;
}
unsigned MoveLaneDeadzone() { return gDeadzoneHundredths.load(std::memory_order_relaxed); }
bool PsiButtonHeld() { return gPsiHeld.load(std::memory_order_acquire); }

void UpdateMoveLane()
{
    const std::uint64_t now = MonotonicNanoseconds();
    // Once a frame, for both lanes (input::MenuDebounce): a menu flag that has
    // been up for one frame only does not take the sticks -- that used to stop
    // the player until the stick was centred.
    const bool allowed = HudGameplayInputAllowed();
    gSticksPlay = gMenuDebounce.Update(allowed);
    gMenuFlagFrame = !allowed && gSticksPlay;
    if (gMenuDebounce.Flickered()) { gMenuFlickers.fetch_add(1, std::memory_order_relaxed); }
    static bool awaitNeutral=false;
    if (!gSticksPlay) {
        if (!awaitNeutral) { gMenuTook.fetch_add(1, std::memory_order_relaxed); }
        awaitNeutral=true;
    }

    // The move action (the LEFT stick by default) moves, matching
    // `xi_thumblx`/`xi_thumbly` and every mod in the fleet survey.
    Navigation state{};
    const bool active = gMode.load(std::memory_order_acquire) == 2u;
    const bool neutralize = gMoveNeutralize.exchange(false, std::memory_order_acq_rel);
    const auto movementReference=HeadTrackingReferenceGeneration();
    TrackingFrame movementFrame{};
    unsigned release = kReleaseCount;   // none
    bool hold = gMenuFlagFrame;
    if (!active || neutralize) { release = kReleaseOff; hold = false; }
    else if (!gSticksPlay) { release = kReleaseMenu; }
    else if (!hold && !TryGetTrackingFrame(movementFrame)) { release = kReleaseRead; }
    bool haveInput = release == kReleaseCount && !hold;
    if(haveInput) state=movementFrame.play.move;
    if (haveInput && awaitNeutral) {
        if (std::fabs(state.x)<=.15f && std::fabs(state.y)<=.15f) awaitNeutral=false;
        else { haveInput=false; release=kReleaseCentre; }
    }
    if(haveInput && gHeadRelative.load()) {
        const auto yaw=stereo::RecenterYawFromHeadPose(movementFrame.head);
        if(!yaw || !IsPoseUsable(movementFrame.head,movementFrame.headValidity,kMaxPoseAgeNs)) {
            haveInput=false; release=kReleaseHead;
        } else {
            const auto v=comfort::HeadRelativeStick({state.x,state.y},
                *yaw-HeadTrackingReferenceYaw(),gStrafeScale.load(),gBackwardScale.load());
            state.x=v.x;state.y=v.y;
        }
    }
    if(haveInput && !stereo::ReferenceCurrent(movementReference, HeadTrackingReferenceGeneration())) {
        haveInput=false; release=kReleaseRecentre;
    }
    // A short gap in the samples, or in head tracking moving head relative,
    // keeps the stick the engine holds (move.hold): releasing it for a frame
    // and posting it again the next was a one-frame stop.
    if (haveInput) { gMoveSamples.Sampled(now); }
    else if ((release == kReleaseRead || release == kReleaseHead) && gMoveSamples.Bridges(now, SampleHoldNs())) {
        hold = true;
        gBridged.fetch_add(1, std::memory_order_relaxed);
    }
    if (hold) { return; }
    if (!haveInput) {
        if (gMoveMayBeHeld && release < kReleaseCount) { gReleases[release].fetch_add(1, std::memory_order_relaxed); }
        // A partially delivered X/Y pair can leave one axis held even though
        // the candidate shaper was not committed. Release both owned axes and
        // retry refusals; never infer successful delivery from shaper state.
        bool released = true;
        if (gMoveMayBeHeld) {
            for (const unsigned axis : {0u, 1u}) {
                if (PostRawInputImmediate(kAxisKeys[axis], locomotion::kStateChanged, 0) == 0) {
                    gPosted.fetch_add(1, std::memory_order_relaxed);
                    HoldMove(axis, 0);
                } else {
                    released = false;
                    gDropped.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        if (released) { gMoveMayBeHeld = false; gStick.Reset(); HoldMove(0, 0); HoldMove(1, 0); }
        else { gMoveNeutralize.store(true, std::memory_order_release); }
        return;
    }
    const unsigned int hundredths = gDeadzoneHundredths.load(std::memory_order_relaxed);
    if (hundredths != gStickPolicyHundredths) {
        locomotion::StickPolicy policy{};
        policy.deadzone = hundredths / 100.0f;
        // Keep the last successful values when changing the deadzone.
        gStick.SetPolicy(policy);
        gStickPolicyHundredths = hundredths;
    }
    locomotion::AxisEvent events[2]{};
    auto candidate = gStick;
    const unsigned int count = candidate.Update(state.x, state.y, events);
    bool delivered = true;
    for (unsigned int i = 0; i < count; ++i) {
        const int valueMilli = static_cast<int>(events[i].value * 1000.0f);
        // Immediate, not queued: this already runs on the drain thread, and the
        // queue drains one event per frame while a two-axis stick produces two.
        if (PostRawInputImmediate(events[i].keyId, static_cast<unsigned int>(events[i].state),
                                  valueMilli) == 0) {
            if (valueMilli != 0) { gMoveMayBeHeld = true; }
            gPosted.fetch_add(1, std::memory_order_relaxed);
            HoldMove(events[i].keyId == locomotion::kKeyThumbLX ? 0u : 1u, valueMilli);
        } else {
            delivered = false;
            gDropped.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (delivered) {
        gStick = candidate;
        if (gStick.LastSentX() == 0 && gStick.LastSentY() == 0) { gMoveMayBeHeld = false; }
    }
    ReassertHeld(0, 1);
}

void UpdateTurnAndFireLanes()
{
    // A snap queued here last frame that no frame-start seam took -- no player
    // update and no view ran -- is applied now, a frame late, as it used to be.
    ApplyQueuedSnapTurn(true);

    // Read both hands from one publication: separate latest reads can invent a
    // recenter/support chord that was never held at the same instant.
    TrackingFrame inputFrame{};
    const bool haveInput = TryGetTrackingFrame(inputFrame);
    const std::uint64_t now = MonotonicNanoseconds();
    static locomotion::SampleHold turnSamples;
    if (haveInput) { turnSamples.Sampled(now); }
    const ControllerState right = haveInput ? inputFrame.hands[1] : ControllerState{};
    const ControllerState leftHand = haveInput ? inputFrame.hands[0] : ControllerState{};
    // Play's named actions, whichever buttons they are bound to (ControllerProfiles).
    const GameplayInput play = haveInput ? inputFrame.play : GameplayInput{};
    // The recenter chord's button: Menu (pause), whose own actions wait for its
    // release or a long hold, so the chord can claim it (VrOptions' Menu).
    const bool menuHeld = haveInput && inputFrame.system.pause;
    UpdateHolsterInput(inputFrame,haveInput&&gActionsEnabled.load());
    // The frame a menu closes is still the menu's (ReleaseGate.h): the Back
    // that closed the inventory was posted, and the menu sampled as closed,
    // before this ran, so the B that did it must not crouch.
    static input::PlayResume resume;
    const bool menuFrame = resume.Update(HudGameplayInputAllowed());
    // Prey's own weapon wheel (Y held): its focusmode action map is enabled.
    const bool wheelOpen = PreyWheelOpen();

    // Both grips + Menu: both grips alone are a natural two-handed hold.
    // Requiring Menu keeps recenter deliberate and independent of grab timing.
    // Either order recentres; Menu first never reloads (the use button's chord
    // input, below), and the chord neither pauses nor opens VR Options.
    //
    // Edge-triggered: held grips must recentre ONCE, not every frame, or the
    // reference would be rebuilt continuously and the view would never settle.
    {
        const bool both = haveInput && leftHand.gripPressed && right.gripPressed && menuHeld;
        if (both && !gRecenterHeld) {
            // This bumps the head-tracking reference generation, which
            // deliberately invalidates the IK calibration -- the hands were
            // calibrated against the old reference and would be wrong against
            // the new one. Recalibrating after a recentre is expected rather
            // than a fault, and saying so here saves rediscovering it while
            // wearing a headset.
            if (RecenterHeadTracking() == 0) { gRecenters.fetch_add(1, std::memory_order_relaxed); }
        }
        gRecenterHeld = both;
    }

    // The menu state the move lane settled this frame (input::MenuDebounce).
    static bool turnBlocked=false;
    if (!gSticksPlay) turnBlocked=true;
    else if (haveInput && std::fabs(play.turn.x)<=.15f && std::fabs(play.turn.y)<=.15f) turnBlocked=false;
    const bool turnOn = gTurnEnabled.load(std::memory_order_acquire) && gSticksPlay && !turnBlocked;
    const bool releaseTurn = gTurnNeutralize.exchange(false, std::memory_order_acq_rel);
    // The menu flag's first frame, or a short gap in the samples: the engine
    // keeps the turn it holds, as the move lane keeps the walk.
    const bool bridging = !haveInput && turnSamples.Bridges(now, SampleHoldNs());
    const bool turnHold = turnOn && !releaseTurn && (gMenuFlagFrame || bridging);
    if (turnHold && !gMenuFlagFrame) { gTurnBridged.fetch_add(1, std::memory_order_relaxed); }
    static comfort::SnapLatch snap;
    static unsigned previousMode=~0u;
    const unsigned snapDegrees=gSnapDegrees.load();
    const bool modeChanged=snapDegrees!=previousMode;previousMode=snapDegrees;
    // Not updated while holding: a sample missed mid-push would otherwise
    // disarm a snap until the stick came back to centre.
    const int steps=turnHold ? 0 : snap.Update(play.turn.x,play.turn.y,
        turnOn && haveInput && !releaseTurn && !modeChanged && snapDegrees!=0);
    if(steps) {
        // Queued, not applied: this frame's view was built before this hook,
        // against the reference the snap changes (QueueSnapTurn).
        if(QueueSnapTurn(steps,snapDegrees)==0) ++gTurnPosted; else ++gTurnRefused;
    }
    if (!turnHold && (turnOn || (gTurnPrimed && gLastTurnSent != 0))) {
        const float dead = gTurnDeadzoneHundredths.load(std::memory_order_relaxed) / 100.0f;
        const float scale = gTurnScalePercent.load(std::memory_order_relaxed) / 100.0f;
        float value = (turnOn && haveInput && !releaseTurn && snapDegrees==0) ? play.turn.x : 0.0f;
        if (!std::isfinite(value)) { value = 0.0f; }
        // Rescaled past the deadzone rather than clipped, so leaving the dead
        // area is gentle instead of a step to full rate.
        const float magnitude = std::fabs(value);
        value = (magnitude <= dead || dead >= 1.0f)
                    ? 0.0f
                    : ((value > 0.0f) ? 1.0f : -1.0f) * ((magnitude - dead) / (1.0f - dead));
        value *= scale;
        // Emit only on change, and ALWAYS emit the return to zero: the engine
        // keeps turning on the last value it was told until contradicted, which
        // is the same failure the movement lane's release case exists to avoid.
        if (!gTurnPrimed || (value == 0 && gLastTurnSent != 0) ||
            std::fabs(value - gLastTurnSent) > 0.002f) {
            const int milli = static_cast<int>(value * 1000.0f);
            if (PostRawInputImmediate(locomotion::kKeyThumbRX,
                                      locomotion::kStateChanged, milli) == 0) {
                gLastTurnSent = value;
                gTurnPrimed = true;
                gHeldMilli[2].store(milli, std::memory_order_relaxed);
                gTurnPosted.fetch_add(1, std::memory_order_relaxed);
            } else {
                if (releaseTurn) { gTurnNeutralize.store(true, std::memory_order_release); }
                gTurnRefused.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    if (!turnHold) { ReassertHeld(2, 2); }

    static bool awaitTriggerRelease=false;
    // A head through a wall is blacked out (RoomScaleLane.h): no shots from the
    // far side, and a held trigger waits for release after it comes back.
    if (menuFrame || BodyHeadOut()) { awaitTriggerRelease=true; }
    if (haveInput && play.fire<=.05f) { awaitTriggerRelease=false; }
    const bool fireOn = gFireEnabled.load(std::memory_order_acquire) &&
                       HudGameplayInputAllowed() && !awaitTriggerRelease;
    const bool releaseFire = gFireNeutralize.exchange(false, std::memory_order_acq_rel);
    if (fireOn || gFireHeld || (gFirePrimed && gLastFireSent != 0)) {
        // **The real travel is posted, not a quantised press.** `xi_triggerr` is
        // an analog axis, and sending only 0 or 1000 threw away everything
        // between -- which matters for a weapon whose native binding may ramp.
        float value = (fireOn && !releaseFire) ? play.fire : 0.0f;
        if (!std::isfinite(value)) { value = 0.0f; }
        value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        const bool pressed = fireOn && !releaseFire && play.firePressed;
        // Emit on a meaningful change OR on a press-state crossing, so the
        // counters still name a discrete pull while the value stays continuous.
        if (!gFirePrimed ||
            (value == 0 && gLastFireSent != 0) || std::fabs(value - gLastFireSent) > 0.02f) {
            const int milli = static_cast<int>(value * 1000.0f);
            if (PostRawInputImmediate(input::kTriggerR,
                                      locomotion::kStateChanged, milli) == 0) {
                gLastFireSent = value;
                gFirePrimed = true;
            } else {
                if (releaseFire) { gFireNeutralize.store(true, std::memory_order_release); }
                gFireRefused.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // **The axis alone fires nothing, and that cost a headset session.**
        // The trigger is TWO keys: 0x20F is the analog axis and 0x21D is the
        // digital button, and firing is bound to the button (R-115). Posting
        // only the axis was accepted -- 36 presses, zero refusals -- and did
        // nothing at all. A valid key that nothing is bound to is
        // indistinguishable in the counters from one that works, which is
        // exactly what the fail-closed refusal cannot catch.
        //
        // Sent as a press/release EDGE rather than a changed value, because a
        // button posted as "changed" is not a press and the action map wants
        // the transition.
        // **A held trigger must also be posted as HELD.** CryEngine turns a key
        // that stays down into one eIS_Down event per frame from its own list of
        // held symbols, and a posted press never joins that list: the event
        // carries no device symbol. So the action map saw a press and then
        // nothing until the release. Every "while held" action was dead -- the
        // GLOO's automatic fire stopped after one shot (HANDOFF 2026-10-05: "held
        // 1.2 s, ONE shot") and the Disruptor could not charge at all. This
        // posts the hold the engine would have, once per frame.
        if (pressed && gFireHeld && gFireHold.load(std::memory_order_relaxed)) {
            if (PostRawInputImmediate(input::kTriggerRButton, input::kStateDown, 1000) == 0) {
                gFireHoldPosted.fetch_add(1, std::memory_order_relaxed);
            } else {
                gFireRefused.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (pressed != gFireHeld) {
            const unsigned int state = pressed ? static_cast<unsigned int>(input::kStatePressed)
                                               : static_cast<unsigned int>(input::kStateReleased);
            // A grenade leaves the hand at this edge, with the hand's motion as
            // of now (GrenadeLane.h). Noted BEFORE the post: posting runs the
            // game's fire handler -- and the release -- synchronously.
            NoteGrenadeTrigger(pressed);
            if (PostRawInputImmediate(input::kTriggerRButton, state, pressed ? 1000 : 0) == 0) {
                gFireHeld = pressed;
                (pressed ? gFirePressed : gFireReleased)
                    .fetch_add(1, std::memory_order_relaxed);
            } else {
                // A refused RELEASE must be retried, or the weapon keeps firing.
                if (!pressed) { gFireNeutralize.store(true, std::memory_order_release); }
                gFireRefused.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    // --- interaction bindings -------------------------------------------------
    //
    // Suppressed while a menu is open. The navigator owns A/B there, and a
    // button that both confirms a menu choice and jumps is the same class of
    // double-binding as the right stick that also changed weapons.
    const bool actionsOn = gActionsEnabled.load(std::memory_order_acquire) &&
                           HudGameplayInputAllowed();
    const bool releaseActions = gActionsNeutralize.exchange(false, std::memory_order_acq_rel);
    {
        const bool haveLeftHand = haveInput;
        // **Use and reload share the native button (X).** With use.hand left
        // (the default) grab (X) uses what the left hand points at, or reloads
        // with nothing to use, and the right grip only reloads, clearing the
        // selection first; with use.hand 0 the right grip is the button, as before.
        use::ButtonInput useIn{};
        useIn.gameplay = actionsOn && !releaseActions && haveInput && !BodyHeadOut() && !menuFrame;
        useIn.leftHand = UseHandLeft() != 0;
        useIn.leftPress = play.grabPressed;
        useIn.rightPress = play.reloadPressed;
        useIn.chord = menuHeld;
        // The hip slot's trigger is the medkit's, and a hand holding the
        // foregrip is not free. The foregrip region and the psychoscope
        // gesture are the grip's: they stop a press only while the grip is
        // squeezed too -- a press rebound to the grip, or both at once. The
        // same list keeps psi (the left trigger) off the left hand's other jobs.
        const bool leftBusy = gRecenterHeld || MedkitZoneHasLeftHand() || TwoHandedAimHeld() ||
                              (leftHand.gripPressed && (SupportGripNear() || PsychoscopeZoneHasLeftHand()));
        useIn.leftBusy = leftBusy;
        // A paired squeeze is a hold, not use/reload.
        // While something is carried the button drops it: the left grip lets
        // go, and the right one (reload) has nothing to do.
        const bool carrying = UseCarrying();
        useIn.rightBusy = gRecenterHeld || HolsterOwnsGrip() || TwoHandedAimHeld() ||
                          (haveLeftHand && leftHand.squeezeValue >= .45f && !useIn.leftHand) ||
                          (carrying && useIn.leftHand);
        useIn.target = UseTargetPresent() || carrying;
        // Phase 2: held in the hand for as long as the grip is (carry.hold).
        useIn.carrying = carrying;
        useIn.holdToHold = CarryHoldToHold() != 0;
        useIn.inHand = carrying && CarryHoldingInHand();
        useIn.targetId = UseTargetId();
        useIn.tapOnly = UseTargetTapOnly();
        useIn.holstered = ArmsHolstered();
        const use::ButtonOutput useOut = gUseButton.Update(useIn);
        // Prey's psi button, the left trigger: held to aim, let go to use. A
        // press the left hand is busy with waits for its release.
        static input::ReleaseGate psiGate;
        const bool psi = psiGate.Update(play.psiPressed, menuFrame || leftBusy || BodyHeadOut());
        gPsiHeld.store(actionsOn && !releaseActions && haveInput && psi, std::memory_order_release);
        if (useOut.releaseCarry) { RequestCarryRelease(); }
        if (useOut.drawWeapon) { RequestDrawWeapon(); }
        RequestUseTargetSuppression(useOut.suppressTarget);
        NoteUseButton(useOut.down, static_cast<unsigned>(useOut.owner), useOut.pressed, useOut.released,
                      (useOut.noTarget ? 1u : 0u) | (useOut.gaveUp ? 2u : 0u) | (useOut.busy ? 4u : 0u));
        // The rest are Prey's own buttons, mirrored: Prey decides what a tap
        // or a hold of each means.
        const bool sources[kActionCount] = {
            useOut.down,                              // use/reload, decided above
            play.inventory,                           // inventory <- Frame View, Touch left stick click
            play.jump,                                // jump      <- right A
            play.crouch,                              // crouch    <- right B
            play.weaponWheel,                         // wheel     <- unbound (SteamVR rebinding)
            play.special,                             // Prey's Y  <- Y
            play.sprint,                              // sprint    <- left stick click
            play.psychoscope,                         // psychoscope <- right stick click
            play.quickSlotLeft,                       // quick slots <- the Frame's D-pad
            play.quickSlotRight,
            play.quickSlotDown,
            play.torch,                               // torch     <- D-pad up
            psi && PsiTargetMode() == 0,              // psi power <- left trigger, Original targeting
            play.wheelSuitRepair,                     // Prey's LB <- Frame left bumper, in the wheel
            play.wheelFood,                           // Prey's RB <- Frame right bumper, in the wheel
        };
        // A button down in a menu, or as one closes, waits for its release; the
        // wheel's bumpers likewise, pressed outside the wheel or as it closes.
        static input::ReleaseGate gates[kActionCount];
        for (int slot = 0; slot < kActionCount; ++slot) {
            const bool wheelSlot = slot == kActionWheelLeft || slot == kActionWheelRight;
            // Slot 0's grip arbitration lives in the use button above.
            const bool source = slot == kActionInteract ? sources[slot] :
                gates[slot].Update(sources[slot], menuFrame || (wheelSlot && !wheelOpen));
            ActionBinding& binding = gActions[slot];
            const bool wanted = actionsOn && !releaseActions && source;
            if (wanted == binding.held) { continue; }
            const int keyId = binding.keyId.load(std::memory_order_acquire);
            const unsigned int state = wanted ? static_cast<unsigned int>(input::kStatePressed)
                                              : static_cast<unsigned int>(input::kStateReleased);
            if (PostRawInputImmediate(keyId, state, wanted ? 1000 : 0) == 0) {
                binding.held = wanted;
                if(slot==kActionWeaponWheel)Log(std::string("result=0 detail=wheel_input pressed=")+
                    (wanted?"1":"0")+" key="+std::to_string(keyId)+" source=wheel_button");
                (wanted ? binding.pressed : binding.released)
                    .fetch_add(1, std::memory_order_relaxed);
            } else {
                // A refused RELEASE must be retried or the button sticks down --
                // the same hazard the fire lane guards, and worse here because a
                // stuck "use" re-triggers whatever it is pointed at.
                if (!wanted) { gActionsNeutralize.store(true, std::memory_order_release); }
                binding.refused.fetch_add(1, std::memory_order_relaxed);
            }
        }
        // **A held button must also be posted as HELD**, as a pad's is, for the
        // reason the fire lane gives: a posted press never joins the engine's
        // held-symbol list, so Prey's hold actions -- hold to carry (a bin, a
        // body), the psi button's aim, Y's wheel -- would never complete.
        static bool heldLastFrame[kActionCount]{};
        for (int slot = 0; slot < kActionCount; ++slot) {
            ActionBinding& binding = gActions[slot];
            if (binding.held && heldLastFrame[slot] && (slot != kActionInteract || UseHoldEnabled()) &&
                PostRawInputImmediate(binding.keyId.load(std::memory_order_acquire), input::kStateDown, 1000) == 0 &&
                slot == kActionInteract) {
                gUseHoldPosted.fetch_add(1, std::memory_order_relaxed);
            }
            heldLastFrame[slot] = binding.held;
        }
    }

}

DWORD SetTurnLaneEnabled(unsigned int enabled)
{
    if (enabled > 1) { return 1; }
    if (enabled && SetInputPostEnabled(1) != 0) { return 2; }
    gTurnEnabled.store(enabled != 0u, std::memory_order_release);
    if (!enabled) { gTurnNeutralize.store(true, std::memory_order_release); }
    Log(std::string("result=0 detail=turn value=") + (enabled ? "1" : "0"));
    return 0;
}

DWORD SetTurnLaneDeadzone(unsigned int hundredths)
{
    if (hundredths > 60u) { return 1; }
    gTurnDeadzoneHundredths.store(hundredths, std::memory_order_relaxed);
    return 0;
}
unsigned TurnLaneDeadzone() { return gTurnDeadzoneHundredths.load(std::memory_order_relaxed); }

DWORD SetTurnLaneScale(unsigned int percent)
{
    if (percent < 10u || percent > 200u) { return 1; }
    gTurnScalePercent.store(percent, std::memory_order_relaxed);
    return 0;
}
unsigned TurnLaneScalePercent(){return gTurnScalePercent.load();}

DWORD SetFireLaneEnabled(unsigned int enabled)
{
    if (enabled > 1) { return 1; }
    if (enabled && SetInputPostEnabled(1) != 0) { return 2; }
    gFireEnabled.store(enabled != 0u, std::memory_order_release);
    if (!enabled) { gFireNeutralize.store(true, std::memory_order_release); }
    Log(std::string("result=0 detail=fire value=") + (enabled ? "1" : "0"));
    return 0;
}

unsigned int TurnLaneEnabled() { return gTurnEnabled.load(std::memory_order_relaxed) ? 1u : 0u; }
unsigned long long TurnLanePosted() { return gTurnPosted.load(std::memory_order_relaxed); }
unsigned long long TurnLaneRefused() { return gTurnRefused.load(std::memory_order_relaxed); }
unsigned int FireLaneEnabled() { return gFireEnabled.load(std::memory_order_relaxed) ? 1u : 0u; }
unsigned long long FireLanePressed() { return gFirePressed.load(std::memory_order_relaxed); }
unsigned long long FireLaneReleased() { return gFireReleased.load(std::memory_order_relaxed); }
unsigned long long FireLaneRefused() { return gFireRefused.load(std::memory_order_relaxed); }
DWORD SetFireLaneHold(unsigned int enabled)
{
    if (enabled > 1) { return 1; }
    gFireHold.store(enabled != 0u, std::memory_order_relaxed);
    Log(std::string("result=0 detail=fire_hold value=") + (enabled ? "1" : "0"));
    return 0;
}
unsigned int FireLaneHold() { return gFireHold.load(std::memory_order_relaxed) ? 1u : 0u; }
unsigned long long FireLaneHoldPosted() { return gFireHoldPosted.load(std::memory_order_relaxed); }

unsigned int MoveLaneMode() { return gMode.load(std::memory_order_relaxed); }
DWORD SetInteractionEnabled(unsigned int enabled)
{
    const bool on = enabled != 0;
    if (!on) { gActionsNeutralize.store(true, std::memory_order_release); }
    gActionsEnabled.store(on, std::memory_order_release);
    return 0;
}

DWORD SetInteractionBinding(unsigned int slot, int keyId)
{
    if (slot >= kActionCount) { return 2; }
    // Only names read from the target are accepted, so a typo refuses here
    // rather than posting an id nothing can consume (F-011's shape).
    if (input::KeyNameFor(keyId) == nullptr) { return 3; }
    gActions[slot].keyId.store(keyId, std::memory_order_release);
    return 0;
}

std::string InteractionReport()
{
    static const char* const kSlotNames[kActionCount] = {
        "interact", "inventory", "jump", "crouch", "wheel", "y", "sprint", "psychoscope",
        "quickLeft", "quickRight", "quickDown", "torch", "psi", "wheelLB", "wheelRB"};
    std::ostringstream out;
    out << " actions=" << (gActionsEnabled.load(std::memory_order_relaxed) ? 1 : 0)
        << " useHoldPosted=" << gUseHoldPosted.load(std::memory_order_relaxed);
    for (int slot = 0; slot < kActionCount; ++slot) {
        const int keyId = gActions[slot].keyId.load(std::memory_order_relaxed);
        const char* const name = input::KeyNameFor(keyId);
        out << ' ' << kSlotNames[slot] << "=0x" << std::hex << keyId << std::dec
            << '(' << (name != nullptr ? name : "?") << ')'
            << " sent=" << gActions[slot].pressed.load(std::memory_order_relaxed)
            << " released=" << gActions[slot].released.load(std::memory_order_relaxed)
            << " refused=" << gActions[slot].refused.load(std::memory_order_relaxed);
    }
    return out.str();
}

unsigned int MoveLaneHooked() { return gInstalled.load(std::memory_order_relaxed) ? 1u : 0u; }
unsigned long long MoveLaneOursX() { return gOursX.load(std::memory_order_relaxed); }
unsigned long long MoveLaneOursY() { return gOursY.load(std::memory_order_relaxed); }
unsigned long long MoveLaneNative() { return gNative.load(std::memory_order_relaxed); }
unsigned long long MoveLanePosted() { return gPosted.load(std::memory_order_relaxed); }
unsigned long long MoveLaneDropped() { return gDropped.load(std::memory_order_relaxed); }
unsigned long long MoveLaneInputObject() { return gInputObject.load(std::memory_order_relaxed); }
int MoveLaneAxisMilli(unsigned int axis) { return axis < 2 ? gAxisMilli[axis].load(std::memory_order_relaxed) : 0; }
int MoveLaneCinematicGate() { return gCinematic.load(std::memory_order_relaxed); }
unsigned long long MoveLaneRecenterCount() { return gRecenters.load(std::memory_order_relaxed); }

int MoveLaneHeldMilli(unsigned axis) { return axis < 3 ? gHeldMilli[axis].load(std::memory_order_relaxed) : 0; }

bool MoveLaneStickStarted(std::uint64_t now, std::uint64_t windowNs)
{
    const std::uint64_t pushed = gStickPushedNs.load(std::memory_order_relaxed);
    return (gHeldMilli[0].load(std::memory_order_relaxed) != 0 || gHeldMilli[1].load(std::memory_order_relaxed) != 0) &&
           pushed != 0 && now >= pushed && now - pushed <= windowNs;
}

DWORD SetStickSampleHoldMs(unsigned ms)
{
    if (ms > 250u) { return ERROR_INVALID_PARAMETER; }
    gSampleHoldMs.store(ms, std::memory_order_relaxed);
    Log("result=0 detail=sample_hold ms=" + std::to_string(ms));
    return 0;
}

unsigned StickSampleHoldMs() { return gSampleHoldMs.load(std::memory_order_relaxed); }

std::string MoveLaneStatusReport()
{
    std::ostringstream out;
    out << " lane={mode=" << MoveLaneMode() << " turn=" << TurnLaneEnabled() << " posted=" << gPosted.load()
        << " dropped=" << gDropped.load() << " heldMilli=" << gHeldMilli[0].load() << ',' << gHeldMilli[1].load()
        << ',' << gHeldMilli[2].load() << " holdMs=" << StickSampleHoldMs()
        << " released={off=" << gReleases[kReleaseOff].load() << " menu=" << gReleases[kReleaseMenu].load()
        << " read=" << gReleases[kReleaseRead].load() << " head=" << gReleases[kReleaseHead].load()
        << " recentre=" << gReleases[kReleaseRecentre].load() << " centre=" << gReleases[kReleaseCentre].load()
        << "} bridged=" << gBridged.load() << " turnBridged=" << gTurnBridged.load()
        << " menuFlickers=" << gMenuFlickers.load() << " menuTook=" << gMenuTook.load()
        << " reasserted=" << gReasserted[0].load() << ',' << gReasserted[1].load() << ',' << gReasserted[2].load()
        << " reassertRefused=" << gReassertRefused[0].load() << ',' << gReassertRefused[1].load() << ','
        << gReassertRefused[2].load() << " native=" << gNative.load() << " }";
    return out.str();
}

DWORD SetSnapTurnDegrees(unsigned degrees) {
    if(degrees!=0 && (degrees<15 || degrees>90)) return 1;
    gSnapDegrees=degrees;gTurnNeutralize=true;return 0;
}
unsigned SnapTurnDegrees() {return gSnapDegrees.load();}
DWORD SetHeadRelativeMovement(unsigned enabled) {
    if(enabled>1) return 1;
    gHeadRelative=enabled!=0;gMoveNeutralize=true;return 0;
}
bool HeadRelativeMovementEnabled() {return gHeadRelative.load();}
DWORD SetMovementAxisScales(unsigned strafe,unsigned backward) {
    if(strafe<1 || strafe>200 || backward<1 || backward>200) return 1;
    gStrafeScale=strafe/100.f;gBackwardScale=backward/100.f;gMoveNeutralize=true;return 0;
}
unsigned MovementStrafePercent() {return static_cast<unsigned>(std::lround(gStrafeScale.load()*100.f));}
unsigned MovementBackwardPercent() {return static_cast<unsigned>(std::lround(gBackwardScale.load()*100.f));}
} // namespace preyvr::dll
