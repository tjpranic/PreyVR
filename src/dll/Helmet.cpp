#include "Helmet.h"
#include "AnimIkTakeover.h"
#include "ArmsLane.h"
#include "EngineAccess.h"
#include "EnvSwitch.h"
#include "HeadTrackingHook.h"
#include "HookInstall.h"
#include "Logger.h"
#include "SafeMemory.h"
#include "preyvr/HeadLampNative.h"
#include "preyvr/Helmet.h"
#include "preyvr/HelmetNative.h"
#include "preyvr/NativeContract.h"
#include "preyvr/StickRereadNative.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string_view>

namespace preyvr::dll {
namespace {
namespace hm = helmet::native;
namespace hn = headlamp::native;
namespace sn = stick::native;

void Log(const std::string& line) { lifecycle::Log("preyvr_helmet " + line); }

static_assert(contract::Gated(hm::Contracts, hm::ModelRelative) && contract::Gated(hm::Contracts, hm::RelativeDefault) &&
              contract::Gated(hm::Contracts, hm::Hide) && contract::Gated(hm::Contracts, hm::EntityGetWorldTm));
static_assert(contract::Gated(hm::Contracts, hm::CharacterRender) && contract::Gated(hm::Contracts, hm::RenderAttachment));
static_assert(contract::Gated(sn::Contracts, sn::PlayerSingleton));
static_assert(hm::CharacterVtable == engine::CharacterInstanceLayout::vtableRva);
// The model transform is a QuatT as Prey lays it out: q xyzw, then t.
static_assert(sizeof(Pose) == 28 && hm::AttachModel + sizeof(Pose) == hn::AttachK);

std::uintptr_t gBase = 0;   // PreyDll's, once the gate passed
std::atomic<bool> gActive{false};
std::atomic<bool> gShown{true};
// VR Options' Helmet Height, Forward / Back (mm, along the eyes' axes) and Size (%).
std::atomic<int> gHeightMm{0}, gForwardMm{0};
std::atomic<unsigned> gSizePercent{100};

// The size: the helmet's character, while it is drawn on the head at a size
// other than Prey's, and the scale its Render is given. Set before the render;
// read by any thread that renders.
using RenderFn = void(__fastcall*)(void* character, void* params, const float* offset, void* pass, std::uintptr_t flags);
std::atomic<RenderFn> gRenderOriginal{nullptr};
std::atomic<std::uintptr_t> gScaledCharacter{0};
std::atomic<float> gScale{1.0f};
std::atomic<bool> gScaleHooked{false};
std::atomic<std::uint64_t> gScaledDraws{0};
std::mutex gGateMutex;
bool gGateTried = false;
std::atomic<const char*> gGate{"unchecked"};

// What this frame changed before the render, settled after it. Main thread,
// but VR stop may come from another, so it is locked.
std::mutex gFrameMutex;
struct Hidden {
    std::uintptr_t attachment = 0;
    std::uint32_t bits = 0;   // Prey's own hide bits, given back
};
Hidden gHidden{};
struct Written {
    std::uintptr_t attachment = 0;
    Pose model{};
};
Written gWritten{};
// LastAppliedView's sequence when the last frame was rendered: a newer one is
// this frame's view.
std::atomic<unsigned long long> gRendered{0};

std::atomic<std::uint64_t> gPlaced{0}, gHiddenFrames{0}, gStowed{0}, gNative{0}, gNoHelmet{0}, gUnread{0},
    gUnplaced{0}, gKept{0}, gOverwritten{0};
// The mount as last read (one log line a change), and how far the eye Prey
// composed, its position taken relative to the player entity, sits from the
// `#camera` the body draws: the check of where the character is drawn.
std::mutex gStatsMutex;
Pose gMount{};
bool gMountSeen = false;
float gEyeGapMm = 0.0f, gEyeGapDeg = 0.0f, gEyeGapMaxMm = 0.0f, gEyeGapMaxDeg = 0.0f;

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

// Where the player's character is drawn: its entity's world matrix.
bool PlayerLocation(Pose& location, float& scale)
{
    using PlayerFn = std::uintptr_t(__fastcall*)();
    float matrix[12]{};
    __try {
        const std::uintptr_t player = reinterpret_cast<PlayerFn>(gBase + sn::PlayerSingleton)();
        if (player == 0) { return false; }
        const auto entity = *reinterpret_cast<const std::uintptr_t*>(player + hm::PlayerEntity);
        if (entity == 0 || *reinterpret_cast<const std::uintptr_t*>(entity) != gBase + hm::EntityVtable) { return false; }
        std::memcpy(matrix, reinterpret_cast<const void*>(entity + hm::EntityWorldTm), sizeof(matrix));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return helmet::FromMatrix(matrix, location, scale);
}

bool WriteModel(std::uintptr_t attachment, const Pose& model)
{
    __try {
        std::memcpy(reinterpret_cast<void*>(attachment + hm::AttachModel), &model, sizeof(model));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// HideAttachment's bits: `bits` in place of the ones there.
bool WriteHideBits(std::uintptr_t attachment, std::uint32_t bits)
{
    __try {
        auto* flags = reinterpret_cast<std::uint32_t*>(attachment + hm::AttachFlags);
        *flags = (*flags & ~hm::HideBits) | (bits & hm::HideBits);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::uint64_t (*gPlayerCharacter)() = PlayerCharacter;
std::uint64_t (*gFindAttachment)(std::uint64_t, std::string_view) = FindAttachment;
AppliedView (*gLastView)() = LastAppliedView;
bool (*gPlayerLocation)(Pose&, float&) = PlayerLocation;

// --- the size: CCharInstance::Render (0x81BCB0) -----------------------------------
// The helmet's character is drawn with its offset's scale times Helmet Size;
// every other character, and the helmet at Prey's size, as Prey draws it.
void __fastcall RenderDetour(void* character, void* params, const float* offset, void* pass, std::uintptr_t flags)
{
    const RenderFn original = gRenderOriginal.load(std::memory_order_acquire);
    const std::uintptr_t helmet = gScaledCharacter.load(std::memory_order_acquire);
    if (helmet != 0 && reinterpret_cast<std::uintptr_t>(character) == helmet && offset != nullptr) {
        float scaled[hm::OffsetFloats];
        std::memcpy(scaled, offset, sizeof(scaled));
        scaled[hm::OffsetScale] *= gScale.load(std::memory_order_relaxed);
        gScaledDraws.fetch_add(1, std::memory_order_relaxed);
        original(character, params, scaled, pass, flags);
        return;
    }
    original(character, params, offset, pass, flags);
}

bool InstallScale(std::uintptr_t target)
{
    const hook::Result result =
        hook::Install(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&RenderDetour), gRenderOriginal);
    if (!result) {
        Log(std::string("result=failed detail=") + hook::StepName(result.step) +
            " status=" + std::to_string(result.status) + " target=CharacterRender size=prey");
        return false;
    }
    return true;
}
// Tests replace the installer here.
bool (*gInstallScale)(std::uintptr_t) = InstallScale;

// The helmet's character, if its object is the character attachment Prey uses.
std::uintptr_t HelmetCharacter(std::uintptr_t helmet)
{
    std::uintptr_t object = 0, vtable = 0, character = 0;
    return SafeRead(helmet + hm::AttachObject, object) && object != 0 && SafeRead(object, vtable) &&
                   vtable == gBase + hm::ChrAttachmentVtable && SafeRead(object + hm::ObjectCharacter, character)
               ? character
               : 0;
}

// --- the helmet -------------------------------------------------------------------
struct Rig {
    std::uintptr_t helmet = 0;
    std::uint32_t flags = 0;
    Pose eyeModel{};   // `#camera`'s model transform this frame
    Pose mount{};      // the helmet on the eye, without the sway
};

// A bone attachment and its joint, relative default and K.
bool Bone(std::uintptr_t attachment, int& joint, Pose& relative, Quaternion& k)
{
    std::uintptr_t vtable = 0;
    return SafeRead(attachment, vtable) && vtable == gBase + hn::BoneVtable && SafeRead(attachment + hn::AttachJoint, joint) &&
           SafeCopy(&relative, reinterpret_cast<const void*>(attachment + hn::AttachRelative), sizeof(relative)) &&
           SafeRead(attachment + hn::AttachK, k) && Usable(k);
}

// The player's helmet and its eye, both on one joint. `found` says whether
// there is a helmet at all.
bool ReadRig(std::uint64_t character, Rig& out, bool& found)
{
    out.helmet = static_cast<std::uintptr_t>(gFindAttachment(character, "helmet"));
    found = out.helmet != 0;
    const auto eye = static_cast<std::uintptr_t>(gFindAttachment(character, "#camera"));
    int helmetJoint = -1, eyeJoint = -1;
    Pose helmetRelative{}, eyeRelative{};
    Quaternion helmetK{}, eyeK{};
    if (!found || eye == 0 || !Bone(out.helmet, helmetJoint, helmetRelative, helmetK) ||
        !Bone(eye, eyeJoint, eyeRelative, eyeK) || helmetJoint != eyeJoint || !Usable(eyeRelative.orientation) ||
        !Finite(eyeRelative.position) || !SafeRead(out.helmet + hm::AttachFlags, out.flags) ||
        !SafeCopy(&out.eyeModel, reinterpret_cast<const void*>(eye + hm::AttachModel), sizeof(out.eyeModel)) ||
        !Usable(out.eyeModel.orientation) || !Finite(out.eyeModel.position)) {
        return false;
    }
    out.mount = helmet::Mount(headlamp::OnJoint(eyeRelative, eyeK), helmetK);
    return true;
}

void NoteMount(const Pose& mount)
{
    std::lock_guard lock(gStatsMutex);
    if (gMountSeen && Length(Sub(gMount.position, mount.position)) < 0.0005f &&
        Degrees(Multiply(Conjugate(gMount.orientation), mount.orientation)) < 0.1f) {
        return;
    }
    gMount = mount;
    gMountSeen = true;
    std::ostringstream s;
    s.precision(1);
    s << std::fixed << "mount mm=" << mount.position.x * 1000.0f << "," << mount.position.y * 1000.0f << ","
      << mount.position.z * 1000.0f << " deg=" << Degrees(mount.orientation);
    Log(s.str());
}

void NoteEyeGap(const Pose& prey, const Pose& drawn)
{
    const float mm = Length(Sub(prey.position, drawn.position)) * 1000.0f;
    const float deg = Degrees(Multiply(Conjugate(Normalize(prey.orientation)), drawn.orientation));
    std::lock_guard lock(gStatsMutex);
    gEyeGapMm = mm;
    gEyeGapDeg = deg;
    gEyeGapMaxMm = (std::max)(gEyeGapMaxMm, mm);
    gEyeGapMaxDeg = (std::max)(gEyeGapMaxDeg, deg);
}

bool ThisFramesView(const AppliedView& view)
{
    return view.sequence != 0 && view.sequence != gRendered.load(std::memory_order_acquire);
}

// Prey's hide bits back, after a render the helmet was hidden from. Under
// gFrameMutex.
void Unhide()
{
    if (gHidden.attachment != 0) { WriteHideBits(gHidden.attachment, gHidden.bits); }
    gHidden = {};
}

// --- the gate (main thread) -------------------------------------------------------
bool SlotIs(std::uintptr_t base, std::uintptr_t vtable, std::size_t offset, std::uintptr_t target)
{
    return *reinterpret_cast<const std::uintptr_t*>(base + vtable + offset) == base + target;
}
// The four bodies, the bone vtable's three slots and the entity's GetWorldTM,
// and the player getter StickReread pins.
bool GateBodies(std::uintptr_t base)
{
    __try {
        bool player = false;
        for (const auto& row : sn::Contracts) {
            if (row.rva == sn::PlayerSingleton) { player = contract::BodyMatches(base, row); }
        }
        return player && contract::BodiesMatch(base, hm::Contracts) &&
               SlotIs(base, hn::BoneVtable, hm::VtableModelRelative, hm::ModelRelative) &&
               SlotIs(base, hn::BoneVtable, hm::VtableRelativeDefault, hm::RelativeDefault) &&
               SlotIs(base, hn::BoneVtable, hm::VtableHide, hm::Hide) &&
               SlotIs(base, hm::EntityVtable, hm::EntityWorldTmSlot, hm::EntityGetWorldTm) &&
               SlotIs(base, hm::ChrAttachmentVtable, hm::VtableRenderAttachment, hm::RenderAttachment) &&
               SlotIs(base, hm::CharacterVtable, hm::VtableRender, hm::CharacterRender);
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

// Once: a refused gate stays refused. Before PreyDll is loaded it is not yet tried.
bool CheckGate()
{
    std::lock_guard lock(gGateMutex);
    if (gGateTried) { return gBase != 0; }
    std::uintptr_t base = 0;
    const DWORD ready = gResolve(base);
    if (ready == ERROR_NOT_READY) {
        Log("result=unavailable detail=no_preydll");
        return false;
    }
    gGateTried = true;
    if (ready != ERROR_SUCCESS) {
        gGate = "refused";
        Log("result=unavailable detail=bytes_mismatch target=HelmetAttachment");
        return false;
    }
    gBase = base;
    gGate = "ok";
    // Without the size hook the helmet still goes on the head, at Prey's size.
    gScaleHooked.store(gInstallScale(base + hm::CharacterRender), std::memory_order_release);
    Log("result=0 detail=gate_ok target=HelmetAttachment rows=" + std::to_string(std::size(hm::Contracts)));
    return true;
}

} // namespace

void EnsureHelmet()
{
    // PREYVR_HELMET=0: Prey's helmet, untouched.
    if (EnvSwitchOff(L"PREYVR_HELMET")) {
        gActive.store(false, std::memory_order_release);
        Log("result=0 detail=disabled_by_environment");
        return;
    }
    gActive.store(CheckGate(), std::memory_order_release);
}

void StopHelmet()
{
    gActive.store(false, std::memory_order_release);
    gScaledCharacter.store(0, std::memory_order_release);
    std::lock_guard lock(gFrameMutex);
    Unhide();
    gWritten = {};
}

void SetHelmetShown(unsigned shown)
{
    gShown.store(shown != 0, std::memory_order_release);
    Log(std::string("result=0 detail=shown value=") + (shown != 0 ? "1" : "0"));
}

unsigned HelmetShown() { return gShown.load(std::memory_order_acquire) ? 1u : 0u; }

void SetHelmetHeightMm(int mm) { gHeightMm.store(mm, std::memory_order_release); }
int HelmetHeightMm() { return gHeightMm.load(std::memory_order_acquire); }
void SetHelmetForwardMm(int mm) { gForwardMm.store(mm, std::memory_order_release); }
int HelmetForwardMm() { return gForwardMm.load(std::memory_order_acquire); }
void SetHelmetSizePercent(unsigned percent) { gSizePercent.store(percent, std::memory_order_release); }
unsigned HelmetSizePercent() { return gSizePercent.load(std::memory_order_acquire); }

void PlaceHelmetBeforeRender()
{
    if (!gActive.load(std::memory_order_acquire)) { return; }
    std::lock_guard lock(gFrameMutex);
    // Prey's size unless this frame puts the helmet on the head (below).
    gScaledCharacter.store(0, std::memory_order_release);
    const std::uint64_t character = gPlayerCharacter();
    if (character == 0) { return; }   // no player rig: nothing to do
    Rig rig{};
    bool found = false;
    if (!ReadRig(character, rig, found)) {
        (found ? gUnread : gNoHelmet).fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if ((rig.flags & hm::HideMainPass) != 0) {
        gStowed.fetch_add(1, std::memory_order_relaxed);   // Prey has it put away
        return;
    }
    NoteMount(rig.mount);
    if (!gShown.load(std::memory_order_acquire)) {
        if (WriteHideBits(rig.helmet, hm::HideBits)) {
            gHidden = {rig.helmet, rig.flags & hm::HideBits};
            gHiddenFrames.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }
    const AppliedView view = gLastView();
    if (!ThisFramesView(view)) {
        gNative.fetch_add(1, std::memory_order_relaxed);   // VR is not driving the view: Prey's helmet
        return;
    }
    Pose location{};
    float scale = 1.0f;
    if (!Usable(view.rotation) || !Finite(view.offset) || !Finite(view.prey.position) ||
        !Usable(view.prey.orientation) || !gPlayerLocation(location, scale)) {
        gUnread.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // In first person Prey's view position is relative to the player entity
    // (preyvr/Helmet.h), so only its rotation and the head's offset from it go
    // into the helmet; its position is only checked.
    NoteEyeGap({view.prey.orientation, Add(location.position, view.prey.position)},
               helmet::InWorld(location, scale, rig.eyeModel));
    const Pose eye =
        helmet::EyeInModel(location.orientation, scale, rig.eyeModel, view.rotation, view.offset);
    const Vec3 shift = Scale(Vec3{0.0f, static_cast<float>(gForwardMm.load(std::memory_order_acquire)),
                                  static_cast<float>(gHeightMm.load(std::memory_order_acquire))},
                             0.001f / scale);
    const Pose model = helmet::OnEye(eye, rig.mount, shift);
    if (!Usable(model.orientation) || !Finite(model.position) || !WriteModel(rig.helmet, model)) {
        gUnplaced.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    gWritten = {rig.helmet, model};
    gPlaced.fetch_add(1, std::memory_order_relaxed);
    const unsigned size = gSizePercent.load(std::memory_order_acquire);
    if (size != 100 && gScaleHooked.load(std::memory_order_acquire)) {
        gScale.store(static_cast<float>(size) / 100.0f, std::memory_order_relaxed);
        gScaledCharacter.store(HelmetCharacter(rig.helmet), std::memory_order_release);
    }
}

void EndHelmetFrame()
{
    std::lock_guard lock(gFrameMutex);
    Unhide();
    if (gWritten.attachment != 0) {
        // Still what was written: nothing in the render made it again.
        Pose now{};
        const bool kept = SafeCopy(&now, reinterpret_cast<const void*>(gWritten.attachment + hm::AttachModel), sizeof(now)) &&
                          std::memcmp(&now, &gWritten.model, sizeof(now)) == 0;
        (kept ? gKept : gOverwritten).fetch_add(1, std::memory_order_relaxed);
        gWritten = {};
    }
    gRendered.store(gLastView().sequence, std::memory_order_release);
}

std::string HelmetReport()
{
    std::ostringstream s;
    s << "helmet={active=" << (gActive.load() ? 1 : 0) << " gate=" << gGate.load() << " shown=" << HelmetShown()
      << " placed=" << gPlaced.load() << " hidden=" << gHiddenFrames.load() << " stowed=" << gStowed.load()
      << " native=" << gNative.load() << " noHelmet=" << gNoHelmet.load() << " unread=" << gUnread.load()
      << " unplaced=" << gUnplaced.load() << " kept=" << gKept.load() << " overwritten=" << gOverwritten.load()
      << " heightMm=" << HelmetHeightMm() << " forwardMm=" << HelmetForwardMm() << " sizePct=" << HelmetSizePercent()
      << " sizeHook=" << (gScaleHooked.load() ? 1 : 0) << " sized=" << (gScaledCharacter.load() != 0 ? 1 : 0)
      << " sizedDraws=" << gScaledDraws.load();
    std::lock_guard lock(gStatsMutex);
    s << " mountMm=";
    if (gMountSeen) {
        const Vec3 mm = Scale(gMount.position, 1000.0f);
        s << std::lround(mm.x) << "," << std::lround(mm.y) << "," << std::lround(mm.z);
    } else {
        s << "none";
    }
    s.precision(1);
    s << std::fixed << " eyeGapMm=" << gEyeGapMm << " eyeGapDeg=" << gEyeGapDeg << " eyeGapMaxMm=" << gEyeGapMaxMm
      << " eyeGapMaxDeg=" << gEyeGapMaxDeg << "}";
    return s.str();
}

} // namespace preyvr::dll
