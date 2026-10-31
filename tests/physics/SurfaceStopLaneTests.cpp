// Surface stop: the production lane with the engine's rays replaced by a fake
// world of boxes (a static wall, loose props and bodies), and the rest of the DLL
// at its boundary.
#include "physics/contact/SurfaceStopLane.cpp"
#include "TestSupport.h"
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
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

// Fake physics objects: each starts with its class's vtable, as the engine's do.
// GetType (slot 0x08) answers the static class; the rigid and articulated
// GetStatus (slot 0x30) report the object's mass (kept at +0x108); an articulated
// body's host is at +0x4F8.
struct Object { void** table; unsigned char bytes[0x600]; };
constexpr std::size_t kMassAt = 0x100;
int __fastcall StaticType(const void*) { return cn::TypeStatic; }
int __fastcall Status(void* self, void* status)
{
    int type = 0;
    std::memcpy(&type, status, 4);
    if (type != 8) { return 0; }
    std::memcpy(static_cast<std::uint8_t*>(status) + 0x4C, static_cast<Object*>(self)->bytes + kMassAt, 4);
    return 1;
}
void* staticTable[8]{nullptr, reinterpret_cast<void*>(StaticType)};
void* rigidTable[8]{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void*>(Status), nullptr};
void* articulatedTable[8]{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void*>(Status), nullptr};
Object wallObject{staticTable}, can{rigidTable}, crate{rigidTable}, corpse{articulatedTable}, carried{rigidTable};
void SetMass(Object& o, float kg) { std::memcpy(o.bytes + kMassAt, &kg, 4); }
void SetHost(Object& o, const void* host) { std::memcpy(o.bytes + cn::ArticulatedHost - 8, &host, 8); }

// The world: boxes owned by those objects. A ray meets the box it enters first;
// one starting inside a box does not see it (one-sided faces). Loose boxes answer
// only a query asking for the rigid classes; the skipped object never answers.
struct Block { Vec3 lo, hi; Object* owner; bool loose; };
const Block kWall{{1, -2, 0}, {1.3f, 2, 3}, &wallObject, false};   // fixed scenery from x 1.0
constexpr float wallX = 1.0f;
std::vector<Block> blocks{kWall};
unsigned rayCalls = 0;
int lastTypes = -1;
void* lastSkip = nullptr;
bool queryFails = false;
bool FakeQuery(const GameplayPoseFrame&, Vec3 a, Vec3 b, void* skip, int types, scene::Hit& hit)
{
    ++rayCalls;
    lastTypes = types;
    lastSkip = skip;
    const Vec3 d = Sub(b, a);
    Check(Length(d) <= 2.0f && Length(d) >= .001f, "every segment within what the query takes");
    hit = {};
    hit.distance = -1;
    if (queryFails) { return false; }
    float best = 2;
    const Block* met = nullptr;
    Vec3 normal{};
    for (const auto& k : blocks) {
        if (k.owner == skip || (k.loose && !(types & 0x6))) { continue; }
        const float lo[3] = {k.lo.x, k.lo.y, k.lo.z}, hi[3] = {k.hi.x, k.hi.y, k.hi.z};
        const float o[3] = {a.x, a.y, a.z}, v[3] = {d.x, d.y, d.z};
        bool inside = true;
        for (int i = 0; i < 3; ++i) { inside &= o[i] > lo[i] && o[i] < hi[i]; }
        if (inside) { continue; }
        float enter = -1e9f, leave = 1e9f;
        int axis = -1;
        bool miss = false;
        for (int i = 0; i < 3 && !miss; ++i) {
            if (std::fabs(v[i]) < 1e-9f) {
                miss = o[i] < lo[i] || o[i] > hi[i];
                continue;
            }
            float t0 = (lo[i] - o[i]) / v[i], t1 = (hi[i] - o[i]) / v[i];
            if (t0 > t1) { std::swap(t0, t1); }
            if (t0 > enter) { enter = t0; axis = i; }
            leave = std::min(leave, t1);
        }
        if (miss || axis < 0 || enter > leave || enter < 0 || enter > 1 || enter >= best) { continue; }
        best = enter;
        met = &k;
        normal = {};
        (&normal.x)[axis] = v[axis] > 0 ? -1.0f : 1.0f;
    }
    if (!met) { return true; }
    hit.distance = best * Length(d);
    hit.point = Add(a, Scale(d, best));
    hit.normal = normal;
    hit.collider = met->owner;
    hit.surface = 131;
    return true;
}
// The loose-body classes the lane tells apart: the fakes' vtables; the carried object.
bool objectsReady = true;
Objects FakeObjects()
{
    if (!objectsReady) { return {}; }
    return {true, reinterpret_cast<std::uintptr_t>(rigidTable), reinterpret_cast<std::uintptr_t>(articulatedTable), &carried};
}

bool menu = false, haveBox = false, twoHanded = false;
WeaponBox weapon{};
SurfaceOverlayState lastOverlay{};
unsigned overlays = 0;
}
namespace preyvr::dll {
unsigned long InputDrainThreadId() { return GetCurrentThreadId(); }
bool HudGameplayInputAllowed() { return !menu; }
bool BodyHeadOut() { return false; }
unsigned long long HeadTrackingReferenceGeneration() { return 2; }
bool TryGetWeaponBox(WeaponBox& out) { out = weapon; return haveBox; }
bool SceneQueryFaulted() { return false; }
bool QueryObjectsBetween(const GameplayPoseFrame&, Vec3, Vec3, void*, int, scene::Hit&) { return false; }
DWORD GateLooseBodies() { return ERROR_SUCCESS; }
std::uintptr_t CarriedPhysics() { return 0; }
bool DebugOverlayWantsPhysical() { return true; }
void DebugOverlaySurface(const GameplayPoseFrame&, const SurfaceOverlayState& s) { lastOverlay = s; ++overlays; }
unsigned TwoHandedAimEnabled() { return twoHanded ? 1u : 0u; }
}

namespace {
// The camera at (0, 0, 1.6), facing +y (yaw 0), the head at the tracking origin:
// a controller at OpenXR (x, y, z) is in the world at (x, -z, 1.6 + y).
Vec3 Xr(Vec3 world) { return {world.x, world.z - 1.6f, -world.y}; }
void Place(GameplayPoseFrame& f, Hand hand, Vec3 world)
{
    auto& h = f.tracking.hands[static_cast<unsigned>(hand)];
    h.gripPose.position = Xr(world);
    h.gripValidity = {true, true, true, true, 0};
    h.aimPose = h.gripPose;
    h.aimValidity = h.gripValidity;
}
}

int main()
{
    gQuery = FakeQuery;
    gObjects = FakeObjects;
    GameplayPoseFrame f{};
    f.player = 1;
    f.cameraCentreValid = true;
    f.cameraCentre = {0, 0, 1.6f};
    f.headYawUsable = true;
    f.referenceGeneration = 2;
    f.tracking.epoch = 1;
    f.tracking.headValidity = {true, true, true, true, 0};
    Place(f, Hand::right, {.6f, 0, 1.2f});
    Place(f, Hand::left, {.3f, .4f, 1.2f});
    auto tick = [&] {
        ++f.tracking.sequence;
        f.tracking.displayTime += 11111111ll;
        f.tracking.publishedNs = MonotonicNanoseconds();
        UpdateSurfaceStop(f, true);
    };
    const auto right = static_cast<unsigned>(Hand::right), left = static_cast<unsigned>(Hand::left);

    Check(SurfaceStopEnabled() == 1 && SurfaceStopWeapon() == 1 && SurfaceStopEventLog() == 0,
          "on by default with the weapon's box, its event lines off");
    SetSurfaceStopEventLog(1);
    tick();
    Check(Logged("preyvr_surface settings enabled=1 weapon=1 heavyKg=10 log=1"), "the settings line");
    Check(!f.surface[right].blocked && !f.surface[left].blocked && rayCalls == 18,
          "in the open: nothing held, a ray per probe of each hand (grip and eight corners)");
    Check(lastTypes == scene::SceneryAndLooseObjects && lastSkip == &carried,
          "the rays ask for scenery and loose objects, skipping the carried one");
    Check(overlays == 1 && lastOverlay.hands[right].active && lastOverlay.hands[left].active &&
              lastOverlay.status.find("gate open") != std::string::npos,
          "the overlay gets both hands");

    // --- the right hand pushed into the wall ---
    Place(f, Hand::right, {1.0f, 0, 1.2f});   // the back of the hand, 6.5 cm past the grip: into the wall
    tick();
    const Vec3 drawn = Add(Vec3{1.0f, 0, 1.2f}, f.surface[right].offset);
    Check(f.surface[right].blocked && f.surface[right].offset.x < -.02f && std::fabs(f.surface[right].offset.y) < 1e-4f &&
              drawn.x < wallX,
          "pushed into the wall: the drawn hand is held back out of it");
    Check(!f.surface[left].blocked, "the other hand is not");
    Check(Logged("preyvr_surface hold hand=right path=moved", "met=scenery type=1 surface=131") && lastOverlay.hands[right].held &&
              lastOverlay.count > 0 && lastOverlay.status.find("held -R") != std::string::npos,
          "a hold line naming the static wall; the overlay marks the hand and the surface");
    {
        // A game frame between two tracking samples: the same sample again keeps
        // the hand's memory.
        const auto before = resets.load();
        const Vec3 offset = f.surface[right].offset;
        f.tracking.publishedNs = MonotonicNanoseconds();
        UpdateSurfaceStop(f, true);
        Check(resets.load() == before && f.surface[right].blocked && Length(Sub(f.surface[right].offset, offset)) < 1e-5f,
              "the same tracking sample twice: held the same, nothing forgotten");
    }
    Place(f, Hand::right, {.6f, 0, 1.2f});
    tick();
    Check(!f.surface[right].blocked && Logged("preyvr_surface free hand=right"), "back in the open: let go, with a line");

    // --- a gate ---
    menu = true;
    tick();
    Check(!f.surface[right].blocked && !std::strcmp(lastGate.load(), "menu") && lastOverlay.status.find("gate menu") != std::string::npos,
          "a menu: no correction");
    menu = false;
    SetSurfaceStop(0);
    Place(f, Hand::right, {1.0f, 0, 1.2f});
    tick();
    Check(!f.surface[right].blocked && !std::strcmp(lastGate.load(), "off"), "off: nothing held, even in the wall");
    SetSurfaceStop(1);
    Place(f, Hand::right, {.6f, 0, 1.2f});
    tick();

    // --- a 65 cm weapon, its muzzle in the wall ---
    // Drawn where the controller holds it, last frame: along +x from the grip.
    weapon = {};
    weapon.worldFromReference = {{0, 0, 0, 1}, f.cameraCentre};
    weapon.gripPose = {{0, 0, 0, 1}, Sub(Vec3{.6f, 0, 1.2f}, f.cameraCentre)};
    const auto box = physical::Bounds({-.05f, -.03f, -.05f}, {.6f, .03f, .05f});
    for (unsigned i = 0; i < 9; ++i) {
        weapon.local[i] = Add(weapon.gripPose.position, box[i]);
        weapon.points[i] = physical::Transform(weapon.worldFromReference, weapon.local[i]);
    }
    weapon.rig = 7;
    weapon.reference = 2;
    weapon.epoch = 1;
    haveBox = true;
    rayCalls = 0;
    weapon.publishedNs = MonotonicNanoseconds();
    tick();
    Check(f.surface[right].blocked && std::fabs(f.surface[right].offset.x + (.6f + .6f - wallX + .005f)) < 2e-3f,
          "the muzzle 20 cm into the wall: the weapon and hand held back until it touches");
    Check(weaponFrames.load() == 1 && resets.load() >= 1, "with the weapon's probes; a new key starts its memory afresh");
    // Drawn held back, the takeover publishes it so: the lane takes the correction out.
    weapon.correction = f.surface[right].offset;
    for (unsigned i = 0; i < 9; ++i) {
        weapon.local[i] = Add(weapon.local[i], weapon.correction);
        weapon.points[i] = Add(weapon.points[i], weapon.correction);
    }
    weapon.publishedNs = MonotonicNanoseconds();
    const Vec3 heldBack = f.surface[right].offset;
    tick();
    Check(f.surface[right].blocked && std::fabs(f.surface[right].offset.x - heldBack.x) < 2e-3f,
          "the next frame, from the drawn box, the same correction: never counted twice");
    SetSurfaceStopWeapon(0);
    tick();
    Check(!f.surface[right].blocked, "weapon 0: the hand alone, which is clear of the wall");
    SetSurfaceStopWeapon(1);

    // --- a left hand on the foregrip rides on the weapon ---
    twoHanded = true;
    f.twoHand.blend = 1;
    Place(f, Hand::left, {1.1f, .1f, 1.2f});   // in the wall, but drawn on the weapon
    rayCalls = 0;
    tick();
    Check(!f.surface[left].blocked && !lastOverlay.hands[left].active, "the left hand on the foregrip is not solved");
    twoHanded = false;
    f.twoHand = {};
    tick();
    Check(f.surface[left].blocked, "let go of the foregrip, the left hand stops at the wall too");
    haveBox = false;

    // --- a snap turn forgets ---
    const auto before = resets.load();
    f.yaw = .5f;
    tick();
    Check(resets.load() > before, "a snap turn: each hand's memory starts again");
    f.yaw = 0;

    // --- the query refusing ---
    queryFails = true;
    tick();
    Check(!f.surface[right].blocked && !f.surface[left].blocked && refused.load() > 0, "a query that cannot run: no correction");
    queryFails = false;

    // --- loose objects, clear of the wall (at y 3) ---
    // The right hand from the open to x, through a 2 kg can at x 0.85-0.90, into
    // whatever stands at x 1.0: its back corners reach 6.5 cm in.
    auto reach = [&](float x) {
        Place(f, Hand::right, {.6f, 3, 1.2f});
        tick();
        Place(f, Hand::right, {x, 3, 1.2f});
        tick();
        return f.surface[right];
    };
    auto standing = [&](Object& o) {
        blocks = {kWall, {{.85f, 2.9f, 1.1f}, {.9f, 3.1f, 1.3f}, &can, true}, {{1, 2.7f, 0}, {1.4f, 3.3f, 1.5f}, &o, true}};
    };
    SetMass(can, 2);
    SetMass(crate, 50);
    standing(crate);
    {
        const auto passedBefore = passedLight.load();
        const auto c = reach(1.0f);
        Check(c.blocked && c.offset.x < -.065f && c.offset.x > -.075f && passedLight.load() > passedBefore,
              "a 50 kg crate holds the hand back at its face; the 2 kg can before it does not");
        Check(c.press.object == &crate && std::fabs(c.press.depth - .065f) < 2e-3f && c.press.normal.x < -.99f &&
                  std::fabs(c.press.point.x - 1.0f) < 1e-3f,
              "and is handed on as a press: the real hand 6.5 cm past its face");
        Check(Logged("preyvr_surface hold hand=right", "met=heavy mass=50.0"), "the hold line names it heavy, with its mass");
    }
    SetSurfaceHeavyMass(60);
    {
        const auto c = reach(1.0f);
        Check(!c.blocked && !c.press.object && Logged("preyvr_surface settings", "heavyKg=60"),
              "Heavy Objects at 60 kg: the 50 kg crate is pushed through instead");
    }
    SetSurfaceHeavyMass(0);
    Check(SurfaceHeavyMass() == 1, "the mass clamps to 1..100 kg");
    SetSurfaceHeavyMass(10);
    SetMass(crate, 0);
    {
        const auto c = reach(1.0f);
        Check(c.blocked && !c.press.object && Logged("hold hand=right", "met=immovable"),
              "a prop the game gave no mass holds the hand back, and is not pressed");
    }
    // Corpses: pushed limb by limb, never in the way, but for a massless one; a
    // live character's skeleton (it has a host) never.
    standing(corpse);
    SetMass(corpse, 30);
    {
        const auto passedBefore = passedBodies.load();
        const auto c = reach(1.0f);
        Check(!c.blocked && passedBodies.load() > passedBefore, "a corpse is not in the way");
    }
    SetMass(corpse, 0);
    Check(reach(1.0f).blocked, "a massless corpse is");
    SetHost(corpse, &wallObject);
    {
        const auto passedBefore = passedLive.load();
        Check(!reach(1.0f).blocked && passedLive.load() > passedBefore, "a live character's skeleton never is");
    }
    SetHost(corpse, nullptr);
    // What the hand carries is never met.
    SetMass(carried, 50);
    standing(carried);
    Check(!reach(1.0f).blocked, "the carried object, however heavy, is skipped");
    // Without the loose-body contracts: scenery alone.
    standing(crate);
    SetMass(crate, 50);
    objectsReady = false;
    Check(!reach(1.0f).blocked && lastTypes == scene::SceneryObjects, "no loose-body contracts: the rays ask for scenery alone");
    objectsReady = true;
    blocks = {kWall};

    const auto report = SurfaceStopReport();
    Check(report.find(" enabled=1 weapon=1 heavyKg=10") != std::string::npos && report.find(" holds=") != std::string::npos &&
              report.find(" usMax=") != std::string::npos && report.find(" static=") != std::string::npos &&
              report.find(" heavy=1 ") != std::string::npos && report.find(" presses=") != std::string::npos,
          "the report");
    std::cout << "surface_stop_lane: " << lines.size() << " lines\n";
    return 0;
}
