#include "TorsoLane.h"

#include "EngineAccess.h"
#include "HeadTrackingHook.h"
#include "HookInstall.h"
#include "Logger.h"
#include "SafeMemory.h"
#include "XrInput.h"
#include "preyvr/ArmPose.h"
#include "preyvr/BodyTurn.h"
#include "preyvr/EngineMap.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/MonotonicClock.h"
#include "preyvr/MotionController.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string_view>

namespace preyvr::dll {
namespace {

void Log(const std::string& line) { lifecycle::Log("preyvr_torso " + line); }

// ArkPlayer::Update(SEntityUpdateContext&, int), primary vtable slot 19; R-021
// returns into it. RCX the player, RDX the context (frame time at +0x14), R8 the
// update slot. No float arguments, so the three registers are all it forwards.
using PlayerUpdateFn = void(__fastcall*)(void* player, void* context, int slot);
constexpr std::uintptr_t kPlayerUpdateRva = 0x1584FE0;
constexpr std::array<std::uint8_t, 32> kPlayerUpdatePrologue = {
    0x48, 0x8b, 0xc4, 0x55, 0x57, 0x48, 0x81, 0xec, 0xf8, 0x00, 0x00, 0x00, 0x48, 0x89, 0x58, 0x08,
    0x48, 0x8b, 0xf9, 0x0f, 0xb6, 0x99, 0x85, 0x18, 0x00, 0x00, 0x4c, 0x89, 0x78, 0xe8, 0x4c, 0x8b,
};
constexpr std::uintptr_t kPlayerVtableRva = 0x1E56B30;
constexpr unsigned kUpdateSlot = 19, kSetSlot = 52, kGetSlot = 53;
constexpr std::uint64_t kTorsoFreshNs = 100000000ull;

// The view-rotation pair, pinned offline by tools/re/verify_view_rotation.py:
// slots 52 and 53 of the primary vtable. The setter copies q to +0x13F0
// (ArkPlayerCamera::m_rotation), calls the entity's SetRotation slot with
// (q, 0) and hands q to the component at +0x1468, which keeps its own copy --
// so it is called, never imitated with a field write. The complete bodies are
// compared before the first call.
using SetViewRotationFn = void(__fastcall*)(void* player, const Quaternion* rotation);
using GetViewRotationFn = Quaternion*(__fastcall*)(void* player, Quaternion* out);
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
constexpr std::size_t kCinematicGate = engine::ArkPlayerLayout::input + engine::ArkPlayerInputLayout::cinematic;
// Closer than this the body is where the torso is.
constexpr float kMinimumStep = 0.01f * kRadiansPerDegree;

std::mutex gInstallMutex;
std::atomic<bool> gInstalled{false}, gActive{false}, gResetTorso{false};
std::atomic<PlayerUpdateFn> gOriginal{nullptr};
std::atomic<std::uintptr_t> gVtable{0};
std::atomic<SetViewRotationFn> gSet{nullptr};
std::atomic<GetViewRotationFn> gGet{nullptr};
// A fault in engine code is not assumed recoverable: the snap's body turn stays
// off for the rest of the process.
std::atomic<bool> gTurnFault{false};
std::atomic<int> gDeadzoneMilliDeg{35000};
std::atomic<int> gRelaxPerMille{600};
// The snap turns the body has been turned after (HeadTrackingSnapCount).
std::atomic<unsigned long long> gSnapsSeen{0};

// The main thread's own.
armpose::TorsoYaw gTorso;
std::uint64_t gTorsoNs = 0;
unsigned long long gTorsoEpoch = ~0ull;

struct TorsoSample {
    float yaw = 0.0f;
    std::uint64_t ns = 0;
};
LatestSnapshot<TorsoSample> gTorsoSample;

std::atomic<unsigned long long> gTicks{0}, gNotPlayer{0}, gNoHead{0};
std::atomic<unsigned long long> gSnapTurns{0}, gSnapWaits{0}, gReadbackMaxMdeg{0};
std::atomic<int> gHeadMdeg{0}, gTorsoMdeg{0}, gLastSnapTurnMdeg{0};

int Mdeg(float radians)
{
    return std::isfinite(radians) ? static_cast<int>(std::lround(radians * kDegreesPerRadian * 1000.0f)) : 0;
}

template<class T> bool ReadAt(const void* base, std::size_t offset, T& out)
{
    return SafeCopy(&out, static_cast<const std::uint8_t*>(base) + offset, sizeof(out));
}

bool BytesMatch(std::uintptr_t address, std::string_view hex)
{
    std::uint8_t bytes[0x80]{};
    const std::size_t size = hex.size() / 2;
    if (size == 0 || size > sizeof(bytes) || !SafeCopy(bytes, reinterpret_cast<const void*>(address), size)) {
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
    return ReadAt(reinterpret_cast<const void*>(base + kPlayerVtableRva), slot * sizeof(value), value) &&
           value == base + rva;
}

bool CallGet(void* player, Quaternion* out)
{
    __try {
        gGet.load(std::memory_order_acquire)(player, out);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallSet(void* player, const Quaternion* rotation)
{
    __try {
        gSet.load(std::memory_order_acquire)(player, rotation);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Fault(const char* where)
{
    // Snaps go back to turning about the head: the body no longer turns with them.
    SetSnapTurnsAboutBody(false);
    if (!gTurnFault.exchange(true)) { Log(std::string("result=fault detail=") + where + " snap_body_turn=off_for_process"); }
}

bool InGameplay(void* player)
{
    int mode = -1;
    int cinematic = -1;
    std::uintptr_t customView = 1;
    return ReadAt(player, kCameraMode, mode) && ReadAt(player, kCustomView, customView) &&
           ReadAt(player, kCinematicGate, cinematic) && mode == 0 && customView == 0 && cinematic == 0;
}

// After a snap turn, the native player is turned to the torso and the reference
// with it, by what the setter actually applied: the body faces the player again
// and the view stays where the snap put it (preyvr/BodyTurn.h). True when there
// is nothing more to do; false to try again next frame -- outside gameplay, or
// while a recenter or another reference change holds the lock.
bool TurnToTorso(void* player, float torso)
{
    if (gSet.load(std::memory_order_acquire) == nullptr || gTurnFault.load(std::memory_order_acquire)) { return true; }
    if (!InGameplay(player)) { return false; }
    float applied = 0.0f;
    const int result = TurnHeadTrackingReferenceWithNative([&](float reference) -> std::optional<float> {
        const auto step = bodyturn::TorsoStep(reference, torso, kMinimumStep);
        if (!step.apply) { return std::nullopt; }   // already facing the torso
        Quaternion before{};
        if (!CallGet(player, &before)) {
            Fault("get_view_rotation");
            return std::nullopt;
        }
        const auto beforeYaw = bodyturn::EngineYawOf(before);
        const auto target = bodyturn::LevelledTurn(before, step.delta);
        if (!beforeYaw || !target) { return std::nullopt; }
        if (!CallSet(player, &*target)) {
            Fault("set_view_rotation");
            return std::nullopt;
        }
        Quaternion after{};
        if (!CallGet(player, &after)) {
            Fault("get_view_rotation");
            return std::nullopt;
        }
        const auto afterYaw = bodyturn::EngineYawOf(after);
        applied = afterYaw ? controller::WrapAngle(*afterYaw - *beforeYaw) : step.delta;
        const auto readback = static_cast<unsigned long long>(std::abs(Mdeg(controller::WrapAngle(applied - step.delta))));
        if (readback > gReadbackMaxMdeg.load(std::memory_order_relaxed)) { gReadbackMaxMdeg.store(readback); }
        return applied;
    });
    if (result == 0) {
        gSnapTurns.fetch_add(1, std::memory_order_relaxed);
        gLastSnapTurnMdeg.store(Mdeg(applied), std::memory_order_relaxed);
    }
    return result != 2;
}

void Tick(void* player)
{
    gTicks.fetch_add(1, std::memory_order_relaxed);
    if (!gActive.load(std::memory_order_acquire) || player == nullptr) { return; }
    std::uintptr_t vtable = 0;
    if (!ReadAt(player, 0, vtable) || vtable != gVtable.load(std::memory_order_relaxed)) {
        gNotPlayer.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    TrackingFrame frame{};
    const std::uint64_t now = MonotonicNanoseconds();
    if (!TryGetTrackingFrame(frame) || !FreshSample(now, frame.publishedNs) ||
        !IsPoseUsable(frame.head, frame.headValidity, kMaxPoseAgeNs)) {
        gNoHead.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // A recenter faces the torso the new forward; a rebase redefines the space.
    const auto epoch = HeadTrackingRecenterEpoch();
    if (epoch != gTorsoEpoch || gResetTorso.exchange(false)) {
        gTorso.Reset();
        gTorsoEpoch = epoch;
    }
    const float dt = gTorsoNs != 0 && now > gTorsoNs ? static_cast<float>(now - gTorsoNs) * 1e-9f : 0.0f;
    gTorsoNs = now;
    const float head = armpose::HeadYaw(frame.head.orientation);
    float deadzone = 0.0f, relax = 0.0f;
    BodyTorsoSettings(deadzone, relax);
    const float torso = gTorso.Update(head, dt, deadzone, relax);
    gTorsoSample.Publish(TorsoSample{torso, now});
    gHeadMdeg.store(Mdeg(head), std::memory_order_relaxed);
    gTorsoMdeg.store(Mdeg(torso), std::memory_order_relaxed);

    // Only after a snap: turning the body every frame made smooth turning jitter.
    const auto snaps = HeadTrackingSnapCount();
    if (snaps != gSnapsSeen.load(std::memory_order_relaxed)) {
        if (TurnToTorso(player, torso)) { gSnapsSeen.store(snaps, std::memory_order_relaxed); }
        else { gSnapWaits.fetch_add(1, std::memory_order_relaxed); }
    }
}

void __fastcall PlayerUpdateDetour(void* player, void* context, int slot)
{
    // A snap queued by last frame's render hook lands first, so the body turns
    // with it in this same frame, and the view and the IK after it see one
    // reference (HeadTrackingHook.h QueueSnapTurn).
    ApplyQueuedSnapTurn(false);
    Tick(player);
    const PlayerUpdateFn original = gOriginal.load(std::memory_order_acquire);
    if (original != nullptr) { original(player, context, slot); }
}

DWORD Install()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstalled.load(std::memory_order_acquire)) { return 0; }
    const auto base = PreyBase();
    if (base == 0) { return 1; }
    if (!SlotIs(base, kUpdateSlot, kPlayerUpdateRva)) {
        Log("result=unavailable detail=player_vtable");
        return 2;
    }
    // Without the setter the torso is still estimated; a snap leaves the body behind.
    const char* mismatch = nullptr;
    if (!BytesMatch(base + kSetViewRotationRva, kSetViewRotationBody)) { mismatch = "set_view_rotation_body"; }
    else if (!BytesMatch(base + kGetViewRotationRva, kGetViewRotationBody)) { mismatch = "get_view_rotation_body"; }
    else if (!BytesMatch(base + kComponentCopyRva, kComponentCopyBody)) { mismatch = "component_copy_body"; }
    else if (!SlotIs(base, kSetSlot, kSetViewRotationRva) || !SlotIs(base, kGetSlot, kGetViewRotationRva)) {
        mismatch = "view_rotation_slots";
    }
    if (mismatch != nullptr) {
        Log(std::string("result=unavailable detail=") + mismatch + " snap_body_turn=off");
    } else {
        gSet.store(reinterpret_cast<SetViewRotationFn>(base + kSetViewRotationRva), std::memory_order_release);
        gGet.store(reinterpret_cast<GetViewRotationFn>(base + kGetViewRotationRva), std::memory_order_release);
    }
    gVtable.store(base + kPlayerVtableRva, std::memory_order_release);
    void* const target = reinterpret_cast<void*>(base + kPlayerUpdateRva);
    if (!hook::HookAt(target, kPlayerUpdatePrologue, reinterpret_cast<void*>(&PlayerUpdateDetour), gOriginal,
                      "ArkPlayer::Update", &Log)) {
        return 3;
    }
    gInstalled.store(true, std::memory_order_release);
    Log(std::string("result=0 detail=hook_installed target=ArkPlayer::Update snap_body_turn=") +
        (gSet.load() != nullptr ? "available" : "unavailable"));
    return 0;
}

} // namespace

DWORD SetTorsoLaneActive(unsigned active)
{
    if (active > 1) { return ERROR_INVALID_PARAMETER; }
    if (active != 0) {
        const DWORD installed = Install();
        if (installed != 0) { return installed; }
        gResetTorso.store(true, std::memory_order_release);
        gSnapsSeen.store(HeadTrackingSnapCount(), std::memory_order_relaxed);   // no turn for snaps before now
    } else {
        gTorsoSample.Clear();
    }
    gActive.store(active != 0, std::memory_order_release);
    SetSnapTurnsAboutBody(active != 0 && gSet.load(std::memory_order_acquire) != nullptr &&
                          !gTurnFault.load(std::memory_order_acquire));
    Log(std::string("result=0 detail=active value=") + (active ? "1" : "0"));
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

void BodyTorsoSettings(float& deadzoneRadians, float& relax)
{
    deadzoneRadians = gDeadzoneMilliDeg.load(std::memory_order_relaxed) * 1e-3f * kRadiansPerDegree;
    relax = gRelaxPerMille.load(std::memory_order_relaxed) * 1e-3f;
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

std::string TorsoLaneReport()
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << " hooked=" << gInstalled.load() << " active=" << gActive.load()
        << " torsoDeadzoneDeg=" << gDeadzoneMilliDeg.load() / 1000 << " torsoRelax=" << gRelaxPerMille.load() / 1000.0
        << " ticks=" << gTicks.load() << " notPlayer=" << gNotPlayer.load() << " noHead=" << gNoHead.load()
        << " headDeg=" << gHeadMdeg.load() / 1000.0 << " torsoDeg=" << gTorsoMdeg.load() / 1000.0
        << " snapBodyTurn=" << (gSet.load() != nullptr && !gTurnFault.load()) << " turnFault=" << gTurnFault.load()
        << " snapTurns=" << gSnapTurns.load() << " lastSnapTurnMdeg=" << gLastSnapTurnMdeg.load()
        << " snapWaits=" << gSnapWaits.load() << " readbackMaxMdeg=" << gReadbackMaxMdeg.load();
    return out.str();
}

} // namespace preyvr::dll
