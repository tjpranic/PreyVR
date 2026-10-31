// HookInstall.h against real MinHook, on functions in this process: what every
// installer now relies on (publish before enable, remove on failure, all or
// nothing) checked offline instead of only in a game session.
#include "engine/hooks/HookInstall.h"
#include "TestSupport.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

namespace preyvr::dll {
bool EnsureMinHook()
{
    static const bool ready = MH_Initialize() == MH_OK;
    return ready;
}
} // namespace preyvr::dll

namespace {

using preyvr::test::Require;
namespace hook = preyvr::dll::hook;
using Fn = int (*)(int);

// Bodies long enough for MinHook's jump, and different so the linker cannot fold them.
__declspec(noinline) int Twice(int x) { volatile int y = x; y = y * 2; return y; }
__declspec(noinline) int Thrice(int x) { volatile int y = x; y = y * 3; return y; }
__declspec(noinline) int Square(int x) { volatile int y = x; y = y * y; return y; }

std::atomic<Fn> gTwice{nullptr};
std::atomic<Fn> gThrice{nullptr};
Fn gSquare = nullptr;   // a plain pointer slot, as HudLayer and PsiMedkit use
int TwiceDetour(int x) { return gTwice.load()(x) + 1; }
int ThriceDetour(int x) { return gThrice.load()(x) + 1; }
int SquareDetour(int x) { return gSquare(x) + 1; }

int Call(Fn fn, int x)
{
    Fn volatile through = fn;
    return through(x);
}
void* At(Fn fn) { return reinterpret_cast<void*>(fn); }
// A function's first bytes, read through volatile: a plain memcpy from a
// function's address trips an MSVC internal compiler error.
void CopyCode(Fn fn, std::uint8_t* out, std::size_t size)
{
    const volatile std::uint8_t* code = reinterpret_cast<const volatile std::uint8_t*>(reinterpret_cast<std::uintptr_t>(fn));
    for (std::size_t i = 0; i < size; ++i) { out[i] = code[i]; }
}

std::vector<std::string> gLines;
void Log(const std::string& line) { gLines.push_back(line); }

void InstallPublishesAndRefusesASecondHook()
{
    Require(Call(Twice, 5) == 10, "native before hooking");
    const auto installed = hook::Install(At(Twice), reinterpret_cast<void*>(&TwiceDetour), gTwice);
    Require(static_cast<bool>(installed), "install succeeds");
    Require(gTwice.load() != nullptr, "the trampoline is published");
    Require(Call(Twice, 5) == 11, "the detour runs and calls the original through the trampoline");
    const Fn before = gTwice.load();
    const auto again = hook::Install(At(Twice), reinterpret_cast<void*>(&TwiceDetour), gTwice);
    Require(again.step == hook::Step::create && again.status == MH_ERROR_ALREADY_CREATED,
            "a second hook on the same target fails at create");
    Require(gTwice.load() == before && Call(Twice, 5) == 11, "and leaves the first hook working");
    Require(MH_RemoveHook(At(Twice)) == MH_OK && Call(Twice, 5) == 10, "removed cleanly");
}

void VerifiedRefusesWrongAndEmptyPrologues()
{
    std::uint8_t real[8];
    CopyCode(Thrice, real, sizeof(real));
    std::uint8_t wrong[8];
    std::memcpy(wrong, real, sizeof(wrong));
    wrong[3] ^= 0xFF;
    auto result = hook::InstallVerified(At(Thrice), wrong, reinterpret_cast<void*>(&ThriceDetour), gThrice);
    Require(result.step == hook::Step::prologue && Call(Thrice, 2) == 6, "a mismatching prologue is refused, unhooked");
    result = hook::InstallVerified(At(Thrice), {}, reinterpret_cast<void*>(&ThriceDetour), gThrice);
    Require(result.step == hook::Step::prologue && Call(Thrice, 2) == 6, "an empty prologue never matches");
    result = hook::InstallVerified(At(Thrice), real, reinterpret_cast<void*>(&ThriceDetour), gThrice);
    Require(static_cast<bool>(result) && Call(Thrice, 2) == 7, "the real prologue installs");
    Require(MH_RemoveHook(At(Thrice)) == MH_OK, "removed");
}

void CreateDoesNotEnable()
{
    const auto created = hook::Create(At(Square), reinterpret_cast<void*>(&SquareDetour), gSquare);
    Require(static_cast<bool>(created) && gSquare != nullptr, "create publishes into a plain pointer slot");
    Require(Call(Square, 3) == 9, "but the hook is not live");
    Require(MH_EnableHook(At(Square)) == MH_OK && Call(Square, 3) == 10, "until the caller enables it");
    Require(MH_RemoveHook(At(Square)) == MH_OK, "removed");
}

void InstallAllIsAllOrNothing()
{
    // A data address is not executable: MinHook refuses it at create.
    static int notCode = 0;
    void* originals[3]{};
    const hook::Spec broken[] = {
        {At(Twice), reinterpret_cast<void*>(&TwiceDetour), &originals[0]},
        {At(Thrice), reinterpret_cast<void*>(&ThriceDetour), &originals[1]},
        {&notCode, reinterpret_cast<void*>(&SquareDetour), &originals[2]},
    };
    int published = 0;
    const auto failed = hook::InstallAll(broken, [&] { ++published; });
    Require(failed.step == hook::Step::create && failed.index == 2, "the failing member is named");
    Require(published == 0, "nothing is published when a member cannot be created");
    Require(Call(Twice, 4) == 8 && Call(Thrice, 4) == 12, "and the members that were created are gone");

    const hook::Spec both[] = {
        {At(Twice), reinterpret_cast<void*>(&TwiceDetour), &originals[0]},
        {At(Thrice), reinterpret_cast<void*>(&ThriceDetour), &originals[1]},
    };
    const auto installed = hook::InstallAll(both, [&] {
        ++published;
        gTwice.store(reinterpret_cast<Fn>(originals[0]));
        gThrice.store(reinterpret_cast<Fn>(originals[1]));
        // Nothing is live while publishing.
        Require(Call(Twice, 4) == 8 && Call(Thrice, 4) == 12, "publish runs before any hook is enabled");
    });
    Require(static_cast<bool>(installed) && published == 1, "a retry of the valid set succeeds once");
    Require(Call(Twice, 4) == 9 && Call(Thrice, 4) == 13, "every member is live");
    Require(MH_RemoveHook(At(Twice)) == MH_OK && MH_RemoveHook(At(Thrice)) == MH_OK, "removed");
}

void HookAtLogsTheLanesLines()
{
    gLines.clear();
    const std::uint8_t wrong[] = {0x00, 0x11, 0x22, 0x33, 0x44};
    Require(!hook::HookAt(At(Twice), wrong, reinterpret_cast<void*>(&TwiceDetour), gTwice, "twice", &Log),
            "mismatch refused");
    std::uint8_t real[5];
    CopyCode(Twice, real, sizeof(real));
    Require(hook::HookAt(At(Twice), real, reinterpret_cast<void*>(&TwiceDetour), gTwice, "twice", &Log), "installs");
    // The bytes now start with MinHook's jump, so check against a copy taken before.
    Require(!hook::HookAt(At(Twice), {}, reinterpret_cast<void*>(&TwiceDetour), gTwice, "twice", &Log),
            "empty prologue refused");
    Require(gLines.size() == 2 && gLines[0] == "result=unavailable detail=prologue_mismatch target=twice" &&
                gLines[1] == "result=unavailable detail=prologue_mismatch target=twice",
            "prologue refusals use the lanes' line");
    std::uint8_t now[5];
    CopyCode(Twice, now, sizeof(now));
    Require(!hook::HookAt(At(Twice), now, reinterpret_cast<void*>(&TwiceDetour), gTwice, "twice", &Log),
            "a second hook fails");
    Require(gLines.size() == 3 && gLines[2] == "result=failed detail=create_hook target=twice",
            "create failures use the lanes' line");
    Require(MH_RemoveHook(At(Twice)) == MH_OK, "removed");
}

} // namespace

int main()
{
    Require(preyvr::dll::EnsureMinHook(), "MinHook initialises");
    InstallPublishesAndRefusesASecondHook();
    VerifiedRefusesWrongAndEmptyPrologues();
    CreateDoesNotEnable();
    InstallAllIsAllOrNothing();
    HookAtLogsTheLanesLines();
    MH_Uninitialize();
    std::cout << "hook_install PASS: publish before enable, second hook refused, prologue gates, create without "
                 "enable, all-or-nothing sets, lane log lines\n";
    return 0;
}
