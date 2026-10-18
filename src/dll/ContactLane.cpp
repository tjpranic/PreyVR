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
#include "InteractionLane.h"
#include "Logger.h"
#include "MoveLane.h"
#include "PhysicalInteractions.h"
#include "PhysicsCalls.h"
#include "PsiMedkit.h"
#include "SceneQuery.h"
#include "XrInput.h"
#include "preyvr/AnimIk.h"
#include "preyvr/ContactNative.h"
#include "preyvr/ContactQuery.h"
#include "preyvr/MonotonicClock.h"
#include "preyvr/NativeContract.h"
#include "preyvr/PhysicalNative.h"
#include "preyvr/SceneQueryNative.h"
#include "preyvr/StereoCamera.h"
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

std::atomic<bool> enabled{true}, fault{false}, pushReady{false}, bodyReady{false}, eventLog{false};
std::atomic<unsigned> strength{100};
std::atomic<std::uint64_t> settings{1};
std::atomic<std::uint64_t> pushes{0}, pushBegins{0}, limitedPushes{0}, slidingPushes{0}, pushFailed{0}, dvMaxMm{0};
std::atomic<std::uint64_t> partPushes{0}, hostedRefused{0}, immovableRefused{0}, standingSkipped{0};
std::atomic<std::uint64_t> queries{0}, refused{0}, overflows{0}, entities{0}, entitiesMax{0}, others{0};
std::atomic<std::uint64_t> found{0}, overlaps{0}, sweeps{0}, begins{0}, ends{0}, dropped{0}, usTotal{0}, usMax{0};
std::atomic<DWORD> error{0};
std::atomic<const char*> lastGate{"none"};

// --- the engine's calls ---------------------------------------------------------
using EntitiesInBox = int(__fastcall*)(std::uintptr_t, const Vec3*, const Vec3*, void***, int, int);
// CollideEntityWithPrimitive(world, entity, itype, primitive, sweep, hit, ip): the
// primitive is the itype's (contact::BoxPrimitive, contact::SpherePrimitive).
using Collide = int(__fastcall*)(std::uintptr_t, void*, int, void*, const Vec3*, scene::Hit*, void*);
using SurfaceParameters = int(__fastcall*)(std::uintptr_t, int, float*, float*, unsigned*);
struct Natives {
    std::uintptr_t world = 0, rigid = 0, articulated = 0;
    void* player = nullptr;
    void* carried = nullptr;
    EntitiesInBox entitiesInBox = nullptr;
    Collide collide = nullptr;
    SurfaceParameters surface = nullptr;
    bool push = false;     // the rigid Action, QueueAction and the velocity queue passed
    bool bodies = false;   // and the articulated Action and step slots: corpses push too
    std::uintptr_t living = 0;   // the living vtable, its GetStatus slot passed: what the player stands on
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
// Pushing a corpse sets one part's velocity through the articulated Action, which
// the step then starts from (ContactNative.h): its Action and step slots, on top
// of the rigid push gate's queue.
static_assert(contract::Gated(cn::Contracts, cn::ArticulatedAction) && contract::Gated(cn::Contracts, cn::ArticulatedStep) &&
              contract::Gated(cn::Contracts, cn::ArticulatedStepJoints) && contract::Gated(cn::Contracts, cn::ArticulatedStepBack) &&
              contract::Gated(cn::Contracts, cn::ArticulatedGetParams));
bool GateBodyPush(std::uintptr_t base)
{
    __try {
        return SlotIs(base, cn::ArticulatedVtable, cn::ActionSlot, cn::ArticulatedAction) &&
               SlotIs(base, cn::ArticulatedVtable, cn::StepSlot, cn::ArticulatedStep) &&
               SlotIs(base, cn::ArticulatedVtable, cn::StepBackSlot, cn::ArticulatedStepBack) &&
               SlotIs(base, cn::ArticulatedVtable, cn::ParamsSlot, cn::ArticulatedGetParams);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// What the player stands on comes from the player's living GetStatus
// (pe_status_living) and GetParams (pe_player_dimensions): the living vtable's
// slots, their bodies and the base functions each asks first.
static_assert(contract::Gated(cn::Contracts, cn::LivingGetStatus) && contract::Gated(cn::Contracts, cn::StaticGetStatus) &&
              contract::Gated(cn::Contracts, cn::LivingGetParams) && contract::Gated(cn::Contracts, cn::StaticGetParams));
bool GateLiving(std::uintptr_t base)
{
    __try {
        return SlotIs(base, cn::LivingVtable, cn::StatusSlot, cn::LivingGetStatus) &&
               SlotIs(base, cn::LivingVtable, cn::ParamsSlot, cn::LivingGetParams);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool gated = false, gatedPush = false, gatedBodyPush = false, gatedLiving = false;
DWORD Resolve(const GameplayPoseFrame& frame, Natives& n)
{
    n = {};
    std::uintptr_t world = 0;
    void* player = nullptr;
    if (const DWORD ready = SceneQueryWorld(frame, world, player)) { return ready; }
    const auto base = PreyBase();
    if (!gated) {
        if (const DWORD bodies = GateBodies(base)) { return bodies; }
        gatedPush = GatePush(base);
        gatedBodyPush = gatedPush && GateBodyPush(base);
        gatedLiving = GateLiving(base);
        gated = true;
    }
    n.push = gatedPush;
    n.bodies = gatedBodyPush;
    n.living = gatedLiving ? base + cn::LivingVtable : 0;
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

// How a push's read of a body ended. Immovable: the game gave it no mass, pinned
// in place on purpose; told apart from a read that failed.
enum class BodyRead { ok, immovable, hosted, failed };
BodyRead Checked(const contact::Body& body, float mass)
{
    if (mass == 0 && body.inverseMass == 0) { return BodyRead::immovable; }
    return body.inverseMass > 0 && std::isfinite(body.inverseMass) && mass > 0 &&
                   std::fabs(body.inverseMass * mass - 1) < .05f
               ? BodyRead::ok
               : BodyRead::failed;
}
// A rigid body's state for a push: dynamics status (velocity, spin, centre of
// mass, mass) and the inverse mass and world inverse inertia its Action uses. A
// body whose inverse mass disagrees with its mass is not pushed.
BodyRead ReadRigid(void* entity, contact::Body& body, float& mass)
{
    phys::Dynamics d{};
    if (!phys::QueryDynamics(reinterpret_cast<std::uintptr_t>(entity), d)) { return BodyRead::failed; }
    body = {};
    __try {
        const auto at = reinterpret_cast<std::uintptr_t>(entity);
        std::memcpy(&body.inverseMass, reinterpret_cast<const void*>(at + cn::RigidInverseMass), sizeof(float));
        std::memcpy(body.inverseInertia.data(), reinterpret_cast<const void*>(at + cn::RigidInverseInertia),
                    sizeof(body.inverseInertia));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return BodyRead::failed;
    }
    body.v = d.v;
    body.w = d.w;
    body.centre = d.centre;
    mass = d.mass;
    return Checked(body, mass);
}

// An articulated body's host: set for a live character's skeleton, null for a
// ragdoll. -1: unreadable.
int Hosted(const void* entity)
{
    __try {
        return *reinterpret_cast<void* const*>(reinterpret_cast<std::uintptr_t>(entity) + cn::ArticulatedHost) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
// One part of a corpse, for a push: its joint's body, read live where the
// per-part set velocity writes it (ContactNative.h), and checked against the
// hashed per-part dynamics status (the same mass, the same centre). A body with a
// host is a live character's skeleton and is never pushed.
constexpr int kMaxParts = 256;
BodyRead ReadPart(void* entity, int partId, contact::Body& body, float& mass)
{
    body = {};
    mass = 0;
    alignas(16) std::uint8_t status[128]{};
    const int type = 8, unused = INT_MIN;
    std::memcpy(status, &type, 4);
    std::memcpy(status + 4, &partId, 4);   // partid, as ray_hit names it
    std::memcpy(status + 8, &unused, 4);   // ipart
    __try {
        const auto at = reinterpret_cast<std::uintptr_t>(entity);
        if (*reinterpret_cast<void* const*>(at + cn::ArticulatedHost)) { return BodyRead::hosted; }
        const auto table = *reinterpret_cast<const std::uintptr_t*>(at);
        const auto getStatus = *reinterpret_cast<int(__fastcall* const*)(void*, void*)>(table + cn::StatusSlot);
        if (!getStatus(entity, status)) { return BodyRead::failed; }
        const auto parts = *reinterpret_cast<const std::uintptr_t*>(at + cn::ArticulatedParts);
        const auto infos = *reinterpret_cast<const std::uintptr_t*>(at + cn::ArticulatedInfos);
        const auto joints = *reinterpret_cast<const std::uintptr_t*>(at + cn::ArticulatedJoints);
        const int count = *reinterpret_cast<const int*>(at + cn::ArticulatedPartCount);
        const int jointCount = *reinterpret_cast<const int*>(at + cn::ArticulatedJointCount);
        if (!parts || !infos || !joints || count <= 0 || count > kMaxParts || jointCount <= 0 || jointCount > kMaxParts) {
            return BodyRead::failed;
        }
        int i = 0;
        while (i < count && *reinterpret_cast<const int*>(parts + i * cn::PartStride + cn::PartId) != partId) { ++i; }
        if (i == count) { return BodyRead::failed; }
        const int j = *reinterpret_cast<const int*>(infos + i * cn::InfoStride + cn::InfoJoint);
        if (j < 0 || j >= jointCount) { return BodyRead::failed; }
        const auto b = joints + static_cast<std::uintptr_t>(j) * cn::JointStride + cn::JointBody;
        std::memcpy(&body.centre, reinterpret_cast<const void*>(b + cn::BodyCentre), sizeof(Vec3));
        std::memcpy(&body.w, reinterpret_cast<const void*>(b + cn::BodyW), sizeof(Vec3));
        std::memcpy(&body.v, reinterpret_cast<const void*>(b + cn::BodyV), sizeof(Vec3));
        std::memcpy(&mass, reinterpret_cast<const void*>(b + cn::BodyMass), sizeof(float));
        std::memcpy(&body.inverseMass, reinterpret_cast<const void*>(b + cn::BodyInverseMass), sizeof(float));
        std::memcpy(body.inverseInertia.data(), reinterpret_cast<const void*>(b + cn::BodyInverseInertia),
                    sizeof(body.inverseInertia));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return BodyRead::failed;
    }
    Vec3 centre{};
    float reported = 0;
    std::memcpy(&centre, status + 0x3C, sizeof(Vec3));
    std::memcpy(&reported, status + 0x4C, sizeof(float));
    const bool same = std::isfinite(reported) && std::fabs(reported - mass) <= .001f * mass && Finite(centre) &&
                      Finite(body.centre) && Length(Sub(centre, body.centre)) < .1f;
    return same ? Checked(body, mass) : BodyRead::failed;
}

// How the player stands, from their living body (ContactNative.h): the engine's
// ground collider (only while the body is active: standing still, it lets go),
// the stance offset, and pe_player_dimensions' pivot height and collider radius.
// Not its airborne flag: standing on a light object it reports the player flying
// in bursts (PreyVR-33), so where the feet are decides.
struct Footing {
    bool read = false, feetValid = false;
    const void* collider = nullptr;
    Vec3 feet{};
    float radius = NAN;   // NaN: unread
};
const char* groundClass = "unread";   // living, other: logged once
Footing ReadFooting(const Natives& n)
{
    Footing f{};
    if (!n.living || !n.player) { return f; }
    alignas(16) std::uint8_t status[0x100]{}, dims[0x80]{};
    static_assert(cn::LivingStatusSize <= sizeof(status) && cn::LivingGroundCollider + 8 <= cn::LivingStatusSize &&
                  cn::DimensionsBytes <= sizeof(dims));
    const int type = cn::StatusLiving, dimsType = cn::ParamsPlayerDimensions;
    std::memcpy(status, &type, 4);
    std::memcpy(dims, &dimsType, 4);
    Vec3 position{};
    bool haveDims = false;
    __try {
        const auto table = *reinterpret_cast<const std::uintptr_t*>(n.player);
        if (table != n.living) { groundClass = "other"; return f; }
        groundClass = "living";
        const auto getStatus = *reinterpret_cast<int(__fastcall* const*)(void*, void*)>(table + cn::StatusSlot);
        if (!getStatus(n.player, status)) { return f; }
        const auto getParams = *reinterpret_cast<int(__fastcall* const*)(void*, void*)>(table + cn::ParamsSlot);
        haveDims = getParams(n.player, dims) != 0;
        std::memcpy(&position, reinterpret_cast<const std::uint8_t*>(n.player) + cn::EntityPosition, sizeof(position));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return f;
    }
    Vec3 stance{};
    void* collider = nullptr;
    std::memcpy(&stance, status + cn::LivingCamOffset, sizeof(stance));
    std::memcpy(&collider, status + cn::LivingGroundCollider, sizeof(collider));
    float pivot = 0, radius = NAN;
    if (haveDims) {
        std::memcpy(&pivot, dims + cn::DimensionsPivot, 4);
        std::memcpy(&radius, dims + cn::DimensionsSize, 4);
    }
    if (!std::isfinite(pivot) || std::fabs(pivot) >= 2) { pivot = 0; }
    f.read = true;
    f.collider = collider;
    f.radius = std::isfinite(radius) && radius > .05f && radius < 1.5f ? radius : NAN;
    f.feet = Add(Add(position, stance), Vec3{0, 0, -pivot});
    f.feetValid = Finite(f.feet) && Length(stance) < 3;
    return f;
}
// The test under the feet, per object: is its top where the player's feet are?
// At the centre, at 4 points half the collider's radius out and at 8 on its edge
// (the radius from pe_player_dimensions; 35 cm when unread), a 3 cm sphere just
// below the feet's height goes through the per-entity test against that object
// alone, so nothing over it (the held weapon, another prop) can hide it. Where the
// object's top runs through the sphere it reports a contact facing up: the player
// stands on it there. Where it is not under that point, nothing. A can beside the
// feet meets the sphere with its bottom or its side: facing down or sideways, not
// underfoot. The sphere straddles the surface it looks for, so it is never wholly
// inside a mesh. (What failed first, and why: the scope doc's phase 5.)
constexpr float kFootRadiusUnread = .35f, kFootRadiusMin = .15f, kFootRadiusMax = .75f;
constexpr float kFootSphere = .03f, kFootSphereDown = .005f, kFootBelow = .06f, kFootAbove = .025f, kFootUp = .7f;
constexpr float kHeadAboveFeetMin = .2f, kHeadAboveFeetMax = 2.6f;
constexpr unsigned kFootSamples = 13;
float FootRadius(float radius) { return std::isfinite(radius) ? std::clamp(radius, kFootRadiusMin, kFootRadiusMax) : kFootRadiusUnread; }
// Sample i of kFootSamples, as an offset from the feet: the centre, 4 at half the
// radius, 8 at the radius.
Vec3 FootSample(unsigned i, float radius)
{
    if (i == 0) { return {}; }
    const bool inner = i <= 4;
    const float r = inner ? radius * .5f : radius, a = inner ? (i - 1) * 1.5707963f : (i - 5) * .78539816f;
    return {r * std::cos(a), r * std::sin(a), 0};
}
bool Supports(const scene::Hit& hit, float feetZ)
{
    return hit.normal.z > kFootUp && hit.point.z >= feetZ - kFootBelow && hit.point.z <= feetZ + kFootAbove;
}
// How the player stands this frame (read once, when something is to be pushed),
// what is underfoot so far, and which objects were tested against the feet.
struct Underfoot {
    bool probed = false, feetValid = false;
    Box probe{};   // the samples' footprint, for the overlay
    Vec3 feet{};
    float radius = NAN;
    const void* engine = nullptr;   // the engine's ground collider
    std::array<const void*, 8> objects{}, tested{};
    unsigned count = 0, testedCount = 0;
    bool Has(const void* o) const { return std::find(objects.begin(), objects.begin() + count, o) != objects.begin() + count; }
    bool Tested(const void* o) const { return std::find(tested.begin(), tested.begin() + testedCount, o) != tested.begin() + testedCount; }
    void Insert(const void* o)
    {
        if (o && count < objects.size() && !Has(o)) { objects[count++] = o; }
    }
};
Underfoot ReadUnderfoot(const GameplayPoseFrame& frame, const Natives& n)
{
    Underfoot u{};
    const auto footing = ReadFooting(n);
    if (!footing.read) { return u; }
    u.engine = footing.collider;
    u.radius = footing.radius;
    if (!footing.feetValid || !frame.cameraCentreValid) { return u; }
    u.feet = footing.feet;
    u.feetValid = true;
    const float headAboveFeet = frame.cameraCentre.z - footing.feet.z;
    if (!(headAboveFeet >= kHeadAboveFeetMin && headAboveFeet <= kHeadAboveFeetMax)) { return u; }
    u.probed = true;
    const float radius = FootRadius(footing.radius);
    u.probe.axis = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
    u.probe.centre = footing.feet;
    u.probe.half = {radius, radius, .01f};
    return u;
}
bool CollideSphere(const Natives& n, const void* object, contact::SpherePrimitive sphere, scene::Hit& hit)
{
    __try {
        const Vec3 still{};
        return n.collide(n.world, const_cast<void*>(object), cn::PrimitiveSphere, &sphere, &still, &hit, nullptr) &&
               contact::Valid(hit, object);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// Whether any of the samples finds the object's top under the feet.
bool OnTopOf(const Natives& n, const Underfoot& u, const void* object)
{
    const float radius = FootRadius(u.radius);
    for (unsigned i = 0; i < kFootSamples; ++i) {
        contact::SpherePrimitive sphere{Add(u.feet, Add(FootSample(i, radius), Vec3{0, 0, -kFootSphereDown})), kFootSphere};
        scene::Hit hit{};
        if (CollideSphere(n, object, sphere, hit) && Supports(hit, u.feet.z)) { return true; }
    }
    return false;
}

// --- the lane (input-drain thread) ----------------------------------------------
// The boxes that push (Contact::proxy): the held weapon; the right hand while no
// weapon is drawn (a drawn weapon is its box); the left hand while it is free.
enum Proxy : std::uint8_t { kWeapon = 0, kRightHand = 1, kLeftHand = 2, kProxies = 3 };
const char* ProxyName(unsigned p) { return p == kWeapon ? "weapon" : p == kRightHand ? "right_hand" : "left_hand"; }
Hand ProxyHand(unsigned p) { return p == kLeftHand ? Hand::left : Hand::right; }
// One proxy this frame: its box, and what tells this pose from the last.
struct ProxyNow {
    bool active = false;
    const char* why = "";   // why not, when inactive
    Box box{};
    std::uint64_t key = 0, sequence = 0, stamp = 0, reference = 0;
    float yaw = 0;
};
using Proxies = std::array<ProxyNow, kProxies>;
struct Last { Box box{}; std::uint64_t key = 0, sequence = 0, reference = 0, stamp = 0; float yaw = 0; bool valid = false; };
std::array<Last, kProxies> last{};
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

// The per-event lines (begin, end, push) are off unless `contact log 1` asks for
// them; the counters, the 10 s report and the settings, gate, ground and fault
// lines always run. At most kLogLinesPerSecond event lines; the rest are counted.
bool LogBudget()
{
    const auto now = MonotonicNanoseconds();
    if (now - logSecondNs >= 1000000000ull) { logSecondNs = now; logLines = 0; }
    if (logLines >= kLogLinesPerSecond) { ++dropped; return false; }
    ++logLines;
    return true;
}
std::string TypeName(int type) { return type == cn::TypeArticulated ? "articulated" : type == cn::TypeRigid ? "rigid" : "other"; }
// What was underfoot stays so while the player's feet stay within kStandStay of
// where it was last found under them (standing still, the engine lets go of its
// collider), and for kStandHoldFrames after they move or leave the ground: a push
// then would slide it from under them. Each object starting and ending is an
// event line.
constexpr std::uint64_t kStandHoldFrames = 20;
constexpr float kStandStay = .10f, kStandStayUp = .05f;
struct Stood { const void* object = nullptr; std::uint64_t first = 0, frame = 0; Vec3 feet{}; bool feetValid = false; };
std::array<Stood, 8> stood{};
bool StaysOn(const Stood& held, const Underfoot& u)
{
    if (!held.feetValid || !u.feetValid) { return false; }
    const Vec3 d = Sub(u.feet, held.feet);
    return std::hypot(d.x, d.y) <= kStandStay && std::fabs(d.z) <= kStandStayUp;
}
// At the frame's first read: lapse what the player has left, keep the rest underfoot.
void ExpireStood(Underfoot& u)
{
    for (auto& held : stood) {
        if (!held.object) { continue; }
        if (StaysOn(held, u)) { held.frame = pushFrame; }   // still on it: the hold starts when they move
        else if (pushFrame - held.frame > kStandHoldFrames) {
            if (eventLog.load() && LogBudget()) {
                Log("stand_end entity=" + Hex(reinterpret_cast<std::uintptr_t>(held.object)) +
                    " frames=" + std::to_string(held.frame - held.first + 1));
            }
            held = {};
            continue;
        }
        u.Insert(held.object);
    }
}
// `object` was found underfoot (by the engine, or the feet's samples).
void Stand(Underfoot& u, const void* object, const char* via)
{
    u.Insert(object);
    auto it = std::find_if(stood.begin(), stood.end(), [&](const Stood& h) { return h.object == object; });
    if (it == stood.end()) {
        it = std::find_if(stood.begin(), stood.end(), [](const Stood& h) { return !h.object; });
        if (it == stood.end()) { return; }
        *it = {object, pushFrame, pushFrame};
        if (eventLog.load() && LogBudget()) { Log("stand entity=" + Hex(reinterpret_cast<std::uintptr_t>(object)) + " via=" + via); }
    }
    it->frame = pushFrame;
    it->feet = u.feet;
    it->feetValid = u.feetValid;
}
void EndAll(const char* why)
{
    contact::Tracker::Event events[contact::Tracker::kCapacity]{};
    const unsigned n = tracker.Reset(events, contact::Tracker::kCapacity);
    for (unsigned i = 0; i < n; ++i) {
        ++ends;
        if (eventLog.load() && LogBudget()) {
            Log("end entity=" + Hex(reinterpret_cast<std::uintptr_t>(events[i].collider)) +
                " frames=" + std::to_string(events[i].frames) + " why=" + why);
        }
    }
}
void LogEvents(const Natives& n, const Contact* contacts, unsigned count)
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
            if (eventLog.load() && LogBudget()) { Log("end entity=" + id + " frames=" + std::to_string(event.frames)); }
            continue;
        }
        ++begins;
        if (!eventLog.load() || !LogBudget()) { continue; }
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
            (both ? "+sweep" : "") + " proxy=" + ProxyName(c->proxy) + " dist=" + Fixed(c->hit.distance, 4) +
            " partId=" + std::to_string(c->hit.partId) +
            (c->type == cn::TypeArticulated ? " host=" + std::to_string(Hosted(c->collider)) : std::string()) +
            " surface=" + std::to_string(c->hit.surface) +
            " friction=" + Fixed(d.friction, 2) + " bounciness=" + Fixed(d.bounciness, 2) + " mass=" + Fixed(d.mass, 2) +
            " point=" + Point(c->hit.point) + " normal=" + Point(c->hit.normal));
    }
}
void Publish(const GameplayPoseFrame& frame, const Proxies& proxy, const char* gate, const Contact* contacts, unsigned count,
             const Vec3* pushed = nullptr, unsigned pushCount = 0, const Underfoot* under = nullptr)
{
    if (!DebugOverlayWantsPhysical()) { return; }
    ContactOverlayState state{};
    if (under && under->probed) {
        state.feet = true;
        state.feetBox = contact::BoxPoints(under->probe);
    }
    state.standing = under && under->count > 0;
    state.box = proxy[kWeapon].active;
    if (state.box) { state.points = contact::BoxPoints(proxy[kWeapon].box); }
    for (unsigned h = 0; h < 2; ++h) {
        const auto& hand = proxy[kRightHand + h];
        state.handBox[h] = hand.active;
        if (hand.active) { state.hands[h] = contact::BoxPoints(hand.box); }
    }
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
    std::string boxes;
    for (unsigned p = 0; p < kProxies; ++p) {
        if (proxy[p].active) { boxes += std::string(boxes.empty() ? "" : "+") + ProxyName(p); }
    }
    const auto q = queries.load();
    state.status = std::string(enabled.load() ? "on" : "off") + "  gate " + gate + "  boxes " + (boxes.empty() ? "-" : boxes) +
                   "  overlap " + std::to_string(overlap) + "  sweep " + std::to_string(sweep) + "  objects " +
                   std::to_string(tracker.Tracked()) + "  push " + std::to_string(pushCount) +
                   (!pushReady.load() ? " (unavailable)" : !bodyReady.load() ? " (props only)" : "") +
                   "  us avg " + std::to_string(q ? usTotal.load() / q : 0) +
                   " max " + std::to_string(usMax.load());
    DebugOverlayContact(frame, state);
}
void MaybeReport()
{
    const auto now = MonotonicNanoseconds();
    if (now - reportNs < 10000000000ull) { return; }
    reportNs = now;
    const auto sum = queries.load() + refused.load() + found.load() + begins.load() + ends.load() + overflows.load() +
                     pushes.load() + pushFailed.load() + hostedRefused.load() + immovableRefused.load() +
                     standingSkipped.load();
    const char* gate = lastGate.load();
    if (sum == reportSum && gate == reportGate) { return; }
    reportSum = sum;
    reportGate = gate;
    Log("report" + ContactReport());
}
// One push per touched rigid object, and per touched part of a corpse: its
// deepest overlap from any proxy, else its sweep. The velocity change keeps its
// surface with that proxy (contact::Solve), so two proxies on one object never
// fight; a corpse's part moves alone, and its joints carry the rest. What the
// player stands on is never pushed. `pushed` gets each applied contact's
// velocity change, by contact index.
bool loggedGround = false;
unsigned PushContacts(const GameplayPoseFrame& frame, const Natives& n, const Contact* contacts, unsigned count,
                      const Proxies& proxy, const std::array<const Box*, kProxies>& previous,
                      const std::array<float, kProxies>& dt, Vec3* pushed, Underfoot* underfoot = nullptr)
{
    if (!n.push) { return 0; }
    contact::PushSettings tuning{};
    tuning.force *= static_cast<float>(strength.load()) * .01f;
    ++pushFrame;
    unsigned applied = 0;
    Underfoot under{};
    bool groundRead = false;
    for (unsigned i = 0; i < count; ++i) {
        const auto& c = contacts[i];
        const bool part = c.type == cn::TypeArticulated;
        if (part ? !n.bodies : c.type != cn::TypeRigid) { continue; }
        bool best = true;
        for (unsigned j = 0; j < count && best; ++j) {
            if (j == i || contacts[j].collider != c.collider || (part && contacts[j].hit.partId != c.hit.partId)) { continue; }
            const bool overlap = c.kind == Kind::Overlap, other = contacts[j].kind == Kind::Overlap;
            best = overlap != other ? overlap : c.hit.distance > contacts[j].hit.distance || (c.hit.distance == contacts[j].hit.distance && i < j);
        }
        if (!best) { continue; }
        if (!groundRead) {
            groundRead = true;
            under = ReadUnderfoot(frame, n);
            ExpireStood(under);
            if (under.engine) { Stand(under, under.engine, "engine"); }
            if (!loggedGround) {
                loggedGround = true;
                Log(std::string("ground player=") + groundClass + " radius=" + Fixed(under.radius, 2));
            }
        }
        // Each pushable object, once a frame: is its top under the feet?
        if (!under.Has(c.collider) && under.probed && !under.Tested(c.collider) && under.testedCount < under.tested.size()) {
            under.tested[under.testedCount++] = c.collider;
            if (OnTopOf(n, under, c.collider)) { Stand(under, c.collider, "feet"); }
        }
        if (under.Has(c.collider)) { ++standingSkipped; continue; }
        contact::Body body{};
        float mass = 0;
        const auto read = part ? ReadPart(c.collider, c.hit.partId, body, mass) : ReadRigid(c.collider, body, mass);
        if (read == BodyRead::hosted) { ++hostedRefused; continue; }
        if (read == BodyRead::immovable) { ++immovableRefused; continue; }
        if (read != BodyRead::ok) { ++pushFailed; continue; }
        float bounciness = 0, friction = 0;
        unsigned flags = 0;
        if (!n.surface || c.hit.surface < 0 || !n.surface(n.world, c.hit.surface, &bounciness, &friction, &flags)) {
            friction = tuning.proxyFriction;   // the engine's default surface
        }
        const unsigned p = c.proxy < kProxies ? c.proxy : kWeapon;
        const float step = dt[p] > 0 ? dt[p] : 1.0f / 90.0f;
        const Vec3 velocity = previous[p] && dt[p] > 0 ? contact::PointVelocity(proxy[p].box, *previous[p], c.hit.point, dt[p]) : Vec3{};
        const float depth = c.kind == Kind::Overlap ? c.hit.distance : 0.0f;
        const auto push = contact::Solve(body, c.hit.point, c.hit.normal, depth, velocity, friction, step, tuning);
        if (!push.apply) { continue; }
        phys::SetVelocityAction action{};
        if (part) { action.partid = c.hit.partId; }
        action.v = push.v;
        action.w = push.w;
        if (!phys::SetVelocity(reinterpret_cast<std::uintptr_t>(c.collider), action)) { ++pushFailed; continue; }
        ++pushes;
        if (part) { ++partPushes; }
        if (push.limited) { ++limitedPushes; }
        if (push.sliding) { ++slidingPushes; }
        const Vec3 change = Sub(push.v, body.v);
        const auto mm = static_cast<std::uint64_t>(Length(change) * 1000);
        if (mm > dvMaxMm.load()) { dvMaxMm = mm; }
        pushed[i] = change;
        ++applied;
        if (!PushStarts(c.collider)) { continue; }
        ++pushBegins;
        QueueHaptic(ProxyHand(p), haptics::Event::WeaponContact, frame.tracking);
        if (eventLog.load() && LogBudget()) {
            Log("push entity=" + Hex(reinterpret_cast<std::uintptr_t>(c.collider)) + " type=" + TypeName(c.type) +
                (part ? " part=" + std::to_string(c.hit.partId) : std::string()) + " proxy=" + ProxyName(p) +
                " kind=" + contact::KindName(c.kind) + " mass=" + Fixed(mass, 2) + " friction=" + Fixed(friction, 2) +
                " approachCms=" + Fixed(push.approach * 100, 1) + " handCms=" + Fixed(Length(velocity) * 100, 1) +
                " dvCms=" + Fixed(Length(change) * 100, 1) + " depthMm=" + Fixed(depth * 1000, 1) +
                " limited=" + std::to_string(push.limited) + " sliding=" + std::to_string(push.sliding) +
                " dtMs=" + Fixed(step * 1000, 1));
        }
    }
    if (underfoot) { *underfoot = under; }
    return applied;
}
float AngleBetween(float a, float b)
{
    const float d = std::remainder(a - b, 6.2831853f);
    return std::fabs(d);
}
// A hand's box, from its controller's grip pose mapped into the world as the
// hands are placed (animik::ControllerWorldFromHead, the carry lane's own route).
void HandProxy(const GameplayPoseFrame& f, Hand hand, ProxyNow& out)
{
    const auto& h = f.tracking.hands[static_cast<unsigned>(hand)];
    if (!IsPoseUsable(h.gripPose, h.gripValidity, kMaxPoseAgeNs)) { out.why = "hand_pose"; return; }
    const Pose grip = animik::ControllerWorldFromHead(f.yaw, f.cameraCentre, f.tracking.head, h.gripPose);
    const auto& q = grip.orientation;
    if (!Finite(grip.position) || !std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) {
        out.why = "hand_pose";
        return;
    }
    const std::array<Vec3, 3> axes{Rotate(q, stereo::ToEngineSpace(Vec3{1, 0, 0})), Rotate(q, stereo::ToEngineSpace(Vec3{0, 1, 0})),
                                   Rotate(q, stereo::ToEngineSpace(Vec3{0, 0, 1}))};
    out.box = contact::HandBox(grip.position, axes, hand == Hand::left);
    out.key = 1 + static_cast<unsigned>(hand);
    out.sequence = f.tracking.sequence;
    out.stamp = static_cast<std::uint64_t>(f.tracking.displayTime);
    out.reference = f.referenceGeneration;
    out.yaw = f.yaw;
    out.active = true;
}
// The left hand is not free while it carries, holds the foregrip, works the
// medkit slot or aims psi: those are its jobs, and a push would fight them.
const char* LeftHandBusy()
{
    if (UseCarrying() || CarryHoldingInHand()) { return "carrying"; }
    if (TwoHandedAimHeld()) { return "foregrip"; }
    if (MedkitZoneHasLeftHand()) { return "medkit"; }
    if (PsiButtonHeld()) { return "psi"; }
    return nullptr;
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
    Proxies proxy{};
    const char* why = nullptr;
    if (!enabled.load()) { why = "off"; }
    else if (fault.load()) { why = "fault"; }
    else if (!allowed) { why = "tracking"; }
    else if (!HudGameplayInputAllowed()) { why = "menu"; }
    else if (BodyHeadOut()) { why = "head_out"; }
    else if (!frame.cameraCentreValid || !frame.headYawUsable || !std::isfinite(frame.yaw)) { why = "camera"; }
    else if (!FreshSample(now, frame.tracking.publishedNs, kFreshNs) ||
             frame.referenceGeneration != HeadTrackingReferenceGeneration()) { why = "pose"; }
    else {
        auto& weapon = proxy[kWeapon];
        WeaponBox sample{};
        if (!TryGetWeaponBox(sample) || !FreshSample(now, sample.publishedNs, kFreshNs)) { weapon.why = "no_weapon"; }
        else if (!contact::BoxFromPoints(sample.points, weapon.box)) { weapon.why = "bad_box"; }
        else if (sample.reference != frame.referenceGeneration || sample.epoch != frame.tracking.epoch) { weapon.why = "reference"; }
        else {
            weapon.active = true;
            weapon.key = sample.rig;
            weapon.sequence = sample.sequence;
            weapon.stamp = sample.stamp;
            weapon.reference = sample.reference;
            weapon.yaw = sample.yaw;
        }
        // A drawn weapon is the right hand's box, whatever its own state.
        if (std::strcmp(weapon.why, "no_weapon") != 0) { proxy[kRightHand].why = "weapon"; }
        else { HandProxy(frame, Hand::right, proxy[kRightHand]); }
        if (const char* busy = LeftHandBusy()) { proxy[kLeftHand].why = busy; }
        else { HandProxy(frame, Hand::left, proxy[kLeftHand]); }
        if (!proxy[kWeapon].active && !proxy[kRightHand].active && !proxy[kLeftHand].active) { why = "idle"; }
    }
    lastGate = why ? why : "open";
    if (why) {
        if (tracker.Tracked()) { EndAll(why); }
        last = {};
        pushing = {};
        stood = {};
        Publish(frame, proxy, why, nullptr, 0);
        return;
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
    // What this frame may push, for the report and the overlay.
    pushReady = natives.push;
    bodyReady = natives.push && natives.bodies;
    if (!loggedGate) {
        loggedGate = true;
        Log("gate result=0 rows=" + std::to_string(std::size(cn::Contracts)) + " push=" + std::to_string(natives.push) +
            " bodies=" + std::to_string(natives.bodies));
    }
    Contact contacts[kMaxContacts]{};
    unsigned count = 0;
    // Last frame's box per proxy, if it is the same weapon or hand, a newer pose,
    // and nothing jumped (a snap turn swings every box about the player). Copies:
    // `last` takes this frame's boxes before the push reads the previous ones.
    std::array<Box, kProxies> before{};
    std::array<const Box*, kProxies> previous{};
    // Each proxy's speed comes from its last two poses. Outside 1/240..1/20 s
    // apart it is not trusted: no velocity, only easing an overlap out.
    std::array<float, kProxies> dt{};
    QueryStats total{};
    LARGE_INTEGER start{}, end{}, frequency{};
    QueryPerformanceCounter(&start);
    for (unsigned p = 0; p < kProxies; ++p) {
        const auto& current = proxy[p];
        auto& was = last[p];
        if (!current.active) { was = {}; continue; }
        if (was.valid && was.key == current.key && was.reference == current.reference && current.sequence > was.sequence &&
            AngleBetween(current.yaw, was.yaw) < .05f && Length(Sub(current.box.centre, was.box.centre)) < .3f) {
            before[p] = was.box;
            previous[p] = &before[p];
            float d = current.stamp > was.stamp ? static_cast<float>(current.stamp - was.stamp) * 1e-9f : 0.0f;
            dt[p] = d >= 1.0f / 240.0f && d <= 1.0f / 20.0f ? d : 0.0f;
        }
        QueryStats stats{};
        unsigned written = 0;
        const DWORD result = SafeQuery(natives, current.box, previous[p], contacts + count, kMaxContacts - count, written, stats);
        if (result == ERROR_BUFFER_OVERFLOW) {
            ++overflows;
            ++refused;
            written = 0;
        } else if (result) {
            fault = true;
            error = result;
            Log("fault error=" + std::to_string(result) + " detail=query proxy=" + ProxyName(p));
            EndAll("fault");
            return;
        }
        for (unsigned i = count; i < count + written; ++i) { contacts[i].proxy = static_cast<std::uint8_t>(p); }
        count += written;
        total.entities += stats.entities;
        total.other += stats.other;
        was = {current.box, current.key, current.sequence, current.reference, current.stamp, current.yaw, true};
    }
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    const auto us = static_cast<std::uint64_t>((end.QuadPart - start.QuadPart) * 1000000 / std::max<LONGLONG>(frequency.QuadPart, 1));
    ++queries;
    usTotal += us;
    if (us > usMax.load()) { usMax = us; }
    entities += total.entities;
    if (total.entities > entitiesMax.load()) { entitiesMax = total.entities; }
    others += total.other;
    found += count;
    for (unsigned i = 0; i < count; ++i) { (contacts[i].kind == Kind::Sweep ? sweeps : overlaps) += 1; }
    LogEvents(natives, contacts, count);
    Vec3 pushed[kMaxContacts]{};
    Underfoot under{};
    const unsigned pushCount = PushContacts(frame, natives, contacts, count, proxy, previous, dt, pushed, &under);
    Publish(frame, proxy, "open", contacts, count, pushed, pushCount, &under);
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
void SetContactEventLog(unsigned on) { eventLog = on != 0; }
unsigned ContactEventLog() { return eventLog.load() ? 1u : 0u; }
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
           " bodies=" + std::to_string(bodyReady.load()) + " partPushes=" + std::to_string(partPushes.load()) +
           " hosted=" + std::to_string(hostedRefused.load()) + " immovable=" + std::to_string(immovableRefused.load()) +
           " standing=" + std::to_string(standingSkipped.load()) + " ground=" + groundClass +
           " log=" + std::to_string(eventLog.load()) +
           " dvMaxCms=" + std::to_string(dvMaxMm.load() / 10) + " droppedLines=" + std::to_string(dropped.load()) +
           " usAvg=" + std::to_string(q ? usTotal.load() / q : 0) + " usMax=" + std::to_string(usMax.load()) +
           " error=" + std::to_string(error.load()) + " gate=" + lastGate.load();
}
} // namespace preyvr::dll
