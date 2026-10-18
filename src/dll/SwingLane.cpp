#include "SwingLane.h"
#include "AimTakeover.h"
#include "BodyFollow.h"
#include "CarryLane.h"
#include "DebugOverlay.h"
#include "EngineAccess.h"
#include "Haptics.h"
#include "HeadTrackingHook.h"
#include "HookInstall.h"
#include "HudBridge.h"
#include "InputPost.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "PhysicalInteractions.h"
#include "SceneQuery.h"
#include "preyvr/ContactNative.h"
#include "preyvr/ContactQuery.h"
#include "preyvr/MonotonicClock.h"
#include "preyvr/NativeContract.h"
#include "preyvr/PhysicalInteractions.h"
#include "preyvr/PhysicalNative.h"
#include "preyvr/SwingNative.h"
#include "preyvr/SwingStrike.h"
#include "preyvr/TextFormat.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstring>
#include <iterator>

namespace preyvr::dll {
namespace {
namespace cn = contact::native;
namespace sn = swing::native;
namespace pn = physical::native;
using contact::Box;
using swing::Decision;
constexpr unsigned kMaxEntities = 64, kMaxTouches = 48, kLogLinesPerSecond = 40;
constexpr std::uint64_t kFreshNs = 100000000ull;

// The option (Free-Swing Wrench, on by default) and Minimum Swing Speed (cm/s).
std::atomic<bool> enabled{true}, fault{false}, eventLog{true}, installed{false};
std::atomic<unsigned> minimumCms{120};
std::atomic<std::uint64_t> settingsGeneration{1};
std::atomic<std::uint64_t> failed{0}, attacksBlocked{0}, substituted{0}, impulsesTurned{0}, collisionsTurned{0}, handoffs{0};
std::atomic<std::uint64_t> frames{0}, queries{0}, refused{0}, overflows{0}, entities{0}, entitiesMax{0};
std::atomic<std::uint64_t> statics{0}, rigids{0}, bodies{0}, others{0}, touches{0}, headTouches{0}, begins{0};
std::atomic<std::uint64_t> strikes{0}, heavies{0}, tooSlow{0}, locked{0}, drained{0}, blocked{0}, sightUnknown{0};
std::atomic<std::uint64_t> swings{0}, dropped{0}, usTotal{0}, usMax{0}, selfSkipped{0};
std::atomic<DWORD> error{0};
std::atomic<const char*> lastGate{"none"};

// --- the engine's calls ---------------------------------------------------------
using EntitiesInBox = int(__fastcall*)(std::uintptr_t, const Vec3*, const Vec3*, void***, int, int);
using Collide = int(__fastcall*)(std::uintptr_t, void*, int, void*, const Vec3*, scene::Hit*, void*);
using SurfaceTypeOf = void*(__fastcall*)(void*, int, const char*);
// The strike's calls (SwingNative.h): the component's hit (RCX component, XMM1
// angle, R8 weapon, XMM3 scale, a byte on the stack: charged), its fatigue setter
// (RCX component, DL charged, XMM2 the charge bonus) and Prey's stamina check.
using ComponentHit = int(__fastcall*)(std::uintptr_t, float, std::uintptr_t, float, bool);
using FatigueSetter = void(__fastcall*)(std::uintptr_t, bool, float);
using StaminaCheck = bool(__fastcall*)(void*);
struct Natives {
    std::uintptr_t base = 0, world = 0, articulated = 0, wrench = 0, surfaceTypeVtable = 0;
    void* player = nullptr;            // the player's own physics: never tested
    void* carried = nullptr;           // what the left hand carries: never tested
    std::uintptr_t playerObject = 0;   // the player (frame.player), for stamina
    EntitiesInBox entitiesInBox = nullptr;
    Collide collide = nullptr;
    void* entitySystem = nullptr;      // for names; null when unreadable
    void* surfaces = nullptr;          // CSurfaceTypeManager, when its chain checks out
    SurfaceTypeOf surfaceType = nullptr;
    ComponentHit componentHit = nullptr;
    FatigueSetter setFatigue = nullptr;
    StaminaCheck staminaAvailable = nullptr;
};

bool EngineThread() { return InputDrainThreadId() != 0 && InputDrainThreadId() == GetCurrentThreadId(); }
void Log(const std::string& line) { lifecycle::Log("preyvr_swing " + line); }

// Every function called or hooked through a named RVA is one the gate hashes.
static_assert(contract::Gated(sn::Contracts, sn::GetSurfaceType) && contract::Gated(cn::Contracts, cn::GetEntitiesInBox) &&
              contract::Gated(cn::Contracts, cn::CollideEntityWithPrimitive) && contract::Gated(pn::Contracts, sn::ComponentHit) &&
              contract::Gated(sn::Contracts, sn::FatigueSetter) && contract::Gated(sn::Contracts, sn::StaminaAvailable) &&
              contract::Gated(sn::Contracts, sn::CollisionEventBuilder) && contract::Gated(pn::Contracts, pn::Attack) &&
              contract::Gated(pn::Contracts, pn::GetHits) && contract::Gated(pn::Contracts, pn::AppendHit) &&
              contract::Gated(pn::Contracts, pn::WeaponImpulse));
bool GateBodies(std::uintptr_t base)
{
    __try {
        return contract::BodiesMatch(base, sn::Contracts) && contract::BodiesMatch(base, cn::Contracts) &&
               contract::BodiesMatch(base, pn::Contracts);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// What names things for the log (SwingNative.h): the entity system, and the
// surface type manager behind the material manager, each vtable checked. Either
// can be missing early in a load; then the log says so and nothing else changes.
void FindNamers(std::uintptr_t base, Natives& n)
{
    __try {
        n.entitySystem = *reinterpret_cast<void* const*>(base + sn::EntitySystem);
        const auto matman = *reinterpret_cast<const std::uintptr_t*>(base + sn::MaterialManager);
        if (!matman || *reinterpret_cast<const std::uintptr_t*>(matman) != base + sn::MatManVtable) { return; }
        const auto types = *reinterpret_cast<const std::uintptr_t*>(matman + sn::MatManSurfaceTypes);
        if (!types || *reinterpret_cast<const std::uintptr_t*>(types) != base + sn::SurfaceTypeManagerVtable) { return; }
        n.surfaces = reinterpret_cast<void*>(types);
        n.surfaceType = reinterpret_cast<SurfaceTypeOf>(base + sn::GetSurfaceType);
        n.surfaceTypeVtable = base + sn::SurfaceTypeVtable;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        n.entitySystem = nullptr;
        n.surfaces = nullptr;
    }
}
// The hooks' view of the engine (they get no Natives): the hit append and the wrench's vtable.
using AppendHit = void(__fastcall*)(void*, const scene::Hit*);
std::atomic<AppendHit> append{nullptr};
std::atomic<std::uintptr_t> wrenchTable{0};
bool gated = false;
DWORD Resolve(const GameplayPoseFrame& frame, Natives& n)
{
    n = {};
    std::uintptr_t world = 0;
    void* player = nullptr;
    if (const DWORD ready = SceneQueryWorld(frame, world, player)) { return ready; }
    const auto base = PreyBase();
    if (!gated) {
        if (!GateBodies(base)) { return ERROR_BAD_EXE_FORMAT; }
        gated = true;
    }
    n.base = base;
    n.world = world;
    n.player = player;
    n.carried = reinterpret_cast<void*>(CarriedPhysics());
    n.playerObject = frame.player;
    n.articulated = base + cn::ArticulatedVtable;
    n.wrench = base + physical::native::WrenchVtable;
    n.entitiesInBox = reinterpret_cast<EntitiesInBox>(base + cn::GetEntitiesInBox);
    n.collide = reinterpret_cast<Collide>(base + cn::CollideEntityWithPrimitive);
    n.componentHit = reinterpret_cast<ComponentHit>(base + sn::ComponentHit);
    n.setFatigue = reinterpret_cast<FatigueSetter>(base + sn::FatigueSetter);
    n.staminaAvailable = reinterpret_cast<StaminaCheck>(base + sn::StaminaAvailable);
    append = reinterpret_cast<AppendHit>(base + pn::AppendHit);
    wrenchTable = n.wrench;
    FindNamers(base, n);
    return ERROR_SUCCESS;
}
// Tests replace the engine here.
DWORD (*gResolve)(const GameplayPoseFrame&, Natives&) = Resolve;

// --- the query ------------------------------------------------------------------
// Overlap: the box at this pose, still. Sweep: last pose's box moved along its
// centre's path. Head: the head's own sweep, for a flick that turns it farther.
enum class Kind : std::uint8_t { overlap, sweep, head };
const char* KindName(Kind k) { return k == Kind::sweep ? "sweep" : k == Kind::head ? "head" : "overlap"; }
struct Touch {
    void* collider = nullptr;
    int type = 0;
    Kind kind = Kind::overlap;
    scene::Hit hit{};
};
struct QueryStats { unsigned entities = 0, statics = 0, rigids = 0, bodies = 0, other = 0, self = 0; DWORD error = 0; };
// A virtual function of an engine object, by its vtable slot's byte offset.
template <class Fn> Fn Virtual(const void* object, std::size_t slot)
{
    return reinterpret_cast<Fn>((*reinterpret_cast<void* const* const*>(object))[slot / sizeof(void*)]);
}
// GetType through the IPhysicalEntity interface, which every class answers.
int TypeOf(void* entity)
{
    const auto table = *reinterpret_cast<void* const* const*>(entity);
    return reinterpret_cast<int(__fastcall*)(void*)>(table[cn::TypeSlot / sizeof(void*)])(entity);
}
// The player's own character: a body whose host is the player's physics, or whose
// entity is the player (SwingNative.h PlayerEntityId). The first one found is
// noted, with which test caught it.
struct Self { const void* body = nullptr; bool byHost = false, byEntity = false; };
Self firstSelf{};
bool IsPlayerBody(const Natives& n, void* entity)
{
    const void* host = nullptr;
    if (*reinterpret_cast<const std::uintptr_t*>(entity) == n.articulated) {
        std::memcpy(&host, static_cast<const std::uint8_t*>(entity) + cn::ArticulatedHost, sizeof(host));
    }
    bool byEntity = false;
    if (n.entitySystem) {
        void* const e = Virtual<void*(__fastcall*)(void*, void*)>(n.entitySystem, sn::EntityFromPhysicsSlot)(n.entitySystem, entity);
        byEntity = e && Virtual<unsigned(__fastcall*)(void*)>(e, sn::EntityIdSlot)(e) == sn::PlayerEntityId;
    }
    const bool byHost = host && host == n.player;
    if ((byHost || byEntity) && !firstSelf.body) { firstSelf = {entity, byHost, byEntity}; }
    return byHost || byEntity;
}
// The broadphase over both poses, asking for what Prey's melee asks for, then the
// box against each scenery, prop and body object. What the player is and what the
// hand carries are never tested. Faults propagate to the caller's __try.
unsigned Query(const Natives& n, const Box& now, const Box* before, const Box& head, const Box* headBefore, Touch* out,
               unsigned capacity, QueryStats& s)
{
    s = {};
    Vec3 lo{}, hi{}, headLo{}, headHi{};
    contact::Bounds(before ? *before : now, now, .02f, lo, hi);
    contact::Bounds(headBefore ? *headBefore : head, head, .02f, headLo, headHi);
    lo = {std::min(lo.x, headLo.x), std::min(lo.y, headLo.y), std::min(lo.z, headLo.z)};
    hi = {std::max(hi.x, headHi.x), std::max(hi.y, headHi.y), std::max(hi.z, headHi.z)};
    void* list[kMaxEntities]{};
    void** listed = list;
    const int count = n.entitiesInBox(n.world, &lo, &hi, &listed, sn::StrikeObjects, static_cast<int>(kMaxEntities));
    // More than our buffer holds: the engine hands back its shared buffer. Refuse it.
    if (count < 0 || count > static_cast<int>(kMaxEntities) || listed != list) { s.error = ERROR_BUFFER_OVERFLOW; return 0; }
    s.entities = static_cast<unsigned>(count);
    const Vec3 still{};
    const Vec3 sweep = before ? Sub(now.centre, before->centre) : Vec3{};
    const Vec3 flick = headBefore ? Sub(head.centre, headBefore->centre) : Vec3{};
    unsigned written = 0;
    auto test = [&](void* entity, int type, const Box& box, const Vec3& motion, Kind kind) {
        if (written >= capacity) { return; }
        contact::BoxPrimitive primitive = contact::ToPrimitive(box);   // per call: the engine edits it
        scene::Hit hit{};
        if (n.collide(n.world, entity, cn::PrimitiveBox, &primitive, &motion, &hit, nullptr) && contact::Valid(hit, entity)) {
            out[written++] = {entity, type, kind, hit};
        }
    };
    for (int i = 0; i < count; ++i) {
        void* const entity = list[i];
        if (!entity || entity == n.player || entity == n.carried) { continue; }
        const int type = TypeOf(entity);
        if (type == cn::TypeStatic) { ++s.statics; }
        else if (type == cn::TypeRigid) { ++s.rigids; }
        else if (type == cn::TypeArticulated) {
            if (IsPlayerBody(n, entity)) { ++s.self; continue; }
            ++s.bodies;
        } else { ++s.other; continue; }
        test(entity, type, now, still, Kind::overlap);
        if (before && Length(sweep) >= .001f) { test(entity, type, *before, sweep, Kind::sweep); }
        if (headBefore && Length(flick) >= .001f) { test(entity, type, *headBefore, flick, Kind::head); }
    }
    return written;
}
DWORD SafeQuery(const Natives& n, const Box& now, const Box* before, const Box& head, const Box* headBefore, Touch* out,
                unsigned capacity, unsigned& written, QueryStats& s)
{
    __try {
        written = Query(n, now, before, head, headBefore, out, capacity, s);
        return s.error;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        written = 0;
        return GetExceptionCode();
    }
}
// One touch per object: the first surface the motion reached (the earlier sweep,
// the body's or the head's), else where the box sits in it.
unsigned PickPerObject(const Touch* all, unsigned count, const Touch** picked, unsigned capacity)
{
    unsigned n = 0;
    for (unsigned i = 0; i < count; ++i) {
        const Touch& t = all[i];
        unsigned j = 0;
        while (j < n && picked[j]->collider != t.collider) { ++j; }
        if (j == n) {
            if (n < capacity) { picked[n++] = &t; }
            continue;
        }
        const Touch& had = *picked[j];
        const bool moving = t.kind != Kind::overlap, hadMoving = had.kind != Kind::overlap;
        if ((moving && !hadMoving) || (moving && hadMoving && t.hit.distance < had.hit.distance)) { picked[j] = &t; }
    }
    return n;
}

// --- naming things, for the log -----------------------------------------------------
void CopyText(const char* text, char* out, std::size_t size)   // inside the caller's __try
{
    std::size_t i = 0;
    for (; text && i + 1 < size && text[i]; ++i) { out[i] = text[i] == ' ' ? '_' : text[i]; }
    out[i] = 0;
}
struct Described {
    std::uintptr_t vtable = 0;
    int foreign = INT_MIN;   // the physical entity's foreign-data kind; INT_MIN unread
    bool entity = false, hostRead = false, faulted = false;
    const void* host = nullptr;
    unsigned id = 0;
    char className[48]{}, name[48]{};
};
void Describe(const Natives& n, void* collider, int type, Described& d)
{
    __try {
        d.vtable = *reinterpret_cast<const std::uintptr_t*>(collider);
        d.foreign = Virtual<int(__fastcall*)(void*)>(collider, sn::ForeignKindSlot)(collider);
        if (type == cn::TypeArticulated && d.vtable == n.articulated) {
            std::memcpy(&d.host, static_cast<const std::uint8_t*>(collider) + cn::ArticulatedHost, sizeof(d.host));
            d.hostRead = true;
        }
        if (!n.entitySystem) { return; }
        void* const entity = Virtual<void*(__fastcall*)(void*, void*)>(n.entitySystem, sn::EntityFromPhysicsSlot)(n.entitySystem, collider);
        if (!entity) { return; }
        d.entity = true;
        d.id = Virtual<unsigned(__fastcall*)(void*)>(entity, sn::EntityIdSlot)(entity);
        if (void* const cls = Virtual<void*(__fastcall*)(void*)>(entity, sn::EntityClassSlot)(entity)) {
            CopyText(Virtual<const char*(__fastcall*)(void*)>(cls, sn::ClassNameSlot)(cls), d.className, sizeof(d.className));
        }
        CopyText(Virtual<const char*(__fastcall*)(void*)>(entity, sn::EntityNameSlot)(entity), d.name, sizeof(d.name));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        d.faulted = true;
    }
}
// Surface types by id, as the hit's own breakability test finds them. Their names
// do not change, so each is looked up once.
struct SurfaceInfo { bool looked = false, found = false; int breakability = 0, flags = 0; char name[40]{}; };
std::array<SurfaceInfo, 256> surfaceCache{};
void LookUpSurface(const Natives& n, int id, SurfaceInfo& s)
{
    __try {
        auto* const type = static_cast<const std::uint8_t*>(n.surfaceType(n.surfaces, id, nullptr));
        if (!type || *reinterpret_cast<const std::uintptr_t*>(type) != n.surfaceTypeVtable) { return; }
        CopyText(*reinterpret_cast<const char* const*>(type + sn::SurfaceTypeName), s.name, sizeof(s.name));
        std::memcpy(&s.breakability, type + sn::SurfaceTypeBreakability, 4);
        std::memcpy(&s.flags, type + sn::SurfaceTypeFlags, 4);
        s.found = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s.found = false;
    }
}
const SurfaceInfo* Surface(const Natives& n, int id)
{
    if (id < 0 || id >= static_cast<int>(surfaceCache.size()) || !n.surfaces || !n.surfaceType) { return nullptr; }
    auto& s = surfaceCache[static_cast<std::size_t>(id)];
    if (!s.looked) {
        s = {};
        s.looked = true;
        LookUpSurface(n, id, s);
    }
    return &s;
}
std::string SurfaceText(const Natives& n, int id)
{
    const auto* s = Surface(n, id);
    if (!s) { return "surface=" + std::to_string(id) + " surfaceName=?"; }
    if (!s->found) { return "surface=" + std::to_string(id) + " surfaceName=none"; }
    return "surface=" + std::to_string(id) + " surfaceName=" + s->name + " breakable=" + std::to_string(s->breakability) +
           " surfaceFlags=" + Hex(static_cast<unsigned>(s->flags));
}

// --- the player's stamina and the wrench's own fields (SwingNative.h) --------------
struct Stamina { bool read = false, drained = false; float a = NAN, b = NAN; };
Stamina ReadStamina(const Natives& n)
{
    Stamina s{};
    __try {
        if (!n.playerObject) { return s; }
        const auto stamina = *reinterpret_cast<const std::uintptr_t*>(n.playerObject + sn::PlayerStamina + sn::StaminaPointer);
        if (!stamina) { return s; }
        std::memcpy(&s.a, reinterpret_cast<const void*>(stamina + sn::StaminaPair), 4);
        std::memcpy(&s.b, reinterpret_cast<const void*>(stamina + sn::StaminaPair + 4), 4);
        // As the check compares them (ucomiss, jne): equal or unordered is drained.
        s.drained = !(s.b < s.a || s.b > s.a);
        s.read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s = {};
    }
    return s;
}
struct WrenchFields {
    bool read = false, wrench = false;
    float chargeTime = NAN, chargeFull = NAN, chargeBonus = NAN, fatigue = NAN;
    std::uint8_t charged = 0, dodge = 0;
    std::uint64_t packages[4]{};
};
WrenchFields ReadWrench(const Natives& n, std::uintptr_t weapon)
{
    WrenchFields w{};
    __try {
        if (!weapon) { return w; }
        w.wrench = *reinterpret_cast<const std::uintptr_t*>(weapon) == n.wrench;
        if (!w.wrench) { return w; }
        const auto* p = reinterpret_cast<const std::uint8_t*>(weapon);
        std::memcpy(&w.chargeTime, p + sn::ChargeTime, 4);
        std::memcpy(&w.chargeFull, p + sn::ChargeFull, 4);
        std::memcpy(&w.chargeBonus, p + sn::ChargeBonus, 4);
        w.charged = p[sn::ChargedFlag];
        const auto* c = p + sn::ComponentOffset;
        std::memcpy(&w.packages[0], c + sn::PackageNormal, 8);
        std::memcpy(&w.packages[1], c + sn::PackageCritical, 8);
        std::memcpy(&w.packages[2], c + sn::PackageCharged, 8);
        std::memcpy(&w.packages[3], c + sn::PackageChargedCritical, 8);
        std::memcpy(&w.fatigue, c + sn::FatiguePerHit, 4);
        w.dodge = c[sn::DodgeFlag];
        w.read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        w.read = false;
    }
    return w;
}

// --- the strike (SwingNative.h) ----------------------------------------------------
// Prey's own hit, called without its attack: the component's hit, given the
// wrench's contact as its hits. Four hooks make it the wrench's own: GetHits
// answers with the contact; the impulse wrapper and the collision builder take
// the swing's direction; and Prey's attack, with the wrench out, does nothing
// (user decision 1).
struct HitVector { scene::Hit* begin = nullptr; scene::Hit* end = nullptr; scene::Hit* capacity = nullptr; };
using AttackFn = bool(__fastcall*)(std::uintptr_t, unsigned, const void*, int, float);
using GetHitsFn = HitVector*(__fastcall*)(std::uintptr_t, HitVector*, std::uintptr_t, float);
using ImpulseFn = void(__fastcall*)(void*, void*, const Vec3*, int, std::uintptr_t, float, float, float);
using BuilderFn = void(__fastcall*)(void*, void*, const Vec3*, void*, void*, float, float, bool, short);
std::atomic<AttackFn> originalAttack{nullptr};
std::atomic<GetHitsFn> originalGetHits{nullptr};
std::atomic<ImpulseFn> originalImpulse{nullptr};
std::atomic<BuilderFn> originalBuilder{nullptr};
// The strike in flight: set around the call to the hit on the input-drain thread,
// and read by the hooks the hit reaches on the same thread. Nothing else matches it.
struct Striking {
    bool active = false;
    std::uintptr_t weapon = 0;
    const void* entity = nullptr;   // the weapon's entity, as the hit hands it to the builder
    scene::Hit hit{};
    Vec3 direction{}, reversed{};
};
thread_local Striking striking{};
bool __fastcall AttackHook(std::uintptr_t weapon, unsigned id, const void* name, int activation, float value)
{
    const auto wrench = wrenchTable.load();
    if (enabled.load() && !fault.load() && weapon && wrench && *reinterpret_cast<const std::uintptr_t*>(weapon) == wrench) {
        ++attacksBlocked;
        return false;
    }
    return originalAttack.load()(weapon, id, name, activation, value);
}
HitVector* __fastcall GetHitsHook(std::uintptr_t component, HitVector* out, std::uintptr_t weapon, float angle)
{
    if (striking.active && weapon == striking.weapon && component == weapon + sn::ComponentOffset) {
        *out = {};
        // The hit frees the vector: allocate it through the engine's own append.
        append.load()(out, &striking.hit);
        ++substituted;
        return out;
    }
    return originalGetHits.load()(component, out, weapon, angle);
}
void __fastcall ImpulseHook(void* entity, void* collider, const Vec3* direction, int part, std::uintptr_t weapon, float force,
                            float minMass, float maxMass)
{
    // The wrapper negates what it is given, so it is given the swing's reverse.
    const bool ours = striking.active && weapon == striking.weapon;
    if (ours) { ++impulsesTurned; }
    originalImpulse.load()(entity, collider, ours ? &striking.reversed : direction, part, weapon, force, minMass, maxMass);
}
void __fastcall BuilderHook(void* event, void* material, const Vec3* direction, void* weapon, void* player, float mass, float radius,
                            bool dispatch, short surface)
{
    const bool ours = striking.active && weapon == striking.entity;
    if (ours) { ++collisionsTurned; }
    originalBuilder.load()(event, material, ours ? &striking.direction : direction, weapon, player, mass, radius, dispatch, surface);
}
// The hooks go in before the query, so no collider pointer is held while MinHook
// suspends threads. All four or none.
bool Install(const Natives& n)
{
    if (installed.load()) { return true; }
    if (!EnsureMinHook()) { return false; }
    void* originals[4]{};
    const hook::Spec hooks[] = {
        {reinterpret_cast<void*>(n.base + pn::Attack), reinterpret_cast<void*>(AttackHook), &originals[0]},
        {reinterpret_cast<void*>(n.base + pn::GetHits), reinterpret_cast<void*>(GetHitsHook), &originals[1]},
        {reinterpret_cast<void*>(n.base + pn::WeaponImpulse), reinterpret_cast<void*>(ImpulseHook), &originals[2]},
        {reinterpret_cast<void*>(n.base + sn::CollisionEventBuilder), reinterpret_cast<void*>(BuilderHook), &originals[3]}};
    const auto result = hook::InstallAll(hooks, [&] {
        originalAttack = reinterpret_cast<AttackFn>(originals[0]);
        originalGetHits = reinterpret_cast<GetHitsFn>(originals[1]);
        originalImpulse = reinterpret_cast<ImpulseFn>(originals[2]);
        originalBuilder = reinterpret_cast<BuilderFn>(originals[3]);
    });
    if (!result) {
        Log(std::string("hooks result=") + hook::StepName(result.step) + " status=" + std::to_string(result.status) +
            " index=" + std::to_string(result.index));
        return false;
    }
    installed = true;
    Log("hooks result=0 count=4");
    return true;
}
// Tests replace the installer here.
bool (*gInstall)(const Natives&) = Install;

// What a strike did.
enum class Outcome : std::uint8_t { struck, drained, noStamina, notWrench, noHooks, fault };
const char* OutcomeName(Outcome o)
{
    switch (o) {
    case Outcome::struck: return "struck";
    case Outcome::drained: return "drained";
    case Outcome::noStamina: return "no_stamina";
    case Outcome::notWrench: return "not_wrench";
    case Outcome::noHooks: return "no_hooks";
    case Outcome::fault: return "fault";
    }
    return "none";
}
struct StrikeResult { Outcome outcome = Outcome::fault; int returned = -1; float fatigue = NAN; DWORD code = 0; };
// The strike: Prey's stamina check (drained, it flashes the HUD and nothing
// strikes, Prey's rule), the fatigue setter as Prey's attack calls it, the dodge
// flag cleared (no Prey clip is playing to have set it), then the hit with this
// contact as its only hit. Plain data in and out: all inside __try.
void Strike(const Natives& n, std::uintptr_t weapon, const scene::Hit& hit, Vec3 direction, const swing::Tier& tier, float bonus,
            StrikeResult& r)
{
    r = {};
    if (!installed.load()) {
        r.outcome = Outcome::noHooks;
        return;
    }
    __try {
        if (!weapon || *reinterpret_cast<const std::uintptr_t*>(weapon) != n.wrench) {
            r.outcome = Outcome::notWrench;
            return;
        }
        void* stamina = nullptr;
        if (n.playerObject) {
            std::memcpy(&stamina, reinterpret_cast<const void*>(n.playerObject + sn::PlayerStamina + sn::StaminaPointer), sizeof(stamina));
        }
        if (!stamina) {
            r.outcome = Outcome::noStamina;
            return;
        }
        if (!n.staminaAvailable(stamina)) {
            r.outcome = Outcome::drained;
            return;
        }
        const auto component = weapon + sn::ComponentOffset;
        n.setFatigue(component, tier.heavy, bonus);
        std::memcpy(&r.fatigue, reinterpret_cast<const void*>(component + sn::FatiguePerHit), 4);
        *reinterpret_cast<std::uint8_t*>(component + sn::DodgeFlag) = 0;
        striking.weapon = weapon;
        std::memcpy(&striking.entity, reinterpret_cast<const void*>(weapon + sn::WeaponEntity), sizeof(striking.entity));
        striking.hit = hit;
        striking.hit.next = nullptr;
        striking.direction = direction;
        striking.reversed = Scale(direction, -1);
        striking.active = true;
        r.returned = n.componentHit(component, 0.0f, weapon, tier.scale, tier.heavy);
        striking.active = false;
        r.outcome = Outcome::struck;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        striking.active = false;
        r.outcome = Outcome::fault;
        r.code = GetExceptionCode();
    }
}
// A struck prop or body is the hit's to move for a moment: physical contact leaves
// it alone (SwingRecentlyStruck), so a push does not overwrite the hit's impulse.
constexpr std::uint64_t kHandOffNs = 150000000ull;
struct HandOff { const void* collider = nullptr; std::uint64_t until = 0; };
std::array<HandOff, 8> handOff{};
unsigned handOffNext = 0;
void HandOver(const void* collider, std::uint64_t now)
{
    handOff[handOffNext++ % handOff.size()] = {collider, now + kHandOffNs};
    ++handoffs;
}

// --- the lane (input-drain thread) ----------------------------------------------
// Last frame's sample: its reference-space points and grip, so this frame can map
// them through its own world transform and see the arm's motion alone.
struct Last {
    std::array<Vec3, 9> local{};
    Vec3 grip{};
    std::uint64_t rig = 0, sequence = 0, reference = 0, epoch = 0, stamp = 0;
    bool valid = false;
};
Last last;
swing::Passes passes;
// One swing, while the head moves at 60% of the strike speed or more; it ends
// after a tenth of a second slower than a quarter of it. One line per swing.
struct Run { bool active = false; float head = 0, centre = 0, quiet = 0; unsigned frames = 0, contacts = 0, strikes = 0; };
Run run;
std::uint64_t loggedRig = 0, reportNs = 0, reportSum = 0, logSecondNs = 0;
unsigned logLines = 0;
std::uint64_t seenSettings = 0;
float chargeBonus = 0;
const char* reportGate = nullptr;
bool loggedGate = false, loggedSelf = false;

swing::StrikeSettings Settings()
{
    swing::StrikeSettings s{};
    s.minSpeed = static_cast<float>(minimumCms.load()) * .01f;
    return s;
}
bool LogBudget()
{
    const auto now = MonotonicNanoseconds();
    if (now - logSecondNs >= 1000000000ull) { logSecondNs = now; logLines = 0; }
    if (logLines >= kLogLinesPerSecond) { ++dropped; return false; }
    ++logLines;
    return true;
}
std::string Cms(float speed) { return std::to_string(std::lround(speed * 100)); }
const char* TypeName(int type) { return type == cn::TypeStatic ? "static" : type == cn::TypeRigid ? "rigid" : "articulated"; }
void EndRun(const char* why)
{
    if (!run.active) { return; }
    const Run r = run;
    run = {};
    ++swings;
    if (!eventLog.load() || !LogBudget()) { return; }
    Log("swing peakHeadCms=" + Cms(r.head) + " peakCentreCms=" + Cms(r.centre) + " frames=" + std::to_string(r.frames) +
        " contacts=" + std::to_string(r.contacts) + " strikes=" + std::to_string(r.strikes) + " end=" + why);
}
void TrackRun(float head, float centre, float dt, unsigned contacts, unsigned struck, const swing::StrikeSettings& s)
{
    if (head >= .6f * s.minSpeed) {
        run.active = true;
        run.quiet = 0;
    }
    if (!run.active) { return; }
    run.head = std::max(run.head, head);
    run.centre = std::max(run.centre, centre);
    ++run.frames;
    run.contacts += contacts;
    run.strikes += struck;
    if (head < .25f * s.minSpeed) {
        run.quiet += dt;
        if (run.quiet >= .1f) { EndRun("still"); }
    }
}
// Is the line from the head to the contact clear of fixed scenery other than the
// target? Stops 2 cm short, so the target's own surface is not in the way.
enum class Sight : std::uint8_t { clear, blocked, unknown };
const char* SightName(Sight s) { return s == Sight::clear ? "clear" : s == Sight::blocked ? "blocked" : "unknown"; }
Sight LineOfSight(const GameplayPoseFrame& frame, Vec3 point, void* target, scene::Hit& blocker)
{
    const Vec3 a = frame.cameraCentre, d = Sub(point, a);
    const float length = Length(d);
    if (!(length > .05f)) { return Sight::clear; }
    const Vec3 b = Add(a, Scale(d, (length - .02f) / length));
    if (!QuerySceneryBetween(frame, a, b, target, blocker)) { return Sight::unknown; }
    return blocker.distance < 0 ? Sight::clear : Sight::blocked;
}
void LogWrench(const Natives& n, const WeaponBox& sample)
{
    const auto w = ReadWrench(n, sample.weapon);
    chargeBonus = w.read && std::isfinite(w.chargeBonus) ? w.chargeBonus : 0.0f;
    if (!w.read) {
        Log("wrench rig=" + std::to_string(sample.rig) + " weapon=" + Hex(sample.weapon) + " read=0 vtable=" +
            std::to_string(w.wrench));
        return;
    }
    Log("wrench rig=" + std::to_string(sample.rig) + " weapon=" + Hex(sample.weapon) + " chargeTime=" + Fixed(w.chargeTime, 3) +
        " chargeFull=" + Fixed(w.chargeFull, 3) + " chargeBonus=" + Fixed(w.chargeBonus, 3) + " charged=" +
        std::to_string(w.charged) + " fatiguePerHit=" + Fixed(w.fatigue, 3) + " dodge=" + std::to_string(w.dodge) +
        " packages=" + Hex(w.packages[0]) + "," + Hex(w.packages[1]) + "," + Hex(w.packages[2]) + "," + Hex(w.packages[3]));
}
std::string Describe(const Natives& n, const Touch& t)
{
    Described d{};
    Describe(n, t.collider, t.type, d);
    std::string s = "entity=" + Hex(reinterpret_cast<std::uintptr_t>(t.collider)) + " type=" + TypeName(t.type) +
                    " vt=" + Hex(d.vtable >= PreyBase() ? d.vtable - PreyBase() : d.vtable) +
                    " foreign=" + (d.foreign == INT_MIN ? std::string("?") : std::to_string(d.foreign));
    if (d.hostRead) { s += " host=" + std::to_string(d.host != nullptr); }
    if (d.entity) {
        s += " entityId=" + std::to_string(d.id) + " class=" + (d.className[0] ? d.className : "?") + " name=" +
             (d.name[0] ? d.name : "?");
    } else {
        s += n.entitySystem ? " entityId=none" : " entityId=?";
    }
    if (d.faulted) { s += " described=fault"; }
    return s;
}
std::string lastText;   // the last decision, for the status line
std::string Status(const char* gate)
{
    const auto q = queries.load();
    return std::string("swing ") + (enabled.load() ? "on" : "off") + "  gate " + gate + "  strikes " + std::to_string(strikes.load()) + "  passes " +
           std::to_string(passes.Active()) + "  last " + (lastText.empty() ? "-" : lastText) + "  us avg " +
           std::to_string(q ? usTotal.load() / q : 0) + " max " + std::to_string(usMax.load());
}
void Publish(const GameplayPoseFrame& frame, const char* gate, const Box* box, const Box* before, bool fast, const Touch* mark,
             Decision decision, const std::string& label)
{
    if (!DebugOverlayWantsPhysical()) { return; }
    PhysicalOverlayState s{};
    s.gate = gate;
    s.open = !std::strcmp(gate, "open");
    if (box) {
        s.box = true;
        s.points = contact::BoxPoints(*box);
    }
    if (box && before && fast) {
        s.swept = true;
        s.from = contact::BoxPoints(*before);
        s.to = s.points;
    }
    if (mark) {
        s.contact = true;
        s.acted = decision == Decision::strike;
        s.point = mark->hit.point;
        s.normal = mark->hit.normal;
        s.result = label.c_str();
    }
    s.status = Status(gate);
    DebugOverlayPhysical(frame, s);
}
void MaybeReport()
{
    const auto now = MonotonicNanoseconds();
    if (now - reportNs < 10000000000ull) { return; }
    reportNs = now;
    const auto sum = queries.load() + refused.load() + touches.load() + begins.load() + strikes.load() + tooSlow.load() +
                     locked.load() + drained.load() + blocked.load() + swings.load();
    const char* gate = lastGate.load();
    if (sum == reportSum && gate == reportGate) { return; }
    reportSum = sum;
    reportGate = gate;
    Log("report" + SwingReport());
}
// How notable a decision is, for the overlay's one mark a frame.
int Rank(Decision d)
{
    switch (d) {
    case Decision::strike: return 4;
    case Decision::blocked:
    case Decision::failed: return 3;
    case Decision::drained: return 2;
    case Decision::tooSlow: return 1;
    default: return 0;
    }
}
void UpdateImpl(const GameplayPoseFrame& frame, bool allowed)
{
    MaybeReport();
    const auto settings = Settings();
    if (settingsGeneration.load() != seenSettings) {
        seenSettings = settingsGeneration.load();
        Log("settings enabled=" + std::to_string(enabled.load()) + " minCms=" + Cms(settings.minSpeed) + " heavyCms=" + Cms(settings.heavySpeed) + " fullCms=" +
            Cms(settings.fullSpeed) + " minScale=" + Fixed(settings.minScale, 2) + " lockMs=" +
            std::to_string(std::lround(settings.lockSeconds * 1000)) + " log=" + std::to_string(eventLog.load()));
    }
    const auto now = MonotonicNanoseconds();
    WeaponBox sample{};
    Box box{};
    const char* why = nullptr;
    if (!enabled.load()) { why = "off"; }
    else if (fault.load()) { why = "fault"; }
    else if (!allowed) { why = "tracking"; }
    else if (!HudGameplayInputAllowed()) { why = "menu"; }
    else if (BodyHeadOut()) { why = "head_out"; }
    else if (!frame.cameraCentreValid || !frame.headYawUsable || !std::isfinite(frame.yaw)) { why = "camera"; }
    else if (!FreshSample(now, frame.tracking.publishedNs, kFreshNs) ||
             frame.referenceGeneration != HeadTrackingReferenceGeneration()) { why = "pose"; }
    else if (!TryGetWeaponBox(sample) || !FreshSample(now, sample.publishedNs, kFreshNs)) { why = "no_weapon"; }
    else if (!sample.wrench) { why = "not_wrench"; }
    else if (!contact::BoxFromPoints(sample.points, box)) { why = "bad_box"; }
    else if (sample.reference != frame.referenceGeneration || sample.epoch != frame.tracking.epoch) { why = "reference"; }
    lastGate = why ? why : "open";
    if (why) {
        EndRun(why);
        passes.Clear();
        last = {};
        Publish(frame, why, nullptr, nullptr, false, nullptr, Decision::tooSlow, {});
        return;
    }
    // A new tracking sample only: the same pose twice would decide twice.
    if (last.valid && sample.rig == last.rig && sample.sequence == last.sequence) {
        Publish(frame, "open", &box, nullptr, false, nullptr, Decision::locked, {});
        return;
    }
    // Last frame's points through this frame's map: the arm's motion, not the player's.
    Box before{};
    const Box* previous = nullptr;
    float dt = 0;
    Vec3 gripBefore{};
    if (last.valid && last.rig == sample.rig && last.reference == sample.reference && last.epoch == sample.epoch &&
        sample.sequence > last.sequence) {
        std::array<Vec3, 9> points{};
        for (std::size_t i = 0; i < points.size(); ++i) { points[i] = physical::Transform(sample.worldFromReference, last.local[i]); }
        const float d = sample.stamp > last.stamp ? static_cast<float>(sample.stamp - last.stamp) * 1e-9f : 0.0f;
        if (d >= 1.0f / 240.0f && d <= 1.0f / 20.0f && contact::BoxFromPoints(points, before)) {
            previous = &before;
            dt = d;
            gripBefore = physical::Transform(sample.worldFromReference, last.grip);
        }
    }
    last = {sample.local, sample.grip, sample.rig, sample.sequence, sample.reference, sample.epoch, sample.stamp, true};
    const Vec3 grip = physical::Transform(sample.worldFromReference, sample.grip);
    const Box head = swing::HeadBox(box, grip);
    Box headBefore{};
    if (previous) { headBefore = swing::HeadBox(before, gripBefore); }
    Natives natives{};
    if (const DWORD ready = gResolve(frame, natives)) {
        ++refused;
        error = ready;
        if (ready != ERROR_NOT_READY && ready != ERROR_INVALID_THREAD_ID) {
            fault = true;
            Log("fault error=" + std::to_string(ready) + " detail=resolve");
        }
        return;
    }
    if (!loggedGate) {
        loggedGate = true;
        Log("gate result=0 rows=" + std::to_string(std::size(sn::Contracts)) + "+" + std::to_string(std::size(cn::Contracts)) +
            "+" + std::to_string(std::size(pn::Contracts)) +
            " entitySystem=" + std::to_string(natives.entitySystem != nullptr) +
            " surfaces=" + std::to_string(natives.surfaces != nullptr));
    }
    if (!installed.load() && !gInstall(natives)) {
        // The trigger stays Prey's and nothing strikes.
        fault = true;
        error = ERROR_INVALID_FUNCTION;
        Log("fault error=" + std::to_string(error.load()) + " detail=hooks");
        return;
    }
    if (sample.rig != loggedRig) {
        loggedRig = sample.rig;
        LogWrench(natives, sample);
    }
    ++frames;
    Touch found[kMaxTouches]{};
    unsigned count = 0;
    QueryStats stats{};
    LARGE_INTEGER start{}, end{}, frequency{};
    QueryPerformanceCounter(&start);
    const DWORD result = SafeQuery(natives, box, previous, head, previous ? &headBefore : nullptr, found, kMaxTouches, count, stats);
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    if (result == ERROR_BUFFER_OVERFLOW) {
        ++overflows;
        ++refused;
        count = 0;
    } else if (result) {
        fault = true;
        error = result;
        Log("fault error=" + std::to_string(result) + " detail=query");
        return;
    }
    const auto us = static_cast<std::uint64_t>((end.QuadPart - start.QuadPart) * 1000000 / std::max<LONGLONG>(frequency.QuadPart, 1));
    ++queries;
    usTotal += us;
    if (us > usMax.load()) { usMax = us; }
    entities += stats.entities;
    if (stats.entities > entitiesMax.load()) { entitiesMax = stats.entities; }
    statics += stats.statics;
    rigids += stats.rigids;
    bodies += stats.bodies;
    others += stats.other;
    selfSkipped += stats.self;
    if (firstSelf.body && !loggedSelf) {
        loggedSelf = true;
        Log("self entity=" + Hex(reinterpret_cast<std::uintptr_t>(firstSelf.body)) + " byHost=" +
            std::to_string(firstSelf.byHost) + " byEntity=" + std::to_string(firstSelf.byEntity) + " skipped");
    }
    const float headSpeed = previous ? Length(contact::PointVelocity(box, before, head.centre, dt)) : 0.0f;
    const float centreSpeed = previous ? Length(Sub(box.centre, before.centre)) / dt : 0.0f;
    const Touch* picked[kMaxTouches]{};
    const unsigned objects = PickPerObject(found, count, picked, kMaxTouches);
    touches += objects;
    const Touch* mark = nullptr;
    Decision markDecision = Decision::locked;
    std::string markLabel;
    unsigned begunNow = 0, struckNow = 0;
    for (unsigned i = 0; i < objects; ++i) {
        const Touch& t = *picked[i];
        if (t.kind == Kind::head) { ++headTouches; }
        const Vec3 v = previous ? contact::PointVelocity(box, before, t.hit.point, dt) : Vec3{};
        const float speed = Length(v), approach = swing::Approach(v, t.hit.normal);
        bool begun = false;
        auto* pass = passes.Touch(t.collider, sample.stamp, settings.lockSeconds, begun);
        if (begun) {
            ++begins;
            ++begunNow;
        }
        const auto tier = swing::Classify(speed, chargeBonus, settings);
        Stamina stamina{}, staminaAfter{};
        Sight sight = Sight::unknown;
        scene::Hit blocker{};
        blocker.distance = -1;
        StrikeResult struck{};
        Decision d = Decision::strike;
        if (pass && pass->struck) { d = Decision::locked; }
        else if (!tier.strike) { d = Decision::tooSlow; }
        else {
            sight = LineOfSight(frame, t.hit.point, t.collider, blocker);
            if (sight != Sight::clear) { d = Decision::blocked; }
            else {
                // Along the swing at the contact; a touch with no motion pushes into the surface.
                const Vec3 direction = speed > .1f ? Scale(v, 1 / speed) : Scale(t.hit.normal, -1);
                stamina = ReadStamina(natives);
                Strike(natives, sample.weapon, t.hit, direction, tier, chargeBonus, struck);
                staminaAfter = ReadStamina(natives);
                d = struck.outcome == Outcome::struck ? Decision::strike
                    : struck.outcome == Outcome::drained ? Decision::drained : Decision::failed;
                if (struck.outcome == Outcome::fault) {
                    fault = true;
                    error = struck.code;
                    Log("fault error=" + std::to_string(struck.code) + " detail=strike");
                }
                // One try per pass, whatever came of it.
                if (pass) { pass->struck = true; }
            }
        }
        switch (d) {
        case Decision::strike:
            ++strikes;
            ++struckNow;
            QueueHaptic(Hand::right, haptics::Event::WeaponStrike, frame.tracking);
            if (tier.heavy) { ++heavies; }
            if (t.type != cn::TypeStatic) { HandOver(t.collider, now); }
            break;
        case Decision::failed: ++failed; break;
        case Decision::locked: ++locked; break;
        case Decision::tooSlow: ++tooSlow; break;
        case Decision::drained: ++drained; break;
        case Decision::blocked:
            ++blocked;
            if (sight == Sight::unknown) { ++sightUnknown; }
            break;
        }
        // The strike line: each strike, and the first refusal of each kind in a pass.
        const std::uint8_t bit = static_cast<std::uint8_t>(1u << static_cast<unsigned>(d));
        const bool note = d == Decision::strike || d == Decision::failed ||
                          ((d == Decision::drained || d == Decision::blocked) && pass && !(pass->noted & bit));
        if (pass) { pass->noted |= bit; }
        const auto* surface = Surface(natives, t.hit.surface);
        const std::string surfaceName = surface && surface->found ? surface->name : std::to_string(t.hit.surface);
        if (!mark || Rank(d) > Rank(markDecision)) {
            mark = &t;
            markDecision = d;
            markLabel = std::string(swing::DecisionName(d)) + " " + surfaceName + " " + Cms(speed) + "cm/s";
        }
        if (d != Decision::locked) { lastText = markLabel; }
        if (!eventLog.load()) { continue; }
        const std::string motion = " kind=" + std::string(KindName(t.kind)) + " partId=" + std::to_string(t.hit.partId) +
                                   " speedCms=" + Cms(speed) + " approachCms=" + Cms(approach) +
                                   " headCms=" + Cms(headSpeed) + " centreCms=" + Cms(centreSpeed);
        if (begun && LogBudget()) {
            Log("begin " + Describe(natives, t) + " " + SurfaceText(natives, t.hit.surface) + motion + " decision=" +
                swing::DecisionName(d) + " point=" + Point(t.hit.point) + " normal=" + Point(t.hit.normal));
        }
        if (note && LogBudget()) {
            std::string line = "strike " + Describe(natives, t) + " " + SurfaceText(natives, t.hit.surface) + motion +
                               " result=" + swing::DecisionName(d);
            if (tier.strike) {
                line += std::string(" tier=") + (tier.heavy ? "heavy" : "normal") + " scale=" + Fixed(tier.scale, 2);
            }
            if (struck.outcome == Outcome::struck) {
                line += " returned=" + std::to_string(struck.returned) + " fatigue=" + Fixed(struck.fatigue, 2);
            } else if (d == Decision::failed) {
                line += std::string(" why=") + OutcomeName(struck.outcome);
            }
            if (stamina.read) {
                line += " stamina=" + Fixed(stamina.a, 1) + "/" + Fixed(stamina.b, 1);
                if (staminaAfter.read) { line += "->" + Fixed(staminaAfter.a, 1) + "/" + Fixed(staminaAfter.b, 1); }
            }
            if (d == Decision::blocked || d == Decision::strike) { line += std::string(" sight=") + SightName(sight); }
            if (d == Decision::blocked && blocker.distance >= 0) {
                line += " blocker=" + Hex(reinterpret_cast<std::uintptr_t>(blocker.collider)) + " blockerSurface=" +
                        std::to_string(blocker.surface);
            }
            Log(line);
        }
    }
    passes.Expire(sample.stamp, settings.lockSeconds);
    TrackRun(headSpeed, centreSpeed, dt, begunNow, struckNow, settings);
    const bool fast = headSpeed >= .6f * settings.minSpeed;
    Publish(frame, "open", &box, previous, fast, mark, markDecision, markLabel);
}
} // namespace

void UpdateSwingLane(const GameplayPoseFrame& frame, bool allowed)
{
    thread_local bool busy = false;
    if (busy || !EngineThread()) { return; }
    struct Guard { bool& value; explicit Guard(bool& v) : value(v) { value = true; } ~Guard() { value = false; } } guard(busy);
    UpdateImpl(frame, allowed);
}
bool SwingLaneWanted() { return enabled.load() && !fault.load(); }
void SetFreeSwing(unsigned on)
{
    enabled = on != 0;
    ++settingsGeneration;
}
unsigned FreeSwingEnabled() { return enabled.load() ? 1u : 0u; }
void SetSwingSpeed(unsigned cms)
{
    minimumCms = std::clamp(cms, 80u, 250u);
    ++settingsGeneration;
}
unsigned SwingSpeed() { return minimumCms.load(); }
bool SwingRecentlyStruck(const void* collider)
{
    if (!collider) { return false; }
    const auto now = MonotonicNanoseconds();
    for (const auto& h : handOff) {
        if (h.collider == collider && now < h.until) { return true; }
    }
    return false;
}
void SetSwingEventLog(unsigned on) { eventLog = on != 0; }
unsigned SwingEventLog() { return eventLog.load() ? 1u : 0u; }
std::string SwingReport()
{
    const auto q = queries.load();
    return " enabled=" + std::to_string(enabled.load()) + " fault=" + std::to_string(fault.load()) +
           " frames=" + std::to_string(frames.load()) + " queries=" + std::to_string(q) +
           " refused=" + std::to_string(refused.load()) + " overflow=" + std::to_string(overflows.load()) +
           " entitiesAvg=" + Fixed(q ? float(entities.load()) / float(q) : 0, 2) + " entitiesMax=" +
           std::to_string(entitiesMax.load()) + " static=" + std::to_string(statics.load()) + " rigid=" +
           std::to_string(rigids.load()) + " articulated=" + std::to_string(bodies.load()) + " other=" +
           std::to_string(others.load()) + " self=" + std::to_string(selfSkipped.load()) + " touches=" + std::to_string(touches.load()) + " headTouches=" +
           std::to_string(headTouches.load()) + " passes=" + std::to_string(begins.load()) + " struck=" +
           std::to_string(strikes.load()) + " heavy=" + std::to_string(heavies.load()) + " failed=" +
           std::to_string(failed.load()) + " tooSlow=" +
           std::to_string(tooSlow.load()) + " locked=" + std::to_string(locked.load()) + " drained=" +
           std::to_string(drained.load()) + " blocked=" + std::to_string(blocked.load()) + " sightUnknown=" +
           std::to_string(sightUnknown.load()) + " swings=" + std::to_string(swings.load()) + " hooks=" +
           std::to_string(installed.load()) + " attacksBlocked=" + std::to_string(attacksBlocked.load()) + " substituted=" +
           std::to_string(substituted.load()) + " impulses=" + std::to_string(impulsesTurned.load()) + " collisions=" +
           std::to_string(collisionsTurned.load()) + " handoffs=" + std::to_string(handoffs.load()) + " log=" +
           std::to_string(eventLog.load()) + " droppedLines=" + std::to_string(dropped.load()) + " usAvg=" +
           std::to_string(q ? usTotal.load() / q : 0) + " usMax=" + std::to_string(usMax.load()) + " error=" +
           std::to_string(error.load()) + " gate=" + lastGate.load();
}
} // namespace preyvr::dll
