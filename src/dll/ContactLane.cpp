#include "ContactLane.h"
#include "AimTakeover.h"
#include "BodyFollow.h"
#include "CarryLane.h"
#include "DebugOverlay.h"
#include "EngineAccess.h"
#include "Haptics.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "InputPost.h"
#include "Logger.h"
#include "PhysicalInteractions.h"
#include "PhysicsCalls.h"
#include "SceneQuery.h"
#include "preyvr/ContactNative.h"
#include "preyvr/ContactQuery.h"
#include "preyvr/MonotonicClock.h"
#include "preyvr/NativeContract.h"
#include "preyvr/PhysicalNative.h"
#include "preyvr/SceneQueryNative.h"
#include "preyvr/TextFormat.h"
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstring>
#include <iterator>

namespace preyvr::dll {
namespace {
namespace cn = contact::native;
using contact::Box;
using contact::Contact;
using contact::Kind;
constexpr unsigned kMaxEntities = 32, kMaxContacts = 32, kLogLinesPerSecond = 40;
constexpr std::uint64_t kFreshNs = 100000000ull;

std::atomic<bool> enabled{false}, fault{false}, pushReady{false};
std::atomic<unsigned> strength{100};
std::atomic<std::uint64_t> settings{1};
std::atomic<std::uint64_t> pushes{0}, pushBegins{0}, limitedPushes{0}, slidingPushes{0}, pushFailed{0}, dvMaxMm{0};
std::atomic<std::uint64_t> queries{0}, refused{0}, overflows{0}, entities{0}, entitiesMax{0}, others{0};
std::atomic<std::uint64_t> found{0}, overlaps{0}, sweeps{0}, begins{0}, ends{0}, dropped{0}, usTotal{0}, usMax{0};
std::atomic<DWORD> error{0};
std::atomic<const char*> lastGate{"none"};

// --- the engine's calls ---------------------------------------------------------
using EntitiesInBox = int(__fastcall*)(std::uintptr_t, const Vec3*, const Vec3*, void***, int, int);
using Collide = int(__fastcall*)(std::uintptr_t, void*, int, contact::BoxPrimitive*, const Vec3*, scene::Hit*, void*);
using SurfaceParameters = int(__fastcall*)(std::uintptr_t, int, float*, float*, unsigned*);
struct Natives {
    std::uintptr_t world = 0, rigid = 0, articulated = 0;
    void* player = nullptr;
    void* carried = nullptr;
    EntitiesInBox entitiesInBox = nullptr;
    Collide collide = nullptr;
    SurfaceParameters surface = nullptr;
    bool push = false;   // the rigid Action, QueueAction and the velocity queue passed
};
struct QueryStats { unsigned entities = 0, other = 0, skipped = 0; DWORD error = 0; };

bool EngineThread() { return InputDrainThreadId() != 0 && InputDrainThreadId() == GetCurrentThreadId(); }
void Log(const std::string& line) { lifecycle::Log("preyvr_contact " + line); }

// The broadphase over both poses, then the box against each movable object: still,
// at this frame's pose (overlap), and from last frame's pose to this one (sweep).
// What the player is and what the hand carries are never tested. Faults
// propagate to the caller's __try.
unsigned Query(const Natives& n, const Box& now, const Box* previous, Contact* out, unsigned capacity, QueryStats& s)
{
    s = {};
    Vec3 lo{}, hi{};
    contact::Bounds(previous ? *previous : now, now, .02f, lo, hi);
    void* list[kMaxEntities]{};
    void** listed = list;
    const int count = n.entitiesInBox(n.world, &lo, &hi, &listed, cn::MovableObjects, static_cast<int>(kMaxEntities));
    // More than our buffer holds: the engine hands back its shared per-caller
    // buffer instead, which another query may overwrite. Refuse it.
    if (count < 0 || count > static_cast<int>(kMaxEntities) || listed != list) { s.error = ERROR_BUFFER_OVERFLOW; return 0; }
    s.entities = static_cast<unsigned>(count);
    const Vec3 still{};
    const Vec3 sweep = previous ? Sub(now.centre, previous->centre) : Vec3{};
    const bool moving = previous && Length(sweep) >= .001f;
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
        if (!entity || entity == n.player || entity == n.carried) { ++s.skipped; continue; }
        const auto table = *reinterpret_cast<const std::uintptr_t*>(entity);
        const int type = table == n.rigid ? cn::TypeRigid : table == n.articulated ? cn::TypeArticulated : 0;
        if (!type) { ++s.other; continue; }
        test(entity, type, now, still, Kind::Overlap);
        if (moving) { test(entity, type, *previous, sweep, Kind::Sweep); }
    }
    return written;
}
DWORD SafeQuery(const Natives& n, const Box& now, const Box* previous, Contact* out, unsigned capacity, unsigned& written,
                QueryStats& s)
{
    __try {
        written = Query(n, now, previous, out, capacity, s);
        return s.error;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        written = 0;
        return GetExceptionCode();
    }
}

bool SlotIs(std::uintptr_t base, std::uintptr_t table, std::size_t slot, std::uintptr_t rva)
{
    return *reinterpret_cast<const std::uintptr_t*>(base + table + slot) == base + rva;
}
// Every function called through a named RVA is one the gate hashes.
static_assert(contract::Gated(cn::Contracts, cn::GetEntitiesInBox) && contract::Gated(cn::Contracts, cn::CollideEntityWithPrimitive) &&
              contract::Gated(cn::Contracts, cn::GetSurfaceParameters) && contract::Gated(cn::Contracts, cn::RigidGetStatus) &&
              contract::Gated(cn::Contracts, cn::ArticulatedGetStatus));
DWORD GateBodies(std::uintptr_t base)
{
    __try {
        if (!contract::BodiesMatch(base, cn::Contracts)) { return ERROR_BAD_EXE_FORMAT; }
        const auto world = scene::native::WorldVtable;
        if (!SlotIs(base, world, cn::EntitiesInBoxSlot, cn::GetEntitiesInBox) ||
            !SlotIs(base, world, cn::CollidePrimitiveSlot, cn::CollideEntityWithPrimitive) ||
            !SlotIs(base, world, cn::SurfaceParametersSlot, cn::GetSurfaceParameters) ||
            !SlotIs(base, cn::RigidVtable, cn::TypeSlot, physical::native::RigidGetType) ||
            !SlotIs(base, cn::RigidVtable, cn::StatusSlot, cn::RigidGetStatus) ||
            !SlotIs(base, cn::ArticulatedVtable, cn::TypeSlot, cn::ArticulatedGetType) ||
            !SlotIs(base, cn::ArticulatedVtable, cn::StatusSlot, cn::ArticulatedGetStatus)) {
            return ERROR_INVALID_ADDRESS;
        }
        return ERROR_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
// Pushing sets a rigid body's velocity through its Action, which may queue it:
// the rigid Action and QueueAction bodies (PhysicalNative.h), the rigid Action
// slot, and the queue's set-velocity entry (40 bytes, no payload).
static_assert(contract::Gated(physical::native::Contracts, physical::native::RigidAction) &&
              contract::Gated(physical::native::Contracts, physical::native::QueueAction));
bool GatePush(std::uintptr_t base)
{
    __try {
        for (const auto& row : physical::native::Contracts) {
            if ((row.rva == physical::native::RigidAction || row.rva == physical::native::QueueAction) &&
                !contract::BodyMatches(base, row)) {
                return false;
            }
        }
        return SlotIs(base, cn::RigidVtable, cn::ActionSlot, physical::native::RigidAction) && phys::VelocityQueueReady(base);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool gated = false;
DWORD Resolve(const GameplayPoseFrame& frame, Natives& n)
{
    n = {};
    std::uintptr_t world = 0;
    void* player = nullptr;
    if (const DWORD ready = SceneQueryWorld(frame, world, player)) { return ready; }
    const auto base = PreyBase();
    if (!gated) {
        if (const DWORD bodies = GateBodies(base)) { return bodies; }
        pushReady = GatePush(base);
        gated = true;
    }
    n.push = pushReady.load();
    n.world = world;
    n.player = player;
    n.carried = reinterpret_cast<void*>(CarriedPhysics());
    n.rigid = base + cn::RigidVtable;
    n.articulated = base + cn::ArticulatedVtable;
    n.entitiesInBox = reinterpret_cast<EntitiesInBox>(base + cn::GetEntitiesInBox);
    n.collide = reinterpret_cast<Collide>(base + cn::CollideEntityWithPrimitive);
    n.surface = reinterpret_cast<SurfaceParameters>(base + cn::GetSurfaceParameters);
    return ERROR_SUCCESS;
}
// Tests replace the engine here.
DWORD (*gResolve)(const GameplayPoseFrame&, Natives&) = Resolve;

// What a newly touched object is, for its log line: its mass (pe_status_dynamics,
// type 8, through the hashed rigid or articulated GetStatus) and its surface's
// friction and bounciness from the engine's own tables. -1: unreadable.
struct Described { float mass = -1, friction = -1, bounciness = -1; };
Described Describe(const Natives& n, void* entity, int surface)
{
    Described d{};
    __try {
        alignas(16) std::uint8_t status[128]{};
        const int type = 8, unused = INT_MIN;
        std::memcpy(status, &type, 4);
        std::memcpy(status + 4, &unused, 4);
        std::memcpy(status + 8, &unused, 4);
        const auto table = *reinterpret_cast<const std::uintptr_t*>(entity);
        const auto getStatus = *reinterpret_cast<int(__fastcall* const*)(void*, void*)>(table + cn::StatusSlot);
        if (getStatus(entity, status)) { std::memcpy(&d.mass, status + 0x4C, 4); }
        float bounciness = 0, friction = 0;
        unsigned flags = 0;
        if (n.surface && surface >= 0 && n.surface(n.world, surface, &bounciness, &friction, &flags)) {
            d.friction = friction;
            d.bounciness = bounciness;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return d;
}

// A rigid body's state for a push: dynamics status (velocity, spin, centre of
// mass, mass) and the inverse mass and world inverse inertia its Action uses. A
// body whose inverse mass disagrees with its mass, or is zero, is not pushed.
bool ReadRigid(void* entity, contact::Body& body, float& mass)
{
    phys::Dynamics d{};
    if (!phys::QueryDynamics(reinterpret_cast<std::uintptr_t>(entity), d)) { return false; }
    body = {};
    __try {
        const auto at = reinterpret_cast<std::uintptr_t>(entity);
        std::memcpy(&body.inverseMass, reinterpret_cast<const void*>(at + cn::RigidInverseMass), sizeof(float));
        std::memcpy(body.inverseInertia.data(), reinterpret_cast<const void*>(at + cn::RigidInverseInertia),
                    sizeof(body.inverseInertia));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    body.v = d.v;
    body.w = d.w;
    body.centre = d.centre;
    mass = d.mass;
    return body.inverseMass > 0 && std::isfinite(body.inverseMass) && mass > 0 &&
           std::fabs(body.inverseMass * mass - 1) < .05f;
}

// --- the lane (input-drain thread) ----------------------------------------------
struct Last { Box box{}; std::uint64_t rig = 0, sequence = 0, reference = 0, stamp = 0; float yaw = 0; bool valid = false; };
Last last;
contact::Tracker tracker;
std::uint64_t seenSettings = 0, reportNs = 0, reportSum = 0, logSecondNs = 0;
const char* reportGate = nullptr;
unsigned logLines = 0;
bool loggedGate = false;
// What is being pushed, and the frame it last was. A velocity-matched object needs
// no push on frames it already keeps up, so a push lasts until the object goes
// kPushGapFrames without one; one that starts gets a log line and a pulse.
constexpr std::uint64_t kPushGapFrames = 30;
struct Pushing { const void* collider = nullptr; std::uint64_t frame = 0; };
std::array<Pushing, kMaxContacts> pushing{};
std::uint64_t pushFrame = 0;
// True when `collider` starts a push this frame; records it either way.
bool PushStarts(const void* collider)
{
    Pushing* free = nullptr;
    for (auto& p : pushing) {
        if (p.collider && pushFrame - p.frame > kPushGapFrames) { p = {}; }
        if (p.collider == collider) { p.frame = pushFrame; return false; }
        if (!p.collider && !free) { free = &p; }
    }
    if (free) { *free = {collider, pushFrame}; }
    return true;
}

// At most kLogLinesPerSecond event lines; the rest are counted.
bool LogBudget()
{
    const auto now = MonotonicNanoseconds();
    if (now - logSecondNs >= 1000000000ull) { logSecondNs = now; logLines = 0; }
    if (logLines >= kLogLinesPerSecond) { ++dropped; return false; }
    ++logLines;
    return true;
}
std::string TypeName(int type) { return type == cn::TypeArticulated ? "articulated" : type == cn::TypeRigid ? "rigid" : "other"; }
void EndAll(const char* why)
{
    contact::Tracker::Event events[contact::Tracker::kCapacity]{};
    const unsigned n = tracker.Reset(events, contact::Tracker::kCapacity);
    for (unsigned i = 0; i < n; ++i) {
        ++ends;
        if (LogBudget()) { Log("end entity=" + Hex(reinterpret_cast<std::uintptr_t>(events[i].collider)) +
                               " frames=" + std::to_string(events[i].frames) + " why=" + why); }
    }
}
void LogEvents(const Natives& n, const Contact* contacts, unsigned count, const Vec3& sweep)
{
    const void* touched[kMaxContacts]{};
    for (unsigned i = 0; i < count; ++i) { touched[i] = contacts[i].collider; }
    contact::Tracker::Event events[kMaxContacts + contact::Tracker::kCapacity]{};
    const unsigned total = tracker.Update(touched, count, events, static_cast<unsigned>(std::size(events)));
    for (unsigned e = 0; e < total; ++e) {
        const auto& event = events[e];
        const std::string id = Hex(reinterpret_cast<std::uintptr_t>(event.collider));
        if (!event.begin) {
            ++ends;
            if (LogBudget()) { Log("end entity=" + id + " frames=" + std::to_string(event.frames)); }
            continue;
        }
        ++begins;
        if (!LogBudget()) { continue; }
        // The overlap record when there is one, else the sweep's.
        const Contact* c = nullptr;
        for (unsigned i = 0; i < count; ++i) {
            if (contacts[i].collider != event.collider) { continue; }
            if (!c || contacts[i].kind == Kind::Overlap) { c = &contacts[i]; }
        }
        if (!c) { continue; }
        bool both = false;
        for (unsigned i = 0; i < count; ++i) { both |= contacts[i].collider == event.collider && contacts[i].kind != c->kind; }
        const auto d = Describe(n, c->collider, c->hit.surface);
        Log("begin entity=" + id + " type=" + TypeName(c->type) + " kind=" + contact::KindName(c->kind) +
            (both ? "+sweep" : "") + " dist=" + Fixed(c->hit.distance, 4) + " partId=" + std::to_string(c->hit.partId) +
            " surface=" + std::to_string(c->hit.surface) + " friction=" + Fixed(d.friction, 2) +
            " bounciness=" + Fixed(d.bounciness, 2) + " mass=" + Fixed(d.mass, 2) + " point=" + Point(c->hit.point) +
            " normal=" + Point(c->hit.normal) + " sweepCm=" + Fixed(Length(sweep) * 100, 1));
    }
}
void Publish(const GameplayPoseFrame& frame, const Box* box, const std::array<Vec3, 9>* points, const char* gate,
             const Contact* contacts, unsigned count, const Vec3* pushed = nullptr, unsigned pushCount = 0)
{
    if (!DebugOverlayWantsPhysical()) { return; }
    ContactOverlayState state{};
    state.box = box && points;
    if (state.box) { state.points = *points; }
    state.open = !std::strcmp(gate, "open");
    state.gate = gate;
    unsigned overlap = 0, sweep = 0;
    for (unsigned i = 0; i < count; ++i) {
        (contacts[i].kind == Kind::Sweep ? sweep : overlap) += 1;
        if (state.count < state.marks.size()) {
            auto& m = state.marks[state.count++];
            m.point = contacts[i].hit.point;
            m.normal = contacts[i].hit.normal;
            m.dist = contacts[i].hit.distance;
            m.sweep = contacts[i].kind == Kind::Sweep;
            m.articulated = contacts[i].type == cn::TypeArticulated;
            if (pushed && Length(pushed[i]) > 0) { m.push = pushed[i]; m.pushed = true; }
        }
    }
    const auto q = queries.load();
    state.status = std::string(enabled.load() ? "on" : "off") + "  gate " + gate + "  overlap " + std::to_string(overlap) +
                   "  sweep " + std::to_string(sweep) + "  objects " + std::to_string(tracker.Tracked()) +
                   "  push " + std::to_string(pushCount) + (pushReady.load() ? "" : " (unavailable)") + "  us avg " +
                   std::to_string(q ? usTotal.load() / q : 0) + " max " + std::to_string(usMax.load());
    DebugOverlayContact(frame, state);
}
void MaybeReport()
{
    const auto now = MonotonicNanoseconds();
    if (now - reportNs < 10000000000ull) { return; }
    reportNs = now;
    const auto sum = queries.load() + refused.load() + found.load() + begins.load() + ends.load() + overflows.load() +
                     pushes.load() + pushFailed.load();
    const char* gate = lastGate.load();
    if (sum == reportSum && gate == reportGate) { return; }
    reportSum = sum;
    reportGate = gate;
    Log("report" + ContactReport());
}
// One push per touched rigid object: its deepest overlap, else its sweep. The
// velocity change keeps its surface with the weapon (contact::Solve). `pushed`
// gets each applied contact's velocity change, by contact index.
unsigned PushContacts(const GameplayPoseFrame& frame, const Natives& n, const Contact* contacts, unsigned count,
                      const Box& box, const Box* previous, float dt, Vec3* pushed)
{
    if (!n.push) { return 0; }
    contact::PushSettings tuning{};
    tuning.force *= static_cast<float>(strength.load()) * .01f;
    const float step = dt > 0 ? dt : 1.0f / 90.0f;
    ++pushFrame;
    unsigned applied = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto& c = contacts[i];
        if (c.type != cn::TypeRigid) { continue; }   // bodies: phase 4
        bool best = true;
        for (unsigned j = 0; j < count && best; ++j) {
            if (j == i || contacts[j].collider != c.collider) { continue; }
            const bool overlap = c.kind == Kind::Overlap, other = contacts[j].kind == Kind::Overlap;
            best = overlap != other ? overlap : c.hit.distance > contacts[j].hit.distance || (c.hit.distance == contacts[j].hit.distance && i < j);
        }
        if (!best) { continue; }
        contact::Body body{};
        float mass = 0;
        if (!ReadRigid(c.collider, body, mass)) { ++pushFailed; continue; }
        float bounciness = 0, friction = 0;
        unsigned flags = 0;
        if (!n.surface || c.hit.surface < 0 || !n.surface(n.world, c.hit.surface, &bounciness, &friction, &flags)) {
            friction = tuning.proxyFriction;   // the engine's default surface
        }
        const Vec3 velocity = previous ? contact::PointVelocity(box, *previous, c.hit.point, dt) : Vec3{};
        const float depth = c.kind == Kind::Overlap ? c.hit.distance : 0.0f;
        const auto push = contact::Solve(body, c.hit.point, c.hit.normal, depth, velocity, friction, step, tuning);
        if (!push.apply) { continue; }
        phys::SetVelocityAction action{};
        action.v = push.v;
        action.w = push.w;
        if (!phys::SetVelocity(reinterpret_cast<std::uintptr_t>(c.collider), action)) { ++pushFailed; continue; }
        ++pushes;
        if (push.limited) { ++limitedPushes; }
        if (push.sliding) { ++slidingPushes; }
        const Vec3 change = Sub(push.v, body.v);
        const auto mm = static_cast<std::uint64_t>(Length(change) * 1000);
        if (mm > dvMaxMm.load()) { dvMaxMm = mm; }
        pushed[i] = change;
        ++applied;
        if (!PushStarts(c.collider)) { continue; }
        ++pushBegins;
        QueueHaptic(Hand::right, haptics::Event::WeaponContact, frame.tracking);
        if (LogBudget()) {
            Log("push entity=" + Hex(reinterpret_cast<std::uintptr_t>(c.collider)) + " kind=" + contact::KindName(c.kind) +
                " mass=" + Fixed(mass, 2) + " friction=" + Fixed(friction, 2) + " approachCms=" + Fixed(push.approach * 100, 1) +
                " handCms=" + Fixed(Length(velocity) * 100, 1) + " dvCms=" + Fixed(Length(change) * 100, 1) +
                " depthMm=" + Fixed(depth * 1000, 1) + " limited=" + std::to_string(push.limited) +
                " sliding=" + std::to_string(push.sliding) + " dtMs=" + Fixed(step * 1000, 1));
        }
    }
    return applied;
}
float AngleBetween(float a, float b)
{
    const float d = std::remainder(a - b, 6.2831853f);
    return std::fabs(d);
}
void UpdateImpl(const GameplayPoseFrame& frame, bool allowed)
{
    MaybeReport();
    const auto generation = settings.load();
    if (generation != seenSettings) {
        seenSettings = generation;
        Log("settings enabled=" + std::to_string(enabled.load()) + " strength=" + std::to_string(strength.load()));
    }
    const auto now = MonotonicNanoseconds();
    WeaponBox sample{};
    Box box{};
    bool boxed = false;
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
    else {
        boxed = contact::BoxFromPoints(sample.points, box);
        if (!boxed) { why = "bad_box"; }
        else if (sample.reference != frame.referenceGeneration || sample.epoch != frame.tracking.epoch) { why = "reference"; }
    }
    lastGate = why ? why : "open";
    if (why) {
        if (tracker.Tracked()) { EndAll(why); }
        last = {};
        pushing = {};
        Publish(frame, boxed ? &box : nullptr, boxed ? &sample.points : nullptr, why, nullptr, 0);
        return;
    }
    // Last frame's box, if it is the same weapon, a newer pose, and nothing jumped
    // (a snap turn swings the whole box about the player).
    // A copy: `last` takes this frame's box before the push reads the previous one.
    Box before{};
    const Box* previous = nullptr;
    if (last.valid && last.rig == sample.rig && last.reference == sample.reference && sample.sequence > last.sequence &&
        AngleBetween(sample.yaw, last.yaw) < .05f && Length(Sub(box.centre, last.box.centre)) < .3f) {
        before = last.box;
        previous = &before;
    }
    Natives natives{};
    if (const DWORD ready = gResolve(frame, natives)) {
        ++refused;
        error = ready;
        if (ready != ERROR_NOT_READY && ready != ERROR_INVALID_THREAD_ID) {
            fault = true;
            Log("fault error=" + std::to_string(ready) + " detail=resolve");
            EndAll("fault");
        }
        return;
    }
    if (!loggedGate) {
        loggedGate = true;
        Log("gate result=0 rows=" + std::to_string(std::size(cn::Contracts)) + " push=" + std::to_string(natives.push));
    }
    Contact contacts[kMaxContacts]{};
    QueryStats stats{};
    unsigned count = 0;
    LARGE_INTEGER start{}, end{}, frequency{};
    QueryPerformanceCounter(&start);
    const DWORD result = SafeQuery(natives, box, previous, contacts, kMaxContacts, count, stats);
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    const auto us = static_cast<std::uint64_t>((end.QuadPart - start.QuadPart) * 1000000 / std::max<LONGLONG>(frequency.QuadPart, 1));
    ++queries;
    usTotal += us;
    if (us > usMax.load()) { usMax = us; }
    if (result == ERROR_BUFFER_OVERFLOW) {
        ++overflows;
        ++refused;
        count = 0;
    } else if (result) {
        fault = true;
        error = result;
        Log("fault error=" + std::to_string(result) + " detail=query");
        EndAll("fault");
        return;
    }
    entities += stats.entities;
    if (stats.entities > entitiesMax.load()) { entitiesMax = stats.entities; }
    others += stats.other;
    found += count;
    for (unsigned i = 0; i < count; ++i) { (contacts[i].kind == Kind::Sweep ? sweeps : overlaps) += 1; }
    const Vec3 sweep = previous ? Sub(box.centre, previous->centre) : Vec3{};
    // The weapon's speed comes from its last two poses. Outside 1/240..1/20 s apart
    // it is not trusted: no velocity, only easing an overlap out.
    float dt = previous && sample.stamp > last.stamp ? static_cast<float>(sample.stamp - last.stamp) * 1e-9f : 0.0f;
    if (dt < 1.0f / 240.0f || dt > 1.0f / 20.0f) { dt = 0; }
    last = {box, sample.rig, sample.sequence, sample.reference, sample.stamp, sample.yaw, true};
    LogEvents(natives, contacts, count, sweep);
    Vec3 pushed[kMaxContacts]{};
    const unsigned pushCount = PushContacts(frame, natives, contacts, count, box, dt > 0 ? previous : nullptr, dt, pushed);
    Publish(frame, &box, &sample.points, "open", contacts, count, pushed, pushCount);
}
} // namespace

void UpdateContactLane(const GameplayPoseFrame& frame, bool allowed)
{
    thread_local bool busy = false;
    if (busy || !EngineThread()) { return; }
    struct Guard { bool& value; explicit Guard(bool& v) : value(v) { value = true; } ~Guard() { value = false; } } guard(busy);
    UpdateImpl(frame, allowed);
}
void SetPhysicalContact(unsigned v)
{
    enabled = v != 0;
    ++settings;
}
unsigned PhysicalContactEnabled() { return enabled.load() ? 1u : 0u; }
void SetPushStrength(unsigned percent)
{
    strength = std::clamp(percent, 25u, 200u);
    ++settings;
}
unsigned PushStrength() { return strength.load(); }
std::string ContactReport()
{
    const auto q = queries.load();
    return " enabled=" + std::to_string(enabled.load()) + " fault=" + std::to_string(fault.load()) +
           " queries=" + std::to_string(q) + " refused=" + std::to_string(refused.load()) +
           " overflow=" + std::to_string(overflows.load()) + " entitiesAvg=" + Fixed(q ? float(entities.load()) / float(q) : 0, 2) +
           " entitiesMax=" + std::to_string(entitiesMax.load()) + " other=" + std::to_string(others.load()) +
           " contacts=" + std::to_string(found.load()) + " overlaps=" + std::to_string(overlaps.load()) +
           " sweeps=" + std::to_string(sweeps.load()) + " begins=" + std::to_string(begins.load()) +
           " ends=" + std::to_string(ends.load()) + " push=" + std::to_string(pushReady.load()) +
           " strength=" + std::to_string(strength.load()) + " pushes=" + std::to_string(pushes.load()) +
           " pushBegins=" + std::to_string(pushBegins.load()) + " limited=" + std::to_string(limitedPushes.load()) +
           " sliding=" + std::to_string(slidingPushes.load()) + " pushFailed=" + std::to_string(pushFailed.load()) +
           " dvMaxCms=" + std::to_string(dvMaxMm.load() / 10) + " droppedLines=" + std::to_string(dropped.load()) +
           " usAvg=" + std::to_string(q ? usTotal.load() / q : 0) + " usMax=" + std::to_string(usMax.load()) +
           " error=" + std::to_string(error.load()) + " gate=" + lastGate.load();
}
} // namespace preyvr::dll
