// The time-stop fix: the production detour with Prey replaced at its ABI
// boundary -- the player's and another character's animated character, the
// player, Prey's own start, the gate and the installer.
#include "engine/fixes/TimeStopAnimation.cpp"
#include "DllTestSupport.h"
#include "TestSupport.h"
#include <iostream>
#include <limits>
#include <string>

PREYVR_TEST_CAPTURE_LOG()
PREYVR_TEST_STUB_MINHOOK()

// What the lane reaches outside its file, replaced below.
namespace timestop_test { unsigned long long matched = 0; }
unsigned long long preyvr::dll::AnimIkMatched() { return timestop_test::matched; }

namespace timestop_test {
using namespace preyvr::dll;
using preyvr::test::Check;
using preyvr::test::Count;
using preyvr::test::Logged;
using preyvr::test::Put;
namespace tn = preyvr::timestop::native;
namespace engine = preyvr::engine;

// Never read: the gate, the installer and the engine calls are all replaced.
constexpr std::uintptr_t kImage = 0x180000000;

alignas(16) std::uint8_t playerAnimated[0x900]{}, otherAnimated[0x900]{}, player[0x1900]{};
std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

bool havePlayer = true;
bool originalSetsTime = false;   // the original moves the frame time on, after its own test
unsigned originals = 0, starts = 0, resolves = 0, installs = 0;
void* startedOn = nullptr;
const void* startedAt = nullptr;
Start startResult = Start::started;
DWORD readiness = ERROR_SUCCESS;

void __fastcall FakeOriginal(void* animated)
{
    ++originals;
    if (originalSetsTime) { Put(animated, tn::FrameTime, 1.0 / 60.0); }
}
std::uintptr_t FakePlayer(std::uintptr_t base)
{
    Check(base == kImage, "the player is asked of the gated PreyDll");
    return havePlayer ? Address(player) : 0;
}
Start FakeStart(void* animated, const void* location)
{
    ++starts;
    startedOn = animated;
    startedAt = location;
    return startResult;
}
DWORD FakeResolve(std::uintptr_t& base)
{
    ++resolves;
    base = readiness == ERROR_SUCCESS ? kImage : 0;
    return readiness;
}
bool FakeInstall(std::uintptr_t target)
{
    ++installs;
    Check(target == kImage + tn::PrePhysicsUpdate, "the hook goes on the animated character's PrePhysicsUpdate");
    gOriginal = FakeOriginal;
    return true;
}
void Retry()
{
    gInstallTried = false;
    gGate = "unchecked";
}

bool Reports(const char* field) { return preyvr::test::ReportHas(TimeStopAnimationReport(), field); }

// Prey's pre-physics pass reaching an animated character whose frame time
// UpdateTime left at `frameTime`.
void Pass(std::uint8_t* animated, double frameTime)
{
    Put(animated, tn::FrameTime, frameTime);
    PrePhysicsUpdateDetour(animated);
}

void TestGate()
{
    gResolve = FakeResolve;
    gInstall = FakeInstall;
    gPlayer = FakePlayer;
    gStart = FakeStart;
    Check(Reports("active=0") && Reports("hooks=0") && Reports("gate=unchecked") && Reports("stops=0"),
          "nothing hooked, nothing seen");

    readiness = ERROR_NOT_READY;
    EnsureTimeStopAnimation();
    Check(installs == 0 && Reports("gate=unchecked") && Logged("preyvr_time_stop", "no_preydll"),
          "no PreyDll: not yet");
    readiness = ERROR_BAD_EXE_FORMAT;
    EnsureTimeStopAnimation();
    EnsureTimeStopAnimation();
    Check(installs == 0 && resolves == 2 && Reports("gate=refused") && Reports("active=0") &&
              Logged("result=unavailable", "bytes_mismatch", "AnimatedCharacterPrePhysicsUpdate"),
          "other bytes refuse the hook, for good");
    Retry();
    readiness = ERROR_SUCCESS;
    gInstall = Install;
    EnsureTimeStopAnimation();
    Check(Reports("gate=hook_failed") && Reports("active=0") && Logged("result=failed", "create_hook"),
          "a hook that cannot be created is reported, and Prey's behaviour kept");
    Retry();
    gInstall = FakeInstall;
    EnsureTimeStopAnimation();
    EnsureTimeStopAnimation();
    Check(installs == 1 && Reports("hooks=1") && Reports("gate=ok") && Reports("active=1") &&
              Logged("hook_installed", "AnimatedCharacterPrePhysicsUpdate", "rows=4"),
          "VR start hooks PrePhysicsUpdate, once");
}

void TestStops()
{
    Put(player, engine::ArkPlayerLayout::animatedCharacter, Address(playerAnimated));

    // Play, and every other character, which UpdateTime never leaves at 0.
    Pass(playerAnimated, 1.0 / 60.0);
    Pass(otherAnimated, 1e-4);
    Check(originals == 2 && starts == 0 && !Logged("stop_begin"), "a running clock is Prey's frame, untouched");

    // The wheel opens: the player's clock reads 0, and Prey skipped the start.
    matched = 10;
    Pass(playerAnimated, 0.0);
    Check(originals == 3 && starts == 1 && startedOn == playerAnimated &&
              startedAt == playerAnimated + tn::Location && Logged("preyvr_time_stop", "stop_begin") &&
              Reports("stops=1") && Reports("stoppedFrames=1") && Reports("started=1"),
          "the player's animation is started with Prey's own call, on its own location");
    startResult = Start::declined;
    Pass(playerAnimated, -0.0);
    Check(starts == 2 && Reports("declined=1") && Reports("stoppedFrames=2") && Reports("stops=1"),
          "-0 is stopped too (Prey's comisd), and a start Prey declines is counted, not retried");

    // Another character at 0 (not updated yet): Prey's behaviour.
    Pass(otherAnimated, 0.0);
    Check(starts == 2 && Reports("others=1") && Count("stop_end") == 0, "only the player's is started");

    // A character whose start moved to its transform event.
    Put(playerAnimated, tn::StartsOnTransform, std::uint8_t{1});
    Pass(playerAnimated, 0.0);
    Put(playerAnimated, tn::StartsOnTransform, std::uint8_t{0});
    Check(starts == 2 && Reports("onTransform=1") && Reports("stoppedFrames=3"),
          "nor one that Prey starts from its transform event");

    // The test is the original's, made before it runs.
    startResult = Start::started;
    originalSetsTime = true;
    matched = 25;
    Pass(playerAnimated, 0.0);
    originalSetsTime = false;
    Check(starts == 3 && Reports("started=2"), "the frame time is read as the original reads it, before it runs");

    // The wheel closes.
    Pass(playerAnimated, 1.0 / 60.0);
    Check(starts == 3 && Count("stop_end") == 1 &&
              Logged("stop_end reason=clock", "frames=4 started=2 declined=1 onTransform=1", "ikMatched=15"),
          "the stop ends with the clock, logged with what it started and the IK frames it matched");

    // A NaN frame time: Prey started it itself.
    Pass(playerAnimated, std::numeric_limits<double>::quiet_NaN());
    Check(starts == 3 && Count("stop_begin") == 1, "a NaN frame time is not a stop");

    // No player yet.
    havePlayer = false;
    Pass(playerAnimated, 0.0);
    havePlayer = true;
    Check(starts == 3 && Reports("others=2") && Count("stop_begin") == 1, "no player: nothing started");

    // VR stops in a stop, and starts again.
    Pass(playerAnimated, 0.0);
    StopTimeStopAnimation();
    Pass(playerAnimated, 0.0);
    Check(starts == 4 && Reports("active=0") && Logged("stop_end reason=vr_off", "frames=1 started=1"),
          "VR stop ends the stop and leaves Prey's behaviour");
    EnsureTimeStopAnimation();
    Pass(playerAnimated, 0.0);
    Check(installs == 1 && starts == 5 && Reports("active=1") && Reports("stops=3"),
          "VR again: started again, with the same hook");

    // The player's animated character changes in a stop (a level load).
    Put(player, engine::ArkPlayerLayout::animatedCharacter, Address(otherAnimated));
    Pass(otherAnimated, 0.0);
    Check(starts == 6 && startedOn == otherAnimated && Logged("stop_end reason=new_character") &&
              Reports("stops=4"),
          "a new character of the player's ends the old stop and begins its own");
    Check(originals == 14 && Reports("faulted=0") && Reports("unread=0"), "the original runs on every frame");
}

} // namespace timestop_test

int main()
{
    timestop_test::TestGate();
    timestop_test::TestStops();
    std::cout << "time_stop_animation: the player's animation started while Prey stops its clock\n";
}
