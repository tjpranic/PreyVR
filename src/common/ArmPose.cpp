#include "preyvr/ArmPose.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace preyvr::armpose {
namespace {


Vec3 Unit(Vec3 a, Vec3 fallback)
{
    const float l = Length(a);
    return l > 1e-6f && std::isfinite(l) ? Scale(a, 1.0f / l) : fallback;
}
// The part of v perpendicular to the unit axis.
Vec3 Perp(Vec3 v, Vec3 axis) { return Sub(v, Scale(axis, Dot(v, axis))); }
float Wrap(float a)
{
    while (a > kPi) { a -= 2.0f * kPi; }
    while (a < -kPi) { a += 2.0f * kPi; }
    return a;
}
// Any unit vector perpendicular to the unit v.
Vec3 AnyPerp(Vec3 v)
{
    const Vec3 other = std::fabs(v.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    return Unit(Cross(v, other), Vec3{1, 0, 0});
}

} // namespace

float YawOf(const Vec3& forward)
{
    return std::atan2(-forward.x, -forward.z);
}

TorsoFrame TorsoFromYaw(float yaw)
{
    const float s = std::sin(yaw), c = std::cos(yaw);
    TorsoFrame t;
    t.forward = {-s, 0.0f, -c};
    t.right = {c, 0.0f, -s};
    t.up = {0.0f, 1.0f, 0.0f};
    return t;
}

float HeadYaw(const Quaternion& head)
{
    // Looking down, the head's up vector points the way the face is turned;
    // looking up, away from it. Weighted by the pitch, the sum stays
    // horizontal-forward through +-90 degrees.
    const Vec3 f = Rotate(head, Vec3{0, 0, -1});
    const Vec3 u = Rotate(head, Vec3{0, 1, 0});
    Vec3 h{f.x - f.y * u.x, 0.0f, f.z - f.y * u.z};
    if (Length(h) < 1e-5f) { h = {f.x, 0.0f, f.z}; }
    return YawOf(h);
}

float TorsoYaw::Update(float headYaw, float dtSeconds, float deadzone, float relax)
{
    if (!std::isfinite(headYaw)) { return yaw; }
    if (!valid || !std::isfinite(yaw)) {
        yaw = headYaw;
        valid = true;
        return yaw;
    }
    float diff = Wrap(headYaw - yaw);
    if (diff > deadzone) { yaw = Wrap(headYaw - deadzone); }
    else if (diff < -deadzone) { yaw = Wrap(headYaw + deadzone); }
    diff = Wrap(headYaw - yaw);
    const float dt = std::clamp(dtSeconds, 0.0f, 0.25f);
    yaw = Wrap(yaw + diff * std::min(1.0f, relax * dt));
    return yaw;
}

Vec3 ShoulderTarget(const Vec3& eye, const TorsoFrame& torso, Side side, const Body& body)
{
    return Add(Add(Add(eye, Scale(torso.forward, body.shoulderForward)),
                   Scale(torso.right, static_cast<float>(side) * body.shoulderOut)),
               Scale(torso.up, body.shoulderUp));
}

Vec3 LevelEye(const Pose& head, float torsoYaw)
{
    const Quaternion& q = head.orientation;
    const float lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.5f || !std::isfinite(torsoYaw)) { return head.position; }
    const Vec3 now = Rotate(Normalize(q), kNeckFromEye);
    const Quaternion torso{0.0f, std::sin(torsoYaw * 0.5f), 0.0f, std::cos(torsoYaw * 0.5f)};
    const Vec3 rest = Rotate(torso, kNeckFromEye);
    return {head.position.x + now.x - rest.x, head.position.y + now.y - rest.y, head.position.z + now.z - rest.z};
}

Vec3 ShoulderFromHead(const Pose& head, float torsoYaw, Side side, const Body& body)
{
    return ShoulderTarget(LevelEye(head, torsoYaw), TorsoFromYaw(torsoYaw), side, body);
}

float ArmLength(const Body& body) { return body.upper + body.fore; }

Body WithArmLength(Body body, float metres)
{
    const Body defaults{};
    if (!(metres > 0.0f) || !std::isfinite(metres)) { return body; }
    body.upper = metres * defaults.upper / ArmLength(defaults);
    body.fore = metres - body.upper;
    return body;
}

Vec3 PlaceShoulder(const Vec3& clavicleRoot, float clavicleLength, const Vec3& target, float stretch)
{
    const Vec3 d = Sub(target, clavicleRoot);
    const float l = Length(d);
    if (!(l > 1e-6f) || !(clavicleLength > 0.0f)) { return target; }
    const float s = std::clamp(stretch, 0.0f, 0.9f);
    const float length = std::clamp(l, clavicleLength * (1.0f - s), clavicleLength * (1.0f + s));
    return Add(clavicleRoot, Scale(d, length / l));
}

Vec3 BodyPole(const TorsoFrame& torso, Side side)
{
    return Unit(Add(Add(Scale(torso.forward, -0.2f), Scale(torso.right, 0.4f * static_cast<float>(side))),
                    Scale(torso.up, -0.8f)),
                Vec3{0, -1, 0});
}

float WristFlexionDeg(const Vec3& forearm, const Vec3& length, const Vec3& palm)
{
    // Flexed by a towards the palm, the forearm (in the hand's frame) is
    // length*cos(a) - palm*sin(a).
    return std::atan2(-Dot(forearm, palm), Dot(forearm, length)) * kDegreesPerRadian;
}

float WristRadialDeg(const Vec3& forearm, const Vec3& length, const Vec3& palm, Side side)
{
    // Deviated by d towards the thumb, the hand's length is forearm*cos(d) +
    // thumb*sin(d): the forearm leans away from the thumb.
    const Vec3 thumb = Scale(Unit(Cross(length, palm), Vec3{0, 0, 1}), side >= 0 ? 1.0f : -1.0f);
    return std::asin(std::clamp(-Dot(Unit(forearm, length), thumb), -1.0f, 1.0f)) * kDegreesPerRadian;
}

Vec3 ChooseElbowPole(const Vec3& shoulder, const Vec3& wrist, float upper, float fore, float maxStretch,
                     const Vec3& handLength, const Vec3& handPalm, Side side, const Vec3& bodyPole,
                     const WristLimits& limits, const Vec3* previousPole, const ArmObstacle* obstacle)
{
    if (obstacle != nullptr && (obstacle->torso == nullptr || obstacle->count <= 0)) { obstacle = nullptr; }
    const Vec3 axis = Unit(Sub(wrist, shoulder), Vec3{0, -1, 0});
    Vec3 body = Perp(bodyPole, axis);
    body = Length(body) > 1e-4f ? Unit(body, AnyPerp(axis)) : AnyPerp(axis);
    const Vec3 length = Unit(handLength, axis);
    Vec3 palm = Perp(handPalm, length);
    palm = Length(palm) > 1e-4f ? Unit(palm, AnyPerp(length)) : AnyPerp(length);
    // The circle: centre and radius from the same solve SolveElbow does.
    const ArmSolution reference = SolveElbow(shoulder, wrist, upper, fore, body, maxStretch);
    const Vec3 offset = Sub(reference.elbow, shoulder);
    const Vec3 centre = Add(shoulder, Scale(axis, Dot(offset, axis)));
    const float radius = Length(Perp(offset, axis));
    if (radius < 1e-3f) { return body; }
    const Vec3 u = body, v = Cross(axis, body);
    // The previous choice as an angle on this circle (none if unusable).
    bool hold = false;
    float held = 0.0f;
    if (previousPole != nullptr && limits.holdSigmaDeg > 0.0f) {
        const Vec3 pp = Perp(*previousPole, axis);
        if (Length(pp) > 0.2f) { hold = true; held = std::atan2(Dot(pp, v), Dot(pp, u)); }
    }
    // 2 (1 - cos a) ~ a^2: a Gaussian in the angle, without a seam at +-180.
    const auto swivel = [](float a, float sigmaDeg) {
        const float s = sigmaDeg / kDegreesPerRadian;
        return 2.0f * (1.0f - std::cos(a)) / (s * s);
    };
    const auto elbowAt = [&](float phi) {
        return Add(centre, Scale(Add(Scale(u, std::cos(phi)), Scale(v, std::sin(phi))), radius));
    };
    const auto cost = [&](float phi) {
        const Vec3 forearm = Unit(Sub(wrist, elbowAt(phi)), axis);
        const float flex = WristFlexionDeg(forearm, length, palm);
        const float rad = WristRadialDeg(forearm, length, palm, side);
        const float f = flex / limits.flexionSigmaDeg;
        const float d = (rad - limits.deviationRestDeg) / limits.deviationSigmaDeg;
        const float over = std::max({0.0f, flex - limits.flexionMaxDeg, -flex - limits.extensionMaxDeg,
                                     rad - limits.radialMaxDeg, -rad - limits.ulnarMaxDeg}) / limits.wallDeg;
        float c = f * f + d * d + over * over + swivel(phi, limits.swivelSigmaDeg);
        if (hold) { c += swivel(phi - held, limits.holdSigmaDeg); }
        return c;
    };
    // The torso's part of the cost: none clear of it by the margin.
    const auto torso = [&](float phi) {
        if (obstacle == nullptr) { return 0.0f; }
        const float in = (obstacle->margin - ArmClear(shoulder, elbowAt(phi), wrist, *obstacle)) / obstacle->wall;
        return in > 0.0f ? in * in : 0.0f;
    };
    constexpr int kSteps = 72;
    const auto angle = [](int i) { return 2.0f * kPi * static_cast<float>(((i % kSteps) + kSteps) % kSteps) / kSteps; };
    float base[kSteps];
    int best = 0;
    for (int i = 0; i < kSteps; ++i) {
        base[i] = cost(angle(i));
        if (base[i] < base[best]) { best = i; }
    }
    float bestCost = base[best];
    if (obstacle != nullptr) {
        // The torso's part is never negative, so in order of the rest of the
        // cost only an elbow below the best total so far can still win: near
        // nothing, one is measured against the torso.
        int order[kSteps];
        for (int i = 0; i < kSteps; ++i) { order[i] = i; }
        std::sort(order, order + kSteps, [&base](int a, int b) { return base[a] < base[b]; });
        bestCost = std::numeric_limits<float>::max();
        for (const int i : order) {
            if (base[i] >= bestCost) { break; }
            const float c = base[i] + torso(angle(i));
            if (c < bestCost) { bestCost = c; best = i; }
        }
    }
    // Parabolic refinement between the neighbours.
    const float h = 2.0f * kPi / kSteps;
    const float cm = base[(best + kSteps - 1) % kSteps] + torso(angle(best - 1));
    const float cp = base[(best + 1) % kSteps] + torso(angle(best + 1));
    float phi = angle(best);
    const float den = cm - 2.0f * bestCost + cp;
    if (den > 1e-9f) { phi += 0.5f * h * (cm - cp) / den; }
    return Unit(Add(Scale(u, std::cos(phi)), Scale(v, std::sin(phi))), body);
}

ArmSolution SolveElbow(const Vec3& shoulder, const Vec3& wrist, float upper, float fore,
                       const Vec3& pole, float maxStretch)
{
    ArmSolution out;
    const Vec3 along = Sub(wrist, shoulder);
    const float d = Length(along);
    if (!(d > 1e-5f) || !(upper > 0.0f) || !(fore > 0.0f)) {
        out.elbow = Add(shoulder, Scale(Unit(pole, Vec3{0, -1, 0}), upper));
        return out;
    }
    const Vec3 axis = Scale(along, 1.0f / d);
    const float reach = 0.995f * (upper + fore);
    if (d > reach) {
        // Lengthen both segments to meet the wrist; past maxStretch the caller
        // was supposed to have kept the wrist within reach -- stay connected
        // anyway, and say so.
        out.scale = d / reach;
        out.straight = out.scale > maxStretch;
    }
    const float l1 = upper * out.scale, l2 = fore * out.scale;
    const float ca = std::clamp((l1 * l1 + d * d - l2 * l2) / (2.0f * l1 * d), -1.0f, 1.0f);
    const float sa = std::sqrt(std::max(0.0f, 1.0f - ca * ca));
    Vec3 perp = Perp(pole, axis);
    perp = Length(perp) > 1e-5f ? Unit(perp, AnyPerp(axis)) : AnyPerp(axis);
    out.elbow = Add(shoulder, Add(Scale(axis, l1 * ca), Scale(perp, l1 * sa)));
    return out;
}

Quaternion FromTo(const Vec3& from, const Vec3& to)
{
    const Vec3 a = Unit(from, Vec3{1, 0, 0}), b = Unit(to, Vec3{1, 0, 0});
    const float d = Dot(a, b);
    if (d < -0.999999f) {
        const Vec3 axis = AnyPerp(a);
        return {axis.x, axis.y, axis.z, 0.0f};
    }
    const Vec3 c = Cross(a, b);
    return Normalize(Quaternion{c.x, c.y, c.z, 1.0f + d});
}

Vec3 SmoothDirection(const Vec3& previous, const Vec3& current, float dtSeconds, float tau)
{
    if (!(Length(previous) > 0.5f) || !(tau > 0.0f)) { return Unit(current, previous); }
    const float a = 1.0f - std::exp(-std::clamp(dtSeconds, 0.0f, 0.25f) / tau);
    return Unit(Add(previous, Scale(Sub(current, previous), a)), current);
}

float CapsuleDistance(const Capsule& capsule, const Vec3& p)
{
    if (!(capsule.radius > 0.0f)) { return std::numeric_limits<float>::max(); }
    const Vec3 ab = Sub(capsule.b, capsule.a);
    const float span = Dot(ab, ab);
    const float t = span > 1e-12f ? std::clamp(Dot(Sub(p, capsule.a), ab) / span, 0.0f, 1.0f) : 0.0f;
    return Length(Sub(p, Add(capsule.a, Scale(ab, t)))) - capsule.radius;
}

Capsule PlaceCapsule(const Vec3& joint, const Vec3& right, const Vec3& forward, const Vec3& up, const CapsuleSpec& spec)
{
    const Vec3 centre = Add(joint, Add(Scale(forward, spec.forward), Scale(up, spec.up)));
    const Vec3 half = Scale(right, spec.halfWidth);
    return {Sub(centre, half), Add(centre, half), spec.radius};
}

const char* TorsoPartName(int part)
{
    constexpr const char* kNames[kTorsoParts] = {"chest", "ribs", "waist", "hips"};
    return part >= 0 && part < kTorsoParts ? kNames[part] : "none";
}

const char* ArmSampleName(int sample)
{
    constexpr const char* kNames[kArmSamples] = {"upper30", "upper55", "upper80", "elbow",
                                                 "fore20", "fore45", "fore70", "wrist"};
    return sample >= 0 && sample < kArmSamples ? kNames[sample] : "none";
}

namespace {

// Where the arm is measured (ArmSample's order) and how thick it is there.
void ArmSamples(const Vec3& shoulder, const Vec3& elbow, const Vec3& wrist, const ArmThickness& thickness,
                Vec3 points[kArmSamples], float radii[kArmSamples])
{
    const auto along = [](Vec3 from, Vec3 to, float f) { return Add(from, Scale(Sub(to, from), f)); };
    const Vec3 p[kArmSamples] = {along(shoulder, elbow, 0.30f), along(shoulder, elbow, 0.55f),
                                 along(shoulder, elbow, 0.80f), elbow,
                                 along(elbow, wrist, 0.20f), along(elbow, wrist, 0.45f),
                                 along(elbow, wrist, 0.70f), wrist};
    const float r[kArmSamples] = {thickness.upper, thickness.upper, thickness.upper, thickness.elbow,
                                  thickness.fore, thickness.fore, thickness.fore, thickness.wrist};
    for (int i = 0; i < kArmSamples; ++i) {
        points[i] = p[i];
        radii[i] = r[i];
    }
}

} // namespace

ArmClearance MeasureArm(const Vec3& shoulder, const Vec3& elbow, const Vec3& wrist, const Capsule* torso, int count,
                        const ArmThickness& thickness)
{
    ArmClearance out;
    Vec3 points[kArmSamples];
    float radii[kArmSamples];
    ArmSamples(shoulder, elbow, wrist, thickness, points, radii);
    for (int i = 0; i < kArmSamples; ++i) {
        out.point[i] = points[i];
        out.radius[i] = radii[i];
        out.clear[i] = std::numeric_limits<float>::max();
        out.part[i] = -1;
        for (int k = 0; k < count && torso != nullptr; ++k) {
            if (!(torso[k].radius > 0.0f)) { continue; }
            const float d = CapsuleDistance(torso[k], points[i]) - radii[i];
            if (d < out.clear[i]) { out.clear[i] = d; out.part[i] = k; }
        }
        if (out.part[i] >= 0) { out.valid = true; }
        if (out.clear[i] < out.clear[out.deepest]) { out.deepest = i; }
    }
    return out;
}

float ArmClear(const Vec3& shoulder, const Vec3& elbow, const Vec3& wrist, const ArmObstacle& obstacle)
{
    Vec3 points[kArmSamples];
    float radii[kArmSamples];
    ArmSamples(shoulder, elbow, wrist, obstacle.thickness, points, radii);
    float least = std::numeric_limits<float>::max();
    for (int k = 0; k < obstacle.count && obstacle.torso != nullptr; ++k) {
        const Capsule& c = obstacle.torso[k];
        if (!(c.radius > 0.0f)) { continue; }
        for (int i = 0; i < kWristAt; ++i) { least = std::min(least, CapsuleDistance(c, points[i]) - radii[i]); }
    }
    return least;
}

TrackedPole TrackElbowPole(const Vec3& shoulder, const Vec3& wrist, float upper, float fore, float maxStretch,
                           const Vec3& wanted, const Vec3* drawn, float maxStep, const ArmObstacle& obstacle)
{
    TrackedPole out{wanted, Track::kFree};
    if (obstacle.torso == nullptr || obstacle.count <= 0) { return out; }
    const Vec3 axis = Unit(Sub(wrist, shoulder), Vec3{0, -1, 0});
    // Angles round the circle are from the drawn elbow, or the wanted one.
    Vec3 u = drawn != nullptr ? Perp(*drawn, axis) : Vec3{};
    const bool held = drawn != nullptr && Length(u) > 0.2f;
    if (!held) { u = Perp(wanted, axis); }
    if (!(Length(u) > 1e-4f)) { return out; }
    u = Unit(u, AnyPerp(axis));
    const Vec3 v = Cross(axis, u);
    const ArmSolution reference = SolveElbow(shoulder, wrist, upper, fore, u, maxStretch);
    const Vec3 offset = Sub(reference.elbow, shoulder);
    const Vec3 centre = Add(shoulder, Scale(axis, Dot(offset, axis)));
    const float radius = Length(Perp(offset, axis));
    if (radius < 1e-3f) { return out; }   // a straight arm has one elbow
    const auto pole = [&](float phi) { return Add(Scale(u, std::cos(phi)), Scale(v, std::sin(phi))); };
    const auto clearAt = [&](float phi) {
        return ArmClear(shoulder, Add(centre, Scale(pole(phi), radius)), wrist, obstacle);
    };
    // The circle, coarsely, measured as it is needed: sample 0 is the drawn
    // elbow (or the wanted one).
    constexpr int kSteps = 36;
    constexpr float kStep = 2.0f * kPi / kSteps;
    constexpr float kSlack = 0.005f;
    float table[kSteps];
    bool measured[kSteps]{};
    const auto sample = [&](int k) {
        k = ((k % kSteps) + kSteps) % kSteps;
        if (!measured[k]) {
            table[k] = clearAt(static_cast<float>(k) * kStep);
            measured[k] = true;
        }
        return table[k];
    };
    // Clear: by the margin, until it turns out nothing on the circle is.
    float need = obstacle.margin;
    const auto ok = [&](float phi) { return clearAt(phi) >= need; };
    const auto sampleOk = [&](float phi) { return sample(static_cast<int>(std::lround(phi / kStep))) >= need; };
    // The clear edge between a clear `lo` and a blocked `hi` (distances along
    // `dir` from `from`), by halving.
    const auto edge = [&](float from, int dir, float lo, float hi) {
        for (int i = 0; i < 6; ++i) {
            const float mid = 0.5f * (lo + hi);
            if (ok(from + static_cast<float>(dir) * mid)) { lo = mid; } else { hi = mid; }
        }
        return lo;
    };
    // How far from `from` along `dir` (+1 or -1) the elbow can go, up to
    // `length`, through clear elbows only: the coarse samples crossed, then the end.
    const auto reach = [&](float from, int dir, float length) {
        const float base = from / kStep;
        float s = (dir > 0 ? std::floor(base) + 1.0f - base : base - (std::ceil(base) - 1.0f)) * kStep;
        float clear = 0.0f;
        for (; s < length; s += kStep) {
            if (!sampleOk(from + static_cast<float>(dir) * s)) { return edge(from, dir, clear, s); }
            clear = s;
        }
        return ok(from + static_cast<float>(dir) * length) ? length : edge(from, dir, clear, length);
    };
    const Vec3 pw = Perp(wanted, axis);
    const float want = Length(pw) > 1e-4f ? std::atan2(Dot(pw, v), Dot(pw, u)) : 0.0f;
    const float at = 0.0f;   // the drawn elbow (without one, `want` is 0 too)
    const float d = Wrap(want - at);
    const int toward = d >= 0.0f ? 1 : -1;
    const float shortWay = std::fabs(d), longWay = 2.0f * kPi - shortWay;
    // Clear by the margin all the way there: the wanted elbow, the circle
    // measured only along the way.
    if (sample(0) >= need && reach(at, toward, shortWay) >= shortWay - 1e-4f) { return out; }
    // Something is in the way: all of the circle, and whether anything on it is clear.
    float best = -std::numeric_limits<float>::max();
    for (int k = 0; k < kSteps; ++k) { best = std::max(best, sample(k)); }
    need = std::min(obstacle.margin, best - kSlack);
    // The body moved onto the drawn elbow: out to the nearest clear one, either way.
    if (sample(0) < need) {
        out.how = Track::kPushed;
        for (int j = 1; j <= kSteps / 2; ++j) {
            for (int dir : {1, -1}) {
                const float s = static_cast<float>(j) * kStep;
                if (sampleOk(at + static_cast<float>(dir) * s)) {
                    // The clear side of the edge, from the blocked side.
                    float lo = s - kStep, hi = s;
                    for (int i = 0; i < 6; ++i) {
                        const float mid = 0.5f * (lo + hi);
                        if (ok(at + static_cast<float>(dir) * mid)) { hi = mid; } else { lo = mid; }
                    }
                    out.pole = pole(at + static_cast<float>(dir) * hi);
                    return out;
                }
            }
        }
        return out;   // nothing clear at all: `need` makes the clearest so, so not reached
    }
    // Towards the wanted elbow: straight there when nothing is in the way;
    // else round the other way when that is clear; else along the body to its
    // edge on this side.
    const float clearRun = reach(at, toward, shortWay);
    if (clearRun >= shortWay - 1e-4f) { return out; }
    float move = 0.0f;
    int dir = toward;
    if (reach(at, -toward, longWay) >= longWay - 1e-4f) {
        out.how = Track::kAround;
        dir = -toward;
        move = std::min(longWay, maxStep);
    } else {
        out.how = Track::kParked;
        move = std::min(clearRun, maxStep);
    }
    out.pole = pole(at + static_cast<float>(dir) * move);
    return out;
}

} // namespace preyvr::armpose
