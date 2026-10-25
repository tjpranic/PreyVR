#include "preyvr/BodyRig.h"

#include "preyvr/HandPose.h"

#include <algorithm>
#include <cmath>

namespace preyvr::bodyrig {

namespace {

constexpr Vec3 kUp{0.0f, 0.0f, 1.0f};
constexpr Vec3 kForward{0.0f, 1.0f, 0.0f};

// Cumulative shares of the spine's bend, pelvis to spine4: most of a lean is the
// hips' (the pelvis tips with the back, as in a squat or over a desk), the rest
// is spread up the spine. And of its turn, which the pelvis does not take (the
// legs stay on the feet).
constexpr float kBendShare[kSpine4 + 1] = {0.60f, 0.70f, 0.80f, 0.90f, 1.0f};
constexpr float kTurnShare[kSpine4 + 1] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
// The swing that aims the bent spine converges in a few rounds.
constexpr int kAimRounds = 4;
// Leaning further forward to unfold the legs: steps of the lean's sine, then
// halvings of the last one.
constexpr float kLeanStep = 0.02f;
constexpr int kLeanSteps = 50, kLeanHalvings = 5;
// The neck turns at most this far past the bend towards an atlas the spine
// could not bring it under.
constexpr float kNeckCraneRad = 30.0f * kRadiansPerDegree;
// A straight leg's knee bends forward: this much of forward joins the game's
// knee, fading out as the game's leg bends this far off straight.
constexpr float kKneeForward = 0.03f, kKneeBent = 0.02f;
// The head's pitch the neck follows: from looking up this far to straight down.
constexpr float kPitchUpRad = 45.0f * kRadiansPerDegree;
constexpr float kPitchDownRad = 90.0f * kRadiansPerDegree;

Vec3 Unit(Vec3 v, Vec3 fallback)
{
    const float l = Length(v);
    return l > 1e-6f && std::isfinite(l) ? Scale(v, 1.0f / l) : fallback;
}
Vec3 Level(Vec3 v) { return {v.x, v.y, 0.0f}; }
Vec3 Perp(Vec3 v, Vec3 axis) { return Sub(v, Scale(axis, Dot(v, axis))); }

bool FiniteJoint(const Joint& j)
{
    const float n = j.q.x * j.q.x + j.q.y * j.q.y + j.q.z * j.q.z + j.q.w * j.q.w;
    return Finite(j.t) && std::isfinite(n) && n > 0.5f && n < 1.5f;
}

// The shortest rotation from `from` to `to`, at most `limit` radians of it.
Quaternion TurnAtMost(const Vec3& from, const Vec3& to, float limit)
{
    const Quaternion full = armpose::FromTo(from, to);
    const float angle = 2.0f * std::acos(std::clamp(std::fabs(full.w), 0.0f, 1.0f));
    return angle > limit ? handpose::Slerp(Quaternion{}, full, limit / angle) : full;
}

// The lean as the sine of its angle from upright, level: within an ellipse of
// the forward (or back) and side limits.
Vec3 LimitLean(const Vec3& lean, const PinSettings& s)
{
    const float along = std::sin((lean.y >= 0.0f ? s.leanForwardDeg : s.leanBackDeg) * kRadiansPerDegree);
    const float side = std::sin(s.leanSideDeg * kRadiansPerDegree);
    if (!(along > 1e-3f) || !(side > 1e-3f)) { return {}; }
    const float e = (lean.x / side) * (lean.x / side) + (lean.y / along) * (lean.y / along);
    return e > 1.0f ? Scale(lean, 1.0f / std::sqrt(e)) : lean;
}

// The pelvis `length` below and behind `neck`, the spine leaning `lean`.
Vec3 PelvisUnder(const Vec3& neck, const Vec3& lean, float length)
{
    const float upright = std::sqrt(std::max(0.0f, 1.0f - Dot(lean, lean)));
    return Sub(neck, Scale(Add(lean, Scale(kUp, upright)), length));
}

// How far the atlas is followed level: all of it up to `full`, then less, until
// none of it from `none`.
float Followed(float away, float full, float none)
{
    if (away <= full) { return away; }
    if (away >= none || !(none > full)) { return 0.0f; }
    return full * (none - away) / (none - full);
}

struct Leg {
    Vec3 hip;       // from the pelvis
    Vec3 ankle;
    float reach;    // the longest the leg may be
};

} // namespace

bool Subtree(const int* parent, int count, int root, int exclude, bool* inSubtree)
{
    if (parent == nullptr || inSubtree == nullptr || count <= 0) { return false; }
    for (int j = 0; j < count; ++j) { inSubtree[j] = false; }
    if (root < 0 || root >= count || root == exclude) { return false; }
    for (int j = 0; j < count; ++j) {
        // Up the chain to the root or the excluded joint; the depth bound
        // refuses a cycle in a bad parent table.
        for (int at = j, depth = 0; at >= 0 && at < count && depth <= count; at = parent[at], ++depth) {
            if (at == exclude) { break; }
            if (at == root) {
                inSubtree[j] = true;
                break;
            }
        }
    }
    return true;
}

CapsuleSpan SpanOf(float heightCollider, float heightPivot, float radius, float halfHeight, bool capsule)
{
    const float centre = heightCollider - heightPivot;
    const float reach = halfHeight + (capsule ? radius : 0.0f);
    return {centre - reach, centre, centre + reach};
}

Joint Carry(const Joint& parentOld, const Joint& parentNew, const Joint& child)
{
    const Quaternion turn = Multiply(parentNew.q, Conjugate(parentOld.q));
    return {Normalize(Multiply(turn, child.q)), Add(parentNew.t, Rotate(turn, Sub(child.t, parentOld.t)))};
}

PinResult SolvePin(const PinInput& in, const PinSettings& s)
{
    PinResult out{};
    for (const Joint& j : in.native) {
        if (!FiniteJoint(j)) { return out; }
    }
    if (!Finite(in.atlas) || !Finite(in.throat) || !Finite(in.realAtlas) || !Finite(in.eye) ||
        !std::isfinite(in.pitch) || !std::isfinite(in.twist)) {
        return out;
    }
    const Joint* n = in.native;
    const Vec3 neck0 = n[kNeck].t, pelvis0 = n[kPelvis].t;
    const float length = Length(Sub(neck0, pelvis0));
    if (!(length > 1e-3f)) { return out; }
    const Vec3 facing = Unit(Level(in.facing), kForward);

    // The atlas to follow: all of its height, and level only within a lean.
    Vec3 atlas = in.realAtlas;
    {
        const Vec3 away = Level(Sub(in.realAtlas, in.atlas));
        const float d = Length(away);
        const float kept = Followed(d, s.followFull, s.followNone);
        atlas = Add(Sub(in.realAtlas, away), Scale(away, d > 1e-6f ? kept / d : 0.0f));
    }

    // The neck under it, bent forward with part of the head's pitch.
    const float pitch = std::clamp(in.pitch, -kPitchUpRad, kPitchDownRad);
    const float flex = pitch >= 0.0f ? s.neckFlex * pitch : s.neckExtend * pitch;
    const Quaternion bend = handpose::AxisAngle(Unit(Cross(kUp, facing), Vec3{-1.0f, 0.0f, 0.0f}), flex);
    const Vec3 toAtlas = Sub(in.atlas, neck0);
    Vec3 neckGoal = Sub(atlas, Rotate(bend, toAtlas));

    // The throat behind the eyes, or below them; else the neck goes back.
    {
        const Vec3 gap = Sub(in.eye, Add(neckGoal, Rotate(bend, Sub(in.throat, neck0))));
        const float down = s.throatDown > 1e-6f ? std::clamp(1.0f - gap.z / s.throatDown, 0.0f, 1.0f) : 0.0f;
        out.throatPush = std::max(0.0f, s.throatBack * down - Dot(gap, facing));
        neckGoal = Sub(neckGoal, Scale(facing, out.throatPush));
        atlas = Sub(atlas, Scale(facing, out.throatPush));
    }

    // The pelvis, the spine's length from the neck: back as the neck drops.
    out.drop = std::max(0.0f, neck0.z - neckGoal.z);
    const Vec3 wanted = Sub(pelvis0, Scale(kForward, s.pelvisBack * out.drop));
    const Vec3 leanWanted = Scale(Level(Sub(neckGoal, wanted)), 1.0f / length);
    const Vec3 lean = LimitLean(leanWanted, s);
    Leg legs[2];
    for (int side = 0; side < 2; ++side) {
        const int hip = side == 0 ? kUpperLegR : kUpperLegL;
        legs[side].hip = Sub(n[hip].t, pelvis0);
        legs[side].ankle = n[hip + 2].t;
        legs[side].reach = s.legReach * (Length(Sub(n[hip + 1].t, n[hip].t)) + Length(Sub(n[hip + 2].t, n[hip + 1].t)));
    }
    // How far the legs are outside their fold and reach, and whether one folds too far.
    const auto beyond = [&](const Vec3& pelvis) {
        float v = 0.0f;
        for (const Leg& leg : legs) {
            const float chord = Length(Sub(Add(pelvis, leg.hip), leg.ankle));
            v += std::max(0.0f, s.legFold - chord) + std::max(0.0f, chord - leg.reach);
        }
        return v;
    };
    const auto folds = [&](const Vec3& pelvis) {
        for (const Leg& leg : legs) {
            if (Length(Sub(Add(pelvis, leg.hip), leg.ankle)) < s.legFold) { return true; }
        }
        return false;
    };
    const auto leaning = [&](float t) {
        return PelvisUnder(neckGoal, LimitLean(Add(lean, Scale(kForward, t)), s), length);
    };
    Vec3 pelvis{}, aim = neckGoal;
    if (Length(Sub(lean, leanWanted)) < 1e-5f) {
        pelvis = PelvisUnder(neckGoal, lean, length);
        if (folds(pelvis)) {
            // Leaning further forward takes the hips back and up, unfolding the
            // knees: the least lean that fits both legs, else the best one.
            float best = beyond(pelvis), fits = -1.0f;
            Vec3 bestPelvis = pelvis;
            for (int i = 1; i <= kLeanSteps && fits < 0.0f; ++i) {
                const float t = static_cast<float>(i) * kLeanStep;
                const Vec3 at = leaning(t);
                const float v = beyond(at);
                if (v < best) { best = v; bestPelvis = at; }
                if (v <= 0.0f) { fits = t; }
            }
            if (fits > 0.0f) {
                float lo = fits - kLeanStep, hi = fits;
                for (int i = 0; i < kLeanHalvings; ++i) {
                    const float mid = 0.5f * (lo + hi);
                    if (beyond(leaning(mid)) > 0.0f) { lo = mid; } else { hi = mid; }
                }
                bestPelvis = leaning(hi);
                out.folded = true;
            }
            pelvis = bestPelvis;
        }
    } else {
        // Further than the spine leans: the pelvis stays, the neck goes as far as it can.
        const float upright = std::sqrt(std::max(0.0f, 1.0f - Dot(lean, lean)));
        pelvis = {wanted.x, wanted.y, neckGoal.z - length * upright};
        aim = Add(pelvis, Scale(Add(lean, Scale(kUp, upright)), length));
    }
    // Too high for a leg to reach its foot: the pelvis comes down, the neck short of its goal.
    bool held = false;
    for (const Leg& leg : legs) {
        const float level = Length(Level(Sub(Add(pelvis, leg.hip), leg.ankle)));
        if (level >= leg.reach) { continue; }
        const float top = leg.ankle.z + std::sqrt(leg.reach * leg.reach - level * level) - leg.hip.z;
        if (top < pelvis.z) { pelvis.z = top; held = true; }
    }
    // Lower than the legs fold even so: the feet sink.
    for (const Leg& leg : legs) {
        const Vec3 hip = Add(pelvis, leg.hip);
        const float level = Length(Level(Sub(hip, leg.ankle)));
        if (level >= s.legFold) { continue; }
        out.sink = std::max(out.sink, std::sqrt(s.legFold * s.legFold - level * level) - (hip.z - leg.ankle.z));
    }
    out.sink = std::clamp(out.sink, 0.0f, std::max(0.0f, s.sinkMax));

    // The spine from the pelvis to its aim: each joint takes its share of one
    // swing and of the chest's turn.
    Vec3 segment[kSpine4 + 1];
    Quaternion turn[kSpine4 + 1];
    for (int k = kPelvis; k <= kSpine4; ++k) {
        segment[k] = Sub(n[k + 1].t, n[k].t);
        turn[k] = handpose::AxisAngle(kUp, in.twist * kTurnShare[k]);
    }
    Quaternion delta[kSpine4 + 1];
    Vec3 at[kNeck + 1];
    const auto build = [&](const Quaternion& swing) {
        at[kPelvis] = pelvis;
        for (int k = kPelvis; k <= kSpine4; ++k) {
            delta[k] = Normalize(Multiply(handpose::Slerp(Quaternion{}, swing, kBendShare[k]), turn[k]));
            at[k + 1] = Add(at[k], Rotate(delta[k], segment[k]));
        }
    };
    Quaternion swing = armpose::FromTo(Sub(neck0, pelvis0), Sub(aim, pelvis));
    for (int i = 0; i < kAimRounds; ++i) {
        build(swing);
        swing = Normalize(Multiply(armpose::FromTo(Sub(at[kNeck], pelvis), Sub(aim, pelvis)), swing));
    }
    build(swing);
    // Bent, the spine is a little shorter than straight: the pelvis rises the
    // difference along it, unless a leg holds it down.
    if (!held) {
        const Vec3 along = Unit(Sub(aim, pelvis), kUp);
        pelvis = Add(pelvis, Scale(along, Length(Sub(aim, pelvis)) - Length(Sub(at[kNeck], pelvis))));
        build(swing);
    }
    out.shortfall = Length(Sub(neckGoal, at[kNeck]));
    {
        const Vec3 chord = Unit(Sub(at[kNeck], pelvis), kUp);
        out.leanDeg = std::acos(std::clamp(chord.z, -1.0f, 1.0f)) * kDegreesPerRadian;
    }
    for (int k = kPelvis; k <= kSpine4; ++k) { out.pose[k] = {Normalize(Multiply(delta[k], n[k].q)), at[k]}; }
    // The neck from where the spine left it towards the atlas: bent as planned,
    // craning a little further after an atlas the spine could not reach.
    {
        const Vec3 bent = Rotate(bend, toAtlas);
        const Quaternion crane = TurnAtMost(bent, Sub(atlas, at[kNeck]), kNeckCraneRad);
        out.pose[kNeck] = {Normalize(Multiply(Multiply(crane, bend), n[kNeck].q)), at[kNeck]};
    }

    // The legs, from the hip the pelvis carries to the planted foot, sunk if need be.
    for (int side = 0; side < 2; ++side) {
        const int hipIndex = side == 0 ? kUpperLegR : kUpperLegL;
        const Joint& hip0 = n[hipIndex];
        const Joint& knee0 = n[hipIndex + 1];
        const Joint& foot0 = n[hipIndex + 2];
        const Vec3 hip = Add(pelvis, Rotate(delta[kPelvis], Sub(hip0.t, pelvis0)));
        Vec3 ankle = Sub(foot0.t, Scale(kUp, out.sink));
        const float thigh = Length(Sub(knee0.t, hip0.t)), shin = Length(Sub(foot0.t, knee0.t));
        // Out of reach even straight: the foot comes up off the floor to it.
        const float reach = thigh + shin;
        if (Length(Sub(ankle, hip)) > reach) { ankle = Add(hip, Scale(Unit(Sub(ankle, hip), Scale(kUp, -1.0f)), reach)); }
        const Vec3 axis0 = Unit(Sub(foot0.t, hip0.t), Scale(kUp, -1.0f));
        const Vec3 bent = Perp(Sub(knee0.t, hip0.t), axis0);
        const float straight = std::clamp(1.0f - Length(bent) / kKneeBent, 0.0f, 1.0f);
        const Vec3 pole = Add(bent, Scale(kForward, kKneeForward * straight));
        const Vec3 knee = armpose::SolveElbow(hip, ankle, thigh, shin, pole, 1.0f).elbow;
        out.pose[hipIndex] = {Normalize(Multiply(armpose::FromTo(Sub(knee0.t, hip0.t), Sub(knee, hip)), hip0.q)), hip};
        out.pose[hipIndex + 1] = {Normalize(Multiply(armpose::FromTo(Sub(foot0.t, knee0.t), Sub(ankle, knee)), knee0.q)),
                                  knee};
        out.pose[hipIndex + 2] = {foot0.q, ankle};
    }
    for (const Joint& j : out.pose) {
        if (!FiniteJoint(j)) { return PinResult{}; }
    }
    out.valid = true;
    return out;
}

Vec3 StraightenShift(const Quaternion& spine1, const Vec3& spine2Offset, const Vec3& rest, float limit)
{
    const Vec3 off = Sub(spine2Offset, rest);
    const float l = Length(off);
    if (!std::isfinite(l) || !(limit >= 0.0f) || l <= limit) { return {}; }
    return Rotate(spine1, Scale(off, limit / l - 1.0f));
}

Vec3 FacingOf(const Vec3& forward, const Vec3& up)
{
    // Looking down, the head's up points the way the face is turned; looking
    // up, away from it. Weighted by the pitch, the sum stays level-forward.
    Vec3 h{forward.x - forward.z * up.x, forward.y - forward.z * up.y, 0.0f};
    if (Length(h) < 1e-5f) { h = Level(forward); }
    return Unit(h, kForward);
}

float PitchOf(const Vec3& forward)
{
    const float l = Length(forward);
    return l > 1e-6f && std::isfinite(l) ? std::asin(std::clamp(-forward.z / l, -1.0f, 1.0f)) : 0.0f;
}

float TurnAbout(const Vec3& from, const Vec3& to)
{
    const Vec3 a = Level(from), b = Level(to);
    if (!(Length(a) > 1e-6f) || !(Length(b) > 1e-6f)) { return 0.0f; }
    return std::atan2(a.x * b.y - a.y * b.x, a.x * b.x + a.y * b.y);
}

Vec3 ShoulderFromAtlas(const armpose::Body& body, armpose::Side side)
{
    // ShoulderTarget's offsets from the level eye, plus the eye from the atlas
    // (-kNeckFromEye), in the model's axes: OpenXR's x, -z and y.
    const Vec3& k = armpose::kNeckFromEye;
    return {static_cast<float>(side) * body.shoulderOut - k.x, body.shoulderForward + k.z, body.shoulderUp - k.y};
}

} // namespace preyvr::bodyrig
