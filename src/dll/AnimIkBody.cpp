#include "AnimIkTakeover.h"
#include "AnimIkState.h"

#include "RoomScaleLane.h"
#include "preyvr/BodyRig.h"

#include <cstdlib>

// The rest of the body in the IK pass: the body pin (bodyrig::SolvePin), which
// poses the drawn body under the player's head after the native pass
// (docs/BODY-FOLLOW.md).
//
// The body pin's frame: before the native pass the posed body is planned from
// the pose as the animation left it (PlanBody), so the shoulders can be placed
// on it; after the pass it is written (ApplyBody), the arms riding its chest.
// Only the owning rig's absolute pose is written, as the arms' is: the shadow
// and the physics keep the game's.

namespace preyvr::dll {
using namespace ik;

namespace {

// The pinned joints by name; the pelvis is the joint spine1 hangs from.
constexpr const char* kPinnedNames[bodyrig::kPinnedCount] = {
    nullptr, "spine1_jnt", "spine2_jnt", "spine3_jnt", "spine4_jnt", "neck_jnt",
    "r_upperLeg_jnt", "r_lowerLeg_jnt", "r_foot_jnt", "l_upperLeg_jnt", "l_lowerLeg_jnt", "l_foot_jnt"};

// Each pinned joint hangs from the one before it in the chain the solve bends.
constexpr int kPinnedParent[][2] = {
    {bodyrig::kSpine2, bodyrig::kSpine1}, {bodyrig::kSpine3, bodyrig::kSpine2}, {bodyrig::kSpine4, bodyrig::kSpine3},
    {bodyrig::kNeck, bodyrig::kSpine4}, {bodyrig::kUpperLegR, bodyrig::kPelvis},
    {bodyrig::kLowerLegR, bodyrig::kUpperLegR}, {bodyrig::kFootR, bodyrig::kLowerLegR},
    {bodyrig::kUpperLegL, bodyrig::kPelvis}, {bodyrig::kLowerLegL, bodyrig::kUpperLegL},
    {bodyrig::kFootL, bodyrig::kLowerLegL}};

// **The waist's rest**, measured on the player's rig (PreyVR-84, 2026-10-25):
// spine2's local offset in the bind pose. The game's crouch moves it 32 cm
// (the kinked waist), the standing idle 4 cm; it is brought back to within
// kWaistSlack of the rest. Only on that rig: spine1's, spine3's, spine4's and
// the neck's local offsets, which nothing animates, must match these.
constexpr Vec3 kSpine2Rest{0.1255f, 0.0f, 0.0f};
constexpr float kWaistSlack = 0.05f;   // m
constexpr struct {
    int joint;
    Vec3 offset;
} kRigOffsets[] = {
    {bodyrig::kSpine1, {0.1101f, 0.0f, 0.0f}}, {bodyrig::kSpine3, {0.1456f, 0.0f, 0.0f}},
    {bodyrig::kSpine4, {0.1149f, 0.0f, 0.0f}}, {bodyrig::kNeck, {0.1069f, -0.0274f, 0.0f}}};
constexpr float kRigMatch = 0.002f;

// The chest turns at most this far onto the torso the shoulders face.
constexpr float kMaxTwistRad = 45.0f * kRadiansPerDegree;
// The head followed level while room-scale moves the capsule (m): out to what
// the spine's lean reaches, and given up only well beyond it.
constexpr float kRoomFollowFull = 1.0f, kRoomFollowNone = 1.5f;
// Hips and Legs Back (VR Options' hips_back), cm: the pin's hipsBack. 8 cm is
// the eyes from the head's pivot (armpose::kNeckFromEye), which a person's hips
// stand under; Prey's standing idle has them under the eyes.
constexpr unsigned kHipsBackMaxCm = 20;
std::atomic<unsigned> gHipsBackCm{8};

bodyrig::Joint ToJoint(const QuatT& a)
{
    return {Quaternion{a.q[0], a.q[1], a.q[2], a.q[3]}, Vec3{a.t[0], a.t[1], a.t[2]}};
}

int Mm(float metres) { return static_cast<int>(std::lround(metres * 1000.0f)); }

void StoreMax(std::atomic<int>& most, int value)
{
    int seen = most.load(std::memory_order_relaxed);
    while (value > seen && !most.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {}
}

} // namespace

namespace ik {

// Under gIkMutex, with the hand rig: the body pin's joints on this rig, and
// whether its waist can be straightened. Not ready unless every joint reads and
// hangs where the solve expects.
void BuildBodyRig(HandRig& rig, const std::uint8_t* skeleton, unsigned jointCount, std::uint8_t* relative,
                  unsigned poseCount, std::uintptr_t character)
{
    rig.bodyReady = rig.straighten = false;
    rig.posedCount = 0;
    rig.arm.fill(0);
    rig.camera = AttachmentJoint(character, "#camera");
    const unsigned count = std::min({jointCount, poseCount, kMaxJoints});
    const int n = static_cast<int>(count);
    std::array<int, kMaxJoints> parent{};
    const char* fault = count == 0 || !RigParents(skeleton, count, parent.data()) ? "parents" : nullptr;
    for (int k = bodyrig::kSpine1; k < bodyrig::kPinnedCount && fault == nullptr; ++k) {
        rig.pinned[k] = RigJointByName(skeleton, count, kPinnedNames[k]);
        if (rig.pinned[k] < 0) { fault = kPinnedNames[k]; }
    }
    if (fault == nullptr) {
        rig.pinned[bodyrig::kPelvis] = parent[static_cast<std::size_t>(rig.pinned[bodyrig::kSpine1])];
        if (rig.pinned[bodyrig::kPelvis] < 0) { fault = "pelvis"; }
        for (const auto& link : kPinnedParent) {
            if (fault == nullptr && parent[static_cast<std::size_t>(rig.pinned[link[0]])] != rig.pinned[link[1]]) {
                fault = kPinnedNames[link[0]];
            }
        }
    }
    rig.throat = RigJointByName(skeleton, count, "throat_jnt");
    const int clavicle[2] = {RigJointByName(skeleton, count, "r_clavicle_jnt"),
                             RigJointByName(skeleton, count, "l_clavicle_jnt")};
    rig.bodyHand[0] = RigJointByName(skeleton, count, "r_hand_jnt");
    rig.bodyHand[1] = RigJointByName(skeleton, count, "l_hand_jnt");
    const int targets = RigJointByName(skeleton, count, "spine_target_start");
    if (fault == nullptr && (rig.camera < 0 || rig.camera >= n || rig.throat < 0 || clavicle[0] < 0 || clavicle[1] < 0)) {
        fault = "joints";
    }
    // What the pin poses: the pelvis and all it carries, less the camera joint's
    // subtree (the game's eye), the arms below the collarbones (they ride the
    // chest, and the arm solve places them) and the hands' IK targets.
    if (fault == nullptr) {
        std::array<bool, kMaxJoints> body{}, camera{}, arms[2]{}, hands[2]{}, iks{};
        bodyrig::Subtree(parent.data(), n, rig.pinned[bodyrig::kPelvis], -1, body.data());
        bodyrig::Subtree(parent.data(), n, rig.camera, -1, camera.data());
        for (int s = 0; s < 2; ++s) {
            bodyrig::Subtree(parent.data(), n, clavicle[s], -1, arms[s].data());
            arms[s][static_cast<std::size_t>(clavicle[s])] = false;
            if (rig.bodyHand[s] >= 0) { bodyrig::Subtree(parent.data(), n, rig.bodyHand[s], -1, hands[s].data()); }
        }
        if (targets >= 0) { bodyrig::Subtree(parent.data(), n, targets, -1, iks.data()); }
        for (int k = 0; k < bodyrig::kPinnedCount; ++k) { rig.pinnedSlot[k] = -1; }
        for (int j = 0; j < n && fault == nullptr; ++j) {
            const auto at = static_cast<std::size_t>(j);
            for (int s = 0; s < 2; ++s) {
                if (arms[s][at]) { rig.arm[at] = static_cast<unsigned char>((s + 1) | (hands[s][at] ? kArmHand : 0)); }
            }
            if (!body[at] || camera[at] || arms[0][at] || arms[1][at] || iks[at]) { continue; }
            if (rig.posedCount >= kMaxBodyJoints) {
                fault = "size";
                break;
            }
            const int slot = rig.posedCount++;
            rig.posed[static_cast<std::size_t>(slot)] = static_cast<short>(j);
            int role = -1;
            for (int k = 0; k < bodyrig::kPinnedCount; ++k) {
                if (rig.pinned[k] == j) { role = k; }
            }
            rig.posedRole[static_cast<std::size_t>(slot)] = static_cast<signed char>(role);
            rig.posedParent[static_cast<std::size_t>(slot)] = -1;
            if (role >= 0) {
                rig.pinnedSlot[role] = slot;
                continue;
            }
            // A joint the pin carries hangs from one it posed before it.
            for (int p = 0; p < slot; ++p) {
                if (rig.posed[static_cast<std::size_t>(p)] == parent[at]) {
                    rig.posedParent[static_cast<std::size_t>(slot)] = static_cast<signed char>(p);
                }
            }
            if (rig.posedParent[static_cast<std::size_t>(slot)] < 0) { fault = "order"; }
        }
        for (int k = 0; k < bodyrig::kPinnedCount && fault == nullptr; ++k) {
            if (rig.pinnedSlot[k] < 0) { fault = "unposed"; }
        }
    }
    // The waist's rest holds on the rig it was measured on: matched once, the
    // skeleton stays matched.
    float off = -1.0f;
    int offJoint = -1;
    if (fault == nullptr) {
        for (const auto& known : kRigOffsets) {
            QuatT local{};
            const float d = ReadQuatT(relative, poseCount, rig.pinned[known.joint], local)
                ? Length(Sub(Vec3{local.t[0], local.t[1], local.t[2]}, known.offset)) : 1.0f;
            if (d > off) { off = d; offJoint = rig.pinned[known.joint]; }
        }
        const auto skeletonAt = reinterpret_cast<std::uintptr_t>(skeleton);
        if (off <= kRigMatch) { gWaistSkeleton = skeletonAt; }
        rig.straighten = gWaistSkeleton == skeletonAt;
    }
    rig.bodyReady = fault == nullptr;
    int arms = 0;
    for (unsigned j = 0; j < count; ++j) { arms += rig.arm[j] != 0 ? 1 : 0; }
    gBodyReady.store(rig.bodyReady, std::memory_order_relaxed);
    gBodyStraighten.store(rig.straighten, std::memory_order_relaxed);
    gBodyPosed.store(rig.bodyReady ? rig.posedCount : 0, std::memory_order_relaxed);
    char line[256];
    std::snprintf(line, sizeof(line),
        "result=%d detail=body_rig fault=%s pelvis=%d spine=%d,%d,%d,%d neck=%d legs=%d,%d,%d/%d,%d,%d camera=%d "
        "throat=%d posed=%d arms=%d straighten=%d",
        rig.bodyReady ? 0 : 1, fault != nullptr ? fault : "none", rig.pinned[bodyrig::kPelvis],
        rig.pinned[bodyrig::kSpine1], rig.pinned[bodyrig::kSpine2], rig.pinned[bodyrig::kSpine3],
        rig.pinned[bodyrig::kSpine4], rig.pinned[bodyrig::kNeck], rig.pinned[bodyrig::kUpperLegR],
        rig.pinned[bodyrig::kLowerLegR], rig.pinned[bodyrig::kFootR], rig.pinned[bodyrig::kUpperLegL],
        rig.pinned[bodyrig::kLowerLegL], rig.pinned[bodyrig::kFootL], rig.camera, rig.throat, rig.posedCount, arms,
        rig.straighten ? 1 : 0);
    // Every snap turn re-binds the rig: only a change is logged, with how far
    // the spine's offsets were from the measured rig's on that rebuild.
    if (gBodyRigLast != line) {
        gBodyRigLast = line;
        char offText[64];
        std::snprintf(offText, sizeof(offText), " rigOffMm=%.1f rigOffJoint=%d", off * 1000.0f, offJoint);
        Log(std::string(line) + offText);
    }
}

// Before the native pass, under gIkMutex: the posed body, from the pose as the
// animation left it and the player's head in the rig's model space (through the
// same anchor and location the hands use).
void PlanBody(const GameplayPoseFrame& frame, const animik::Location& location, std::uint8_t* relative,
              std::uint8_t* absolute, unsigned count, float shoulderYaw, BodyPlan& plan)
{
    plan.apply = false;
    const HandRig& rig = gHandRig;
    XrMap map{&frame, {}, location};
    if (!rig.bodyReady || rig.skeleton != gRigSkeleton.load() || !AnchorPoint(frame, kIkAnchor, map.anchor) ||
        !(location.s > 1e-4f)) {
        gBodyRefused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const float unit = 1.0f / location.s;   // model units per metre
    std::array<bodyrig::Joint, kMaxBodyJoints> old{};
    QuatT read{};
    for (int slot = 0; slot < rig.posedCount; ++slot) {
        if (!ReadQuatT(absolute, count, rig.posed[static_cast<std::size_t>(slot)], read)) {
            gBodyRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        old[static_cast<std::size_t>(slot)] = ToJoint(read);
    }
    QuatT camera{}, throat{};
    if (!ReadQuatT(absolute, count, rig.camera, camera) || !ReadQuatT(absolute, count, rig.throat, throat)) {
        gBodyRefused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    bodyrig::PinInput in{};
    for (int k = 0; k < bodyrig::kPinnedCount; ++k) { in.native[k] = old[static_cast<std::size_t>(rig.pinnedSlot[k])]; }
    // The rig's atlas: the game's eye less kNeckFromEye, in the eye's frame
    // (OpenXR's x, -z and y are its x, y and z).
    const Vec3& k = armpose::kNeckFromEye;
    const bodyrig::Joint eye = ToJoint(camera);
    const Vec3 atlas = Add(eye.t, Rotate(eye.q, Scale(Vec3{k.x, -k.z, k.y}, unit)));
    in.atlas = atlas;
    in.throat = ToJoint(throat).t;
    // The waist straightened: spine2 and all above it moved back to its rest.
    Vec3 waist{};
    QuatT spine2{};
    if (rig.straighten && ReadQuatT(relative, count, rig.pinned[bodyrig::kSpine2], spine2)) {
        waist = bodyrig::StraightenShift(in.native[bodyrig::kSpine1].q, Vec3{spine2.t[0], spine2.t[1], spine2.t[2]},
                                         kSpine2Rest, kWaistSlack * unit);
    }
    for (int j = bodyrig::kSpine2; j <= bodyrig::kNeck; ++j) { in.native[j].t = Add(in.native[j].t, waist); }
    in.atlas = Add(in.atlas, waist);
    in.throat = Add(in.throat, waist);
    // The player's head.
    const Pose& head = frame.tracking.head;
    const auto toward = [&](const Vec3& direction) {
        return Sub(map.ToModel(Add(head.position, Rotate(head.orientation, direction))), map.ToModel(head.position));
    };
    in.eye = map.ToModel(head.position);
    in.realAtlas = map.ToModel(Add(head.position, Rotate(head.orientation, k)));
    const Vec3 forward = toward(Vec3{0.0f, 0.0f, -1.0f});
    in.facing = bodyrig::FacingOf(forward, toward(Vec3{0.0f, 1.0f, 0.0f}));
    in.pitch = bodyrig::PitchOf(forward);
    // The chest turns from the game's onto the torso the shoulders face.
    const Vec3 torso = Sub(map.ToModel(Add(head.position, armpose::TorsoFromYaw(shoulderYaw).forward)), in.eye);
    const Vec3 chest = Rotate(in.native[bodyrig::kSpine4].q, Vec3{0.0f, 1.0f, 0.0f});
    in.twist = std::clamp(bodyrig::TurnAbout(chest, torso), -kMaxTwistRad, kMaxTwistRad);
    bodyrig::PinSettings s{};
    s.hipsBack = 0.01f * static_cast<float>(gHipsBackCm.load(std::memory_order_relaxed));
    // With room-scale following, a head away from the capsule is a lean in its
    // lean zone, or one the capsule could not follow (a desk, a wall): the
    // spine leans as far as it can rather than give up on it as a step away.
    if (RoomScaleActive()) {
        s.followFull = kRoomFollowFull;
        s.followNone = kRoomFollowNone;
    }
    s.hipsBack *= unit;
    s.throatBack *= unit;
    s.throatDown *= unit;
    s.legFold *= unit;
    s.followFull *= unit;
    s.followNone *= unit;
    s.sinkMax *= unit;
    const bodyrig::PinResult r = bodyrig::SolvePin(in, s);
    if (!r.valid) {
        gBodyRefused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // Each posed joint: a pinned one where the solve put it, the rest carried
    // with its parent from the pose as the animation left it.
    for (int slot = 0; slot < rig.posedCount; ++slot) {
        const auto at = static_cast<std::size_t>(slot);
        const int role = rig.posedRole[at];
        if (role >= 0) {
            plan.pose[at] = r.pose[role];
        } else {
            const auto up = static_cast<std::size_t>(rig.posedParent[at]);
            plan.pose[at] = bodyrig::Carry(old[up], plan.pose[up], old[at]);
        }
    }
    plan.count = rig.posedCount;
    const auto chest4 = static_cast<std::size_t>(rig.pinnedSlot[bodyrig::kSpine4]);
    plan.chestOld = old[chest4];
    plan.chestNew = plan.pose[chest4];
    plan.atlas = atlas;
    plan.chestTurn = bodyrig::TurnAbout(Vec3{0.0f, 1.0f, 0.0f}, chest);
    plan.apply = true;
    // How far the game's eye this frame's view was built on (the view less the
    // head offset it added) lies from the camera joint as this pose has it: the
    // anchor and the pose describe the same frame when it is near zero, and a
    // walk would show a stale anchor as about its step.
    Vec3 offset{};
    if (frame.cameraCentreValid && ViewPositionOffset(offset)) {
        const int gap = Mm(Length(Sub(Sub(frame.cameraCentre, offset), animik::ModelToWorld(location, eye.t))));
        gBodyEyeGapMm.store(gap, std::memory_order_relaxed);
        StoreMax(gBodyEyeGapMaxMm, gap);
    }
    gBodyFrames.fetch_add(1, std::memory_order_relaxed);
    if (r.folded) { gBodyFolded.fetch_add(1, std::memory_order_relaxed); }
    const float metres = location.s;
    gBodyDropMm.store(Mm(r.drop * metres), std::memory_order_relaxed);
    gBodyLeanDeci.store(static_cast<int>(std::lround(r.leanDeg * 10.0f)), std::memory_order_relaxed);
    gBodyTwistDeci.store(static_cast<int>(std::lround(in.twist * kDegreesPerRadian * 10.0f)), std::memory_order_relaxed);
    gBodyWaistMm.store(Mm(Length(waist) * metres), std::memory_order_relaxed);
    gBodyPushMm.store(Mm(r.throatPush * metres), std::memory_order_relaxed);
    gBodySinkMm.store(Mm(r.sink * metres), std::memory_order_relaxed);
    gBodyShortMm.store(Mm(r.shortfall * metres), std::memory_order_relaxed);
    StoreMax(gBodyPushMaxMm, Mm(r.throatPush * metres));
    StoreMax(gBodySinkMaxMm, Mm(r.sink * metres));
    StoreMax(gBodyShortMaxMm, Mm(r.shortfall * metres));
}

// After the native pass, before the fingers and the arms, under no IK lock: the
// posed body written, and the arms carried with its chest -- each hand moved
// with it but turned as the native pass left it, on the controller's rotation.
void ApplyBody(std::uint8_t* absolute, unsigned count, const HandRig& rig, const BodyPlan& plan)
{
    if (!plan.apply || rig.skeleton != gRigSkeleton.load() || plan.count != rig.posedCount) { return; }
    for (int slot = 0; slot < plan.count; ++slot) {
        const bodyrig::Joint& j = plan.pose[static_cast<std::size_t>(slot)];
        if (!WriteQuatT(absolute, count, rig.posed[static_cast<std::size_t>(slot)], j.q, j.t)) { return; }
    }
    const Quaternion turn = Multiply(plan.chestNew.q, Conjugate(plan.chestOld.q));
    Vec3 handMove[2]{};
    bool handRead[2]{};
    for (int s = 0; s < 2; ++s) {
        QuatT hand{};
        if (rig.bodyHand[s] >= 0 && ReadQuatT(absolute, count, rig.bodyHand[s], hand)) {
            const Vec3 at{hand.t[0], hand.t[1], hand.t[2]};
            handMove[s] = Sub(plan.Chest(at), at);
            handRead[s] = true;
        }
    }
    const unsigned joints = std::min(count, kMaxJoints);
    for (unsigned j = 0; j < joints; ++j) {
        const unsigned char arm = rig.arm[j];
        if (arm == 0) { continue; }
        const int s = (arm & 3) - 1;
        QuatT a{};
        if (!ReadQuatT(absolute, count, static_cast<int>(j), a)) { return; }
        const bodyrig::Joint was = ToJoint(a);
        if ((arm & kArmHand) != 0) {
            if (handRead[s]) { WriteQuatT(absolute, count, static_cast<int>(j), was.q, Add(was.t, handMove[s])); }
        } else {
            WriteQuatT(absolute, count, static_cast<int>(j), Multiply(turn, was.q), plan.Chest(was.t));
        }
    }
}

} // namespace ik

void SetBodyHipsBackCm(unsigned cm) { gHipsBackCm.store(std::min(cm, kHipsBackMaxCm), std::memory_order_relaxed); }

unsigned BodyHipsBackCm() { return gHipsBackCm.load(std::memory_order_relaxed); }

std::string BodyPinReport()
{
    const auto deci = [](int tenths) {
        return (tenths < 0 ? "-" : "") + std::to_string(std::abs(tenths) / 10) + "." + std::to_string(std::abs(tenths) % 10);
    };
    std::ostringstream out;
    out << " pinReady=" << (gBodyReady.load() ? 1 : 0) << " straighten=" << (gBodyStraighten.load() ? 1 : 0)
        << " hipsBackCm=" << gHipsBackCm.load()
        << " pinJoints=" << gBodyPosed.load() << " pinFrames=" << gBodyFrames.load()
        << " pinRefused=" << gBodyRefused.load() << " folded=" << gBodyFolded.load() << " dropMm=" << gBodyDropMm.load()
        << " leanDeg=" << deci(gBodyLeanDeci.load()) << " twistDeg=" << deci(gBodyTwistDeci.load())
        << " waistMm=" << gBodyWaistMm.load() << " throatPushMm=" << gBodyPushMm.load()
        << " sinkMm=" << gBodySinkMm.load() << " shortMm=" << gBodyShortMm.load()
        << " throatPushMaxMm=" << gBodyPushMaxMm.exchange(0) << " sinkMaxMm=" << gBodySinkMaxMm.exchange(0)
        << " shortMaxMm=" << gBodyShortMaxMm.exchange(0) << " eyeGapMm=" << gBodyEyeGapMm.load()
        << " eyeGapMaxMm=" << gBodyEyeGapMaxMm.exchange(0);
    return out.str();
}

} // namespace preyvr::dll
