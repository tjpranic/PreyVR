#include "AnimIkTakeover.h"
#include "AnimIkState.h"

// The research records and reports: the per-hand trace (ik.trace), the marks
// (pose.marks), the skeleton and helper dumps (ik.skel, ik.helpers), and the
// reports the command channel prints.

namespace preyvr::dll {
using namespace ik;

namespace {

std::string LiftLimitText(float metres)
{
    return std::isfinite(metres) ? std::to_string(static_cast<int>(std::lround(metres * 1000.0f))) : "none";
}

void CopyName(const char* name, char* out, std::size_t capacity)
{
    __try {
        std::size_t i = 0;
        for (; name != nullptr && i + 1 < capacity && name[i] != '\0'; ++i) { out[i] = name[i]; }
        out[i] = '\0';
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(out, capacity, "?");
    }
}

std::string WeaponVtableText()
{
    char text[16];
    std::snprintf(text, sizeof(text), "0x%X", gWeaponVtableRva.load(std::memory_order_relaxed));
    return text;
}

std::string SupportLatched()
{
    std::lock_guard lock(gSupportLatchMutex);
    if (!gSupportLatch.Latched()) { return "0"; }
    const Vec3 s = gSupportLatch.Socket();
    return "1 latchedSupportMm=" + std::to_string(static_cast<int>(s.x * 1000.f)) + "," +
        std::to_string(static_cast<int>(s.y * 1000.f)) + "," + std::to_string(static_cast<int>(s.z * 1000.f));
}

float AngleDeg(Vec3 a, Vec3 b)
{
    const float la = Length(a), lb = Length(b);
    if (la < 1e-6f || lb < 1e-6f) { return -1.0f; }
    const float c = std::clamp((a.x * b.x + a.y * b.y + a.z * b.z) / (la * lb), -1.0f, 1.0f);
    return std::acos(c) * kDegreesPerRadian;
}

HandMarks LatestMarks() { std::lock_guard lock(gMarksMutex); return gMarks; }

} // namespace

namespace ik {

// Each frame's +Y (the native "forward" of a helper) and +Z in the weapon model,
// so an authored helper can be compared against the model's long axis.
void DumpWeaponFrames(const RigIdentity& owner)
{
    static weaponrig::HelperInfo frames[400];
    unsigned count = 0;
    Vec3 lo{}, hi{};
    if (!weaponrig::ListWeaponFrames({nullptr, ReadAlignmentMemory},
            PreyBase(), owner, frames, 400, count, lo, hi)) {
        lifecycle::Log("preyvr_anim_ik helpers result=refused");
        return;
    }
    char line[320];
    std::snprintf(line, sizeof(line), "preyvr_anim_ik helpers result=0 count=%u boundsMm=%.0f,%.0f,%.0f..%.0f,%.0f,%.0f",
                  count, lo.x * 1000, lo.y * 1000, lo.z * 1000, hi.x * 1000, hi.y * 1000, hi.z * 1000);
    lifecycle::Log(line);
    for (unsigned i = 0; i < count; ++i) {
        const auto& f = frames[i];
        const Vec3 y = Rotate(f.frame.orientation, Vec3{0, 1, 0});
        const Vec3 z = Rotate(f.frame.orientation, Vec3{0, 0, 1});
        std::snprintf(line, sizeof(line),
                      "preyvr_anim_ik helper %s name=%s posMm=%.0f,%.0f,%.0f yAxis=%.3f,%.3f,%.3f zAxis=%.3f,%.3f,%.3f",
                      f.joint ? "joint" : (f.bone ? "bone" : "skin"), f.name, f.frame.position.x * 1000,
                      f.frame.position.y * 1000, f.frame.position.z * 1000, y.x, y.y, y.z, z.x, z.y, z.z);
        lifecycle::Log(line);
    }
}

void DumpSkeletonPose(std::uint8_t* character, std::uint8_t* relative, const std::uint8_t* before,
                      std::uint8_t* absolute, unsigned count)
{
    void* skeletonPtr = nullptr;
    if (!ReadPointer(character + kCharSkeleton, &skeletonPtr) || skeletonPtr == nullptr) { return; }
    auto* const skeleton = static_cast<std::uint8_t*>(skeletonPtr);
    void* table = nullptr;
    ReadPointer(skeleton + kSkelJoints, &table);
    Log("skel begin joints=" + std::to_string(count));
    for (unsigned j = 0; j < count && j < kMaxJoints; ++j) {
        QuatT rel{}, abs{}, pre{};
        ReadFloats(relative + j * kQuatTStride, &rel.q[0], 7);
        ReadFloats(absolute + j * kQuatTStride, &abs.q[0], 7);
        std::memcpy(&pre, before + j * kQuatTStride, sizeof(pre));
        int fields[6]{-9, -9, -9, -9, -9, -9};
        if (table != nullptr) {
            for (int k = 0; k < 6; ++k) {
                ReadInt(static_cast<std::uint8_t*>(table) + j * kJointStride + 8 + 4 * k, &fields[k]);
            }
        }
        // abs[p] * rel[j] == abs[j], judged on the pre-ADIK pose (consistent by construction).
        int parent = -1;
        float best = 1e9f;
        for (unsigned p = 0; p < count; ++p) {
            if (p == j) { continue; }
            QuatT a{};
            std::memcpy(&a, before + p * kQuatTStride, sizeof(a));
            const Vec3 r = Rotate(Quaternion{a.q[0], a.q[1], a.q[2], a.q[3]}, Vec3{rel.t[0], rel.t[1], rel.t[2]});
            const float dx = a.t[0] + r.x - pre.t[0], dy = a.t[1] + r.y - pre.t[1], dz = a.t[2] + r.z - pre.t[2];
            const Quaternion q = Multiply(Quaternion{a.q[0], a.q[1], a.q[2], a.q[3]},
                                          Quaternion{rel.q[0], rel.q[1], rel.q[2], rel.q[3]});
            const float dq = std::fabs(q.x * pre.q[0] + q.y * pre.q[1] + q.z * pre.q[2] + q.w * pre.q[3]);
            const float err = std::sqrt(dx * dx + dy * dy + dz * dz) + (1.0f - dq);
            if (err < best) { best = err; parent = static_cast<int>(p); }
        }
        char name[64];
        CopyName(JointName(skeleton, static_cast<int>(j)), name, sizeof(name));
        char line[512];
        std::snprintf(line, sizeof(line),
            "skel j=%u name=%s f=%d,%d,%d,%d,%d,%d parent=%d perr=%.5f "
            "rel=%.4f,%.4f,%.4f,%.4f|%.4f,%.4f,%.4f pre=%.4f,%.4f,%.4f,%.4f|%.4f,%.4f,%.4f "
            "abs=%.4f,%.4f,%.4f,%.4f|%.4f,%.4f,%.4f",
            j, name, fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], parent, best,
            rel.q[0], rel.q[1], rel.q[2], rel.q[3], rel.t[0], rel.t[1], rel.t[2],
            pre.q[0], pre.q[1], pre.q[2], pre.q[3], pre.t[0], pre.t[1], pre.t[2],
            abs.q[0], abs.q[1], abs.q[2], abs.q[3], abs.t[0], abs.t[1], abs.t[2]);
        Log(line);
    }
    Log("skel end");
}

// After the native pass: the hands as the engine will skin them, in the app's
// OpenXR space, for pose.marks and pose.report.
void CaptureMarks(const GameplayPoseFrame& frame, std::uint8_t* absolute, unsigned count,
                  const animik::Location& location, const HandRig& rig, const FingerPlan& fingers)
{
    Vec3 anchor{};
    if (!AnchorPoint(frame, IkAnchor(), anchor)) { return; }
    const PoseSettings settings = ReadPoseSettings();
    HandMarks marks{};
    const auto toXr = [&](const Vec3& model) {
        return stereo::EngineToTracking(animik::ModelToWorld(location, model), anchor, frame.yaw, frame.tracking.head);
    };
    for (int s = 0; s < 2; ++s) {
        if (!rig.valid[s]) { continue; }
        QuatT w{};
        if (!ReadQuatT(absolute, count, rig.wrist[s], w)) { return; }
        const Quaternion wq{w.q[0], w.q[1], w.q[2], w.q[3]};
        const Vec3 wt{w.t[0], w.t[1], w.t[2]};
        marks.wrist[s] = toXr(wt);
        for (int j = 0; j < handpose::kJointCount; ++j) {
            QuatT a{};
            if (!ReadQuatT(absolute, count, rig.joint[s][j], a)) { return; }
            const Vec3 at{a.t[0], a.t[1], a.t[2]};
            marks.joint[s][j] = toXr(at);
            const bool distal = j == handpose::kThumb3 || j == handpose::kIndex3 || j == handpose::kMiddle3 ||
                                j == handpose::kRing3 || j == handpose::kPinky3;
            if (distal) {
                // No tip joint in the rig: the distal phalanx, ~2.2 cm along its bone.
                marks.tip[s][j] = toXr(Add(at, Rotate(Quaternion{a.q[0], a.q[1], a.q[2], a.q[3]},
                                                       Vec3{0.022f, 0.0f, 0.0f})));
            }
        }
        const Vec3 point = handpose::GripPointInWrist(rig.frame[s], rig.knuckle[s], settings.point);
        marks.gripPoint[s] = toXr(Add(wt, Rotate(wq, point)));
        // Axes as directions in OpenXR space: difference of two mapped points.
        const auto dir = [&](const Vec3& local) {
            return Sub(toXr(Add(wt, Rotate(wq, local))), marks.wrist[s]);
        };
        marks.length[s] = dir(rig.frame[s].length);
        marks.palm[s] = dir(rig.frame[s].palm);
        const auto& state = frame.tracking.hands[s == 0 ? static_cast<unsigned>(Hand::right)
                                                        : static_cast<unsigned>(Hand::left)];
        marks.grip[s] = state.gripPose;
        marks.aim[s] = state.aimPose;
        QuatT pj{};
        if (rig.handProp[s] >= 0 && ReadQuatT(absolute, count, rig.handProp[s], pj)) {
            const Quaternion pq{pj.q[0], pj.q[1], pj.q[2], pj.q[3]};
            const Vec3 pt{pj.t[0], pj.t[1], pj.t[2]};
            marks.prop[s] = toXr(pt);
            const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            for (int a = 0; a < 3; ++a) {
                marks.propAxis[s][a] = Sub(toXr(Add(pt, Rotate(pq, Scale(axes[a], 0.05f)))), marks.prop[s]);
            }
            marks.propValid[s] = true;
        }
        marks.posed[s] = settings.mode == 1;
        QuatT c{}, u{}, l{};
        if (rig.clavicle[s] >= 0 && rig.upperArm[s] >= 0 && rig.lowerArm[s] >= 0 &&
            ReadQuatT(absolute, count, rig.clavicle[s], c) && ReadQuatT(absolute, count, rig.upperArm[s], u) &&
            ReadQuatT(absolute, count, rig.lowerArm[s], l)) {
            marks.clavicle[s] = toXr(Vec3{c.t[0], c.t[1], c.t[2]});
            marks.shoulder[s] = toXr(Vec3{u.t[0], u.t[1], u.t[2]});
            marks.elbow[s] = toXr(Vec3{l.t[0], l.t[1], l.t[2]});
            marks.arm[s] = true;
        }
        if (gMarkGoal[s].set) {
            marks.goalRaw[s] = toXr(gMarkGoal[s].raw);
            marks.goalWritten[s] = toXr(gMarkGoal[s].written);
            marks.goal[s] = true;
            gMarkGoal[s].set = false;
        }
    }
    {
        QuatT n{}, h{}, e{};
        if (rig.neck >= 0 && rig.head >= 0 && rig.headEnd >= 0 && ReadQuatT(absolute, count, rig.neck, n) &&
            ReadQuatT(absolute, count, rig.head, h) && ReadQuatT(absolute, count, rig.headEnd, e)) {
            marks.neck = toXr(Vec3{n.t[0], n.t[1], n.t[2]});
            marks.headJoint = toXr(Vec3{h.t[0], h.t[1], h.t[2]});
            marks.headEnd = toXr(Vec3{e.t[0], e.t[1], e.t[2]});
            marks.headValid = true;
        }
    }
    marks.head = frame.tracking.head;
    marks.support = fingers.support;
    marks.stamp = frame.tracking.sequence;
    marks.valid = true;
    {
        // The arms as skinned for this frame, for the wrist hologram (s: 0 right, 1 left).
        WristArmFrame arm{};
        arm.displayTime = frame.tracking.displayTime;
        arm.publishedNs = preyvr::timing::MonotonicNanoseconds();
        if (rig.valid[1] && marks.arm[1]) {
            arm.left = true;
            arm.wrist = marks.wrist[1];
            arm.elbow = marks.elbow[1];
            arm.palm = marks.palm[1];
        }
        if (rig.valid[0]) {
            arm.right = true;
            arm.rightWrist = marks.wrist[0];
            arm.rightGrip = marks.gripPoint[0];
            arm.rightAim = Rotate(marks.aim[0].orientation, Vec3{0.0f, 0.0f, -1.0f});
            arm.weapon = !AnimIkFreeArmsBound();
        }
        PublishWristArm(arm);
    }
    {
        std::unique_lock lock(gMarksMutex, std::try_to_lock);
        if (lock.owns_lock()) { gMarks = marks; }
    }
    // Every 30 s into the log: on a headset this records what the mock can only
    // estimate -- the runtime's grip pose relative to its aim pose -- and how
    // far each drawn palm is from the controller, with no command to type.
    static std::atomic<std::uint64_t> lastLog{0};
    const std::uint64_t now = preyvr::timing::MonotonicNanoseconds();
    std::uint64_t last = lastLog.load();
    if ((last == 0 || now - last > 30000000000ull) && lastLog.compare_exchange_strong(last, now)) {
        Log("pose_report" + HandPoseReport());
    }
}

} // namespace ik

std::string AnimIkWeaponAlignmentReport()
{
    std::lock_guard stateLock(gIkMutex);
    return " enabled=" + std::to_string(gAlignWeapon) +
        " status=" + weaponrig::StatusName(gAlignStatus) +
        " basis=" + weaponrig::SourceName(gAlignBasis.source) +
        " helper=" + (gAlignBasis.helper[0] ? std::string(gAlignBasis.helper) : "none") +
        " socket=" + std::to_string(gAlignBasis.socketJoint) +
        " generation=" + std::to_string(gAlignGeneration) +
        " sequence=" + std::to_string(gAlignSequence) +
        " applied=" + std::to_string(gAlignApplied) +
        " refused=" + std::to_string(gAlignRefused) +
        " wideSupport=" + std::to_string(AnimIkWideSupport()) +
        " supportLatch=" + std::to_string(AnimIkSupportLatch()) + "/" + SupportLatched() +
        " longReach=" + std::to_string(gLongOn.load() ? 1 : 0) + " holdBackMm=" + std::to_string(gHoldBackMm.load()) +
        " comfortMm=" + std::to_string(gLongComfortMm.load()) + " maxBackMm=" + std::to_string(gLongMaxBackMm.load()) +
        " supportBackMm=" + std::to_string(gLongSupportBackMm.load()) + " twistDeg=" + std::to_string(gLongTwistDeg.load()) +
        " weaponVtable=" + WeaponVtableText() +
        " nativeSupportMm=" + std::to_string(gSupportSocketMm[0].load()) + "," +
        std::to_string(gSupportSocketMm[1].load()) + "," + std::to_string(gSupportSocketMm[2].load());
}

std::string AnimIkTraceReport()
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (!gTraceEnabled.load(std::memory_order_acquire)) { return " ikTrace=off"; }
    std::lock_guard stateLock(gIkMutex);
    const auto mm = [](float v) { return static_cast<int>(v * 1000.0f); };
    const auto vec = [&out, &mm](const char* name, const Vec3& v) {
        out << ' ' << name << '=' << mm(v.x) << ',' << mm(v.y) << ',' << mm(v.z);
    };
    for (unsigned int hand = 0; hand < 2; ++hand) {
        const auto& t = gTrace[hand];
        const char* const tag = hand == 0 ? "R" : "L";
        out << " ikTrace" << tag << '=' << (t.valid ? "fresh" : "none");
        if (!t.valid) { continue; }
        out << " ikSeq" << tag << '=' << t.trackingSequence
            << " ikOwner" << tag << "=0x" << std::hex << t.owner << std::dec;
        // Millimetres throughout, so a term that moves is visible against terms
        // that do not without reading floats out of a log.
        vec((std::string("ikAnchor") + tag).c_str(), t.anchor);
        vec((std::string("ikHeadPos") + tag).c_str(), t.head.position);
        vec((std::string("ikGrip") + tag).c_str(), t.grip.position);
        vec((std::string("ikCtrlWorld") + tag).c_str(), t.controllerWorld);
        vec((std::string("ikCharLoc") + tag).c_str(), t.characterLocation);
        vec((std::string("ikShoulder") + tag).c_str(), t.shoulderModel);
        vec((std::string("ikGoalRaw") + tag).c_str(), t.goalRaw);
        vec((std::string("ikGoalScaled") + tag).c_str(), t.goalScaled);
        vec((std::string("ikGoalClamped") + tag).c_str(), t.goalClamped);
        out << " ikYawMdeg" << tag << '='
            << static_cast<int>(t.yaw * 57295.779513f)
            << " ikReachLimit" << tag << '=' << mm(t.reachLimit)
            << " ikReachPct" << tag << '=' << t.reachPercent
            << " ikDidClamp" << tag << '=' << (t.clamped ? 1 : 0)
            << " ikCal" << tag << '=' << (t.calibrated ? 1 : 0)
            << " ikRot" << tag << '=' << (t.wroteRotation ? 1 : 0);
    }
    return out.str();
}

DWORD DumpAnimIk()
{
    const auto skeleton = reinterpret_cast<std::uint8_t*>(gRigSkeleton.load(std::memory_order_acquire));
    if (skeleton == nullptr) {
        Log("result=refused detail=no_rig_identified");
        return 1;
    }
    void* adik = nullptr;
    if (ReadPointer(skeleton + kSkelAdik, &adik) && adik != nullptr) {
        const unsigned int count = DynArrayCount(adik);
        for (unsigned int i = 0; i < count && i < 64; ++i) {
            auto* const entry = static_cast<std::uint8_t*>(adik) + i * kAdikStride;
            int t = -1, w = -1;
            void* tn = nullptr;
            void* wn = nullptr;
            ReadInt(entry + kAdikTarget, &t);
            ReadInt(entry + kAdikWeight, &w);
            ReadPointer(entry + kAdikTargetName, &tn);
            ReadPointer(entry + 0x20, &wn);
            char line[256];
            FormatAdikLine(line, sizeof(line), i, t, static_cast<const char*>(tn), w,
                           static_cast<const char*>(wn));
            Log(line);
        }
    }
    void* limbs = nullptr;
    if (ReadPointer(skeleton + kSkelLimbs, &limbs) && limbs != nullptr) {
        const unsigned int count = DynArrayCount(limbs);
        for (unsigned int i = 0; i < count && i < 32; ++i) {
            auto* const limb = static_cast<std::uint8_t*>(limbs) + i * kLimbStride;
            void* chain = nullptr;
            int c0 = -1, c1 = -1, c2 = -1, c3 = -1, tag = 0;
            ReadInt(limb + kLimbTag, &tag);
            if (ReadPointer(limb + kLimbChain, &chain) && chain != nullptr) {
                ReadInt(static_cast<std::uint8_t*>(chain) + 0x00, &c0);
                ReadInt(static_cast<std::uint8_t*>(chain) + 0x10, &c1);
                ReadInt(static_cast<std::uint8_t*>(chain) + 0x20, &c2);
                ReadInt(static_cast<std::uint8_t*>(chain) + 0x30, &c3);
            }
            char line[160];
            std::snprintf(line, sizeof(line), "limb[%u] tag=0x%08x chain=%d/%d/%d/%d", i,
                          static_cast<unsigned int>(tag), c0, c1, c2, c3);
            Log(line);
        }
    }
    Log("result=0 detail=dumped");
    return 0;
}

std::string ArmReport()
{
    const PoseSettings p = ReadPoseSettings();
    float torsoDeadzone = 0.0f, torsoRelax = 0.0f;
    BodyTorsoSettings(torsoDeadzone, torsoRelax);
    ArmStats st;
    float reachMax[2]{};
    {
        std::lock_guard lock(gArmMutex);
        st = gArmStats;
        for (int s = 0; s < 2; ++s) {
            reachMax[s] = gReachMaxCm[s];
            gReachMaxCm[s] = 0.0f;
        }
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed;
    out.precision(1);
    const armpose::Body& b = p.body;
    out << " armMode=" << p.armMode << " anchor=" << (p.neckAnchor ? "neck" : "eyes")
        << " shoulderMm=" << b.shoulderForward * 1000 << ',' << b.shoulderOut * 1000
        << ',' << b.shoulderUp * 1000 << " lenMm=" << b.upper * 1000 << ',' << b.fore * 1000
        << " lengthCm=" << armpose::ArmLength(b) * 100
        << " lift=" << p.torsoLift << " liftLimitsMm=" << LiftLimitText(p.liftUp) << ',' << LiftLimitText(p.liftDown)
        << " liftMm=" << gLiftMm.load(std::memory_order_relaxed)
        << " liftReady=" << gLiftReady.load(std::memory_order_relaxed)
        << " cameraJoint=" << gLiftCamera.load(std::memory_order_relaxed)
        << " clavStretch=" << b.clavicleStretch << " armStretch=" << b.armStretch << " knee=" << p.armKnee
        << " priorDeg=" << p.wrist.flexionSigmaDeg << ',' << p.wrist.deviationRestDeg << ','
        << p.wrist.deviationSigmaDeg << ',' << p.wrist.swivelSigmaDeg
        << " romDeg=" << p.wrist.flexionMaxDeg << ',' << p.wrist.extensionMaxDeg << ','
        << p.wrist.radialMaxDeg << ',' << p.wrist.ulnarMaxDeg
        << " holdDeg=" << p.wrist.holdSigmaDeg << " poleTauMs=" << p.poleTau * 1000
        << " torsoDeadzoneDeg=" << torsoDeadzone * kDegreesPerRadian << " torsoRelax=" << torsoRelax
        << " headYawDeg=" << st.headYawDeg << " torsoYawDeg=" << st.torsoYawDeg;
    for (int s = 0; s < 2; ++s) {
        const char* t = s == 0 ? "R" : "L";
        out << " solved" << t << '=' << st.solved[s];
        if (!st.solved[s]) { continue; }
        const Vec3 d = Sub(st.shoulder[s], st.target[s]);
        out << " shoulderToTargetCm" << t << '=' << std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) * 100.0f
            << " handMoveCm" << t << '=' << st.handMoveCm[s]
            << " reachCm" << t << '=' << st.reachCm[s] << " reachMaxCm" << t << '=' << reachMax[s];
        out.precision(3);
        out << " scale" << t << '=' << st.scale[s];
        out.precision(1);
        out << " straight" << t << '=' << st.straight[s];
    }
    return out.str();
}

std::string HandPoseReport()
{
    const PoseSettings settings = ReadPoseSettings();
    const HandMarks marks = LatestMarks();
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed;
    out.precision(1);
    out << " poseMode=" << settings.mode << " curl=" << settings.curl * 100.0f
        << " along=" << settings.point.alongHand * 1000.0f << " outOfPalmMm=" << settings.point.outOfPalm * 1000.0f
        << " thumbMm=" << settings.point.towardThumb * 1000.0f
        << " leanDeg=" << settings.tubeLean * kDegreesPerRadian
        << " reachMode=" << settings.reachMode << " knee=" << settings.knee * 100.0f
        << " weaponRoll=" << (settings.weaponRoll ? 1 : 0) << " stretch=" << settings.stretch * 100.0f
        << " rollDeg=" << gWeaponRollMilliDeg.load(std::memory_order_relaxed) / 1000.0f;
    {
        std::lock_guard lock(gIkMutex);
        out << " rigR=" << gHandRig.valid[0] << " rigL=" << gHandRig.valid[1];
    }
    out << " marks=" << (marks.valid ? "fresh" : "none");
    if (!marks.valid) { return out.str(); }
    out << " support=" << marks.support;
    for (int s = 0; s < 2; ++s) {
        const char* tag = s == 0 ? "R" : "L";
        const auto side = s == 1 ? handpose::Side::left : handpose::Side::right;
        const Pose& grip = marks.grip[s];
        const handpose::Frame expected = handpose::GripHandFrame(side, settings.tubeLean);
        const Vec3 lenExp = Rotate(grip.orientation, expected.length);
        const Vec3 palmExp = Rotate(grip.orientation, expected.palm);
        // The game palm's grip point against the controller's grip origin,
        // and the game hand's axes against the real hand's.
        out << " palmErrCm" << tag << '=' << Length(Sub(marks.gripPoint[s], grip.position)) * 100.0f
            << " lenErrDeg" << tag << '=' << AngleDeg(marks.length[s], lenExp)
            << " palmNormErrDeg" << tag << '=' << AngleDeg(marks.palm[s], palmExp)
            << " wristToGripCm" << tag << '=' << Length(Sub(marks.wrist[s], grip.position)) * 100.0f;
        // The game hand's axes in the controller's grip frame (unit), to read
        // WHICH way a hand is off, not only by how much.
        const auto inGrip = [&](Vec3 v) {
            const float l = Length(v);
            return l > 1e-6f ? Rotate(Conjugate(grip.orientation), Scale(v, 1.0f / l)) : Vec3{};
        };
        const Vec3 lg = inGrip(marks.length[s]), pg = inGrip(marks.palm[s]);
        out.precision(2);
        out << " lenInGrip" << tag << '=' << lg.x << ',' << lg.y << ',' << lg.z
            << " palmInGrip" << tag << '=' << pg.x << ',' << pg.y << ',' << pg.z;
        out.precision(1);
        // grip in aim: what a real runtime says about its controller; the mock
        // uses an estimate (DVR_MockGrip.txt) until a headset has reported it.
        const Pose& aim = marks.aim[s];
        const Quaternion rel = Normalize(Multiply(Conjugate(aim.orientation), grip.orientation));
        const Vec3 off = Rotate(Conjugate(aim.orientation), Sub(grip.position, aim.position));
        const float angle = handpose::AngleBetween(aim.orientation, grip.orientation) * kDegreesPerRadian;
        out.precision(3);
        out << " gripInAim" << tag << "=q" << rel.x << ',' << rel.y << ',' << rel.z << ',' << rel.w;
        out.precision(1);
        out << "|deg" << angle << "|cm" << off.x * 100.0f << ',' << off.y * 100.0f << ',' << off.z * 100.0f;
        // The prop joint (a held grenade's) in the grip frame: cm, and its axes (unit).
        if (marks.propValid[s]) {
            const Vec3 at = Rotate(Conjugate(grip.orientation), Sub(marks.prop[s], grip.position));
            const Vec3 px = inGrip(marks.propAxis[s][0]), py = inGrip(marks.propAxis[s][1]),
                       pz = inGrip(marks.propAxis[s][2]);
            out << " propInGripCm" << tag << '=' << at.x * 100.0f << ',' << at.y * 100.0f << ',' << at.z * 100.0f;
            out.precision(2);
            out << " propXInGrip" << tag << '=' << px.x << ',' << px.y << ',' << px.z << " propYInGrip" << tag << '='
                << py.x << ',' << py.y << ',' << py.z << " propZInGrip" << tag << '=' << pz.x << ',' << pz.y << ','
                << pz.z;
            out.precision(1);
        }
    }
    return out.str();
}

std::string HandPoseMarks()
{
    const PoseSettings settings = ReadPoseSettings();
    const HandMarks marks = LatestMarks();
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed;
    out.precision(4);
    if (!marks.valid) { return out.str(); }
    const auto seg = [&](Vec3 a, Vec3 b, int r, int g, int bl) {
        out << a.x << ' ' << a.y << ' ' << a.z << ' ' << b.x << ' ' << b.y << ' ' << b.z << ' '
            << r << ' ' << g << ' ' << bl << '\n';
    };
    const auto unit = [](Vec3 v, float length) {
        const float l = Length(v);
        return l > 1e-6f ? Scale(v, length / l) : Vec3{};
    };
    for (int s = 0; s < 2; ++s) {
        const int r = s == 1 ? 255 : 255, g = s == 1 ? 255 : 200, b = s == 1 ? 255 : 120;
        // The skeleton: wrist -> metacarpal -> three phalanges -> tip, and the thumb.
        const int chains[5][4] = {
            {handpose::kThumb1, handpose::kThumb2, handpose::kThumb3, -1},
            {handpose::kIndexBase, handpose::kIndex1, handpose::kIndex2, handpose::kIndex3},
            {handpose::kMiddleBase, handpose::kMiddle1, handpose::kMiddle2, handpose::kMiddle3},
            {handpose::kRingBase, handpose::kRing1, handpose::kRing2, handpose::kRing3},
            {handpose::kPinkyBase, handpose::kPinky1, handpose::kPinky2, handpose::kPinky3},
        };
        for (const auto& chain : chains) {
            Vec3 previous = marks.wrist[s];
            int last = -1;
            for (int k = 0; k < 4 && chain[k] >= 0; ++k) {
                seg(previous, marks.joint[s][chain[k]], r, g, b);
                previous = marks.joint[s][chain[k]];
                last = chain[k];
            }
            if (last >= 0) { seg(previous, marks.tip[s][last], r, g, b); }
        }
        // The game hand's frame at its grip point: length cyan, palm normal magenta.
        const Vec3 p = marks.gripPoint[s];
        seg(p, Add(p, unit(marks.length[s], 0.08f)), 0, 255, 255);
        seg(p, Add(p, unit(marks.palm[s], 0.06f)), 255, 0, 255);
        // The real hand's frame at the controller's grip origin, darker.
        const auto side = s == 1 ? handpose::Side::left : handpose::Side::right;
        const handpose::Frame expected = handpose::GripHandFrame(side, settings.tubeLean);
        const Pose& grip = marks.grip[s];
        seg(grip.position, Add(grip.position, Scale(Rotate(grip.orientation, expected.length), 0.08f)), 0, 110, 110);
        seg(grip.position, Add(grip.position, Scale(Rotate(grip.orientation, expected.palm), 0.06f)), 110, 0, 110);
        // The drawn arm, orange: clavicle -> shoulder -> elbow -> wrist.
        if (marks.arm[s]) {
            seg(marks.clavicle[s], marks.shoulder[s], 255, 140, 0);
            seg(marks.shoulder[s], marks.elbow[s], 255, 140, 0);
            seg(marks.elbow[s], marks.wrist[s], 255, 140, 0);
        }
        // The wrist goals as small crosses: white before reach shaping, red as written.
        const auto cross = [&](Vec3 c, int r, int g, int bl) {
            const float h = 0.015f;
            seg(Vec3{c.x - h, c.y, c.z}, Vec3{c.x + h, c.y, c.z}, r, g, bl);
            seg(Vec3{c.x, c.y - h, c.z}, Vec3{c.x, c.y + h, c.z}, r, g, bl);
            seg(Vec3{c.x, c.y, c.z - h}, Vec3{c.x, c.y, c.z + h}, r, g, bl);
        };
        if (marks.goal[s]) {
            cross(marks.goalRaw[s], 255, 255, 255);
            cross(marks.goalWritten[s], 255, 40, 40);
        }
        // The prop joint (what a held grenade hangs on): pale red/green/blue axes.
        if (marks.propValid[s] && s == 0) {
            const Vec3 o = marks.prop[s];
            seg(o, Add(o, marks.propAxis[s][0]), 255, 120, 120);
            seg(o, Add(o, marks.propAxis[s][1]), 120, 255, 120);
            seg(o, Add(o, marks.propAxis[s][2]), 120, 160, 255);
        }
    }
    if (marks.headValid) {
        seg(marks.neck, marks.headJoint, 255, 140, 0);
        seg(marks.headJoint, marks.headEnd, 255, 140, 0);
    }
    // The same as data (not drawn: the mock reads numeric lines only).
    const auto point = [&](const char* name, Vec3 v) {
        out << "J " << name << ' ' << v.x << ' ' << v.y << ' ' << v.z << '\n';
    };
    const auto pose = [&](const char* name, const Pose& p) {
        out << "P " << name << ' ' << p.position.x << ' ' << p.position.y << ' ' << p.position.z << ' '
            << p.orientation.x << ' ' << p.orientation.y << ' ' << p.orientation.z << ' ' << p.orientation.w << '\n';
    };
    pose("head", marks.head);
    {
        ArmStats st;
        {
            std::lock_guard lock(gArmMutex);
            st = gArmStats;
        }
        for (int s = 0; s < 2; ++s) {
            if (!st.solved[s]) { continue; }
            point(s == 0 ? "shoulderTargetR" : "shoulderTargetL", st.target[s]);
        }
        out << "A torsoYawDeg " << st.torsoYawDeg << " headYawDeg " << st.headYawDeg << '\n';
    }
    if (marks.headValid) { point("neck", marks.neck); point("headJnt", marks.headJoint); point("headEnd", marks.headEnd); }
    for (int s = 0; s < 2; ++s) {
        const std::string t = s == 0 ? "R" : "L";
        pose(("grip" + t).c_str(), marks.grip[s]);
        pose(("aim" + t).c_str(), marks.aim[s]);
        point(("wrist" + t).c_str(), marks.wrist[s]);
        point(("palm" + t).c_str(), marks.gripPoint[s]);
        point(("handLen" + t).c_str(), marks.length[s]);     // directions, not points
        point(("handPalm" + t).c_str(), marks.palm[s]);
        if (marks.arm[s]) {
            point(("clavicle" + t).c_str(), marks.clavicle[s]);
            point(("shoulder" + t).c_str(), marks.shoulder[s]);
            point(("elbow" + t).c_str(), marks.elbow[s]);
        }
        if (marks.goal[s]) {
            point(("goalRaw" + t).c_str(), marks.goalRaw[s]);
            point(("goalWritten" + t).c_str(), marks.goalWritten[s]);
        }
        if (marks.propValid[s]) { point(("prop" + t).c_str(), marks.prop[s]); }
    }
    return out.str();
}

std::string AnimIkLongReachReport()
{
    return " longReach=" + std::to_string(gLongOn.load() ? 1 : 0) + " comfortMm=" + std::to_string(gLongComfortMm.load()) +
        " maxBackMm=" + std::to_string(gLongMaxBackMm.load()) + " supportBackMm=" + std::to_string(gLongSupportBackMm.load()) +
        " twistDeg=" + std::to_string(gLongTwistDeg.load()) + " holdBackMm=" + std::to_string(gHoldBackMm.load());
}

DWORD RequestAnimIkHelperDump()
{
    if (!gInstalled.load(std::memory_order_acquire)) { return 1; }
    gHelperDump.store(true);
    return 0;
}

DWORD RequestAnimIkSkeletonDump()
{
    if (!gInstalled.load(std::memory_order_acquire)) { return 1; }
    gSkelDump.store(1);
    return 0;
}

std::string AnimIkFreeArmsReport()
{
    std::ostringstream out;
    out << " free=" << gFreeArms.load() << " freeBound=" << (gFreeBound.load() ? 1 : 0)
        << " freeFrames=" << gFreeFrames.load() << " freeGateZero=" << gFreeGateZero.load()
        << " fistMilli=" << gFreeFistMilli[0].load() << "," << gFreeFistMilli[1].load()
        << " ikMatched=" << gMatched.load() << " ikWritten=" << gWritten[0].load() << "," << gWritten[1].load()
        << " ikNoOwner=" << gNoOwner.load();
    return out.str();
}

} // namespace preyvr::dll
