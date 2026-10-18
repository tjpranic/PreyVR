#pragma once
#include "preyvr/SceneQuery.h"
#include "preyvr/VrMath.h"
#include <array>
#include <cstddef>
#include <cstdint>

// Physical contact, phase 1 (docs/PHYSICAL-CONTACT-SCOPE-2026-10-18.md): which
// movable objects the weapon box touches, found with the engine's own volume
// tests (ContactNative.h). Nothing is pushed yet. Pure geometry and bookkeeping
// here, testable offline; the DLL half is ContactLane.cpp.
namespace preyvr::contact {

// primitives::box as CollideEntityWithPrimitive reads it. The rows of `basis`
// are the box's axes in world space -- the native melee builds its box so, with
// row 1 its swing direction -- and `half` is the half-extent along each row.
// The engine zeroes `centre` while it builds its geometry and writes it back:
// every call gets its own copy.
struct BoxPrimitive {
    float basis[3][3]{};
    int oriented = 1;
    Vec3 centre{}, half{};
};
static_assert(sizeof(BoxPrimitive) == 0x40 && offsetof(BoxPrimitive, oriented) == 0x24 &&
              offsetof(BoxPrimitive, centre) == 0x28 && offsetof(BoxPrimitive, half) == 0x34);
// primitives::sphere (itype 4): centre +0x00 (the collide's sphere branch zeroes
// and restores it), radius +0x0C.
struct SpherePrimitive {
    Vec3 centre{};
    float radius = 0;
};
static_assert(sizeof(SpherePrimitive) == 0x10 && offsetof(SpherePrimitive, radius) == 0x0C);

// An oriented box in world space: unit axes and half-extents along them.
struct Box {
    Vec3 centre{};
    std::array<Vec3, 3> axis{};
    Vec3 half{};
};
// From the weapon sweep's nine points (physical::Bounds): the centre, then
// corner i+1 whose bits x/y/z pick the maximum on that axis. False when a side
// is shorter than a millimetre, the axes are not square, or a point is not finite.
bool BoxFromPoints(const std::array<Vec3, 9>& points, Box& out);
BoxPrimitive ToPrimitive(const Box& box);
// The world AABB holding the box at both poses, grown by `margin`.
void Bounds(const Box& a, const Box& b, float margin, Vec3& lo, Vec3& hi);
// The nine points of a box, as the weapon sweep publishes them: the centre, then
// corner i+1 whose bits x/y/z pick +half on that axis. BoxFromPoints' inverse.
std::array<Vec3, 9> BoxPoints(const Box& box);
// A hand as a box, from its controller's grip pose in world space. `axes` are the
// grip's own x, y, z (OpenXR: x across the palm, y up the hand, z along the
// handle) in world space. The palm faces +x on the left hand and -x on the right,
// and the handle sits in the fist 3.5 cm out of the palm (HandPose.h GripPoint):
// the box runs from the back of the hand to the fingers round the handle.
struct HandShape {
    float back = .065f;   // grip centre to the back of the hand, along the palm normal
    float front = .03f;   // grip centre to the fingers wrapped round it
    float up = .05f;      // half-size along y
    float along = .06f;   // half-size along z, the handle
};
Box HandBox(Vec3 grip, const std::array<Vec3, 3>& axes, bool left, const HandShape& shape = {});
// How fast the box's material at `point` (on the box at `now`) moved since
// `before`, dt seconds earlier: its translation, its turn and the player's own motion.
Vec3 PointVelocity(const Box& now, const Box& before, Vec3 point, float dt);

// Overlap: the box at this frame's pose, not moving. Sweep: last frame's box
// moved to this frame's, so a fast pass cannot skip through a thin object.
enum class Kind : std::uint8_t { Overlap, Sweep };
const char* KindName(Kind kind);
struct Contact {
    void* collider = nullptr;
    int type = 0;            // pe_type: 2 rigid, 6 articulated
    Kind kind = Kind::Overlap;
    scene::Hit hit{};        // dist, part, partId, surface, point, normal (out of the object)
    std::uint8_t proxy = 0;  // which box touched it: the lane's own numbering
};
// A native record this lane can use: about `collider`, finite, a unit normal.
bool Valid(const scene::Hit& hit, const void* collider);

// --- pushing (phase 2) ---------------------------------------------------------
// A rigid body as a push needs it, in world space: velocity and spin, centre of
// mass, inverse mass and world inverse inertia (row-major; zeros: linear only).
struct Body {
    Vec3 v{}, w{}, centre{};
    float inverseMass = 0;
    std::array<float, 9> inverseInertia{};
};
struct PushSettings {
    float force = 400;          // N: the most the hand pushes with (the strength setting scales it)
    float maxSpeed = 6;         // m/s: the fastest hand motion a push follows
    float slop = .005f;         // m: overlap left alone before the object is eased out
    float separation = 10;      // 1/s: easing-out speed per metre of overlap past the slop
    float maxSeparation = .5f;  // m/s
    float proxyFriction = .7f;  // the weapon's own surface: the engine's default friction
    float deadband = .01f;      // m/s: a slower approach is tracking jitter, not a push
};
struct Push {
    bool apply = false;
    Vec3 v{}, w{};              // the body's new velocity and spin
    Vec3 impulse{};             // N*s, as given
    float approach = 0;         // m/s: the proxy into the surface, relative to the surface
    bool limited = false;       // the force limit cut it: the body is heavy for this push
    bool sliding = false;       // friction could not hold the surface to the proxy
};
// The engine's friction for two surfaces, as its rigid contact solver combines
// them (0xBE5FD0): max(0, a + b) * 0.5.
float CombinedFriction(float a, float b);
// The velocity change that makes the body's surface at `point` keep up with the
// proxy along `normal` (out of the body, towards the proxy), easing out an overlap
// past the slop. Friction drags the surface along with the proxy's sideways motion
// up to CombinedFriction times the normal push, and 2/3 of that once it slides.
// The whole impulse is at most force * dt, so heavy bodies resist.
// Pure: the same body state gives the same result, so a velocity set twice before
// a physics step is set once.
Push Solve(const Body& body, Vec3 point, Vec3 normal, float depth, Vec3 proxyVelocity,
           float surfaceFriction, float dt, const PushSettings& settings);

// Which objects began and ended contact, frame to frame, for the log. An object
// unseen for `kGrace` frames has ended; the frame count is how long it lasted.
class Tracker {
public:
    static constexpr unsigned kCapacity = 32, kGrace = 3;
    struct Event { const void* collider = nullptr; bool begin = false; unsigned frames = 0; };
    // This frame's touched colliders (repeats allowed). Writes up to `capacity`
    // events and returns how many; an object it has no room to track is not reported.
    unsigned Update(const void* const* colliders, unsigned count, Event* events, unsigned capacity);
    // Ends everything tracked (the lane switching off, a menu, a weapon change).
    unsigned Reset(Event* events, unsigned capacity);
    unsigned Tracked() const;

private:
    struct Entry { const void* collider = nullptr; unsigned frames = 0, missed = 0; };
    std::array<Entry, kCapacity> entries_{};
};

} // namespace preyvr::contact
