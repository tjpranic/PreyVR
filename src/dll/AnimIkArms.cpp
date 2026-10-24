#include "AnimIkTakeover.h"
#include "AnimIkState.h"

// The VR arm (arm.*): the player's shoulders placed before the native pass, the
// torso's lift, and the arm rewritten from those shoulders after it.

namespace preyvr::dll {
using namespace ik;

namespace {

// The torso's lift (armpose::TorsoLift): how far the player's neck has risen
// since calibration -- the head offset the view applies (cameraCentre less the
// game's eye, vertical), less what tipping the head moved the eye -- along the
// world's up in model space, shared up the spine by the joints' heights.
TorsoLiftPlan PlanTorsoLift(const GameplayPoseFrame& frame, const animik::Location& location,
                            const std::uint8_t* absolute, unsigned count, const PoseSettings& pose)
{
    TorsoLiftPlan lift{};
    if (!pose.torsoLift || !gHandRig.liftReady || !frame.cameraCentreValid) {
        gLiftMm.store(0, std::memory_order_relaxed);
        return lift;
    }
    const Pose& head = frame.tracking.head;
    const float rise = (frame.cameraCentre.z - frame.reticleOrigin.z) + (armpose::LevelEyeHeight(head) - head.position.y);
    const float metres = armpose::TorsoLift(rise, pose.liftUp, pose.liftDown);
    const Vec3 up = Sub(animik::WorldToModel(location, Add(frame.reticleOrigin, Vec3{0, 0, 1})),
                        animik::WorldToModel(location, frame.reticleOrigin));
    const float upLength = Length(up);
    if (!(upLength > 1e-6f) || !std::isfinite(upLength)) { return lift; }
    const Vec3 unit = Scale(up, 1.0f / upLength);
    const auto height = [&](int joint, float& out) {
        QuatT j{};
        if (!ReadQuatT(const_cast<std::uint8_t*>(absolute), count, joint, j)) { return false; }
        out = Dot(Vec3{j.t[0], j.t[1], j.t[2]}, unit);
        return std::isfinite(out);
    };
    float base = 0.0f, heights[armpose::kSpineJoints]{};
    bool read = height(gHandRig.spineBase, base);
    for (int k = 0; k < armpose::kSpineJoints && read; ++k) { read = height(gHandRig.spine[k], heights[k]); }
    if (!read || !armpose::LiftShares(base, heights, lift.shares)) { return lift; }
    lift.model = Scale(up, metres);
    lift.apply = true;
    gLiftMm.store(static_cast<int>(std::lround(metres * 1000.0f)), std::memory_order_relaxed);
    return lift;
}

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

// Before the native pass, under gIkMutex: the torso and each shoulder, and the
// torso's lift (applied after the pass; the shoulders are placed on it now).
void PrepareArms(const GameplayPoseFrame& frame, const animik::Location& location, std::uint8_t* absolute,
                 unsigned count, const PoseSettings& pose, ArmPlan plan[2], TorsoLiftPlan& lift)
{
    lift = {};
    if (pose.mode != 1 || pose.armMode == 0 || gHandRig.skeleton != gRigSkeleton.load()) { return; }
    XrMap map{&frame, {}, location};
    if (!AnchorPoint(frame, IkAnchor(), map.anchor) || !(location.s > 1e-4f)) { return; }
    lift = PlanTorsoLift(frame, location, absolute, count, pose);
    const Pose& head = frame.tracking.head;
    const float headYaw = armpose::HeadYaw(head.orientation);
    armpose::TorsoFrame torso;
    float shoulderYaw = 0.0f;
    {
        std::lock_guard lock(gArmMutex);
        const std::uint64_t now = preyvr::timing::MonotonicNanoseconds();
        const float dt = gTorsoNs != 0 ? static_cast<float>(now - gTorsoNs) * 1e-9f : 0.0f;
        gTorsoNs = now;
        // The shared torso (TorsoLane), so the shoulders and the body slots
        // agree; this filter only while that lane is not running, with that
        // lane's settings.
        float yaw = 0.0f;
        if (BodyTorsoYaw(yaw)) { gTorsoYaw.Reset(); }
        else {
            float deadzone = 0.0f, relax = 0.0f;
            BodyTorsoSettings(deadzone, relax);
            yaw = gTorsoYaw.Update(headYaw, dt, deadzone, relax);
        }
        // Two hands on a long weapon: the shooter's bladed stance, left
        // shoulder forward (the torso turns right), so the drawn support arm
        // reaches the foregrip. Eased by the hold's own blend.
        const std::uint64_t longOwner = gLongGeneration.load(std::memory_order_relaxed);
        const float twist = longOwner != 0 && frame.weaponGeneration == longOwner && frame.twoHand.blend > 0.0f
            ? gLongTwistDeg.load(std::memory_order_relaxed) * 0.01745329f * frame.twoHand.blend : 0.0f;
        shoulderYaw = yaw - twist;
        torso = armpose::TorsoFromYaw(shoulderYaw);
        gArmStats.torsoYawDeg = shoulderYaw * kDegreesPerRadian;
        gArmStats.headYawDeg = headYaw * kDegreesPerRadian;
    }
    const armpose::Body& body = pose.body;
    for (int s = 0; s < 2; ++s) {
        QuatT clavicle{}, upper{};
        if (!ReadQuatT(absolute, count, gHandRig.clavicle[s], clavicle) ||
            !ReadQuatT(absolute, count, gHandRig.upperArm[s], upper)) { continue; }
        ArmPlan& p = plan[s];
        p.torso = torso;
        // Where the lift will put them.
        p.clavicleXr = map.ToXr(Add(Vec3{clavicle.t[0], clavicle.t[1], clavicle.t[2]},
                                    lift.Of(gHandRig, gHandRig.clavicle[s])));
        const Vec3 animated = map.ToXr(Add(Vec3{upper.t[0], upper.t[1], upper.t[2]},
                                           lift.Of(gHandRig, gHandRig.upperArm[s])));
        // From the neck, the twist turning the shoulders about it.
        p.targetXr = pose.neckAnchor ? armpose::ShoulderFromHead(head, shoulderYaw, s == 0 ? 1 : -1, body)
                                     : armpose::ShoulderTarget(head.position, torso, s == 0 ? 1 : -1, body);
        p.shoulderXr = armpose::PlaceShoulder(p.clavicleXr, Dist3(animated, p.clavicleXr), p.targetXr,
                                              body.clavicleStretch);
        p.shoulderModel = map.ToModel(p.shoulderXr);
        p.reachModel = 0.995f * (body.upper + body.fore) * body.armStretch / location.s;
        p.valid = std::isfinite(p.shoulderModel.x) && std::isfinite(p.shoulderModel.y) &&
                  std::isfinite(p.shoulderModel.z);
    }
}

// After the native pass, before the arms: the torso and all it carries, up
// (or down) by each joint's share of the lift. The camera's joint stays.
void ApplyTorsoLift(std::uint8_t* absolute, unsigned count, const HandRig& rig, const TorsoLiftPlan& lift)
{
    if (!lift.apply || rig.skeleton != gRigSkeleton.load()) { return; }
    const unsigned joints = std::min(count, kMaxJoints);
    for (unsigned j = 0; j < joints; ++j) {
        const Vec3 by = lift.Of(rig, static_cast<int>(j));
        if (by.x == 0.0f && by.y == 0.0f && by.z == 0.0f) { continue; }
        QuatT a{};
        if (!ReadQuatT(absolute, count, static_cast<int>(j), a)) { return; }
        const float t[3] = {a.t[0] + by.x, a.t[1] + by.y, a.t[2] + by.z};
        if (!WriteFloatsGuarded(absolute + static_cast<std::size_t>(j) * kQuatTStride + 4 * sizeof(float), t, 3)) {
            return;
        }
    }
}

void SolveArms(const GameplayPoseFrame& frame, std::uint8_t* absolute, unsigned count,
               const animik::Location& location, const HandRig& rig, const ArmPlan plan[2],
               const PoseSettings& pose)
{
    XrMap map{&frame, {}, location};
    if (!AnchorPoint(frame, IkAnchor(), map.anchor)) { return; }
    const armpose::Body& body = pose.body;
    ArmStats stats{};
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
        Vec3 pole{};
        {
            std::lock_guard lock(gArmMutex);
            const float dt = gPoleNs[s] != 0 ? static_cast<float>(now - gPoleNs[s]) * 1e-9f : 0.0f;
            const bool fresh = gPoleNs[s] != 0 && dt < 0.5f;
            gPoleNs[s] = now;
            pole = armpose::ChooseElbowPole(shoulder, wrist, body.upper, body.fore, body.armStretch, handLength,
                                            handPalm, side, bodyPole, pose.wrist, fresh ? &gPole[s] : nullptr);
            pole = fresh ? armpose::SmoothDirection(gPole[s], pole, dt, pose.poleTau) : pole;
            gPole[s] = pole;
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
    }
    std::lock_guard lock(gArmMutex);
    for (int s = 0; s < 2; ++s) {
        if (stats.solved[s] && std::isfinite(stats.reachCm[s])) {
            gReachMaxCm[s] = std::max(gReachMaxCm[s], stats.reachCm[s]);
        }
    }
    stats.torsoYawDeg = gArmStats.torsoYawDeg;
    stats.headYawDeg = gArmStats.headYawDeg;
    gArmStats = stats;
}

} // namespace ik

} // namespace preyvr::dll
