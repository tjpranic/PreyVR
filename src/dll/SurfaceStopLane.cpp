#include "SurfaceStopLane.h"
#include "AimTakeover.h"
#include "HandWorld.h"
#include "BodyFollow.h"
#include "CarryLane.h"
#include "ContactLane.h"
#include "DebugOverlay.h"
#include "EngineAccess.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "InputPost.h"
#include "Logger.h"
#include "PhysicalInteractions.h"
#include "PhysicalLane.h"
#include "PhysicsCalls.h"
#include "SceneQuery.h"
#include "XrInput.h"
#include "preyvr/ContactNative.h"
#include "preyvr/ContactQuery.h"
#include "preyvr/MonotonicClock.h"
#include "preyvr/PhysicalInteractions.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/SurfaceStop.h"
#include "preyvr/TextFormat.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace preyvr::dll {
namespace {
namespace cn = contact::native;
constexpr std::uint64_t kFreshNs = 100000000ull;
constexpr unsigned kLogLinesPerSecond = 20;
// A jump the memory cannot follow: a turn of more than 3 degrees in a frame (a snap
// turn swings every hand around the player), the grip 50 cm in a frame, a gap
// over 100 ms between tracking samples.
constexpr float kMaxTurn = .05f, kMaxGripJump = .5f, kMaxGap = .1f;
// A long weapon (its box longer than this) gets probes halfway along its length too.
constexpr float kLongWeapon = .3f;
// Heavy Objects (VR Options, Hands): props heavier than this hold a hand back, kg.
constexpr unsigned kHeavyKgMin = 1, kHeavyKgMax = 100;

std::atomic<bool> enabled{true}, weaponProbes{true}, eventLog{false};
std::atomic<unsigned> heavyKg{10};
std::atomic<std::uint64_t> settings{1};
std::atomic<std::uint64_t> frames{0}, solves{0}, rays{0}, raysMax{0}, held{0}, holds{0}, fresh{0}, moved{0}, unstuck{0},
    refused{0}, fallbacks{0}, limited{0}, unconverged{0}, backFaces{0}, resets{0}, weaponFrames{0}, offsetMaxMm{0},
    statics{0}, others{0}, heavyHolds{0}, immovableHolds{0}, presses{0}, passedLight{0}, passedBodies{0}, passedLive{0},
    unread{0}, usTotal{0}, usMax{0}, dropped{0};
std::atomic<const char*> lastGate{"none"};
std::atomic<int> objectsGate{-1};   // -1 not asked yet; 0 scenery alone; 1 loose objects too

void Log(const std::string& line) { lifecycle::Log("preyvr_surface " + line); }
const char* HandName(Hand hand) { return hand == Hand::left ? "left" : "right"; }

// --- what a ray meets -----------------------------------------------------------
// Loose objects are told apart by vtable, once the contact lane's contracts for
// them pass (GateLooseBodies), and weighed through their hashed GetStatus.
// Without those contracts the rays ask for fixed scenery alone.
struct Objects {
    bool ready = false;
    std::uintptr_t rigid = 0, articulated = 0;
    void* carried = nullptr;   // never met: the hand carrying it holds it
};
Objects ResolveObjects()
{
    Objects o{};
    int gate = objectsGate.load();
    if (gate < 0) {
        const DWORD result = GateLooseBodies();
        gate = result == ERROR_SUCCESS ? 1 : 0;
        objectsGate = gate;
        Log("objects gate=" + std::to_string(result) + " loose=" + std::to_string(gate));
    }
    const auto base = PreyBase();
    if (!gate || !base) { return o; }
    o.ready = true;
    o.rigid = base + cn::RigidVtable;
    o.articulated = base + cn::ArticulatedVtable;
    o.carried = reinterpret_cast<void*>(CarriedPhysics());
    return o;
}
Objects (*gObjects)() = ResolveObjects;   // the lane test replaces it

// What a ray met. Scenery, a heavy prop (heavier than Heavy Objects) and an
// immovable one (the game gave it no mass) hold a hand back. A light prop, a
// corpse (pushed limb by limb instead; a massless one is immovable), a live
// character's skeleton and a body whose mass cannot be read let the ray on.
enum class Met : std::uint8_t { scenery, heavy, immovable, light, body, live, unread };
bool Blocks(Met m) { return m == Met::scenery || m == Met::heavy || m == Met::immovable; }
const char* MetName(Met m)
{
    switch (m) {
    case Met::scenery: return "scenery";
    case Met::heavy: return "heavy";
    case Met::immovable: return "immovable";
    case Met::light: return "light";
    case Met::body: return "body";
    case Met::live: return "live";
    case Met::unread: break;
    }
    return "unread";
}
// Each object met is weighed once a frame.
struct Seen { const void* collider = nullptr; Met met = Met::scenery; float mass = 0; };
std::array<Seen, 16> seen{};
unsigned seenCount = 0;
Seen Classify(const Objects& o, const void* collider)
{
    for (unsigned i = 0; i < seenCount; ++i) {
        if (seen[i].collider == collider) { return seen[i]; }
    }
    Seen s{collider, Met::scenery, 0};
    const auto table = o.ready ? phys::VtableOf(collider) : 0;
    if (table && (table == o.rigid || table == o.articulated)) {
        const bool body = table == o.articulated;
        phys::Dynamics d{};
        if (body && phys::ArticulatedHosted(collider) != 0) { s.met = Met::live; }   // a host, or unreadable
        else if (!phys::QueryDynamics(reinterpret_cast<std::uintptr_t>(collider), d) || !std::isfinite(d.mass)) { s.met = Met::unread; }
        else {
            s.mass = d.mass;
            s.met = d.mass <= 0 ? Met::immovable
                    : body       ? Met::body
                    : d.mass > static_cast<float>(heavyKg.load()) ? Met::heavy
                                                                   : Met::light;
        }
    }
    if (seenCount < seen.size()) { seen[seenCount++] = s; }
    return s;
}

// The scene query's segment (SceneQuery.h): scenery and loose objects, or
// scenery alone without their contracts; the player and the carried object
// skipped. Replaceable for the lane test.
bool (*gQuery)(const GameplayPoseFrame&, Vec3, Vec3, void*, int, scene::Hit&) = QueryObjectsBetween;
// The query takes 1 mm to 2 m: a longer segment is cast in pieces.
constexpr float kPiece = 1.9f, kMinSegment = .001f;
// A ray goes on past what does not hold a hand back from just inside it (the
// engine's faces are one-sided: leaving, it does not meet it again), at most
// this many times; past that, the rest of the ray counts as clear.
constexpr unsigned kPassThrough = 4;
constexpr float kInside = .001f;
class WorldRays final : public surface::Rays {
public:
    WorldRays(const GameplayPoseFrame& frame, const Objects& objects) : frame_(frame), objects_(objects) {}
    bool Cast(Vec3 a, Vec3 b, surface::Hit& out) override
    {
        out = {};
        const Vec3 d = Sub(b, a);
        const float length = Length(d);
        if (!std::isfinite(length) || !(length > 0)) { return false; }
        const Vec3 dir = Scale(d, 1 / length);
        const int types = objects_.ready ? scene::SceneryAndLooseObjects : scene::SceneryObjects;
        float from = 0;
        unsigned passed = 0;
        while (length - from >= kMinSegment) {
            const float to = std::min(length, from + kPiece);
            scene::Hit hit{};
            if (!gQuery(frame_, Add(a, Scale(dir, from)), Add(a, Scale(dir, to)), objects_.carried, types, hit)) { return false; }
            if (hit.distance < 0) {
                from = to;
                continue;
            }
            const Seen s = Classify(objects_, hit.collider);
            if (Blocks(s.met)) {
                out.hit = true;
                out.distance = from + hit.distance;
                out.point = hit.point;
                out.normal = hit.normal;
                out.collider = hit.collider;
                out.surface = hit.surface;
                return true;
            }
            (s.met == Met::light ? passedLight : s.met == Met::body ? passedBodies : s.met == Met::live ? passedLive : unread) += 1;
            if (++passed > kPassThrough) { return true; }
            from += hit.distance + kInside;
        }
        return true;
    }

private:
    const GameplayPoseFrame& frame_;
    const Objects& objects_;
};

// The heavy prop holding a hand back hardest, and how far past its surface the
// real hand presses: what the contact lane pushes it with (ContactLane.h).
surface::Press PressOf(const Objects& o, const surface::Probes& real, const surface::Result& r)
{
    surface::Press press{};
    if (!r.correction.blocked) { return press; }
    for (unsigned i = 0; i < r.planeCount; ++i) {
        const auto& plane = r.planes[i];
        if (!plane.collider || plane.probe >= real.count || Classify(o, plane.collider).met != Met::heavy) { continue; }
        const float depth = Dot(Sub(plane.point, real.point[plane.probe]), plane.normal);
        if (depth > press.depth) { press = {plane.collider, plane.point, plane.normal, depth}; }
    }
    return press;
}

// GetType through the IPhysicalEntity interface (SwingLane.cpp's TypeOf), for the
// report: what held a hand back. -1 when it cannot be read.
int TypeOf(const void* collider)
{
    if (!collider) { return -1; }
    __try {
        const auto table = *reinterpret_cast<void* const* const*>(collider);
        return reinterpret_cast<int(__fastcall*)(const void*)>(table[cn::TypeSlot / sizeof(void*)])(collider);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// One hand's lane: its solver memory and what that memory belongs to.
struct HandLane {
    surface::State state{};
    std::uint64_t key = 0, reference = 0, epoch = 0;
    long long time = 0;
    float yaw = 0, dt = 0;
    bool held = false;
    std::uint64_t heldFrames = 0;
    float heldMaxCm = 0;
};
std::array<HandLane, 2> lanes{};   // by Hand
std::uint64_t seenSettings = 0, logSecondNs = 0;
unsigned logLines = 0;
lane::ReportThrottle reportThrottle;

bool LogBudget() { return lane::TakeLine(logSecondNs, logLines, kLogLinesPerSecond, dropped); }
void Forget(HandLane& lane)
{
    if (lane.state.valid) { ++resets; }
    lane = {};
}
void ForgetAll()
{
    for (auto& lane : lanes) { Forget(lane); }
}

// A hand this frame: its probes, the grip they hang from, and the memory key.
struct HandNow {
    bool active = false;
    const char* why = "";
    surface::Probes probes{};
    std::uint64_t key = 0;
    bool weapon = false;
};
// The weapon's box as last drawn (the takeover's own sample), carried to this
// frame's grip: where the controller holds it now, without the correction it was
// drawn with. Its eight corners and, on a long weapon, the middles of its four
// long edges.
bool AddWeapon(const GameplayPoseFrame& f, const WeaponBox& box, surface::Probes& p)
{
    const auto& right = f.tracking.hands[static_cast<unsigned>(Hand::right)];
    const Pose grip = stereo::ToEngineSpace(right.gripPose);
    // The play-space -> world map the hands are placed with (HandWorld.h,
    // on the camera centre).
    const Pose world = stereo::TrackingOriginInWorld(f.yaw, f.cameraCentre, f.tracking.head);
    const Vec3 drawnWith = Rotate(Conjugate(box.worldFromReference.orientation), box.correction);
    const Pose fromGrip = physical::Inverse(box.gripPose);
    std::array<Vec3, 9> now{};
    for (unsigned i = 1; i < now.size(); ++i) {
        const Vec3 inHand = physical::Transform(fromGrip, Sub(box.local[i], drawnWith));
        now[i] = physical::Transform(world, physical::Transform(grip, inHand));
        if (!Finite(now[i])) { return false; }
    }
    for (unsigned i = 1; i < now.size(); ++i) { p.Add(now[i]); }
    // Corners 1, 2 and 4 (bits) differ from corner 0 along each of the box's axes.
    unsigned longest = 0;
    float length = 0;
    for (unsigned k = 0; k < 3; ++k) {
        const float edge = Length(Sub(now[(1u << k) + 1], now[1]));
        if (edge > length) { length = edge; longest = k; }
    }
    if (length > kLongWeapon) {
        const unsigned bit = 1u << longest;
        for (unsigned c = 0; c < 8; ++c) {
            if (!(c & bit)) { p.Add(Scale(Add(now[c + 1], now[(c | bit) + 1]), .5f)); }
        }
    }
    return true;
}

// A hand's probes: its grip, then the palm box's eight corners (contact::HandBox,
// the contact lane's own). With a weapon drawn, the right hand keeps the box's
// back four -- the knuckles and the back of the hand, which stand out of a grip
// -- and the weapon's box joins them.
void BuildHand(const GameplayPoseFrame& f, Hand hand, const WeaponBox* weapon, HandNow& out)
{
    const auto& h = f.tracking.hands[static_cast<unsigned>(hand)];
    if (!IsPoseUsable(h.gripPose, h.gripValidity, kMaxPoseAgeNs)) { out.why = "hand_pose"; return; }
    Pose grip{};
    if (!HandWorld(f, hand, ControllerPose::grip, Anchor::camera, Placement::controller, grip)) { out.why = "camera"; return; }
    const auto& q = grip.orientation;
    if (!Finite(grip.position) || !std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) {
        out.why = "hand_pose";
        return;
    }
    const auto box = contact::BoxPoints(contact::HandBoxAt(grip, hand == Hand::left));
    out.probes.Add(grip.position);
    // The back of the right hand is the box's +x face (its palm faces -x).
    const bool armed = weapon != nullptr;
    for (unsigned i = 0; i < 8; ++i) {
        if (!armed || (i & 1)) { out.probes.Add(box[i + 1]); }
    }
    out.key = 1 + static_cast<unsigned>(hand);
    if (armed && AddWeapon(f, *weapon, out.probes)) {
        out.weapon = true;
        out.key = weapon->rig << 2 | 3;
    }
    out.active = true;
}

// Forget a hand's memory when this frame cannot follow from it. The same
// tracking sample again (a game frame between two samples) follows, catching up
// at last frame's pace.
void Follow(const GameplayPoseFrame& f, HandLane& lane, const HandNow& now, float& dt)
{
    dt = 0;
    if (!lane.state.valid) { return; }
    const long long time = f.tracking.displayTime;
    const float gap = time >= lane.time ? static_cast<float>(time - lane.time) * 1e-9f : -1.0f;
    if (lane.key != now.key || lane.reference != f.referenceGeneration || lane.epoch != f.tracking.epoch ||
        YawDistance(f.yaw, lane.yaw) > kMaxTurn || Length(Sub(now.probes.point[0], lane.state.grip)) > kMaxGripJump ||
        gap < 0 || gap > kMaxGap) {
        Forget(lane);
        return;
    }
    dt = gap > 0 ? gap : lane.dt;
}

// A hand starts being held back: counted by what holds it (scenery by its class,
// static or other; a heavy or immovable prop), and a line with `surface log 1`.
void LogHold(const Objects& o, Hand hand, const surface::Result& r)
{
    const surface::Plane* plane = r.planeCount ? &r.planes[0] : nullptr;
    const void* collider = plane ? plane->collider : r.first.collider;
    const Seen s = Classify(o, collider);
    const int type = s.met == Met::scenery ? TypeOf(collider) : -1;
    if (s.met == Met::heavy) { ++heavyHolds; }
    else if (s.met == Met::immovable) { ++immovableHolds; }
    else { (type == cn::TypeStatic ? statics : others) += 1; }
    if (!eventLog.load() || !LogBudget()) { return; }
    std::string line = std::string("hold hand=") + HandName(hand) + " path=" + surface::PathName(r.path) +
                       " offsetCm=" + Fixed(Length(r.correction.offset) * 100, 1) + " planes=" + std::to_string(r.planeCount) +
                       " collider=" + Hex(reinterpret_cast<std::uintptr_t>(collider)) + " met=" + MetName(s.met) +
                       (s.met == Met::scenery ? " type=" + std::to_string(type) : " mass=" + Fixed(s.mass, 1)) +
                       " surface=" + std::to_string(r.first.surface);
    if (plane) { line += " probe=" + std::to_string(plane->probe) + " point=" + Point(plane->point) + " normal=" + Point(plane->normal); }
    Log(line);
}

void Publish(const GameplayPoseFrame& frame, const char* gate, const std::array<HandNow, 2>& now,
             const std::array<surface::Result, 2>& result, std::uint64_t rayCount)
{
    if (!DebugOverlayWantsPhysical()) { return; }
    SurfaceOverlayState state{};
    for (unsigned h = 0; h < 2; ++h) {
        auto& hand = state.hands[h];
        hand.active = now[h].active;
        if (!hand.active) { continue; }
        hand.real = now[h].probes.point[0];
        hand.drawn = Add(hand.real, result[h].correction.offset);
        hand.held = result[h].correction.blocked;
        for (unsigned i = 0; i < result[h].planeCount && state.count < state.marks.size(); ++i) {
            state.marks[state.count++] = {result[h].planes[i].point, result[h].planes[i].normal};
        }
    }
    const auto f = frames.load();
    state.status = std::string(enabled.load() ? "on" : "off") + "  gate " + gate + "  held " +
                   (result[0].correction.blocked ? "L" : "-") + (result[1].correction.blocked ? "R" : "-") +
                   "  rays " + std::to_string(rayCount) + "  us avg " + std::to_string(f ? usTotal.load() / f : 0) +
                   " max " + std::to_string(usMax.load());
    DebugOverlaySurface(frame, state);
}

void MaybeReport()
{
    const auto counters = [] { return frames.load() + refused.load() + holds.load() + resets.load(); };
    if (reportThrottle.Due(counters, lastGate)) { Log("report" + SurfaceStopReport()); }
}

void UpdateImpl(GameplayPoseFrame& frame, bool allowed)
{
    MaybeReport();
    frame.surface = {};
    seenCount = 0;
    const auto generation = settings.load();
    if (generation != seenSettings) {
        seenSettings = generation;
        Log("settings enabled=" + std::to_string(enabled.load()) + " weapon=" + std::to_string(weaponProbes.load()) +
            " heavyKg=" + std::to_string(heavyKg.load()) + " log=" + std::to_string(eventLog.load()));
    }
    const auto now = MonotonicNanoseconds();
    const char* why = lane::GateReason(frame, allowed, enabled.load(), SceneQueryFaulted(), now, kFreshNs);
    if (!why && stereo::Recentering(frame.referenceGeneration)) { why = "pose"; }   // the surface stop's pose also refuses an odd generation
    std::array<HandNow, 2> hands{};
    std::array<surface::Result, 2> result{};
    lastGate = why ? why : "open";
    if (why) {
        ForgetAll();
        Publish(frame, why, hands, result, 0);
        return;
    }
    // The drawn weapon, if it is fresh and of this reference: the right hand's.
    WeaponBox box{};
    const bool haveWeapon = weaponProbes.load() && TryGetWeaponBox(box) && FreshSample(now, box.publishedNs, kFreshNs) &&
                            box.reference == frame.referenceGeneration && box.epoch == frame.tracking.epoch;
    BuildHand(frame, Hand::right, haveWeapon ? &box : nullptr, hands[static_cast<unsigned>(Hand::right)]);
    // A left hand on the foregrip is drawn on the weapon, which the right hand carries.
    if (TwoHandedAimEnabled() && frame.twoHand.snapSupport && frame.twoHand.blend >= .999f) {
        hands[static_cast<unsigned>(Hand::left)].why = "foregrip";
    } else {
        BuildHand(frame, Hand::left, nullptr, hands[static_cast<unsigned>(Hand::left)]);
    }
    if (hands[1].weapon) { ++weaponFrames; }
    const lane::Stopwatch watch;
    const Objects objects = gObjects();
    WorldRays world(frame, objects);
    std::uint64_t rayCount = 0;
    bool anyHeld = false;
    for (unsigned h = 0; h < 2; ++h) {
        const Hand hand = static_cast<Hand>(h);
        auto& lane = lanes[h];
        const auto& current = hands[h];
        if (!current.active) {
            if (lane.held && eventLog.load() && LogBudget()) {
                Log(std::string("free hand=") + HandName(hand) + " frames=" + std::to_string(lane.heldFrames) +
                    " maxCm=" + Fixed(lane.heldMaxCm, 1) + " why=" + current.why);
            }
            Forget(lane);
            continue;
        }
        float dt = 0;
        Follow(frame, lane, current, dt);
        auto& r = result[h];
        r = surface::Solve(frame.cameraCentre, current.probes, dt, lane.state, world);
        ++solves;
        rayCount += r.rays;
        backFaces += r.backFaces;
        switch (r.path) {
        case surface::Path::Fresh: ++fresh; break;
        case surface::Path::Moved: ++moved; break;
        case surface::Path::Unstuck: ++unstuck; break;
        case surface::Path::Refused: ++refused; break;
        }
        if (r.fallback) { ++fallbacks; }
        if (r.limited) { ++limited; }
        if (r.unconverged) { ++unconverged; }
        lane.key = current.key;
        lane.reference = frame.referenceGeneration;
        lane.epoch = frame.tracking.epoch;
        lane.time = frame.tracking.displayTime;
        lane.yaw = frame.yaw;
        if (dt > 0) { lane.dt = dt; }
        frame.surface[h] = r.correction;
        frame.surface[h].press = PressOf(objects, current.probes, r);
        if (frame.surface[h].press.object) { ++presses; }
        const float cm = Length(r.correction.offset) * 100;
        if (r.correction.blocked) {
            anyHeld = true;
            const auto mm = static_cast<std::uint64_t>(cm * 10);
            if (mm > offsetMaxMm.load()) { offsetMaxMm = mm; }
            if (!lane.held) {
                ++holds;
                lane.heldFrames = 0;
                lane.heldMaxCm = 0;
                LogHold(objects, hand, r);
            }
            ++lane.heldFrames;
            lane.heldMaxCm = std::max(lane.heldMaxCm, cm);
        } else if (lane.held && eventLog.load() && LogBudget()) {
            Log(std::string("free hand=") + HandName(hand) + " frames=" + std::to_string(lane.heldFrames) +
                " maxCm=" + Fixed(lane.heldMaxCm, 1) + " path=" + surface::PathName(r.path));
        }
        lane.held = r.correction.blocked;
    }
    const auto us = watch.Micros();
    ++frames;
    if (anyHeld) { ++held; }
    lane::AddMicros(us, usTotal, usMax);
    rays += rayCount;
    if (rayCount > raysMax.load()) { raysMax = rayCount; }
    Publish(frame, "open", hands, result, rayCount);
}
} // namespace

void UpdateSurfaceStop(GameplayPoseFrame& frame, bool allowed)
{
    thread_local bool busy = false;
    if (busy || !OnEngineMainThread()) {
        frame.surface = {};
        return;
    }
    const lane::ReentryGuard guard(busy);
    UpdateImpl(frame, allowed);
}
void SetSurfaceStop(unsigned v)
{
    enabled = v != 0;
    ++settings;
}
unsigned SurfaceStopEnabled() { return enabled.load() ? 1u : 0u; }
void SetSurfaceStopWeapon(unsigned on)
{
    weaponProbes = on != 0;
    ++settings;
}
unsigned SurfaceStopWeapon() { return weaponProbes.load() ? 1u : 0u; }
void SetSurfaceStopEventLog(unsigned on)
{
    eventLog = on != 0;
    ++settings;
}
unsigned SurfaceStopEventLog() { return eventLog.load() ? 1u : 0u; }
void SetSurfaceHeavyMass(unsigned kg)
{
    heavyKg = std::clamp(kg, kHeavyKgMin, kHeavyKgMax);
    ++settings;
}
unsigned SurfaceHeavyMass() { return heavyKg.load(); }
std::string SurfaceStopReport()
{
    const auto f = frames.load();
    return " enabled=" + std::to_string(enabled.load()) + " weapon=" + std::to_string(weaponProbes.load()) +
           " heavyKg=" + std::to_string(heavyKg.load()) + " loose=" + std::to_string(objectsGate.load()) +
           " frames=" + std::to_string(f) + " held=" + std::to_string(held.load()) + " holds=" + std::to_string(holds.load()) +
           " solves=" + std::to_string(solves.load()) + " fresh=" + std::to_string(fresh.load()) +
           " moved=" + std::to_string(moved.load()) + " unstuck=" + std::to_string(unstuck.load()) +
           " refused=" + std::to_string(refused.load()) + " fallback=" + std::to_string(fallbacks.load()) +
           " limited=" + std::to_string(limited.load()) + " unconverged=" + std::to_string(unconverged.load()) +
           " backFaces=" + std::to_string(backFaces.load()) + " resets=" + std::to_string(resets.load()) +
           " weaponFrames=" + std::to_string(weaponFrames.load()) + " static=" + std::to_string(statics.load()) +
           " other=" + std::to_string(others.load()) + " heavy=" + std::to_string(heavyHolds.load()) +
           " immovable=" + std::to_string(immovableHolds.load()) + " presses=" + std::to_string(presses.load()) +
           " passedLight=" + std::to_string(passedLight.load()) + " passedBodies=" + std::to_string(passedBodies.load()) +
           " passedLive=" + std::to_string(passedLive.load()) + " unread=" + std::to_string(unread.load()) +
           " offsetMaxCm=" + Fixed(static_cast<float>(offsetMaxMm.load()) / 10, 1) +
           " raysAvg=" + Fixed(f ? static_cast<float>(rays.load()) / static_cast<float>(f) : 0, 1) +
           " raysMax=" + std::to_string(raysMax.load()) + " usAvg=" + std::to_string(f ? usTotal.load() / f : 0) +
           " usMax=" + std::to_string(usMax.load()) + " log=" + std::to_string(eventLog.load()) +
           " droppedLines=" + std::to_string(dropped.load()) + " gate=" + lastGate.load();
}
} // namespace preyvr::dll
