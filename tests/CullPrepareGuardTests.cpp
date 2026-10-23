// Exercise the occlusion-prepare guard, its observation detour, gate and report
// without loading Prey. Including this TU exposes internal seams only inside this
// test executable.
#include "../src/dll/CullPrepareGuard.cpp"
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include "DllTestSupport.h"
#include "TestSupport.h"

PREYVR_TEST_CAPTURE_LOG()
PREYVR_TEST_STUB_MINHOOK()
namespace preyvr::dll {
DWORD gPinned = 0;
DWORD ModulePinStatus() { return gPinned; }
// The frame probe, as the guard drives it.
struct ProbeCall {
    ProbeStep step;
    std::uint32_t a, b, c, d;
};
std::vector<ProbeCall> gProbeCalls;
void ProbeRecord(ProbeStep step, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d)
{
    gProbeCalls.push_back({step, a, b, c, d});
}
} // namespace preyvr::dll

namespace {
using namespace preyvr::dll;
using preyvr::test::Count;
using preyvr::test::gLines;
using preyvr::test::Logged;
using preyvr::test::Require;

template <typename T> void Put(std::vector<std::uint8_t>& memory, std::uintptr_t offset, T value)
{
    std::memcpy(memory.data() + offset, &value, sizeof(T));
}
template <typename T> T Get(const std::vector<std::uint8_t>& memory, std::uintptr_t offset)
{
    T value{};
    std::memcpy(&value, memory.data() + offset, sizeof(T));
    return value;
}
std::uintptr_t At(const std::vector<std::uint8_t>& memory) { return reinterpret_cast<std::uintptr_t>(memory.data()); }
std::vector<ProbeCall> Calls(ProbeStep step)
{
    std::vector<ProbeCall> out;
    for (const auto& call : gProbeCalls) {
        if (call.step == step) out.push_back(call);
    }
    return out;
}

constexpr std::uintptr_t kFakeVtable = 0x7FF012346000;
struct Fixture;
Fixture* gFixture = nullptr;
unsigned gPrepareCalls = 0;
void* gPrepareReceiver = nullptr;
const void* gPrepareCamera = nullptr;
bool gPrepareDoesNothing = false;

// CObjManager with CCullThread at +0x200, the cvars, the gEnv flag byte,
// gEnv->p3DEngine and CSystem::m_ViewCamera. The fake PrepareOcclusion does what
// 0x20DA10 and PrepareCullbufferAsync do to the handshake: state 1 when its gates pass.
struct Fixture {
    std::vector<std::uint8_t> manager = std::vector<std::uint8_t>(0x300, 0);
    std::vector<std::uint8_t> cvars = std::vector<std::uint8_t>(0x200, 0);
    std::vector<std::uint8_t> engine = std::vector<std::uint8_t>(0x10, 0);
    std::vector<std::uint8_t> camera = std::vector<std::uint8_t>(0x240, 0);
    std::uintptr_t managerGlobal = 0, cvarsGlobal = 0, engineGlobal = 0;
    std::uint8_t noWorld = 0;

    Fixture()
    {
        gFixture = this;
        gPrepareCalls = 0;
        gPrepareReceiver = nullptr;
        gPrepareCamera = nullptr;
        gPrepareDoesNothing = false;
        managerGlobal = At(manager);
        cvarsGlobal = At(cvars);
        engineGlobal = At(engine);
        Put(engine, 0, kFakeVtable);
        Put(cvars, kCVarRenderTasks, 1);
        Translate(512.0f, 1024.0f, 30.0f);
        State(4);
        gObjManagerSlot.store(reinterpret_cast<std::uintptr_t>(&managerGlobal));
        gEngineSlot.store(reinterpret_cast<std::uintptr_t>(&engineGlobal));
        gEngineVtable.store(kFakeVtable);
        gCVarsSlot.store(reinterpret_cast<std::uintptr_t>(&cvarsGlobal));
        gNoWorldFlag.store(reinterpret_cast<std::uintptr_t>(&noWorld));
        gEpsilon.store(0.05f);
        gPrepareOriginal.store(&FakePrepare);
        gPrepareTarget.store(nullptr);
        gInstalled.store(true);
        gEnabled.store(true);
    }
    void State(std::uint32_t state) { Put(manager, kCullThread + kState, state); }
    std::uint32_t State() const { return Get<std::uint32_t>(manager, kCullThread + kState); }
    void Translate(float x, float y, float z)
    {
        Put(camera, kCameraX, x);
        Put(camera, kCameraY, y);
        Put(camera, kCameraZ, z);
    }
    const void* Camera() const { return camera.data(); }

    static void __fastcall FakePrepare(void* receiver, const void* camera)
    {
        ++gPrepareCalls;
        gPrepareReceiver = receiver;
        gPrepareCamera = camera;
        Fixture& f = *gFixture;
        if (gPrepareDoesNothing) return;
        float t[3]{};
        std::memcpy(&t[0], static_cast<const std::uint8_t*>(camera) + kCameraX, 4);
        std::memcpy(&t[1], static_cast<const std::uint8_t*>(camera) + kCameraY, 4);
        std::memcpy(&t[2], static_cast<const std::uint8_t*>(camera) + kCameraZ, 4);
        const bool translated = std::fabs(t[0]) > 0.05f || std::fabs(t[1]) > 0.05f || std::fabs(t[2]) > 0.05f;
        if (Get<int>(f.cvars, kCVarRenderTasks) != 0 && f.noWorld == 0 && translated) f.State(1);
    }
};

void Reset()
{
    for (auto* counter : {&gPrepares, &gPrepareRan, &gSkippedOrigin, &gSkippedRenderTasks, &gSkippedNoWorld,
                          &gGateUnread, &gRenders, &gUnprepared, &gKicks, &gKickFailed, &gOrphans,
                          &gWrongReceiver, &gUnreadable}) {
        counter->store(0);
    }
    gLastPrepare.store(0);
    gFrameLast.store(0);
    gLoggedFirstOrigin.store(false);
    gNextSummary.store(~0ull);
    gProbeCalls.clear();
    gLines.clear();
}

void TestNormalFrames()
{
    Reset();
    Fixture f;
    for (const std::uint32_t state : {1u, 2u}) {
        f.State(state);
        CullPrepareBeforeRender(f.Camera());
    }
    Require(gPrepareCalls == 0 && gKicks == 0 && gUnprepared == 0 && gRenders == 2,
            "a prepare in flight or done: nothing to do");
    // CullStart parking behind a prepare in flight is the normal race.
    CullPrepareAfterRender(1, 3);
    CullPrepareAfterRender(2, 4);
    CullPrepareAfterRender(4, 4);
    Require(gOrphans == 0 && gProbeCalls.empty(), "and no orphan");
}

void TestKick()
{
    for (const std::uint32_t stale : {4u, 0u, 3u}) {
        Reset();
        Fixture f;
        f.State(stale);
        CullPrepareBeforeRender(f.Camera());
        Require(gPrepareCalls == 1 && gPrepareReceiver == f.engine.data() && gPrepareCamera == f.Camera(),
                "no prepare in flight and a camera to render: the engine's prepare, on gEnv->p3DEngine");
        Require(f.State() == 1 && gKicks == 1 && gKickFailed == 0 && gUnprepared == 1, "the handshake can pair");
        const auto kicks = Calls(ProbeStep::prepareKick);
        Require(kicks.size() == 1 && kicks[0].a == stale && kicks[0].b == 1 && kicks[0].c == kGatesOpen &&
                    kicks[0].d == 0,
                "a probe trigger with the states, the gates and no earlier prepare");
        Require(Logged("preyvr_cull_prepare detail=kick n=1 state=") && Logged("->1 lastPrepare=none unprepared=1") &&
                    Logged("camera=512.000,1024.000,30.000"),
                "the kick is logged with the camera it prepared for");
        CullPrepareAfterRender(1, 4);
        Require(gOrphans == 0, "the render after it pairs");
    }
}

void TestGates()
{
    Reset();
    Fixture f;
    const auto expectNoKick = [&](const char* message) {
        f.State(4);
        const auto before = gPrepareCalls;
        CullPrepareBeforeRender(f.Camera());
        Require(gPrepareCalls == before && gUnprepared == 0, message);
    };
    f.Translate(0.04f, -0.05f, 0.0f);
    expectNoKick("within 5 cm of the origin on every axis: PrepareOcclusion and the world both skip");
    f.Translate(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);
    expectNoKick("a NaN is not past the epsilon, as COMISS/JA has it");
    f.Translate(0.0f, 0.0f, 0.06f);
    f.noWorld = 1;
    expectNoKick("the gEnv flag set: no world");
    f.noWorld = 0;
    Put(f.cvars, kCVarRenderTasks, 0);
    expectNoKick("the cvar off: PrepareOcclusion would skip");
    Put(f.cvars, kCVarRenderTasks, 1);
    f.cvarsGlobal = 0;
    expectNoKick("unreadable cvars: no kick");
    f.cvarsGlobal = At(f.cvars);
    f.State(4);
    CullPrepareBeforeRender(f.Camera());
    Require(gKicks == 1 && f.State() == 1, "one axis past 5 cm is enough");
}

void TestGuardOffCountsOrphans()
{
    Reset();
    Fixture f;
    gEnabled.store(false);
    // PostUpdate's own prepare saw the camera at the origin...
    f.Translate(0.0f, 0.0f, 0.0f);
    ObservePrepare(f.engine.data(), f.Camera());
    Require(gSkippedOrigin == 1 && f.State() == 4 && Logged("detail=first_origin_skip state=4 camera=0.000"),
            "the engine's prepare skipped at the origin");
    // ...and Render's world had one by the time it ran.
    f.Translate(10.0f, 20.0f, 1.7f);
    CullPrepareBeforeRender(f.Camera());
    Require(gPrepareCalls == 1 && gUnprepared == 1 && gKicks == 0, "observe only: counted, not prepared");
    f.State(3); // CullStart parked
    CullPrepareAfterRender(4, 3);
    Require(gOrphans == 1, "CullStart parked with no prepare in flight: orphaned");
    const auto orphans = Calls(ProbeStep::orphan);
    Require(orphans.size() == 1 && orphans[0].a == 4 && orphans[0].b == 3 && orphans[0].c == 0 &&
                (orphans[0].d & kSeen) != 0 && (orphans[0].d & kGateCamera) == 0,
            "a probe trigger carrying the guard and the skipped prepare");
    Require(Logged("detail=orphan n=1 state=4->3 guard=0 lastPrepare=skipped_origin"), "and a log line");
    CullPrepareAfterRender(3, 3);
    Require(gOrphans == 2, "every frame it repeats is another");
    CullPrepareAfterRender(kCullStateUnknown, 3);
    Require(gOrphans == 2, "an unread state proves nothing");
}

void TestObservation()
{
    Reset();
    Fixture f;
    f.State(4);
    ObservePrepare(f.engine.data(), f.Camera());
    Require(gPrepareCalls == 1 && gPrepareCamera == f.Camera() && f.State() == 1, "the original runs unchanged");
    Require(gPrepares == 1 && gPrepareRan == 1 && gLastPrepare == (kSeen | kGatesOpen | 1u << 4),
            "a prepare that ran, and the state it left");
    const auto prepares = Calls(ProbeStep::prepare);
    Require(prepares.size() == 1 && prepares[0].a == 4 && prepares[0].b == 1 && prepares[0].c == kGatesOpen,
            "recorded for the probe");
    CullPrepareBeforeRender(f.Camera());
    Require(gPrepareCalls == 1 && gLastPrepare == 0 && (gFrameLast & kSeen) != 0,
            "the render consumes it and has nothing to do");
    CullPrepareBeforeRender(f.Camera());
    Require(gFrameLast == 0, "a render with no prepare since the last one sees none");

    f.State(4);
    Put(f.cvars, kCVarRenderTasks, 0);
    ObservePrepare(f.engine.data(), f.Camera());
    Put(f.cvars, kCVarRenderTasks, 1);
    f.noWorld = 1;
    ObservePrepare(f.engine.data(), f.Camera());
    f.noWorld = 0;
    f.cvarsGlobal = 0;
    ObservePrepare(f.engine.data(), f.Camera());
    f.cvarsGlobal = At(f.cvars);
    Require(gSkippedRenderTasks == 1 && gSkippedNoWorld == 1 && gGateUnread == 1 && gPrepares == 4,
            "each skip by its gate");
    gPrepareOriginal.store(nullptr);
    ObservePrepare(f.engine.data(), f.Camera());
    Require(gPrepares == 5 && gPrepareCalls == 4, "no original: observed, nothing called");
}

void TestKickFailedAndReceiver()
{
    Reset();
    Fixture f;
    gPrepareDoesNothing = true;
    CullPrepareBeforeRender(f.Camera());
    Require(gKickFailed == 1 && gKicks == 0 && Logged("detail=kick_failed n=1 state=4->4"),
            "a prepare that did not start is logged as failed");

    Reset();
    Fixture g;
    Put(g.engine, 0, kFakeVtable + 8);
    CullPrepareBeforeRender(g.Camera());
    g.engineGlobal = 0;
    CullPrepareBeforeRender(g.Camera());
    Require(gPrepareCalls == 0 && gWrongReceiver == 2, "never on anything but a C3DEngine");

    Reset();
    Fixture h;
    gPrepareOriginal.store(nullptr);
    gPrepareTarget.store(&Fixture::FakePrepare);
    CullPrepareBeforeRender(h.Camera());
    Require(gPrepareCalls == 1 && gKicks == 1, "without the observation hook, the gated function itself");
}

void TestUnreadableAndUninstalled()
{
    Reset();
    Fixture f;
    Require(CullThreadState() == 4, "the state for the render bracket");
    f.managerGlobal = 0;
    Require(CullThreadState() == kCullStateUnknown, "no CObjManager: unknown");
    CullPrepareBeforeRender(f.Camera());
    Require(gUnreadable == 1 && gPrepareCalls == 0, "nothing done on an unread state");
    gObjManagerSlot.store(0);
    Require(CullThreadState() == kCullStateUnknown, "not resolved: unknown");

    Reset();
    Fixture g;
    gInstalled.store(false);
    CullPrepareBeforeRender(g.Camera());
    CullPrepareAfterRender(4, 3);
    Require(gRenders == 0 && gOrphans == 0 && gPrepareCalls == 0, "not installed: nothing");
    gInstalled.store(true);
    CullPrepareBeforeRender(nullptr);
    Require(gRenders == 0, "no camera: nothing");
}

void TestLogBudget()
{
    Reset();
    Fixture f;
    for (unsigned i = 0; i < 300; ++i) {
        f.State(4);
        CullPrepareBeforeRender(f.Camera());
        CullPrepareAfterRender(4, 3);
    }
    Require(gKicks == 300 && gOrphans == 300 && Calls(ProbeStep::prepareKick).size() == 300,
            "every kick and orphan is counted and recorded");
    Require(Count("detail=kick n=") == kLogged + 1 && Count("detail=orphan n=") == kLogged + 1,
            "the first 32, then one in 256, are logged");
}

void TestGate()
{
    for (const auto& span : kCode) {
        Require(SafeEqual(span.bytes, span.bytes, span.size), "known gate bytes match");
        std::vector<std::uint8_t> copy(span.bytes, span.bytes + span.size);
        copy[copy.size() / 2] ^= 1;
        Require(!SafeEqual(copy.data(), span.bytes, span.size), "a changed byte is refused");
    }
    // A committed but untouched image is demand-zero, so this costs little.
    const std::size_t size = kEngineVtableRva + 0x1000;
    auto* image = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Require(image != nullptr, "fake image");
    const auto base = reinterpret_cast<std::uintptr_t>(image);
    for (const auto& span : kCode) std::memcpy(image + span.rva, span.bytes, span.size);
    const std::uintptr_t slot = base + kPrepareRva;
    std::memcpy(image + kEngineVtableRva + kPrepareSlot, &slot, sizeof(slot));
    Require(CodeMatches(base), "the supported layout matches");
    const std::uintptr_t other = base + kPrepareRva + 0x10;
    std::memcpy(image + kEngineVtableRva + kPrepareSlot, &other, sizeof(other));
    Require(!CodeMatches(base), "the function must be the C3DEngine +0x40 slot");
    std::memcpy(image + kEngineVtableRva + kPrepareSlot, &slot, sizeof(slot));
    image[0x23F061 + 3] ^= 1; // CullStart's "launch from 2"
    Require(!CodeMatches(base), "a different handshake is refused");
    image[0x23F061 + 3] ^= 1;
    image[0x281E40 + 3] ^= 1; // the CCullThread offset
    Require(!CodeMatches(base), "a different CCullThread offset is refused");
    VirtualFree(image, 0, MEM_RELEASE);
}

void TestCommandsAndSummary()
{
    Reset();
    gInstalled.store(false);
    gEnabled.store(false);
    Require(SetCullPrepareGuard(2) == 1, "bad value refused");
    gPinned = 0;
    Require(SetCullPrepareGuard(1) == 2 && !gEnabled, "not pinned: not installed, guard off");
    gPinned = 1;
    Require(SetCullPrepareGuard(1) == 3 && Logged("detail=preydll_not_loaded") && !gEnabled,
            "no PreyDll: not installed");
    Require(CullPrepareReport().find(" installed=0 guard=0 hooked=") == 0, "report leads with state");

    gStartAttempted.store(false);
    gPinned = 0;
    ServiceCullPrepareGuard();
    Require(!gStartAttempted, "start waits for the pin");
    gPinned = 1;
    SetEnvironmentVariableW(L"PREYVR_CULL_PREPARE", L"0");
    gLines.clear();
    ServiceCullPrepareGuard();
    Require(gStartAttempted && Logged("detail=guard_off reason=PREYVR_CULL_PREPARE") && !gEnabled,
            "opt-out honoured");
    SetEnvironmentVariableW(L"PREYVR_CULL_PREPARE", nullptr);

    // Installed: the guard switches without reinstalling, and the summary runs.
    Fixture f;
    Require(SetCullPrepareGuard(0) == 0 && !gEnabled && Logged("detail=guard value=0"), "off");
    Require(SetCullPrepareGuard(1) == 0 && gEnabled && Logged("detail=guard value=1"), "on");
    gNextSummary.store(0);
    gLines.clear();
    ServiceCullPrepareGuard();
    Require(gLines.size() == 1 && gLines[0].find("preyvr_cull_prepare summary installed=1 guard=1") == 0 &&
                gLines[0].find(" state=4") != std::string::npos,
            "the minute summary, off the game thread");
    gLines.clear();
    ServiceCullPrepareGuard();
    Require(gLines.empty(), "and not again before the next minute");
}
} // namespace

int main()
{
    TestNormalFrames();
    TestKick();
    TestGates();
    TestGuardOffCountsOrphans();
    TestObservation();
    TestKickFailedAndReceiver();
    TestUnreadableAndUninstalled();
    TestLogBudget();
    TestGate();
    TestCommandsAndSummary();
    std::cout << "cull prepare guard tests passed\n";
    return 0;
}
