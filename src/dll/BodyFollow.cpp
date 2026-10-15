#include "BodyFollow.h"

#include "AimTakeover.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "SceneQuery.h"
#include "XrSessionHost.h"
#include "XrInput.h"
#include "preyvr/ArmPose.h"
#include "preyvr/BodyFollow.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/RoomScale.h"

#include <MinHook.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
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

// Room-scale (docs/ROOMSCALE-2026-10-11.md): the player entity's position. The
// concrete CEntity vtable is the one holding SceneQuery's hash-gated GetPhysics
// (+0x228). SetPos(pos, whyFlags, recalcPhysics, force) at +0x108 stores
// m_vPos and dispatches the transform update; GetPos at +0x110 returns
// &m_vPos. The game's own stance change moves the player with this pair.
using SetPosFn = void(__fastcall*)(void* entity, const Vec3* position, int whyFlags, bool recalcPhysics, bool force);
using GetPosFn = const Vec3*(__fastcall*)(void* entity);
constexpr std::size_t kPlayerEntity = 0x38;
constexpr std::uintptr_t kEntityVtableRva = 0x1D30BD0;
constexpr unsigned kSetPosSlot = 0x108 / 8, kGetPosSlot = 0x110 / 8, kGetPhysicsSlot = 0x228 / 8;
constexpr std::uintptr_t kSetPosRva = 0x90AF40, kGetPosRva = 0x10EA250, kGetPhysicsRva = 0x9042B0;
constexpr std::string_view kSetPosBody =
    "f30f1002458bd00f2e41387516f30f1042040f2e413c750bf30f1042080f2e41407424f20f10024183ca02f20f114138"
    "450fb6c18b4208418bd2894140488b0148ffa050030000c3";
constexpr std::string_view kGetPosBody = "488d4138c3";

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
std::atomic<bool> gRoomAvailable{false}, gRoomScale{true}, gNudgePending{false};
std::atomic<SetPosFn> gSetPos{nullptr};
std::atomic<GetPosFn> gGetPos{nullptr};
std::atomic<std::uintptr_t> gEntityVtable{0};
std::atomic<int> gStartMm{30}, gStopMm{5}, gFollowMs{50}, gRadiusMm{350};
std::atomic<int> gNudgeRightMm{0}, gNudgeForwardMm{0};
std::atomic<int> gDeadzoneMilliDeg{35000};
std::atomic<int> gRelaxPerMille{600};
std::atomic<int> gTurnRequestMdeg{0};
// Diagnostic requests wait for gameplay -- typed in the tuner, the game is
// unfocused and paused -- and for their delay, so the wearer can be back in
// the headset and looking down. Dropped a minute after they fell due.
std::atomic<std::uint64_t> gTurnDueNs{0}, gNudgeDueNs{0};
constexpr std::uint64_t kRequestLifeNs = 60000000000ull;
// The head-in-wall layer: out while the head is through geometry, or far
// from a body the sweep stopped. Published for the XR fade and the inputs.
std::atomic<bool> gHeadOut{false};
std::atomic<std::uint64_t> gHeadOutNs{0};
constexpr float kOutResidual = 0.30f, kInResidual = 0.25f;

// The game thread's own.
armpose::TorsoYaw gTorso;
std::uint64_t gTorsoNs = 0;
unsigned long long gTorsoEpoch = ~0ull;
roomscale::Follower gRoomFollower;
// Where the capsule was after the last tick, to tell a capsule the engine is
// moving -- stick walking, a jump, a fall, a lift -- from one at rest.
Vec3 gLastFeet{};
std::uint64_t gLastFeetNs = 0;
constexpr float kNativeMovingSpeed = 0.25f;   // m/s

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

// Room-scale. The play-space yaw the last view composed with: the move is
// expressed in its frame.
std::atomic<float> gViewP{0.0f};
std::atomic<unsigned long long> gViewPGeneration{~0ull};
std::atomic<std::uint64_t> gViewPNs{0};
Counter gMoved{0}, gMovedMm{0}, gBlocked{0}, gSweepRefused{0}, gRoomBusy{0}, gPositionOff{0}, gNoView{0};
Counter gNoPose{0}, gJumps{0}, gNudges{0}, gViewDrift{0}, gHeadOutEvents{0}, gHeadOutFrames{0}, gHeadHits{0};
Counter gNativeMoving{0};
std::atomic<int> gLastMoveMm{0}, gResidualMm{0}, gViewDriftMaxMm{0}, gNudgeDriftMaxMm{0};
// Where SetPos left the entity, for the view of the same frame to read back;
// and after a nudge, for the next views. Under gViewMutex.
bool gMovedPending = false;
Vec3 gMovedPos{};
void* gMovedEntity = nullptr;
int gNudgeWatch = 0;
Vec3 gNudgePos{};

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

bool SlotIs(std::uintptr_t base, std::uintptr_t vtableRva, unsigned slot, std::uintptr_t rva)
{
    std::uintptr_t value = 0;
    return ReadMemory(reinterpret_cast<const void*>(base + vtableRva + slot * sizeof(value)), &value,
                      sizeof(value)) &&
           value == base + rva;
}

bool CallGetPos(GetPosFn get, void* entity, Vec3& out)
{
    __try {
        out = *get(entity);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallSetPos(SetPosFn set, void* entity, const Vec3* position)
{
    __try {
        set(entity, position, 0, false, false);   // as the stance change calls it
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The player's entity, when it is the concrete CEntity the pair was verified on.
void* PlayerEntity(void* player)
{
    void* entity = nullptr;
    std::uintptr_t table = 0;
    if (!ReadAt(player, kPlayerEntity, entity) || entity == nullptr || !ReadAt(entity, 0, table) ||
        table != gEntityVtable.load(std::memory_order_relaxed)) {
        return nullptr;
    }
    return entity;
}

int Mm(float metres) { return std::isfinite(metres) ? static_cast<int>(std::lround(metres * 1000.0f)) : 0; }

roomscale::Settings RoomSettings()
{
    roomscale::Settings settings;
    settings.startRadius = gStartMm.load(std::memory_order_relaxed) / 1000.0f;
    settings.stopRadius = gStopMm.load(std::memory_order_relaxed) / 1000.0f;
    settings.followTime = gFollowMs.load(std::memory_order_relaxed) / 1000.0f;
    settings.radius = gRadiusMm.load(std::memory_order_relaxed) / 1000.0f;
    return settings;
}

// The play-space yaw the latest view of this reference composed with.
bool ViewPlaySpaceYaw(float& yaw)
{
    if (gViewPGeneration.load(std::memory_order_acquire) != HeadTrackingReferenceGeneration() ||
        !FreshSample(MonotonicNanoseconds(), gViewPNs.load(std::memory_order_acquire), kTorsoFreshNs)) {
        return false;
    }
    yaw = gViewP.load(std::memory_order_acquire);
    return std::isfinite(yaw);
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
int Turn(void* player, float torso, float requested, bool toTorso)
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
    return result;
}

// Moves the player's capsule -- towards the head, or by `nudge` -- swept for
// walls first, and the origin with it by exactly what SetPos applied.
int Move(void* player, const GameplayPoseFrame& pose, float playSpaceYaw, const Pose& head, float dt,
         const Vec3* nudge, float& residual)
{
    residual = std::numeric_limits<float>::quiet_NaN();
    void* const entity = PlayerEntity(player);
    if (entity == nullptr) {
        gNotPlayer.fetch_add(1, std::memory_order_relaxed);
        return -1;
    }
    const SetPosFn set = gSetPos.load(std::memory_order_acquire);
    const GetPosFn get = gGetPos.load(std::memory_order_acquire);
    const roomscale::Settings settings = RoomSettings();
    bool blocked = false;
    bool sweepRefused = false;
    Vec3 applied{};
    Vec3 movedTo{};
    const int result = MoveHeadTrackingOriginWithNative(head, playSpaceYaw, [&](const Vec3& offset) -> std::optional<Vec3> {
        residual = std::hypot(offset.x, offset.y);   // until a move takes some of it off
        const Vec3 move = nudge != nullptr ? *nudge : gRoomFollower.Update(offset, dt, settings);
        if (nudge == nullptr && gRoomFollower.Jumped()) { gJumps.fetch_add(1, std::memory_order_relaxed); }
        const float length = std::hypot(move.x, move.y);
        if (!(length > 1e-5f)) { return std::nullopt; }
        Vec3 feet{};
        if (!CallGetPos(get, entity, feet)) {
            Fault("get_pos");
            return std::nullopt;
        }
        roomscale::Rays rays{};
        if (!roomscale::SweepRays(feet, move, settings, rays)) { return std::nullopt; }
        std::array<float, roomscale::kRays> hits{};
        for (std::size_t i = 0; i < rays.size(); ++i) {
            scene::Hit hit{};
            if (!QuerySegment(pose, rays[i].from, rays[i].to, hit)) {
                sweepRefused = true;   // no move without a sweep
                return std::nullopt;
            }
            hits[i] = hit.distance;
        }
        const float allowed = roomscale::AllowedLength(length, rays, hits, settings);
        if (allowed < length) { blocked = true; }
        if (allowed < 1e-4f) { return std::nullopt; }
        const float k = allowed / length;
        const Vec3 target{feet.x + move.x * k, feet.y + move.y * k, feet.z};
        if (!CallSetPos(set, entity, &target)) {
            Fault("set_pos");
            return std::nullopt;
        }
        if (!CallGetPos(get, entity, movedTo)) {
            Fault("get_pos");
            return std::nullopt;
        }
        // Horizontal only: height stays the view's and the engine's business.
        applied = {movedTo.x - feet.x, movedTo.y - feet.y, 0.0f};
        gLastFeet = movedTo;   // our move is not the engine's
        residual = std::hypot(offset.x - applied.x, offset.y - applied.y);
        return applied;
    });
    if (std::isfinite(residual)) { gResidualMm.store(Mm(residual), std::memory_order_relaxed); }
    if (blocked) { gBlocked.fetch_add(1, std::memory_order_relaxed); }
    if (sweepRefused) { gSweepRefused.fetch_add(1, std::memory_order_relaxed); }
    if (result == 0) {
        const int mm = Mm(std::hypot(applied.x, applied.y));
        gMoved.fetch_add(1, std::memory_order_relaxed);
        gMovedMm.fetch_add(static_cast<unsigned long long>(mm), std::memory_order_relaxed);
        gLastMoveMm.store(mm, std::memory_order_relaxed);
        if (nudge != nullptr) { gNudges.fetch_add(1, std::memory_order_relaxed); }
        std::lock_guard lock(gViewMutex);
        gMovedPending = true;
        gMovedPos = movedTo;
        gMovedEntity = entity;
        if (nudge != nullptr) {
            gNudgeWatch = 300;
            gNudgePos = movedTo;
        }
    } else if (result == 1) {
        gNoReference.fetch_add(1, std::memory_order_relaxed);
    } else if (result == 2) {
        gRoomBusy.fetch_add(1, std::memory_order_relaxed);
    } else if (result == 4) {
        gPositionOff.fetch_add(1, std::memory_order_relaxed);
    }
    if (result == 0 && nudge != nullptr) {
        Log("result=0 detail=nudge_done movedMm=" + std::to_string(Mm(std::hypot(applied.x, applied.y))));
    } else if (nudge != nullptr) {
        Log("result=" + std::to_string(result) + " detail=nudge_refused blocked=" + std::to_string(blocked) +
            " sweepRefused=" + std::to_string(sweepRefused));
    }
    return result;
}

// The head-in-wall layer (docs/ROOMSCALE-2026-10-11.md): is the head through
// geometry? The segment from the capsule's axis at head height to the head --
// inside the capsule at one end, so a wall between them is one the head went
// through -- or a head far from a body the sweep stopped.
void HeadCheck(void* player, const GameplayPoseFrame& pose, float residual)
{
    bool hit = false;
    void* const entity = PlayerEntity(player);
    Vec3 feet{};
    if (entity != nullptr && pose.cameraCentreValid && CallGetPos(gGetPos.load(std::memory_order_acquire), entity, feet)) {
        const Vec3 head = pose.cameraCentre;
        const Vec3 axis{feet.x, feet.y, head.z};
        const float span = std::hypot(head.x - axis.x, head.y - axis.y);
        if (span > 2.0f) {
            hit = true;
        } else if (span >= 0.01f) {
            scene::Hit h{};
            hit = QuerySegment(pose, axis, head, h) && h.distance >= 0.0f;
        }
    }
    if (hit) { gHeadHits.fetch_add(1, std::memory_order_relaxed); }
    const float away = std::isfinite(residual) ? residual : 0.0f;
    const bool wasOut = gHeadOut.load(std::memory_order_relaxed);
    const bool out = hit || away > (wasOut ? kInResidual : kOutResidual);
    if (out && !wasOut) { gHeadOutEvents.fetch_add(1, std::memory_order_relaxed); }
    if (out) { gHeadOutFrames.fetch_add(1, std::memory_order_relaxed); }
    gHeadOut.store(out, std::memory_order_release);
    gHeadOutNs.store(MonotonicNanoseconds(), std::memory_order_release);
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
        gRoomFollower.Reset();
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

    // Diagnostics run when due, in gameplay; until then they stay pending.
    int requestMdeg = gTurnRequestMdeg.load(std::memory_order_acquire);
    if (requestMdeg != 0 && now > gTurnDueNs.load() + kRequestLifeNs) {
        gTurnRequestMdeg.store(0, std::memory_order_release);
        Log("result=expired detail=turn");
        requestMdeg = 0;
    }
    const bool turnDue = requestMdeg != 0 && now >= gTurnDueNs.load();
    bool nudge = gNudgePending.load(std::memory_order_acquire);
    if (nudge && now > gNudgeDueNs.load() + kRequestLifeNs) {
        gNudgePending.store(false, std::memory_order_release);
        Log("result=expired detail=nudge");
        nudge = false;
    }
    const bool nudgeDue = nudge && now >= gNudgeDueNs.load();
    const bool following = gEnabled.load(std::memory_order_acquire);
    const bool roomScale = gRoomScale.load(std::memory_order_acquire) && gRoomAvailable.load(std::memory_order_acquire);
    if (!roomScale) { gHeadOut.store(false, std::memory_order_release); }
    if (!turnDue && !nudgeDue && !following && !roomScale) { return; }
    if (!InGameplay(player)) {
        gNotGameplay.fetch_add(1, std::memory_order_relaxed);
        gRoomFollower.Reset();
        gHeadOut.store(false, std::memory_order_release);
        return;
    }
    // The turn first: it changes G and R together and leaves the play space,
    // and so the head's offset, alone.
    if (turnDue) {
        gTurnRequestMdeg.store(0, std::memory_order_release);
        const int result = Turn(player, torso, requestMdeg * 1e-3f * kRadians, false);
        Log("result=" + std::to_string(result) + " detail=turn_done degrees=" + std::to_string(requestMdeg / 1000) +
            " appliedMdeg=" + std::to_string(gLastDeltaMdeg.load()));
    }
    if (following && !gFault.load(std::memory_order_acquire)) { Turn(player, torso, 0.0f, true); }

    if ((!roomScale && !nudgeDue) || !gRoomAvailable.load(std::memory_order_acquire) ||
        gFault.load(std::memory_order_acquire) || !HudGameplayInputAllowed()) {
        gRoomFollower.Reset();
        gHeadOut.store(false, std::memory_order_release);
        return;
    }
    float playSpaceYaw = 0.0f;
    if (!ViewPlaySpaceYaw(playSpaceYaw)) {
        gNoView.fetch_add(1, std::memory_order_relaxed);
        gRoomFollower.Reset();
        return;
    }
    GameplayPoseFrame pose{};
    if (!TryGetGameplayPoseFrame(pose, true) || pose.player != reinterpret_cast<std::uintptr_t>(player)) {
        gNoPose.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    float residual = std::numeric_limits<float>::quiet_NaN();
    // The engine is moving the capsule: leave it be. A SetPos while the stick
    // walks the player stalled the walk (second headset run).
    bool nativeMoving = false;
    if (void* const entity = PlayerEntity(player)) {
        Vec3 feet{};
        if (CallGetPos(gGetPos.load(std::memory_order_acquire), entity, feet)) {
            if (gLastFeetNs != 0 && now > gLastFeetNs && now - gLastFeetNs < kTorsoFreshNs) {
                const float seconds = static_cast<float>(now - gLastFeetNs) * 1e-9f;
                const float travelled = std::sqrt((feet.x - gLastFeet.x) * (feet.x - gLastFeet.x) +
                                                  (feet.y - gLastFeet.y) * (feet.y - gLastFeet.y) +
                                                  (feet.z - gLastFeet.z) * (feet.z - gLastFeet.z));
                nativeMoving = travelled > kNativeMovingSpeed * seconds;
            }
            gLastFeet = feet;
            gLastFeetNs = now;
        }
    }
    if (nativeMoving) {
        gNativeMoving.fetch_add(1, std::memory_order_relaxed);
        gRoomFollower.Reset();
    }
    // The body follows the neck, not the eyes.
    const Pose followHead{frame.head.orientation, roomscale::FollowPoint(frame.head, torso)};
    if (nudgeDue) {
        gNudgePending.store(false, std::memory_order_release);
        // Right and forward of the body's facing.
        Quaternion facing{};
        const auto yaw = CallGet(gGet.load(std::memory_order_acquire), player, &facing)
                             ? bodyfollow::EngineYawOf(facing) : std::nullopt;
        if (yaw) {
            const float right = gNudgeRightMm.load() / 1000.0f;
            const float forward = gNudgeForwardMm.load() / 1000.0f;
            const float c = std::cos(*yaw), sn = std::sin(*yaw);
            // A yaw of t sends +X to (cos t, sin t) and +Y to (-sin t, cos t).
            const Vec3 move{right * c - forward * sn, right * sn + forward * c, 0.0f};
            Move(player, pose, playSpaceYaw, followHead, dt, &move, residual);
        }
    }
    if (roomScale) {
        if (!nativeMoving) { Move(player, pose, playSpaceYaw, followHead, dt, nullptr, residual); }
        HeadCheck(player, pose, residual);
    } else {
        gRoomFollower.Reset();
    }
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
    else if (!SlotIs(base, kPlayerVtableRva, kUpdateSlot, kPlayerUpdateRva) ||
             !SlotIs(base, kPlayerVtableRva, kSetSlot, kSetViewRotationRva) ||
             !SlotIs(base, kPlayerVtableRva, kGetSlot, kGetViewRotationRva)) {
        mismatch = "player_vtable";
    }
    if (mismatch != nullptr) {
        Log(std::string("result=unavailable detail=") + mismatch);
        return 2;
    }
    const char* roomMismatch = nullptr;
    if (!BytesMatch(base + kSetPosRva, kSetPosBody)) { roomMismatch = "set_pos_body"; }
    else if (!BytesMatch(base + kGetPosRva, kGetPosBody)) { roomMismatch = "get_pos_body"; }
    else if (!SlotIs(base, kEntityVtableRva, kSetPosSlot, kSetPosRva) ||
             !SlotIs(base, kEntityVtableRva, kGetPosSlot, kGetPosRva) ||
             !SlotIs(base, kEntityVtableRva, kGetPhysicsSlot, kGetPhysicsRva)) {
        roomMismatch = "entity_vtable";
    }
    if (roomMismatch != nullptr) {
        Log(std::string("result=unavailable detail=room_scale_") + roomMismatch + " turning=available");
    } else {
        gSetPos.store(reinterpret_cast<SetPosFn>(base + kSetPosRva), std::memory_order_release);
        gGetPos.store(reinterpret_cast<GetPosFn>(base + kGetPosRva), std::memory_order_release);
        gEntityVtable.store(base + kEntityVtableRva, std::memory_order_release);
        gRoomAvailable.store(true, std::memory_order_release);
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
    Log(std::string("result=0 detail=hook_installed target=ArkPlayer::Update setter=0x1584CC0 roomScale=") +
        (gRoomAvailable.load() ? "available" : "unavailable"));
    return 0;
}

void ResetStats()
{
    for (Counter* c : {&gApplied, &gTurns, &gNotPlayer, &gNoHead, &gNotGameplay, &gNoReference, &gBusy,
                       &gNativeRefused, &gTurnedMdeg, &gViewAfterFollow, &gViewWithoutFollow, &gThreadMismatch,
                       &gIkBetween, &gIkAfterView, &gIkAfterRender, &gStepFollowed, &gStepIdle, &gMoved,
                       &gMovedMm, &gBlocked, &gSweepRefused, &gRoomBusy, &gPositionOff, &gNoView, &gNoPose,
                       &gJumps, &gNudges, &gViewDrift, &gHeadOutEvents, &gHeadOutFrames, &gHeadHits,
                       &gNativeMoving}) {
        c->store(0, std::memory_order_relaxed);
    }
    gReadbackMaxMdeg.store(0, std::memory_order_relaxed);
    gStepFollowedMaxMdeg.store(0, std::memory_order_relaxed);
    gStepIdleMaxMdeg.store(0, std::memory_order_relaxed);
    gViewDriftMaxMm.store(0, std::memory_order_relaxed);
    gNudgeDriftMaxMm.store(0, std::memory_order_relaxed);
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

DWORD SetRoomScale(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    if ((enabled != 0) != gRoomScale.load()) { ResetStats(); }
    gRoomScale.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=room_scale value=") + (enabled ? "1" : "0"));
    return 0;
}

unsigned RoomScaleEnabled() { return gRoomScale.load(std::memory_order_relaxed) ? 1u : 0u; }

DWORD SetRoomScaleZone(int startMm, int stopMm, int followMs)
{
    if (startMm < 5 || startMm > 500 || stopMm < 0 || stopMm >= startMm || followMs > 1000) {
        return ERROR_INVALID_PARAMETER;
    }
    gStartMm.store(startMm, std::memory_order_relaxed);
    gStopMm.store(stopMm, std::memory_order_relaxed);
    if (followMs >= 0) { gFollowMs.store(followMs, std::memory_order_relaxed); }
    return 0;
}

int RoomScaleStartMm() { return gStartMm.load(std::memory_order_relaxed); }
int RoomScaleStopMm() { return gStopMm.load(std::memory_order_relaxed); }
int RoomScaleFollowMs() { return gFollowMs.load(std::memory_order_relaxed); }

DWORD SetRoomScaleRadius(int radiusMm)
{
    if (radiusMm < 100 || radiusMm > 600) { return ERROR_INVALID_PARAMETER; }
    gRadiusMm.store(radiusMm, std::memory_order_relaxed);
    return 0;
}

DWORD RequestBodyNudge(int rightCm, int forwardCm, int delaySeconds)
{
    if ((rightCm == 0 && forwardCm == 0) || std::abs(rightCm) > 100 || std::abs(forwardCm) > 100 ||
        delaySeconds < 0 || delaySeconds > 60) {
        return ERROR_INVALID_PARAMETER;
    }
    if (!gActive.load(std::memory_order_acquire) || !gRoomAvailable.load(std::memory_order_acquire)) {
        return ERROR_NOT_READY;
    }
    gNudgeRightMm.store(rightCm * 10, std::memory_order_relaxed);
    gNudgeForwardMm.store(forwardCm * 10, std::memory_order_relaxed);
    gNudgeDueNs.store(MonotonicNanoseconds() + static_cast<std::uint64_t>(delaySeconds) * 1000000000ull);
    gNudgePending.store(true, std::memory_order_release);
    return 0;
}

DWORD SetBodyFollowMode(unsigned mode)
{
    if (mode > 2) { return ERROR_INVALID_PARAMETER; }
    SetBodyFollow(mode != 0 ? 1u : 0u);
    SetRoomScale(mode == 2 ? 1u : 0u);
    return 0;
}

unsigned BodyFollowMode()
{
    if (!gEnabled.load(std::memory_order_relaxed)) { return 0; }
    return gRoomScale.load(std::memory_order_relaxed) ? 2u : 1u;
}

DWORD RequestBodyTurn(int degrees, int delaySeconds)
{
    if (degrees == 0 || degrees < -180 || degrees > 180 || delaySeconds < 0 || delaySeconds > 60) {
        return ERROR_INVALID_PARAMETER;
    }
    if (!gActive.load(std::memory_order_acquire)) { return ERROR_NOT_READY; }
    gTurnDueNs.store(MonotonicNanoseconds() + static_cast<std::uint64_t>(delaySeconds) * 1000000000ull);
    gTurnRequestMdeg.store(degrees * 1000, std::memory_order_release);
    return 0;
}

bool BodyHeadOut()
{
    return gActive.load(std::memory_order_acquire) && gHeadOut.load(std::memory_order_acquire) &&
           FreshSample(MonotonicNanoseconds(), gHeadOutNs.load(std::memory_order_acquire), kTorsoFreshNs);
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
    if (std::isfinite(playSpaceYaw)) {
        gViewP.store(playSpaceYaw, std::memory_order_release);
        gViewPGeneration.store(referenceGeneration, std::memory_order_release);
        gViewPNs.store(MonotonicNanoseconds(), std::memory_order_release);
    }
    const bool turned = gTurnedSinceView.exchange(false, std::memory_order_acq_rel);
    std::lock_guard lock(gViewMutex);
    // Where the entity is now against where SetPos left it this frame, and
    // after a nudge for the next 300 views: a physics step computed from the
    // old position would put it back (docs/ROOMSCALE-2026-10-11.md, risk 1).
    const GetPosFn getPos = gGetPos.load(std::memory_order_acquire);
    if ((gMovedPending || gNudgeWatch > 0) && getPos != nullptr && gMovedEntity != nullptr) {
        Vec3 now{};
        if (CallGetPos(getPos, gMovedEntity, now)) {
            if (gMovedPending) {
                const int drift = Mm(std::hypot(now.x - gMovedPos.x, now.y - gMovedPos.y));
                StoreMax(gViewDriftMaxMm, drift);
                if (drift > 5) { gViewDrift.fetch_add(1, std::memory_order_relaxed); }
            }
            if (gNudgeWatch > 0) {
                StoreMax(gNudgeDriftMaxMm, Mm(std::hypot(now.x - gNudgePos.x, now.y - gNudgePos.y)));
                --gNudgeWatch;
            }
        }
    }
    gMovedPending = false;
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
        << " roomReadFallbacks=" << HeadTrackingRoomReadFallbacks()
        << " roomScale=" << gRoomScale.load() << " roomAvailable=" << gRoomAvailable.load()
        << " zoneMm=" << gStartMm.load() << '/' << gStopMm.load() << " followMs=" << gFollowMs.load()
        << " radiusMm=" << gRadiusMm.load()
        << " moved=" << gMoved.load() << " movedM=" << gMovedMm.load() / 1000.0
        << " lastMoveMm=" << gLastMoveMm.load() << " residualMm=" << gResidualMm.load()
        << " blocked=" << gBlocked.load() << " sweepRefused=" << gSweepRefused.load()
        << " roomBusy=" << gRoomBusy.load() << " positionOff=" << gPositionOff.load()
        << " noView=" << gNoView.load() << " noPose=" << gNoPose.load() << " jumps=" << gJumps.load()
        << " viewDrift=" << gViewDrift.load() << " viewDriftMaxMm=" << gViewDriftMaxMm.load()
        << " nudges=" << gNudges.load() << " nudgeDriftMaxMm=" << gNudgeDriftMaxMm.load()
        << " pendingTurnDeg=" << gTurnRequestMdeg.load() / 1000 << " pendingNudge=" << gNudgePending.load()
        << " headOut=" << BodyHeadOut() << " headOutEvents=" << gHeadOutEvents.load()
        << " headOutFrames=" << gHeadOutFrames.load() << " headHits=" << gHeadHits.load()
        << " fadeFrames=" << XrFadeFrames() << " nativeMoving=" << gNativeMoving.load();
    return out.str();
}

} // namespace preyvr::dll
