// Physical contact, phase 1: the production lane with the engine replaced at its
// ABI boundary -- the broadphase, the per-entity box test, dynamics status and
// the surface tables.
#include "../src/dll/ContactLane.cpp"
#include "TestSupport.h"
#include "preyvr/PhysicalInteractions.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

std::vector<std::string> lines;
namespace preyvr::lifecycle { void Log(std::string_view line) { lines.emplace_back(line); } }
namespace {
using namespace preyvr;
using namespace preyvr::dll;
using preyvr::test::Check;
bool Logged(const char* a, const char* b = "")
{
    for (const auto& l : lines) {
        if (l.find(a) != std::string::npos && l.find(b) != std::string::npos) { return true; }
    }
    return false;
}

// Fake physics objects: each starts with its class's vtable, as the engine's do,
// and carries the rigid fields a push reads (inverse mass +0x2E8, inertia +0x328),
// or an articulated body's part, info and joint arrays (ContactNative.h).
using Slot = void*;
struct Object { Slot* table; unsigned char bytes[0x600]; };
void Put(void* base, std::size_t offset, const void* value, std::size_t size)
{
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, value, size);
}
float InverseMass(const void* self)
{
    float inverse = 0;
    std::memcpy(&inverse, static_cast<const std::uint8_t*>(self) + 0x2E8, 4);
    return inverse;
}
void SetMass(Object& o, float mass)
{
    const float inverse = 1 / mass, inertia[9] = {10 / mass, 0, 0, 0, 10 / mass, 0, 0, 0, 10 / mass};
    std::memcpy(reinterpret_cast<std::uint8_t*>(&o) + 0x2E8, &inverse, 4);
    std::memcpy(reinterpret_cast<std::uint8_t*>(&o) + 0x328, inertia, sizeof(inertia));
}
// pe_status_dynamics: at rest, centre of mass under the contact point, mass from
// the body; the massless one (pinned in place by the game) reports none.
const void* massless = nullptr;
int __fastcall Status(void* self, void* status)
{
    auto* s = static_cast<std::uint8_t*>(status);
    const float inverse = InverseMass(self), mass = inverse > 0 ? 1 / inverse : self == massless ? 0.0f : 2.5f;
    const Vec3 centre{860.5f, 1579.6f, 7.70f};
    std::memcpy(s + 0x3C, &centre, sizeof(centre));
    std::memcpy(s + 0x4C, &mass, 4);
    int type = 0;
    std::memcpy(&type, s, 4);
    return type == 8;
}
struct SetCall { void* entity; phys::SetVelocityAction action; int threadSafe; };
std::vector<SetCall> sets;
int __fastcall Action(void* self, const void* action, int threadSafe)
{
    sets.push_back({self, *static_cast<const phys::SetVelocityAction*>(action), threadSafe});
    return 1;
}
// A two-part ragdoll in CArticulatedEntity's layout: part 3 (an arm, 4 kg) on
// joint 1 and part 7 (the torso, 30 kg) on joint 0, so a lookup must go through
// the infos. Each joint body's centre sits 5 cm under its part's contact point.
struct Part { unsigned char bytes[cn::PartStride]; };
struct Info { unsigned char bytes[cn::InfoStride]; };
struct Joint { unsigned char bytes[cn::JointStride]; };
Part ragParts[2]{};
Info ragInfos[2]{};
Joint ragJoints[2]{};
constexpr int kPartIds[2] = {3, 7}, kPartJoints[2] = {1, 0};
constexpr float kPartMasses[2] = {4, 30};
const Vec3 kPartPoints[2] = {{860.5f, 1579.6f, 7.75f}, {860.9f, 1579.6f, 7.75f}};
std::uint8_t* PartBody(int part) { return ragJoints[kPartJoints[part]].bytes + cn::JointBody; }
void SetPartVelocity(int part, Vec3 v) { Put(PartBody(part), cn::BodyV, &v, sizeof(v)); }
float statusSkew = 0;   // a per-part status that disagrees with the joint body
// The articulated GetStatus: by partid (+4), that part's joint body; else the whole body.
int __fastcall BodyStatus(void*, void* status)
{
    auto* s = static_cast<std::uint8_t*>(status);
    int type = 0, partid = 0;
    std::memcpy(&type, s, 4);
    std::memcpy(&partid, s + 4, 4);
    if (type != 8) { return 0; }
    if (partid == INT_MIN) {
        const float mass = kPartMasses[0] + kPartMasses[1];
        const Vec3 centre{860.7f, 1579.6f, 7.70f};
        std::memcpy(s + 0x3C, &centre, sizeof(centre));
        std::memcpy(s + 0x4C, &mass, 4);
        return 1;
    }
    for (int i = 0; i < 2; ++i) {
        if (kPartIds[i] != partid) { continue; }
        float mass = 0;
        std::memcpy(s + 0x3C, PartBody(i) + cn::BodyCentre, sizeof(Vec3));
        std::memcpy(&mass, PartBody(i) + cn::BodyMass, 4);
        mass += statusSkew;
        std::memcpy(s + 0x4C, &mass, 4);
        return 1;
    }
    return 0;
}
// The player's living GetStatus: pe_status_living (type 2) with bFlying (+4),
// camOffset (+0x0C: the ground 90 cm below the body's position) and the ground
// collider (+0x70) the test sets.
const void* groundCollider = nullptr;
int airborne = 0;
int __fastcall LivingStatus(void*, void* status)
{
    auto* s = static_cast<std::uint8_t*>(status);
    int type = 0;
    std::memcpy(&type, s, 4);
    if (type != cn::StatusLiving) { return 0; }
    const Vec3 down{0, 0, -.9f};
    std::memcpy(s + cn::LivingFlying, &airborne, 4);
    std::memcpy(s + cn::LivingCamOffset, &down, sizeof(down));
    std::memcpy(s + cn::LivingGroundCollider, &groundCollider, sizeof(groundCollider));
    return 1;
}
// The living GetParams: pe_player_dimensions (type 1), heightPivot 0 and a 35 cm
// collider (sizeCollider x).
int __fastcall LivingParams(void*, void* params)
{
    auto* s = static_cast<std::uint8_t*>(params);
    int type = 0;
    std::memcpy(&type, s, 4);
    if (type != cn::ParamsPlayerDimensions) { return 0; }
    const float pivot = 0, size[3] = {.35f, .35f, .6f};
    std::memcpy(s + cn::DimensionsPivot, &pivot, 4);
    std::memcpy(s + cn::DimensionsSize, size, sizeof(size));
    return 1;
}
// What lies under the feet: an object, the height of its surface above the feet,
// how far from the feet (horizontally) it reaches under them, from `inner` to
// `outer`, and which way that surface faces (z of its normal: 1 a top, -1 a
// bottom); the feet's samples seen.
struct Under { void* object; float top, inner, outer, nz = 1; };
std::vector<Under> underFeet;
const Vec3 kFeet{860.5f, 1579.6f, 7.80f};
unsigned footSamplesSeen = 0;
float footSampleReach = 0;
Slot rigidTable[8]{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<Slot>(Status), reinterpret_cast<Slot>(Action)};
Slot articulatedTable[8]{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<Slot>(BodyStatus), reinterpret_cast<Slot>(Action)};
Slot livingTable[8]{nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<Slot>(LivingParams),
                     reinterpret_cast<Slot>(LivingStatus), nullptr};
Slot staticTable[8]{};
Object player{livingTable}, carried{rigidTable}, can{rigidTable}, body{articulatedTable}, wall{staticTable}, crate{rigidTable},
    pinned{rigidTable}, heavyBox{rigidTable};
void BuildRagdoll()
{
    for (int i = 0; i < 2; ++i) {
        Put(&ragParts[i], cn::PartId, &kPartIds[i], 4);
        Put(&ragInfos[i], cn::InfoJoint, &kPartJoints[i], 4);
        const float mass = kPartMasses[i], inverse = 1 / mass, inertia[9] = {10 / mass, 0, 0, 0, 10 / mass, 0, 0, 0, 10 / mass};
        const Vec3 centre = Sub(kPartPoints[i], {0, 0, .05f});
        Put(PartBody(i), cn::BodyCentre, &centre, sizeof(centre));
        Put(PartBody(i), cn::BodyMass, &mass, 4);
        Put(PartBody(i), cn::BodyInverseMass, &inverse, 4);
        Put(PartBody(i), cn::BodyInverseInertia, inertia, sizeof(inertia));
    }
    const void* parts = ragParts;
    const void* infos = ragInfos;
    const void* joints = ragJoints;
    const int count = 2;
    Put(&body, cn::ArticulatedParts, &parts, 8);
    Put(&body, cn::ArticulatedInfos, &infos, 8);
    Put(&body, cn::ArticulatedJoints, &joints, 8);
    Put(&body, cn::ArticulatedPartCount, &count, 4);
    Put(&body, cn::ArticulatedJointCount, &count, 4);
}
void SetHost(const void* host) { Put(&body, cn::ArticulatedHost, &host, 8); }

std::vector<void*> listed;
int boxCalls = 0, objtypes = -1, prealloc = -1, overflowCount = 0;
bool swapBuffer = false;
Vec3 boxLo{}, boxHi{};
int __fastcall EntitiesInBox(std::uintptr_t world, const Vec3* lo, const Vec3* hi, void*** list, int types, int capacity)
{
    ++boxCalls;
    Check(world == 0x5000, "the resolved world");
    boxLo = *lo;
    boxHi = *hi;
    objtypes = types;
    prealloc = capacity;
    if (overflowCount) { return overflowCount; }
    static void* shared[4]{};
    if (swapBuffer) { *list = shared; return 1; }
    Check(list && *list, "the caller's own buffer");
    for (std::size_t i = 0; i < listed.size(); ++i) { (*list)[i] = listed[i]; }
    return static_cast<int>(listed.size());
}
struct Call { void* entity; int itype; contact::BoxPrimitive primitive; Vec3 motion; };
std::vector<Call> calls;
int __fastcall Collide(std::uintptr_t, void* entity, int itype, void* shape, const Vec3* motion, scene::Hit* out, void* ip)
{
    Check(!ip, "the engine builds its own intersection parameters");
    // The feet's samples: a still sphere, touching the object where its surface
    // runs through it.
    if (itype == cn::PrimitiveSphere) {
        const auto* sphere = static_cast<const contact::SpherePrimitive*>(shape);
        Check(Length(*motion) == 0 && sphere->radius == kFootSphere &&
              std::fabs(sphere->centre.z - (kFeet.z - kFootSphereDown)) < 1e-4f, "a feet sample: a still sphere at the feet");
        ++footSamplesSeen;
        const float reach = std::hypot(sphere->centre.x - kFeet.x, sphere->centre.y - kFeet.y);
        footSampleReach = std::max(footSampleReach, reach);
        for (const auto& u : underFeet) {
            const float surface = kFeet.z + u.top;
            if (u.object != entity || reach < u.inner - 1e-4f || reach > u.outer + 1e-4f ||
                std::fabs(surface - sphere->centre.z) > sphere->radius) {
                continue;
            }
            out->distance = sphere->radius - std::fabs(surface - sphere->centre.z);
            out->collider = entity;
            out->point = {sphere->centre.x, sphere->centre.y, surface};
            out->normal = {0, 0, u.nz};
            return 1;
        }
        return 0;
    }
    auto* primitive = static_cast<contact::BoxPrimitive*>(shape);
    calls.push_back({entity, itype, *primitive, *motion});
    primitive->centre = {};   // the engine's own scratch use: the next call must not see it
    const bool sweeping = Length(*motion) > 0;
    if (entity == &wall) { return 1; }  // never asked: not a movable class
    if (((entity == &can || entity == &body || entity == &pinned) && !sweeping) || (entity == &crate && sweeping)) {
        out->distance = sweeping ? .03f : .002f;
        out->collider = entity;
        out->partId = 3;
        out->surface = 41;
        out->point = {860.5f, 1579.6f, 7.75f};
        out->normal = {0, 0, 1};
        return 1;
    }
    return 0;
}
int __fastcall Surface(std::uintptr_t, int surface, float* bounciness, float* friction, unsigned* flags)
{
    *bounciness = .1f;
    *friction = surface == 41 ? .6f : 0;
    *flags = 0;
    return surface < 512;
}

DWORD gReady = ERROR_SUCCESS;
bool bodiesGated = true;
DWORD FakeResolve(const GameplayPoseFrame&, Natives& n)
{
    n = {};
    if (gReady) { return gReady; }
    n.world = 0x5000;
    n.player = &player;
    n.carried = &carried;
    n.rigid = reinterpret_cast<std::uintptr_t>(rigidTable);
    n.articulated = reinterpret_cast<std::uintptr_t>(articulatedTable);
    n.entitiesInBox = EntitiesInBox;
    n.collide = Collide;
    n.surface = Surface;
    n.push = true;
    n.bodies = bodiesGated;
    n.living = reinterpret_cast<std::uintptr_t>(livingTable);
    return ERROR_SUCCESS;
}

// The rest of the DLL, as the lane sees it.
bool menu = false, haveBox = true;
WeaponBox weapon{};
ContactOverlayState lastOverlay{};
unsigned overlays = 0;
}
namespace preyvr::dll {
unsigned long InputDrainThreadId() { return GetCurrentThreadId(); }
bool HudGameplayInputAllowed() { return !menu; }
unsigned long long HeadTrackingReferenceGeneration() { return 2; }
bool TryGetWeaponBox(WeaponBox& out) { out = weapon; return haveBox; }
DWORD SceneQueryWorld(const GameplayPoseFrame&, std::uintptr_t&, void*&) { return ERROR_NOT_READY; }
std::uintptr_t CarriedPhysics() { return 0; }
bool DebugOverlayWantsPhysical() { return true; }
void DebugOverlayContact(const GameplayPoseFrame&, const ContactOverlayState& s) { lastOverlay = s; ++overlays; }
unsigned pulses = 0, leftPulses = 0;
void QueueHaptic(Hand hand, haptics::Event event, const TrackingFrame&)
{
    Check(event == haptics::Event::WeaponContact, "a push pulses as a contact");
    ++(hand == Hand::left ? leftPulses : pulses);
}
bool carrying = false, foregrip = false, medkit = false, psi = false;
bool UseCarrying() { return carrying; }
bool CarryHoldingInHand() { return false; }
bool TwoHandedAimHeld() { return foregrip; }
bool MedkitZoneHasLeftHand() { return medkit; }
bool PsiButtonHeld() { return psi; }
bool SwingRecentlyStruck(const void*) { return false; }
}

int main()
{
    SetMass(can, 1);
    SetMass(crate, 10000);
    BuildRagdoll();
    const Vec3 playerAt{860.5f, 1579.6f, 8.70f};   // its feet at z 7.80, by camOffset
    Put(&player, cn::EntityPosition, &playerAt, sizeof(playerAt));
    // --- the query itself ---
    Natives n{};
    FakeResolve({}, n);
    contact::Box now{}, before{};
    const auto local = physical::Bounds({-.05f, -.05f, -.1f}, {.05f, .05f, .3f});
    std::array<Vec3, 9> points{}, earlier{};
    for (unsigned i = 0; i < 9; ++i) {
        points[i] = Add({860.5f, 1579.6f, 7.8f}, local[i]);
        earlier[i] = Add(points[i], {-.02f, 0, 0});
    }
    contact::BoxFromPoints(points, now);
    contact::BoxFromPoints(earlier, before);
    listed = {&player, &carried, &can, &body, &wall, &crate, nullptr};
    contact::Contact out[8]{};
    QueryStats stats{};
    unsigned count = Query(n, now, &before, out, 8, stats);
    Check(objtypes == cn::MovableObjects && prealloc == static_cast<int>(kMaxEntities), "movable classes, into our own buffer");
    Check(boxLo.x < before.centre.x - .05f && boxHi.x > now.centre.x + .05f, "the broadphase covers both poses");
    Check(stats.entities == 7 && stats.skipped == 3 && stats.other == 1, "player, carried and null skipped; the wall is not movable");
    bool askedSkipped = false;
    for (const auto& c : calls) { askedSkipped |= c.entity == &player || c.entity == &carried || c.entity == &wall; }
    Check(!askedSkipped && calls.size() == 6, "an overlap and a sweep for each of the three movable objects");
    bool centres = true, still = true, swept = true;
    for (const auto& c : calls) {
        const bool sweeping = Length(c.motion) > 0;
        centres &= std::fabs(c.primitive.centre.x - (sweeping ? before.centre.x : now.centre.x)) < 1e-4f && c.itype == cn::PrimitiveBox &&
                   c.primitive.oriented == 1;
        if (!sweeping) { still &= c.motion.x == 0 && c.motion.y == 0 && c.motion.z == 0; }
        else { swept &= std::fabs(c.motion.x - .02f) < 1e-4f; }
    }
    Check(centres, "every call gets its own primitive: overlap at this pose, sweep from the last");
    Check(still && swept, "overlap still, sweep along this frame's motion");
    Check(count == 3 && out[0].collider == &can && out[0].kind == contact::Kind::Overlap && out[0].type == cn::TypeRigid &&
          out[1].collider == &body && out[1].type == cn::TypeArticulated && out[2].collider == &crate &&
          out[2].kind == contact::Kind::Sweep, "contacts by object, class and kind");
    calls.clear();
    count = Query(n, now, nullptr, out, 8, stats);
    Check(calls.size() == 3 && count == 2, "no previous pose: overlap only");
    calls.clear();
    overflowCount = 40;
    count = Query(n, now, nullptr, out, 8, stats);
    Check(count == 0 && stats.error == ERROR_BUFFER_OVERFLOW && calls.empty(), "more objects than the buffer: refused");
    overflowCount = 0;
    swapBuffer = true;
    count = Query(n, now, nullptr, out, 8, stats);
    Check(count == 0 && stats.error == ERROR_BUFFER_OVERFLOW && calls.empty(), "the engine's shared buffer is never read");
    swapBuffer = false;

    // --- the lane ---
    gResolve = FakeResolve;
    GameplayPoseFrame f{};
    f.player = 1;
    f.cameraCentreValid = true;
    f.headYawUsable = true;
    f.referenceGeneration = 2;
    f.tracking.epoch = 1;
    weapon.points = points;
    weapon.rig = 7;
    weapon.reference = 2;
    weapon.epoch = 1;
    weapon.stamp = 1000000000ull;
    auto move = [&](Vec3 d) {
        for (auto& p : weapon.points) { p = Add(p, d); }
        ++weapon.sequence;
        weapon.stamp += 11111111ull;   // 90 Hz
        ++f.tracking.sequence;
        f.tracking.displayTime += 11111111ll;
        weapon.publishedNs = f.tracking.publishedNs = MonotonicNanoseconds();
        UpdateContactLane(f, true);
    };
    auto tick = [&](float dx) { move({dx, 0, 0}); };
    Check(PhysicalContactEnabled() == 1 && ContactEventLog() == 0, "physical contact starts on, its event lines off");
    SetPhysicalContact(0);
    SetContactEventLog(1);   // most checks below read the event lines
    listed = {&can, &crate};
    tick(0);
    Check(Logged("preyvr_contact settings enabled=0") && overlays == 1 && !std::strcmp(lastOverlay.gate, "off"),
          "off: nothing queried, the gate says so");
    SetPhysicalContact(1);
    calls.clear();
    tick(0);
    Check(Logged("preyvr_contact gate result=0") && Logged("begin entity=", "type=rigid kind=overlap") &&
          Logged("friction=0.60", "mass=1.00"), "a touched can begins, with its mass and friction");
    Check(lastOverlay.box && lastOverlay.open && lastOverlay.count == 1 && !lastOverlay.marks[0].sweep, "the overlay gets the box and the contact");
    Check(calls.size() == 2, "first frame: overlap only, no previous pose");
    calls.clear();
    tick(.02f);
    Check(calls.size() == 4 && Logged("type=rigid kind=sweep"), "next frame: the crate is swept into");
    listed = {};
    for (int i = 0; i < 3; ++i) { tick(.01f); }
    Check(Logged("end entity=", "frames=2") && Logged("end entity=", "frames=1"), "both end after the grace frames");
    listed = {&can};
    tick(0);
    menu = true;
    tick(0);
    Check(Logged("end entity=", "why=menu") && !std::strcmp(lastOverlay.gate, "menu"), "a menu ends contact at once");
    menu = false;
    // --- pushing ---
    // Sideways past the can's top (normal up) pushes nothing: no approach, no
    // overlap past the slop.
    Check(sets.empty() && pulses == 0, "touching and sliding past: nothing pushed");
    listed = {&can};
    for (int i = 0; i < 4; ++i) { tick(0); }
    sets.clear();
    move({0, 0, -.01f});   // down into the can at 0.9 m/s
    Check(sets.size() == 1 && sets[0].entity == &can && sets[0].threadSafe == 1 && sets[0].action.type == 10 &&
          sets[0].action.v.z < -.8f && sets[0].action.v.z > -1, "pressing down sets the can's velocity to follow the weapon");
    Check(Logged("push entity=", "mass=1.00") && Logged("push entity=", "approachCms=90") && pulses == 1,
          "a push starts with a log line and a pulse");
    Check(lastOverlay.count == 1 && lastOverlay.marks[0].pushed && lastOverlay.marks[0].push.z < 0, "the overlay shows the push");
    move({0, 0, -.01f});
    Check(sets.size() == 2 && pulses == 1, "pushing on: no new pulse or line");
    // --- corpses ---
    // Pressing down on a corpse moves the touched part alone, by its partid, as
    // hard as its own 4 kg allows.
    listed = {&body};
    sets.clear();
    logSecondNs = 0;
    move({0, 0, -.01f});
    Check(sets.size() == 1 && sets[0].entity == &body && sets[0].action.partid == 3 && sets[0].action.ipart == INT_MIN &&
          sets[0].action.v.z < -.8f && sets[0].action.v.z > -1, "pressing on a corpse sets the touched part's velocity");
    Check(Logged("begin entity=", "type=articulated") && Logged("type=articulated", "host=0") &&
          Logged("push entity=", "type=articulated part=3") && Logged("part=3", "mass=4.00"),
          "a corpse begins with no host; its push names the part and that part's mass");
    // The part's own live velocity is what the solver starts from: already
    // moving with the weapon, it needs no push.
    SetPartVelocity(0, sets[0].action.v);
    sets.clear();
    move({0, 0, -.01f});
    Check(sets.empty(), "a part keeping up with the weapon is left alone");
    SetPartVelocity(0, {});
    // A live character's skeleton rides on a host: never pushed.
    SetHost(&player);
    move({0, 0, -.01f});
    Check(sets.empty() && ContactReport().find(" hosted=1 ") != std::string::npos, "a body with a host is never pushed");
    SetHost(nullptr);
    // A status that disagrees with the joint body: the layout is not trusted.
    statusSkew = 1;
    const auto failedBefore = pushFailed.load();
    move({0, 0, -.01f});
    Check(sets.empty() && pushFailed.load() == failedBefore + 1, "a part whose status disagrees is not pushed");
    statusSkew = 0;
    // The articulated gate failed: props still push, bodies do not.
    bodiesGated = false;
    move({0, 0, -.01f});
    Check(sets.empty() && lastOverlay.status.find("(props only)") != std::string::npos, "no body gate: corpses are left alone");
    bodiesGated = true;
    move({0, 0, -.01f});
    Check(sets.size() == 1 && sets[0].action.partid == 3, "and with it, pushed again");
    // Two boxes on two parts of one corpse push both; two boxes on one part push it once.
    {
        Natives nb{};
        FakeResolve({}, nb);
        Proxies px{};
        std::array<Box, kProxies> was{};
        std::array<const Box*, kProxies> prior{};
        std::array<float, kProxies> step{};
        for (unsigned p : {static_cast<unsigned>(kWeapon), static_cast<unsigned>(kLeftHand)}) {
            px[p].active = true;
            px[p].box = now;
            was[p] = now;
            was[p].centre = Add(now.centre, {0, 0, .01f});   // came down 1 cm in a 90 Hz frame
            prior[p] = &was[p];
            step[p] = 1.0f / 90.0f;
        }
        auto hitOn = [](int part) {
            scene::Hit h{};
            h.distance = .002f;
            h.collider = &body;
            h.partId = kPartIds[part];
            h.surface = 41;
            h.point = kPartPoints[part];
            h.normal = {0, 0, 1};
            return h;
        };
        Contact both[2] = {{&body, cn::TypeArticulated, Kind::Overlap, hitOn(0), kWeapon},
                           {&body, cn::TypeArticulated, Kind::Overlap, hitOn(1), kLeftHand}};
        Vec3 changes[2]{};
        sets.clear();
        Check(PushContacts(f, nb, both, 2, px, prior, step, changes) == 2 && sets.size() == 2 && sets[0].action.partid == 3 &&
              sets[1].action.partid == 7, "two parts, two pushes");
        Check(sets[0].action.v.z < -.8f && sets[1].action.v.z < 0 && sets[1].action.v.z > -.2f,
              "the arm keeps up; the 30 kg torso resists (400 N for a frame)");
        both[1].hit = hitOn(0);
        both[1].hit.distance = .001f;
        sets.clear();
        Check(PushContacts(f, nb, both, 2, px, prior, step, changes) == 1 && sets.size() == 1 && sets[0].action.partid == 3,
              "one part, one push: the deeper contact's");
    }
    // The 10,000 kg crate swept into from above: limited to almost nothing.
    listed = {&crate};
    move({0, 0, -.01f});
    move({0, 0, -.01f});
    Check(!sets.empty() && sets.back().entity == &crate && Length(sets.back().action.v) < .01f && Logged("push entity=", "limited=1"),
          "a 10,000 kg crate barely moves");
    // --- what the player stands on ---
    // Standing on the can, pressing down on it pushes nothing.
    listed = {&can};
    groundCollider = &can;
    sets.clear();
    logSecondNs = 0;
    const auto standingBefore = standingSkipped.load();
    move({0, 0, -.01f});
    Check(sets.empty() && standingSkipped.load() == standingBefore + 1 && Logged("preyvr_contact ground player=living") &&
          Logged("preyvr_contact stand entity=", "via=engine"),
          "what the player stands on is never pushed; the player's class is logged once, the stand as an event");
    // Jumping off (airborne, the feet 10 cm up, nothing underfoot): held a moment,
    // in case a frame was missed, then just a can again.
    groundCollider = nullptr;
    airborne = 1;
    const Vec3 jumped = Add(playerAt, {0, 0, .10f});
    Put(&player, cn::EntityPosition, &jumped, sizeof(jumped));
    sets.clear();
    for (std::uint64_t i = 0; i < kStandHoldFrames; ++i) { move({0, 0, -.01f}); }
    Check(sets.empty(), "what was underfoot is held for a moment");
    logSecondNs = 0;
    move({0, 0, -.01f});
    Check(sets.size() == 1 && sets[0].entity == &can && Logged("preyvr_contact stand_end entity="), "then it pushes again");
    airborne = 0;
    Put(&player, cn::EntityPosition, &playerAt, sizeof(playerAt));
    groundCollider = &body;
    listed = {&body};
    sets.clear();
    move({0, 0, -.01f});
    Check(sets.empty(), "standing on a corpse: none of its parts are pushed");
    groundCollider = nullptr;
    for (std::uint64_t i = 0; i < kStandHoldFrames; ++i) { move({0, 0, -.01f}); }
    // Standing still, the engine lets go of its ground collider (the PreyVR-28
    // run); the feet's samples still find the can's top. Feet at z 7.80, the head
    // 1.70 m above them; the samples reach the 35 cm collider's edge.
    f.cameraCentre = {860.5f, 1579.6f, 9.50f};
    underFeet = {{&can, 0.0f, 0.0f, 1.0f}};
    listed = {&can};
    sets.clear();
    footSamplesSeen = 0;
    footSampleReach = 0;
    const auto probedBefore = standingSkipped.load();
    logSecondNs = 0;
    move({0, 0, -.01f});
    Check(sets.empty() && standingSkipped.load() == probedBefore + 1 && lastOverlay.feet && lastOverlay.standing &&
          Logged("stand entity=", "via=feet"),
          "with no ground collider, the feet's samples keep what the player stands on from being pushed");
    Check(footSamplesSeen <= kFootSamples && std::fabs(footSampleReach) < 1e-4f && Logged("preyvr_contact ground player=living", "radius=0.35"),
          "the centre sample finds it first; the player's collider radius is logged once");
    Check(std::fabs(lastOverlay.feetBox[0].z - 7.80f) < 1e-3f && std::fabs(lastOverlay.feetBox[0].x - 860.5f) < 1e-3f,
          "the overlay draws the samples' footprint at the feet");
    // Standing still on it, the samples missing: held as long as the feet stay put,
    // even while the engine calls the player airborne (PreyVR-33: on a light object
    // it does, in bursts).
    underFeet = {};
    airborne = 1;
    for (std::uint64_t i = 0; i < 3 * kStandHoldFrames; ++i) { move({0, 0, -.01f}); }
    Check(sets.empty(), "standing still, what was underfoot stays underfoot, airborne flag or not");
    airborne = 0;
    // Stepping 15 cm away: held a moment more, then just a can.
    const Vec3 stepped = Add(playerAt, {.15f, 0, 0});
    Put(&player, cn::EntityPosition, &stepped, sizeof(stepped));
    for (std::uint64_t i = 0; i < kStandHoldFrames; ++i) { move({0, 0, -.01f}); }
    Check(sets.empty(), "after stepping off, held a moment");
    move({0, 0, -.01f});
    Check(sets.size() == 1, "then it pushes");
    Put(&player, cn::EntityPosition, &playerAt, sizeof(playerAt));
    for (std::uint64_t i = 0; i < kStandHoldFrames; ++i) { move({0, 0, -.01f}); }
    // On its edge: only the outer samples find it -- the case the wearer hit --
    // with the engine calling the player airborne.
    underFeet = {{&can, 0.0f, .3f, 1.0f}};
    airborne = 1;
    sets.clear();
    move({0, 0, -.01f});
    Check(sets.empty() && lastOverlay.standing, "standing on its edge, the outer samples still find it, airborne flag or not");
    airborne = 0;
    underFeet = {};
    Put(&player, cn::EntityPosition, &stepped, sizeof(stepped));
    for (std::uint64_t i = 0; i <= kStandHoldFrames; ++i) { move({0, 0, -.01f}); }
    Put(&player, cn::EntityPosition, &playerAt, sizeof(playerAt));
    // A can whose top is 3 cm above the feet, or whose bottom is at them (beside
    // the feet on the same floor), pushes; so does one the samples miss.
    underFeet = {{&can, .03f, 0.0f, 1.0f}};
    sets.clear();
    logSecondNs = 0;
    move({0, 0, -.01f});
    Check(sets.size() == 1 && lastOverlay.feet && !lastOverlay.standing, "a top above the feet pushes; the footprint shows grey");
    underFeet = {{&can, 0.0f, 0.0f, 1.0f, -1.0f}};
    sets.clear();
    footSamplesSeen = 0;
    footSampleReach = 0;
    move({0, 0, -.01f});
    Check(sets.size() == 1 && !lastOverlay.standing && footSamplesSeen == kFootSamples && std::fabs(footSampleReach - .35f) < 1e-4f,
          "a bottom at the feet pushes, after all 13 samples, out to the collider radius, found no top");
    underFeet = {};
    sets.clear();
    move({0, 0, -.01f});
    Check(sets.size() == 1 && !lastOverlay.standing, "what the samples miss pushes");
    // Feet implausibly far below the head: no samples.
    underFeet = {{&can, 0.0f, 0.0f, 1.0f}};
    f.cameraCentre.z = 20;
    sets.clear();
    footSamplesSeen = 0;
    move({0, 0, -.01f});
    Check(sets.size() == 1 && !lastOverlay.feet && footSamplesSeen == 0, "feet implausibly far below the head: no samples");
    f.cameraCentre = {};
    underFeet = {};
    // A prop the game gave no mass is pinned in place: refused, not a failure.
    massless = &pinned;
    listed = {&pinned};
    const auto failedBefore2 = pushFailed.load(), immovableBefore = immovableRefused.load();
    sets.clear();
    move({0, 0, -.01f});
    Check(sets.empty() && immovableRefused.load() == immovableBefore + 1 && pushFailed.load() == failedBefore2 &&
          ContactReport().find(" immovable=1 ") != std::string::npos, "a massless prop is immovable, not a failure");
    // With the event lines off, contacts and pushes are counted but not logged.
    SetContactEventLog(0);
    listed = {};
    for (int i = 0; i < 3; ++i) { tick(0); }
    lines.clear();
    const auto beginsBefore = begins.load(), pushesBefore = pushes.load();
    listed = {&can};
    move({0, 0, -.01f});
    Check(begins.load() == beginsBefore + 1 && pushes.load() == pushesBefore + 1 && !Logged("begin entity=") &&
          !Logged("push entity=") && !Logged("end entity="), "event lines off: counted, not logged");
    SetContactEventLog(1);
    // --- hands ---
    // No weapon drawn: the right hand's own box pushes, and that hand pulses.
    const PoseValidity tracked{true, true, true, true, 0};
    auto& right = f.tracking.hands[static_cast<unsigned>(Hand::right)];
    auto& left = f.tracking.hands[static_cast<unsigned>(Hand::left)];
    f.tracking.head = {{0, 0, 0, 1}, {0, 1.6f, 0}};
    f.tracking.headValidity = tracked;
    right.gripPose = {{0, 0, 0, 1}, {.2f, 1.2f, -.3f}};
    right.gripValidity = tracked;
    haveBox = false;
    listed = {&can};
    auto hands = [&](Vec3 d) {
        right.gripPose.position = Add(right.gripPose.position, d);
        left.gripPose.position = Add(left.gripPose.position, d);
        move({});
    };
    sets.clear();
    for (int i = 0; i < 31; ++i) { hands({}); }   // the can's last push lapses
    Check(sets.empty(), "a hand resting on the can pushes nothing (float noise and jitter are below the deadband)");
    const unsigned pulsesBefore = pulses;
    logSecondNs = 0;   // this test runs faster than the 40-lines-a-second budget
    hands({0, -.01f, 0});   // OpenXR down: engine -z, into the can's top
    Check(Logged("push entity=", "proxy=right_hand"), "with no weapon drawn the right hand's box pushes the can");
    Check(!sets.empty() && sets.back().entity == &can && sets.back().action.v.z < -.8f && pulses == pulsesBefore + 1,
          "the right hand pushes it down, and the right hand pulses");
    Check(lastOverlay.handBox[0] && !lastOverlay.handBox[1] && !lastOverlay.box, "the overlay shows the right hand's box alone");
    // The left hand, free: its own box, its own pulse.
    right.gripValidity = {};
    left.gripPose = {{0, 0, 0, 1}, {-.2f, 1.2f, -.3f}};
    left.gripValidity = tracked;
    for (int i = 0; i < 31; ++i) { hands({}); }
    sets.clear();
    logSecondNs = 0;
    hands({0, -.01f, 0});
    Check(!sets.empty() && leftPulses == 1 && Logged("push entity=", "proxy=left_hand"), "the free left hand pushes, and pulses itself");
    // A busy left hand pushes nothing: carrying, the foregrip, the medkit slot, psi.
    for (bool* busy : {&carrying, &foregrip, &medkit, &psi}) {
        *busy = true;
        sets.clear();
        hands({0, -.01f, 0});
        Check(sets.empty() && !lastOverlay.handBox[1] && !std::strcmp(lastOverlay.gate, "idle"), "a busy left hand pushes nothing");
        *busy = false;
    }
    // Weapon and left hand on one object: one push, from the deepest contact.
    haveBox = true;
    for (int i = 0; i < 2; ++i) { hands({}); }
    sets.clear();
    hands({0, -.01f, 0});
    unsigned canSets = 0;
    for (const auto& s : sets) { canSets += s.entity == &can; }
    Check(canSets <= 1 && lastOverlay.box && lastOverlay.handBox[1] && !lastOverlay.handBox[0],
          "weapon drawn: its box and the left hand's; one push per object a frame");
    left.gripValidity = {};
    // --- presses: a heavy prop holding a drawn hand back (SurfaceStopLane.h) ---
    {
        const auto rightHand = static_cast<unsigned>(Hand::right);
        SetMass(heavyBox, 15);
        underFeet.clear();
        groundCollider = nullptr;
        listed = {};
        for (int i = 0; i < 31; ++i) { tick(0); }   // the pushes above lapse
        sets.clear();
        logSecondNs = 0;
        const unsigned pulsesAtPress = pulses + leftPulses;
        // The real hand 5 cm past the box's face (normal -x): pushed away along +x,
        // asking 0.5 m/s, as hard as the hand can (400 N on 15 kg: 0.3 m/s a frame).
        f.surface[rightHand].press = {&heavyBox, {860.6f, 1579.6f, 7.8f}, {-1, 0, 0}, .05f};
        tick(0);
        Check(sets.size() == 1 && sets[0].entity == &heavyBox && sets[0].action.v.x > .25f && sets[0].action.v.x < .31f &&
                  std::fabs(sets[0].action.v.y) < .01f && std::fabs(sets[0].action.v.z) < .01f,
              "a press pushes the heavy box away from the hand, as hard as the hand can");
        Check(pulses + leftPulses == pulsesAtPress && Logged("push entity=", "kind=press") && Logged("kind=press", "depthMm=50.0"),
              "a press line, and no pulse: a hand stopped at a surface gives none");
        Check(ContactReport().find(" pressed=1 pressPushes=1 ") != std::string::npos, "counted as a press");
        // A press and the weapon's overlap on one object: one push, the press's.
        listed = {&can};
        f.surface[rightHand].press = {&can, {860.5f, 1579.6f, 7.75f}, {0, 0, 1}, .05f};
        sets.clear();
        tick(0);
        unsigned canPushes = 0;
        for (const auto& s : sets) { canPushes += s.entity == &can; }
        Check(canPushes == 1 && sets.back().action.v.z < -.45f && sets.back().action.v.z > -.55f,
              "a press beats an overlap on the same object: one push, into its surface at 0.5 m/s");
        // Something that is not a loose prop is never pushed by a press.
        listed = {};
        f.surface[rightHand].press = {&wall, {860.5f, 1579.6f, 7.75f}, {0, 0, 1}, .05f};
        sets.clear();
        tick(0);
        Check(sets.empty(), "a press on scenery pushes nothing");
        f.surface[rightHand].press = {};
    }
    SetPushStrength(10);
    tick(0);
    Check(PushStrength() == 25 && Logged("settings enabled=1 strength=25"), "strength clamps to 25..200 percent");
    SetPushStrength(100);
    gReady = ERROR_INVALID_ADDRESS;
    tick(0);
    Check(Logged("fault error=", "detail=resolve") && std::string(ContactReport()).find("fault=1") != std::string::npos,
          "a bad engine contract latches the lane off");
    tick(0);
    Check(!std::strcmp(lastOverlay.gate, "fault"), "and it stays off");
    std::cout << "contact_lane: query, buffer, classes, kinds, events and gates passed\n";
}
