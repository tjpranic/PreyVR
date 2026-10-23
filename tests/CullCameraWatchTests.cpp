// Exercise the production detour, code gate and report without loading Prey.
// Including this TU exposes internal seams only inside this test executable.
#include "../src/dll/CullCameraWatch.cpp"
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include "DllTestSupport.h"
#include "TestSupport.h"

PREYVR_TEST_CAPTURE_LOG()
PREYVR_TEST_STUB_MINHOOK()
namespace preyvr::dll {
DWORD gPinned = 0;
DWORD ModulePinStatus() { return gPinned; }
// The frame probe, as the watch drives it.
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
using preyvr::test::gLines;
using preyvr::test::Logged;

using preyvr::test::Require;
template <typename T> void Put(std::vector<std::uint8_t>& memory, std::uintptr_t offset, T value)
{
    std::memcpy(memory.data() + offset, &value, sizeof(T));
}
std::uintptr_t At(const std::vector<std::uint8_t>& memory, std::uintptr_t offset = 0)
{
    return reinterpret_cast<std::uintptr_t>(memory.data()) + offset;
}
std::string LastLine() { return gLines.empty() ? std::string() : gLines.back(); }

constexpr std::uintptr_t kFakeVtable = 0x7FF012345000;
constexpr unsigned kRing = 16;
unsigned gOriginalCalls = 0;
void* gLastReceiver = nullptr;
struct Fixture;
Fixture* gFixture = nullptr;

// C3DEngine with pool 0 (4 primary cameras plus one overflow page holding 2,
// count 6 as the engine keeps it) and pool 1 (4 primary, count 2); CObjManager
// with a 16-item occlusion ring. The fake original resets pool 0 the way
// 0x212680 does: coalesced into a new array when it overflowed, else count = 0.
struct Fixture {
    std::vector<std::uint8_t> engine = std::vector<std::uint8_t>(0x600, 0);
    std::vector<std::uint8_t> primary0 = std::vector<std::uint8_t>(4 * kCameraSize, 0);
    std::vector<std::uint8_t> primary1 = std::vector<std::uint8_t>(4 * kCameraSize, 0);
    std::vector<std::uint8_t> page = std::vector<std::uint8_t>(0x20, 0);
    std::vector<std::uint8_t> pageCameras = std::vector<std::uint8_t>(7 * kCameraSize, 0);
    std::vector<std::uint8_t> coalesced = std::vector<std::uint8_t>(6 * kCameraSize, 0);
    std::vector<std::uint8_t> manager = std::vector<std::uint8_t>(0x500, 0);
    std::vector<std::uint8_t> items = std::vector<std::uint8_t>(kRing * kItemSize, 0);
    std::uintptr_t managerGlobal = 0;
    bool overflow = true;

    explicit Fixture(bool withOverflow = true) : overflow(withOverflow)
    {
        gFixture = this;
        gOriginalCalls = 0;
        Put(engine, 0, kFakeVtable);
        const std::uintptr_t pool0 = kPoolFirst, pool1 = kPoolFirst + kPoolStride;
        Put(engine, pool0 + kPoolData, At(primary0));
        Put(engine, pool0 + kPoolCapacity, 4);
        Put(engine, pool0 + kPoolOverflow, overflow ? At(page) : std::uintptr_t{0});
        Put(engine, pool0 + kPoolCount, overflow ? 6 : 3);
        Put(page, kPageNext, std::uintptr_t{0});
        Put(page, kPageCount, 2);
        Put(page, kPageCapacity, 7);
        Put(page, kPageData, At(pageCameras));
        Put(engine, pool1 + kPoolData, At(primary1));
        Put(engine, pool1 + kPoolCapacity, 4);
        Put(engine, pool1 + kPoolCount, 2);
        Put(manager, kQueue + kQueueBuffer, At(items));
        Put(manager, kQueue + kQueueCapacity, kRing);
        Put(manager, kQueue2Capacity, 0x8000u);
        managerGlobal = At(manager);
        gEngineVtable.store(kFakeVtable);
        gObjManagerSlot.store(reinterpret_cast<std::uintptr_t>(&managerGlobal));
        gOriginal.store(&FakeReset);
        gEnabled.store(true);
        gNextSummary.store(~0ull);
    }
    // Queues octree items with `cameras` starting at the consumer's index `read`.
    void Queue(unsigned read, std::initializer_list<std::uintptr_t> cameras)
    {
        unsigned k = 0;
        for (const std::uintptr_t camera : cameras) {
            Item(read, k++, kItemOctree, camera);
        }
        Put(manager, kQueue + kQueueRead, read);
        Put(manager, kQueue + kQueueWrite, read + k);
    }
    // Queues items of any type: {type, camera}.
    void QueueTyped(unsigned read, std::initializer_list<std::pair<unsigned, std::uintptr_t>> entries)
    {
        unsigned k = 0;
        for (const auto& [type, camera] : entries) {
            Item(read, k++, type, camera);
        }
        Put(manager, kQueue + kQueueRead, read);
        Put(manager, kQueue + kQueueWrite, read + k);
    }
    void Item(unsigned read, unsigned k, unsigned type, std::uintptr_t camera)
    {
        const unsigned slot = (read + k) % kRing;
        Put(items, slot * kItemSize + kItemType, type);
        Put(items, slot * kItemSize + kItemNode, std::uintptr_t{0x1000} + k);
        Put(items, slot * kItemSize + kItemCamera, camera);
    }
    void Run() { ObserveCameraReset(engine.data()); }

    static void __fastcall FakeReset(void* receiver)
    {
        ++gOriginalCalls;
        gLastReceiver = receiver;
        Fixture& f = *gFixture;
        if (receiver != f.engine.data()) {
            return;
        }
        if (f.overflow) {
            Put(f.engine, kPoolFirst + kPoolData, At(f.coalesced));
            Put(f.engine, kPoolFirst + kPoolCapacity, 6);
            Put(f.engine, kPoolFirst + kPoolOverflow, std::uintptr_t{0});
        }
        Put(f.engine, kPoolFirst + kPoolCount, 0);
    }
    // Resets pool 1 instead, by count only.
    static void __fastcall FakeResetPool1(void*)
    {
        ++gOriginalCalls;
        Put(gFixture->engine, kPoolFirst + kPoolStride + kPoolCount, 0);
    }
};

void ResetCounters()
{
    for (auto* counter : {&gResets, &gObserved, &gUnreadable, &gWrongReceiver, &gIdle, &gFreeing, &gAmbiguous,
                          &gPagesTruncated, &gQueueUnread, &gInconsistent, &gScanFaults, &gPendingResets,
                          &gStaleResets, &gStaleItems, &gDoomedResets, &gDoomedItems, &gOutsideResets,
                          &gOutsideItems, &gNullItems, &gPreviousItems, &gEvents, &gSuppressed, &gBaseline,
                          &gOffThread, &gMarkerItems, &gOtherTypeItems, &gInferredResets, &gDoomedLogged,
                          &gStaleEvents, &gDepthChanges}) {
        counter->store(0);
    }
    for (auto& count : gAtDepth) count.store(0);
    gDepth.store(kDepthNone);
    gDepthMax.store(kDepthNone);
    gProbeCalls.clear();
    gPendingMax.store(0);
    gMaxPages.store(0);
    gMaxOverflowCameras.store(0);
    gFirstThread.store(0);
    gLastThread.store(0);
    gLoggedFirstFreeing.store(false);
    gLines.clear();
}

void TestEmptyQueueFreeingReset()
{
    ResetCounters();
    Fixture f;
    f.Queue(5, {});
    const auto engineBefore = f.engine;
    f.Run();
    Require(gOriginalCalls == 1 && gLastReceiver == f.engine.data(), "original runs once, on the same receiver");
    Require(gResets == 1 && gObserved == 1 && gFreeing == 1 && gIdle == 0, "an overflowed pool reset frees");
    Require(gPendingResets == 0 && gStaleItems == 0 && gEvents == 0, "an empty queue is not an event");
    Require(gMaxPages == 1 && gMaxOverflowCameras == 2, "the overflow page is walked");
    Require(Logged("detail=first_freeing_reset") && Logged("reset=pool0 freed=1"), "first freeing reset is logged");
    Require(gFirstThread == GetCurrentThreadId() && gOffThread == 0, "reset thread recorded");
    // Mutation-free apart from what the original itself did to pool 0.
    for (std::size_t i = 0; i < engineBefore.size(); ++i) {
        if (i < kPoolFirst || i >= kPoolFirst + kPoolStride) {
            Require(f.engine[i] == engineBefore[i], "no engine bytes outside the original's own change");
        }
    }
    f.Run();
    Require(gResets == 2 && gIdle == 1 && gFreeing == 1, "a second reset of an empty pool is idle");
}

void TestStaleAndDoomedItems()
{
    ResetCounters();
    Fixture f;
    // Read index 14 of a 16-item ring, so the scan wraps: 14, 15, 0, 1, 2, 3.
    f.Queue(14, {
        At(f.primary0, 1 * kCameraSize),    // pool 0 primary: stale, freed with it
        At(f.pageCameras, 0),               // pool 0 overflow page: stale, freed
        At(f.primary1, 0),                  // pool 1: the previous frame's, untouched
        0,                                  // no camera
        0x0000026100000000ull,              // in neither pool: already dangling
        At(f.primary0, 3 * kCameraSize + 0x23F), // last byte of pool 0's primary
    });
    f.Run();
    Require(gOriginalCalls == 1, "original runs exactly once with items queued");
    Require(gPendingResets == 1 && gPendingMax == 6, "pending is write - read");
    Require(gStaleResets == 1 && gStaleItems == 3, "items whose camera the reset reclaims");
    Require(gDoomedResets == 1 && gDoomedItems == 3, "and the storage was freed");
    Require(gPreviousItems == 1 && gOutsideItems == 1 && gOutsideResets == 1 && gNullItems == 1,
            "other pool, outside and null are told apart");
    Require(gEvents == 1 && gDoomedLogged == 1, "one event, logged as doomed");
    const std::string line = LastLine();
    Require(line.find("detail=doomed") != std::string::npos && line.find("stale=3 doomed=3") != std::string::npos &&
                line.find("staleInOverflow=1") != std::string::npos && line.find("previous=1") != std::string::npos &&
                line.find("outside=1 null=1") != std::string::npos,
            "event line carries the classification");
    Require(line.find("oldestStale=node:0x1000,") != std::string::npos && line.find("age:0") != std::string::npos,
            "oldest stale item is the consumer's next");
    Require(line.find("oldestOutside=node:0x1004,camera:0x26100000000") != std::string::npos,
            "oldest outside item names its camera");
    Require(line.find("queue=20/14/16") != std::string::npos, "raw queue indices");
}

void TestResetWithoutOverflowDoesNotFree()
{
    ResetCounters();
    Fixture f(false);
    f.Queue(0, {At(f.primary0, 0)});
    f.Run();
    Require(gFreeing == 0 && gStaleItems == 1 && gDoomedItems == 0,
            "count reset alone: the camera goes stale but its storage stays");
    Require(LastLine().find("freed=0") != std::string::npos && LastLine().find("doomed=0") != std::string::npos,
            "logged as stale, not doomed");
}

void TestCounterWrap()
{
    ResetCounters();
    Fixture f;
    f.Queue(0xFFFFFFFEu, {At(f.primary0, 0), At(f.primary0, 0), At(f.primary0, 0)});
    f.Run();
    Require(gPendingMax == 3 && gStaleItems == 3, "wrapped 32-bit counters still give write - read");
}

void TestRefusals()
{
    ResetCounters();
    {
        Fixture f;
        Put(f.engine, 0, kFakeVtable + 8);
        f.Run();
        Require(gOriginalCalls == 1 && gWrongReceiver == 1 && gObserved == 0, "wrong receiver: original only");
    }
    {
        Fixture f;
        ObserveCameraReset(nullptr);
        Require(gOriginalCalls == 1 && gLastReceiver == nullptr && gUnreadable == 1,
                "null receiver: original only, still called");
    }
    {
        Fixture f;
        gObjManagerSlot.store(0);
        f.Run();
        Require(gQueueUnread == 1 && gObserved == 1 && gFreeing == 1, "no queue: pools still observed");
        f.managerGlobal = 0;
        gObjManagerSlot.store(reinterpret_cast<std::uintptr_t>(&f.managerGlobal));
        f.Run();
        Require(gQueueUnread == 2, "null CObjManager: queue unread");
    }
    {
        ResetCounters();
        Fixture f;
        f.Queue(0, {});
        Put(f.manager, kQueue + kQueueWrite, 17u);
        f.Run();
        Require(gInconsistent == 1 && gPendingResets == 0 && gEvents == 1 && Logged("inconsistent=1"),
                "pending beyond capacity is reported, never scanned");
    }
    {
        ResetCounters();
        Fixture f;
        f.managerGlobal = 1; // unreadable CObjManager
        f.Run();
        Require(gOriginalCalls == 1 && gQueueUnread == 1 && gScanFaults == 1, "faulting queue read is caught");
    }
    {
        ResetCounters();
        Fixture f;
        gEnabled.store(false);
        f.Run();
        Require(gOriginalCalls == 1 && gResets == 0, "disabled: pass straight through");
        gOriginal.store(nullptr);
        gEnabled.store(true);
        f.Run();
        Require(gOriginalCalls == 1 && gResets == 0, "no original: nothing runs");
    }
}

void TestOffThreadAndBounds()
{
    ResetCounters();
    Fixture f;
    f.Run();
    std::thread([&] { f.Run(); }).join();
    Require(gOffThread == 1 && gLastThread != gFirstThread, "a reset on another thread is counted");

    ResetCounters();
    for (unsigned i = 0; i < 600; ++i) {
        f.Queue(i % kRing, {At(f.primary1, 0)});
        // Pool 1 is the one reclaimed here, so its queued camera goes stale.
        Put(f.engine, kPoolFirst + kPoolCount, 0);
        Put(f.engine, kPoolFirst + kPoolStride + kPoolCount, 2);
        gOriginal.store(&Fixture::FakeResetPool1);
        f.Run();
    }
    std::size_t events = 0;
    for (const auto& line : gLines) events += line.find("detail=event") != std::string::npos ? 1 : 0;
    Require(gEvents == 600 && gStaleItems == 600, "every stale reset counted");
    Require(events == kEventsLogged + 1 && gSuppressed == 600 - events, "the first 64 then one in 512 are logged");
}

void TestBaselineAndSummary()
{
    ResetCounters();
    Fixture f;
    for (unsigned i = 0; i < 12; ++i) {
        f.Queue(0, {At(f.primary1, 0)}); // queued, but the previous frame's
        Put(f.engine, kPoolFirst + kPoolCount, 1);
        f.Run();
    }
    std::size_t baseline = 0;
    for (const auto& line : gLines) baseline += line.find("pending_at_reset") != std::string::npos ? 1 : 0;
    Require(baseline == kBaselineLogged && gEvents == 0 && gPreviousItems == 12,
            "items of the other pool are a bounded baseline, not events");
    gNextSummary.store(0);
    f.Run();
    Require(LastLine().find("preyvr_cull_watch summary installed=") == 0, "minute summary");
    Require(gNextSummary.load() > GetTickCount64(), "summary rescheduled");
}

void TestGate()
{
    for (const auto& span : kCode) {
        Require(SpanMatches(span.bytes, span.bytes, span.size), "known gate bytes match");
        std::vector<std::uint8_t> copy(span.bytes, span.bytes + span.size);
        copy[copy.size() / 2] ^= 1;
        Require(!SpanMatches(copy.data(), span.bytes, span.size), "a changed byte is refused");
    }
    Require(!SpanMatches(nullptr, kResetCode, sizeof(kResetCode)), "null refused");
    Require(!SpanMatches(reinterpret_cast<const void*>(1), kResetCode, sizeof(kResetCode)),
            "unreadable code caught by SEH");

    // A committed but untouched image is demand-zero, so this costs little.
    const std::size_t size = kEngineVtableRva + 0x1000;
    auto* image = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Require(image != nullptr, "fake image");
    const auto base = reinterpret_cast<std::uintptr_t>(image);
    for (const auto& span : kCode) std::memcpy(image + span.rva, span.bytes, span.size);
    const std::uintptr_t slot = base + kResetRva;
    std::memcpy(image + kEngineVtableRva + kResetSlot, &slot, sizeof(slot));
    Require(CodeMatches(base), "the supported layout matches");
    const std::uintptr_t other = base + kResetRva + 0x10;
    std::memcpy(image + kEngineVtableRva + kResetSlot, &other, sizeof(other));
    Require(!CodeMatches(base), "the hooked function must be the C3DEngine +0x90 slot");
    std::memcpy(image + kEngineVtableRva + kResetSlot, &slot, sizeof(slot));
    image[0x23ED6F + 7] ^= 1; // the item offset the camera is copied from
    Require(!CodeMatches(base), "a different item layout is refused");
    VirtualFree(image, 0, MEM_RELEASE);
}

void TestItemTypes()
{
    ResetCounters();
    Fixture f;
    // A batch's end marker and a type-2 item carry no camera at +0x38, whatever
    // their bytes say (the false "outside" of 2026-10-18).
    f.QueueTyped(0, {{kItemOctree, At(f.primary0, 0)}, {kItemMarker, 0xAB1A5ED150ull}, {2u, 0x1234ull}});
    f.Run();
    Require(gMarkerItems == 1 && gOtherTypeItems == 1 && gOutsideItems == 0 && gStaleItems == 1,
            "only octree items are classified");
    Require(LastLine().find("markers=1 otherTypes=1") != std::string::npos, "the event line counts the rest");
}

void TestIdleResetInference()
{
    ResetCounters();
    Fixture f(false);
    // Pool 0 registered nothing since its last reset; pool 1 holds the frame in
    // flight. The reset changes nothing, yet pool 0 is the one it reclaims.
    Put(f.engine, kPoolFirst + kPoolCount, 0);
    f.Queue(0, {At(f.primary0, 0), At(f.primary1, 0)});
    f.Run();
    Require(gIdle == 1 && gInferredResets == 1, "an idle reset is told by the empty pool");
    Require(gStaleItems == 1 && gPreviousItems == 1 && gDepth == kDepthStale, "and its items are classified");
    Require(LastLine().find("reset=pool0") != std::string::npos && LastLine().find("inferred=1") != std::string::npos,
            "logged as inferred");

    ResetCounters();
    Fixture both(false);
    Put(both.engine, kPoolFirst + kPoolCount, 0);
    Put(both.engine, kPoolFirst + kPoolStride + kPoolCount, 0);
    both.Queue(0, {At(both.primary1, 0)});
    both.Run();
    Require(gInferredResets == 0 && gStaleItems == 0 && gAtDepth[1] == 0 && gDepth == kDepthNone,
            "two empty pools: nothing inferred, the depth unknown and left as it was");
}

void TestLagDepth()
{
    ResetCounters();
    Fixture f(false);
    const auto run = [&](std::initializer_list<std::uintptr_t> cameras, int pool0Count) {
        f.Queue(0, cameras);
        Put(f.engine, kPoolFirst + kPoolCount, pool0Count);
        f.Run();
    };
    run({}, 3);                     // nothing queued: depth 0, as it starts
    run({At(f.primary1, 0)}, 3);    // last frame's: 1
    run({At(f.primary1, 0)}, 3);    // unchanged
    run({At(f.primary0, 0)}, 3);    // the reset pool's: 2
    run({0x26100000000ull}, 3);     // neither: 3
    run({}, 3);                     // drained: 0
    std::vector<std::pair<std::uint32_t, std::uint32_t>> changes;
    for (const auto& call : gProbeCalls) {
        if (call.step == ProbeStep::lagChange) changes.emplace_back(call.a, call.b);
    }
    Require(changes == std::vector<std::pair<std::uint32_t, std::uint32_t>>{{1, 0}, {2, 1}, {3, 2}, {0, 3}},
            "every change of depth opens a probe window");
    std::size_t lines = 0;
    for (const auto& line : gLines) lines += line.find("detail=lag ") != std::string::npos ? 1 : 0;
    Require(lines == 4 && gDepthChanges == 4 && Logged("detail=lag depth=2 was=1 change=2"), "and is logged");
    Require(gAtDepth[0] == 2 && gAtDepth[1] == 2 && gAtDepth[2] == 1 && gAtDepth[3] == 1 && gDepthMax == 3,
            "resets are counted by depth");
    const std::string report = CullCameraWatchReport();
    Require(report.find(" depth=0 depthMax=3 depthChanges=4 atDepth=2/2/1/1") != std::string::npos,
            "the report carries the depth");
}

void TestDoomedBudget()
{
    ResetCounters();
    Fixture f;
    // Exhaust the stale budget with count-only resets of pool 1 (pool 0 idle,
    // without its page, so only pool 1 reads as reset)...
    Put(f.engine, kPoolFirst + kPoolOverflow, std::uintptr_t{0});
    for (unsigned i = 0; i < 100; ++i) {
        f.Queue(i % kRing, {At(f.primary1, 0)});
        Put(f.engine, kPoolFirst + kPoolCount, 0);
        Put(f.engine, kPoolFirst + kPoolStride + kPoolCount, 2);
        gOriginal.store(&Fixture::FakeResetPool1);
        f.Run();
    }
    Require(gStaleEvents == 100 && gSuppressed == 100 - kEventsLogged, "stale events past the budget are dropped");
    // ...then a reset that frees what is still queued is logged regardless.
    gOriginal.store(&Fixture::FakeReset);
    Put(f.engine, kPoolFirst + kPoolOverflow, At(f.page));
    Put(f.engine, kPoolFirst + kPoolCount, 6);
    f.Queue(0, {At(f.pageCameras, 0)});
    f.Run();
    Require(gDoomedResets == 1 && gDoomedLogged == 1 && LastLine().find("detail=doomed") != std::string::npos,
            "a doomed reset has its own budget");
}

void TestTicks()
{
    ResetCounters();
    Fixture f;
    f.Queue(3, {At(f.primary1, 0), At(f.primary1, 0)});
    f.Run();
    gEnabled.store(false);
    f.Run();
    gEnabled.store(true);
    std::vector<ProbeCall> ticks;
    for (const auto& call : gProbeCalls) {
        if (call.step == ProbeStep::tick) ticks.push_back(call);
    }
    Require(ticks.size() == 2, "every reset is a probe tick, observed or not");
    Require(ticks[0].a == kDepthPrevious && ticks[0].b == 2 && ticks[0].c == 5 && ticks[0].d == 3,
            "an observed tick carries depth, pending and the queue");
    Require(ticks[1].a == kLagUnknown && ticks[1].c == 5 && ticks[1].d == 3, "a passed-through one only the queue");
    std::uint32_t write = 0, read = 0;
    Require(CullQueueIndices(write, read) && write == 5 && read == 3, "queue indices for the probe");
    gObjManagerSlot.store(0);
    Require(!CullQueueIndices(write, read) && write == 0 && read == 0, "none without CObjManager");
}

void TestCommands()
{
    ResetCounters();
    gEnabled.store(true);
    Require(SetCullCameraWatch(2) == 1 && gEnabled, "bad value refused, setting kept");
    gPinned = 0;
    Require(SetCullCameraWatch(1) == 2, "not pinned: not installed");
    gPinned = 1;
    Require(SetCullCameraWatch(1) == 3 && Logged("detail=preydll_not_loaded"), "no PreyDll: not installed");
    Require(SetCullCameraWatch(0) == 0 && !gEnabled, "off needs no hook");
    Require(CullCameraWatchReport().find(" installed=0 enabled=0 resets=") == 0, "report leads with state");

    gStartAttempted.store(false);
    gPinned = 0;
    ServiceCullCameraWatch();
    Require(!gStartAttempted, "start waits for the pin");
    gPinned = 1;
    SetEnvironmentVariableW(L"PREYVR_CULL_WATCH", L"0");
    ServiceCullCameraWatch();
    Require(gStartAttempted && Logged("detail=off reason=PREYVR_CULL_WATCH") && !gEnabled, "opt-out honoured");
    gLines.clear();
    ServiceCullCameraWatch();
    Require(gLines.empty(), "start is attempted once");
    SetEnvironmentVariableW(L"PREYVR_CULL_WATCH", nullptr);
}
} // namespace

int main()
{
    TestEmptyQueueFreeingReset();
    TestStaleAndDoomedItems();
    TestResetWithoutOverflowDoesNotFree();
    TestCounterWrap();
    TestRefusals();
    TestOffThreadAndBounds();
    TestBaselineAndSummary();
    TestGate();
    TestItemTypes();
    TestIdleResetInference();
    TestLagDepth();
    TestDoomedBudget();
    TestTicks();
    TestCommands();
    std::cout << "cull camera watch tests passed\n";
    return 0;
}
