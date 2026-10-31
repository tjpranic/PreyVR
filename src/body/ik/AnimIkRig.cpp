#include "body/ik/AnimIkTakeover.h"
#include "body/ik/AnimIkState.h"

// SEH-guarded reads and writes of the character's pose and skeleton, the rig's
// identification (its IK targets and limbs), and the hand rig: the hand's joints
// and frames on the identified rig (and, from AnimIkBody.cpp, the body pin's).

namespace preyvr::dll {
using namespace ik;

namespace {

// Bounded string compare against engine memory.
bool NameEquals(const char* name, const char* expected)
{
    __try {
        if (name == nullptr) { return false; }
        for (unsigned int i = 0; i < 64; ++i) {
            if (name[i] != expected[i]) { return false; }
            if (expected[i] == '\0') { return true; }
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int JointIndexByName(const std::uint8_t* skeleton, unsigned int count, const char* expected)
{
    for (unsigned int i = 0; i < count; ++i) {
        if (NameEquals(JointName(skeleton, static_cast<int>(i)), expected)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int ParentOf(const std::uint8_t* skeleton, int index)
{
    void* table = nullptr;
    int field = -1;
    if (!ReadPointer(skeleton + kSkelJoints, &table) || table == nullptr ||
        !ReadInt(static_cast<std::uint8_t*>(table) + static_cast<std::size_t>(index) * kJointStride + 0x18, &field)) {
        return -2;
    }
    // Low 16 bits: the parent (0xFFFF = none); high 16 bits: the depth.
    const int parent = field & 0xFFFF;
    return parent == 0xFFFF ? -1 : parent;
}

} // namespace

namespace ik {

// --- raw reads, SEH-guarded, POD only --------------------------------------

unsigned int DynArrayCount(const void* data)
{
    __try {
        if (data == nullptr) { return 0; }
        return *reinterpret_cast<const unsigned int*>(
                   reinterpret_cast<const std::uint8_t*>(data) - 4) & 0x7FFFFFFFu;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool ReadPointer(const void* at, void** out)
{
    return SafeRead(at, *out);
}

bool ReadInt(const void* at, int* out)
{
    return SafeRead(at, *out);
}

bool ReadFloats(const void* at, float* out, unsigned int count)
{
    return SafeCopy(out, at, count * sizeof(float));
}

// POD-only, so the SEH guard is legal: the names are engine memory and may be
// anything at all.
void FormatAdikLine(char* line, std::size_t capacity, unsigned int index, int target,
                    const char* targetName, int weight, const char* weightName)
{
    __try {
        std::snprintf(line, capacity, "adik[%u] target=%d(%s) weight=%d(%s)", index, target,
                      targetName ? targetName : "?", weight, weightName ? weightName : "?");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(line, capacity, "adik[%u] unreadable", index);
    }
}

const char* JointName(const std::uint8_t* skeleton, int index)
{
    void* table = nullptr;
    if (!ReadPointer(skeleton + kSkelJoints, &table) || table == nullptr) { return nullptr; }
    void* name = nullptr;
    if (!ReadPointer(reinterpret_cast<std::uint8_t*>(table) + static_cast<std::size_t>(index) * kJointStride,
                     &name)) {
        return nullptr;
    }
    return static_cast<const char*>(name);
}

// --- rig identification -----------------------------------------------------
//
// The signature identifies an asset, not a live owner. The caller first checks
// the current selected weapon's attachment-manager owner; validate indices anew
// on each matching callback, even when a skeleton pointer is reused.

bool TryGetFreeArms(std::uintptr_t player, EquippedRig& out)
{
    const auto base = PreyBase();
    if (!base || !player) { return false; }
    __try {
        if (*reinterpret_cast<std::uintptr_t*>(player + kPlayerInput) != base + kPlayerInputVtableRva ||
            *reinterpret_cast<int*>(player + kPlayerInput + kInputCinematic) != 0) { return false; }
        const auto animated = *reinterpret_cast<std::uintptr_t*>(player + kPlayerAnimatedCharacter);
        if (!animated) { return false; }
        const auto character = *reinterpret_cast<std::uintptr_t*>(animated + kAnimatedCharacterInstance);
        if (!character || *reinterpret_cast<std::uintptr_t*>(character) != base + kArmsCharacterVtableRva) { return false; }
        out = {};
        out.character = character;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool IdentifyRig(std::uint8_t* character)
{
    void* skeletonPtr = nullptr;
    if (!ReadPointer(character + kCharSkeleton, &skeletonPtr) || skeletonPtr == nullptr) {
        return false;
    }
    auto* const skeleton = static_cast<std::uint8_t*>(skeletonPtr);
    void* joints = nullptr;
    if (!ReadPointer(skeleton + kSkelJoints, &joints)) { return false; }
    const unsigned int jointCount = DynArrayCount(joints);
    if (jointCount != gSignatureJoints.load(std::memory_order_relaxed) || jointCount > kMaxJoints) {
        return false;
    }
    void* adik = nullptr;
    if (!ReadPointer(skeleton + kSkelAdik, &adik) || adik == nullptr) { return false; }
    const unsigned int adikCount = DynArrayCount(adik);
    if (adikCount == 0 || adikCount > 64) { return false; }

    int target[2] = {-1, -1}, weight[2] = {-1, -1};
    for (unsigned int i = 0; i < adikCount; ++i) {
        auto* const entry = static_cast<std::uint8_t*>(adik) + i * kAdikStride;
        void* name = nullptr;
        if (!ReadPointer(entry + kAdikTargetName, &name)) { continue; }
        int t = -1, w = -1;
        if (!ReadInt(entry + kAdikTarget, &t) || !ReadInt(entry + kAdikWeight, &w)) { continue; }
        if (NameEquals(static_cast<const char*>(name), "r_hand_spine_target")) { target[0] = t; weight[0] = w; }
        if (NameEquals(static_cast<const char*>(name), "l_hand_spine_target")) { target[1] = t; weight[1] = w; }
    }
    if (target[0] < 0 && target[1] < 0) {
        return false;   // the signature is the ADIK target, not the joint count alone
    }
    const int hand[2] = {JointIndexByName(skeleton, jointCount, "r_hand_jnt"),
                         JointIndexByName(skeleton, jointCount, "l_hand_jnt")};

    // The limb the engine will solve: matched by its chain's END joint, which is
    // the fabrication-free way to know the arm is defined at all.
    int upper[2] = {-1, -1}, mid[2] = {-1, -1}, end[2] = {-1, -1};
    unsigned int tag[2] = {0, 0};
    void* limbs = nullptr;
    if (ReadPointer(skeleton + kSkelLimbs, &limbs) && limbs != nullptr) {
        const unsigned int limbCount = DynArrayCount(limbs);
        for (unsigned int i = 0; i < limbCount && i < 32; ++i) {
            auto* const limb = static_cast<std::uint8_t*>(limbs) + i * kLimbStride;
            void* chain = nullptr;
            if (!ReadPointer(limb + kLimbChain, &chain) || chain == nullptr || DynArrayCount(chain) < 4 || DynArrayCount(chain) > kMaxJoints) { continue; }
            int c1 = -1, c2 = -1, c3 = -1, t = 0;
            if (!ReadInt(static_cast<std::uint8_t*>(chain) + 0x10, &c1) ||
                !ReadInt(static_cast<std::uint8_t*>(chain) + 0x20, &c2) ||
                !ReadInt(static_cast<std::uint8_t*>(chain) + 0x30, &c3) ||
                !ReadInt(limb + kLimbTag, &t)) {
                continue;
            }
            for (unsigned int h = 0; h < 2; ++h) {
                if (hand[h] >= 0 && c3 == hand[h] && animik::ValidJoint(c1, jointCount) &&
                    animik::ValidJoint(c2, jointCount) && animik::ValidJoint(c3, jointCount)) {
                    upper[h] = c1; mid[h] = c2; end[h] = c3; tag[h] = static_cast<unsigned int>(t);
                }
            }
        }
    }
    for (unsigned int h = 0; h < 2; ++h) {
        gTargetJoint[h].store(target[h], std::memory_order_relaxed);
        gWeightJoint[h].store(weight[h], std::memory_order_relaxed);
        gHandJoint[h].store(hand[h], std::memory_order_relaxed);
        gLimbUpper[h].store(upper[h], std::memory_order_relaxed);
        gLimbMid[h].store(mid[h], std::memory_order_relaxed);
        gLimbEnd[h].store(end[h], std::memory_order_relaxed);
        gLimbTagValue[h].store(tag[h], std::memory_order_relaxed);
    }
    const bool changed = gRigSkeleton.load() != reinterpret_cast<std::uintptr_t>(skeleton);
    gRigJoints.store(jointCount, std::memory_order_relaxed);
    gRigSkeleton.store(reinterpret_cast<std::uintptr_t>(skeleton), std::memory_order_release);
    if (changed) Log("result=0 detail=rig_identified skeleton=0x" +
        [&] { char b[32]; std::snprintf(b, sizeof(b), "%llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(skeleton))); return std::string(b); }() +
        " joints=" + std::to_string(jointCount) + " adik=" + std::to_string(adikCount) +
        " rTarget=" + std::to_string(target[0]) + " rWeight=" + std::to_string(weight[0]) +
        " rHand=" + std::to_string(hand[0]) + " rLimb=" + std::to_string(upper[0]) + "/" +
        std::to_string(mid[0]) + "/" + std::to_string(end[0]) +
        " lTarget=" + std::to_string(target[1]) + " lWeight=" + std::to_string(weight[1]) +
        " lHand=" + std::to_string(hand[1]) + " lLimb=" + std::to_string(upper[1]) + "/" +
        std::to_string(mid[1]) + "/" + std::to_string(end[1]));
    return true;
}

bool ReadQuatT(std::uint8_t* array, unsigned count, int index, QuatT& out)
{
    if (count>kMaxJoints || !animik::ValidJoint(index, count) ||
        !ReadFloats(array + static_cast<std::size_t>(index) * kQuatTStride, &out.q[0], 7)) { return false; }
    return animik::ValidLocation({{out.q[0], out.q[1], out.q[2], out.q[3]},
                                 {out.t[0], out.t[1], out.t[2]}, 1.0f});
}

// The animation job owns these arrays. Restore a partially attempted edit on
// an access fault, then disarm; do not report a partial target as a solved hand.
bool WriteGoal(std::uint8_t* target, float* weight, const Vec3& position,
               const Quaternion& rotation, bool writeRotation)
{
    QuatT backup{};
    float oldWeight = 0;
    bool haveBackup = false;
    __try {
        std::memcpy(&backup, target, sizeof(backup));
        oldWeight = *weight;
        haveBackup = true;
        std::memcpy(target + 0x10, &position, sizeof(position));
        if (writeRotation) { std::memcpy(target, &rotation, sizeof(rotation)); }
        *weight = 1.0f;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (haveBackup) {
            __try { std::memcpy(target, &backup, sizeof(backup)); *weight = oldWeight; }
            __except (EXCEPTION_EXECUTE_HANDLER) { /* destroyed storage: cannot restore */ }
        }
        return false;
    }
}

bool ReadAlignmentMemory(void*, std::uintptr_t address, void* output, std::size_t size)
{
    return SafeCopy(output, reinterpret_cast<const void*>(address), size);
}

// Under gIkMutex. Names identify the joints; the joint table's parent field
// must agree with the expected chain or the hand is left alone.
void BuildHandRig(std::uint8_t* skeleton, unsigned jointCount, std::uint8_t* relative, unsigned poseCount,
                  std::uintptr_t character)
{
    HandRig rig{};
    rig.skeleton = reinterpret_cast<std::uintptr_t>(skeleton);
    for (int s = 0; s < 2; ++s) {
        const auto side = s == 1 ? handpose::Side::left : handpose::Side::right;
        rig.wrist[s] = JointIndexByName(skeleton, jointCount, handpose::WristName(side));
        bool ok = rig.wrist[s] >= 0;
        for (int j = 0; ok && j < handpose::kJointCount; ++j) {
            rig.joint[s][j] = JointIndexByName(skeleton, jointCount, handpose::JointName(side, j));
            const int role = handpose::ParentRole(j);
            const int expected = role < 0 ? rig.wrist[s] : rig.joint[s][role];
            ok = rig.joint[s][j] >= 0 && static_cast<unsigned>(rig.joint[s][j]) < poseCount &&
                 ParentOf(skeleton, rig.joint[s][j]) == expected;
        }
        if (!ok) { continue; }
        const int bases[4] = {handpose::kIndexBase, handpose::kMiddleBase, handpose::kRingBase, handpose::kPinkyBase};
        Vec3 baseOffset[4], firstOffset[4];
        Quaternion baseRotation[4];
        for (int f = 0; f < 4 && ok; ++f) {
            QuatT base{}, first{};
            ok = ReadQuatT(relative, poseCount, rig.joint[s][bases[f]], base) &&
                 ReadQuatT(relative, poseCount, rig.joint[s][bases[f] + 1], first);
            baseOffset[f] = {base.t[0], base.t[1], base.t[2]};
            firstOffset[f] = {first.t[0], first.t[1], first.t[2]};
            baseRotation[f] = handpose::RelaxedRelative(side, bases[f], 1.0f);
        }
        rig.valid[s] = ok && handpose::MeasureHandFrame(side, baseOffset, baseRotation, firstOffset,
                                                         rig.frame[s], rig.knuckle[s]);
    }
    const auto armJoint = [&](const char* name) {
        const int j = JointIndexByName(skeleton, jointCount, name);
        return j >= 0 && static_cast<unsigned>(j) < poseCount ? j : -1;
    };
    rig.clavicle[0] = armJoint("r_clavicle_jnt"); rig.clavicle[1] = armJoint("l_clavicle_jnt");
    rig.upperArm[0] = armJoint("r_upperArm_jnt"); rig.upperArm[1] = armJoint("l_upperArm_jnt");
    rig.lowerArm[0] = armJoint("r_lowerArm_jnt"); rig.lowerArm[1] = armJoint("l_lowerArm_jnt");
    rig.neck = armJoint("neck_jnt"); rig.head = armJoint("head_jnt"); rig.headEnd = armJoint("headEnd_jnt");
    rig.handProp[0] = armJoint("r_handProp_jnt"); rig.handProp[1] = armJoint("l_handProp_jnt");
    for (int s = 0; s < 2; ++s) {
        const std::string p = s == 0 ? "r_" : "l_";
        rig.upperChild[s][0] = armJoint((p + "upperArmTop_jnt").c_str());
        rig.upperChild[s][1] = armJoint((p + "upperArmTwist_jnt").c_str());
        rig.upperChild[s][2] = armJoint((p + "upperArmMuscle_jnt").c_str());
        rig.lowerTwist[s] = armJoint((p + "lowerArmTwist_jnt").c_str());
    }
    BuildBodyRig(rig, skeleton, jointCount, relative, poseCount, character);
    gHandRig = rig;
    char line[256];
    std::snprintf(line, sizeof(line),
        "result=0 detail=hand_rig right=%d left=%d knuckleR=%.4f knuckleL=%.4f "
        "lenL=%.3f,%.3f,%.3f palmL=%.3f,%.3f,%.3f",
        rig.valid[0] ? 1 : 0, rig.valid[1] ? 1 : 0, rig.knuckle[0], rig.knuckle[1],
        rig.frame[1].length.x, rig.frame[1].length.y, rig.frame[1].length.z,
        rig.frame[1].palm.x, rig.frame[1].palm.y, rig.frame[1].palm.z);
    Log(line);
}

bool WriteFloatsGuarded(std::uint8_t* at, const float* values, unsigned count)
{
    return SafeCopy(at, values, count * sizeof(float));
}

// After the native pass: the arm from the player's shoulder (see ArmPlan).
// Every write is to the absolute pose, which is what the engine skins, and
// each joint the arm carries keeps its offset from its parent as the pass left
// it -- lengthened along the bone with the segment.
bool WriteQuatT(std::uint8_t* absolute, unsigned count, int index, const Quaternion& q, const Vec3& t)
{
    if (index < 0 || static_cast<unsigned>(index) >= count) { return false; }
    const Quaternion n = Normalize(q);
    const float v[7] = {n.x, n.y, n.z, n.w, t.x, t.y, t.z};
    return WriteFloatsGuarded(absolute + static_cast<std::size_t>(index) * kQuatTStride, v, 7);
}

int RigJointByName(const std::uint8_t* skeleton, unsigned count, const char* name)
{
    return JointIndexByName(skeleton, count, name);
}

bool RigParents(const std::uint8_t* skeleton, unsigned count, int* parent)
{
    for (unsigned j = 0; j < count; ++j) {
        parent[j] = ParentOf(skeleton, static_cast<int>(j));
        if (parent[j] < -1 || parent[j] >= static_cast<int>(count)) { return false; }
    }
    return true;
}

} // namespace ik

} // namespace preyvr::dll
