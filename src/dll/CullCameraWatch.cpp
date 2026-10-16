#include "CullCameraWatch.h"
#include "Bootstrap.h"
#include "Logger.h"
#include "MinHookInit.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

namespace preyvr::dll {
namespace {
// Steam PreyDll SHA256 7d6e322f...05311a7. Every offset below is fixed by the
// exact code gate (kCode); docs/CULL-CAMERA-CRASH-2026-10-17.md has the dump and
// the disassembly they were read from. ABI: void Reset(C3DEngine*).
using ResetPassCamerasFn = void(__fastcall*)(void* engine);
constexpr std::uintptr_t kResetRva = 0x2105A0;
constexpr std::uintptr_t kEngineVtableRva = 0x1C912A0;
constexpr std::uintptr_t kResetSlot = 0x90;
// CObjManager*, the owner of the cull queues (its constructor is 0x1F7DF0).
constexpr std::uintptr_t kObjManagerRva = 0x243A698;

// Pool i is engine + (i + 0xA) * 0x80, i the fill thread id (registration
// 0x20B2D0): {CCamera* data; int capacity; Page* overflow; int count}.
constexpr std::uintptr_t kPoolFirst = 0x500, kPoolStride = 0x80;
constexpr std::uintptr_t kPoolData = 0x00, kPoolCapacity = 0x08, kPoolOverflow = 0x10, kPoolCount = 0x18;
// Overflow page, 0x1000 bytes and 7 cameras: {Page* next; int count; int capacity; CCamera* data}.
constexpr std::uintptr_t kPageNext = 0x00, kPageCount = 0x08, kPageCapacity = 0x0C, kPageData = 0x10;
constexpr std::uintptr_t kCameraSize = 0x240;

// The occlusion queue, CObjManager+0x400. The consumer (0x27DA40) waits while
// write == read and takes buffer + (read % capacity) * 0x40, so write - read is
// exactly what it has still to take.
constexpr std::uintptr_t kQueue = 0x400, kQueueBuffer = 0x00, kQueueCapacity = 0x08;
constexpr std::uintptr_t kQueueWrite = 0x10, kQueueRead = 0x40;
// Item: type +0x00, COctreeNode* +0x08, camera +0x38 -- the pointer 0x23ED73
// copies into the render-content job's SRenderingPassInfo+0x18.
constexpr std::uintptr_t kItemSize = 0x40, kItemType = 0x00, kItemNode = 0x08, kItemCamera = 0x38;
// The second queue (+0x480, 0x50-byte items, the one the main thread was
// blocked pushing into at the crash). Recorded raw: which index is the write
// side has not been established.
constexpr std::uintptr_t kQueue2Capacity = 0x4A0, kQueue2A = 0x4A4, kQueue2B = 0x4A8;

// Exact build gate, not a signature for other builds.
// The hooked function: fill id query, pool reset 0x212680, vtable +0x660.
constexpr std::uint8_t kResetCode[] = {
    0x40,0x53,0x48,0x83,0xEC,0x30,0x33,0xD2,0x48,0x8B,0xD9,0x48,0x8B,0x0D,0xA6,0xA0,
    0x22,0x02,0x89,0x54,0x24,0x40,0x48,0x85,0xC9,0x74,0x1F,0x48,0x8B,0x01,0x44,0x8D,
    0x4A,0x04,0x89,0x54,0x24,0x28,0x4C,0x8D,0x44,0x24,0x40,0x48,0x89,0x54,0x24,0x20,
    0x41,0x8D,0x51,0x03,0xFF,0x90,0x88,0x08,0x00,0x00,0x48,0x8B,0x0D,0xEF,0xA0,0x22,
    0x02,0xE8,0x7A,0x79,0x03,0x00,0x8B,0x4C,0x24,0x40,0x33,0xD2,0x48,0x83,0xC1,0x0A,
    0x48,0xC1,0xE1,0x07,0x48,0x03,0xCB,0xE8,0x84,0x20,0x00,0x00,0x48,0x8B,0x03,0x33,
    0xD2,0x48,0x8B,0xCB,0xFF,0x90,0x60,0x06,0x00,0x00,0x48,0x83,0xC4,0x30,0x5B,0xC3};
// Registration: the pool address, count +0x18, capacity +8, data +0, stride 0x240.
constexpr std::uint8_t kPoolRegisterCode[] = {
    0x8B,0x74,0x24,0x50,0x48,0x83,0xC6,0x0A,0x48,0xC1,0xE6,0x07,0x48,0x03,0xF3,0x48,
    0x63,0x56,0x18,0x8B,0x46,0x08,0x3B,0xD0,0x7D,0x1C,0x8D,0x4A,0x01,0x8B,0xC2,0xF0,
    0x0F,0xB1,0x4E,0x18,0x75,0xE9,0x48,0x8D,0x0C,0xD2,0x48,0xC1,0xE1,0x06,0x48,0x03,
    0x0E};
// Overflow walk: head +0x10, page next +0, count +8, capacity +0xC.
constexpr std::uint8_t kPoolOverflowCode[] = {
    0x48,0x8B,0x56,0x10,0x48,0x85,0xD2,0x74,0x20,0x0F,0x1F,0x80,0x00,0x00,0x00,0x00,
    0x48,0x63,0x4A,0x0C,0x48,0x63,0x42,0x08,0x48,0x3B,0xC8,0x0F,0x85,0x8F,0x00,0x00,
    0x00,0x48,0x8B,0x12,0x48,0x85,0xD2,0x75,0xE7,0x45,0x33,0xC0};
// Taking a page slot: page data +0x10.
constexpr std::uint8_t kPageTakeCode[] = {
    0x4C,0x63,0x42,0x08,0x8B,0x42,0x0C,0x44,0x3B,0xC0,0x0F,0x8D,0x69,0xFF,0xFF,0xFF,
    0x41,0x8D,0x48,0x01,0x41,0x8B,0xC0,0xF0,0x0F,0xB1,0x4A,0x08,0x75,0xE2,0x4B,0x8D,
    0x0C,0xC0,0x48,0xC1,0xE1,0x06,0x48,0x03,0x4A,0x10};
// The queue pop: +0x400, buffer, capacity +8, write +0x10, read +0x40, 0x40-byte items.
constexpr std::uint8_t kQueuePopCode[] = {
    0x48,0x83,0xEC,0x48,0x48,0x81,0xC1,0x00,0x04,0x00,0x00,0xC7,0x44,0x24,0x30,0x40,
    0x00,0x00,0x00,0x48,0x8B,0x01,0x4C,0x8D,0x49,0x40,0x48,0x89,0x44,0x24,0x28,0x4C,
    0x8D,0x41,0x10,0x8B,0x41,0x08,0x89,0x44,0x24,0x20,0xE8,0xA1,0xED,0xE1,0xFF,0x48,
    0x83,0xC4,0x48,0xC3};
// The consumer's frame: rbp = rsp + 0x100, the popped item at rsp + 0x50.
constexpr std::uint8_t kConsumerFrameCode[] = {
    0x48,0x89,0x7C,0x24,0x18,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,0xD0,0xFE,
    0xFF,0xFF,0x48,0x81,0xEC,0x30,0x02,0x00,0x00,0x48,0x8B,0x05,0x98,0xBA,0x1F,0x02,
    0x4C,0x8D,0x45,0x7F,0x49,0x83,0xE0,0xF0,0x44,0x0F,0x29,0x84,0x24,0x00,0x02,0x00,
    0x00,0x4C,0x8B,0xF2,0x48,0x8B,0xF9,0x45,0x33,0xFF,0x48,0x8D,0x54,0x24,0x50};
// The consumer's pop through the CObjManager global (0x243A698).
constexpr std::uint8_t kConsumerQueueCode[] = {
    0x48,0x8B,0x0D,0xDD,0xB9,0x1F,0x02,0x41,0x89,0x40,0x7C,0x44,0x89,0x7D,0x80,0xE8,
    0x78,0xED,0x03,0x00};
// Item +0x30/+0x38 (rbp-0x80/-0x78) into the job's pass info +0x14/+0x18.
constexpr std::uint8_t kConsumerCameraCode[] = {
    0x41,0x89,0x46,0x14,0x48,0x8B,0x45,0x88,0x49,0x89,0x46,0x18};
struct CodeSpan {
    std::uintptr_t rva;
    const std::uint8_t* bytes;
    std::size_t size;
};
constexpr std::array<CodeSpan, 8> kCode{{
    {kResetRva, kResetCode, sizeof(kResetCode)},
    {0x20B312, kPoolRegisterCode, sizeof(kPoolRegisterCode)},
    {0x20B360, kPoolOverflowCode, sizeof(kPoolOverflowCode)},
    {0x20B410, kPageTakeCode, sizeof(kPageTakeCode)},
    {0x27DA40, kQueuePopCode, sizeof(kQueuePopCode)},
    {0x23EBD0, kConsumerFrameCode, sizeof(kConsumerFrameCode)},
    {0x23ECB4, kConsumerQueueCode, sizeof(kConsumerQueueCode)},
    {0x23ED6F, kConsumerCameraCode, sizeof(kConsumerCameraCode)},
}};

// Pages beyond this many are not walked, and say so (pagesTruncated): a camera
// in an unwalked page would otherwise be miscounted as outside both pools.
constexpr unsigned kMaxPages = 64;
// Every queued item is classified up to this many; the gate's queue holds 4096.
constexpr unsigned kMaxScan = 0x10000;
// Bounded telemetry: the first events in full, then one in kEventStride.
constexpr unsigned long long kEventsLogged = 64, kEventStride = 512, kBaselineLogged = 8;
constexpr unsigned long long kSummaryMs = 60000;

struct Range {
    std::uintptr_t begin = 0, end = 0;
};
struct Pool {
    bool read = false, pagesTruncated = false;
    std::uintptr_t data = 0, overflow = 0;
    int capacity = 0, count = 0;
    unsigned pages = 0, overflowCameras = 0;
    std::array<Range, kMaxPages> page{};
};
struct Sample {
    std::uintptr_t node = 0, camera = 0;
    unsigned type = 0, age = 0; // age: position from the consumer's read index
};
struct Queue {
    bool read = false, inconsistent = false, scanFault = false;
    std::uintptr_t buffer = 0;
    unsigned capacity = 0, write = 0, readIndex = 0, pending = 0, scanned = 0;
    unsigned q2Capacity = 0, q2A = 0, q2B = 0;
    unsigned nullCamera = 0, outside = 0;
    unsigned inPool[2]{}, inOverflow[2]{};
    Sample oldestIn[2]{}, oldestOutside{};
};
struct Before {
    Pool pools[2];
    Queue queue;
};

std::atomic<ResetPassCamerasFn> gOriginal{nullptr};
std::atomic<std::uintptr_t> gEngineVtable{0}, gObjManagerSlot{0};
std::atomic<bool> gEnabled{false}, gStartAttempted{false}, gInstalled{false};
std::mutex gInstallMutex;
void* gTarget = nullptr;
bool gHookCreated = false;

std::atomic<unsigned long long> gResets{0}, gObserved{0}, gUnreadable{0}, gWrongReceiver{0};
std::atomic<unsigned long long> gIdle{0}, gFreeing{0}, gAmbiguous{0}, gPagesTruncated{0};
std::atomic<unsigned long long> gQueueUnread{0}, gInconsistent{0}, gScanFaults{0};
std::atomic<unsigned long long> gPendingResets{0}, gStaleResets{0}, gStaleItems{0};
std::atomic<unsigned long long> gDoomedResets{0}, gDoomedItems{0};
std::atomic<unsigned long long> gOutsideResets{0}, gOutsideItems{0}, gNullItems{0}, gPreviousItems{0};
std::atomic<unsigned long long> gEvents{0}, gSuppressed{0}, gBaseline{0}, gOffThread{0};
std::atomic<unsigned> gPendingMax{0}, gMaxPages{0}, gMaxOverflowCameras{0};
std::atomic<DWORD> gFirstThread{0}, gLastThread{0};
std::atomic<bool> gLoggedFirstFreeing{false};
std::atomic<ULONGLONG> gNextSummary{0};

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_cull_watch " + line);
}

void Raise(std::atomic<unsigned>& peak, unsigned value)
{
    unsigned seen = peak.load(std::memory_order_relaxed);
    while (value > seen && !peak.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
    }
}

bool SpanMatches(const void* at, const std::uint8_t* bytes, std::size_t size)
{
    __try {
        return at != nullptr && std::memcmp(at, bytes, size) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadWord(std::uintptr_t at, std::uintptr_t& out)
{
    __try {
        out = *reinterpret_cast<const std::uintptr_t*>(at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The gate, plus the proof that the hooked function is the C3DEngine slot --
// which is what makes its receiver a C3DEngine.
bool CodeMatches(std::uintptr_t base)
{
    for (const auto& span : kCode) {
        if (!SpanMatches(reinterpret_cast<const void*>(base + span.rva), span.bytes, span.size)) {
            return false;
        }
    }
    std::uintptr_t slot = 0;
    return ReadWord(base + kEngineVtableRva + kResetSlot, slot) && slot == base + kResetRva;
}

bool ReadPool(std::uintptr_t engine, unsigned index, Pool& out)
{
    __try {
        const std::uintptr_t pool = engine + kPoolFirst + index * kPoolStride;
        out.data = *reinterpret_cast<const std::uintptr_t*>(pool + kPoolData);
        out.capacity = *reinterpret_cast<const int*>(pool + kPoolCapacity);
        out.overflow = *reinterpret_cast<const std::uintptr_t*>(pool + kPoolOverflow);
        out.count = *reinterpret_cast<const int*>(pool + kPoolCount);
        for (std::uintptr_t page = out.overflow; page != 0;
             page = *reinterpret_cast<const std::uintptr_t*>(page + kPageNext)) {
            if (out.pages == kMaxPages) {
                out.pagesTruncated = true;
                break;
            }
            const int count = *reinterpret_cast<const int*>(page + kPageCount);
            const int capacity = *reinterpret_cast<const int*>(page + kPageCapacity);
            const std::uintptr_t data = *reinterpret_cast<const std::uintptr_t*>(page + kPageData);
            const std::uintptr_t slots = capacity > 0 ? static_cast<std::uintptr_t>(capacity) : 0;
            out.page[out.pages++] = Range{data, data + slots * kCameraSize};
            out.overflowCameras += count > 0 ? static_cast<unsigned>(count) : 0u;
        }
        out.read = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out.read = false;
        return false;
    }
}

// 0: not in the pool, 1: its primary array, 2: an overflow page.
int Where(const Pool& pool, std::uintptr_t camera)
{
    if (!pool.read || camera == 0) {
        return 0;
    }
    const std::uintptr_t slots = pool.capacity > 0 ? static_cast<std::uintptr_t>(pool.capacity) : 0;
    if (camera >= pool.data && camera < pool.data + slots * kCameraSize) {
        return 1;
    }
    for (unsigned i = 0; i < pool.pages; ++i) {
        if (camera >= pool.page[i].begin && camera < pool.page[i].end) {
            return 2;
        }
    }
    return 0;
}

void ReadQueue(std::uintptr_t managerSlot, const Pool (&pools)[2], Queue& out)
{
    __try {
        const std::uintptr_t manager =
            managerSlot != 0 ? *reinterpret_cast<const std::uintptr_t*>(managerSlot) : 0;
        if (manager == 0) {
            return;
        }
        const std::uintptr_t queue = manager + kQueue;
        out.buffer = *reinterpret_cast<const std::uintptr_t*>(queue + kQueueBuffer);
        out.capacity = *reinterpret_cast<const unsigned*>(queue + kQueueCapacity);
        out.write = *reinterpret_cast<const unsigned*>(queue + kQueueWrite);
        out.readIndex = *reinterpret_cast<const unsigned*>(queue + kQueueRead);
        out.q2Capacity = *reinterpret_cast<const unsigned*>(manager + kQueue2Capacity);
        out.q2A = *reinterpret_cast<const unsigned*>(manager + kQueue2A);
        out.q2B = *reinterpret_cast<const unsigned*>(manager + kQueue2B);
        out.read = true;
        // Unsigned, as the consumer compares them: the counters wrap.
        out.pending = out.write - out.readIndex;
        if (out.pending == 0) {
            return;
        }
        if (out.buffer == 0 || out.capacity == 0 || out.pending > out.capacity) {
            out.inconsistent = true;
            return;
        }
        for (unsigned k = 0; k < out.pending && k < kMaxScan; ++k) {
            const unsigned index = out.readIndex + k;
            const std::uintptr_t item =
                out.buffer + static_cast<std::uintptr_t>(index % out.capacity) * kItemSize;
            const std::uintptr_t camera = *reinterpret_cast<const std::uintptr_t*>(item + kItemCamera);
            ++out.scanned;
            if (camera == 0) {
                ++out.nullCamera;
                continue;
            }
            Sample* oldest = nullptr;
            bool found = false;
            for (unsigned p = 0; p < 2 && !found; ++p) {
                const int where = Where(pools[p], camera);
                if (where == 0) {
                    continue;
                }
                found = true;
                if (out.inPool[p]++ == 0) {
                    oldest = &out.oldestIn[p];
                }
                if (where == 2) {
                    ++out.inOverflow[p];
                }
            }
            if (!found && out.outside++ == 0) {
                oldest = &out.oldestOutside;
            }
            if (oldest != nullptr) {
                oldest->camera = camera;
                oldest->age = k;
                oldest->node = *reinterpret_cast<const std::uintptr_t*>(item + kItemNode);
                oldest->type = *reinterpret_cast<const unsigned*>(item + kItemType);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out.scanFault = true;
    }
}

bool Capture(void* engine, Before& out)
{
    std::uintptr_t vtable = 0;
    const auto address = reinterpret_cast<std::uintptr_t>(engine);
    if (engine == nullptr || !ReadWord(address, vtable)) {
        gUnreadable.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (vtable != gEngineVtable.load(std::memory_order_acquire)) {
        gWrongReceiver.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (!ReadPool(address, 0, out.pools[0]) || !ReadPool(address, 1, out.pools[1])) {
        gUnreadable.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    ReadQueue(gObjManagerSlot.load(std::memory_order_acquire), out.pools, out.queue);
    return true;
}

void AppendPool(std::ostringstream& line, const Pool& before, const Pool& after)
{
    line << std::hex << "0x" << before.data << std::dec << ',' << before.capacity << ',' << before.count
         << ",pages=" << before.pages << (before.pagesTruncated ? "+" : "") << ",overflowCameras="
         << before.overflowCameras << "->0x" << std::hex << after.data << std::dec << ',' << after.capacity
         << ',' << after.count;
}

void AppendSample(std::ostringstream& line, const char* name, const Sample& sample)
{
    line << ' ' << name << "=node:0x" << std::hex << sample.node << ",camera:0x" << sample.camera << std::dec
         << ",type:" << sample.type << ",age:" << sample.age;
}

// After the original: which pool it reset, whether that freed storage, and
// where the cameras of the items that were still queued pointed.
void Conclude(void* engine, const Before& before)
{
    gObserved.fetch_add(1, std::memory_order_relaxed);
    const auto address = reinterpret_cast<std::uintptr_t>(engine);
    Pool after[2]{};
    ReadPool(address, 0, after[0]);
    ReadPool(address, 1, after[1]);

    int reset = -1;
    bool ambiguous = false;
    for (int p = 0; p < 2; ++p) {
        const Pool& was = before.pools[p];
        const Pool& now = after[p];
        if (was.read && now.read && now.count == 0 &&
            (was.count != 0 || was.overflow != 0 || was.data != now.data)) {
            ambiguous = reset >= 0;
            if (reset < 0) {
                reset = p;
            }
        }
    }
    // 0x212680 reallocates the primary array, and frees it and every page,
    // only when the pool had overflow pages. Without them the storage stays and
    // is only reused by the next registrations.
    const bool freed = reset >= 0 &&
        (before.pools[reset].overflow != 0 || before.pools[reset].data != after[reset].data);
    const Queue& queue = before.queue;
    const unsigned stale = reset >= 0 ? queue.inPool[reset] : 0;
    const unsigned previous = reset >= 0 ? queue.inPool[1 - reset] : 0;
    const unsigned doomed = freed ? stale : 0;

    if (reset < 0) gIdle.fetch_add(1, std::memory_order_relaxed);
    if (ambiguous) gAmbiguous.fetch_add(1, std::memory_order_relaxed);
    if (freed) gFreeing.fetch_add(1, std::memory_order_relaxed);
    for (const Pool& pool : before.pools) {
        Raise(gMaxPages, pool.pages);
        Raise(gMaxOverflowCameras, pool.overflowCameras);
        if (pool.pagesTruncated) gPagesTruncated.fetch_add(1, std::memory_order_relaxed);
    }
    if (!queue.read) gQueueUnread.fetch_add(1, std::memory_order_relaxed);
    if (queue.inconsistent) gInconsistent.fetch_add(1, std::memory_order_relaxed);
    if (queue.scanFault) gScanFaults.fetch_add(1, std::memory_order_relaxed);
    if (queue.pending != 0 && !queue.inconsistent) {
        gPendingResets.fetch_add(1, std::memory_order_relaxed);
        Raise(gPendingMax, queue.pending);
    }
    if (stale != 0) {
        gStaleResets.fetch_add(1, std::memory_order_relaxed);
        gStaleItems.fetch_add(stale, std::memory_order_relaxed);
    }
    if (doomed != 0) {
        gDoomedResets.fetch_add(1, std::memory_order_relaxed);
        gDoomedItems.fetch_add(doomed, std::memory_order_relaxed);
    }
    if (queue.outside != 0) {
        gOutsideResets.fetch_add(1, std::memory_order_relaxed);
        gOutsideItems.fetch_add(queue.outside, std::memory_order_relaxed);
    }
    gNullItems.fetch_add(queue.nullCamera, std::memory_order_relaxed);
    gPreviousItems.fetch_add(previous, std::memory_order_relaxed);

    const DWORD thread = GetCurrentThreadId();
    DWORD first = 0;
    if (!gFirstThread.compare_exchange_strong(first, thread, std::memory_order_relaxed) && first != thread) {
        gOffThread.fetch_add(1, std::memory_order_relaxed);
    }
    gLastThread.store(thread, std::memory_order_relaxed);

    // An event is a queued item the reset invalidates (stale), one already
    // pointing outside both pools, or a queue that could not be read sensibly.
    const bool event = stale != 0 || queue.outside != 0 || queue.inconsistent || queue.scanFault;
    bool log = false;
    const char* detail = "event";
    if (event) {
        const auto n = gEvents.fetch_add(1, std::memory_order_relaxed) + 1;
        log = n <= kEventsLogged || n % kEventStride == 0;
        if (!log) gSuppressed.fetch_add(1, std::memory_order_relaxed);
    } else if (queue.pending != 0 && gBaseline.fetch_add(1, std::memory_order_relaxed) < kBaselineLogged) {
        // Whether items are queued at a reset at all, on a frame where none is stale.
        log = true;
        detail = "pending_at_reset";
    } else if (freed && !gLoggedFirstFreeing.exchange(true, std::memory_order_relaxed)) {
        log = true;
        detail = "first_freeing_reset";
    }
    if (log) {
        std::ostringstream line;
        line << "detail=" << detail << " event=" << gEvents.load(std::memory_order_relaxed)
             << " reset=" << (reset < 0 ? "none" : reset == 0 ? "pool0" : "pool1")
             << " freed=" << (freed ? 1 : 0) << " pending=" << queue.pending << " scanned=" << queue.scanned
             << " stale=" << stale << " doomed=" << doomed << " staleInOverflow="
             << (reset >= 0 ? queue.inOverflow[reset] : 0) << " previous=" << previous
             << " outside=" << queue.outside << " null=" << queue.nullCamera
             << " inconsistent=" << (queue.inconsistent ? 1 : 0) << " scanFault=" << (queue.scanFault ? 1 : 0)
             << " thread=" << thread << " pool0=";
        AppendPool(line, before.pools[0], after[0]);
        line << " pool1=";
        AppendPool(line, before.pools[1], after[1]);
        line << " queue=" << queue.write << '/' << queue.readIndex << '/' << queue.capacity << " q2="
             << queue.q2A << '/' << queue.q2B << '/' << queue.q2Capacity;
        if (reset >= 0 && stale != 0) AppendSample(line, "oldestStale", queue.oldestIn[reset]);
        if (reset >= 0 && previous != 0) AppendSample(line, "oldestPrevious", queue.oldestIn[1 - reset]);
        if (queue.outside != 0) AppendSample(line, "oldestOutside", queue.oldestOutside);
        Log(line.str());
    }

    const ULONGLONG now = GetTickCount64();
    ULONGLONG next = gNextSummary.load(std::memory_order_relaxed);
    if (now >= next && gNextSummary.compare_exchange_strong(next, now + kSummaryMs, std::memory_order_relaxed)) {
        Log("summary" + CullCameraWatchReport());
    }
}

void __fastcall ObserveCameraReset(void* engine)
{
    const auto original = gOriginal.load(std::memory_order_acquire);
    if (original == nullptr) {
        return;
    }
    if (!gEnabled.load(std::memory_order_acquire)) {
        original(engine);
        return;
    }
    gResets.fetch_add(1, std::memory_order_relaxed);
    Before before{};
    const bool captured = Capture(engine, before);
    original(engine);
    if (captured) {
        Conclude(engine, before);
    }
}

DWORD Install()
{
    // Caller holds gInstallMutex. The hook and trampoline stay until process
    // exit; cull.watch 0 only makes the detour pass through.
    if (gInstalled.load(std::memory_order_acquire)) {
        return 0;
    }
    if (ModulePinStatus() == 0) {
        return 2;
    }
    const HMODULE module = GetModuleHandleW(L"PreyDll.dll");
    if (module == nullptr) {
        Log("result=unavailable detail=preydll_not_loaded");
        return 3;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    if (!gHookCreated) {
        if (!CodeMatches(base)) {
            Log("result=refused detail=code_mismatch");
            return 4;
        }
        if (!EnsureMinHook()) {
            return 5;
        }
        gTarget = reinterpret_cast<void*>(base + kResetRva);
        ResetPassCamerasFn original = nullptr;
        const MH_STATUS status = MH_CreateHook(gTarget, reinterpret_cast<void*>(&ObserveCameraReset),
                                               reinterpret_cast<void**>(&original));
        if (status != MH_OK) {
            Log(std::string("result=failed detail=create_hook minhook=") + MH_StatusToString(status));
            return 5;
        }
        gOriginal.store(original, std::memory_order_release);
        gHookCreated = true;
    }
    // Published before the hook can run.
    gEngineVtable.store(base + kEngineVtableRva, std::memory_order_release);
    gObjManagerSlot.store(base + kObjManagerRva, std::memory_order_release);
    gNextSummary.store(GetTickCount64() + kSummaryMs, std::memory_order_relaxed);
    const MH_STATUS status = MH_EnableHook(gTarget);
    if (status != MH_OK) {
        Log(std::string("result=failed detail=enable_hook minhook=") + MH_StatusToString(status));
        return 5;
    }
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=hook_installed target=C3DEngine+0x90 rva=0x2105A0 mutation=none telemetry=bounded");
    return 0;
}
} // namespace

DWORD SetCullCameraWatch(unsigned int enabled)
{
    if (enabled > 1) {
        return 1;
    }
    if (enabled != 0) {
        std::lock_guard lock(gInstallMutex);
        const DWORD installed = Install();
        if (installed != 0) {
            Log("result=unavailable code=" + std::to_string(installed));
            return installed;
        }
    }
    gEnabled.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=enabled value=") + (enabled != 0 ? "1" : "0"));
    return 0;
}

void ServiceCullCameraWatch()
{
    if (gStartAttempted.load(std::memory_order_acquire) || ModulePinStatus() == 0) {
        return;
    }
    gStartAttempted.store(true, std::memory_order_release);
    wchar_t setting[8]{};
    GetEnvironmentVariableW(L"PREYVR_CULL_WATCH", setting, 8);
    if (setting[0] == L'0') {
        Log("result=0 detail=off reason=PREYVR_CULL_WATCH");
        return;
    }
    SetCullCameraWatch(1);
}

std::string CullCameraWatchReport()
{
    const auto n = [](const std::atomic<unsigned long long>& value) { return value.load(std::memory_order_relaxed); };
    std::ostringstream out;
    out << " installed=" << (gInstalled.load() ? 1 : 0) << " enabled=" << (gEnabled.load() ? 1 : 0)
        << " resets=" << n(gResets) << " observed=" << n(gObserved) << " idle=" << n(gIdle)
        << " freeing=" << n(gFreeing) << " maxPages=" << gMaxPages.load()
        << " maxOverflowCameras=" << gMaxOverflowCameras.load() << " pendingResets=" << n(gPendingResets)
        << " pendingMax=" << gPendingMax.load() << " staleResets=" << n(gStaleResets)
        << " staleItems=" << n(gStaleItems) << " doomedResets=" << n(gDoomedResets)
        << " doomedItems=" << n(gDoomedItems) << " outsideResets=" << n(gOutsideResets)
        << " outsideItems=" << n(gOutsideItems) << " previousItems=" << n(gPreviousItems)
        << " nullItems=" << n(gNullItems) << " queueUnread=" << n(gQueueUnread)
        << " inconsistent=" << n(gInconsistent) << " scanFaults=" << n(gScanFaults)
        << " pagesTruncated=" << n(gPagesTruncated) << " unreadable=" << n(gUnreadable)
        << " wrongReceiver=" << n(gWrongReceiver) << " ambiguous=" << n(gAmbiguous)
        << " thread=" << gFirstThread.load() << " offThread=" << n(gOffThread)
        << " events=" << n(gEvents) << " suppressed=" << n(gSuppressed);
    return out.str();
}
} // namespace preyvr::dll
