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
// and carries the rigid fields a push reads (inverse mass +0x2E8, inertia +0x328).
using Slot = void*;
struct Object { Slot* table; unsigned char bytes[0x400]; };
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
// pe_status_dynamics: at rest, centre of mass under the contact point, mass from the body.
int __fastcall Status(void* self, void* status)
{
    auto* s = static_cast<std::uint8_t*>(status);
    const float inverse = InverseMass(self), mass = inverse > 0 ? 1 / inverse : 2.5f;
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
Slot rigidTable[8]{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<Slot>(Status), reinterpret_cast<Slot>(Action)};
Slot articulatedTable[8]{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<Slot>(Status), reinterpret_cast<Slot>(Action)};
Slot staticTable[8]{};
Object player{rigidTable}, carried{rigidTable}, can{rigidTable}, body{articulatedTable}, wall{staticTable}, crate{rigidTable};

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
int __fastcall Collide(std::uintptr_t, void* entity, int itype, contact::BoxPrimitive* primitive, const Vec3* motion,
                       scene::Hit* out, void* ip)
{
    Check(!ip, "the engine builds its own intersection parameters");
    calls.push_back({entity, itype, *primitive, *motion});
    primitive->centre = {};   // the engine's own scratch use: the next call must not see it
    const bool sweeping = Length(*motion) > 0;
    if (entity == &wall) { return 1; }  // never asked: not a movable class
    if ((entity == &can && !sweeping) || (entity == &body && !sweeping) || (entity == &crate && sweeping)) {
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
bool BodyHeadOut() { return false; }
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
}

int main()
{
    SetMass(can, 1);
    SetMass(crate, 10000);
    SetMass(body, 40);
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
    listed = {&body};
    sets.clear();
    move({0, 0, -.01f});
    Check(sets.empty(), "bodies are not pushed yet (phase 4)");
    // The 10,000 kg crate swept into from above: limited to almost nothing.
    listed = {&crate};
    move({0, 0, -.01f});
    move({0, 0, -.01f});
    Check(!sets.empty() && sets.back().entity == &crate && Length(sets.back().action.v) < .01f && Logged("push entity=", "limited=1"),
          "a 10,000 kg crate barely moves");
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
