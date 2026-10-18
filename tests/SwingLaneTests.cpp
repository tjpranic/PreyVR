// The free-swing wrench: the production lane with Prey replaced at its ABI
// boundary -- the broadphase, the per-entity box test, the physical entities'
// interface, the entity system, the surface types, stamina, the scenery ray, and
// (phase 2) the hit, its fatigue setter, Prey's stamina check and the four hooks'
// originals.
#include "../src/dll/SwingLane.cpp"
#include "TestSupport.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

std::vector<std::string> lines;
namespace preyvr::lifecycle { void Log(std::string_view line) { lines.emplace_back(line); } }
namespace {
using namespace preyvr;
using namespace preyvr::dll;
using preyvr::test::Check;
bool Logged(const char* a, const char* b = "", const char* c = "")
{
    for (const auto& l : lines) {
        if (l.find(a) != std::string::npos && l.find(b) != std::string::npos && l.find(c) != std::string::npos) { return true; }
    }
    return false;
}
unsigned Count(const char* a, const char* b = "")
{
    unsigned n = 0;
    for (const auto& l : lines) { n += l.find(a) != std::string::npos && l.find(b) != std::string::npos; }
    return n;
}
void Put(void* base, std::size_t offset, const void* value, std::size_t size)
{
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, value, size);
}

// Fake physical entities, each starting with its class's vtable: GetType at
// +0x08 and the foreign-data kind at +0x70, as the engine's answer them.
using Slot = void*;
struct Object { Slot* table; unsigned char bytes[0x600]; };
int __fastcall StaticType(void*) { return 1; }
int __fastcall RigidType(void*) { return 2; }
int __fastcall BodyType(void*) { return 6; }
int __fastcall LivingType(void*) { return 4; }
int __fastcall SceneryKind(void*) { return 1; }
int __fastcall EntityKind(void*) { return 2; }
Slot staticTable[32]{}, rigidTable[32]{}, bodyTable[32]{}, livingTable[32]{};
Object wall{staticTable, {}}, glass{staticTable, {}}, crate{rigidTable, {}}, body{bodyTable, {}}, capsule{livingTable, {}};
Object self{rigidTable, {}}, carried{rigidTable, {}}, blocker{staticTable, {}};
// The player's own character skeleton: found as the player's entity, or by its host.
Object selfBody{bodyTable, {}}, selfHosted{bodyTable, {}};
void Tables()
{
    for (Slot* t : {staticTable, rigidTable, bodyTable, livingTable}) {
        t[0x70 / 8] = reinterpret_cast<Slot>(t == staticTable ? SceneryKind : EntityKind);
    }
    staticTable[1] = reinterpret_cast<Slot>(StaticType);
    rigidTable[1] = reinterpret_cast<Slot>(RigidType);
    bodyTable[1] = reinterpret_cast<Slot>(BodyType);
    livingTable[1] = reinterpret_cast<Slot>(LivingType);
}

// The broadphase hands out `listed`; the box test answers per object and kind of
// call: still (overlap), the body's sweep, or the head's (a box at most 7 cm half).
std::vector<void*> listed;
int objtypes = 0, prealloc = 0, overflowCount = 0;
int __fastcall Broadphase(std::uintptr_t, const Vec3*, const Vec3*, void*** list, int types, int pre)
{
    objtypes = types;
    prealloc = pre;
    if (overflowCount) { return overflowCount; }
    for (std::size_t i = 0; i < listed.size() && static_cast<int>(i) < pre; ++i) { (*list)[i] = listed[i]; }
    return static_cast<int>(listed.size());
}
struct Response { bool overlap = false, sweep = false, head = false; Vec3 point{}; short surface = 0; int partId = 0; };
std::map<const void*, Response> responses;
struct Call { void* entity; Kind kind; };
std::vector<Call> calls;
int __fastcall FakeCollide(std::uintptr_t, void* entity, int itype, void* primitive, const Vec3* sweep, scene::Hit* out, void*)
{
    Check(itype == cn::PrimitiveBox, "the wrench is a box");
    const auto& p = *static_cast<const contact::BoxPrimitive*>(primitive);
    const float longest = std::max({p.half.x, p.half.y, p.half.z});
    const Kind kind = Length(*sweep) == 0 ? Kind::overlap : longest <= .07f ? Kind::head : Kind::sweep;
    calls.push_back({entity, kind});
    const auto it = responses.find(entity);
    if (it == responses.end()) { return 0; }
    const auto& r = it->second;
    if (!(kind == Kind::overlap ? r.overlap : kind == Kind::sweep ? r.sweep : r.head)) { return 0; }
    *out = {};
    out->collider = entity;
    out->distance = kind == Kind::overlap ? 0.0f : .5f;
    out->point = r.point;
    out->normal = {-1, 0, 0};
    out->surface = r.surface;
    out->partId = r.partId;
    return 1;
}

// The entity system: the crate and the body are entities, the scenery is not.
struct FakeClass { Slot* table; const char* name; };
struct FakeEntity { Slot* table; unsigned id; const char* name; FakeClass* cls; };
const char* __fastcall ClassName(void* c) { return static_cast<FakeClass*>(c)->name; }
unsigned __fastcall EntityId(void* e) { return static_cast<FakeEntity*>(e)->id; }
void* __fastcall EntityClass(void* e) { return static_cast<FakeEntity*>(e)->cls; }
const char* __fastcall EntityName(void* e) { return static_cast<FakeEntity*>(e)->name; }
Slot classTable[8]{}, entityTable[32]{}, systemTable[64]{};
FakeClass propClass{classTable, "ArkPhysicsProp"}, npcClass{classTable, "ArkNpcMimic"};
FakeClass playerClass{classTable, "ArkPlayer"};
FakeEntity crateEntity{entityTable, 42, "Crate 1", &propClass}, bodyEntity{entityTable, 77, "Mimic3", &npcClass};
FakeEntity playerEntity{entityTable, 0x7777, "Dude", &playerClass};
void* __fastcall EntityOf(void*, void* collider)
{
    return collider == &crate ? static_cast<void*>(&crateEntity) : collider == &body ? static_cast<void*>(&bodyEntity) :
           collider == &selfBody ? static_cast<void*>(&playerEntity) : nullptr;
}
struct { Slot* table; } entitySystem{systemTable};

// Surface types: name +0x08, flags +0x1C, breakability +0x68.
Slot surfaceTypeTable[16]{};
alignas(8) unsigned char metal[0x80]{}, glassType[0x80]{};
unsigned surfaceLookups = 0;
void* __fastcall SurfaceOf(void*, int id, const char*)
{
    ++surfaceLookups;
    return id == 131 ? static_cast<void*>(metal) : id == 140 ? static_cast<void*>(glassType) : nullptr;
}
void MakeSurface(unsigned char* type, const char* name, int flags, int breakability)
{
    const auto table = reinterpret_cast<std::uintptr_t>(surfaceTypeTable);
    Put(type, 0, &table, 8);
    Put(type, sn::SurfaceTypeName, &name, 8);
    Put(type, sn::SurfaceTypeFlags, &flags, 4);
    Put(type, sn::SurfaceTypeBreakability, &breakability, 4);
}
int surfaceManager = 0;

// The player (stamina at +0x678 then +0x88: two floats at +0x10) and the wrench
// (its vtable, charge fields and component).
alignas(8) unsigned char playerObject[0x800]{}, staminaObject[0x40]{}, wrench[0x600]{};
Slot fakeWrenchTable[4]{};
void SetStamina(float a, float b)
{
    Put(staminaObject, sn::StaminaPair, &a, 4);
    Put(staminaObject, sn::StaminaPair + 4, &b, 4);
}
void MakeWrench()
{
    const auto table = reinterpret_cast<std::uintptr_t>(fakeWrenchTable);
    Put(wrench, 0, &table, 8);
    const float time = 0, full = .8f, bonus = .5f, fatigue = 12.5f;
    Put(wrench, sn::ChargeTime, &time, 4);
    Put(wrench, sn::ChargeFull, &full, 4);
    Put(wrench, sn::ChargeBonus, &bonus, 4);
    const std::uint64_t packages[4] = {0x1111, 0x2222, 0x3333, 0x4444};
    Put(wrench, sn::ComponentOffset + sn::PackageNormal, packages, sizeof(packages));
    Put(wrench, sn::ComponentOffset + sn::FatiguePerHit, &fatigue, 4);
    const auto stamina = reinterpret_cast<std::uintptr_t>(staminaObject);
    Put(playerObject, sn::PlayerStamina + sn::StaminaPointer, &stamina, 8);
    const std::uintptr_t weaponEntity = 0xe17;
    Put(wrench, sn::WeaponEntity, &weaponEntity, 8);
}

// The strike's engine. The hit does what Prey's does with the hooks in place: asks
// GetHits for its hits, pushes through the impulse wrapper, and builds the collision
// with the weapon's entity. Each original records what it was given.
struct HitCall { std::uintptr_t component = 0, weapon = 0; float angle = -1, scale = 0; bool charged = false; };
std::vector<HitCall> hitCalls;
std::vector<scene::Hit> hitsSeen;
Vec3 impulseGiven{}, collisionGiven{};
unsigned nativeGetHits = 0, nativeImpulses = 0, nativeCollisions = 0, nativeAttacks = 0, staminaChecks = 0;
struct FatigueCall { std::uintptr_t component = 0; bool charged = false; float bonus = 0; };
std::vector<FatigueCall> fatigueCalls;
void __fastcall FakeAppend(void* vector, const scene::Hit* hit)
{
    auto* v = static_cast<HitVector*>(vector);
    Check(!v->begin && !v->end && !v->capacity, "an empty engine vector at the append");
    v->begin = new scene::Hit(*hit);
    v->end = v->capacity = v->begin + 1;
}
HitVector* __fastcall FakeGetHits(std::uintptr_t, HitVector* out, std::uintptr_t, float) { ++nativeGetHits; *out = {}; return out; }
void __fastcall FakeImpulse(void*, void*, const Vec3* d, int, std::uintptr_t, float, float, float) { ++nativeImpulses; impulseGiven = *d; }
void __fastcall FakeBuilder(void*, void*, const Vec3* d, void*, void*, float, float, bool, short) { ++nativeCollisions; collisionGiven = *d; }
bool __fastcall FakeAttack(std::uintptr_t, unsigned, const void*, int, float) { ++nativeAttacks; return true; }
const Vec3 nativeDirection{0, 1, 0};
int __fastcall FakeComponentHit(std::uintptr_t component, float angle, std::uintptr_t weapon, float scale, bool charged)
{
    hitCalls.push_back({component, weapon, angle, scale, charged});
    HitVector v{};
    GetHitsHook(component, &v, weapon, angle);
    for (auto* h = v.begin; h != v.end; ++h) { hitsSeen.push_back(*h); }
    delete v.begin;
    void* entity = nullptr;
    std::memcpy(&entity, reinterpret_cast<const void*>(weapon + sn::WeaponEntity), sizeof(entity));
    ImpulseHook(nullptr, hitsSeen.empty() ? nullptr : hitsSeen.back().collider, &nativeDirection, 1, weapon, 1, 0, 100);
    BuilderHook(nullptr, nullptr, &nativeDirection, entity, nullptr, 10, .2f, true, 3);
    return 1;
}
void __fastcall FakeSetFatigue(std::uintptr_t component, bool charged, float bonus)
{
    fatigueCalls.push_back({component, charged, bonus});
    const float fatigue = charged ? 25.0f : 12.5f;
    std::memcpy(reinterpret_cast<void*>(component + sn::FatiguePerHit), &fatigue, 4);
}
bool __fastcall FakeStaminaAvailable(void* stamina)
{
    ++staminaChecks;
    float a = 0, b = 0;
    std::memcpy(&a, static_cast<const std::uint8_t*>(stamina) + sn::StaminaPair, 4);
    std::memcpy(&b, static_cast<const std::uint8_t*>(stamina) + sn::StaminaPair + 4, 4);
    return a != b;
}
bool installFails = false;
unsigned installs = 0;
bool FakeInstall(const Natives&)
{
    ++installs;
    if (installFails) { return false; }
    originalAttack = FakeAttack;
    originalGetHits = FakeGetHits;
    originalImpulse = FakeImpulse;
    originalBuilder = FakeBuilder;
    installed = true;
    return true;
}

DWORD FakeResolve(const GameplayPoseFrame& frame, Natives& n)
{
    n = {};
    n.world = 1;
    n.player = &self;
    n.carried = &carried;
    n.playerObject = frame.player;
    n.articulated = reinterpret_cast<std::uintptr_t>(bodyTable);
    n.wrench = reinterpret_cast<std::uintptr_t>(fakeWrenchTable);
    n.entitiesInBox = Broadphase;
    n.collide = FakeCollide;
    n.entitySystem = &entitySystem;
    n.surfaces = &surfaceManager;
    n.surfaceType = SurfaceOf;
    n.surfaceTypeVtable = reinterpret_cast<std::uintptr_t>(surfaceTypeTable);
    n.componentHit = FakeComponentHit;
    n.setFatigue = FakeSetFatigue;
    n.staminaAvailable = FakeStaminaAvailable;
    append = FakeAppend;
    wrenchTable = n.wrench;
    return ERROR_SUCCESS;
}

// The scenery ray: clear, blocked by a wall, or refused.
bool sightBlocked = false, sightRefused = false;
unsigned sightRays = 0;
void* sightSkip = nullptr;
Vec3 sightEnd{};
bool menu = false, haveBox = true;
WeaponBox weapon{};
PhysicalOverlayState lastOverlay{};
std::string lastResult;
unsigned overlays = 0, pulses = 0;
} // namespace

namespace preyvr::dll {
unsigned long InputDrainThreadId() { return GetCurrentThreadId(); }
bool HudGameplayInputAllowed() { return !menu; }
bool BodyHeadOut() { return false; }
unsigned long long HeadTrackingReferenceGeneration() { return 2; }
bool TryGetWeaponBox(WeaponBox& out) { out = weapon; return haveBox; }
DWORD SceneQueryWorld(const GameplayPoseFrame&, std::uintptr_t&, void*&) { return ERROR_NOT_READY; }
std::uintptr_t CarriedPhysics() { return 0; }
bool DebugOverlayWantsPhysical() { return true; }
void DebugOverlayPhysical(const GameplayPoseFrame&, const PhysicalOverlayState& s)
{
    lastOverlay = s;
    lastResult = s.contact && s.result ? s.result : "";
    ++overlays;
}
bool EnsureMinHook() { return false; }
void QueueHaptic(Hand hand, haptics::Event event, const TrackingFrame&)
{
    Check(hand == Hand::right && event == haptics::Event::WeaponStrike, "a strike pulses the wrench hand");
    ++pulses;
}
bool QuerySceneryBetween(const GameplayPoseFrame&, Vec3, Vec3 b, void* skip, scene::Hit& hit)
{
    ++sightRays;
    sightSkip = skip;
    sightEnd = b;
    hit = {};
    hit.distance = -1;
    if (sightRefused) { return false; }
    if (sightBlocked) {
        hit.distance = .4f;
        hit.collider = &blocker;
        hit.surface = 9;
    }
    return true;
}
} // namespace preyvr::dll

int main()
{
    Tables();
    classTable[sn::ClassNameSlot / 8] = reinterpret_cast<Slot>(ClassName);
    entityTable[sn::EntityIdSlot / 8] = reinterpret_cast<Slot>(EntityId);
    entityTable[sn::EntityClassSlot / 8] = reinterpret_cast<Slot>(EntityClass);
    entityTable[sn::EntityNameSlot / 8] = reinterpret_cast<Slot>(EntityName);
    systemTable[sn::EntityFromPhysicsSlot / 8] = reinterpret_cast<Slot>(EntityOf);
    MakeSurface(metal, "mat_metal", 0x10, 0);
    MakeSurface(glassType, "mat_glass", 0x24, 1);
    MakeWrench();
    SetStamina(100, 30);
    const std::uintptr_t host = 0xabc;
    Put(&body, cn::ArticulatedHost, &host, 8);
    const void* playerPhysics = &self;
    Put(&selfHosted, cn::ArticulatedHost, &playerPhysics, 8);
    gResolve = FakeResolve;
    gInstall = FakeInstall;

    // A wrench along +y from the grip: the handle 10 cm behind it, the head 34 cm
    // ahead. Tracking space is the world here (an identity map), so moving the
    // points moves the wrench at points' speed.
    const Vec3 origin{10, 20, 1};
    const auto local = physical::Bounds({-.06f, -.10f, -.06f}, {.06f, .34f, .06f});
    for (unsigned i = 0; i < 9; ++i) { weapon.points[i] = Add(origin, local[i]); }
    weapon.local = weapon.points;
    weapon.worldFromReference = {{0, 0, 0, 1}, {}};
    weapon.grip = origin;
    weapon.rig = 1;
    weapon.reference = 2;
    weapon.epoch = 1;
    weapon.stamp = 1000000000ull;
    weapon.wrench = true;
    weapon.weapon = reinterpret_cast<std::uintptr_t>(wrench);
    GameplayPoseFrame f{};
    f.player = reinterpret_cast<std::uintptr_t>(playerObject);
    f.cameraCentreValid = true;
    f.headYawUsable = true;
    f.referenceGeneration = 2;
    f.tracking.epoch = 1;
    f.cameraCentre = {10, 19.4f, 1.6f};
    // One tracking sample later, the wrench moved by `d` (90 Hz).
    auto move = [&](Vec3 d) {
        for (auto& p : weapon.points) { p = Add(p, d); }
        weapon.local = weapon.points;
        weapon.grip = Add(weapon.grip, d);
        ++weapon.sequence;
        weapon.stamp += 11111111ull;
        weapon.publishedNs = f.tracking.publishedNs = MonotonicNanoseconds();
        logLines = 0;   // the test's frames come far faster than 90 Hz: no line budget here
        UpdateSwingLane(f, true);
    };
    auto swingAt = [&](float metresPerSecond, unsigned n = 1) {
        for (unsigned i = 0; i < n; ++i) { move({metresPerSecond / 90.0f, 0, 0}); }
    };
    auto still = [&](unsigned n) {
        for (unsigned i = 0; i < n; ++i) { move({}); }
    };
    const Vec3 headPoint{10.06f, 20.28f, 1};

    Check(SwingLaneWanted() && FreeSwingEnabled() == 1 && SwingSpeed() == 120 && HeavySwingSpeed() == 700 && SwingEventLog() == 0,
          "on by default, at 120 cm/s and heavy from 700, its event lines off");
    SetSwingEventLog(1);   // most checks below read the event lines
    listed = {&self, &carried, &selfBody, &selfHosted, &wall, &capsule, nullptr};
    still(1);
    Check(Logged("preyvr_swing gate result=0 rows=7+19+15 entitySystem=1 surfaces=1"), "the gate line names its rows and namers");
    Check(Logged("preyvr_swing settings enabled=1 minCms=120 heavyCms=700 fullCms=1000 minScale=0.50 lockMs=150"), "the settings");
    Check(installs == 1 && installed.load(), "the hooks go in once the wrench is out");
    Check(Logged("preyvr_swing wrench rig=1", "chargeFull=0.800 chargeBonus=0.500 charged=0 fatiguePerHit=12.500 dodge=0",
                 "packages=0x1111,0x2222,0x3333,0x4444"), "the wrench's own fields, once per equip");
    Check(objtypes == sn::StrikeObjects && prealloc == 64, "Prey's melee classes, into our own 64");
    bool askedSkipped = false;
    for (const auto& c : calls) { askedSkipped |= c.entity == &self || c.entity == &carried || c.entity == &capsule; }
    for (const auto& c : calls) { askedSkipped |= c.entity == &selfBody || c.entity == &selfHosted; }
    Check(!askedSkipped && calls.size() == 1 && calls[0].kind == Kind::overlap, "player, carried and living skipped; no pose before: overlap only");
    Check(selfSkipped.load() == 2 && Logged("preyvr_swing self entity=", "byHost=0 byEntity=1 skipped"),
          "the player's own skeleton is skipped, by its entity or by its host");

    // --- a slow touch on a wall: too slow, told once ---
    responses[&wall] = {true, true, true, headPoint, 131, 1};
    calls.clear();
    swingAt(.5f, 3);
    Check(calls.size() == 9, "moving: an overlap, the body's sweep and the head's, per object");
    Check(Logged("preyvr_swing begin entity=", "type=static", "foreign=1") &&
          Logged("begin", "entityId=none surface=131 surfaceName=mat_metal breakable=0 surfaceFlags=0x10", "decision=too_slow"),
          "the first touch: what it is, its surface by name, and the decision");
    Check(Count("preyvr_swing begin") == 1 && !Logged("preyvr_swing strike"), "one begin line per pass; a touch is no strike");
    Check(sightRays == 0 && hitCalls.empty(), "a touch asks no line of sight and strikes nothing");

    // --- off it, then a swing at 3 m/s: a strike ---
    responses.erase(&wall);
    still(20);
    responses[&wall] = {true, true, true, headPoint, 131, 1};
    wrench[sn::ComponentOffset + sn::DodgeFlag] = 1;
    swingAt(3.0f);
    Check(Logged("preyvr_swing strike entity=", "surfaceName=mat_metal",
                 "result=struck tier=normal scale=0.66 returned=1 fatigue=12.50 stamina=100.0/30.0->100.0/30.0 sight=clear"),
          "a swing strikes, scaled by speed");
    const auto component = reinterpret_cast<std::uintptr_t>(wrench) + sn::ComponentOffset;
    Check(hitCalls.size() == 1 && hitCalls[0].component == component && hitCalls[0].weapon == reinterpret_cast<std::uintptr_t>(wrench) &&
          hitCalls[0].angle == 0 && std::fabs(hitCalls[0].scale - .655f) < .01f && !hitCalls[0].charged,
          "Prey's hit, called on the wrench's component, uncharged, at the strike's scale");
    Check(fatigueCalls.size() == 1 && fatigueCalls[0].component == component && !fatigueCalls[0].charged && fatigueCalls[0].bonus == .5f,
          "the fatigue setter first, as Prey's attack calls it: the component, uncharged, the wrench's bonus");
    Check(wrench[sn::ComponentOffset + sn::DodgeFlag] == 0 && staminaChecks == 1, "the dodge flag cleared; Prey's stamina check asked");
    Check(hitsSeen.size() == 1 && hitsSeen[0].collider == &wall && hitsSeen[0].surface == 131 && nativeGetHits == 0,
          "GetHits answers with the contact, through the engine's append");
    Check(nativeImpulses == 1 && impulseGiven.x < -.99f && nativeCollisions == 1 && collisionGiven.x > .99f,
          "the impulse wrapper gets the swing's reverse (it negates), the collision the swing itself");
    Check(!SwingRecentlyStruck(&wall), "scenery is not handed to anything");
    Check(pulses == 1, "and pulses the hand");
    Check(Logged("strike", "speedCms=300 approachCms=300"), "the speed at the contact, and into the surface");
    Check(sightRays == 1 && sightSkip == &wall && std::fabs(Length(Sub(sightEnd, headPoint)) - .02f) < 1e-3f,
          "the line of sight skips the target and stops 2 cm short");
    Check(lastOverlay.contact && lastOverlay.acted && lastResult == "struck mat_metal 300cm/s" && lastOverlay.box &&
          lastOverlay.swept, "the overlay marks it");
    swingAt(3.0f, 5);
    Check(Count("result=struck") == 1 && strikes.load() == 1 && locked.load() == 5 && pulses == 1 && hitCalls.size() == 1,
          "one strike per pass");
    responses.erase(&wall);
    still(20);
    Check(Logged("preyvr_swing swing peakHeadCms=300 peakCentreCms=300", "contacts=1 strikes=1 end=still"),
          "the swing's own line once it stops");
    responses[&wall] = {true, true, true, headPoint, 131, 1};
    swingAt(3.0f);
    Check(Count("result=struck") == 2 && hitCalls.size() == 2, "off it for longer than the lock: the next pass strikes again");

    // --- outside a strike the hooks are Prey's ---
    HitVector plain{};
    GetHitsHook(component, &plain, reinterpret_cast<std::uintptr_t>(wrench), 0);
    ImpulseHook(nullptr, nullptr, &nativeDirection, 1, reinterpret_cast<std::uintptr_t>(wrench), 1, 0, 100);
    BuilderHook(nullptr, nullptr, &nativeDirection, nullptr, nullptr, 1, 1, true, 0);
    Check(nativeGetHits == 1 && impulseGiven.y == 1 && collisionGiven.y == 1, "Prey's own queries and directions pass through");
    const auto table = reinterpret_cast<std::uintptr_t>(fakeWrenchTable);
    Check(!AttackHook(reinterpret_cast<std::uintptr_t>(wrench), 0, nullptr, 1, 1) && nativeAttacks == 0 && attacksBlocked.load() == 1,
          "the trigger does nothing with the wrench out");
    alignas(8) unsigned char pistol[0x600]{};
    const std::uintptr_t otherTable = table + 8;
    Put(pistol, 0, &otherTable, 8);
    Check(AttackHook(reinterpret_cast<std::uintptr_t>(pistol), 0, nullptr, 1, 1) && nativeAttacks == 1, "other weapons attack as ever");
    SetFreeSwing(0);
    Check(AttackHook(reinterpret_cast<std::uintptr_t>(wrench), 0, nullptr, 1, 1) && nativeAttacks == 2, "the option off gives the trigger back");
    SetFreeSwing(1);

    // --- refusals ---
    responses.erase(&wall);
    still(20);
    sightBlocked = true;
    responses[&wall] = {true, true, true, headPoint, 131, 1};
    swingAt(3.0f, 3);
    Check(Count("result=blocked") == 1 && Logged("result=blocked", "sight=blocked blocker=") && blocked.load() == 3 &&
          strikes.load() == 2, "scenery in the way: no strike, told once a pass");
    sightBlocked = false;
    sightRefused = true;
    responses.erase(&wall);
    still(20);
    responses[&wall] = {true, true, true, headPoint, 131, 1};
    swingAt(3.0f);
    Check(Logged("result=blocked", "sight=unknown") && sightUnknown.load() == 1, "an unanswered line of sight is no strike");
    sightRefused = false;
    responses.erase(&wall);
    still(20);
    SetStamina(50, 50);
    const auto hitsBefore = hitCalls.size();
    const auto checksBefore = staminaChecks;
    responses[&wall] = {true, true, true, headPoint, 131, 1};
    swingAt(3.0f, 4);
    Check(Logged("result=drained", "stamina=50.0/50.0") && drained.load() == 1 && hitCalls.size() == hitsBefore &&
          staminaChecks == checksBefore + 1, "out of stamina: Prey's check says no, nothing strikes, once a pass");
    SetStamina(100, 30);

    // --- what is struck ---
    responses.clear();
    still(20);
    listed = {&glass, &crate, &body};
    responses[&glass] = {true, true, true, headPoint, 140, 1};
    responses[&crate] = {false, false, true, headPoint, 131, 2};
    responses[&body] = {true, false, false, headPoint, 131, 12};
    swingAt(8.5f);
    Check(Logged("begin", "surfaceName=mat_glass breakable=1 surfaceFlags=0x24"), "glass by its surface, breakable");
    Check(Logged("strike", "surfaceName=mat_glass", "tier=heavy scale=1.25") && Logged("strike", "surfaceName=mat_glass", "fatigue=25.00"),
          "a heavy swing: Prey's charged hit and half its bonus, at the charged fatigue");
    bool heavyCall = false, heavyFatigue = false;
    for (const auto& c : hitCalls) { heavyCall |= c.charged && std::fabs(c.scale - 1.25f) < .01f; }
    for (const auto& c : fatigueCalls) { heavyFatigue |= c.charged; }
    Check(heavyCall && heavyFatigue, "the hit and its fatigue both told it is charged");
    Check(SwingRecentlyStruck(&crate) && SwingRecentlyStruck(&body) && !SwingRecentlyStruck(&glass) && handoffs.load() == 2,
          "struck props and bodies are the hit's to move for a moment");

    // --- a higher heavy swing speed: the same swing is a normal hit ---
    responses.clear();
    still(20);
    SetHeavySwingSpeed(900);
    responses[&glass] = {true, true, true, headPoint, 140, 1};
    swingAt(8.5f);
    Check(Logged("preyvr_swing settings", "heavyCms=900 fullCms=1200") && Logged("strike", "surfaceName=mat_glass", "tier=normal scale=0.97"),
          "Heavy Swing Speed at 9 m/s: an 8.5 m/s swing is Prey's normal hit, nearly whole");
    // --- a minimum above the heavy speed: every strike heavy ---
    responses.clear();
    still(20);
    SetSwingSpeed(800);
    Check(SwingSpeed() == 700, "the minimum swing speed stops at 7 m/s");
    SetHeavySwingSpeed(300);
    responses[&glass] = {true, true, true, headPoint, 140, 1};
    swingAt(7.5f);
    Check(Logged("preyvr_swing settings", "minCms=700 heavyCms=700 fullCms=1000") &&
          Logged("strike", "surfaceName=mat_glass", "tier=heavy scale=1.08"),
          "a heavy speed under the minimum: the strike is heavy from the minimum on");
    responses.clear();
    still(20);
    swingAt(6.5f);
    Check(!Logged("speedCms=650", "result=struck"), "and under the minimum nothing strikes");
    SetSwingSpeed(50);
    Check(SwingSpeed() == 100, "nor below 1 m/s");
    SetSwingSpeed(120);
    SetHeavySwingSpeed(700);
    responses.clear();
    still(20);
    listed = {&glass, &crate, &body};
    responses[&glass] = {true, true, true, headPoint, 140, 1};
    responses[&crate] = {false, false, true, headPoint, 131, 2};
    responses[&body] = {true, false, false, headPoint, 131, 12};
    Check(pulses == strikes.load(), "every strike pulsed");
    Check(Logged("begin", "type=rigid", "entityId=42 class=ArkPhysicsProp name=Crate_1") && Logged("begin", "class=ArkPhysicsProp", "kind=head"),
          "an entity by class and name; the head's own sweep found the crate");
    Check(headTouches.load() >= 1, "head touches counted");
    Check(Logged("begin", "type=articulated", "host=1 entityId=77 class=ArkNpcMimic"), "a body names its host");
    Check(surfaceLookups == 2, "each surface looked up once");

    // --- gates ---
    still(20);
    menu = true;
    const auto queriesBefore = queries.load();
    swingAt(3.0f);
    Check(lastGate.load() == std::string("menu") && queries.load() == queriesBefore && lastOverlay.gate == std::string("menu") &&
          !lastOverlay.box, "a menu stops it");
    menu = false;
    weapon.wrench = false;
    swingAt(3.0f);
    Check(lastGate.load() == std::string("not_wrench") && queries.load() == queriesBefore, "another weapon: nothing");
    weapon.wrench = true;
    swingAt(3.0f);
    const auto queriesNow = queries.load();
    UpdateSwingLane(f, true);
    Check(queries.load() == queriesNow, "the same tracking sample is not decided twice");
    overflowCount = 70;
    swingAt(3.0f);
    Check(overflows.load() == 1, "more objects than the buffer: refused");
    overflowCount = 0;
    Sleep(200);
    Check(!SwingRecentlyStruck(&crate), "and then contact's again");

    // --- quiet logging ---
    responses.clear();
    still(20);
    SetSwingEventLog(0);
    const auto begunBefore = begins.load();
    const auto linesBefore = Count("preyvr_swing begin");
    responses[&glass] = {true, true, true, headPoint, 140, 1};
    swingAt(3.0f);
    Check(begins.load() == begunBefore + 1 && Count("preyvr_swing begin") == linesBefore, "event lines off: counted, not logged");
    const auto report = SwingReport();
    Check(report.find(" struck=") != std::string::npos && report.find(" hooks=1 attacksBlocked=1") != std::string::npos &&
          report.find(" handoffs=2") != std::string::npos && report.find(" passes=") != std::string::npos &&
          report.find(" static=") != std::string::npos && report.find(" gate=open") != std::string::npos &&
          report.find(" log=0") != std::string::npos, "the report");

    // --- not the wrench any more, then hooks that will not go in ---
    SetSwingEventLog(1);
    responses.clear();
    still(20);
    const std::uintptr_t notWrench = reinterpret_cast<std::uintptr_t>(fakeWrenchTable) + 16;
    Put(wrench, 0, &notWrench, 8);
    responses[&glass] = {true, true, true, headPoint, 140, 1};
    swingAt(3.0f);
    Check(Logged("result=failed", "why=not_wrench") && failed.load() == 1, "a weapon that is no longer the wrench is never struck with");
    const auto wrenchVtable = reinterpret_cast<std::uintptr_t>(fakeWrenchTable);
    Put(wrench, 0, &wrenchVtable, 8);
    installed = false;
    installFails = true;
    swingAt(3.0f);
    Check(fault.load() && Logged("preyvr_swing fault", "detail=hooks") && !SwingLaneWanted(),
          "hooks that fail to go in: nothing strikes");
    Check(AttackHook(reinterpret_cast<std::uintptr_t>(wrench), 0, nullptr, 1, 1), "and the trigger is Prey's again");
    std::cout << "swing_lane: " << lines.size() << " lines\n";
}
