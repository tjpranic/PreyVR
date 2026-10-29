#include "AnimIkTakeover.h"
#include "AnimIkState.h"
#include "DebugOverlay.h"

// The VR arm (arm.*): the torso the shoulders face, the player's shoulders
// placed before the native pass, and the arm rewritten from those shoulders
// after it; and the drawn arm against the drawn torso, measured.

namespace preyvr::dll {
using namespace ik;

namespace {

// The spine joint each torso capsule rides (armpose::TorsoPart's order). On the
// player's rig their local x runs up the spine, y forward and z to the left
// (the ik.skel dumps standing, armed and crouched, PreyVR-84).
constexpr int kTorsoJoint[armpose::kTorsoParts] = {bodyrig::kSpine4, bodyrig::kSpine3, bodyrig::kSpine2,
                                                   bodyrig::kSpine1};

// How often the arms against the torso are logged while the overlay draws them.
constexpr std::uint64_t kArmFitLogNs = 1000000000ull;

// A child of `parent` re-placed under the parent's new pose; its offset along
// the bone (local X) scaled with the segment.
void CarryChild(std::uint8_t* absolute, unsigned count, int child, const QuatT& parentOld,
                const Quaternion& parentNew, const Vec3& parentNewT, float alongScale)
{
    QuatT c{};
    if (!ReadQuatT(absolute, count, child, c)) { return; }
    const Quaternion po{parentOld.q[0], parentOld.q[1], parentOld.q[2], parentOld.q[3]};
    Vec3 local = Rotate(Conjugate(po), Vec3{c.t[0] - parentOld.t[0], c.t[1] - parentOld.t[1], c.t[2] - parentOld.t[2]});
    local.x *= alongScale;
    const Quaternion localQ = Multiply(Conjugate(po), Quaternion{c.q[0], c.q[1], c.q[2], c.q[3]});
    WriteQuatT(absolute, count, child, Multiply(parentNew, localQ), Add(parentNewT, Rotate(parentNew, local)));
}

} // namespace

namespace ik {

// The torso the shoulders face this frame (radians, tracking space), under
// gIkMutex: the shared one (TorsoLane), so the shoulders, the body and the body
// slots agree, else this lane's own filter with that lane's settings; turned
// right while two hands hold a long weapon.
float ShoulderYaw(const GameplayPoseFrame& frame)
{
    const float headYaw = armpose::HeadYaw(frame.tracking.head.orientation);
    std::lock_guard lock(gArmMutex);
    const std::uint64_t now = preyvr::timing::MonotonicNanoseconds();
    const float dt = gTorsoNs != 0 ? static_cast<float>(now - gTorsoNs) * 1e-9f : 0.0f;
    gTorsoNs = now;
    float yaw = 0.0f;
    if (BodyTorsoYaw(yaw)) { gTorsoYaw.Reset(); }
    else {
        float deadzone = 0.0f, relax = 0.0f;
        BodyTorsoSettings(deadzone, relax);
        yaw = gTorsoYaw.Update(headYaw, dt, deadzone, relax);
    }
    // Two hands on a long weapon: the shooter's bladed stance, left shoulder
    // forward (the torso turns right), so the drawn support arm reaches the
    // foregrip. Eased by the hold's own blend.
    const std::uint64_t longOwner = gLongGeneration.load(std::memory_order_relaxed);
    const float twist = longOwner != 0 && frame.weaponGeneration == longOwner && frame.twoHand.blend > 0.0f
        ? gLongTwistDeg.load(std::memory_order_relaxed) * 0.01745329f * frame.twoHand.blend : 0.0f;
    const float shoulderYaw = yaw - twist;
    gArmStats.torsoYawDeg = shoulderYaw * kDegreesPerRadian;
    gArmStats.headYawDeg = headYaw * kDegreesPerRadian;
    return shoulderYaw;
}

// Before the native pass, under gIkMutex, after the body is planned: each
// shoulder. On the posed body the collarbone and the shoulder ride its chest,
// the shoulder where armpose::ShoulderFromHead would put it with the body as
// the game left it; without the pin, from the player's neck.
void PrepareArms(const GameplayPoseFrame& frame, const animik::Location& location, std::uint8_t* absolute,
                 unsigned count, const PoseSettings& pose, float shoulderYaw, const BodyPlan& posed, ArmPlan plan[2])
{
    if (pose.mode != 1 || pose.armMode == 0 || gHandRig.skeleton != gRigSkeleton.load()) { return; }
    XrMap map{&frame, {}, location};
    if (!AnchorPoint(frame, kIkAnchor, map.anchor) || !(location.s > 1e-4f)) { return; }
    const Pose& head = frame.tracking.head;
    const armpose::TorsoFrame torso = armpose::TorsoFromYaw(shoulderYaw);
    const armpose::Body& body = pose.body;
    for (int s = 0; s < 2; ++s) {
        QuatT clavicle{}, upper{};
        if (!ReadQuatT(absolute, count, gHandRig.clavicle[s], clavicle) ||
            !ReadQuatT(absolute, count, gHandRig.upperArm[s], upper)) { continue; }
        ArmPlan& p = plan[s];
        p.torso = torso;
        const armpose::Side side = s == 0 ? 1 : -1;
        Vec3 clavicleModel{clavicle.t[0], clavicle.t[1], clavicle.t[2]}, upperModel{upper.t[0], upper.t[1], upper.t[2]};
        if (posed.apply) {
            clavicleModel = posed.Chest(clavicleModel);
            upperModel = posed.Chest(upperModel);
            // The shoulder laid out from the chest as the game turned it, then
            // carried with the chest's pose: as the chest turns, so do they.
            const Vec3 offset = Rotate(handpose::AxisAngle(Vec3{0.0f, 0.0f, 1.0f}, posed.chestTurn),
                                       Scale(bodyrig::ShoulderFromAtlas(body, side), 1.0f / location.s));
            p.targetXr = map.ToXr(posed.Chest(Add(posed.atlas, offset)));
        } else {
            p.targetXr = armpose::ShoulderFromHead(head, shoulderYaw, side, body);
        }
        p.clavicleXr = map.ToXr(clavicleModel);
        const Vec3 animated = map.ToXr(upperModel);
        p.shoulderXr = armpose::PlaceShoulder(p.clavicleXr, Dist3(animated, p.clavicleXr), p.targetXr,
                                              body.clavicleStretch);
        p.shoulderModel = map.ToModel(p.shoulderXr);
        p.reachModel = 0.995f * (body.upper + body.fore) * body.armStretch / location.s;
        p.valid = std::isfinite(p.shoulderModel.x) && std::isfinite(p.shoulderModel.y) &&
                  std::isfinite(p.shoulderModel.z);
    }
}

void SolveArms(const GameplayPoseFrame& frame, std::uint8_t* absolute, unsigned count,
               const animik::Location& location, const HandRig& rig, const ArmPlan plan[2],
               const PoseSettings& pose)
{
    XrMap map{&frame, {}, location};
    if (!AnchorPoint(frame, kIkAnchor, map.anchor)) { return; }
    const armpose::Body& body = pose.body;
    ArmStats stats{};
    // The torso as it is drawn: the body pin has written the spine by now.
    armpose::Capsule torso[armpose::kTorsoParts]{};
    stats.torsoPlaced = PlaceTorso(map, absolute, count, rig, pose.torsoShape, torso);
    const std::uint64_t now = preyvr::timing::MonotonicNanoseconds();
    for (int s = 0; s < 2; ++s) {
        const ArmPlan& p = plan[s];
        if (!p.valid || !p.goalSet) { continue; }
        QuatT clav{}, upper{}, lower{}, hand{};
        if (!ReadQuatT(absolute, count, rig.clavicle[s], clav) || !ReadQuatT(absolute, count, rig.upperArm[s], upper) ||
            !ReadQuatT(absolute, count, rig.lowerArm[s], lower) || !ReadQuatT(absolute, count, rig.wrist[s], hand)) {
            continue;
        }
        const Vec3 c0{clav.t[0], clav.t[1], clav.t[2]};
        const Vec3 s0{upper.t[0], upper.t[1], upper.t[2]};
        const Vec3 e0{lower.t[0], lower.t[1], lower.t[2]};
        const Vec3 w0{hand.t[0], hand.t[1], hand.t[2]};
        const Quaternion handQ{hand.q[0], hand.q[1], hand.q[2], hand.q[3]};
        // The solve, in OpenXR metres.
        const Vec3 shoulder = p.shoulderXr;
        const Vec3 wrist = map.ToXr(p.goal);
        const armpose::Side side = s == 0 ? 1 : -1;
        const Vec3 bodyPole = armpose::BodyPole(p.torso, side);
        // The elbow the PLAYER's hand allows: the real hand's frame from the
        // controller's grip pose (OpenXR defines it), not the drawn hand's --
        // a held weapon turns the drawn hand to its barrel, the real one stays.
        const auto& state = frame.tracking.hands[s == 0 ? static_cast<unsigned>(Hand::right)
                                                        : static_cast<unsigned>(Hand::left)];
        const handpose::Frame real = handpose::GripHandFrame(s == 1 ? handpose::Side::left : handpose::Side::right,
                                                             pose.tubeLean);
        const Vec3 handLength = Rotate(state.gripPose.orientation, real.length);
        const Vec3 handPalm = Rotate(state.gripPose.orientation, real.palm);
        // The elbow kept out of the drawn torso: the chooser prefers elbows
        // outside it, and the drawn one goes round it, never through it.
        const armpose::ArmObstacle obstacle{torso, stats.torsoPlaced ? armpose::kTorsoParts : 0, pose.thickness,
                                            pose.avoidMargin, pose.avoidWall};
        const bool avoid = stats.torsoPlaced;
        Vec3 pole{};
        armpose::Track how = armpose::Track::kFree;
        {
            std::lock_guard lock(gArmMutex);
            const float dt = gPoleNs[s] != 0 ? static_cast<float>(now - gPoleNs[s]) * 1e-9f : 0.0f;
            const bool fresh = gPoleNs[s] != 0 && dt < 0.5f;
            gPoleNs[s] = now;
            pole = armpose::ChooseElbowPole(shoulder, wrist, body.upper, body.fore, body.armStretch, handLength,
                                            handPalm, side, bodyPole, pose.wrist, fresh ? &gPole[s] : nullptr,
                                            avoid ? &obstacle : nullptr);
            pole = fresh ? armpose::SmoothDirection(gPole[s], pole, dt, pose.poleTau) : pole;
            gPole[s] = pole;
            if (avoid) {
                const armpose::TrackedPole tracked =
                    armpose::TrackElbowPole(shoulder, wrist, body.upper, body.fore, body.armStretch, pole,
                                            fresh ? &gDrawnPole[s] : nullptr, pose.avoidRate * dt, obstacle);
                pole = tracked.pole;
                how = tracked.how;
            }
            gDrawnPole[s] = pole;
        }
        const armpose::ArmSolution arm = armpose::SolveElbow(shoulder, wrist, body.upper, body.fore, pole,
                                                             body.armStretch);
        // Back to the model, and the joints.
        const Vec3 sm = p.shoulderModel, em = map.ToModel(arm.elbow), wm = p.goal;
        if (!std::isfinite(em.x) || !std::isfinite(em.y) || !std::isfinite(em.z)) { continue; }
        const auto unit = [](Vec3 v) {
            const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            return l > 1e-6f ? Scale(v, 1.0f / l) : Vec3{1, 0, 0};
        };
        const auto len = [](Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
        // Clavicle: turned about its root towards the new shoulder.
        const Quaternion clavQ{clav.q[0], clav.q[1], clav.q[2], clav.q[3]};
        const Quaternion clavNew = Multiply(armpose::FromTo(unit(Sub(s0, c0)), unit(Sub(sm, c0))), clavQ);
        WriteQuatT(absolute, count, rig.clavicle[s], clavNew, c0);
        // Upper arm: at the shoulder, along shoulder -> elbow.
        const Quaternion upperQ{upper.q[0], upper.q[1], upper.q[2], upper.q[3]};
        const Quaternion upperNew = Multiply(armpose::FromTo(unit(Sub(e0, s0)), unit(Sub(em, sm))), upperQ);
        const float upperScale = len(Sub(e0, s0)) > 1e-4f ? len(Sub(em, sm)) / len(Sub(e0, s0)) : 1.0f;
        for (int k = 0; k < 3; ++k) {
            CarryChild(absolute, count, rig.upperChild[s][k], upper, upperNew, sm, upperScale);
        }
        WriteQuatT(absolute, count, rig.upperArm[s], upperNew, sm);
        // Forearm: at the elbow, along elbow -> wrist.
        const Quaternion lowerQ{lower.q[0], lower.q[1], lower.q[2], lower.q[3]};
        const Quaternion lowerNew = Multiply(armpose::FromTo(unit(Sub(w0, e0)), unit(Sub(wm, em))), lowerQ);
        const float lowerScale = len(Sub(w0, e0)) > 1e-4f ? len(Sub(wm, em)) / len(Sub(w0, e0)) : 1.0f;
        CarryChild(absolute, count, rig.lowerTwist[s], lower, lowerNew, em, lowerScale);
        WriteQuatT(absolute, count, rig.lowerArm[s], lowerNew, em);
        // The hand on its goal, its rotation as the pass left it, and what it
        // carries (fingers, the weapon's prop joint) moved with it.
        const Vec3 move = Sub(wm, w0);
        if (len(move) > 1e-6f) {
            const auto shift = [&](int j) {
                QuatT a{};
                if (ReadQuatT(absolute, count, j, a)) {
                    WriteQuatT(absolute, count, j, Quaternion{a.q[0], a.q[1], a.q[2], a.q[3]},
                               Add(Vec3{a.t[0], a.t[1], a.t[2]}, move));
                }
            };
            shift(rig.wrist[s]);
            shift(rig.handProp[s]);
            if (rig.valid[s]) {
                for (int j = 0; j < handpose::kJointCount; ++j) { shift(rig.joint[s][j]); }
            }
        }
        if (p.forceRotation && p.rotationSet) {
            // Turn the hand and all it carries about the wrist onto the drive's rotation.
            const Quaternion turn = Normalize(Multiply(p.rotation, Conjugate(handQ)));
            const auto spin = [&](int j) {
                QuatT a{};
                if (ReadQuatT(absolute, count, j, a)) {
                    const Vec3 t = Add(wm, Rotate(turn, Sub(Vec3{a.t[0], a.t[1], a.t[2]}, wm)));
                    WriteQuatT(absolute, count, j, Multiply(turn, Quaternion{a.q[0], a.q[1], a.q[2], a.q[3]}), t);
                }
            };
            spin(rig.wrist[s]);
            spin(rig.handProp[s]);
            if (rig.valid[s]) {
                for (int j = 0; j < handpose::kJointCount; ++j) { spin(rig.joint[s][j]); }
            }
        }
        stats.solved[s] = true;
        stats.target[s] = p.targetXr;
        stats.shoulder[s] = shoulder;
        stats.elbow[s] = arm.elbow;
        stats.handMoveCm[s] = len(move) * location.s * 100.0f;
        stats.scale[s] = arm.scale;
        stats.straight[s] = arm.straight;
        stats.reachCm[s] = p.reach >= 0.0f ? p.reach * 100.0f : 0.0f;
        if (stats.torsoPlaced) {
            stats.clearance[s] = armpose::MeasureArm(shoulder, arm.elbow, wrist, torso, armpose::kTorsoParts,
                                                     pose.thickness);
        }
        stats.track[s] = avoid ? static_cast<int>(how) : -1;
    }
    const bool drawn = DebugOverlayWantsArmFit();
    std::string fitLine;
    {
        std::lock_guard lock(gArmMutex);
        for (int s = 0; s < 2; ++s) {
            if (stats.solved[s] && std::isfinite(stats.reachCm[s])) {
                gReachMaxCm[s] = std::max(gReachMaxCm[s], stats.reachCm[s]);
            }
            const armpose::ArmClearance& c = stats.clearance[s];
            if (!stats.solved[s] || !c.valid) { continue; }
            ArmFitTally& t = gArmFitTally;
            if (stats.track[s] >= 0 && stats.track[s] < 4) { ++t.track[s][stats.track[s]]; }
            ++t.frames[s];
            if (c.Worst() < 0.0f) { ++t.inside[s]; }
            if (c.Worst() < t.worst[s]) {
                t.worst[s] = c.Worst();
                t.worstSample[s] = c.deepest;
                t.worstPart[s] = c.part[c.deepest];
            }
        }
        stats.torsoYawDeg = gArmStats.torsoYawDeg;
        stats.headYawDeg = gArmStats.headYawDeg;
        gArmStats = stats;
        // While the overlay draws them, the arms against the torso once a second:
        // a pose held in the headset, read back from the log.
        if (drawn && stats.torsoPlaced && (gArmFitLogNs == 0 || now - gArmFitLogNs >= kArmFitLogNs)) {
            gArmFitLogNs = now;
            fitLine = "arm_fit" + ArmFitText(stats.solved[0] ? stats.clearance[0] : armpose::ArmClearance{}, "R") +
                      ArmFitText(stats.solved[1] ? stats.clearance[1] : armpose::ArmClearance{}, "L");
        }
    }
    if (!fitLine.empty()) { Log(fitLine); }
    if (drawn) {
        debugdraw::ArmFit fit{};
        fit.valid = stats.torsoPlaced;
        for (int k = 0; k < armpose::kTorsoParts; ++k) { fit.torso[static_cast<std::size_t>(k)] = torso[k]; }
        for (int s = 0; s < 2; ++s) {
            debugdraw::ArmFitArm& arm = fit.arms[static_cast<std::size_t>(s)];
            arm.valid = stats.solved[s] && stats.clearance[s].valid;
            arm.shoulder = stats.shoulder[s];
            arm.elbow = stats.elbow[s];
            arm.wrist = stats.clearance[s].point[armpose::kWristAt];
            arm.clearance = stats.clearance[s];
        }
        DebugOverlayArmFit(fit, frame.referenceGeneration);
    }
}

bool PlaceTorso(const XrMap& map, std::uint8_t* absolute, unsigned count, const HandRig& rig,
                const armpose::TorsoShape& shape, armpose::Capsule torso[armpose::kTorsoParts])
{
    if (!rig.bodyReady) { return false; }
    bool placed = false;
    for (int k = 0; k < armpose::kTorsoParts; ++k) {
        torso[k] = {};
        QuatT j{};
        if (!(shape.part[k].radius > 0.0f) || !ReadQuatT(absolute, count, rig.pinned[kTorsoJoint[k]], j)) { continue; }
        const Quaternion q{j.q[0], j.q[1], j.q[2], j.q[3]};
        const Vec3 t{j.t[0], j.t[1], j.t[2]};
        const Vec3 at = map.ToXr(t);
        // A joint axis in OpenXR: the model's unit, mapped, made unit again.
        const auto axis = [&](Vec3 local) {
            const Vec3 d = Sub(map.ToXr(Add(t, Rotate(q, local))), at);
            const float l = Length(d);
            return l > 1e-6f ? Scale(d, 1.0f / l) : d;
        };
        const armpose::Capsule c =
            armpose::PlaceCapsule(at, axis(Vec3{0, 0, -1}), axis(Vec3{0, 1, 0}), axis(Vec3{1, 0, 0}), shape.part[k]);
        if (Finite(c.a) && Finite(c.b)) {
            torso[k] = c;
            placed = true;
        }
    }
    return placed;
}

} // namespace ik

} // namespace preyvr::dll
