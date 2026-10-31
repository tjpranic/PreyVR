// The body-depth lane: the production detours with Prey replaced at its ABI
// boundary -- the slot render's, SetViewport's and UpdateNearestChange's
// originals, an entity slot, its render params, the character, the pass info,
// a render pass, the renderer, the player that leads to the character, the gate
// and the installer -- and NearViewStereo's packer and the eye records at theirs.
#include "render/stereo/BodyDepthHook.cpp"
#include "DllTestSupport.h"
#include "TestSupport.h"
#include <cstring>
#include <iostream>
#include <string>

PREYVR_TEST_CAPTURE_LOG()
PREYVR_TEST_STUB_MINHOOK()

// NearViewStereo's half of the near pass at the world's depth: what body.depth
// asked of it, and whether VR is drawing.
namespace {
bool packerDepth = false, packerStereo = true;
}
void preyvr::dll::SetNearViewWorldDepth(bool enabled) { packerDepth = enabled; }
bool preyvr::dll::NearViewWorldDepthActive() { return packerDepth && packerStereo; }
std::string preyvr::dll::NearViewWorldDepthReport() { return "nearDepth={stub}"; }

// The eye records: one eye, at kEye.
namespace {
constexpr float kEye[3] = {10.0f, 20.0f, 1.5f};
}
bool preyvr::dll::FindBuiltEyeByPosition(const float position[3], BuiltEye& out)
{
    if (position[0] != kEye[0] || position[1] != kEye[1] || position[2] != kEye[2]) { return false; }
    out = {};
    out.eye = 1;
    return true;
}

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
alignas(16) std::uint8_t renderPass[0x100]{}, renderer[0x9600]{};
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

// SetViewport as Prey's (0xF90390): the viewport's depth at +0x50, or the near
// slice [0, 0.1], flipped to [0.9, 1] with reverse depth.
void __fastcall FakeSetViewport(void* target, const void* viewport)
{
    std::memcpy(static_cast<std::uint8_t*>(target) + 0x40, viewport, 0x18);
    const auto flags = Get<std::uint32_t>(target, bn::PassFlags);
    if (flags & bn::PassNearDepth) {
        Put(target, bn::PassViewportDepth, 0.0f);
        Put(target, bn::PassViewportDepth + 4, 0.1f);
        if (flags & 2) {
            Put(target, bn::PassViewportDepth, 0.9f);
            Put(target, bn::PassViewportDepth + 4, 1.0f);
        }
    }
}
// A viewport: x, y, w, h, then its depth [0, 1].
const float kViewport[6] = {0.0f, 0.0f, 1280.0f, 1440.0f, 0.0f, 1.0f};
float MinDepth() { return Get<float>(renderPass, bn::PassViewportDepth); }
float MaxDepth() { return Get<float>(renderPass, bn::PassViewportDepth + 4); }
void SetPass(std::uint32_t flags)
{
    std::memset(renderPass, 0, sizeof(renderPass));
    Put(renderPass, bn::PassFlags, flags);
    SetViewportDetour(renderPass, kViewport);
}

// UpdateNearestChange as Prey's (0xF43D70), entering: the camera saved, the
// renderer near, the pending viewport's max depth the near slice; leaving: the
// saved camera's own max depth back. With `noNearFov` it switches nothing.
bool noNearFov = false;
void __fastcall FakeNearestChange(void* target, std::uint32_t nearest)
{
    auto* r = static_cast<std::uint8_t*>(target);
    if (noNearFov) { return; }
    if (nearest & bn::NearestEntering) {
        Put(r, bn::RendererNearestFlags, Get<std::uint32_t>(r, bn::RendererNearestFlags) | bn::NearestEntering);
        Put(r, bn::RendererViewportMaxDepth, 0.1f);
    } else {
        Put(r, bn::RendererNearestFlags, Get<std::uint32_t>(r, bn::RendererNearestFlags) & ~bn::NearestEntering);
        Put(r, bn::RendererViewportMaxDepth,
            Get<float>(r, bn::RendererSavedCamera + engine::CameraLayout::zRangeMax));
    }
    r[bn::RendererViewportDirty] = 1;
}
// The renderer drawing from a camera at `at`, its own max depth 1.
void ResetRenderer(const float at[3])
{
    std::memset(renderer, 0, sizeof(renderer));
    Put(renderer, bn::RendererSavedCamera + engine::CameraLayout::matrix + 3 * 4, at[0]);
    Put(renderer, bn::RendererSavedCamera + engine::CameraLayout::matrix + 7 * 4, at[1]);
    Put(renderer, bn::RendererSavedCamera + engine::CameraLayout::matrix + 11 * 4, at[2]);
    Put(renderer, bn::RendererSavedCamera + engine::CameraLayout::zRangeMax, 1.0f);
    Put(renderer, bn::RendererViewportMaxDepth, 1.0f);
}
float PendingMaxDepth() { return Get<float>(renderer, bn::RendererViewportMaxDepth); }

void RenderTarget() {}     // the addresses the fake gate resolves; never called
void ViewportTarget() {}
void NearestTarget() {}
unsigned resolves = 0, installs = 0;
DWORD readiness = ERROR_SUCCESS;
DWORD FakeResolve(Targets& targets)
{
    ++resolves;
    targets = {};
    if (!readiness) {
        targets = {reinterpret_cast<std::uintptr_t>(&RenderTarget), reinterpret_cast<std::uintptr_t>(&ViewportTarget),
                   reinterpret_cast<std::uintptr_t>(&NearestTarget)};
    }
    return readiness;
}
bool FakeInstall(const Targets& targets)
{
    ++installs;
    Check(targets.slotRender == reinterpret_cast<std::uintptr_t>(&RenderTarget) &&
              targets.setViewport == reinterpret_cast<std::uintptr_t>(&ViewportTarget) &&
              targets.nearestChange == reinterpret_cast<std::uintptr_t>(&NearestTarget),
          "the hooks go on the resolved functions");
    gOriginal = FakeRender;
    gViewportOriginal = FakeSetViewport;
    gNearestOriginal = FakeNearestChange;
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
    Check(Reports("depth=1") && Reports("mode=near") && Reports("hooks=0") && Reports("gate=unchecked") &&
              Reports("character=0"),
          "the near pass at the world's depth by default, nothing hooked yet");

    // Not ready (no PreyDll yet): not tried, so tried again.
    gResolve = FakeResolve;
    gInstall = FakeInstall;
    gBase = FakeBase;
    readiness = ERROR_NOT_READY;
    EnsureBodyDepth();
    Check(installs == 0 && Reports("gate=unchecked") && Logged("preyvr_body_depth", "no_preydll") && !packerDepth,
          "no PreyDll: not yet, and the packer is not asked");
    EnsureBodyDepth();
    Check(resolves == 2, "asked again once PreyDll may be there");

    // Before the hook, the player is not even read.
    PublishBodyDepthPlayer(reinterpret_cast<std::uintptr_t>(player));
    Check(Reports("character=0"), "unhooked, no character is published");

    // Other bytes: refused for good.
    readiness = ERROR_BAD_EXE_FORMAT;
    Check(SetBodyDepth(1) == ERROR_NOT_SUPPORTED && installs == 0 && Reports("gate=refused") && Reports("hooks=0") &&
              Logged("result=unavailable", "bytes_mismatch", "SetViewport") && !packerDepth,
          "mismatched bytes refuse the hooks, and the packer is not asked");
    Check(SetBodyDepth(1) == ERROR_NOT_SUPPORTED && resolves == 3, "and a refusal is not retried");

    // The real installer with MinHook never initialised: the hooks cannot be made.
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
    Check(installs == 0 && Reports("depth=0") && Reports("mode=native") && Logged("disabled_by_environment") &&
              !packerDepth,
          "the launch switch leaves it unhooked");
    Check(SetBodyDepth(1) == 0 && installs == 1 && Reports("depth=1") && Reports("hooks=1") && Reports("gate=ok") &&
              Logged("hook_installed", "SlotRender,SetViewport,NearestChange") && packerDepth,
          "1: the three functions hooked and the packer given the world's depth");
    Check(SetBodyDepth(1) == 0 && installs == 1, "once");
    Check(SetBodyDepth(3) == ERROR_INVALID_PARAMETER && Reports("depth=1"), "3 is refused");
    PublishBodyDepthPlayer(reinterpret_cast<std::uintptr_t>(player));
    Check(Reports("character=1"), "the player's character is published");

    // --- 1: the near pass at the world's depth --------------------------------------
    // The player's slot stays in the near pass.
    Reset();
    Check(Render(slot) == 0xABC && seen.argsForwarded && seen.slotFlags == 0x0003 &&
              (seen.slotObjectFlags & bn::ObjectNearest) != 0 && Reports("native=1") && Reports("world=0"),
          "the player's slot draws near, its arguments and result forwarded");

    // A near render pass: the viewport's own depth, not the slice; reverse depth too.
    SetPass(bn::PassNearDepth);
    Check(MinDepth() == 0.0f && MaxDepth() == 1.0f && Reports("viewports=1") && Logged("first_near_viewport"),
          "a near pass is given its viewport's depth range");
    SetPass(bn::PassNearDepth | 2);
    Check(MinDepth() == 0.0f && MaxDepth() == 1.0f && Reports("viewports=2"), "with reverse depth as well");
    SetPass(2);
    Check(MinDepth() == 0.0f && MaxDepth() == 1.0f && Get<float>(renderPass, 0x48) == 1280.0f && Reports("viewports=2"),
          "a pass that is not near is Prey's");
    // VR not drawing: the packer is not giving the projection the world's depth,
    // so the slice stays.
    packerStereo = false;
    SetPass(bn::PassNearDepth | 2);
    Check(MinDepth() == 0.9f && MaxDepth() == 1.0f && Reports("viewports=2"), "without the packer the slice stays");
    packerStereo = true;

    // The legacy pipeline entering the near pass for an eye: the camera's own depth.
    ResetRenderer(kEye);
    NearestChangeDetour(renderer, bn::NearestEntering);
    Check(PendingMaxDepth() == 1.0f && renderer[bn::RendererViewportDirty] == 1 && Reports("nearest=1") &&
              Logged("first_near_switch", "eye=1"),
          "entering for an eye, the max depth is the saved camera's");
    NearestChangeDetour(renderer, 0);
    Check(PendingMaxDepth() == 1.0f && Reports("nearest=1"), "leaving is Prey's");
    // Any other camera (a shadow, another view): the slice.
    const float elsewhere[3] = {0.0f, 0.0f, 50.0f};
    ResetRenderer(elsewhere);
    NearestChangeDetour(renderer, bn::NearestEntering);
    Check(PendingMaxDepth() == 0.1f && Reports("nearestOther=1") && Reports("nearest=1"),
          "another camera keeps the near slice");
    // Prey not switching (no near field of view): nothing to do.
    ResetRenderer(kEye);
    noNearFov = true;
    NearestChangeDetour(renderer, bn::NearestEntering);
    noNearFov = false;
    Check(PendingMaxDepth() == 1.0f && renderer[bn::RendererViewportDirty] == 0 && Reports("nearest=1") &&
              Reports("nearestOther=1"),
          "no switch, no write");

    // --- 2: the body drawn in the world ---------------------------------------------
    Check(SetBodyDepth(2) == 0 && installs == 1 && Reports("depth=2") && Reports("mode=world") && !packerDepth,
          "2: the packer's depth off");
    SetPass(bn::PassNearDepth | 2);
    ResetRenderer(kEye);
    NearestChangeDetour(renderer, bn::NearestEntering);
    Check(MinDepth() == 0.9f && PendingMaxDepth() == 0.1f && Reports("viewports=2") && Reports("nearest=1"),
          "the near slice is Prey's in mode 2");

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
    Check(seen.slot == otherSlot && seen.slotFlags == 0x0003 && Reports("others=1"), "another character's slot is Prey's");

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

    // --- 0: Prey's near pass, for A/B -------------------------------------------------
    Reset();
    Check(SetBodyDepth(0) == 0 && Reports("depth=0") && Reports("mode=native") && !packerDepth, "0: everything Prey's");
    Render(slot);
    SetPass(bn::PassNearDepth | 2);
    Check(seen.slotFlags == 0x0003 && Reports("native=2") && MinDepth() == 0.9f && Reports("viewports=2"),
          "off, the slot and the slice are Prey's");
    SetBodyDepth(2);

    // A player whose character is not a CCharInstance, or none: nothing taken.
    Put(character, 0, std::uintptr_t{0x1234});
    PublishBodyDepthPlayer(reinterpret_cast<std::uintptr_t>(player));
    Check(Reports("character=0"), "a character with another vtable is not the player's");
    Reset();
    Render(slot);
    Check(seen.slotFlags == 0x0003 && Reports("others=2"), "and its slot is Prey's");
    PublishBodyDepthPlayer(0);
    Check(Reports("character=0"), "no player, no character");
    PublishBodyDepthPlayer(reinterpret_cast<std::uintptr_t>(player));
    Check(Reports("character=1") && Reports("unread=0"), "and the player's again");

    std::cout << "body_depth_hook: the near pass at the world's depth, or the player's slot drawn in the world, "
                 "for the main view; Prey's state put back\n";
}
