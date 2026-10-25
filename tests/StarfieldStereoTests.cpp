// The starfield lane: the production detour with Prey replaced at its ABI
// boundary -- the ArkStars draw's original, the space sky's render element and
// its star block, the gate and the installer.
#include "../src/dll/StarfieldStereo.cpp"
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
namespace sn = preyvr::starfield::native;
// "stars={depth=1 ... distanceM=10000}" with each field between spaces.
std::string Report()
{
    std::string report = StarfieldDepthReport() + " ";
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

// The space sky's render element and its star block, as Prey's main thread
// leaves them: the block at +0x40, the star mesh +0x48, the texture's id +0x50
// and +0x54.
struct Element { std::uint8_t bytes[0x100]; };
Element element{};
preyvr::starfield::StarBlock block{};
std::uint8_t mesh[0x100]{};
constexpr float kRotation[9] = {0.8f, -0.6f, 0, 0.6f, 0.8f, 0, 0, 0, 1};

// What the ArkStars draw read of the element it was handed, as it reads it:
// the mesh first (nothing drawn without one), then the block's fields.
struct Seen {
    const void* element = nullptr;
    std::uintptr_t mesh = 0;
    std::uint32_t texture = 0, sampler = 0;
    bool blockRead = false;
    float rotation[9]{}, intensity = 0, size = 0, share = 0;
};
Seen seen{};
unsigned draws = 0;
void __fastcall FakeDraw(void* drawn)
{
    ++draws;
    seen = {};
    seen.element = drawn;
    if (!SafeRead(static_cast<const std::uint8_t*>(drawn) + 0x48, seen.mesh) || !seen.mesh) { return; }
    SafeRead(static_cast<const std::uint8_t*>(drawn) + 0x50, seen.texture);
    SafeRead(static_cast<const std::uint8_t*>(drawn) + 0x54, seen.sampler);
    const void* at = nullptr;
    preyvr::starfield::StarBlock read{};
    seen.blockRead = SafeRead(static_cast<const std::uint8_t*>(drawn) + 0x40, at) && at && SafeCopy(read.data(), at, read.size());
    if (!seen.blockRead) { return; }
    std::memcpy(seen.rotation, read.data() + 0x08, sizeof(seen.rotation));
    seen.intensity = Get<float>(read.data(), 0x2C);
    seen.size = Get<float>(read.data(), 0x30);
    seen.share = Get<float>(read.data(), 0x34);
}
void DrawTarget() {}   // the address the fake gate resolves; never called

unsigned resolves = 0, installs = 0;
DWORD readiness = ERROR_SUCCESS;
DWORD FakeResolve(std::uintptr_t& target)
{
    ++resolves;
    target = readiness ? 0 : reinterpret_cast<std::uintptr_t>(&DrawTarget);
    return readiness;
}
bool FakeInstall(std::uintptr_t target)
{
    ++installs;
    Check(target == reinterpret_cast<std::uintptr_t>(&DrawTarget), "the hook goes on the resolved draw");
    gOriginal = FakeDraw;
    return true;
}
void Retry()
{
    gInstallTried = false;
    gGate = "unchecked";
}
void Reset()
{
    Put(&element, sn::ElementStarBlock, static_cast<const void*>(block.data()));
    Put(&element, 0x48, static_cast<const void*>(mesh));
    Put(&element, 0x50, std::uint32_t{0x1234});
    Put(&element, 0x54, std::uint32_t{0x5678});
}
} // namespace

int main()
{
    std::memcpy(block.data() + sn::BlockRotation, kRotation, sizeof(kRotation));
    Put(block.data(), 0x2C, 0.7f);
    Put(block.data(), 0x30, 0.0025f);
    Put(block.data(), 0x34, 0.9f);
    Reset();
    Check(Reports("depth=1") && Reports("hooks=0") && Reports("gate=unchecked") && Reports("distanceM=10000"),
          "on by default, nothing hooked yet");

    // Not ready (no PreyDll yet): not tried, so tried again.
    gResolve = FakeResolve;
    gInstall = FakeInstall;
    readiness = ERROR_NOT_READY;
    EnsureStarfieldDepth();
    Check(installs == 0 && Reports("gate=unchecked") && Logged("preyvr_starfield", "no_preydll"), "no PreyDll: not yet");
    EnsureStarfieldDepth();
    Check(resolves == 2, "asked again once PreyDll may be there");

    // Other bytes: refused for good.
    readiness = ERROR_BAD_EXE_FORMAT;
    Check(SetStarfieldDepth(1) == ERROR_NOT_SUPPORTED && installs == 0 && Reports("gate=refused") && Reports("hooks=0") &&
              Logged("result=unavailable", "bytes_mismatch", "ArkStarsDraw"),
          "mismatched bytes refuse the hook");
    Check(SetStarfieldDepth(1) == ERROR_NOT_SUPPORTED && resolves == 3, "and a refusal is not retried");

    // The real installer with MinHook never initialised: the hook cannot be made.
    Retry();
    readiness = ERROR_SUCCESS;
    gInstall = Install;
    Check(SetStarfieldDepth(1) == ERROR_NOT_SUPPORTED && Reports("gate=hook_failed") && Reports("hooks=0") &&
              Logged("result=failed", "create_hook", "ArkStarsDraw"),
          "a hook that cannot be created is reported");

    // PREYVR_STAR_DEPTH=0 at launch: off and unhooked; stars.depth 1 still hooks.
    Retry();
    gInstall = FakeInstall;
    SetEnvironmentVariableW(L"PREYVR_STAR_DEPTH", L"0");
    EnsureStarfieldDepth();
    SetEnvironmentVariableW(L"PREYVR_STAR_DEPTH", nullptr);
    Check(installs == 0 && Reports("depth=0") && Logged("disabled_by_environment"), "the launch switch leaves it unhooked");
    Check(SetStarfieldDepth(1) == 0 && installs == 1 && Reports("depth=1") && Reports("hooks=1") && Reports("gate=ok") &&
              Logged("hook_installed", "ArkStarsDraw"),
          "on, the draw is hooked");
    Check(SetStarfieldDepth(1) == 0 && installs == 1, "once");
    Check(SetStarfieldDepth(2) == ERROR_INVALID_PARAMETER && Reports("depth=1"), "2 is refused");

    // On: the draw is handed a copy whose block draws the stars 10 km out.
    Element before = element;
    const preyvr::starfield::StarBlock blockBefore = block;
    ArkStarsDrawDetour(&element);
    Check(draws == 1 && seen.element != &element && seen.blockRead, "the draw reads a copy of the element");
    Check(seen.mesh == reinterpret_cast<std::uintptr_t>(mesh) && seen.texture == 0x1234 && seen.sampler == 0x5678,
          "with Prey's mesh, texture and sampler");
    for (int i = 0; i < 9; ++i) {
        Check(seen.rotation[i] == kRotation[i] * preyvr::starfield::kStarDistanceMetres, "StarRotation, scaled");
    }
    Check(seen.intensity == 0.7f && seen.size == 0.0025f && seen.share == 0.9f, "the intensity, size and share as Prey's");
    Check(std::memcmp(&before, &element, sizeof(element)) == 0 && block == blockBefore, "Prey's element and block untouched");
    Check(Reports("draws=1") && Reports("far=1") && Logged("first_far", "distanceM=10000"), "counted, and the first logged");

    // Off: Prey's own element.
    SetStarfieldDepth(0);
    ArkStarsDrawDetour(&element);
    Check(seen.element == &element && seen.rotation[0] == kRotation[0] && Reports("native=1") && Reports("depth=0"),
          "off, the draw is Prey's");
    SetStarfieldDepth(1);

    // No block, or one that cannot be read: Prey's own element, whatever it does with it.
    Put(&element, sn::ElementStarBlock, static_cast<const void*>(nullptr));
    ArkStarsDrawDetour(&element);
    Check(seen.element == &element && !seen.blockRead && Reports("unread=1"), "no block: Prey's draw");
    Put(&element, sn::ElementStarBlock, reinterpret_cast<const void*>(std::uintptr_t{0x10}));
    ArkStarsDrawDetour(&element);
    Check(seen.element == &element && !seen.blockRead && Reports("unread=2"), "an unreadable block: Prey's draw");
    Reset();
    ArkStarsDrawDetour(&element);
    Check(seen.element != &element && Reports("far=2") && Reports("draws=5"), "and far again with a block");

    std::cout << "starfield_stereo: the draw handed a far copy, Prey's element and block untouched\n";
}
