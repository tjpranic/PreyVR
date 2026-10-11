#include "BodyFollow.h"

#include "HeadTrackingHook.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "XrInput.h"
#include "preyvr/ArmPose.h"
#include "preyvr/BodyFollow.h"
#include "preyvr/LatestSnapshot.h"

#include <MinHook.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string_view>

namespace preyvr::dll {
namespace {

void Log(const std::string& line) { lifecycle::Log("preyvr_body " + line); }

constexpr float kDegrees = 57.2957795f;
constexpr float kRadians = 0.0174532925f;

// ArkPlayer::Update(SEntityUpdateContext&, int), primary vtable slot 19; R-021
// returns into it. RCX the player, RDX the context (frame time at +0x14), R8 the
// update slot. No float arguments, so the three registers are all it forwards.
using PlayerUpdateFn = void(__fastcall*)(void* player, void* context, int slot);
constexpr std::uintptr_t kPlayerUpdateRva = 0x1584FE0;
constexpr std::string_view kPlayerUpdatePrologue =
    "488bc455574881ecf800000048895808488bf90fb699851800004c8978e84c8b";

// The view-rotation pair, pinned offline by tools/re/verify_view_rotation.py:
// slots 52 and 53 of the primary vtable. The setter copies q to +0x13F0
// (ArkPlayerCamera::m_rotation), calls the entity's SetRotation slot with
// (q, 0) and hands q to the component at +0x1468, which keeps its own copy --
// so it is called, never imitated with a field write. The complete bodies are
// compared before the first call.
using SetViewRotationFn = void(__fastcall*)(void* player, const Quaternion* rotation);
using GetViewRotationFn = Quaternion*(__fastcall*)(void* player, Quaternion* out);
constexpr std::uintptr_t kPlayerVtableRva = 0x1E56B30;
constexpr unsigned kUpdateSlot = 19, kSetSlot = 52, kGetSlot = 53;
constexpr std::uintptr_t kSetViewRotationRva = 0x1584CC0, kGetViewRotationRva = 0x157CE80;
constexpr std::uintptr_t kComponentCopyRva = 0x1327050;
constexpr std::string_view kSetViewRotationBody =
    "48895c2408574883ec208b02488bd98981f01300004533c08b4204488bfa8981f41300008b42088981f8130000"
    "8b420c8981fc130000488b4938488b01ff9018010000488d8b68140000488bd7488b5c24304883c4205fe93423daff";
constexpr std::string_view kGetViewRotationBody =
    "8b81fc13000089420c8b81f013000089028b81f41300008942048b81f8130000894208488bc2c3";
constexpr std::string_view kComponentCopyBody = "8b028941088b420489410c8b42088941108b420c894114c3";

// Gameplay only. ArkPlayerCamera (+0x12A0): the mode UpdateView switches on
// (+0x1A4: 0 is the ordinary camera; modes 1-3 write m_rotation themselves,
// 0x181489010) and the custom-view callable (+0x148, R-009). ArkPlayerInput
// (+0x8E0): the cinematic gate (+0x94, R-089).
constexpr std::size_t kCameraMode = 0x12A0 + 0x1A4;
constexpr std::size_t kCustomView = 0x12A0 + 0x148;
constexpr std::size_t kCinematicGate = 0x8E0 + 0x94;

// Closer than this the body is where the torso is.
constexpr float kMinimumStep = 0.01f * kRadians;
constexpr std::uint64_t kTorsoFreshNs = 100000000ull;

std::mutex gInstallMutex;
std::atomic<bool> gInstalled{false};
std::atomic<PlayerUpdateFn> gOriginal{nullptr};
std::atomic<SetViewRotationFn> gSet{nullptr};
std::atomic<GetViewRotationFn> gGet{nullptr};
std::atomic<std::uintptr_t> gVtable{0};

std::atomic<bool> gActive{false}, gEnabled{true}, gLevel{true}, gFault{false}, gResetTorso{false};
std::atomic<int> gDeadzoneMilliDeg{35000};
std::atomic<int> gRelaxPerMille{600};
std::atomic<int> gTurnRequestMdeg{0};

// The game thread's own.
armpose::TorsoYaw gTorso;
std::uint64_t gTorsoNs = 0;
unsigned long long gTorsoEpoch = ~0ull;

struct TorsoSample {
    float yaw = 0.0f;
    std::uint64_t ns = 0;
};
LatestSnapshot<TorsoSample> gTorsoSample;

using Counter = std::atomic<unsigned long long>;
Counter gTicks{0}, gApplied{0}, gTurns{0}, gNotPlayer{0}, gNoHead{0}, gNotGameplay{0};
Counter gNoReference{0}, gBusy{0}, gNativeRefused{0}, gFaults{0}, gTurnedMdeg{0};
std::atomic<int> gCameraMode{-1}, gHeadMdeg{0}, gTorsoMdeg{0}, gLastDeltaMdeg{0}, gReadbackMaxMdeg{0};

// Frame order: one sequence over the follower, the view and the render hook.
Counter gSequence{0}, gFollowSeq{0}, gViewSeq{0}, gRenderSeq{0};
std::atomic<unsigned long> gFollowThread{0};
std::atomic<bool> gTurnedSinceView{false};
Counter gViewAfterFollow{0}, gViewWithoutFollow{0}, gThreadMismatch{0};
Counter gIkBetween{0}, gIkAfterView{0}, gIkAfterRender{0};
// How far the play space moved between consecutive views of one reference
// generation, on frames the body was turned and on frames it was not. Equal
// maxima mean the turn left the view alone.
std::mutex gViewMutex;
bool gHavePrevious = false;
float gPreviousP = 0.0f;
unsigned long long gPreviousGeneration = 0;
Counter gStepFollowed{0}, gStepIdle{0};
std::atomic<int> gStepFollowedMaxMdeg{0}, gStepIdleMaxMdeg{0};

int Mdeg(float radians)
{
    return std::isfinite(radians) ? static_cast<int>(std::lround(radians * kDegrees * 1000.0f)) : 0;
}

void StoreMax(std::atomic<int>& slot, int value)
{
    int previous = slot.load(std::memory_order_relaxed);
    while (value > previous && !slot.compare_exchange_weak(previous, value)) {}
}

bool ReadMemory(const void* at, void* out, std::size_t size)
{
    __try {
        std::memcpy(out, at, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template<class T> bool ReadAt(void* base, std::size_t offset, T& out)
{
    return ReadMemory(static_cast<const std::uint8_t*>(base) + offset, &out, sizeof(out));
}

bool CallGet(GetViewRotationFn get, void* player, Quaternion* out)
{
    __try {
        get(player, out);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallSet(SetViewRotationFn set, void* player, const Quaternion* rotation)
{
    __try {
        set(player, rotation);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool BytesMatch(std::uintptr_t address, std::string_view hex)
{
    std::uint8_t bytes[0x80]{};
    const std::size_t size = hex.size() / 2;
    if (size == 0 || size > sizeof(bytes) || !ReadMemory(reinterpret_cast<const void*>(address), bytes, size)) {
        return false;
    }
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < size; ++i) {
        if (bytes[i] != static_cast<std::uint8_t>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]))) {
            return false;
        }
    }
    return true;
}

bool SlotIs(std::uintptr_t base, unsigned slot, std::uintptr_t rva)
{
    std::uintptr_t value = 0;
    return ReadMemory(reinterpret_cast<const void*>(base + kPlayerVtableRva + slot * sizeof(value)), &value,
                      sizeof(value)) &&
           value == base + rva;
}

// A fault in engine code is not assumed recoverable: following stays off for
// the rest of the process.
void Fault(const char* where)
{
    gFaults.fetch_add(1, std::memory_order_relaxed);
    if (!gFault.exchange(true)) {
        Log(std::string("result=fault detail=") + where + " following=disarmed_for_process");
    }
}

bool InGameplay(void* player)
{
    int mode = -1;
    int cinematic = -1;
    std::uintptr_t customView = 1;
    if (!ReadAt(player, kCameraMode, mode) || !ReadAt(player, kCustomView, customView) ||
        !ReadAt(player, kCinematicGate, cinematic)) {
        return false;
    }
    gCameraMode.store(mode, std::memory_order_relaxed);
    return mode == 0 && customView == 0 && cinematic == 0;
}

// Turns the native player -- by `requested`, or to the torso when `toTorso` --
// and the reference with it, by what the setter actually applied.
void Turn(void* player, float torso, float requested, bool toTorso)
{
    const SetViewRotationFn set = gSet.load(std::memory_order_acquire);
    const GetViewRotationFn get = gGet.load(std::memory_order_acquire);
    float applied = 0.0f;
    const int result = TurnHeadTrackingReferenceWithNative([&](float reference) -> std::optional<float> {
        float delta = requested;
        if (toTorso) {
            const auto step = bodyfollow::FollowStep(reference, torso, kMinimumStep);
            if (!step.apply) { return std::nullopt; }   // already facing the torso
            delta = step.delta;
        }
        Quaternion before{};
        if (!CallGet(get, player, &before)) {
            Fault("get_view_rotation");
            return std::nullopt;
        }
        const auto beforeYaw = bodyfollow::EngineYawOf(before);
        const auto target = bodyfollow::TurnedViewRotation(before, delta, gLevel.load(std::memory_order_relaxed));
        if (!beforeYaw || !target) {
            gNativeRefused.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        }
        if (!CallSet(set, player, &*target)) {
            Fault("set_view_rotation");
            return std::nullopt;
        }
        Quaternion after{};
        if (!CallGet(get, player, &after)) {
            Fault("get_view_rotation");
            return std::nullopt;
        }
        const auto afterYaw = bodyfollow::EngineYawOf(after);
        applied = afterYaw ? bodyfollow::Wrap(*afterYaw - *beforeYaw) : delta;
        StoreMax(gReadbackMaxMdeg, std::abs(Mdeg(bodyfollow::Wrap(applied - delta))));
        return applied;
    });
    if (result == 0) {
        (toTorso ? gApplied : gTurns).fetch_add(1, std::memory_order_relaxed);
        gLastDeltaMdeg.store(Mdeg(applied), std::memory_order_relaxed);
        gTurnedMdeg.fetch_add(static_cast<unsigned long long>(std::abs(Mdeg(applied))), std::memory_order_relaxed);
        gTurnedSinceView.store(true, std::memory_order_release);
    } else if (result == 1) {
        gNoReference.fetch_add(1, std::memory_order_relaxed);
    } else if (result == 2) {
        gBusy.fetch_add(1, std::memory_order_relaxed);
    }
}

void Tick(void* player)
{
    gFollowSeq.store(gSequence.fetch_add(1) + 1);
    gFollowThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    gTicks.fetch_add(1, std::memory_order_relaxed);
    if (!gActive.load(std::memory_order_acquire) || gFault.load(std::memory_order_acquire) || player == nullptr) {
        return;
    }
    std::uintptr_t vtable = 0;
    if (!ReadAt(player, 0, vtable) || vtable != gVtable.load(std::memory_order_relaxed)) {
        gNotPlayer.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // The torso, every frame the head is tracked -- followed or not, the arm
    // solver and the body slots share it.
    TrackingFrame frame{};
    const std::uint64_t now = MonotonicNanoseconds();
    if (!TryGetTrackingFrame(frame) || !FreshSample(now, frame.publishedNs) ||
        !IsPoseUsable(frame.head, frame.headValidity, 200000000ull)) {
        gNoHead.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // A recenter faces the body the new forward; a rebase redefines the space.
    const auto epoch = HeadTrackingRecenterEpoch();
    if (epoch != gTorsoEpoch || gResetTorso.exchange(false)) {
        gTorso.Reset();
        gTorsoEpoch = epoch;
    }
    const float dt = gTorsoNs != 0 && now > gTorsoNs ? static_cast<float>(now - gTorsoNs) * 1e-9f : 0.0f;
    gTorsoNs = now;
    const float head = armpose::HeadYaw(frame.head.orientation);
    const float torso = gTorso.Update(head, dt, gDeadzoneMilliDeg.load(std::memory_order_relaxed) * 1e-3f * kRadians,
                                      gRelaxPerMille.load(std::memory_order_relaxed) * 1e-3f);
    gTorsoSample.Publish(TorsoSample{torso, now});
    gHeadMdeg.store(Mdeg(head), std::memory_order_relaxed);
    gTorsoMdeg.store(Mdeg(torso), std::memory_order_relaxed);

    const int requestMdeg = gTurnRequestMdeg.exchange(0);
    const bool following = gEnabled.load(std::memory_order_acquire);
    if (requestMdeg == 0 && !following) { return; }
    if (!InGameplay(player)) {
        gNotGameplay.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (requestMdeg != 0) { Turn(player, torso, requestMdeg * 1e-3f * kRadians, false); }
    if (following && !gFault.load(std::memory_order_acquire)) { Turn(player, torso, 0.0f, true); }
}

void __fastcall PlayerUpdateDetour(void* player, void* context, int slot)
{
    // Before the engine's own update: the frame's view, and if the order holds
    // its arm IK, see the turned body.
    Tick(player);
    const PlayerUpdateFn original = gOriginal.load(std::memory_order_acquire);
    if (original != nullptr) { original(player, context, slot); }
}

DWORD Install()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstalled.load(std::memory_order_acquire)) { return 0; }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) { return 1; }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    const char* mismatch = nullptr;
    if (!BytesMatch(base + kPlayerUpdateRva, kPlayerUpdatePrologue)) { mismatch = "update_prologue"; }
    else if (!BytesMatch(base + kSetViewRotationRva, kSetViewRotationBody)) { mismatch = "set_view_rotation_body"; }
    else if (!BytesMatch(base + kGetViewRotationRva, kGetViewRotationBody)) { mismatch = "get_view_rotation_body"; }
    else if (!BytesMatch(base + kComponentCopyRva, kComponentCopyBody)) { mismatch = "component_copy_body"; }
    else if (!SlotIs(base, kUpdateSlot, kPlayerUpdateRva) || !SlotIs(base, kSetSlot, kSetViewRotationRva) ||
             !SlotIs(base, kGetSlot, kGetViewRotationRva)) {
        mismatch = "player_vtable";
    }
    if (mismatch != nullptr) {
        Log(std::string("result=unavailable detail=") + mismatch);
        return 2;
    }
    gSet.store(reinterpret_cast<SetViewRotationFn>(base + kSetViewRotationRva), std::memory_order_release);
    gGet.store(reinterpret_cast<GetViewRotationFn>(base + kGetViewRotationRva), std::memory_order_release);
    gVtable.store(base + kPlayerVtableRva, std::memory_order_release);
    void* const target = reinterpret_cast<void*>(base + kPlayerUpdateRva);
    PlayerUpdateFn original = nullptr;
    EnsureMinHook();
    if (MH_CreateHook(target, reinterpret_cast<void*>(&PlayerUpdateDetour), reinterpret_cast<void**>(&original)) !=
        MH_OK) {
        Log("result=failed detail=create_hook target=ArkPlayer::Update");
        return 3;
    }
    gOriginal.store(original, std::memory_order_release);
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        Log("result=failed detail=enable_hook target=ArkPlayer::Update");
        return 3;
    }
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=hook_installed target=ArkPlayer::Update setter=0x1584CC0");
    return 0;
}

void ResetStats()
{
    for (Counter* c : {&gApplied, &gTurns, &gNotPlayer, &gNoHead, &gNotGameplay, &gNoReference, &gBusy,
                       &gNativeRefused, &gTurnedMdeg, &gViewAfterFollow, &gViewWithoutFollow, &gThreadMismatch,
                       &gIkBetween, &gIkAfterView, &gIkAfterRender, &gStepFollowed, &gStepIdle}) {
        c->store(0, std::memory_order_relaxed);
    }
    gReadbackMaxMdeg.store(0, std::memory_order_relaxed);
    gStepFollowedMaxMdeg.store(0, std::memory_order_relaxed);
    gStepIdleMaxMdeg.store(0, std::memory_order_relaxed);
}

} // namespace

DWORD SetBodyFollowActive(unsigned active)
{
    if (active > 1) { return ERROR_INVALID_PARAMETER; }
    if (active != 0) {
        const DWORD installed = Install();
        if (installed != 0) { return installed; }
        ResetStats();
        gResetTorso.store(true, std::memory_order_release);
    } else {
        gTorsoSample.Clear();
    }
    gActive.store(active != 0, std::memory_order_release);
    Log(std::string("result=0 detail=active value=") + (active ? "1" : "0"));
    return 0;
}

DWORD SetBodyFollow(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    if ((enabled != 0) != gEnabled.load()) { ResetStats(); }
    gEnabled.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=follow value=") + (enabled ? "1" : "0"));
    return 0;
}

unsigned BodyFollowEnabled() { return gEnabled.load(std::memory_order_relaxed) ? 1u : 0u; }

DWORD SetBodyFollowLevel(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gLevel.store(enabled != 0, std::memory_order_release);
    return 0;
}

DWORD SetBodyTorso(int deadzoneDegrees, int relaxPerMille)
{
    if (deadzoneDegrees < 0 || deadzoneDegrees > 180 || relaxPerMille < 0 || relaxPerMille > 10000) {
        return ERROR_INVALID_PARAMETER;
    }
    gDeadzoneMilliDeg.store(deadzoneDegrees * 1000, std::memory_order_relaxed);
    gRelaxPerMille.store(relaxPerMille, std::memory_order_relaxed);
    return 0;
}

DWORD RequestBodyTurn(int degrees)
{
    if (degrees == 0 || degrees < -180 || degrees > 180) { return ERROR_INVALID_PARAMETER; }
    if (!gActive.load(std::memory_order_acquire)) { return ERROR_NOT_READY; }
    gTurnRequestMdeg.store(degrees * 1000, std::memory_order_release);
    return 0;
}

bool BodyTorsoYaw(float& yaw)
{
    if (!gActive.load(std::memory_order_acquire)) { return false; }
    TorsoSample sample{};
    if (!gTorsoSample.TryRead(sample) || !FreshSample(MonotonicNanoseconds(), sample.ns, kTorsoFreshNs)) {
        return false;
    }
    yaw = sample.yaw;
    return true;
}

void NoteBodyFollowView(float playSpaceYaw, unsigned long long referenceGeneration)
{
    const auto sequence = gSequence.fetch_add(1) + 1;
    const auto previousView = gViewSeq.exchange(sequence);
    if (!gActive.load(std::memory_order_acquire)) { return; }
    (gFollowSeq.load() > previousView ? gViewAfterFollow : gViewWithoutFollow).fetch_add(1, std::memory_order_relaxed);
    const auto follower = gFollowThread.load(std::memory_order_relaxed);
    if (follower != 0 && follower != GetCurrentThreadId()) { gThreadMismatch.fetch_add(1, std::memory_order_relaxed); }
    const bool turned = gTurnedSinceView.exchange(false, std::memory_order_acq_rel);
    std::lock_guard lock(gViewMutex);
    if (gHavePrevious && referenceGeneration == gPreviousGeneration && std::isfinite(playSpaceYaw)) {
        const int step = std::abs(Mdeg(bodyfollow::Wrap(playSpaceYaw - gPreviousP)));
        if (turned) {
            StoreMax(gStepFollowedMaxMdeg, step);
            gStepFollowed.fetch_add(1, std::memory_order_relaxed);
        } else {
            StoreMax(gStepIdleMaxMdeg, step);
            gStepIdle.fetch_add(1, std::memory_order_relaxed);
        }
    }
    gHavePrevious = std::isfinite(playSpaceYaw);
    gPreviousP = playSpaceYaw;
    gPreviousGeneration = referenceGeneration;
}

void NoteBodyFollowIk()
{
    if (!gActive.load(std::memory_order_acquire)) { return; }
    const auto follow = gFollowSeq.load(), view = gViewSeq.load(), render = gRenderSeq.load();
    // After the follower and before the view: this frame's IK saw the turned
    // body. After the view: still this frame's turned body. After the render
    // hook: either this frame's IK running late or the next frame's running
    // before its follower -- the case a snap turn shows as a hand flash.
    if (follow > view && follow > render) { gIkBetween.fetch_add(1, std::memory_order_relaxed); }
    else if (view > follow && view > render) { gIkAfterView.fetch_add(1, std::memory_order_relaxed); }
    else { gIkAfterRender.fetch_add(1, std::memory_order_relaxed); }
}

void NoteBodyFollowRender() { gRenderSeq.store(gSequence.fetch_add(1) + 1); }

std::string BodyFollowReport()
{
    std::ostringstream out;
    out << " active=" << gActive.load() << " follow=" << gEnabled.load() << " level=" << gLevel.load()
        << " hooked=" << gInstalled.load() << " fault=" << gFault.load()
        << " torsoDeadzoneDeg=" << gDeadzoneMilliDeg.load() / 1000.0f
        << " torsoRelax=" << gRelaxPerMille.load() / 1000.0f
        << " ticks=" << gTicks.load() << " applied=" << gApplied.load() << " turns=" << gTurns.load()
        << " turnedDeg=" << gTurnedMdeg.load() / 1000 << " lastDeltaMdeg=" << gLastDeltaMdeg.load()
        << " readbackMaxMdeg=" << gReadbackMaxMdeg.load()
        << " headDeg=" << gHeadMdeg.load() / 1000.0f << " torsoDeg=" << gTorsoMdeg.load() / 1000.0f
        << " referenceDeg=" << HeadTrackingReferenceYaw() * kDegrees
        << " notPlayer=" << gNotPlayer.load() << " noHead=" << gNoHead.load()
        << " notGameplay=" << gNotGameplay.load() << " cameraMode=" << gCameraMode.load()
        << " noReference=" << gNoReference.load() << " busy=" << gBusy.load()
        << " nativeRefused=" << gNativeRefused.load() << " faults=" << gFaults.load()
        << " viewAfterFollow=" << gViewAfterFollow.load() << " viewWithoutFollow=" << gViewWithoutFollow.load()
        << " threadMismatch=" << gThreadMismatch.load()
        << " stepFollowed=" << gStepFollowed.load() << " stepFollowedMaxMdeg=" << gStepFollowedMaxMdeg.load()
        << " stepIdle=" << gStepIdle.load() << " stepIdleMaxMdeg=" << gStepIdleMaxMdeg.load()
        << " ikBetween=" << gIkBetween.load() << " ikAfterView=" << gIkAfterView.load()
        << " ikAfterRender=" << gIkAfterRender.load()
        << " roomReadFallbacks=" << HeadTrackingRoomReadFallbacks();
    return out.str();
}

} // namespace preyvr::dll
