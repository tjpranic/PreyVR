#include "preyvr/FiringPosition.h"
#include "HookInstall.h"
#include "EngineAccess.h"
#include "GameplayFrame.h"
#include "ShotRay.h"
#include "preyvr/AnimIk.h"
#include <sstream>
#include "WeaponAttachment.h"
#include "MinHookInit.h"
#include "preyvr/LatestSnapshot.h"

#include "Logger.h"
#include "XrInput.h"
#include "preyvr/EngineMap.h"
#include "preyvr/VrMath.h"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <span>
#include <string>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_weapon_attach " + line);
}

// R-024 CArkWeapon::AttachToHand. The equipped weapon resolves its IAttachment*
// into CArkWeapon+0x2B0 and installs a binding through attachment vtable +0xD8.
using AttachToHandFn = bool(__fastcall*)(void*);

constexpr std::uintptr_t kAttachToHandRva = 0x16914F0;
constexpr std::array<std::uint8_t, 28> kAttachToHandPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
    0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
    0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x81,
    0xEC, 0x90, 0x00, 0x00,
};

constexpr std::size_t kWeaponAttachmentField = 0x2B0;   // R-024

// IAttachment vtable, aligned against R-024's independently recorded +0xD8
// (AddBinding, index 27).
constexpr std::size_t kGetAttAbsoluteDefault = 10 * 8;  // +0x50

// CryEngine QuatT: Quat(x,y,z,w) then Vec3(x,y,z), 28 bytes.
struct QuatT {
    float x, y, z, w;
    float px, py, pz;
};

using GetAbsFn = const QuatT*(__fastcall*)(void* attachment);

void* gTarget = nullptr;
std::atomic<AttachToHandFn> gOriginal{nullptr};
bool gInstalled = false;
LatestSnapshot<EquippedRig> gEquippedRig;
std::mutex gMountMutex;
std::atomic<std::uint64_t> gEquipGeneration{0};

std::atomic<bool> gObserving{false};
std::atomic<unsigned long long> gAttachment{0};
QuatT gBaseline{};
std::atomic<bool> gHaveBaseline{false};

using FiringPositionFn = void*(__fastcall*)(void*, void*, std::uint32_t, void*);
std::atomic<FiringPositionFn> gFiringPositionOriginal{nullptr};
std::atomic<bool> gMuzzleInstalled{false};
struct MuzzleSample {
    EquippedRig owner{};
    Vec3 nativeOrigin{}, aimOrigin{}, gripOrigin{};
    // The grip's ORIENTATION at the same instant, which is what makes the
    // muzzle offset expressible in the grip's own frame and therefore reusable
    // as the weapon moves. Without it the pair of positions is only valid for
    // the one pose it was captured in.
    Quaternion gripOrientation{};
    std::uint64_t poseSequence = 0, poseAgeNs = 0, capturedNs = 0;
    unsigned int cameraFallback = 0;
};
LatestSnapshot<MuzzleSample> gMuzzleSample;
std::atomic<unsigned long long> gMuzzleSamples{0};
constexpr std::array<std::uint8_t, 26> kFiringPositionPrologue{
    0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x55,0x57,
    0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0xA8,0xE8,0xFE,0xFF,0xFF};

bool CopyFiringPosition(const void* result, std::uint8_t* bytes)
{
    if (!result) { return false; }
    __try { std::memcpy(bytes, result, 16); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void* __fastcall FiringPositionObserved(void* weapon, void* output, std::uint32_t flags, void* entity)
{
    const auto original = gFiringPositionOriginal.load(std::memory_order_acquire);
    void* result = original ? original(weapon, output, flags, entity) : nullptr;
    // aim.shot: a camera fallback that a VR reach does not justify goes back to
    // the muzzle. The observer below then records the shot's real origin.
    ShotRayAdjustFiringPosition(weapon, result, flags, entity);
    if (!gObserving.load() || entity != nullptr) { return result; }
    GameplayPoseFrame frame{};
    MuzzleSample sample{};
    if (!TryGetGameplayPoseFrame(frame) || !TryGetEquippedRig(frame.player, sample.owner) ||
        sample.owner.weapon != reinterpret_cast<std::uintptr_t>(weapon)) { return result; }
    std::array<std::uint8_t, 16> bytes{};
    if (!CopyFiringPosition(result, bytes.data())) { return result; }
    const auto firing = DecodeFiringPosition(bytes);
    if (!firing) { return result; }
    sample.nativeOrigin = firing->origin; sample.cameraFallback = firing->cameraFallback;
    const auto& hand = frame.tracking.hands[static_cast<unsigned int>(Hand::right)];
    if (!IsPoseUsable(hand.aimPose, hand.aimValidity, kMaxPoseAgeNs) ||
        !IsPoseUsable(hand.gripPose, hand.gripValidity, kMaxPoseAgeNs)) { return result; }
    sample.aimOrigin = animik::ControllerWorldFromHead(frame.yaw, frame.nativeEye, frame.tracking.head, hand.aimPose).position;
    const Pose gripWorld = animik::ControllerWorldFromHead(
        frame.yaw, frame.nativeEye, frame.tracking.head, hand.gripPose);
    sample.gripOrigin = gripWorld.position;
    sample.gripOrientation = gripWorld.orientation;
    sample.poseSequence = frame.tracking.sequence;
    sample.capturedNs = MonotonicNanoseconds();
    sample.poseAgeNs = sample.capturedNs - frame.tracking.publishedNs;
    gMuzzleSample.Publish(sample);
    gMuzzleSamples.fetch_add(1);
    return result; // native helper selection, wall guard and result unchanged
}

bool InstallMuzzleObserver()
{
    if (gMuzzleInstalled.load()) { return true; }
    const auto base = PreyBase();
    if (!base) { return false; }
    auto* target = reinterpret_cast<void*>(base + 0x1694BC0);
    if (std::memcmp(target, kFiringPositionPrologue.data(), kFiringPositionPrologue.size()) != 0) { return false; }
    if (!hook::Install(target, reinterpret_cast<void*>(&FiringPositionObserved), gFiringPositionOriginal)) { return false; }
    gMuzzleInstalled.store(true);
    return true;
}

bool ReadMount(void* attachment, QuatT& out)
{
    if (attachment == nullptr) {
        return false;
    }
    __try {
        auto* const vtable = *reinterpret_cast<std::uint8_t**>(attachment);
        const auto get = *reinterpret_cast<GetAbsFn*>(vtable + kGetAttAbsoluteDefault);
        const QuatT* const value = get(attachment);
        if (value == nullptr) {
            return false;
        }
        out = *value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    // The data names itself: a mount is a unit quaternion and a finite
    // translation. Anything else means this pointer is not an attachment, and a
    // write would go somewhere unknown.
    const float length = std::sqrt(out.x*out.x + out.y*out.y + out.z*out.z + out.w*out.w);
    if (!std::isfinite(length) || length < 0.9f || length > 1.1f) {
        return false;
    }
    return std::isfinite(out.px) && std::isfinite(out.py) && std::isfinite(out.pz);
}

// POD-only guarded reads, no guessed virtual calls. R-088 bone vtable and
// owner chain; item ID is whole-weapon +0x38 (not secondary interface +0x38).
bool ReadRig(void* weapon, EquippedRig& out)
{
    const auto base = PreyBase();
    if (!base || !weapon) { return false; }
    __try {
        out.weapon = reinterpret_cast<std::uintptr_t>(weapon);
        const auto itemVtable = *reinterpret_cast<std::uintptr_t*>(out.weapon + 8);
        // Concrete GetOwnerId is mov eax,[rcx+58h]; ret at 0x10DFC10,
        // with RCX=weapon+8. Read its proven field, never call a guessed slot.
        if (!itemVtable || *reinterpret_cast<std::uintptr_t*>(itemVtable + 0x1D8) != base + 0x10DFC10 ||
            *reinterpret_cast<std::uint32_t*>(out.weapon + 0x60) != 0x7777) { return false; }
        out.itemId = *reinterpret_cast<std::uint32_t*>(out.weapon + 0x38);
        out.attachment = *reinterpret_cast<std::uintptr_t*>(out.weapon + 0x2B0);
        if (!out.itemId || !out.attachment ||
            *reinterpret_cast<std::uintptr_t*>(out.attachment) != base + 0x1D212B8) { return false; }
        out.binding = *reinterpret_cast<std::uintptr_t*>(out.attachment + 0x20);
        const auto manager = *reinterpret_cast<std::uintptr_t*>(out.attachment + 0x28);
        if (!manager || !out.binding) { return false; }
        out.character = *reinterpret_cast<std::uintptr_t*>(manager + 0x18);
        return out.character != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SelectedItemMatches(std::uintptr_t player, std::uint32_t item)
{
    if (!player || !item) { return false; }
    __try {
        // IsEquipped(0x1275500): direct selected ID. Alias/paired items are
        // deliberately refused until their secondary identity is resolved.
        return *reinterpret_cast<std::uint32_t*>(player + 0x14B8 + 0x58) == item;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool __fastcall AttachToHandObserved(void* weapon)
{
    const AttachToHandFn original = gOriginal.load(std::memory_order_acquire);
    const bool result = original != nullptr && original(weapon);
    if (gObserving.load(std::memory_order_acquire) && weapon != nullptr) {
        EquippedRig rig{};
        if (result && ReadRig(weapon, rig)) {
            std::lock_guard mountLock(gMountMutex);
            rig.generation = gEquipGeneration.fetch_add(1) + 1;
            gEquippedRig.Publish(rig);
            gHaveBaseline.store(false);
            gAttachment.store(0);
            QuatT mount{};
            if (ReadMount(reinterpret_cast<void*>(rig.attachment), mount)) {
                gHaveBaseline.store(false, std::memory_order_release);
                gAttachment.store(rig.attachment, std::memory_order_release);
                gBaseline = mount;
                gHaveBaseline.store(true, std::memory_order_release);
            }
        } else {
            EquippedRig previous{};
            if (gEquippedRig.TryRead(previous) && previous.weapon == reinterpret_cast<std::uintptr_t>(weapon)) {
                gEquippedRig.Clear();
                std::lock_guard mountLock(gMountMutex);
                gHaveBaseline.store(false, std::memory_order_release);
                gAttachment.store(0, std::memory_order_release);
            }
        }
    }
    return result;
}

bool Install()
{
    if (gInstalled) {
        return true;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return false;
    }
    const auto target = reinterpret_cast<std::uintptr_t>(preyDll) + kAttachToHandRva;
    if (std::memcmp(reinterpret_cast<const void*>(target),
                    kAttachToHandPrologue.data(), kAttachToHandPrologue.size()) != 0) {
        Log("result=unavailable detail=attach_prologue_mismatch");
        return false;
    }
    gTarget = reinterpret_cast<void*>(target);
    const auto installed = hook::Install(gTarget, reinterpret_cast<void*>(&AttachToHandObserved), gOriginal);
    if (!installed) {
        Log(std::string("result=failed detail=") + hook::StepName(installed.step));
        return false;
    }
    gInstalled = true;
    Log("result=0 detail=hook_installed target=CArkWeapon::AttachToHand");
    return true;
}

} // namespace

std::string WeaponMuzzleAlignmentReport()
{
    std::ostringstream out;
    out << " muzzleHooked=" << (gMuzzleInstalled.load() ? 1 : 0) << " muzzleSamples=" << gMuzzleSamples.load();
    MuzzleSample sample{};
    if (!gMuzzleSample.TryRead(sample)) { out << " muzzleSample=unavailable"; return out.str(); }
    const auto now = MonotonicNanoseconds();
    const auto gap = [](Vec3 a, Vec3 b) {
        const float x=a.x-b.x, y=a.y-b.y, z=a.z-b.z;
        return std::sqrt(x*x+y*y+z*z) * 1000.0f;
    };
    GameplayPoseFrame frame{};
    EquippedRig owner{};
    const bool current = TryGetGameplayPoseFrame(frame, false) && TryGetEquippedRig(frame.player, owner) &&
        owner.generation == sample.owner.generation;
    out << " muzzleSampleOwnerCurrent=" << (current ? 1 : 0)
        << " muzzleAgeMs=" << (now >= sample.capturedNs ? (now-sample.capturedNs)/1000000 : 0)
        << " muzzleEquipGen=" << sample.owner.generation << " muzzlePoseSeq=" << sample.poseSequence
        << " muzzlePoseAgeMs=" << sample.poseAgeNs/1000000 << " muzzleFallback=" << sample.cameraFallback
        << " muzzleWorld=" << sample.nativeOrigin.x << ',' << sample.nativeOrigin.y << ',' << sample.nativeOrigin.z
        << " muzzleAimGapMm=" << gap(sample.nativeOrigin,sample.aimOrigin)
        << " muzzleGripGapMm=" << gap(sample.nativeOrigin,sample.gripOrigin);
    return out.str();
}

// The muzzle expressed in the GRIP's frame, so it follows the weapon.
struct BarrelOffset {
    Vec3 local{};
    std::uint64_t equipGeneration = 0;
    bool valid = false;
};
LatestSnapshot<BarrelOffset> gBarrelOffset;
std::atomic<unsigned long long> gBarrelCalibrations{0};

DWORD CalibrateWeaponBarrel()
{
    MuzzleSample sample{};
    if (!gMuzzleSample.TryRead(sample)) {
        Log("result=refused detail=no_muzzle_sample note=fire_once_first");
        return 1;
    }
    if (sample.cameraFallback != 0) {
        // The native query fell back to the camera because its safety ray was
        // blocked, so this position is not the muzzle at all. Calibrating from
        // it would bake a wall into the weapon.
        Log("result=refused detail=sample_used_camera_fallback");
        return 2;
    }
    GameplayPoseFrame frame{};
    EquippedRig owner{};
    if (!TryGetGameplayPoseFrame(frame, false) || !TryGetEquippedRig(frame.player, owner) ||
        owner.generation != sample.owner.generation) {
        Log("result=refused detail=sample_belongs_to_another_weapon");
        return 3;
    }
    const Quaternion grip = Normalize(sample.gripOrientation);
    const Vec3 delta{sample.nativeOrigin.x - sample.gripOrigin.x,
                     sample.nativeOrigin.y - sample.gripOrigin.y,
                     sample.nativeOrigin.z - sample.gripOrigin.z};
    BarrelOffset offset{};
    offset.local = Rotate(Conjugate(grip), delta);
    offset.equipGeneration = owner.generation;
    offset.valid = std::isfinite(offset.local.x) && std::isfinite(offset.local.y) &&
                   std::isfinite(offset.local.z);
    // A muzzle a metre from the hand is a bad sample, not a long weapon.
    const float reach = std::sqrt(offset.local.x * offset.local.x +
                                  offset.local.y * offset.local.y +
                                  offset.local.z * offset.local.z);
    if (!offset.valid || reach > 1.0f) {
        Log("result=refused detail=implausible_offset mm=" +
            std::to_string(static_cast<int>(reach * 1000.0f)));
        return 4;
    }
    gBarrelOffset.Publish(offset);
    gBarrelCalibrations.fetch_add(1, std::memory_order_relaxed);
    Log("result=0 detail=barrel_calibrated mm=" +
        std::to_string(static_cast<int>(offset.local.x * 1000.0f)) + "," +
        std::to_string(static_cast<int>(offset.local.y * 1000.0f)) + "," +
        std::to_string(static_cast<int>(offset.local.z * 1000.0f)));
    return 0;
}

bool TryGetMuzzleFromGrip(const Pose& gripWorld, std::uint64_t currentEquipGeneration, Vec3& out)
{
    BarrelOffset offset{};
    if (!gBarrelOffset.TryRead(offset) || !offset.valid) { return false; }
    // The offset belongs to the weapon it was measured on. F-009 again: a
    // different weapon has a different muzzle, and reusing one is a confident
    // wrong answer rather than a missing one.
    if (offset.equipGeneration != currentEquipGeneration) { return false; }
    const Vec3 turned = Rotate(Normalize(gripWorld.orientation), offset.local);
    out = Vec3{gripWorld.position.x + turned.x,
               gripWorld.position.y + turned.y,
               gripWorld.position.z + turned.z};
    return true;
}

unsigned long long WeaponBarrelCalibrations()
{
    return gBarrelCalibrations.load(std::memory_order_relaxed);
}

int WeaponBarrelOffsetMillimetres(unsigned int axis)
{
    BarrelOffset offset{};
    if (!gBarrelOffset.TryRead(offset) || !offset.valid || axis >= 3) { return 0; }
    return static_cast<int>((&offset.local.x)[axis] * 1000.0f);
}

std::uint64_t WeaponEquipGeneration() { return gEquipGeneration.load(std::memory_order_acquire); }

bool TryGetEquippedRig(std::uintptr_t player, EquippedRig& out)
{
    if (!gObserving.load(std::memory_order_acquire) || !gEquippedRig.TryRead(out) ||
        !SelectedItemMatches(player, out.itemId)) { return false; }
    EquippedRig current{};
    return ReadRig(reinterpret_cast<void*>(out.weapon), current) &&
        SameRigBinding(out, current, out.itemId);
}

bool EnsureFiringPositionHook() { return InstallMuzzleObserver(); }

DWORD SetWeaponAttachmentObserving(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on && (!EnsureGameplayPoseObservation() || !Install())) {
        return 1;
    }
    if (on && !InstallMuzzleObserver()) { Log("result=unavailable detail=muzzle_observer"); }
    gObserving.store(on, std::memory_order_release);
    if (!on) {
        gEquippedRig.Clear();
        std::lock_guard mountLock(gMountMutex);
        gHaveBaseline.store(false);
        gAttachment.store(0);
    }
    Log(std::string("result=0 detail=observing value=") + (on ? "1" : "0") +
        " note=re-equip a weapon to capture");
    return 0;
}

int WeaponAttachmentJointIndex()
{
    const auto attachment = reinterpret_cast<const std::uint8_t*>(gAttachment.load(std::memory_order_acquire));
    if (attachment == nullptr) { return -1; }
    __try {
        return *reinterpret_cast<const int*>(attachment + 0x15C);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

unsigned int WeaponAttachmentSimulationFlags()
{
    const auto attachment = reinterpret_cast<const std::uint8_t*>(gAttachment.load(std::memory_order_acquire));
    if (attachment == nullptr) { return 0; }
    __try {
        return static_cast<unsigned int>(attachment[8 + 0x28]) |
               (static_cast<unsigned int>(attachment[8 + 0x2B]) << 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

unsigned long long WeaponAttachmentPointer() { return gAttachment.load(std::memory_order_acquire); }

int WeaponMountPositionMillimetres(unsigned int axis)
{
    std::lock_guard mountLock(gMountMutex);
    if (!gHaveBaseline.load(std::memory_order_acquire)) {
        return 0;
    }
    const float v = axis == 0 ? gBaseline.px : axis == 1 ? gBaseline.py : gBaseline.pz;
    return static_cast<int>(v * 1000.0f);
}

unsigned int WeaponBaselineCaptured()
{
    return gHaveBaseline.load(std::memory_order_acquire) ? 1u : 0u;
}

} // namespace preyvr::dll
