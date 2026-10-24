#include "ArmsLane.h"
#include "CommandArgs.h"
#include "SafeMemory.h"
#include "EngineAccess.h"

#include "GameplayFrame.h"
#include "AnimIkTakeover.h"
#include "InputPost.h"
#include "InteractionLane.h"
#include "Logger.h"
#include "MemoryCommands.h"
#include "WeaponAttachment.h"
#include "preyvr/EngineMap.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <array>
#include <atomic>

namespace preyvr::dll {
namespace {

void Log(const std::string& line) { lifecycle::Log("preyvr_arms " + line); }

// --- holster / draw ---------------------------------------------------------
//
// Holding the right grip holds the native use/reload button, and holding that
// is the game's own holster (UnequipWeapon): no weapon, the arms lowered out of
// view (the IK's arms.free brings them back to the controllers). The game draws
// again on the trigger, which a VR player would expect to fire, so a right-grip
// press with nothing drawn draws the last weapon: the native Equip, with the
// gates Funk's body holsters use (BodyEquipment.cpp, EquipmentNative.h).
//   ArkPlayerWeaponComponent = player+0x14B8: +0x48 player, +0x58 equipped id,
//   +0x5C last equipped id, +0x64 id to re-equip (set while carrying),
//   +0x78 unequip in progress. Equip 0x1274820 (component, id),
//   CanEquip 0x1273EB0 (component), GetPlayer 0x157C990.
//   ArkPlayerInput = player+0x8E0 (vtable 0x1E580E0): +0x94 cinematic input.
//   player+0x7BC: items restricted.
constexpr std::uintptr_t kWeaponComponent = 0x14B8;
constexpr std::uintptr_t kEquipRva = 0x1274820, kCanEquipRva = 0x1273EB0, kGetPlayerRva = engine::ArkPlayerLayout::getInstanceRva;
constexpr std::uintptr_t kInputOffset = engine::ArkPlayerLayout::input, kInputVtableRva = engine::ArkPlayerInputLayout::vtableRva;
constexpr std::array<std::uint8_t, 16> kEquipPrologue{0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                                                      0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48};
constexpr std::array<std::uint8_t, 16> kCanEquipPrologue{0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
                                                         0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
constexpr std::array<std::uint8_t, 7> kGetPlayerPrologue{0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x0D};

std::atomic<bool> gHolstered{false};
std::atomic<bool> gDrawRequested{false};
std::atomic<int> gNativeChecked{0};   // 0 unknown, 1 bytes match, -1 refused
std::atomic<unsigned long long> gDraws{0}, gDrawRefused{0};
std::atomic<int> gLastDrawResult{-1};
std::atomic<std::uint32_t> gLastWeaponId{0};

bool NativeMatches(std::uintptr_t base)
{
    int checked = gNativeChecked.load();
    if (checked == 0) {
        const bool ok =
            std::memcmp(reinterpret_cast<void*>(base + kEquipRva), kEquipPrologue.data(), kEquipPrologue.size()) == 0 &&
            std::memcmp(reinterpret_cast<void*>(base + kCanEquipRva), kCanEquipPrologue.data(), kCanEquipPrologue.size()) == 0 &&
            std::memcmp(reinterpret_cast<void*>(base + kGetPlayerRva), kGetPlayerPrologue.data(), kGetPlayerPrologue.size()) == 0;
        checked = ok ? 1 : -1;
        gNativeChecked.store(checked);
        Log(std::string("result=") + (ok ? "0" : "1") + " detail=native_bytes " + (ok ? "match" : "mismatch"));
    }
    return checked == 1;
}

struct WeaponState {
    std::uint32_t equipped = 0, last = 0, reequip = 0;
    bool unequipping = false, cinematic = true, restricted = true;
};

bool ReadWeaponState(std::uintptr_t base, std::uintptr_t player, WeaponState& out)
{
    __try {
        const auto component = player + kWeaponComponent;
        const auto input = player + kInputOffset;
        if (*reinterpret_cast<const std::uintptr_t*>(component + 0x48) != player ||
            *reinterpret_cast<const std::uintptr_t*>(input) != base + kInputVtableRva) { return false; }
        out.equipped = *reinterpret_cast<const std::uint32_t*>(component + 0x58);
        out.last = *reinterpret_cast<const std::uint32_t*>(component + 0x5C);
        out.reequip = *reinterpret_cast<const std::uint32_t*>(component + 0x64);
        out.unequipping = *reinterpret_cast<const std::uint8_t*>(component + 0x78) != 0;
        out.cinematic = *reinterpret_cast<const int*>(input + 0x94) != 0;
        out.restricted = *reinterpret_cast<const std::uint8_t*>(player + 0x7BC) != 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// 0 drawn (the native equip accepted it); otherwise why not.
int DrawNative(std::uintptr_t base, std::uintptr_t player, std::uint32_t id)
{
    __try {
        if (reinterpret_cast<std::uintptr_t(__fastcall*)()>(base + kGetPlayerRva)() != player) { return 2; }
        const auto component = player + kWeaponComponent;
        if (!reinterpret_cast<bool(__fastcall*)(std::uintptr_t)>(base + kCanEquipRva)(component)) { return 3; }
        return reinterpret_cast<bool(__fastcall*)(std::uintptr_t, std::uint32_t)>(base + kEquipRva)(component, id) ? 0 : 4;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 5; }
}

bool Copy(std::uint64_t address, void* out, std::size_t size)
{
    return SafeCopy(out, reinterpret_cast<const void*>(address), size);
}

// The character's attachment manager (character+0x18) keeps a DynArray of IAttachment* at
// manager+0x20; the element count is the low 31 bits of the dword before the data.
struct AttachmentList {
    unsigned count = 0;
    std::uint64_t items[64]{};
};
bool ReadAttachments(std::uint64_t character, AttachmentList& out)
{
    std::uint64_t data = 0;
    std::uint32_t header = 0;
    if (character == 0 || !Copy(character + 0x38, &data, 8) || data == 0 || !Copy(data - 4, &header, 4)) { return false; }
    out.count = (std::min)(header & 0x7FFFFFFFu, 64u);
    return Copy(data, out.items, out.count * 8);
}

bool SetHiddenBits(std::uint64_t attachment, bool hide)
{
    __try {
        auto* flags = reinterpret_cast<std::uint32_t*>(attachment + 8);
        *flags = hide ? (*flags | 0x70000u) : (*flags & 0xFFF8FFFFu);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool ArmsHolstered() { return gHolstered.load(std::memory_order_acquire); }

void RequestDrawWeapon() { gDrawRequested.store(true, std::memory_order_release); }

void UpdateArmsLane(const GameplayPoseFrame& frame, bool valid)
{
    if (!OnEngineMainThread()) { return; }
    const auto base = PreyBase();
    WeaponState state{};
    const bool read = valid && base && frame.player && ReadWeaponState(base, frame.player, state);
    // Holstered: nothing drawn, a weapon to draw, not mid-unequip, not a carry
    // (a carry keeps the weapon to re-equip at +0x64), not a cinematic.
    const bool holstered = read && state.equipped == 0 && state.last != 0 && state.reequip == 0 &&
        !state.unequipping && !state.cinematic && !state.restricted && !UseCarrying();
    gHolstered.store(holstered, std::memory_order_release);
    if (read) { gLastWeaponId.store(state.last, std::memory_order_relaxed); }
    if (!gDrawRequested.exchange(false, std::memory_order_acq_rel)) { return; }
    int result = 1;
    if (holstered && NativeMatches(base)) { result = DrawNative(base, frame.player, state.last); }
    gLastDrawResult.store(result);
    (result == 0 ? gDraws : gDrawRefused).fetch_add(1);
    Log("result=" + std::to_string(result) + " detail=draw weapon=" + std::to_string(state.last));
}

int AttachmentJoint(std::uint64_t character, std::string_view name)
{
    AttachmentList list{};
    if (!ReadAttachments(character, list)) { return -1; }
    for (unsigned i = 0; i < list.count; ++i) {
        std::uint64_t namePtr = 0;
        int joint = -1;
        if (Copy(list.items[i] + 0x10, &namePtr, 8) && SafeText(namePtr) == name && Copy(list.items[i] + 0x15C, &joint, 4)) {
            return joint;
        }
    }
    return -1;
}

std::string ArmsReport()
{
    std::ostringstream out;
    out << " holstered=" << (gHolstered.load() ? 1 : 0) << " lastWeapon=" << gLastWeaponId.load()
        << " draws=" << gDraws.load() << " drawRefused=" << gDrawRefused.load()
        << " lastDraw=" << gLastDrawResult.load() << " native=" << gNativeChecked.load() << AnimIkFreeArmsReport();
    return out.str();
}

bool ExecuteArmsCommand(const std::vector<std::string>& args, std::ostringstream& out)
{
    const std::string& verb = args[0];
    if (verb.rfind("arms.", 0) != 0) { return false; }
    const std::uint64_t dll = PreyBase();
    if (verb == "arms.att") {
        // arms.att [character]: every attachment of the arms (or the given character).
        std::uint64_t character = AnimIkOwnerCharacter();
        if (args.size() > 1) { ResolveAddress(args[1], character); }
        AttachmentList list{};
        if (!ReadAttachments(character, list)) { out << "arms.att result=2"; return true; }
        out << "arms.att result=0 character=0x" << std::hex << character << std::dec << " count=" << list.count;
        for (unsigned i = 0; i < list.count; ++i) {
            const std::uint64_t a = list.items[i];
            std::uint64_t vt = 0, namePtr = 0, binding = 0;
            std::uint32_t flags = 0;
            int joint = -1;
            Copy(a, &vt, 8);
            Copy(a + 8, &flags, 4);
            Copy(a + 0x10, &namePtr, 8);
            Copy(a + 0x20, &binding, 8);
            Copy(a + 0x15C, &joint, 4);
            out << "\n " << i << " vt=0x" << std::hex << (vt >= dll ? vt - dll : vt) << " flags=0x" << flags
                << " binding=0x" << binding << std::dec << " joint=" << joint << " name=" << SafeText(namePtr);
        }
    } else if (verb == "arms.hide") {
        // arms.hide <index> <0|1>: HideAttachment's own bits (0x70000) on one attachment (test only).
        AttachmentList list{};
        const unsigned index = static_cast<unsigned>(Arg(args, 1, 999));
        if (!ReadAttachments(AnimIkOwnerCharacter(), list) || index >= list.count) { out << "arms.hide result=2"; return true; }
        out << "arms.hide result=" << (SetHiddenBits(list.items[index], args.size() > 2 && args[2] == "1") ? 0 : 3);
    } else if (verb == "arms.joints") {
        // arms.joints [name-substring]: model-space position of the arms' joints (final pose).
        const std::uint64_t character = AnimIkOwnerCharacter();
        const std::string filter = args.size() > 1 ? args[1] : "hand";
        std::uint64_t skeleton = 0, joints = 0, absolute = 0;
        int count = 0, jointCount = 0;
        if (!Copy(character + 0x10, &skeleton, 8) || !Copy(skeleton + 0x08, &joints, 8) ||
            !Copy(joints - 4, &jointCount, 4) || !Copy(character + 0x968, &count, 4) ||
            !Copy(character + 0x978, &absolute, 8) || count <= 0 || count > 768) {
            out << "arms.joints result=2";
            return true;
        }
        out << "arms.joints result=0 count=" << count << " skeletonJoints=" << jointCount;
        for (int j = 0; j < count; ++j) {
            std::uint64_t namePtr = 0;
            Copy(joints + static_cast<std::uint64_t>(j) * 0xA8, &namePtr, 8);
            const std::string name = SafeText(namePtr);
            if (filter != "all" && name.find(filter) == std::string::npos) { continue; }
            float qt[7]{};
            Copy(absolute + static_cast<std::uint64_t>(j) * 28, qt, sizeof(qt));
            out << "\n " << j << " " << name << " t=" << qt[4] << "," << qt[5] << "," << qt[6];
        }
    } else if (verb == "arms.free") {
        if (args.size() > 1) { SetAnimIkFreeArms(args[1] != "0"); }
        out << "arms.free result=0" << ArmsReport();
    } else if (verb == "arms.draw") {
        RequestDrawWeapon();
        out << "arms.draw result=0 (requested)";
    } else if (verb == "arms.report") {
        GameplayPoseFrame frame{};
        const bool haveFrame = TryGetGameplayPoseFrame(frame, false);
        EquippedRig rig{};
        const bool haveRig = haveFrame && TryGetEquippedRig(frame.player, rig);
        out << "arms.report result=0" << std::hex << " player=0x" << (haveFrame ? frame.player : 0)
            << " ikOwner=0x" << AnimIkOwnerCharacter() << " rig=" << haveRig << " weapon=0x" << rig.weapon
            << " character=0x" << rig.character << " attachment=0x" << rig.attachment << std::dec
            << ArmsReport();
    } else {
        return false;
    }
    return true;
}

}  // namespace preyvr::dll
