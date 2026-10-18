#include "preyvr/ContactQuery.h"

#include <algorithm>
#include <cmath>

namespace preyvr::contact {

bool BoxFromPoints(const std::array<Vec3, 9>& p, Box& out)
{
    out = {};
    for (const auto& v : p) {
        if (!Finite(v)) { return false; }
    }
    // Corner i is p[i + 1]; corners 1, 2 and 4 differ from corner 0 on x, y, z.
    const Vec3 corner = p[1];
    const Vec3 edges[3] = {Sub(p[2], corner), Sub(p[3], corner), Sub(p[5], corner)};
    Box box{};
    for (int i = 0; i < 3; ++i) {
        const float length = Length(edges[i]);
        if (!(length >= .001f)) { return false; }
        box.axis[i] = Scale(edges[i], 1 / length);
        (&box.half.x)[i] = length * .5f;
    }
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(Dot(box.axis[i], box.axis[(i + 1) % 3])) > .01f) { return false; }
    }
    box.centre = p[0];
    out = box;
    return true;
}

BoxPrimitive ToPrimitive(const Box& box)
{
    BoxPrimitive p{};
    for (int r = 0; r < 3; ++r) {
        p.basis[r][0] = box.axis[r].x;
        p.basis[r][1] = box.axis[r].y;
        p.basis[r][2] = box.axis[r].z;
    }
    p.oriented = 1;
    p.centre = box.centre;
    p.half = box.half;
    return p;
}

void Bounds(const Box& a, const Box& b, float margin, Vec3& lo, Vec3& hi)
{
    lo = {INFINITY, INFINITY, INFINITY};
    hi = {-INFINITY, -INFINITY, -INFINITY};
    for (const Box* box : {&a, &b}) {
        for (int k = 0; k < 3; ++k) {
            // The box's reach along world axis k: |axis_i[k]| * half_i summed.
            float reach = 0;
            for (int i = 0; i < 3; ++i) { reach += std::fabs((&box->axis[i].x)[k]) * (&box->half.x)[i]; }
            const float c = (&box->centre.x)[k];
            (&lo.x)[k] = std::min((&lo.x)[k], c - reach - margin);
            (&hi.x)[k] = std::max((&hi.x)[k], c + reach + margin);
        }
    }
}

std::array<Vec3, 9> BoxPoints(const Box& box)
{
    std::array<Vec3, 9> p{};
    p[0] = box.centre;
    for (unsigned i = 0; i < 8; ++i) {
        Vec3 c = box.centre;
        for (int k = 0; k < 3; ++k) { c = Add(c, Scale(box.axis[k], ((i >> k) & 1 ? 1.0f : -1.0f) * (&box.half.x)[k])); }
        p[i + 1] = c;
    }
    return p;
}

Box HandBox(Vec3 grip, const std::array<Vec3, 3>& axes, bool left, const HandShape& s)
{
    Box b{};
    b.axis = axes;
    // Out of the palm is +x on the left hand, -x on the right.
    const float palm = left ? 1.0f : -1.0f;
    b.centre = Add(grip, Scale(axes[0], palm * (s.front - s.back) * .5f));
    b.half = {(s.front + s.back) * .5f, s.up, s.along};
    return b;
}

Vec3 PointVelocity(const Box& now, const Box& before, Vec3 point, float dt)
{
    if (!(dt > 0) || !std::isfinite(dt)) { return {}; }
    const Vec3 d = Sub(point, now.centre);
    Vec3 earlier = before.centre;
    for (int i = 0; i < 3; ++i) { earlier = Add(earlier, Scale(before.axis[i], Dot(d, now.axis[i]))); }
    return Scale(Sub(point, earlier), 1 / dt);
}

const char* KindName(Kind kind) { return kind == Kind::Sweep ? "sweep" : "overlap"; }

bool Valid(const scene::Hit& hit, const void* collider)
{
    const float n = Length(hit.normal);
    return collider && hit.collider == collider && std::isfinite(hit.distance) && hit.distance < 1e9f &&
           Finite(hit.point) && Finite(hit.normal) && std::fabs(n - 1) < .01f;
}

namespace {
Vec3 Multiply(const std::array<float, 9>& m, Vec3 v)
{
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z,
            m[6] * v.x + m[7] * v.y + m[8] * v.z};
}
// The inverse mass the body presents at arm `r` to an impulse along unit `d`.
float Effective(const Body& b, Vec3 r, Vec3 d)
{
    return b.inverseMass + Dot(d, Cross(Multiply(b.inverseInertia, Cross(r, d)), r));
}
} // namespace

float CombinedFriction(float a, float b) { return std::max(0.0f, a + b) * .5f; }

Push Solve(const Body& body, Vec3 point, Vec3 normal, float depth, Vec3 proxyVelocity, float surfaceFriction, float dt,
           const PushSettings& s)
{
    Push out{};
    if (!(body.inverseMass > 0) || !std::isfinite(body.inverseMass) || !Finite(body.v) || !Finite(body.w) ||
        !Finite(body.centre) || !Finite(point) || !Finite(normal) || !Finite(proxyVelocity) || !(dt > 0) ||
        !std::isfinite(dt) || !std::isfinite(depth) || !std::isfinite(surfaceFriction)) {
        return out;
    }
    for (float f : body.inverseInertia) {
        if (!std::isfinite(f)) { return out; }
    }
    const float length = Length(normal);
    if (!(length > .5f)) { return out; }
    const Vec3 n = Scale(normal, 1 / length);
    Vec3 hand = proxyVelocity;
    const float speed = Length(hand);
    if (speed > s.maxSpeed) { hand = Scale(hand, s.maxSpeed / speed); }
    const Vec3 r = Sub(point, body.centre);
    const Vec3 rel = Sub(hand, Add(body.v, Cross(body.w, r)));
    out.approach = -Dot(rel, n);
    const float ease = depth > s.slop ? std::min(s.maxSeparation, (depth - s.slop) * s.separation) : 0.0f;
    // Moving apart never pulls; a hand resting on an object, jittering by a few
    // mm/s, does not push it (or keep waking it).
    const float wanted = (out.approach > s.deadband ? out.approach : 0.0f) + ease;
    if (!(wanted > 0)) { return out; }
    const float kn = Effective(body, r, n);
    if (!(kn > 0)) { return out; }
    const float jn = wanted / kn;
    Vec3 impulse = Scale(n, -jn);
    const Vec3 sideways = Sub(rel, Scale(n, Dot(rel, n)));
    const float slip = Length(sideways);
    const float grip = CombinedFriction(surfaceFriction, s.proxyFriction);
    if (slip > 1e-4f && grip > 0) {
        const Vec3 t = Scale(sideways, 1 / slip);
        const float kt = Effective(body, r, t);
        if (kt > 0) {
            float jt = slip / kt;
            if (jt > grip * jn) {
                jt = grip * (2.0f / 3.0f) * jn;
                out.sliding = true;
            }
            impulse = Add(impulse, Scale(t, jt));
        }
    }
    const float limit = s.force * dt, size = Length(impulse);
    if (size > limit) {
        impulse = Scale(impulse, limit / size);
        out.limited = true;
    }
    out.impulse = impulse;
    out.v = Add(body.v, Scale(impulse, body.inverseMass));
    out.w = Add(body.w, Multiply(body.inverseInertia, Cross(r, impulse)));
    out.apply = Finite(out.v) && Finite(out.w);
    return out;
}

unsigned Tracker::Update(const void* const* colliders, unsigned count, Event* events, unsigned capacity)
{
    unsigned written = 0;
    auto emit = [&](const void* c, bool begin, unsigned frames) {
        if (written < capacity) { events[written++] = {c, begin, frames}; }
    };
    for (auto& e : entries_) {
        if (e.collider) { ++e.missed; }
    }
    for (unsigned i = 0; i < count; ++i) {
        const void* c = colliders[i];
        if (!c) { continue; }
        auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.collider == c; });
        if (found == entries_.end()) {
            found = std::find_if(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.collider; });
            if (found == entries_.end()) { continue; }
            *found = {c, 0, 0};
            emit(c, true, 0);
        }
        // A repeat within the frame counts once.
        if (found->missed != 0 || found->frames == 0) { ++found->frames; }
        found->missed = 0;
    }
    for (auto& e : entries_) {
        if (e.collider && e.missed >= kGrace) {
            emit(e.collider, false, e.frames);
            e = {};
        }
    }
    return written;
}

unsigned Tracker::Reset(Event* events, unsigned capacity)
{
    unsigned written = 0;
    for (auto& e : entries_) {
        if (e.collider && written < capacity) { events[written++] = {e.collider, false, e.frames}; }
        e = {};
    }
    return written;
}

unsigned Tracker::Tracked() const
{
    return static_cast<unsigned>(std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.collider; }));
}

} // namespace preyvr::contact
