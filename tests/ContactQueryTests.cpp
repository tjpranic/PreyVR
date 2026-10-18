// Physical contact, phase 1: the box handed to the engine, its broadphase bounds,
// which native records are usable, and the begin/end tracking the log reports.
#include "preyvr/ContactQuery.h"
#include "preyvr/PhysicalInteractions.h"
#include "TestSupport.h"
#include <cmath>
#include <iostream>
using namespace preyvr;
using preyvr::test::Check;
namespace {
bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }
bool NearV(Vec3 a, Vec3 b, float e = 1e-4f) { return Near(a.x, b.x, e) && Near(a.y, b.y, e) && Near(a.z, b.z, e); }
// The nine points the weapon sweep publishes for a box rotated by q about c.
std::array<Vec3, 9> Points(Vec3 lo, Vec3 hi, Quaternion q, Vec3 c)
{
    const auto local = physical::Bounds(lo, hi);
    std::array<Vec3, 9> out{};
    for (unsigned i = 0; i < 9; ++i) { out[i] = Add(c, Rotate(q, local[i])); }
    return out;
}
}
int main()
{
    using namespace contact;
    const float s = std::sin(.35f), co = std::cos(.35f);
    const Quaternion q = Normalize(Quaternion{.2f * s, .1f * s, .97f * s, co});
    const Vec3 lo{-.06f, -.06f, -.1f}, hi{.06f, .06f, .34f}, at{860, 1579, 7.7f};
    Box box{};
    Check(BoxFromPoints(Points(lo, hi, q, at), box), "the wrench's box rebuilds");
    Check(NearV(box.half, {.06f, .06f, .22f}), "half-extents along the box's own axes");
    // Floats at ~1600 m hold ~0.1 mm, so axes rebuilt from 12 cm edges are good to ~0.1%.
    Check(NearV(box.axis[0], Rotate(q, {1, 0, 0}), 2e-3f) && NearV(box.axis[2], Rotate(q, {0, 0, 1}), 2e-3f),
          "axes are the rotated local axes");
    Check(NearV(box.centre, Add(at, Rotate(q, {0, 0, .12f}))), "centre is the bounds' centre, not the grip");
    // The engine reads rows of Basis as the box's axes: Basis * (p - centre) is local.
    const auto p = ToPrimitive(box);
    const Vec3 world = Add(box.centre, Add(Scale(box.axis[0], .05f), Scale(box.axis[2], -.2f)));
    const Vec3 d = Sub(world, p.centre);
    const Vec3 local{p.basis[0][0] * d.x + p.basis[0][1] * d.y + p.basis[0][2] * d.z,
                     p.basis[1][0] * d.x + p.basis[1][1] * d.y + p.basis[1][2] * d.z,
                     p.basis[2][0] * d.x + p.basis[2][1] * d.y + p.basis[2][2] * d.z};
    Check(NearV(local, {.05f, 0, -.2f}) && p.oriented == 1 && NearV(p.half, box.half), "native box layout: rows are axes");
    auto flat = Points(lo, hi, q, at);
    for (unsigned i = 1; i < 9; ++i) { flat[i].z = flat[1].z; flat[i].x = flat[1].x; }
    Check(!BoxFromPoints(flat, box), "a collapsed box is refused");
    auto bad = Points(lo, hi, q, at);
    bad[4].x = NAN;
    Check(!BoxFromPoints(bad, box), "a non-finite corner is refused");
    auto skew = Points(lo, hi, q, at);
    skew[3] = Add(skew[1], Add(Sub(skew[3], skew[1]), Scale(Sub(skew[2], skew[1]), .5f)));
    Check(!BoxFromPoints(skew, box), "axes that are not square are refused");
    // Broadphase: every corner of both poses, plus the margin.
    Box a{}, b{};
    BoxFromPoints(Points(lo, hi, q, at), a);
    BoxFromPoints(Points(lo, hi, q, Add(at, {.2f, -.1f, .05f})), b);
    Vec3 min{}, max{};
    Bounds(a, b, .02f, min, max);
    bool inside = true;
    for (const auto* pose : {&a, &b}) {
        for (unsigned i = 0; i < 8; ++i) {
            Vec3 corner = pose->centre;
            for (int k = 0; k < 3; ++k) {
                corner = Add(corner, Scale(pose->axis[k], ((i >> k) & 1 ? 1.f : -1.f) * (&pose->half.x)[k]));
            }
            inside &= corner.x >= min.x + .0199f && corner.x <= max.x - .0199f && corner.y >= min.y + .0199f &&
                      corner.y <= max.y - .0199f && corner.z >= min.z + .0199f && corner.z <= max.z - .0199f;
        }
    }
    Check(inside, "both poses inside the bounds, with the margin");
    // Native records.
    int object = 0;
    scene::Hit h{};
    h.collider = &object;
    h.distance = .004f;
    h.point = {1, 2, 3};
    h.normal = {0, 0, 1};
    Check(Valid(h, &object), "a record about the queried object");
    Check(!Valid(h, &h), "a record about another object is refused");
    h.normal = {0, 0, 2};
    Check(!Valid(h, &object), "a non-unit normal is refused");
    h.normal = {0, 0, 1};
    h.distance = 1e10f;
    Check(!Valid(h, &object), "the engine's untouched distance means no contact");
    // Tracking: begin once, keep through short gaps, end after the grace frames.
    Tracker t;
    Tracker::Event e[8]{};
    int x = 0, y = 0;
    const void* both[] = {&x, &y, &x};
    Check(t.Update(both, 3, e, 8) == 2 && e[0].begin && e[1].begin && t.Tracked() == 2, "two objects begin, a repeat once");
    const void* one[] = {&x};
    Check(t.Update(one, 1, e, 8) == 0, "still touching: no event");
    Check(t.Update(one, 1, e, 8) == 0 && t.Update(one, 1, e, 8) == 1 && !e[0].begin && e[0].collider == &y && e[0].frames == 1,
          "the other ends after the grace frames, having lasted one frame");
    Check(t.Update(nullptr, 0, e, 8) == 0 && t.Update(one, 1, e, 8) == 0, "a one-frame gap does not end contact");
    Check(t.Reset(e, 8) == 1 && !e[0].begin && e[0].frames == 5 && t.Tracked() == 0, "reset ends what is left, with its frames");
    // --- pushing: velocity matching, the force limit, friction ---
    const float dt = 1.0f / 90.0f;
    PushSettings ps{};
    Body light{};
    light.inverseMass = 1;   // 1 kg
    light.inverseInertia = {10, 0, 0, 0, 10, 0, 0, 0, 10};
    const Vec3 top{0, 0, .05f}, up{0, 0, 1};
    auto push = Solve(light, top, up, 0, {0, 0, -1}, .7f, dt, ps);
    Check(push.apply && !push.limited && Near(push.v.z, -1, 1e-3f) && Length(push.w) < 1e-4f && Near(push.approach, 1),
          "a light can keeps up with the hand along the contact");
    const auto again = Solve(light, top, up, 0, {0, 0, -1}, .7f, dt, ps);
    Check(NearV(again.v, push.v) && NearV(again.w, push.w), "the same state gives the same push (set twice = set once)");
    Body moving = light;
    moving.v = push.v;
    moving.w = push.w;
    Check(!Solve(moving, top, up, 0, {0, 0, -1}, .7f, dt, ps).apply, "once it keeps up, nothing more");
    Check(!Solve(light, top, up, 0, {0, 0, 1}, .7f, dt, ps).apply, "a hand moving away never pulls");
    Body crate = light;
    crate.inverseMass = 1 / 100.0f;
    crate.inverseInertia = {.1f, 0, 0, 0, .1f, 0, 0, 0, .1f};
    push = Solve(crate, top, up, 0, {0, 0, -1}, .7f, dt, ps);
    Check(push.limited && Near(-push.v.z, ps.force * dt / 100, 1e-4f), "a 100 kg crate gains only force x dt / mass");
    Body anvil = light;
    anvil.inverseMass = 1e-4f;
    anvil.inverseInertia = {1e-4f, 0, 0, 0, 1e-4f, 0, 0, 0, 1e-4f};
    Check(Length(Solve(anvil, top, up, 0, {0, 0, -1}, .7f, dt, ps).v) < .005f, "10,000 kg does not budge");
    PushSettings strong = ps;
    strong.force = 1e6f;
    Check(Near(Solve(light, top, up, 0, {0, 0, -20}, .7f, dt, strong).v.z, -strong.maxSpeed, 1e-3f), "a swat counts at most 6 m/s");
    // Off-centre: pushed +x above the centre, it spins about +y as well.
    push = Solve(light, {-.03f, 0, .05f}, {-1, 0, 0}, 0, {1, 0, 0}, .7f, dt, ps);
    Check(push.apply && push.v.x > 0 && push.v.x < 1 && push.w.y > 0, "an off-centre push also turns it");
    // Friction: pressing down while moving sideways drags it; a fast slide drags less.
    push = Solve(light, top, up, 0, {.3f, 0, -1}, .7f, dt, ps);
    Check(push.v.x > .2f && !push.sliding, "pressing down drags it along: the surface sticks");
    push = Solve(light, top, up, 0, {2, 0, -.1f}, .7f, dt, ps);
    Check(push.sliding && push.v.x > 0 && push.v.x < .1f, "a light press and a fast slide: it slides, dynamic friction");
    Check(Solve(light, top, up, 0, {.3f, 0, -1}, -.7f, dt, ps).v.x == 0, "no grip at zero combined friction");
    Check(CombinedFriction(.5f, .7f) == .6f && CombinedFriction(-1, .2f) == 0, "the engine's rule: max(0, a + b) / 2");
    // Overlap: eased out past the slop, left alone within it.
    push = Solve(light, top, up, .02f, {}, .7f, dt, ps);
    Check(push.apply && Near(push.v.z, -(.02f - ps.slop) * ps.separation, 1e-4f), "a held weapon 2 cm in eases it out");
    Check(!Solve(light, top, up, .004f, {}, .7f, dt, ps).apply, "within the slop, a resting contact does nothing");
    Body fixed = light;
    fixed.inverseMass = 0;
    Check(!Solve(fixed, top, up, 0, {0, 0, -1}, .7f, dt, ps).apply, "an immovable body is never pushed");
    Check(!Solve(light, top, {0, 0, 0}, 0, {0, 0, -1}, .7f, dt, ps).apply && !Solve(light, top, up, 0, {NAN, 0, 0}, .7f, dt, ps).apply,
          "a bad normal or velocity is refused");
    // The weapon's speed at a point, from its last two poses.
    Box p0{}, p1{};
    BoxFromPoints(Points(lo, hi, {}, at), p0);
    BoxFromPoints(Points(lo, hi, {}, Add(at, {.01f, 0, 0})), p1);
    Check(NearV(PointVelocity(p1, p0, p1.centre, .01f), {1, 0, 0}, 5e-2f), "1 cm in 10 ms is 1 m/s");
    const float turn = .1f;
    // Turned 0.1 rad about x (the grip) in 10 ms: the tip, 0.34 m out, moves ~3.4 m/s across the box.
    BoxFromPoints(Points(lo, hi, Quaternion{std::sin(turn / 2), 0, 0, std::cos(turn / 2)}, at), p1);
    const Vec3 tip = Add(p1.centre, Scale(p1.axis[2], .22f));
    const Vec3 v = PointVelocity(p1, p0, tip, .01f);
    Check(Near(Length(v), 3.4f, .2f) && std::fabs(Dot(v, p1.axis[2])) < .1f * Length(v),
          "a turn moves the tip across the box, not along it");
    std::cout << "contact_query: box, bounds, records, tracking and pushing passed\n";
}
