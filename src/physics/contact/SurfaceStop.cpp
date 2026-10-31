#include "physics/contact/SurfaceStop.h"

#include <algorithm>
#include <cmath>

namespace preyvr::surface {
namespace {
constexpr float kMinRay = .001f;   // m: the engine's scenery query refuses a shorter segment
constexpr float kHeld = .0005f;    // m: a smaller correction is none
// Held back while the camera sees the real hand in the open for this many frames
// in a row: the drawn hand is stuck on the far side of something (a thin wall the
// real hand went through, then round) and is let go. Sliding along a surface frees
// a hand that can be freed sooner than this.
constexpr unsigned kUnstickFrames = 30;

// Every cast goes through here: it is counted, a surface counts only if the ray
// enters it (its normal faces back along the ray), and the solve stops once a
// query could not run.
class Caster {
public:
    Caster(Rays& rays, Result& result) : rays_(rays), result_(result) {}
    bool Ok() const { return ok_; }
    // True: a surface between a and b, in `hit`.
    bool Cast(Vec3 a, Vec3 b, Hit& hit)
    {
        hit = {};
        const Vec3 d = Sub(b, a);
        if (!ok_ || !(Length(d) >= kMinRay)) { return false; }
        ++result_.rays;
        if (!rays_.Cast(a, b, hit)) {
            ok_ = false;
            hit = {};
            return false;
        }
        if (!hit.hit) { return false; }
        const float n = Length(hit.normal);
        if (!Finite(hit.point) || !std::isfinite(hit.distance) || !(n > .5f)) {
            hit = {};
            return false;
        }
        hit.normal = Scale(hit.normal, 1 / n);
        if (Dot(hit.normal, d) >= 0) {
            ++result_.backFaces;
            hit = {};
            return false;
        }
        if (!result_.first.hit) { result_.first = hit; }
        return true;
    }

private:
    Rays& rays_;
    Result& result_;
    bool ok_ = true;
};

void Keep(Result& r, unsigned probe, const Hit& hit)
{
    if (r.planeCount < r.planes.size()) { r.planes[r.planeCount++] = {probe, hit.point, hit.normal, hit.collider}; }
}

// The real hand, moved by `c`, as the camera sees it: the camera to its grip,
// then the grip to each probe. True when nothing is met. With `keep`, what is met
// becomes planes. A grip the camera cannot see gives its plane alone: rays from
// behind a surface say nothing about where the hand should be.
bool Open(Caster& cast, Vec3 eye, const Probes& p, Vec3 c, Result* keep, Hit* gripHit = nullptr)
{
    Hit hit{};
    const Vec3 grip = Add(p.point[0], c);
    if (cast.Cast(eye, grip, hit)) {
        if (keep) { Keep(*keep, 0, hit); }
        if (gripHit) { *gripHit = hit; }
        return false;
    }
    bool open = true;
    for (unsigned i = 1; i < p.count && cast.Ok(); ++i) {
        if (!cast.Cast(grip, Add(p.point[i], c), hit)) { continue; }
        open = false;
        if (keep) { Keep(*keep, i, hit); }
    }
    return open && cast.Ok();
}

// No memory of where the hand was drawn: place it from the camera's view of the
// real hand. Each round pushes it out of what it met and looks again from there.
Vec3 Fresh(Caster& cast, Vec3 eye, const Probes& p, Result& r, const Settings& s)
{
    const unsigned rounds = std::max(1u, s.rounds);
    Hit firstGrip{};
    Vec3 c{}, seen{};   // seen: the latest correction whose grip the camera saw
    bool open = false, gripSeen = false;
    for (unsigned round = 0; round < rounds && cast.Ok(); ++round) {
        Hit gripHit{};
        open = Open(cast, eye, p, c, &r, &gripHit);
        if (!cast.Ok()) { return {}; }
        gripSeen = !gripHit.hit;
        if (gripSeen) { seen = c; }
        if (round == 0 && gripHit.hit) { firstGrip = gripHit; }
        if (open || round + 1 == rounds) { break; }
        c = Resolve(p, r.planes.data(), r.planeCount, c, s.skin);
    }
    if (open && Length(c) <= s.maxOffset) { return c; }
    r.unconverged = !open;
    // The camera sees the grip where the push put it: as good as this frame gets.
    if (gripSeen && Length(c) <= s.maxOffset) { return c; }
    // Otherwise stop the grip along the camera's first ray, short of what it met.
    r.fallback = true;
    if (firstGrip.hit) {
        const Vec3 d = Sub(p.point[0], eye);
        const float length = Length(d);
        if (length > kMinRay) {
            const float along = std::max(0.0f, firstGrip.distance - s.skin);
            return Sub(Add(eye, Scale(d, along / length)), p.point[0]);
        }
    }
    return Length(seen) <= s.maxOffset ? seen : Vec3{};
}

// Each probe's path from where it was drawn to where it goes now (the real
// hand, moved by `c`), started `backoff` behind the drawn point so a probe
// resting on a surface still meets it. True when any path met a surface; what
// was met becomes planes.
bool Travel(Caster& cast, const Probes& p, const State& st, Vec3 c, Result& r, const Settings& s)
{
    bool met = false;
    for (unsigned i = 0; i < p.count && cast.Ok(); ++i) {
        const Vec3 from = st.drawn.point[i], to = Add(p.point[i], c);
        const Vec3 d = Sub(to, from);
        const float length = Length(d);
        if (!(length >= kMinRay)) { continue; }
        Hit hit{};
        if (cast.Cast(Sub(from, Scale(d, s.backoff / length)), to, hit)) {
            Keep(r, i, hit);
            met = true;
        }
    }
    return met;
}

// From memory: the hand goes where the controller puts it, pushed out of what
// its probes meet on the way. A held-back hand let go catches up at most
// `catchUp` faster than the real hand moves, along a path that is cast again.
Vec3 Moved(Caster& cast, const Probes& p, const State& st, float dt, Result& r, const Settings& s)
{
    const unsigned rounds = std::max(1u, s.rounds);
    Vec3 c{};
    bool met = true;
    for (unsigned round = 0; round < rounds && met && cast.Ok(); ++round) {
        met = Travel(cast, p, st, c, r, s);
        if (met) { c = Resolve(p, r.planes.data(), r.planeCount, c, s.skin); }
    }
    r.unconverged = met;
    if (!cast.Ok() || !(dt > 0) || !std::isfinite(dt)) { return c; }
    const Vec3 jump = Sub(Add(p.point[0], c), st.drawn.point[0]);
    const float allowed = Length(Sub(p.point[0], st.grip)) + s.catchUp * dt;
    const float length = Length(jump);
    if (length > allowed + 1e-4f) {
        c = Sub(Add(st.drawn.point[0], Scale(jump, allowed / length)), p.point[0]);
        r.limited = true;
        // A shorter move is a different one when the hand turned: cast it.
        if (Travel(cast, p, st, c, r, s)) { c = Resolve(p, r.planes.data(), r.planeCount, c, s.skin); }
    }
    return c;
}
} // namespace

Vec3 Resolve(const Probes& probes, const Plane* planes, unsigned count, Vec3 start, float skin, unsigned passes)
{
    Vec3 c = start;
    for (unsigned pass = 0; pass < passes; ++pass) {
        bool moved = false;
        for (unsigned k = 0; k < count; ++k) {
            const Plane& plane = planes[k];
            if (plane.probe >= probes.count) { continue; }
            const float depth = skin - Dot(Sub(Add(probes.point[plane.probe], c), plane.point), plane.normal);
            if (depth > 1e-5f) {
                c = Add(c, Scale(plane.normal, depth));
                moved = true;
            }
        }
        if (!moved) { break; }
    }
    return c;
}

const char* PathName(Path path)
{
    switch (path) {
    case Path::Fresh: return "fresh";
    case Path::Moved: return "moved";
    case Path::Unstuck: return "unstuck";
    case Path::Refused: break;
    }
    return "refused";
}

Result Solve(Vec3 eye, const Probes& p, float dt, State& st, Rays& rays, const Settings& s)
{
    Result r{};
    bool usable = p.count >= 1 && p.count <= kMaxProbes && Finite(eye);
    for (unsigned i = 0; usable && i < p.count; ++i) { usable = Finite(p.point[i]); }
    if (!usable) {
        st.Reset();
        return r;
    }
    Caster cast(rays, r);
    Vec3 c{};
    if (st.valid && st.drawn.count == p.count) {
        r.path = Path::Moved;
        c = Moved(cast, p, st, dt, r, s);
        if (cast.Ok() && Length(c) > s.maxOffset) {
            // Not trusted: place it again from the camera.
            r.planeCount = 0;
            r.limited = r.unconverged = false;
            r.path = Path::Fresh;
            c = Fresh(cast, eye, p, r, s);
        }
    } else {
        r.path = Path::Fresh;
        c = Fresh(cast, eye, p, r, s);
    }
    // Held back while the real hand is in the open: let go, after a while.
    if (cast.Ok() && r.path == Path::Moved && Length(c) > kHeld && Open(cast, eye, p, {}, nullptr)) {
        if (++st.openFrames >= kUnstickFrames) {
            c = {};
            r.path = Path::Unstuck;
        }
    } else {
        st.openFrames = 0;
    }
    if (!cast.Ok()) {
        st.Reset();
        r.path = Path::Refused;
        r.correction = {};
        r.planeCount = 0;
        return r;
    }
    if (!Finite(c) || Length(c) <= kHeld) { c = {}; }
    r.correction = {c, Length(c) > 0};
    st.valid = true;
    st.drawn.count = p.count;
    for (unsigned i = 0; i < p.count; ++i) { st.drawn.point[i] = Add(p.point[i], c); }
    st.grip = p.point[0];
    if (r.path == Path::Unstuck) { st.openFrames = 0; }
    return r;
}

} // namespace preyvr::surface
