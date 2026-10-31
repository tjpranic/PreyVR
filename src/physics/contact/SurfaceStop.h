#pragma once
#include "base/VrMath.h"
#include <array>
#include <cstdint>

// Surface stop (docs/SURFACE-STOP-SCOPE-2026-10-21.md): the drawn hands and weapon
// stop against fixed scenery while the tracked hand goes on. Pure geometry and
// bookkeeping here, tested offline against a fake world; the DLL half, which casts
// the engine's scenery rays, is SurfaceStopLane.cpp.
//
// A hand is a set of probes: points rigidly attached to it (its palm box's
// corners, its weapon's box), probe 0 its grip. Each frame every probe travels
// from where it was drawn last frame to where the controller puts it, along a
// scenery ray. A ray that meets a surface gives that probe a plane, and the hand
// is pushed out of each plane along its normal: one translation for the whole
// hand, so it slides along a wall or a desk top under the real hand. Whether the
// real hand is in the open -- the camera sees its grip, and the grip each probe --
// decides when it is let go, and places it the first frame.
namespace preyvr::surface {

// A heavy loose object holding a hand back, for the contact lane to push: where
// the hand meets it, its surface's normal (out of it), and how far past that
// surface the real hand presses.
struct Press {
    const void* object = nullptr;
    Vec3 point{}, normal{};
    float depth = 0;   // m
};
// One hand's correction, carried in the gameplay frame to every consumer: the
// world translation from where the controller puts the hand to where it is
// drawn. Zero when nothing holds it back, or the lane is off.
struct Correction {
    Vec3 offset{};
    bool blocked = false;
    Press press{};
};

// One scenery ray's answer. `normal` faces back along the ray (out of the surface).
struct Hit {
    bool hit = false;
    float distance = 0;
    Vec3 point{}, normal{};
    const void* collider = nullptr;   // for the report only
    int surface = -1;
};
// The world, as the solve sees it: fixed scenery on the segment a -> b. False
// when the query could not run; the solve then gives up for this frame.
class Rays {
public:
    virtual bool Cast(Vec3 a, Vec3 b, Hit& hit) = 0;

protected:
    ~Rays() = default;
};

inline constexpr unsigned kMaxProbes = 24, kMaxPlanes = 32;
struct Probes {
    std::array<Vec3, kMaxProbes> point{};   // engine world; point[0] is the grip
    unsigned count = 0;
    bool Add(Vec3 p)
    {
        if (count >= point.size()) { return false; }
        point[count++] = p;
        return true;
    }
};
// A surface one probe met: a point on it, its normal (out of the surface) and
// what it belongs to (Hit::collider).
struct Plane {
    unsigned probe = 0;
    Vec3 point{}, normal{};
    const void* collider = nullptr;
};

struct Settings {
    float skin = .005f;        // m kept between a stopped probe and its surface
    unsigned rounds = 3;       // ray rounds a solve may take: each push is cast again
    float maxOffset = 1.5f;    // m: a larger correction is not trusted
    float catchUp = 2.0f;      // m/s beyond the hand's own motion, while a held-back hand catches up
    float backoff = .02f;      // m: a probe's path starts this far behind where it was drawn
};

// The translation, from `start`, that keeps each plane's probe `skin` in front
// of it: each plane in turn pushes the hand out along its normal, over a few
// passes so a corner of two or three planes settles.
Vec3 Resolve(const Probes& probes, const Plane* planes, unsigned count, Vec3 start, float skin, unsigned passes = 4);

// One hand's memory between frames: where each probe was drawn, the grip the
// controller gave, and how many frames in a row the hand was held back while
// the camera saw the real one in the open.
struct State {
    bool valid = false;
    Probes drawn{};
    Vec3 grip{};
    unsigned openFrames = 0;
    void Reset() { *this = {}; }
};

// How this frame's correction was found.
// Fresh: no usable memory; placed from the camera's view of the real hand.
// Moved: each probe travelled from where it was drawn.
// Unstuck: held back while the real hand stayed in the open (30 frames): let go.
// Refused: a ray could not run (or the probes were unusable); no correction.
enum class Path : std::uint8_t { Fresh, Moved, Unstuck, Refused };
const char* PathName(Path path);

struct Result {
    Correction correction{};
    Path path = Path::Refused;
    unsigned rays = 0;
    std::array<Plane, kMaxPlanes> planes{};   // what held it back this frame
    unsigned planeCount = 0;
    bool fallback = false;      // the push was not trusted: stopped along the camera's ray instead
    bool limited = false;       // catching up, slower than the jump it wanted
    bool unconverged = false;   // the last ray round still met something
    unsigned backFaces = 0;     // hits facing along the ray (a ray leaving a surface): ignored
    Hit first{};                // the first surface met, for the report
};

// One hand, one frame. `eye` is on the player's side of every surface (the
// camera); `probes` where the controller puts the hand; `dt` seconds since the
// last solve (0 when unknown: no catching up is limited). The caller resets
// `state` on any jump: a recentre, a snap turn, a gap, a new weapon.
Result Solve(Vec3 eye, const Probes& probes, float dt, State& state, Rays& rays, const Settings& settings = {});

} // namespace preyvr::surface
