#include "HeadLamp.h"
#include "AnimIkTakeover.h"
#include "ArmsLane.h"
#include "EngineAccess.h"
#include "EnvSwitch.h"
#include "HeadTrackingHook.h"
#include "HookInstall.h"
#include "InputPost.h"
#include "Logger.h"
#include "SafeMemory.h"
#include "preyvr/HeadLamp.h"
#include "preyvr/HeadLampNative.h"
#include "preyvr/NativeContract.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>

namespace preyvr::dll {
namespace {
namespace hn = headlamp::native;

void Log(const std::string& line) { lifecycle::Log("preyvr_head_lamp " + line); }

static_assert(contract::Gated(hn::Contracts, hn::ProcessAttachment));
static_assert(contract::Gated(hn::Contracts, hn::BoneWorldAbsolute));
// A QuatT as Prey lays it out: q xyzw, then t.
static_assert(sizeof(Quaternion) == 16 && sizeof(Pose) == 28);

// The two lights by their attachment's name, and as the report names them.
constexpr const char* kAttachments[] = {"flashlight", "psychoscope_light"};
constexpr const char* kNames[] = {"flashlight", "psychoscope"};
constexpr std::size_t kLights = std::size(kAttachments);

using ProcessFn = void(__fastcall*)(void* binding, void* attachment);
std::atomic<ProcessFn> gOriginal{nullptr};
std::uintptr_t gBase = 0;   // PreyDll's, once the gate passed
std::atomic<bool> gActive{false};
std::mutex gInstallMutex;
bool gInstallTried = false;
std::atomic<bool> gInstalled{false};
std::atomic<const char*> gGate{"unchecked"};

// Prey's world placement as the attachment's vtable +0x70 returns it, a QuatTS.
struct Placement {
    Quaternion q{};
    Vec3 t{};
    float s = 1.0f;
};
static_assert(sizeof(Placement) == 32);

// One light this frame: what putting it on an eye needs.
struct Lamp {
    std::uint32_t entity = 0;
    Pose mount{};
    Vec3 eye{};          // Prey's eye in the world
    float scale = 1.0f;  // the character's
    Vec3 size{};         // what Prey hands the entity as its scale
};

// The lights the attachment pass met before this frame's view. Main thread, so
// the lock is an observation rather than a requirement.
std::mutex gPendingMutex;
std::array<std::optional<Lamp>, kLights> gPending{};
// LastAppliedView's sequence when the last frame was rendered: a newer one is
// this frame's view.
std::atomic<unsigned long long> gRendered{0};

std::atomic<std::uint64_t> gPlaced{0}, gDeferred{0}, gLate{0}, gNative{0}, gUnread{0}, gOffThread{0}, gUnplaced{0};
std::array<std::atomic<std::uint64_t>, kLights> gPerLight{};
// Each light's mount as last read: for the report, and one log line a change.
std::mutex gMountMutex;
std::array<Pose, kLights> gMount{};
std::array<bool, kLights> gMountSeen{};

bool Usable(Quaternion q)
{
    const float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return std::isfinite(n) && n > 0.5f && n < 2.0f;
}

float Degrees(Quaternion q)
{
    return 2.0f * std::acos(std::fmin(1.0f, std::fabs(Normalize(q).w))) * 180.0f / 3.14159265f;
}

// --- the engine (tests replace it here) ----------------------------------------
std::uint64_t PlayerCharacter() { return AnimIkOwnerCharacter(); }

bool WorldPlacement(std::uintptr_t attachment, Placement& out)
{
    using WorldFn = const Placement*(__fastcall*)(std::uintptr_t attachment, Placement* out);
    __try {
        const auto vtable = *reinterpret_cast<const std::uintptr_t*>(attachment);
        const auto world = *reinterpret_cast<const WorldFn*>(vtable + hn::VtableWorldAbsolute);
        Placement buffer{};
        out = *world(attachment, &buffer);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// As Prey's placement does it: the entity bound, by SetPosRotScale. No such
// entity is nothing to place, as there.
bool PlaceEntity(std::uint32_t id, const Vec3& position, const Quaternion& rotation, const Vec3& size)
{
    using SetFn = void(__fastcall*)(std::uintptr_t entity, const Vec3* position, const Quaternion* rotation,
                                     const Vec3* scale, int flags);
    const std::uintptr_t entity = EntityById(gBase, id);
    if (entity == 0) { return true; }
    __try {
        const auto vtable = *reinterpret_cast<const std::uintptr_t*>(entity);
        const auto set = *reinterpret_cast<const SetFn*>(vtable + hn::EntitySetPosRotScale);
        set(entity, &position, &rotation, &size, hn::PlacementFlags);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::uint64_t (*gPlayerCharacter)() = PlayerCharacter;
std::uint64_t (*gFindAttachment)(std::uint64_t, std::string_view) = FindAttachment;
AppliedView (*gLastView)() = LastAppliedView;
bool (*gWorldPlacement)(std::uintptr_t, Placement&) = WorldPlacement;
bool (*gPlaceEntity)(std::uint32_t, const Vec3&, const Quaternion&, const Vec3&) = PlaceEntity;
bool (*gMainThread)() = OnEngineMainThread;

// --- the lights ---------------------------------------------------------------------
// Which of the player's two lights `attachment` is, or -1: a bone attachment of
// the player's character, by name.
int LightOf(std::uintptr_t attachment, std::uint64_t player)
{
    std::uintptr_t vtable = 0, manager = 0, character = 0, name = 0;
    if (player == 0 || !SafeRead(attachment, vtable) || vtable != gBase + hn::BoneVtable ||
        !SafeRead(attachment + hn::AttachManager, manager) || !SafeRead(manager + hn::ManagerCharacter, character) ||
        character != player || !SafeRead(attachment + hn::AttachName, name) || name == 0) {
        return -1;
    }
    for (std::size_t i = 0; i < kLights; ++i) {
        // With the terminator: "flashlight" is not "flashlight_fx".
        if (SafeEqual(reinterpret_cast<const void*>(name), kAttachments[i], std::strlen(kAttachments[i]) + 1)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// A bone attachment on its joint, from its relative default and K.
bool OnJointOf(std::uintptr_t attachment, Pose& onJoint, int& joint)
{
    Pose relative{};
    Quaternion k{};
    if (!SafeCopy(&relative, reinterpret_cast<const void*>(attachment + hn::AttachRelative), sizeof(relative)) ||
        !SafeRead(attachment + hn::AttachK, k) || !SafeRead(attachment + hn::AttachJoint, joint) ||
        !Usable(relative.orientation) || !Usable(k) || !Finite(relative.position)) {
        return false;
    }
    onJoint = headlamp::OnJoint(relative, k);
    return true;
}

// The light this frame: its entity, its mount on `#camera` (the eye, on the
// same joint), and Prey's eye in the world from where Prey would place it.
bool ReadLamp(std::uintptr_t binding, std::uintptr_t attachment, std::uint64_t character, Lamp& out)
{
    const std::uint64_t eyeAttachment = gFindAttachment(character, "#camera");
    std::uintptr_t eyeVtable = 0;
    Pose light{}, eye{};
    int lightJoint = -1, eyeJoint = -1;
    Placement world{};
    if (eyeAttachment == 0 || !SafeRead(eyeAttachment, eyeVtable) || eyeVtable != gBase + hn::BoneVtable ||
        !OnJointOf(attachment, light, lightJoint) || !OnJointOf(eyeAttachment, eye, eyeJoint) ||
        lightJoint != eyeJoint || !SafeRead(binding + hn::BindingEntity, out.entity) ||
        !SafeRead(binding + hn::BindingScale, out.size) || !gWorldPlacement(attachment, world) || !Usable(world.q) ||
        !Finite(world.t) || !std::isfinite(world.s)) {
        return false;
    }
    out.mount = headlamp::Mount(eye, light);
    out.scale = world.s;
    out.size = Scale(out.size, world.s);
    out.eye = headlamp::EyeFromLight({world.q, world.t}, world.s, out.mount);
    return true;
}

void NoteMount(std::size_t light, const Pose& mount)
{
    std::lock_guard lock(gMountMutex);
    const Pose& last = gMount[light];
    if (gMountSeen[light] && Length(Sub(last.position, mount.position)) < 0.0005f &&
        Degrees(Multiply(Conjugate(last.orientation), mount.orientation)) < 0.1f) {
        return;
    }
    gMount[light] = mount;
    gMountSeen[light] = true;
    std::ostringstream s;
    s.precision(1);
    s << std::fixed << "mount light=" << kNames[light] << " mm=" << mount.position.x * 1000.0f << ","
      << mount.position.y * 1000.0f << "," << mount.position.z * 1000.0f << " deg=" << Degrees(mount.orientation);
    Log(s.str());
}

// The light on the headset's eye: Prey's eye moved as the view moves it, the
// light at its mount on it.
bool Put(std::size_t light, const Lamp& lamp, const AppliedView& view)
{
    bool done = false;
    if (Usable(view.rotation) && Finite(view.offset)) {
        const Pose placed = headlamp::OnEye(view.rotation, Add(lamp.eye, view.offset), lamp.scale, lamp.mount);
        done = Usable(placed.orientation) && Finite(placed.position) &&
               gPlaceEntity(lamp.entity, placed.position, placed.orientation, lamp.size);
    }
    if (!done) {
        gUnplaced.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    gPerLight[light].fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool ThisFramesView(const AppliedView& view)
{
    return view.sequence != 0 && view.sequence != gRendered.load(std::memory_order_acquire);
}

// True when the light was placed here, and Prey's placement is not wanted.
bool PlaceOnView(std::uintptr_t binding, std::uintptr_t attachment)
{
    const std::uint64_t player = gPlayerCharacter();
    const int light = LightOf(attachment, player);
    if (light < 0) { return false; }
    if (!gMainThread()) {
        gOffThread.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    Lamp lamp{};
    if (!ReadLamp(binding, attachment, player, lamp)) {
        gUnread.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const auto index = static_cast<std::size_t>(light);
    NoteMount(index, lamp.mount);
    const AppliedView view = gLastView();
    if (!ThisFramesView(view)) {
        // The frame's view is still to come: Prey's placement until then, and
        // the light on it before the render.
        std::lock_guard lock(gPendingMutex);
        gPending[index] = lamp;
        gDeferred.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (!Put(index, lamp, view)) { return false; }   // Prey's placement rather than none
    gPlaced.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void __fastcall ProcessAttachmentDetour(void* binding, void* attachment)
{
    if (gActive.load(std::memory_order_acquire) &&
        PlaceOnView(reinterpret_cast<std::uintptr_t>(binding), reinterpret_cast<std::uintptr_t>(attachment))) {
        return;
    }
    gOriginal.load(std::memory_order_acquire)(binding, attachment);
}

// --- the gate and the hook (main thread) ----------------------------------------
// The three bodies, and the two vtable slots that reach them.
bool GateBodies(std::uintptr_t base)
{
    __try {
        return contract::BodiesMatch(base, hn::Contracts) &&
               *reinterpret_cast<const std::uintptr_t*>(base + hn::BindingVtable + hn::VtableProcessAttachment) ==
                   base + hn::ProcessAttachment &&
               *reinterpret_cast<const std::uintptr_t*>(base + hn::BoneVtable + hn::VtableWorldAbsolute) ==
                   base + hn::BoneWorldAbsolute;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// PreyDll's base, once it is loaded and the bodies are the ones read:
// ERROR_NOT_READY before, ERROR_BAD_EXE_FORMAT for other bytes.
DWORD Resolve(std::uintptr_t& base)
{
    base = PreyBase();
    if (base == 0) { return ERROR_NOT_READY; }
    if (!GateBodies(base)) {
        base = 0;
        return ERROR_BAD_EXE_FORMAT;
    }
    return ERROR_SUCCESS;
}
// Tests replace the engine here.
DWORD (*gResolve)(std::uintptr_t&) = Resolve;

bool Install(std::uintptr_t target)
{
    const hook::Result result = hook::Install(reinterpret_cast<void*>(target),
                                              reinterpret_cast<void*>(&ProcessAttachmentDetour), gOriginal);
    if (!result) {
        Log(std::string("result=failed detail=") + hook::StepName(result.step) +
            " status=" + std::to_string(result.status) + " target=EntityProcessAttachment");
        return false;
    }
    return true;
}
// Tests replace the installer here.
bool (*gInstall)(std::uintptr_t) = Install;

// Once: a refused gate or a failed hook stays refused. Before PreyDll is loaded
// it is not yet tried.
bool InstallHook()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstallTried) { return gInstalled.load(std::memory_order_acquire); }
    std::uintptr_t base = 0;
    const DWORD ready = gResolve(base);
    if (ready == ERROR_NOT_READY) {
        Log("result=unavailable detail=no_preydll");
        return false;
    }
    gInstallTried = true;
    if (ready != ERROR_SUCCESS) {
        gGate = "refused";
        Log("result=unavailable detail=bytes_mismatch target=EntityProcessAttachment");
        return false;
    }
    gBase = base;
    if (!gInstall(base + hn::ProcessAttachment)) {
        gGate = "hook_failed";
        return false;
    }
    gGate = "ok";
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=hook_installed target=EntityProcessAttachment rows=" + std::to_string(std::size(hn::Contracts)));
    return true;
}

void ClearPending()
{
    std::lock_guard lock(gPendingMutex);
    gPending = {};
}

} // namespace

void EnsureHeadLamp()
{
    // PREYVR_HEAD_LAMP=0: Prey's lights, not even hooked.
    if (EnvSwitchOff(L"PREYVR_HEAD_LAMP")) {
        gActive.store(false, std::memory_order_release);
        Log("result=0 detail=disabled_by_environment hooks=0");
        return;
    }
    gActive.store(InstallHook(), std::memory_order_release);
}

void StopHeadLamp()
{
    gActive.store(false, std::memory_order_release);
    ClearPending();
}

void PlaceHeadLampsBeforeRender()
{
    std::array<std::optional<Lamp>, kLights> pending{};
    {
        std::lock_guard lock(gPendingMutex);
        pending.swap(gPending);
    }
    if (!gActive.load(std::memory_order_acquire)) { return; }
    const AppliedView view = gLastView();
    for (std::size_t i = 0; i < kLights; ++i) {
        if (!pending[i]) { continue; }
        if (!ThisFramesView(view)) {
            gNative.fetch_add(1, std::memory_order_relaxed);   // no view this frame: Prey's placement stays
        } else if (Put(i, *pending[i], view)) {
            gLate.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void EndHeadLampFrame()
{
    gRendered.store(gLastView().sequence, std::memory_order_release);
    ClearPending();
}

std::string HeadLampReport()
{
    std::ostringstream s;
    s << "flashlight={active=" << (gActive.load() ? 1 : 0) << " hooks=" << (gInstalled.load() ? 1 : 0)
      << " gate=" << gGate.load() << " placed=" << gPlaced.load() << " deferred=" << gDeferred.load()
      << " late=" << gLate.load() << " native=" << gNative.load() << " unread=" << gUnread.load()
      << " offThread=" << gOffThread.load() << " unplaced=" << gUnplaced.load();
    std::lock_guard lock(gMountMutex);
    for (std::size_t i = 0; i < kLights; ++i) {
        s << " " << kNames[i] << "=" << gPerLight[i].load() << " " << kNames[i] << "MountMm=";
        if (!gMountSeen[i]) {
            s << "none";
            continue;
        }
        const Vec3 mm = Scale(gMount[i].position, 1000.0f);
        s << std::lround(mm.x) << "," << std::lround(mm.y) << "," << std::lround(mm.z);
    }
    s << "}";
    return s.str();
}

} // namespace preyvr::dll
