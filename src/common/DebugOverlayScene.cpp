#include "preyvr/DebugOverlayScene.h"
#include "preyvr/TextFormat.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace preyvr::debugdraw {
namespace {

constexpr float kNoticeSeconds = 1.2f;


bool NoticeLive(const Slot& slot) { return slot.notice != Notice::None && slot.noticeAge >= 0 && slot.noticeAge < kNoticeSeconds; }

const char* NoticeText(Notice notice)
{
    switch (notice) {
    case Notice::Ready: return "READY";
    case Notice::Success: return "OK";
    case Notice::Empty: return "EMPTY";
    case Notice::Denied: return "DENIED";
    default: return "";
    }
}

// A laser line from the hand, easing in (kLaserFadeIn).
Line Laser(Vec3 from, Vec3 to, Color color, float width, float minPixels)
{
    Line line{from, to, color, width, minPixels};
    line.fadeIn = std::min(kLaserFadeIn, .5f * Length(Sub(to, from)));
    return line;
}

// The left hand's use pointer: faint while it selects nothing, bright and
// ending on the object when the game selected one, green while the use button
// is held. A pick the native cone made off the line shows as a bend.
void AddUseRay(Scene& scene, const HandRay& ray, unsigned mask)
{
    const Color color = ray.suppressed ? kMuted : ray.held ? kUseHeld : ray.selected ? kLeft : kUseIdle;
    if (ray.selected && !ray.onLine) {
        // The straight pointing line, faint, then the bend to the object.
        const float d = Length(Sub(ray.target, ray.origin));
        scene.lines.push_back(Laser(ray.origin, Add(ray.origin, Scale(ray.direction, d)), WithAlpha(color, 70), .003f, 1.5f));
        Line bend{Add(ray.origin, Scale(ray.direction, d * .55f)), ray.target, color, .004f, 2.0f};
        scene.lines.push_back(Laser(ray.origin, bend.a, color, .0045f, 2.0f));
        scene.lines.push_back(bend);
    } else {
        scene.lines.push_back(Laser(ray.origin, ray.target, color, ray.selected ? .0045f : .003f, ray.selected ? 2.0f : 1.4f));
    }
    if (ray.selected) { scene.lines.push_back(Laser(ray.origin, ray.target, WithAlpha(color, 50), .012f, 4.0f)); }
    Marker end{};
    end.centre = ray.target;
    end.color = color;
    end.filled = ray.selected || ray.hit;
    end.radius = ray.selected ? .02f : .014f;
    end.minPixels = ray.selected ? 6.0f : 4.0f;
    scene.markers.push_back(end);
    if (ray.selected) {
        Marker halo = end;
        halo.filled = false;
        halo.radius = .045f;
        halo.minPixels = 11.0f;
        halo.color = WithAlpha(color, 160);
        scene.markers.push_back(halo);
    }
    if (mask & kLabels) {
        for (unsigned i = 0; i < ray.candidateCount && i < ray.candidates.size(); ++i) {
            Marker c{};
            c.centre = ray.candidates[i];
            c.color = WithAlpha(kLeft, 150);
            c.radius = .012f;
            c.minPixels = 4.0f;
            scene.markers.push_back(c);
        }
        if (!ray.label.empty()) {
            Label label{};
            label.anchor = ray.target;
            label.color = color;
            label.offsetX = .9f;
            label.offsetY = -.5f;
            label.text = ray.label;
            label.minPixels = 18.0f;
            label.maxPixels = 30.0f;
            scene.labels.push_back(label);
        }
    }
}

void AddRay(Scene& scene, const HandRay& ray, Color color, unsigned mask, bool rightHand)
{
    if (!ray.valid || !Finite(ray.origin) || !Finite(ray.target)) { return; }
    if (ray.role == RayRole::Use) { AddUseRay(scene, ray, mask); return; }
    // The right hand without a weapon still has the game's aim ray, but no shot
    // follows it: drawn faint so it is not mistaken for a weapon's.
    if (rightHand && ray.role != RayRole::Weapon) { color = WithAlpha(color, 120); }
    // The path a shot (or a power) takes: from the hand to where the game's ray ends.
    scene.lines.push_back(Laser(ray.origin, ray.target, color, .0045f, 2.0f));
    // A soft halo makes a thin ray readable against bright surfaces.
    scene.lines.push_back(Laser(ray.origin, ray.target, WithAlpha(color, 50), .012f, 4.0f));
    Marker end{};
    end.centre = ray.target;
    end.color = color;
    end.filled = ray.hit;
    end.radius = ray.hit ? .018f : .03f;
    end.minPixels = ray.hit ? 5.0f : 7.0f;
    scene.markers.push_back(end);
    if (ray.hit) {
        Marker halo = end;
        halo.filled = false;
        halo.radius = .04f;
        halo.minPixels = 10.0f;
        halo.color = WithAlpha(color, 150);
        scene.markers.push_back(halo);
    }
    if (mask & kLabels) {
        Label label{};
        label.anchor = ray.target;
        label.color = color;
        label.offsetX = .9f;
        label.offsetY = -.5f;
        const char* role = ray.role == RayRole::Psi ? "PSI " : !rightHand ? "L "
                         : ray.role == RayRole::Weapon ? (ray.fromMuzzle ? "MUZZLE " : "") : "NO WEAPON ";
        label.text = std::string(role) + (ray.hit ? Fixed(ray.distance, 2) + " m" : "no hit " + Fixed(ray.distance, 0) + " m");
        label.minPixels = 18.0f;
        label.maxPixels = 30.0f;
        scene.labels.push_back(label);
    }
}

void AddSlot(Scene& scene, const Slot& slot, unsigned mask)
{
    if (!slot.valid || !Finite(slot.centre)) { return; }
    const Color color = SlotColor(slot);
    const float distance = SlotHandDistance(slot);
    const bool inside = distance <= slot.radius;
    Sphere sphere{};
    sphere.centre = slot.centre;
    sphere.radius = slot.radius;
    sphere.outline = color;
    sphere.fill = WithAlpha(slot.stored ? kStored : color, slot.owned || NoticeLive(slot) ? 70 : inside ? 55 : 24);
    scene.spheres.push_back(sphere);
    if (slot.stored) {
        Marker stored{};
        stored.centre = slot.centre;
        stored.radius = .025f;
        stored.color = kStored;
        scene.markers.push_back(stored);
    }
    // The hand that slot measures, and a tether once it is within reach.
    if (Finite(slot.hand)) {
        Marker hand{};
        hand.centre = slot.hand;
        hand.radius = .012f;
        hand.color = inside ? color : kIdle;
        hand.minPixels = 4.0f;
        scene.markers.push_back(hand);
        if (distance < slot.radius * 3) {
            Line tether{slot.hand, slot.centre, WithAlpha(color, 200), .002f, 1.5f};
            tether.dash = inside ? 0.0f : .015f;
            scene.lines.push_back(tether);
        }
    }
    if (mask & kLabels) {
        Label label{};
        label.anchor = Add(slot.centre, Vec3{0, slot.radius + .02f, 0});
        label.color = color;
        label.centred = true;
        label.offsetY = -2.3f;
        // What the slot will do next, in the words of its own gesture.
        const bool medkit = slot.name == "MEDKIT";
        std::string state;
        if (NoticeLive(slot)) { state = NoticeText(slot.notice); }
        else if (slot.owned) { state = medkit ? (slot.triggerArmed ? "TRIGGER: USE" : "RELEASE TRIGGER") : "HELD"; }
        else if (inside) {
            state = slot.gripPressed && !slot.armed ? "RELEASE GRIP"
                    : medkit ? "SQUEEZE: SELECT" : slot.stored ? "SQUEEZE: DRAW/STOW" : "SQUEEZE: STORE";
        }
        else { state = medkit ? "" : slot.stored ? "STORED" : "EMPTY"; }
        label.text = slot.name + (state.empty() ? "" : "  " + state) + "\n" + Fixed(distance * 100, 0) + " cm";
        label.minPixels = 18.0f;
        label.maxPixels = 32.0f;
        scene.labels.push_back(label);
    }
}

void AddWrist(Scene& scene, const Wrist& wrist, unsigned mask)
{
    if (!wrist.enabled || !Finite(wrist.card.position)) { return; }
    const Color color = wrist.visible ? kActive : wrist.gate ? kWarn : kMuted;
    const Quaternion q = Normalize(wrist.card.orientation);
    const Vec3 x = Rotate(q, {wrist.width * .5f, 0, 0});
    const Vec3 y = Rotate(q, {0, wrist.height * .5f, 0});
    const Vec3 c = wrist.card.position;
    const Vec3 corners[4] = {Add(c, Add(x, y)), Add(c, Sub(y, x)), Sub(c, Add(x, y)), Add(c, Sub(x, y))};
    for (int i = 0; i < 4; ++i) {
        Line edge{corners[i], corners[(i + 1) % 4], color, .002f, 1.8f};
        edge.dash = wrist.gate ? 0.0f : .01f;
        scene.lines.push_back(edge);
    }
    // The card's visible face, the axis the facing test measures.
    scene.lines.push_back({c, Add(c, Rotate(q, {0, 0, .06f})), color, .002f, 1.8f});
    if (mask & kLabels) {
        Label label{};
        label.anchor = Add(c, Scale(y, 1.3f));
        label.color = color;
        label.centred = true;
        label.offsetY = -2.3f;
        label.height = .012f;
        label.minPixels = 16.0f;
        label.maxPixels = 28.0f;
        label.text = std::string("WRIST ") + (wrist.visible ? "VISIBLE" : wrist.gate ? "HIDDEN" : "GATED") +
                     "\nface " + Fixed(wrist.facing, 2) + "/" + Fixed(wrist.facingNeeded, 2) +
                     " view " + Fixed(wrist.viewing, 2) + "/" + Fixed(wrist.viewingNeeded, 2);
        scene.labels.push_back(label);
    }
}

void AddForegrip(Scene& scene, const Foregrip& grip, unsigned mask)
{
    if (!grip.valid || !Finite(grip.start) || !Finite(grip.end)) { return; }
    const Color color = grip.held ? kActive : grip.inRegion ? kNear : WithAlpha(kLeft, 170);
    const Vec3 axis = Sub(grip.end, grip.start);
    const float length = Length(axis);
    const Vec3 unit = length > 1e-5f ? Scale(axis, 1 / length) : Vec3{0, 0, -1};
    const Vec3 u = Perpendicular(unit), v = Cross(unit, u);
    AppendCircle(scene, grip.start, u, v, grip.radius, color, .0015f, 28);
    AppendCircle(scene, grip.end, u, v, grip.radius, color, .0015f, 28);
    for (int i = 0; i < 4; ++i) {
        const float angle = 1.5707963f * static_cast<float>(i);
        const Vec3 offset = Add(Scale(u, grip.radius * std::cos(angle)), Scale(v, grip.radius * std::sin(angle)));
        scene.lines.push_back({Add(grip.start, offset), Add(grip.end, offset), color, .0015f, 1.2f});
    }
    if (grip.held && Finite(grip.socket)) {
        Marker socket{};
        socket.centre = grip.socket;
        socket.radius = .015f;
        socket.color = kActive;
        scene.markers.push_back(socket);
    }
    if (mask & kLabels) {
        Label label{};
        label.anchor = grip.end;
        label.color = color;
        label.offsetX = .8f;
        label.height = .014f;
        label.minPixels = 16.0f;
        label.maxPixels = 28.0f;
        label.text = std::string("FOREGRIP ") + (grip.held ? "HELD" : grip.inRegion ? "SQUEEZE" : "READY");
        scene.labels.push_back(label);
    }
}

// The sweep's box in amber when a swing can query (grey while a gate refuses),
// the last second's swept paths fading in yellow, and the last contact: green
// when it struck, red when it was refused.
void AddPhysical(Scene& scene, const Physical& p, unsigned mask)
{
    for (const auto& sweep : p.sweeps) {
        if (!(sweep.age >= 0) || sweep.age > kSweepSeconds) { continue; }
        const auto alpha = static_cast<std::uint8_t>(kSweep.a * (1 - .7f * sweep.age / kSweepSeconds));
        for (unsigned i = 0; i < sweep.from.size(); ++i) {
            if (Finite(sweep.from[i]) && Finite(sweep.to[i])) {
                scene.lines.push_back({sweep.from[i], sweep.to[i], WithAlpha(kSweep, alpha), .0015f, 1.2f});
            }
        }
    }
    const bool box = p.valid && std::all_of(p.points.begin(), p.points.end(), [](Vec3 v) { return Finite(v); });
    if (box) {
        const Color color = p.open ? kRight : kMuted;
        // Twelve edges: corners whose indices differ in one bit.
        for (unsigned corner = 0; corner < 8; ++corner) {
            for (unsigned bit = 1; bit < 8; bit <<= 1) {
                if (!(corner & bit)) {
                    scene.lines.push_back({p.points[corner + 1], p.points[(corner | bit) + 1], color, .002f, 1.5f});
                }
            }
        }
        Marker centre{};
        centre.centre = p.points[0];
        centre.radius = .008f;
        centre.color = color;
        centre.minPixels = 4.0f;
        scene.markers.push_back(centre);
        if (mask & kLabels) {
            Label label{};
            label.anchor = p.points[0];
            label.color = color;
            label.offsetX = 1.2f;
            label.height = .012f;
            label.minPixels = 16.0f;
            label.maxPixels = 26.0f;
            label.text = "SWEEP " + (p.open ? std::string("OPEN") : p.gate);
            scene.labels.push_back(label);
        }
    }
    // The hands' boxes, cyan: what each hand pushes with.
    for (const auto& hand : p.handBoxes) {
        if (!std::all_of(hand.begin(), hand.end(), [](Vec3 v) { return Finite(v); })) { continue; }
        for (unsigned corner = 0; corner < 8; ++corner) {
            for (unsigned bit = 1; bit < 8; bit <<= 1) {
                if (!(corner & bit)) { scene.lines.push_back({hand[corner + 1], hand[(corner | bit) + 1], kContact, .0015f, 1.2f}); }
            }
        }
    }
    // The probe under the player's feet: orange over what they stand on (never
    // pushed), grey over nothing.
    if (p.foot && std::all_of(p.footBox.begin(), p.footBox.end(), [](Vec3 v) { return Finite(v); })) {
        const Color color = p.standing ? kWarn : kMuted;
        for (unsigned corner = 0; corner < 8; ++corner) {
            for (unsigned bit = 1; bit < 8; bit <<= 1) {
                if (!(corner & bit)) { scene.lines.push_back({p.footBox[corner + 1], p.footBox[(corner | bit) + 1], color, .0015f, 1.2f}); }
            }
        }
    }
    // The contact lane's contacts: overlap filled cyan, sweep a yellow ring, a
    // body violet; each with its normal. The first few are labelled.
    unsigned labelled = 0;
    for (const auto& c : p.contacts) {
        if (!Finite(c.point)) { continue; }
        const Color color = c.articulated ? kPsi : c.sweep ? kNear : kContact;
        Marker mark{};
        mark.centre = c.point;
        mark.radius = .012f;
        mark.color = color;
        mark.filled = !c.sweep;
        mark.minPixels = 6.0f;
        scene.markers.push_back(mark);
        if (Finite(c.normal)) { scene.lines.push_back({c.point, Add(c.point, Scale(c.normal, .06f)), color, .0015f, 1.2f}); }
        // A push: green, 10 cm per m/s of velocity change.
        if (c.pushed && Finite(c.push)) { scene.lines.push_back({c.point, Add(c.point, Scale(c.push, .1f)), kActive, .003f, 2.0f}); }
        if ((mask & kLabels) && labelled < 4) {
            ++labelled;
            Label label{};
            label.anchor = c.point;
            label.color = color;
            label.offsetX = .8f;
            label.offsetY = -.6f;
            label.height = .011f;
            label.minPixels = 15.0f;
            label.maxPixels = 24.0f;
            label.text = std::string(c.sweep ? "SWEEP " : "OVERLAP ") + Fixed(c.dist * 1000, 0) + " mm";
            scene.labels.push_back(label);
        }
    }
    // Surface stop, orange: a held-back hand's real grip as a ring, where it is
    // drawn as a dot, joined; each surface met with its normal.
    for (const auto& h : p.surfaceHands) {
        if (!h.held || !Finite(h.real) || !Finite(h.drawn)) { continue; }
        Marker real{};
        real.centre = h.real;
        real.radius = .015f;
        real.color = kWarn;
        real.filled = false;
        real.minPixels = 6.0f;
        scene.markers.push_back(real);
        Marker drawn = real;
        drawn.centre = h.drawn;
        drawn.radius = .01f;
        drawn.filled = true;
        scene.markers.push_back(drawn);
        scene.lines.push_back({h.real, h.drawn, kWarn, .002f, 1.5f});
    }
    for (const auto& m : p.surfaceMarks) {
        if (!Finite(m.point)) { continue; }
        Marker mark{};
        mark.centre = m.point;
        mark.radius = .006f;
        mark.color = kWarn;
        mark.minPixels = 4.0f;
        scene.markers.push_back(mark);
        if (Finite(m.normal)) { scene.lines.push_back({m.point, Add(m.point, Scale(m.normal, .05f)), kWarn, .0015f, 1.2f}); }
    }
    if (p.contact && p.contactAge >= 0 && p.contactAge <= kContactSeconds && Finite(p.point)) {
        const Color color = p.acted ? kActive : kDenied;
        Marker hit{};
        hit.centre = p.point;
        hit.radius = .02f;
        hit.color = color;
        hit.minPixels = 7.0f;
        scene.markers.push_back(hit);
        if (Finite(p.normal)) { scene.lines.push_back({p.point, Add(p.point, Scale(p.normal, .1f)), color, .002f, 1.5f}); }
        if (mask & kLabels) {
            Label label{};
            label.anchor = p.point;
            label.color = color;
            label.offsetX = .9f;
            label.offsetY = .9f;
            label.height = .012f;
            label.minPixels = 16.0f;
            label.maxPixels = 26.0f;
            label.text = "CONTACT " + p.result;
            scene.labels.push_back(label);
        }
    }
}

// A capsule's wireframe: a ring round its axis at each end, four lines along
// it, and each end's half circles in two planes through the axis.
void AddCapsule(Scene& scene, const armpose::Capsule& c, Color color)
{
    if (!(c.radius > 0) || !Finite(c.a) || !Finite(c.b)) { return; }
    const Vec3 ab = Sub(c.b, c.a);
    const float length = Length(ab);
    const Vec3 u = length > 1e-5f ? Scale(ab, 1 / length) : Vec3{1, 0, 0};
    // Across the axis: up where it can be (a capsule laid across the body), else back.
    Vec3 v = Sub(Vec3{0, 1, 0}, Scale(u, u.y));
    if (Length(v) < .1f) { v = Sub(Vec3{0, 0, 1}, Scale(u, u.z)); }
    v = Scale(v, 1 / Length(v));
    const Vec3 w = Cross(u, v);
    const float r = c.radius;
    // On the circle of radius r round `centre` in the plane of x and y.
    const auto at = [r](Vec3 centre, Vec3 x, Vec3 y, float angle) {
        return Add(centre, Add(Scale(x, r * std::cos(angle)), Scale(y, r * std::sin(angle))));
    };
    const auto line = [&](Vec3 p, Vec3 q) { scene.lines.push_back({p, q, color, .0015f, 1.2f}); };
    constexpr int kRing = 24, kCap = 12;
    for (const Vec3& end : {c.a, c.b}) {
        for (int i = 0; i < kRing; ++i) { line(at(end, v, w, 2 * kPi * i / kRing), at(end, v, w, 2 * kPi * (i + 1) / kRing)); }
    }
    for (const Vec3& d : {v, w, Scale(v, -1), Scale(w, -1)}) { line(Add(c.a, Scale(d, r)), Add(c.b, Scale(d, r))); }
    for (const Vec3& d : {v, w}) {
        for (int i = 0; i < kCap; ++i) {
            const float a0 = kPi * i / kCap, a1 = kPi * (i + 1) / kCap;
            line(at(c.b, d, u, a0), at(c.b, d, u, a1));
            line(at(c.a, d, Scale(u, -1), a0), at(c.a, d, Scale(u, -1), a1));
        }
    }
}

// The arms against the drawn torso: the capsules, and each arm as a thin line
// with a ring at each sample as thick as the arm is there, coloured by its
// clearance; the sample nearest the torso also a dot, labelled.
void AddArmFit(Scene& scene, const ArmFit& fit, unsigned mask)
{
    if (!fit.valid) { return; }
    for (const auto& capsule : fit.torso) { AddCapsule(scene, capsule, kCapsule); }
    const char* names[2] = {"R", "L"};
    for (int s = 0; s < 2; ++s) {
        const ArmFitArm& arm = fit.arms[s];
        const auto& c = arm.clearance;
        if (!arm.valid || !c.valid || !Finite(arm.shoulder) || !Finite(arm.elbow) || !Finite(arm.wrist)) { continue; }
        scene.lines.push_back({arm.shoulder, arm.elbow, WithAlpha(kIdle, 150), .0015f, 1.2f});
        scene.lines.push_back({arm.elbow, arm.wrist, WithAlpha(kIdle, 150), .0015f, 1.2f});
        for (int i = 0; i < armpose::kArmSamples; ++i) {
            Marker ring{};
            ring.centre = c.point[i];
            ring.radius = c.radius[i];
            ring.color = ClearanceColor(c.clear[i]);
            ring.filled = false;
            ring.minPixels = 4.0f;
            ring.ringPixels = 2.0f;
            scene.markers.push_back(ring);
        }
        Marker dot{};
        dot.centre = c.point[c.deepest];
        dot.radius = .008f;
        dot.color = ClearanceColor(c.Worst());
        dot.minPixels = 5.0f;
        scene.markers.push_back(dot);
        if (mask & kLabels) {
            Label label{};
            label.anchor = c.point[c.deepest];
            label.color = dot.color;
            label.offsetX = .9f;
            label.offsetY = -.5f;
            label.height = .011f;
            label.minPixels = 15.0f;
            label.maxPixels = 24.0f;
            label.text = std::string(names[s]) + " " + armpose::ArmSampleName(c.deepest) + " " + Fixed(c.Worst() * 100, 1) +
                         " cm " + armpose::TorsoPartName(c.part[c.deepest]);
            scene.labels.push_back(label);
        }
    }
}

}  // namespace

Color ClearanceColor(float clear)
{
    return clear < 0 ? kDenied : clear < kArmNear ? kWarn : kActive;
}

float SlotHandDistance(const Slot& slot)
{
    const float d = Length(Sub(slot.hand, slot.centre));
    return std::isfinite(d) ? d : 1e9f;
}

Color SlotColor(const Slot& slot)
{
    if (NoticeLive(slot)) {
        switch (slot.notice) {
        case Notice::Success: case Notice::Ready: return kActive;
        case Notice::Empty: return kWarn;
        case Notice::Denied: return kDenied;
        default: break;
        }
    }
    if (slot.owned) { return kActive; }
    if (SlotHandDistance(slot) <= slot.radius) {
        // Inside with grip already held: the gesture refuses on purpose (no
        // activation by drifting in), which is worth showing, not hiding.
        return slot.gripPressed && !slot.armed ? kWarn : kNear;
    }
    return slot.stored ? kStored : kIdle;
}

void AddShot(Scene& scene, const ShotMark& shot, unsigned mask)
{
    if (!shot.valid || !(shot.age >= 0) || shot.age > kShotSeconds || !Finite(shot.spawn) || !Finite(shot.target)) {
        return;
    }
    // Full strength for the first second, then fading out.
    const float fade = shot.age < 1 ? 1.0f : std::max(0.0f, 1 - (shot.age - 1) / (kShotSeconds - 1));
    const auto alpha = static_cast<std::uint8_t>(kShot.a * fade);
    if (alpha < 8) { return; }
    scene.lines.push_back({shot.spawn, shot.target, WithAlpha(kShot, alpha), .0025f, 1.6f});
    Marker spawn{};
    spawn.centre = shot.spawn;
    spawn.color = WithAlpha(kShot, alpha);
    spawn.radius = .012f;
    spawn.minPixels = 6.0f;
    scene.markers.push_back(spawn);
    Marker end{};
    end.centre = shot.target;
    end.color = WithAlpha(kShot, alpha);
    end.filled = true;
    end.radius = .025f;
    end.minPixels = 7.0f;
    scene.markers.push_back(end);
    if (mask & kLabels) {
        Label label{};
        label.anchor = shot.target;
        label.color = WithAlpha(kShot, alpha);
        label.offsetX = .9f;
        label.offsetY = .9f;
        label.text = "SHOT " + Fixed(shot.angleDegrees, 2) + " deg";
        label.minPixels = 16.0f;
        label.maxPixels = 26.0f;
        scene.labels.push_back(label);
    }
}

void BuildOverlayScene(const OverlayFrame& frame, unsigned mask, Scene& scene)
{
    scene.Clear();
    if (mask & kRays) {
        AddShot(scene, frame.shot, mask);
        AddRay(scene, frame.rays[1], kRight, mask, true);
        AddRay(scene, frame.rays[0], frame.rays[0].role == RayRole::Psi ? kPsi : kLeft, mask, false);
    }
    if (mask & kRaw) {
        for (int h = 0; h < 2; ++h) {
            const auto& c = frame.hands[h];
            if (!c.valid) { continue; }
            AppendAxes(scene, c.grip, .06f, .002f);
            // The left ray already is this aim ray; drawing it twice only hides it.
            if (h == 0 && (mask & kRays) && frame.rays[0].valid) { continue; }
            const Vec3 forward = Rotate(Normalize(c.aim.orientation), {0, 0, -1});
            Line aim{c.aim.position, Add(c.aim.position, Scale(forward, 2.0f)), WithAlpha(kIdle, 150), .0015f, 1.2f};
            aim.dash = .02f;
            scene.lines.push_back(aim);
        }
    }
    if (mask & kBody) {
        if (frame.holstersEnabled) {
            for (const auto& slot : frame.holsters) { AddSlot(scene, slot, mask); }
        }
        if (frame.medkitEnabled) { AddSlot(scene, frame.medkit, mask); }
    }
    if (mask & kWrist) { AddWrist(scene, frame.wrist, mask); }
    if (mask & kForegrip) { AddForegrip(scene, frame.foregrip, mask); }
    if (mask & kPhysical) { AddPhysical(scene, frame.physical, mask); }
    if (mask & kArms) { AddArmFit(scene, frame.armFit, mask); }
    if ((mask & kStatus) && frame.headValid) {
        Label status{};
        // About 25 degrees below the gaze: out of the aiming area, a glance down to read.
        status.anchor = Compose(frame.head, Pose{{}, {0, -.50f, -1.05f}}).position;
        status.text = OverlayStatusText(frame, mask);
        status.color = Color{235, 235, 225, 235};
        status.centred = true;
        status.height = .021f;
        status.minPixels = 17.0f;
        status.maxPixels = 30.0f;
        // First, so it holds its place and the other labels step around it.
        scene.labels.insert(scene.labels.begin(), status);
    }
}

std::string OverlayStatusText(const OverlayFrame& f, unsigned mask)
{
    auto ray = [](const HandRay& r) -> std::string {
        if (!r.valid) { return "-"; }
        return (r.hit ? "hit " + Fixed(r.distance, 2) + " m" : "no hit (" + Fixed(r.distance, 0) + " m)");
    };
    auto slot = [](const Slot& s) -> std::string {
        if (!s.valid) { return s.name + " -"; }
        const float d = SlotHandDistance(s);
        std::string state = s.owned ? "held" : d <= s.radius ? "IN" : "out";
        if (s.stored) { state += "+stored"; }
        if (NoticeLive(s)) { state += std::string(" ") + NoticeText(s.notice); }
        return s.name + " " + state + " " + Fixed(d * 100, 0) + "cm";
    };
    char head[64];
    std::snprintf(head, sizeof(head), "PREYVR DEBUG  dbg.draw %u", mask);
    std::string text = head;
    text += "\nR  " + ray(f.rays[1]) + (f.rays[1].valid && f.rays[1].role != RayRole::Weapon ? " (no weapon)" : "");
    text += "   L " + std::string(f.rays[0].role == RayRole::Psi ? "psi " : f.rays[0].role == RayRole::Use ? "use " : "") +
            (f.rays[0].role == RayRole::Use && f.rays[0].selected ? "TARGET " + Fixed(f.rays[0].distance, 2) + " m"
                                                                  : ray(f.rays[0]));
    const char* psi[] = {"native", "head", "left"};
    text += "   psi " + std::string(psi[std::min(f.psiMode, 2u)]);
    if (f.sceneQueryFault) { text += "  QUERY FAULT"; }
    text += "\nHolsters " + std::string(f.holstersEnabled ? "on  " + slot(f.holsters[0]) + "  " + slot(f.holsters[1]) : "off");
    text += "\nMedkit " + std::string(f.medkitEnabled ? "on  " + slot(f.medkit) +
                                                            (f.medkit.owned ? (f.medkit.triggerArmed ? " trig:armed" : " trig:release") : "")
                                                      : "off");
    text += "\nWrist " + std::string(!f.wrist.enabled ? "off" : f.wrist.visible ? "VISIBLE" : f.wrist.gate ? "hidden" : "gated") +
            (f.wrist.enabled ? "  face " + Fixed(f.wrist.facing, 2) + " view " + Fixed(f.wrist.viewing, 2) +
                                   " d " + Fixed(f.wrist.distance, 2)
                             : "");
    text += "   Foregrip " + std::string(!f.foregrip.valid ? "-" : f.foregrip.held ? "HELD" : f.foregrip.inRegion ? "in" : "ready");
    if (f.shot.valid && f.shot.age >= 0 && f.shot.age <= kShotSeconds) {
        text += "\nShot " + f.shot.route + " " + Fixed(f.shot.angleDegrees, 2) + " deg  spawn " + f.shot.spawnKind +
                " " + Fixed(f.shot.spawnOffAimMm, 0) + " mm off aim" + (f.shot.projectileSeen ? "" : " (query only)");
    }
    if ((mask & kPhysical) && !f.physical.status.empty()) { text += "\nPhysical " + f.physical.status; }
    if ((mask & kPhysical) && !f.physical.contactStatus.empty()) { text += "\nContact " + f.physical.contactStatus; }
    if ((mask & kPhysical) && !f.physical.surfaceStatus.empty()) { text += "\nSurface " + f.physical.surfaceStatus; }
    if (mask & kArms) {
        text += "\nArms vs torso";
        if (!f.armFit.valid) { text += " -"; }
        const char* names[2] = {"R", "L"};
        for (int s = 0; s < 2 && f.armFit.valid; ++s) {
            const auto& c = f.armFit.arms[s].clearance;
            text += std::string("  ") + names[s] + " ";
            text += f.armFit.arms[s].valid && c.valid
                ? std::string(armpose::ArmSampleName(c.deepest)) + " " + Fixed(c.Worst() * 100, 1) + " cm " +
                      armpose::TorsoPartName(c.part[c.deepest])
                : "-";
        }
    }
    if (!f.extra.empty()) { text += "\n" + f.extra; }
    return text;
}

}  // namespace preyvr::debugdraw
