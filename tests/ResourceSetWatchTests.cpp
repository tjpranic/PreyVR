// Exercise the resource-set rebuild watch without loading Prey: its detour over a
// fake rebuild, the thread and submission classes, the caller table, the bounded
// race lines, the install refusals and the report. Including this TU exposes its
// internals only inside this test executable.
#include "../src/dll/ResourceSetWatch.cpp"
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "DllTestSupport.h"
#include "TestSupport.h"

PREYVR_TEST_CAPTURE_LOG()
PREYVR_TEST_STUB_MINHOOK()
namespace preyvr::dll {
DWORD gPinned = 0;
DWORD ModulePinStatus() { return gPinned; }
// DualSceneStereo's submission, and the input drain's thread (OnEngineMainThread).
SceneSubmission gScene{};
SceneSubmission CurrentSceneSubmission() { return gScene; }
unsigned long gDrain = 0;
unsigned long InputDrainThreadId() { return gDrain; }
} // namespace preyvr::dll

namespace {
using namespace preyvr::dll;
using preyvr::test::Count;
using preyvr::test::gLines;
using preyvr::test::Logged;
using preyvr::test::Require;

// A resource set's entry pointer and count, and a rebuild that does what
// 0xF63F10 does to them: the count rewritten, the array moved when it grows.
alignas(8) std::uint8_t gSet[0xA0]{};
std::vector<std::uint8_t> gArray(0x100), gGrownArray(0x200);
bool gGrowNext = false, gBuildResult = true;
unsigned gBuildCalls = 0;
void* gBuildSet = nullptr;

std::uintptr_t& Entries() { return *reinterpret_cast<std::uintptr_t*>(gSet + kEntries); }
int& Entrycount() { return *reinterpret_cast<int*>(gSet + kCount); }

bool __fastcall FakeBuild(void* set)
{
    ++gBuildCalls;
    gBuildSet = set;
    if (gGrowNext) {
        // To the other buffer: a reallocation never lands where it was.
        const auto grown = reinterpret_cast<std::uintptr_t>(gGrownArray.data());
        Entries() = Entries() == grown ? reinterpret_cast<std::uintptr_t>(gArray.data()) : grown;
        gGrowNext = false;
    }
    Entrycount() = 13;
    return gBuildResult;
}

// Two fixed call sites, so the detour sees two callers: called through a
// volatile pointer, as the hook calls it, so it is never inlined into them, and
// not as tail calls, so the return address stays in these functions.
BuildFn volatile gObserve = &ObserveBuild;
volatile bool gSink = false;
__declspec(noinline) bool BuildFromA()
{
    const bool result = gObserve(gSet);
    gSink = result;
    return result;
}
__declspec(noinline) bool BuildFromB()
{
    const bool result = gObserve(gSet);
    gSink = !result;
    return result;
}

unsigned long long Idle(unsigned who) { return gIdle[who].load(); }
unsigned long long InPass(unsigned who) { return gInPass[who].load(); }

void Reset()
{
    for (unsigned i = 0; i < kThreads; ++i) {
        gIdle[i] = 0;
        gInPass[i] = 0;
    }
    gRaces = 0;
    gGrown = 0;
    gGrownInPass = 0;
    gCallerCount = 0;
    gCallersDropped = 0;
    gCallers = {};
    gScene = {};
    gDrain = 0;
    gBuildCalls = 0;
    gLines.clear();
    Entries() = reinterpret_cast<std::uintptr_t>(gArray.data());
    // Callers are read against this executable, as Install reads them against PreyDll.
    const auto self = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(self);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(self + static_cast<std::uintptr_t>(dos->e_lfanew));
    gModuleBase = self;
    gModuleSize = nt->OptionalHeader.SizeOfImage;
    gOriginal = &FakeBuild;
    gEnabled = true;
}

void TestThreadsAndSubmissions()
{
    Reset();
    const unsigned long me = GetCurrentThreadId();
    Require(BuildFromA() && gBuildCalls == 1 && gBuildSet == gSet, "the original runs on the same set");
    Require(Idle(kOther) == 1 && gRaces == 0, "no submission, not the main thread: other, idle");

    gDrain = me;
    BuildFromA();
    Require(Idle(kMain) == 1, "the input drain's thread is the main thread");

    gScene = {true, "first_submission", me};
    BuildFromA();
    Require(InPass(kRender) == 1 && gRaces == 0, "the submission's own thread is the render thread, and no race");

    gScene = {true, "first_submission", me + 1};
    gDrain = 0;
    BuildFromA();
    Require(InPass(kOther) == 1 && gRaces == 1, "another thread during a submission is a race");
    Require(Logged("preyvr_resource_set detail=race n=1 who=other", "step=first_submission", "entries=13 grown=0"),
            "and is logged with the step and the entries");
    Require(Logged("detail=race n=1", "caller=0x", "stack=0x"), "with the caller and its stack as RVAs");

    gDrain = me;
    BuildFromA();
    Require(InPass(kMain) == 1 && gRaces == 2, "the main thread during a submission is a race too");

    gDrain = 0;
    gGrowNext = true;
    BuildFromA();
    Require(gGrown == 1 && gGrownInPass == 1 && Logged("detail=race n=3", "grown=1"),
            "a rebuild that moves the array is counted, and so logged");

    gScene = {false, "idle", me + 1};
    gGrowNext = true;
    BuildFromA();
    Require(gGrown == 2 && gGrownInPass == 1 && Idle(kOther) == 2, "and counted outside a submission too");

    gBuildResult = false;
    Require(!BuildFromA(), "the rebuild's result is passed through");
    gBuildResult = true;
}

void TestCallers()
{
    Reset();
    BuildFromA();
    BuildFromA();
    gScene = {true, "second_submission", GetCurrentThreadId() + 1};
    BuildFromB();
    Require(gCallerCount == 2 && gCallers[0].builds == 2 && gCallers[0].races == 0 && gCallers[1].builds == 1 &&
                gCallers[1].races == 1,
            "each call site is one caller, with its rebuilds and races");
    Require(Count("detail=new_caller") == 2 && Logged("detail=new_caller", "who=other", "inPass=1"),
            "each new caller is logged once");
    const std::string report = ResourceSetWatchReport();
    Require(report.find(" callers=0x") != std::string::npos && report.find(":2/0,0x") != std::string::npos &&
                report.find(":1/1 callersDropped=0") != std::string::npos,
            "the report lists them as RVAs");

    // The table keeps the first 16.
    for (std::uintptr_t i = 0; i < 20; ++i) {
        NoteCaller(0x1000 + i, false);
    }
    Require(gCallerCount == kCallers && gCallersDropped == 6, "callers past the sixteenth are only counted");
    Require(ResourceSetWatchReport().find("ext:0x1000:1/0") != std::string::npos,
            "and one outside the module is shown as an address");
}

void TestBoundedLines()
{
    Reset();
    gScene = {true, "single", GetCurrentThreadId() + 1};
    for (int i = 0; i < 300; ++i) {
        BuildFromA();
    }
    Require(gRaces == 300 && Count("detail=race") == 33, "the first 32 races are logged, then one in 256");
    Require(Logged("detail=race n=256 "), "the 256th among them");
}

void TestPaused()
{
    Reset();
    gEnabled = false;
    gScene = {true, "single", GetCurrentThreadId() + 1};
    Require(BuildFromA() && gBuildCalls == 1, "paused, the original still runs");
    Require(gRaces == 0 && InPass(kOther) == 0 && gCallerCount == 0 && gLines.empty(), "and nothing is counted");
}

void TestInstall()
{
    Reset();
    gEnabled = false;
    gPinned = 0;
    ServiceResourceSetWatch();
    Require(!gStartAttempted && gLines.empty(), "nothing before the module is pinned");
    Require(SetResourceSetWatch(2) == 1, "a value other than 0 or 1 is refused");

    gPinned = 1;
    ServiceResourceSetWatch();
    Require(gStartAttempted && Logged("preyvr_resource_set result=unavailable detail=preydll_not_loaded") &&
                Logged("result=unavailable code=3") && !gEnabled && !gInstalled,
            "pinned without PreyDll: refused, and off");
    gLines.clear();
    ServiceResourceSetWatch();
    Require(gLines.empty(), "it starts once; no summary while not installed");

    gStartAttempted = false;
    SetEnvironmentVariableW(L"PREYVR_RSET_WATCH", L"0");
    ServiceResourceSetWatch();
    SetEnvironmentVariableW(L"PREYVR_RSET_WATCH", nullptr);
    Require(Logged("preyvr_resource_set result=0 detail=off reason=PREYVR_RSET_WATCH") &&
                !Logged("detail=preydll_not_loaded"),
            "PREYVR_RSET_WATCH=0 leaves it out");
    Require(preyvr::test::gMinHookCalls == 0, "no hook was attempted");
}

void TestReport()
{
    Reset();
    gEnabled = false;
    const std::string report = ResourceSetWatchReport();
    const char* const fields[] = {" installed=0", " enabled=0", " builds=0", " renderIdle=0", " renderInPass=0",
                                  " mainIdle=0", " mainInPass=0", " otherIdle=0", " otherInPass=0", " races=0",
                                  " grown=0", " grownInPass=0", " sceneThread=0", " callers=", " callersDropped=0"};
    std::size_t at = 0;
    for (const char* field : fields) {
        const std::size_t found = report.find(field, at);
        Require(found != std::string::npos, field);
        at = found;
    }
}
} // namespace

int main()
{
    TestThreadsAndSubmissions();
    TestCallers();
    TestBoundedLines();
    TestPaused();
    TestInstall();
    TestReport();
    return 0;
}
