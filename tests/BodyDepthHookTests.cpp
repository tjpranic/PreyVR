// The body-depth lane: the production detour with Prey replaced at its ABI
// boundary -- the slot render's original, an entity slot, its render params,
// the character, the pass info, the player that leads to the character, the
// gate and the installer.
#include "../src/dll/BodyDepthHook.cpp"
#include "DllTestSupport.h"
#include "TestSupport.h"
#include <cstring>
#include <iostream>
#include <string>

PREYVR_TEST_CAPTURE_LOG()
PREYVR_TEST_STUB_MINHOOK()
namespace {
using namespace preyvr::dll;
using preyvr::test::Check;
using preyvr::test::Logged;
namespace bn = preyvr::bodydepth::native;
namespace engine = preyvr::engine;

// "bodyDepth={depth=1 ... unread=0}" with each field between spaces.
std::string Report()
{
    std::string report = BodyDepthReport() + " ";
    for (char& c : report) {
        if (c == '{' || c == '}') { c = ' '; }
    }
    return report;
}
bool Reports(const char* field) { return Report().find(std::string(" ") + field + " ") != std::string::npos; }

template <class T> void Put(void* base, std::size_t offset, const T& value)
{
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(value));
}
template <class T> T Get(const void* base, std::size_t offset)
{
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

constexpr std::uintptr_t kBase = 0x180000000;
alignas(16) std::uint8_t slot[0x100]{}, otherSlot[0x100]{}, params[0x100]{}, pass[0x40]{};
alignas(16) std::uint8_t character[0xB00]{}, otherCharacter[0xB00]{}, animated[0x800]{}, player[0x1900]{};
void* const kEntity = reinterpret_cast<void*>(std::uintptr_t{0xE0});
void* const kProxy = reinterpret_cast<void*>(std::uintptr_t{0xF0});
constexpr std::uintptr_t kLod = 0x55;

// The player's slot as Prey leaves it: rendered and nearest, its object flags
// carrying FOB_NEAREST, its character drawing its model.
void ResetSlot(std::uint8_t* at, std::uint8_t* of)
{
    std::memset(at, 0, 0x100);
    Put(at, bn::SlotFlags, std::uint16_t{0x0003});
    Put(at, bn::SlotObjectFlags, std::uint32_t{bn::ObjectNearest | 0x4000007u});
    Put(at, bn::SlotCharacter, reinterpret_cast<std::uintptr_t>(of));
}
void Reset()
{
    ResetSlot(slot, character);
    ResetSlot(otherSlot, otherCharacter);
    std::memset(params, 0, sizeof(params));
    Put(params, bn::ParamsObjectFlags, std::uint64_t{0x100});
    Put(character, 0, kBase + engine::CharacterInstanceLayout::vtableRva);
    Put(character, engine::CharacterInstanceLayout::flags, std::uint32_t{0x1});
    Put(otherCharacter, 0, kBase + engine::CharacterInstanceLayout::vtableRva);
    Put(pass, engine::PassInfoLayout::flags, engine::PassInfoLayout::generalPassFlags);
    Put(animated, engine::AnimatedCharacterLayout::characterInstance, reinterpret_cast<std::uintptr_t>(character));
    Put(player, engine::ArkPlayerLayout::animatedCharacter, reinterpret_cast<std::uintptr_t>(animated));
}

// What the slot render read of its slot, params and character, as it reads them
// at its entry; and, like it, it writes a bit of its own into the slot.
struct Seen {
    void* slot = nullptr;
    std::uint16_t slotFlags = 0;
    std::uint32_t slotObjectFlags = 0, characterFlags = 0;
    std::uint64_t paramsFlags = 0;
    bool argsForwarded = false;
};
Seen seen{};
unsigned renders = 0;
std::uintptr_t __fastcall FakeRender(void* drawn, void* entity, void* drawnParams, std::uintptr_t lod, void* proxy,
                                     const void* drawnPass)
{
    ++renders;
    seen = {};
    seen.slot = drawn;
    seen.slotFlags = Get<std::uint16_t>(drawn, bn::SlotFlags);
    seen.slotObjectFlags = Get<std::uint32_t>(drawn, bn::SlotObjectFlags);
    seen.paramsFlags = Get<std::uint64_t>(drawnParams, bn::ParamsObjectFlags);
    const auto* of = reinterpret_cast<const std::uint8_t*>(Get<std::uintptr_t>(drawn, bn::SlotCharacter));
    seen.characterFlags = Get<std::uint32_t>(of, engine::CharacterInstanceLayout::flags);
    seen.argsForwarded = entity == kEntity && drawnParams == params && lod == kLod && proxy == kProxy && drawnPass == pass;
    static_cast<std::uint8_t*>(drawn)[0xAF] |= 0x40;
    return 0xABC;
}
std::uintptr_t Render(std::uint8_t* at) { return SlotRenderDetour(at, kEntity, params, kLod, kProxy, pass); }

void RenderTarget() {}   // the address the fake gate resolves; never called
unsigned resolves = 0, installs = 0;
DWORD readiness = ERROR_SUCCESS;
DWORD FakeResolve(std::uintptr_t& target)
{
    ++resolves;
    target = readiness ? 0 : reinterpret_cast<std::uintptr_t>(&RenderTarget);
    return readiness;
}
bool FakeInstall(std::uintptr_t target)
{
    ++installs;
    Check(target == reinterpret_cast<std::uintptr_t>(&RenderTarget), "the hook goes on the resolved slot render");
    gOriginal = FakeRender;
    return true;
}
std::uintptr_t FakeBase() { return kBase; }
void Retry()
{
    gInstallTried = false;
    gGate = "unchecked";
}
} // namespace

int main()
{
    Reset();
    Check(Reports("depth=1") && Reports("hooks=0") && Reports("gate=unchecked") && Reports("character=0"),
          "on by default, nothing hooked yet");

    // Not ready (no PreyDll yet): not tried, so tried again.
    gResolve = FakeResolve;
    gInstall = FakeInstall;
    gBase = FakeBase;
    readiness = ERROR_NOT_READY;
    EnsureBodyDepth();
    Check(installs == 0 && Reports("gate=unchecked") && Logged("preyvr_body_depth", "no_preydll"), "no PreyDll: not yet");
    EnsureBodyDepth();
    Check(resolves == 2, "asked again once PreyDll may be there");

    // Before the hook, the player is not even read.
    PublishBodyDepthPlayer(reinterpret_cast<std::uintptr_t>(player));
    Check(Reports("character=0"), "unhooked, no character is published");

    // Other bytes: refused for good.
    readiness = ERROR_BAD_EXE_FORMAT;
    Check(SetBodyDepth(1) == ERROR_NOT_SUPPORTED && installs == 0 && Reports("gate=refused") && Reports("hooks=0") &&
              Logged("result=unavailable", "bytes_mismatch", "SlotRender"),
          "mismatched bytes refuse the hook");
    Check(SetBodyDepth(1) == ERROR_NOT_SUPPORTED && resolves == 3, "and a refusal is not retried");

    // The real installer with MinHook never initialised: the hook cannot be made.
    Retry();
    readiness = ERROR_SUCCESS;
    gInstall = Install;
    Check(SetBodyDepth(1) == ERROR_NOT_SUPPORTED && Reports("gate=hook_failed") && Reports("hooks=0") &&
              Logged("result=failed", "create_hook", "SlotRender"),
          "a hook that cannot be created is reported");

    // PREYVR_BODY_DEPTH=0 at launch: off and unhooked; body.depth 1 still hooks.
    Retry();
    gInstall = FakeInstall;
    SetEnvironmentVariableW(L"PREYVR_BODY_DEPTH", L"0");
    EnsureBodyDepth();
    SetEnvironmentVariableW(L"PREYVR_BODY_DEPTH", nullptr);
    Check(installs == 0 && Reports("depth=0") && Logged("disabled_by_environment"), "the launch switch leaves it unhooked");
    Check(SetBodyDepth(1) == 0 && installs == 1 && Reports("depth=1") && Reports("hooks=1") && Reports("gate=ok") &&
              Logged("hook_installed", "SlotRender"),
          "on, the slot render is hooked");
    Check(SetBodyDepth(1) == 0 && installs == 1, "once");
    Check(SetBodyDepth(2) == ERROR_INVALID_PARAMETER && Reports("depth=1"), "2 is refused");

    // No player character yet: the player's slot is Prey's, near.
    Check(Render(slot) == 0xABC && seen.argsForwarded && seen.slotFlags == 0x0003 &&
              (seen.slotObjectFlags & bn::ObjectNearest) != 0 && Reports("others=1") && Reports("nearSlots=1"),
          "with no player known the slot draws near, its arguments and result forwarded");

    // The player: ArkPlayer -> animated character -> its CCharInstance.
    PublishBodyDepthPlayer(reinterpret_cast<std::uintptr_t>(player));
    Check(Reports("character=1"), "the player's character is published");

    // The player's slot in the main view: drawn without the near bits...
    Reset();
    Render(slot);
    Check(seen.slot == slot && seen.slotFlags == 0x0001 && seen.slotObjectFlags == 0x4000007u &&
              seen.paramsFlags == 0x100 && seen.characterFlags == 0x1 && seen.argsForwarded,
          "the slot render reads no near bit, every other bit as Prey left it");
    // ...and they come back, with what the render wrote kept.
    Check(Get<std::uint16_t>(slot, bn::SlotFlags) == 0x0003 &&
              Get<std::uint32_t>(slot, bn::SlotObjectFlags) == (bn::ObjectNearest | 0x4000007u) &&
              Get<std::uint64_t>(params, bn::ParamsObjectFlags) == 0x100 &&
              Get<std::uint32_t>(character, engine::CharacterInstanceLayout::flags) == 0x1 && (slot[0xAF] & 0x40) != 0,
          "after the render the slot, params and character are as Prey left them, and as the render left them");
    Check(Reports("world=1") && Reports("nearSlots=2") && Logged("first_world", "draws=1"), "counted, and the first logged");

    // A shadow or any other pass: Prey's near pass.
    Reset();
    Put(pass, engine::PassInfoLayout::flags, std::uint32_t{0x1234});
    Render(slot);
    Check(seen.slotFlags == 0x0003 && (seen.slotObjectFlags & bn::ObjectNearest) != 0 && Reports("kept=1") &&
              Reports("keptFlags=0x1234") && Reports("world=1"),
          "another pass keeps the slot near, and its flags are reported");

    // Another character's near slot: Prey's.
    Reset();
    Render(otherSlot);
    Check(seen.slot == otherSlot && seen.slotFlags == 0x0003 && Reports("others=2"), "another character's slot is Prey's");

    // A slot that is not near: not looked at past its flags.
    Reset();
    Put(slot, bn::SlotFlags, std::uint16_t{0x0001});
    Render(slot);
    Check(seen.slotFlags == 0x0001 && Reports("nearSlots=4"), "a slot drawn in the world already is untouched");

    // The character, or the params, also asking for the near pass: clear for the
    // render, and back after it.
    Reset();
    Put(character, engine::CharacterInstanceLayout::flags, std::uint32_t{0x3});
    Put(params, bn::ParamsObjectFlags, std::uint64_t{0x100 | bn::ObjectNearest});
    Render(slot);
    Check(seen.characterFlags == 0x1 && seen.paramsFlags == 0x100 &&
              Get<std::uint32_t>(character, engine::CharacterInstanceLayout::flags) == 0x3 &&
              Get<std::uint64_t>(params, bn::ParamsObjectFlags) == (0x100 | bn::ObjectNearest) &&
              Reports("characterNear=1") && Reports("paramsNear=1") && Reports("world=2"),
          "CS_FLAG_DRAW_NEAR and the params' FOB_NEAREST cleared for the render and put back");

    // Off: Prey's near pass, for A/B.
    Reset();
    SetBodyDepth(0);
    Render(slot);
    Check(seen.slotFlags == 0x0003 && Reports("native=1") && Reports("depth=0"), "off, the slot is Prey's");
    SetBodyDepth(1);

    // A player whose character is not a CCharInstance, or none: nothing taken.
    Put(character, 0, std::uintptr_t{0x1234});
    PublishBodyDepthPlayer(reinterpret_cast<std::uintptr_t>(player));
    Check(Reports("character=0"), "a character with another vtable is not the player's");
    Reset();
    Render(slot);
    Check(seen.slotFlags == 0x0003 && Reports("others=3"), "and its slot is Prey's");
    PublishBodyDepthPlayer(0);
    Check(Reports("character=0"), "no player, no character");
    PublishBodyDepthPlayer(reinterpret_cast<std::uintptr_t>(player));
    Check(Reports("character=1") && Reports("unread=0"), "and the player's again");

    std::cout << "body_depth_hook: the player's slot drawn in the world for the main view, Prey's state put back\n";
}
