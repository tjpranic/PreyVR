// Exercise the occlusion-prepare guard, its gate and report without loading
// Prey. Including this TU exposes internal seams only inside this
// test executable.
#include "engine/fixes/CullPrepareGuard.cpp"
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include "DllTestSupport.h"
#include "TestSupport.h"

PREYVR_TEST_CAPTURE_LOG()
namespace preyvr::dll {
DWORD gPinned = 0;
DWORD ModulePinStatus() { return gPinned; }
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
        gPrepareTarget.store(&FakePrepare);
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
    for (auto* counter : {&gKicks, &gKickFailed, &gWrongReceiver, &gUnreadable}) {
        counter->store(0);
    }
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
    Require(gPrepareCalls == 0 && gKicks == 0 && gLines.empty(), "a prepare in flight or done: nothing to do");
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
        Require(f.State() == 1 && gKicks == 1 && gKickFailed == 0, "the handshake can pair");
        Require(gLines.empty(), "a kick that worked is counted, not logged");
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
        Require(gPrepareCalls == before && gKicks == 0, message);
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

void TestGuardOff()
{
    Reset();
    Fixture f;
    gEnabled.store(false);
    CullPrepareBeforeRender(f.Camera());
    Require(gPrepareCalls == 0 && gKicks == 0 && f.State() == 4, "off: only the engine's own handshake");
}

void TestKickFailedAndReceiver()
{
    Reset();
    Fixture f;
    gPrepareDoesNothing = true;
    CullPrepareBeforeRender(f.Camera());
    Require(gKickFailed == 1 && gKicks == 0 && Logged("detail=kick_failed n=1 state=4->4") &&
                Logged("camera=512.000,1024.000,30.000"),
            "a prepare that did not start is logged as failed, with the camera");
    for (unsigned i = 0; i < 20; ++i) {
        f.State(4);
        CullPrepareBeforeRender(f.Camera());
    }
    Require(gKickFailed == 21 && Count("detail=kick_failed") == kFailuresLogged, "the first few, then counted only");

    Reset();
    Fixture g;
    Put(g.engine, 0, kFakeVtable + 8);
    CullPrepareBeforeRender(g.Camera());
    g.engineGlobal = 0;
    CullPrepareBeforeRender(g.Camera());
    Require(gPrepareCalls == 0 && gWrongReceiver == 2, "never on anything but a C3DEngine");

    Reset();
    Fixture h;
    gPrepareTarget.store(nullptr);
    CullPrepareBeforeRender(h.Camera());
    Require(gPrepareCalls == 0 && gKicks == 0, "nothing to call before install");
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
    Require(gUnreadable == 0 && gPrepareCalls == 0, "not installed: nothing");
    gInstalled.store(true);
    CullPrepareBeforeRender(nullptr);
    Require(gUnreadable == 0 && gPrepareCalls == 0, "no camera: nothing");
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

void TestCommands()
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
    Require(CullPrepareReport().find(" installed=0 guard=0 kicks=0 kickFailed=0 wrongReceiver=0 unreadable=0 state=") ==
                0,
            "report leads with state");

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

    // Installed: the guard switches without reinstalling; nothing runs in the background.
    Fixture f;
    Require(SetCullPrepareGuard(0) == 0 && !gEnabled && Logged("detail=guard value=0"), "off");
    Require(SetCullPrepareGuard(1) == 0 && gEnabled && Logged("detail=guard value=1"), "on");
    Require(CullPrepareReport().find(" installed=1 guard=1 kicks=0") == 0 &&
                CullPrepareReport().find(" state=4") != std::string::npos,
            "the counts and the handshake state, on request");
    gLines.clear();
    ServiceCullPrepareGuard();
    Require(gLines.empty(), "no summary any more");
}
} // namespace

int main()
{
    TestNormalFrames();
    TestKick();
    TestGates();
    TestGuardOff();
    TestKickFailedAndReceiver();
    TestUnreadableAndUninstalled();
    TestGate();
    TestCommands();
    std::cout << "cull prepare guard tests passed\n";
    return 0;
}
