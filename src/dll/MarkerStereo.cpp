#include "MarkerStereo.h"

#include "CameraEditHook.h"
#include "EngineAccess.h"
#include "HookInstall.h"
#include "HudLayer.h"
#include "Logger.h"
#include "SafeMemory.h"
#include "preyvr/MarkerDepth.h"
#include "preyvr/MonotonicClock.h"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>

namespace preyvr::dll {
namespace {

void Log(const std::string& line) { lifecycle::Log("preyvr_marker_depth " + line); }

std::atomic<bool> gEnabled{true};
std::atomic<unsigned> gSizePercent{100}; // options::Defaults(): marker_size 100

// The game side records while either the depth or the size wants the table.
bool Recording()
{
    return gEnabled.load(std::memory_order_relaxed) || gSizePercent.load(std::memory_order_relaxed) != 100;
}

// --- the table: per clip, canvas units per metre of eye offset ---------------
//
// Written on the game thread as Prey places each marker, read on the render
// thread when the movie is drawn. Keyed by the clip's path, so a marker keeps
// its depth across a frame the two threads see differently; one that is no
// longer placed expires.
constexpr std::size_t kMaxMarkers = 64;
constexpr std::size_t kPathSize = 112;
constexpr std::uint64_t kExpiryNs = 500'000'000;

struct Entry {
    char path[kPathSize];
    float perMetre;
    std::uint64_t stamp;
};
std::mutex gTableMutex;
std::array<Entry, kMaxMarkers> gTable{};
std::size_t gTableCount = 0;
std::atomic<unsigned long long> gNoted{0}, gTableFull{0};

// Game thread. `clip` is the marker's clip path, e.g. "_root.ar_mc.objectives.Obj1".
void NoteMarker(const char* clip, float perMetre, std::uint64_t now)
{
    const std::size_t length = strnlen(clip, kPathSize);
    if (length == 0 || length == kPathSize || !std::isfinite(perMetre)) {
        return;
    }
    gNoted.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard lock(gTableMutex);
    Entry* slot = nullptr;
    for (std::size_t i = 0; i < gTableCount; ++i) {
        if (std::strcmp(gTable[i].path, clip) == 0) { slot = &gTable[i]; break; }
    }
    if (slot == nullptr) {
        if (gTableCount < kMaxMarkers) {
            slot = &gTable[gTableCount++];
        } else {
            // Full: the stalest entry goes.
            slot = &gTable[0];
            for (std::size_t i = 1; i < gTableCount; ++i) {
                if (gTable[i].stamp < slot->stamp) { slot = &gTable[i]; }
            }
            gTableFull.fetch_add(1, std::memory_order_relaxed);
        }
        std::memcpy(slot->path, clip, length + 1);
    }
    slot->perMetre = perMetre;
    slot->stamp = now;
}

// --- the game side: which marker is at which depth -----------------------------
//
// Prey places each marker in two steps on the game thread: it projects the
// world point (0x16659F0, through 0x16658C0 or followed by 0x16657C0 for the
// canvas units), then hands the movie the marker's ID with that canvas X/Y,
// bit-identical, through the element's CallFunction (+0x210, 0x2F8E70) or
// SetArray (+0x288, 0x2FFC20). Each projection is kept with its depth until a
// call carrying its X/Y names the marker; the movie names the marker's clip
// after that ID (attachMovie(link, ID, ...)), which gives the clip's path.
// Static, 2026-10-21 (docs/MARKER-DEPTH-2026-10-21.md).
constexpr std::uint32_t kProjectRva = 0x16659F0;       // out* (out{x,y,onScreen}, const Vec3* world, bool clamp)
constexpr std::uint32_t kToCanvasRva = 0x16658C0;      // the same, out in canvas units
constexpr std::uint32_t kNormToCanvasRva = 0x16657C0;  // Vec2* (Vec2* out, const Vec2* normalized)
constexpr std::uint32_t kPreRenderRva = 0x1667440;     // CArkUIHUD::OnPreRender(this)
constexpr std::uint32_t kCallFunctionRva = 0x2F8E70;   // UIElement +0x210
constexpr std::uint32_t kSetArrayRva = 0x2FFC20;       // UIElement +0x288
constexpr std::uint32_t kHudMatrixRva = 0x228CAA0;     // OnPreRender's view-projection, 16 floats

// Each stops before the first RIP-relative operand (or call).
constexpr std::uint8_t kProjectPrologue[] = {0x40, 0x55, 0x48, 0x8D, 0x6C, 0x24, 0xA9, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00,
    0x00, 0xF3, 0x0F, 0x10, 0x52, 0x04, 0xF3, 0x0F, 0x10, 0x5A, 0x08, 0x0F, 0x28, 0xC2};
constexpr std::uint8_t kToCanvasPrologue[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B,
    0xF9, 0x0F, 0x29, 0x74, 0x24, 0x50, 0x48, 0x8D, 0x4C, 0x24, 0x38};
constexpr std::uint8_t kNormToCanvasPrologue[] = {0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x33, 0xC0, 0x0F,
    0x29, 0x74, 0x24, 0x30, 0x48, 0x8B, 0xF1, 0x48, 0x89, 0x44, 0x24, 0x68};
constexpr std::uint8_t kPreRenderPrologue[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x48, 0x89, 0x78, 0x20, 0x55,
    0x48, 0x8D, 0x68, 0xC8, 0x48, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00, 0x0F, 0x29, 0x70, 0xE8, 0x48, 0x8B, 0xF9};
constexpr std::uint8_t kCallFunctionPrologue[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48,
    0x89, 0x74, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0x01, 0x4D, 0x8B, 0xF1};
constexpr std::uint8_t kSetArrayPrologue[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x49, 0x8B, 0xF0, 0x49, 0x8B, 0xF9, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xD9};

struct Projected {
    float x, y;
    std::uint8_t onScreen;
};
struct Vec2f {
    float x, y;
};
using ProjectFn = Projected*(__fastcall*)(Projected* out, const Vec3* world, bool clamp);
using NormToCanvasFn = Vec2f*(__fastcall*)(Vec2f* out, const Vec2f* normalized);
using PreRenderFn = void(__fastcall*)(void* hud);
using CallFunctionFn = bool(__fastcall*)(void* element, const char* name, const void* args, void* result,
                                         const char* instance);
using SetArrayFn = bool(__fastcall*)(void* element, const char* name, const void* values, const char* instance);

std::mutex gInstallMutex;
bool gInstallTried = false, gInstalled = false;
std::uintptr_t gBase = 0;
void* gProjectOriginal = nullptr;
void* gToCanvasOriginal = nullptr;
void* gNormToCanvasOriginal = nullptr;
void* gPreRenderOriginal = nullptr;
void* gCallFunctionOriginal = nullptr;
void* gSetArrayOriginal = nullptr;

// The last projection on this thread, as the canvas conversion that follows it needs it.
struct LastProjection {
    float nx = 0, ny = 0;
    Vec3 view{};
    markers::HudScale scale{};
    bool valid = false;
};
thread_local LastProjection tLast;
// Projections waiting for the call that names their marker, this frame.
struct Pending {
    float x, y, perMetre;
    bool used;
};
constexpr std::size_t kMaxPending = 96;
thread_local std::array<Pending, kMaxPending> tPending{};
thread_local std::size_t tPendingCount = 0;
// Canvas width of one normalized unit, from Prey's own conversion once a frame.
thread_local float tCanvasPerUnit = 0;
thread_local bool tCanvasPerUnitFresh = false;

std::atomic<unsigned long long> gProjections{0}, gPendingFull{0}, gNamed{0}, gUnmatched{0}, gBadArgs{0};

bool IsMarkersElement(const void* element)
{
    const char* name = nullptr;
    char text[16]{};
    return element != nullptr && SafeRead(static_cast<const std::uint8_t*>(element) + 0x30, name) &&
           name != nullptr && SafeCopy(text, name, sizeof(text)) && std::memcmp(text, "DanielleMarkers", 16) == 0;
}

// Canvas units per normalized unit: Prey's own conversion of 1 minus that of 0.
// Only called right after a conversion of Prey's succeeded, so the markers
// element exists (0x16657C0 does not check for it).
bool RefreshCanvasPerUnit()
{
    if (tCanvasPerUnitFresh) { return tCanvasPerUnit > 0; }
    tCanvasPerUnitFresh = true;
    const auto convert = reinterpret_cast<NormToCanvasFn>(gNormToCanvasOriginal);
    Vec2f zero{0, 0}, one{1, 0}, at0{}, at1{};
    convert(&at0, &zero);
    convert(&at1, &one);
    const float a = at1.x - at0.x;
    tCanvasPerUnit = std::isfinite(a) && a > 0 ? a : 0;
    return tCanvasPerUnit > 0;
}

// The projection just converted to canvas (x, y): kept with its eye shift.
void KeepProjection(float x, float y)
{
    if (!tLast.valid || !RefreshCanvasPerUnit()) { return; }
    float depth = 0;
    if (!markers::DisplayedDepth(tLast.scale, tLast.view, tLast.nx, tLast.ny, depth)) { return; }
    if (tPendingCount == kMaxPending) {
        gPendingFull.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    tPending[tPendingCount++] = {x, y, markers::CanvasShiftPerMetre(tLast.scale, depth, tCanvasPerUnit), false};
    gProjections.fetch_add(1, std::memory_order_relaxed);
}

Projected* __fastcall ProjectDetour(Projected* out, const Vec3* world, bool clamp)
{
    Projected* result = reinterpret_cast<ProjectFn>(gProjectOriginal)(out, world, clamp);
    tLast.valid = false;
    if (!Recording() || result == nullptr || world == nullptr) { return result; }
    markers::HudMatrix m{};
    Vec3 point{};
    if (SafeCopy(m.data(), reinterpret_cast<const void*>(gBase + kHudMatrixRva), sizeof(m)) &&
        SafeRead(world, point) && markers::ScaleOf(m, tLast.scale)) {
        tLast.nx = result->x;
        tLast.ny = result->y;
        tLast.view = markers::ViewPoint(m, tLast.scale, point);
        tLast.valid = true;
    }
    return result;
}

Projected* __fastcall ToCanvasDetour(Projected* out, const Vec3* world, bool clamp)
{
    tLast.valid = false;
    Projected* result = reinterpret_cast<ProjectFn>(gToCanvasOriginal)(out, world, clamp);  // projects inside
    if (result != nullptr) { KeepProjection(result->x, result->y); }
    tLast.valid = false;
    return result;
}

Vec2f* __fastcall NormToCanvasDetour(Vec2f* out, const Vec2f* normalized)
{
    Vec2f in{};
    const bool read = normalized != nullptr && SafeRead(normalized, in);
    Vec2f* result = reinterpret_cast<NormToCanvasFn>(gNormToCanvasOriginal)(out, normalized);
    // Only the conversion of the projection just made (the awareness markers').
    if (result != nullptr && read && tLast.valid && in.x == tLast.nx && in.y == tLast.ny) {
        KeepProjection(result->x, result->y);
    }
    tLast.valid = false;
    return result;
}

void __fastcall PreRenderDetour(void* hud)
{
    tPendingCount = 0;
    tCanvasPerUnitFresh = false;
    tLast.valid = false;
    reinterpret_cast<PreRenderFn>(gPreRenderOriginal)(hud);
}

// --- reading the call's arguments: SUIArguments / TUIData (static, 2026-10-21) ---
// SUIArguments +0x00: DynArray<TUIData> elements; count at elements-4, bit 31 a
// capacity flag. TUIData, 0x20 apart: +0x00 type (0 int, 1 float, 2 EntityId,
// 4 string, ...), +0x10 the value (a string's is a pointer to its characters).
constexpr int kTypeInt = 0, kTypeFloat = 1, kTypeEntityId = 2, kTypeString = 4;
constexpr std::size_t kArgStride = 0x20, kArgValue = 0x10;

struct Args {
    const std::uint8_t* first = nullptr;
    std::uint32_t count = 0;
};
bool ReadArgs(const void* arguments, Args& out)
{
    const std::uint8_t* first = nullptr;
    std::uint32_t header = 0;
    if (arguments == nullptr || !SafeRead(arguments, first) || first == nullptr || !SafeRead(first - 4, header)) {
        return false;
    }
    out = {first, header & 0x7FFFFFFFu};
    return out.count <= 4096;
}
bool ArgFloat(const Args& args, std::uint32_t i, float& value)
{
    int type = -1;
    return i < args.count && SafeRead(args.first + i * kArgStride, type) && type == kTypeFloat &&
           SafeRead(args.first + i * kArgStride + kArgValue, value) && std::isfinite(value);
}
// The ID as the movie names the clip: an int or an EntityId becomes a Number
// (read signed) and so its decimal text; a string is the clip name itself.
bool ArgName(const Args& args, std::uint32_t i, char (&name)[48])
{
    int type = -1;
    if (i >= args.count || !SafeRead(args.first + i * kArgStride, type)) { return false; }
    const std::uint8_t* value = args.first + i * kArgStride + kArgValue;
    if (type == kTypeInt || type == kTypeEntityId) {
        std::int32_t id = 0;
        return SafeRead(value, id) && std::snprintf(name, sizeof(name), "%d", id) > 0;
    }
    if (type != kTypeString) { return false; }
    const char* text = nullptr;
    char copy[sizeof(name)]{};
    if (!SafeRead(value, text) || text == nullptr || !SafeCopy(copy, text, sizeof(copy) - 1)) { return false; }
    const std::size_t length = strnlen(copy, sizeof(copy) - 1);
    if (length == 0 || length == sizeof(copy) - 1) { return false; }
    std::memcpy(name, copy, length + 1);
    return true;
}

// The pending projection with this canvas position: the newest for a single
// call (it was projected just before), the oldest for an array (projected in
// its order). Two markers pinned to the same edge point stay apart that way.
const Pending* Claim(float x, float y, bool newest)
{
    for (std::size_t k = 0; k < tPendingCount; ++k) {
        Pending& p = tPending[newest ? tPendingCount - 1 - k : k];
        if (!p.used && p.x == x && p.y == y) {
            p.used = true;
            return &p;
        }
    }
    return nullptr;
}

void Name(const char* container, const char (&id)[48], const Pending* pending, std::uint64_t now)
{
    if (pending == nullptr) {
        gUnmatched.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    char clip[kPathSize];
    if (std::snprintf(clip, sizeof(clip), "%s.%s", container, id) > 0) {
        NoteMarker(clip, pending->perMetre, now);
        gNamed.fetch_add(1, std::memory_order_relaxed);
    }
}

// The movie's functions that place a marker: (ID, X, Y) at these argument
// positions, the clip under `container`.
struct Placer {
    const char* function;
    const char* container;
    std::uint32_t id, x, y;
};
constexpr Placer kPlacers[] = {
    {"updateObjectiveMarkerPosition", "_root.ar_mc.objectives", 0, 1, 2},
    {"repairUpdate", "_root.ar_mc.ar", 0, 1, 2},
    {"repairAdd", "_root.ar_mc.ar", 0, 1, 2},
    {"dangerUpdate", "_root.ar_mc.ar", 0, 1, 2},
    {"dangerAdd", "_root.ar_mc.ar", 0, 1, 2},
    {"targetUpdate", "_root.ar_mc.ar", 0, 1, 2},
    {"targetAdd", "_root.ar_mc.ar", 0, 2, 3},
};
// Arrays of (ID, X, Y, ...) groups.
struct ArrayPlacer {
    const char* array;
    const char* container;
    std::uint32_t stride;
};
constexpr ArrayPlacer kArrayPlacers[] = {
    {"markerUpdateArray", "_root.ar_mc.ar.markersAttach", 11},
    {"poiPositions", "_root.ar_mc.objectives.poiAttach", 3},
};

bool __fastcall CallFunctionDetour(void* element, const char* name, const void* args, void* result,
                                   const char* instance)
{
    if (tPendingCount != 0 && name != nullptr && Recording() &&
        IsMarkersElement(element)) {
        for (const Placer& placer : kPlacers) {
            if (std::strcmp(name, placer.function) != 0) { continue; }
            Args a{};
            char id[48]{};
            float x = 0, y = 0;
            if (ReadArgs(args, a) && ArgName(a, placer.id, id) && ArgFloat(a, placer.x, x) && ArgFloat(a, placer.y, y)) {
                Name(placer.container, id, Claim(x, y, true), MonotonicNanoseconds());
            } else {
                gBadArgs.fetch_add(1, std::memory_order_relaxed);
            }
            break;
        }
    }
    return reinterpret_cast<CallFunctionFn>(gCallFunctionOriginal)(element, name, args, result, instance);
}

bool __fastcall SetArrayDetour(void* element, const char* name, const void* values, const char* instance)
{
    if (tPendingCount != 0 && name != nullptr && Recording() &&
        IsMarkersElement(element)) {
        for (const ArrayPlacer& placer : kArrayPlacers) {
            if (std::strcmp(name, placer.array) != 0) { continue; }
            Args a{};
            if (!ReadArgs(values, a) || a.count % placer.stride != 0) {
                gBadArgs.fetch_add(1, std::memory_order_relaxed);
                break;
            }
            const std::uint64_t now = MonotonicNanoseconds();
            for (std::uint32_t group = 0; group < a.count; group += placer.stride) {
                char id[48]{};
                float x = 0, y = 0;
                if (ArgName(a, group, id) && ArgFloat(a, group + 1, x) && ArgFloat(a, group + 2, y)) {
                    Name(placer.container, id, Claim(x, y, false), now);
                } else {
                    gBadArgs.fetch_add(1, std::memory_order_relaxed);
                }
            }
            break;
        }
    }
    return reinterpret_cast<SetArrayFn>(gSetArrayOriginal)(element, name, values, instance);
}

bool Install()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstallTried) { return gInstalled; }
    gInstallTried = true;
    gBase = PreyBase();
    if (gBase == 0) {
        Log("result=unavailable detail=no_preydll");
        return false;
    }
    struct Target {
        std::uint32_t rva;
        std::span<const std::uint8_t> prologue;
        void* detour;
        void** original;
        const char* name;
    };
    const Target targets[] = {
        {kProjectRva, kProjectPrologue, reinterpret_cast<void*>(&ProjectDetour), &gProjectOriginal, "project"},
        {kToCanvasRva, kToCanvasPrologue, reinterpret_cast<void*>(&ToCanvasDetour), &gToCanvasOriginal, "to_canvas"},
        {kNormToCanvasRva, kNormToCanvasPrologue, reinterpret_cast<void*>(&NormToCanvasDetour),
         &gNormToCanvasOriginal, "norm_to_canvas"},
        {kPreRenderRva, kPreRenderPrologue, reinterpret_cast<void*>(&PreRenderDetour), &gPreRenderOriginal,
         "hud_pre_render"},
        {kCallFunctionRva, kCallFunctionPrologue, reinterpret_cast<void*>(&CallFunctionDetour),
         &gCallFunctionOriginal, "ui_call_function"},
        {kSetArrayRva, kSetArrayPrologue, reinterpret_cast<void*>(&SetArrayDetour), &gSetArrayOriginal,
         "ui_set_array"},
    };
    std::array<hook::Spec, std::size(targets)> specs{};
    for (std::size_t i = 0; i < std::size(targets); ++i) {
        void* at = reinterpret_cast<void*>(gBase + targets[i].rva);
        if (!IsExecutableAddress(at) || !SafeEqual(at, targets[i].prologue.data(), targets[i].prologue.size())) {
            Log(std::string("result=unavailable detail=prologue_mismatch target=") + targets[i].name);
            return false;
        }
        specs[i] = {at, targets[i].detour, targets[i].original};
    }
    const hook::Result result = hook::InstallAll(specs);
    if (!result) {
        Log(std::string("result=failed detail=") + hook::StepName(result.step) + " target=" +
            targets[result.index < std::size(targets) ? result.index : 0].name);
        return false;
    }
    gInstalled = true;
    Log("result=0 detail=hooks_installed");
    return true;
}

// --- the render side ----------------------------------------------------------

struct Moved {
    char clip[kPathSize];
    double x, xscale, yscale;
    float perMetre;
    bool scaled;
};
// Begin to Restore happen inside one display callback, on one thread.
thread_local std::array<Moved, kMaxMarkers> tMoved{};
thread_local std::size_t tMovedCount = 0;
thread_local float tHalfIpd = 0;
thread_local bool tShift = false;

// "<clip>.<property>", or false if it does not fit.
bool PropertyPath(char (&out)[kPathSize + 16], const char* clip, const char* property)
{
    const int written = std::snprintf(out, sizeof(out), "%s.%s", clip, property);
    return written > 0 && static_cast<std::size_t>(written) < sizeof(out);
}

std::atomic<unsigned long long> gFrames{0}, gMoved{0}, gUnresolved{0}, gRestoreFailed{0};
std::atomic<int> gLastCount{0};
std::atomic<bool> gLoggedUnresolved{false}, gLoggedMoved{false};
std::atomic<int> gLastShiftCentipx{0};

} // namespace

DWORD SetMarkerDepth(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gEnabled.store(enabled != 0, std::memory_order_release);
    if (enabled != 0 && !Install()) { return ERROR_NOT_SUPPORTED; }
    Log(std::string("result=0 detail=enabled value=") + (enabled ? "1" : "0"));
    return 0;
}

unsigned MarkerDepthEnabled() { return gEnabled.load(std::memory_order_acquire) ? 1u : 0u; }

void EnsureMarkerDepth()
{
    // PREYVR_MARKER_DEPTH=0: leave the markers untouched, not even hooked.
    wchar_t value[8]{};
    if (GetEnvironmentVariableW(L"PREYVR_MARKER_DEPTH", value, 8) > 0 && value[0] == L'0') {
        gEnabled.store(false, std::memory_order_release);
        Log("result=0 detail=disabled_by_environment hooks=0");
        return;
    }
    if (gEnabled.load(std::memory_order_acquire)) { Install(); }
}

DWORD SetMarkerSizePercent(unsigned percent)
{
    if (percent < 50 || percent > 200) { return ERROR_INVALID_PARAMETER; }
    if (gSizePercent.exchange(percent, std::memory_order_acq_rel) != percent) {
        Log("result=0 detail=size percent=" + std::to_string(percent));
    }
    return 0;
}

unsigned MarkerSizePercent() { return gSizePercent.load(std::memory_order_acquire); }

void BeginMarkerEyes(void* root)
{
    tMovedCount = 0;
    tHalfIpd = 0.5f * SyntheticStereoIpdMetres();
    tShift = gEnabled.load(std::memory_order_acquire) && tHalfIpd > 0.0f;
    const unsigned size = gSizePercent.load(std::memory_order_acquire);
    if (!(tShift || size != 100) || root == nullptr) {
        return;
    }
    std::array<Entry, kMaxMarkers> live;
    std::size_t count = 0;
    {
        const std::uint64_t now = MonotonicNanoseconds();
        std::lock_guard lock(gTableMutex);
        for (std::size_t i = 0; i < gTableCount; ++i) {
            if (now - gTable[i].stamp <= kExpiryNs) { live[count++] = gTable[i]; }
        }
    }
    char path[kPathSize + 16];
    for (std::size_t i = 0; i < count; ++i) {
        Moved& m = tMoved[tMovedCount];
        if (!PropertyPath(path, live[i].path, "_x") || !HudMovieNumber(root, path, m.x)) {
            // A clip just removed, or a path the movie does not resolve: the
            // first is named once, so a run says which.
            gUnresolved.fetch_add(1, std::memory_order_relaxed);
            if (!gLoggedUnresolved.exchange(true)) { Log(std::string("first_unresolved path=") + live[i].path); }
            continue;
        }
        if (!gLoggedMoved.exchange(true)) { Log(std::string("first_moved path=") + live[i].path); }
        std::memcpy(m.clip, live[i].path, sizeof(m.clip));
        m.perMetre = live[i].perMetre;
        // Marker Size: the whole marker, icon and label, about the point it
        // marks (the clip's origin), the same in both eyes.
        m.scaled = false;
        char yPath[kPathSize + 16];
        if (size != 100 && PropertyPath(path, m.clip, "_xscale") && PropertyPath(yPath, m.clip, "_yscale") &&
            HudMovieNumber(root, path, m.xscale) && HudMovieNumber(root, yPath, m.yscale)) {
            const double factor = size / 100.0;
            m.scaled = true;  // restored whole even if one of the two writes refuses
            SetHudMovieNumber(root, path, m.xscale * factor);
            SetHudMovieNumber(root, yPath, m.yscale * factor);
        }
        ++tMovedCount;
    }
    gFrames.fetch_add(1, std::memory_order_relaxed);
    gMoved.fetch_add(tMovedCount, std::memory_order_relaxed);
    gLastCount.store(static_cast<int>(tMovedCount), std::memory_order_relaxed);
    if (tMovedCount != 0) {
        gLastShiftCentipx.store(static_cast<int>(tMoved[0].perMetre * tHalfIpd * 100.0f), std::memory_order_relaxed);
    }
}

void ApplyMarkerEye(void* root, int eye)
{
    // The eye camera is the head moved half the IPD along its right axis:
    // left (the first submission) negative, right positive.
    if (!tShift) { return; }
    const float offset = eye == 0 ? -tHalfIpd : tHalfIpd;
    char path[kPathSize + 16];
    for (std::size_t i = 0; i < tMovedCount; ++i) {
        if (PropertyPath(path, tMoved[i].clip, "_x")) {
            SetHudMovieNumber(root, path, tMoved[i].x + static_cast<double>(tMoved[i].perMetre * offset));
        }
    }
}

void RestoreMarkerEyes(void* root)
{
    char path[kPathSize + 16];
    for (std::size_t i = 0; i < tMovedCount; ++i) {
        const Moved& m = tMoved[i];
        bool restored = !tShift || (PropertyPath(path, m.clip, "_x") && SetHudMovieNumber(root, path, m.x));
        if (m.scaled) {
            restored = PropertyPath(path, m.clip, "_xscale") && SetHudMovieNumber(root, path, m.xscale) && restored;
            restored = PropertyPath(path, m.clip, "_yscale") && SetHudMovieNumber(root, path, m.yscale) && restored;
        }
        if (!restored) { gRestoreFailed.fetch_add(1, std::memory_order_relaxed); }
    }
    tMovedCount = 0;
}

std::string MarkerDepthReport()
{
    std::size_t table = 0;
    {
        std::lock_guard lock(gTableMutex);
        table = gTableCount;
    }
    std::ostringstream s;
    s << "markers={depth=" << MarkerDepthEnabled() << " size=" << MarkerSizePercent() << " hooks=" << (gInstalled ? 1 : 0)
      << " projections=" << gProjections.load() << " named=" << gNamed.load() << " unmatched=" << gUnmatched.load()
      << " badArgs=" << gBadArgs.load() << " pendingFull=" << gPendingFull.load() << " noted=" << gNoted.load()
      << " table=" << table << " full=" << gTableFull.load() << " frames=" << gFrames.load()
      << " moved=" << gMoved.load() << " last=" << gLastCount.load() << " unresolved=" << gUnresolved.load()
      << " restoreFailed=" << gRestoreFailed.load() << " lastShiftCentipx=" << gLastShiftCentipx.load() << '}';
    return s.str();
}

} // namespace preyvr::dll
