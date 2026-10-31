// Surface stop: the solve against a fake world of closed boxes. A ray starting
// inside a box does not see it, as the engine's one-sided triangles do not.
#include "physics/contact/SurfaceStop.h"
#include "TestSupport.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
using namespace preyvr;
using namespace preyvr::surface;
using preyvr::test::Check;
namespace {
bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
bool NearV(Vec3 a, Vec3 b, float e = 1e-3f) { return Near(a.x, b.x, e) && Near(a.y, b.y, e) && Near(a.z, b.z, e); }
constexpr float kDt = 1.0f / 90.0f;

struct Block { Vec3 lo, hi; };
class World final : public Rays {
public:
    std::vector<Block> blocks;
    unsigned casts = 0;
    bool fail = false;
    bool backwards = false;   // report every normal along the ray, as a back face would be
    bool Cast(Vec3 a, Vec3 b, Hit& hit) override
    {
        ++casts;
        hit = {};
        if (fail) { return false; }
        const Vec3 d = Sub(b, a);
        float best = 2;
        Vec3 normal{};
        for (const auto& k : blocks) {
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
            normal = {};
            (&normal.x)[axis] = v[axis] > 0 ? -1.0f : 1.0f;
        }
        if (best > 1) { return true; }
        hit.hit = true;
        hit.distance = best * Length(d);
        hit.point = Add(a, Scale(d, best));
        hit.normal = backwards ? Scale(normal, -1) : normal;
        return true;
    }
};

// A hand: its grip, then the eight corners of a 10 cm cube 3 cm ahead (+x) of it.
Probes HandAt(Vec3 grip)
{
    Probes p{};
    p.Add(grip);
    for (unsigned i = 0; i < 8; ++i) {
        p.Add(Add(grip, {i & 1 ? .08f : -.02f, i & 2 ? .05f : -.05f, i & 4 ? .05f : -.05f}));
    }
    return p;
}
float MaxX(const Probes& p, Vec3 c)
{
    float m = -1e9f;
    for (unsigned i = 0; i < p.count; ++i) { m = std::max(m, p.point[i].x + c.x); }
    return m;
}
float MinZ(const Probes& p, Vec3 c)
{
    float m = 1e9f;
    for (unsigned i = 0; i < p.count; ++i) { m = std::min(m, p.point[i].z + c.z); }
    return m;
}
const Vec3 kEye{0, 0, 1.6f};
const Settings kSettings{};
}

int main()
{
    // --- Resolve ---
    {
        Probes p{};
        p.Add({1.1f, 0, 1});
        p.Add({1.2f, 0, 1});
        const Plane wall{1, {1, 0, 1}, {-1, 0, 0}};
        Vec3 c = Resolve(p, &wall, 1, {}, .005f);
        Check(NearV(c, {-.205f, 0, 0}), "the deepest probe's plane pushes the hand out to the skin");
        const Plane floor{0, {0, 0, 1.05f}, {0, 0, 1}};
        const Plane both[2] = {wall, floor};
        c = Resolve(p, both, 2, {}, .005f);
        Check(NearV(c, {-.205f, 0, .055f}), "a corner of two planes settles on both");
        c = Resolve(p, &wall, 1, {-.5f, 0, 0}, .005f);
        Check(NearV(c, {-.5f, 0, 0}), "a start already in front of the plane is kept");
    }

    // --- open air ---
    World world{};
    State state{};
    {
        Vec3 grip{.5f, 0, 1.2f};
        auto r = Solve(kEye, HandAt(grip), 0, state, world);
        Check(r.path == Path::Fresh && !r.correction.blocked && r.rays == 9 && state.valid,
              "first frame in the open: placed from the camera, nothing met, one ray per probe");
        world.casts = 0;
        grip.x += .02f;
        r = Solve(kEye, HandAt(grip), kDt, state, world);
        Check(r.path == Path::Moved && !r.correction.blocked && r.rays == 9, "moving in the open: one path per probe");
        r = Solve(kEye, HandAt(grip), kDt, state, world);
        Check(r.rays == 0 && !r.correction.blocked, "a hand held still casts nothing");
    }

    // --- a wall: x from 1.0 to 1.2 ---
    world.blocks = {{{1, -2, 0}, {1.2f, 2, 3}}};
    state.Reset();
    {
        Vec3 grip{.85f, 0, 1.2f};
        Solve(kEye, HandAt(grip), 0, state, world);
        grip.x = 1.0f;   // fingers 8 cm into the wall
        auto r = Solve(kEye, HandAt(grip), kDt, state, world);
        Check(r.path == Path::Moved && r.correction.blocked && Near(MaxX(HandAt(grip), r.correction.offset), 1 - kSettings.skin),
              "pushed into a wall: the hand stops a skin short of it");
        Check(Near(r.correction.offset.y, 0) && Near(r.correction.offset.z, 0), "pushed straight out of the wall");
        // Sliding along it while pressing in: the drawn hand follows sideways.
        grip = {1.05f, .1f, 1.3f};
        r = Solve(kEye, HandAt(grip), kDt, state, world);
        const Vec3 drawn = Add(grip, r.correction.offset);
        Check(Near(MaxX(HandAt(grip), r.correction.offset), 1 - kSettings.skin) && Near(drawn.y, .1f) && Near(drawn.z, 1.3f),
              "pressing in and sliding: the drawn hand slides along the wall under the real one");
        // Deep behind it, for a long time: held there (the camera cannot see the grip).
        grip = {1.6f, .1f, 1.3f};
        for (int i = 0; i < 60; ++i) { r = Solve(kEye, HandAt(grip), kDt, state, world); }
        Check(r.correction.blocked && Near(MaxX(HandAt(grip), r.correction.offset), 1 - kSettings.skin) && r.path == Path::Moved,
              "the real hand far through the wall: the drawn one stays at the surface");
        // Pulled back out: the drawn hand is the real one again, without a jump past
        // it. The paths start behind the drawn probes, inside the wall: unseen.
        grip = {.85f, .1f, 1.3f};
        r = Solve(kEye, HandAt(grip), kDt, state, world);
        Check(!r.correction.blocked && !r.limited, "the real hand back in the open: no correction");
    }

    // --- a thin wall, crossed in one frame ---
    world.blocks = {{{1, -2, 0}, {1.03f, 2, 3}}};
    state.Reset();
    {
        Vec3 grip{.85f, 0, 1.2f};
        Solve(kEye, HandAt(grip), 0, state, world);
        grip.x = 1.15f;   // the whole hand past the 3 cm wall in one 30 cm step
        const auto r = Solve(kEye, HandAt(grip), kDt, state, world);
        Check(r.correction.blocked && Near(MaxX(HandAt(grip), r.correction.offset), 1 - kSettings.skin),
              "a fast pass through a thin wall still stops at its near face");
    }

    // --- placed fresh inside a wall (after a reset): out along the camera's ray's plane ---
    world.blocks = {{{1, -2, 0}, {1.2f, 2, 3}}};
    state.Reset();
    {
        const Vec3 grip{1.1f, .2f, 1.2f};
        const auto r = Solve(kEye, HandAt(grip), 0, state, world);
        Check(r.path == Path::Fresh && r.correction.blocked && Near(MaxX(HandAt(grip), r.correction.offset), 1 - kSettings.skin) &&
                  !r.fallback,
              "a first frame in the wall: pushed out to its face");
        Settings once{};
        once.rounds = 1;
        state.Reset();
        const auto f = Solve(kEye, HandAt(grip), 0, state, world, once);
        const Vec3 d = Sub(grip, kEye);
        const Vec3 drawn = Add(grip, f.correction.offset);
        Check(f.fallback && Near(Length(Cross(Sub(drawn, kEye), d)) / Length(d), 0) && drawn.x < 1 && drawn.x > .99f,
              "an unchecked push falls back to the camera's ray, short of the wall");
    }

    // --- a desk: a 5 cm top at z 0.70-0.75, from x 0.6 to 1.4 ---
    world.blocks = {{{.6f, -1, .7f}, {1.4f, 1, .75f}}};
    state.Reset();
    {
        Vec3 grip{.9f, 0, .9f};
        Solve(kEye, HandAt(grip), 0, state, world);
        grip.z = .7f;   // pressing down into the top
        auto r = Solve(kEye, HandAt(grip), kDt, state, world);
        Check(Near(MinZ(HandAt(grip), r.correction.offset), .75f + kSettings.skin) && Near(r.correction.offset.x, 0),
              "pressing on a desk: the hand rests on its top");
        // Reaching under the top from beside the desk: the path stays clear, so
        // nothing holds it, though by the end the camera's line to the grip
        // crosses the top (at x 0.73).
        state.Reset();
        grip = {.4f, 0, .55f};
        Solve(kEye, HandAt(grip), 0, state, world);
        for (int i = 0; i < 20; ++i) {
            grip.x += .025f;
            r = Solve(kEye, HandAt(grip), kDt, state, world);
        }
        Check(!r.correction.blocked && grip.x > .89f, "reaching under the desk top from its side is not held");
    }

    // --- catching up: off the desk's edge ---
    {
        state.Reset();
        Vec3 grip{1.3f, 0, .9f};
        Solve(kEye, HandAt(grip), 0, state, world);
        grip = {1.3f, 0, .6f};   // 15 cm down through the top, by its far edge
        auto r = Solve(kEye, HandAt(grip), kDt, state, world);
        Check(r.correction.blocked && Near(r.correction.offset.z, .75f + kSettings.skin - .55f), "held on the top");
        // Past the edge, still below the top: it first slides off along the top...
        grip.x = 1.5f;
        r = Solve(kEye, HandAt(grip), kDt, state, world);
        Vec3 drawn = Add(grip, r.correction.offset);
        Check(r.correction.blocked && !r.limited && Near(drawn.z, .805f) && Near(drawn.x, 1.5f),
              "past the edge: it slides off along the top");
        // ...then drops to the real hand no faster than the catch-up speed.
        r = Solve(kEye, HandAt(grip), kDt, state, world);
        const Vec3 next = Add(grip, r.correction.offset);
        Check(r.limited && r.correction.blocked && Near(Length(Sub(next, drawn)), kSettings.catchUp * kDt),
              "let go: the drawn hand catches up instead of jumping");
        int frames = 1;
        while (r.correction.blocked && frames < 30) {
            r = Solve(kEye, HandAt(grip), kDt, state, world);
            ++frames;
        }
        Check(!r.correction.blocked && frames >= 9 && frames <= 11, "it reaches the real hand in about a tenth of a second");
    }

    // --- a pole across a long weapon, swept sideways into it ---
    world.blocks = {{{.98f, -.6f, 0}, {1.02f, -.56f, 3}}};   // 4 cm square, at y -0.58
    state.Reset();
    {
        // A 1 m weapon along +x at y 0, from its grip at x 0.3: probes every 10 cm.
        auto weapon = [](float y) {
            Probes p{};
            for (int i = 0; i <= 10; ++i) { p.Add({.3f + .1f * static_cast<float>(i), y, 1.3f}); }
            return p;
        };
        float y = -.4f;
        Solve(kEye, weapon(y), 0, state, world);
        Result r{};
        for (int i = 0; i < 8; ++i) {
            y -= .05f;   // sideways, toward the pole
            r = Solve(kEye, weapon(y), kDt, state, world);
        }
        const Vec3 drawn = Add(weapon(y).point[7], r.correction.offset);
        Check(r.correction.blocked && drawn.y > -.56f && drawn.y < -.55f && std::fabs(r.correction.offset.x) < .01f,
              "swept sideways into a pole: the weapon stops beside it, not pulled back along its length");
    }

    // --- stuck on the far side of a wall while the real hand is in the open ---
    world.blocks = {{{1, -1, 0}, {1.5f, 1, 3}}};
    state.Reset();
    {
        // Drawn beyond the block, as if it had gone round: free there.
        const Vec3 behind{1.6f, 0, 1.0f};
        state.valid = true;
        state.drawn = HandAt(behind);
        state.grip = behind;
        const Vec3 real{.5f, 0, 1.0f};   // the real hand on the camera's side
        Result r{};
        int frames = 0;
        do {
            r = Solve(kEye, HandAt(real), kDt, state, world);
            ++frames;
        } while (r.correction.blocked && frames < 100);
        Check(r.path == Path::Unstuck && frames == 30, "held while the real hand is in the open: let go after 30 frames");
        r = Solve(kEye, HandAt(real), kDt, state, world);
        Check(!r.correction.blocked && r.rays == 0, "and then follows it again");
    }

    // --- refusals ---
    {
        world.fail = true;
        state.Reset();
        auto r = Solve(kEye, HandAt({.5f, 0, 1}), kDt, state, world);
        Check(r.path == Path::Refused && !r.correction.blocked && !state.valid, "a query that cannot run: no correction, memory gone");
        world.fail = false;
        world.backwards = true;
        world.blocks = {{{1, -2, 0}, {1.2f, 2, 3}}};
        r = Solve(kEye, HandAt({1.1f, 0, 1.2f}), 0, state, world);
        Check(!r.correction.blocked && r.backFaces > 0, "a surface the ray leaves (normal along it) holds nothing");
        world.backwards = false;
        Probes bad = HandAt({.5f, 0, 1});
        bad.point[3].x = NAN;
        r = Solve(kEye, bad, kDt, state, world);
        Check(r.path == Path::Refused && !state.valid, "a non-finite probe is refused");
    }
    Check(std::string(PathName(Path::Unstuck)) == "unstuck" && std::string(PathName(Path::Refused)) == "refused", "path names");
    std::cout << "surface_stop: ok\n";
    return 0;
}
