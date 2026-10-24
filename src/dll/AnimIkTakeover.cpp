#include "AnimIkTakeover.h"
#include "AnimIkState.h"

namespace preyvr::dll {
using namespace ik;

namespace {

void __fastcall ProcessAdikWithTakeover(void* character, void* params)
{
    gCalls.fetch_add(1, std::memory_order_relaxed);
    const unsigned int mode = gMode.load(std::memory_order_acquire);
    if (mode == 0) {
        const auto original = gOriginal.load(std::memory_order_acquire);
        if (original) { original(character, params); }
        return;
    }
    // Only the owning character needs the IK state. Every other character's
    // animation job used to take the same try-lock and win it against the
    // owner -- measured live 2026-09-07: ikBusy ~105/s while the owner matched
    // ~93/s on a ~132 fps game, a third of its frames skipped, which a wearer
    // would see as the hand flickering between goal and animation. Non-owners
    // pass straight through, except while the owner is unknown or the equipped
    // weapon has changed since it was bound.
    {
        const auto knownOwner = gOwnerCharacter.load(std::memory_order_acquire);
        if (knownOwner != 0 && reinterpret_cast<std::uintptr_t>(character) != knownOwner &&
            WeaponEquipGeneration() == gOwnerGeneration.load(std::memory_order_acquire)) {
            const auto original = gOriginal.load(std::memory_order_acquire);
            if (original) { original(character, params); }
            return;
        }
    }
    std::unique_lock stateLock(gIkMutex, std::try_to_lock);
    GameplayPoseFrame frame{};
    EquippedRig owner{};
    const bool haveFrame = stateLock.owns_lock() && TryGetGameplayPoseFrame(frame, gDrive.load());
    bool haveOwner = haveFrame && TryGetEquippedRig(frame.player, owner);
    // arms.free: no weapon drawn -> the player's own arms. A generation of its
    // own makes calibration and the hand rig rebind on every switch.
    bool free = false;
    if (haveFrame && !haveOwner && gFreeArms.load(std::memory_order_acquire) != 0 &&
        TryGetFreeArms(frame.player, owner)) {
        owner.generation = kFreeGenerationBit | WeaponEquipGeneration();
        haveOwner = free = true;
    }
    if (stateLock.owns_lock()) { gFreeBound.store(free, std::memory_order_relaxed); }
    if (stateLock.owns_lock() && gAlignWeapon) {
        gAlignStatus = weaponrig::Status::noSample;
        gAlignBasis = {};
        gAlignGeneration = haveOwner ? owner.generation : 0;
        gAlignSequence = haveOwner ? frame.tracking.sequence : 0;
    }
    if (mode != 0 && !stateLock.owns_lock()) { gBusy.fetch_add(1); }
    if (mode != 0 && stateLock.owns_lock() && !haveOwner) { gNoOwner.fetch_add(1); }
    if (haveOwner && gCalibration.Bind(owner.generation, frame.referenceGeneration, frame.tracking.epoch)) {
        for (auto& flag : gCalibrated) { flag.store(false); }
        gRigSkeleton.store(0);
        gHandRig.skeleton = 0;   // re-identify the hand's joints with the rig
        gOwnerGeneration.store(owner.generation);
        gOwnerCharacter.store(owner.character);
    }
    // The other characters' fast pass-through compares this with the equip
    // generation; the free owner holds until the next equip bumps it.
    if (free) { gOwnerGeneration.store(WeaponEquipGeneration()); }
    // The owning rig's pose arrays when this callback drove it, for the work
    // that has to follow the native pass (finger propagation, measurement).
    FingerPlan fingerPlan{};
    FingerPlan rightFingers{};
    ArmPlan armPlan[2]{};
    TorsoLiftPlan liftPlan{};
    PoseSettings ownedSettings{};
    std::uint8_t* ownedRelative = nullptr;
    std::uint8_t* ownedAbsolute = nullptr;
    unsigned ownedCount = 0;
    animik::Location ownedLocation{};
    HandRig ownedHandRig{};
    if (mode != 0 && haveOwner && owner.character == reinterpret_cast<std::uintptr_t>(character) && params != nullptr) {
        auto* const ch = static_cast<std::uint8_t*>(character);
        if (IdentifyRig(ch)) {
            gMatched.fetch_add(1, std::memory_order_relaxed);
            NoteBodyFollowIk();
            gCalibration.Tick();   // settles an auto re-take past the equip animation
            int gate = 0;
            if (ReadInt(ch + kCharAnimPlaying, &gate)) {
                gGate.store(static_cast<unsigned int>(gate), std::memory_order_relaxed);
            }
            const std::uintptr_t preyDll = PreyBase();
            int cvar = -1;
            if (preyDll != 0 &&
                ReadInt(reinterpret_cast<std::uint8_t*>(preyDll) + kUseAdikCvarRva, &cvar)) {
                gCvar.store(cvar, std::memory_order_relaxed);
            }
            auto* const p = static_cast<std::uint8_t*>(params);
            float loc[8] = {0};
            animik::Location location{};
            bool validLocation = false;
            if (ReadFloats(p + kParamsLocQ, &loc[0], 4) && ReadFloats(p + kParamsLocT, &loc[4], 3) &&
                ReadFloats(p + kParamsLocS, &loc[7], 1)) {
                location.q = Quaternion{loc[0], loc[1], loc[2], loc[3]};
                location.t = Vec3{loc[4], loc[5], loc[6]};
                location.s = loc[7];
                validLocation = animik::ValidLocation(location);
            }
            if (validLocation) {
                gLocMm[0].store(static_cast<int>(loc[4] * 1000.0f), std::memory_order_relaxed);
                gLocMm[1].store(static_cast<int>(loc[5] * 1000.0f), std::memory_order_relaxed);
                gLocMm[2].store(static_cast<int>(loc[6] * 1000.0f), std::memory_order_relaxed);
                gLocYawMilli.store(static_cast<int>(animik::YawOf(location.q) * 57295.78f),
                                   std::memory_order_relaxed);
            }
            if (free) {
                gFreeFrames.fetch_add(1, std::memory_order_relaxed);
                if (gate == 0) { gFreeGateZero.fetch_add(1, std::memory_order_relaxed); }
            }
            if (mode == 2 && validLocation && (gate != 0 || free) && cvar != 0 && cvar != -1) {
                void* pose = nullptr;
                void* relative = nullptr;
                void* absolute = nullptr;
                int poseCount=0;
                if (ReadPointer(p + kParamsPose, &pose) && pose != nullptr &&
                    ReadInt(static_cast<std::uint8_t*>(pose)+8,&poseCount) && poseCount>0 && poseCount<=kMaxJoints &&
                    ReadPointer(static_cast<std::uint8_t*>(pose) + kPoseRelative, &relative) &&
                    ReadPointer(static_cast<std::uint8_t*>(pose) + kPoseAbsolute, &absolute) &&
                    relative != nullptr && absolute != nullptr) {
                    const unsigned int hands = gHands.load(std::memory_order_relaxed);
                    const PoseSettings poseSettings = ReadPoseSettings();
                    const std::uintptr_t skeleton = gRigSkeleton.load();
                    if (skeleton != 0 && gHandRig.skeleton != skeleton) {
                        BuildHandRig(reinterpret_cast<std::uint8_t*>(skeleton), gRigJoints.load(),
                                     static_cast<std::uint8_t*>(relative), static_cast<unsigned>(poseCount),
                                     owner.character);
                    }
                    PrepareArms(frame, location, static_cast<std::uint8_t*>(absolute),
                                static_cast<unsigned>(poseCount), poseSettings, armPlan, liftPlan);
                    armPlan[0].forceRotation = armPlan[1].forceRotation = free;
                    // The hand that holds a carried object closes around it.
                    {
                        const std::uint64_t now = preyvr::timing::MonotonicNanoseconds();
                        const float dt = gFreeFistNs != 0 && now > gFreeFistNs
                            ? std::min(static_cast<float>(now - gFreeFistNs) * 1e-9f, 0.1f) : 0.0f;
                        gFreeFistNs = now;
                        for (int s = 0; s < 2; ++s) {
                            const float want = free && s == 1 && CarryHoldingInHand() ? 1.0f : 0.0f;
                            gFreeFist[s] += (want - gFreeFist[s]) * ExpAlpha(dt, 14.0f);
                            gFreeFistMilli[s].store(static_cast<int>(gFreeFist[s] * 1000.0f), std::memory_order_relaxed);
                        }
                        // Each free hand's fingers from its controller, eased
                        // over ~55 ms so a touch sensor's edge is a movement.
                        const bool follow = gFingerCurl.load(std::memory_order_relaxed) != 0;
                        for (int s = 0; s < 2; ++s) {
                            const auto& held = frame.tracking.hands[s == 1 ? 0 : 1];
                            handpose::FingerShape want{};
                            if (follow) {
                                want = handpose::ShapeFromController(held.triggerValue, held.indexTouch,
                                                                     held.squeezeValue, held.thumbTouch, poseSettings.curl);
                            } else {
                                for (float& curl : want.curl) { curl = poseSettings.curl; }
                            }
                            const float k = gFingerShapeValid[s] ? ExpAlpha(dt, 18.0f) : 1.0f;
                            for (int f = 0; f < handpose::kFingerCount; ++f) {
                                gFingerShape[s].curl[f] += (want.curl[f] - gFingerShape[s].curl[f]) * k;
                                gFingerShape[s].fist[f] += (want.fist[f] - gFingerShape[s].fist[f]) * k;
                            }
                            gFingerShapeValid[s] = true;
                        }
                    }
                    const OwnedPose owned{static_cast<std::uint8_t*>(relative), static_cast<std::uint8_t*>(absolute),
                                          static_cast<unsigned>(poseCount), location};
                    PrimaryHand primary{};
                    if (hands & 1u) {
                        DriveHand(0, owned, frame, owner, poseSettings, &primary, nullptr,
                                  free ? &rightFingers : nullptr, &armPlan[0]);
                    }
                    if ((hands & 2u) && gMode.load() == 2) {
                        DriveHand(1, owned, frame, owner, poseSettings, nullptr, &primary, &fingerPlan, &armPlan[1]);
                    }
                    if (free) {
                        // From a weapon's grip to the open hand over 150 ms, not in one frame.
                        const std::uint64_t now = preyvr::timing::MonotonicNanoseconds();
                        if (gFreeSinceNs == 0) { gFreeSinceNs = now; }
                        const float ramp = std::clamp(static_cast<float>(now - gFreeSinceNs) * 1e-9f / 0.15f, 0.0f, 1.0f);
                        fingerPlan.weight *= ramp;
                        rightFingers.weight *= ramp;
                    } else {
                        gFreeSinceNs = 0;
                    }
                    if (fingerPlan.apply && gHandRig.valid[1]) {
                        fingerPlan.apply = ApplyRelaxedFingers(static_cast<std::uint8_t*>(relative),
                            static_cast<unsigned>(poseCount), fingerPlan.weight, gFingerShape[1], 1, gFreeFist[1]);
                    }
                    if (rightFingers.apply && gHandRig.valid[0]) {
                        rightFingers.apply = ApplyRelaxedFingers(static_cast<std::uint8_t*>(relative),
                            static_cast<unsigned>(poseCount), rightFingers.weight, gFingerShape[0], 0, gFreeFist[0]);
                    }
                    ownedRelative = static_cast<std::uint8_t*>(relative);
                    ownedAbsolute = static_cast<std::uint8_t*>(absolute);
                    ownedCount = static_cast<unsigned>(poseCount);
                    ownedLocation = location;
                    ownedHandRig = gHandRig;
                    ownedSettings = poseSettings;
                }
            }
        }
    }
    // ik.skel: the pose arrays of the owning rig, captured around the native pass.
    std::uint8_t* dumpRelative = nullptr;
    std::uint8_t* dumpAbsolute = nullptr;
    unsigned dumpCount = 0;
    static std::uint8_t dumpBefore[kMaxJoints * kQuatTStride];
    if (gSkelDump.load() == 1 && haveOwner && owner.character == reinterpret_cast<std::uintptr_t>(character) &&
        params != nullptr) {
        void* pose = nullptr;
        void* rel = nullptr;
        void* abs = nullptr;
        int n = 0;
        auto* const p = static_cast<std::uint8_t*>(params);
        if (ReadPointer(p + kParamsPose, &pose) && pose != nullptr &&
            ReadInt(static_cast<std::uint8_t*>(pose) + 8, &n) && n > 0 && n <= static_cast<int>(kMaxJoints) &&
            ReadPointer(static_cast<std::uint8_t*>(pose) + kPoseRelative, &rel) &&
            ReadPointer(static_cast<std::uint8_t*>(pose) + kPoseAbsolute, &abs) && rel && abs &&
            ReadFloats(abs, reinterpret_cast<float*>(dumpBefore), static_cast<unsigned>(n) * 7)) {
            dumpRelative = static_cast<std::uint8_t*>(rel);
            dumpAbsolute = static_cast<std::uint8_t*>(abs);
            dumpCount = static_cast<unsigned>(n);
        }
    }
    if (stateLock.owns_lock()) { stateLock.unlock(); }
    const ProcessAdikFn original = gOriginal.load(std::memory_order_acquire);
    if (original != nullptr) {
        original(character, params);
    }
    if (ownedAbsolute != nullptr) {
        // First the torso, so the fingers and the arms below are placed on it.
        ApplyTorsoLift(ownedAbsolute, ownedCount, ownedHandRig, liftPlan);
        if (fingerPlan.apply) { PropagateFingers(ownedRelative, ownedAbsolute, ownedCount, ownedHandRig); }
        if (rightFingers.apply) { PropagateFingers(ownedRelative, ownedAbsolute, ownedCount, ownedHandRig, 0); }
        SolveArms(frame, ownedAbsolute, ownedCount, ownedLocation, ownedHandRig, armPlan, ownedSettings);
        CaptureMarks(frame, ownedAbsolute, ownedCount, ownedLocation, ownedHandRig, fingerPlan);
    }
    int expected = 1;
    if (dumpCount != 0 && gSkelDump.compare_exchange_strong(expected, 0)) {
        DumpSkeletonPose(static_cast<std::uint8_t*>(character), dumpRelative, dumpBefore, dumpAbsolute, dumpCount);
    }
}

bool Install()
{
    if (gInstalled.load(std::memory_order_acquire)) { return true; }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        Log("result=unavailable detail=no_preydll");
        return false;
    }
    const auto target = reinterpret_cast<std::uintptr_t>(preyDll) + kProcessAdikRva;
    const engine::Landmark* adik = engine::FindLandmark("anim.process_adik");
    if (adik == nullptr || adik->rva != kProcessAdikRva ||
        std::memcmp(reinterpret_cast<const void*>(target), adik->expected.data(), adik->expected.size()) != 0) {
        Log("result=unavailable detail=adik_prologue_mismatch");
        return false;
    }
    gTarget = reinterpret_cast<void*>(target);
    const auto installed = hook::Install(gTarget, reinterpret_cast<void*>(&ProcessAdikWithTakeover), gOriginal);
    if (!installed) {
        Log(std::string("result=failed detail=") + hook::StepName(installed.step));
        return false;
    }
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=hook_installed target=ProcessAnimationDrivenIK rva=0x877B50");
    return true;
}

} // namespace

DWORD SetAnimIkMode(unsigned int mode)
{
    if (mode > 2u) { return 1; }
    if (mode != 0u && (!EnsureGameplayPoseObservation() ||
        SetWeaponAttachmentObserving(1) != 0 || !Install())) { return 2; }
    std::lock_guard stateLock(gIkMutex);
    if (mode == 0) {
        gCalibration = {};
        for (auto& flag : gCalibrated) { flag.store(false); }
        gAlignStatus = gAlignWeapon ? weaponrig::Status::noSample : weaponrig::Status::disabled;
        gAlignBasis = {};
        gAlignGeneration = gAlignSequence = 0;
    }
    gMode.store(mode, std::memory_order_release);
    Log("result=0 detail=mode value=" + std::to_string(mode));
    return 0;
}

DWORD SetAnimIkTestOffsetMillimetres(int x, int y, int z)
{
    if (x < -2000 || x > 2000 || y < -2000 || y > 2000 || z < -2000 || z > 2000) { return 1; }
    std::lock_guard stateLock(gIkMutex);
    gTestMm[0].store(x, std::memory_order_relaxed);
    gTestMm[1].store(y, std::memory_order_relaxed);
    gTestMm[2].store(z, std::memory_order_relaxed);
    Log("result=0 detail=test_offset x=" + std::to_string(x) + " y=" + std::to_string(y) + " z=" + std::to_string(z));
    return 0;
}

DWORD SetAnimIkControllerDrive(unsigned int enabled)
{
    std::lock_guard stateLock(gIkMutex);
    if (!enabled) {
        gCalibration = {};
        for (auto& flag : gCalibrated) { flag.store(false); }
        gAlignStatus = gAlignWeapon ? weaponrig::Status::noSample : weaponrig::Status::disabled;
        gAlignBasis = {};
        gAlignGeneration = gAlignSequence = 0;
    }
    gDrive.store(enabled != 0u, std::memory_order_release);
    Log(std::string("result=0 detail=controller_drive value=") + (enabled ? "1" : "0"));
    return 0;
}

DWORD CalibrateAnimIk()
{
    if (!gDrive.load(std::memory_order_acquire)) {
        Log("result=refused detail=drive_off");
        return 1;
    }
    std::lock_guard stateLock(gIkMutex);
    // The authored right-hand path has no equip-angle zero to capture.
    gCalibration.Request(gHands.load() & (gAlignWeapon ? 2u : 3u));
    Log("result=0 detail=calibration_requested");
    return 0;
}

DWORD SetAnimIkWeaponAlignment(unsigned int enabled)
{
    if (enabled > 1) { return 1; }
    std::lock_guard stateLock(gIkMutex);
    if (gAlignWeapon != (enabled != 0)) {
        // A baseline comparison needs a fresh explicit calibration; an old
        // grip offset must not survive a different orientation ownership mode.
        gCalibration.pending &= ~1u;
        gCalibration.autoPending &= ~1u;
        gCalibration.calibrated &= ~1u;
        gCalibrated[0].store(false);
    }
    gAlignWeapon = enabled != 0;
    gAlignStatus = gAlignWeapon ? weaponrig::Status::noSample : weaponrig::Status::disabled;
    gAlignBasis = {};
    gAlignGeneration = gAlignSequence = 0;
    return 0;
}

DWORD SetAnimIkJointSignature(unsigned int joints)
{
    if (joints == 0 || joints > kMaxJoints) { return 1; }
    std::lock_guard stateLock(gIkMutex);
    gCalibration.Invalidate();
    for (auto& flag : gCalibrated) { flag.store(false); }
    gSignatureJoints.store(joints, std::memory_order_relaxed);
    gRigSkeleton.store(0, std::memory_order_release);   // re-identify
    Log("result=0 detail=signature joints=" + std::to_string(joints));
    return 0;
}

DWORD SetAnimIkReachPercent(int percent)
{
    if (percent < 50 || percent > 150) { return 1; }
    gReachPercent.store(percent, std::memory_order_relaxed);
    Log("result=0 detail=reach_percent value=" + std::to_string(percent));
    return 0;
}

int AnimIkReachPercent() { return gReachPercent.load(std::memory_order_relaxed); }

DWORD SetAnimIkCameraAnchor(unsigned int enabled)
{
    const bool on = enabled != 0;
    gCameraAnchor.store(on, std::memory_order_release);
    lifecycle::Log(std::string("preyvr_anim_ik result=0 detail=camera_anchor enabled=") +
                   (on ? "1" : "0"));
    return 0;
}

unsigned long long AnimIkAnchorCameraCount() { return gAnchorCamera.load(std::memory_order_relaxed); }

unsigned long long AnimIkAnchorReticleCount() { return gAnchorReticle.load(std::memory_order_relaxed); }

unsigned long long AnimIkAnchorMissingCount() { return gAnchorMissing.load(std::memory_order_relaxed); }

DWORD SetAnimIkTrace(unsigned int enabled)
{
    const bool on = enabled != 0;
    if (!on) {
        std::lock_guard stateLock(gIkMutex);
        gTrace[0] = {};
        gTrace[1] = {};
    }
    gTraceEnabled.store(on, std::memory_order_release);
    lifecycle::Log(std::string("preyvr_ik result=0 detail=trace enabled=") + (on ? "1" : "0"));
    return 0;
}

DWORD SetAnimIkHands(unsigned int mask)
{
    if (mask == 0 || mask > 3) { return 1; }
    std::lock_guard stateLock(gIkMutex);
    gHands.store(mask, std::memory_order_relaxed);
    Log("result=0 detail=hands mask=" + std::to_string(mask));
    return 0;
}

DWORD SetHandPoseMode(unsigned int mode)
{
    if (mode > 1) { return 1; }
    { std::lock_guard lock(gPoseSettingsMutex); gPoseSettings.mode = mode; }
    Log("result=0 detail=pose_mode value=" + std::to_string(mode));
    return 0;
}

DWORD SetHandPoseCurlPercent(int percent)
{
    if (percent < 0 || percent > 300) { return 1; }
    { std::lock_guard lock(gPoseSettingsMutex); gPoseSettings.curl = percent / 100.0f; }
    return 0;
}

DWORD SetHandPoseGrip(int alongPermille, int outOfPalmMm, int towardThumbMm, int leanDeciDegrees)
{
    if (alongPermille < 0 || alongPermille > 1500 || outOfPalmMm < -100 || outOfPalmMm > 150 ||
        towardThumbMm < -100 || towardThumbMm > 100 || leanDeciDegrees < -900 || leanDeciDegrees > 900) { return 1; }
    std::lock_guard lock(gPoseSettingsMutex);
    gPoseSettings.point.alongHand = alongPermille / 1000.0f;
    gPoseSettings.point.outOfPalm = outOfPalmMm / 1000.0f;
    gPoseSettings.point.towardThumb = towardThumbMm / 1000.0f;
    gPoseSettings.tubeLean = leanDeciDegrees / 10.0f * 3.14159265f / 180.0f;
    return 0;
}

// arm.* -- see the declaration. Values are integers: mm, permille, degrees.
DWORD SetArmSetting(const std::string& what, const int* v, unsigned n)
{
    std::lock_guard lock(gPoseSettingsMutex);
    PoseSettings& p = gPoseSettings;
    armpose::Body& b = p.body;
    const auto mm = [](int x) { return x * 0.001f; };
    if (what == "mode" && n >= 1 && v[0] >= 0 && v[0] <= 1) {
        p.armMode = static_cast<unsigned>(v[0]);
    } else if (what == "anchor" && n >= 1 && v[0] >= 0 && v[0] <= 1) {
        p.neckAnchor = v[0] != 0;
    } else if (what == "lift" && n >= 1 && v[0] >= 0 && v[0] <= 1 && (n < 2 || (v[1] >= 0 && v[1] <= 600)) &&
               (n < 3 || (v[2] >= 0 && v[2] <= 400))) {
        p.torsoLift = v[0] != 0;
        // 0: no limit that way.
        if (n >= 2) { p.liftUp = v[1] > 0 ? mm(v[1]) : kNoLiftLimit; }
        if (n >= 3) { p.liftDown = v[2] > 0 ? mm(v[2]) : kNoLiftLimit; }
    } else if (what == "length" && n >= 1 && v[0] >= kArmLengthMinCm && v[0] <= kArmLengthMaxCm) {
        b = armpose::WithArmLength(b, v[0] * 0.01f);
    } else if (what == "shoulder" && n >= 3 && std::abs(v[0]) <= 300 && v[1] >= 50 && v[1] <= 350 &&
               v[2] <= -50 && v[2] >= -450) {
        b.shoulderForward = mm(v[0]); b.shoulderOut = mm(v[1]); b.shoulderUp = mm(v[2]);
    } else if (what == "len" && n >= 2 && v[0] >= 150 && v[0] <= 450 && v[1] >= 150 && v[1] <= 450) {
        b.upper = mm(v[0]); b.fore = mm(v[1]);
    } else if (what == "stretch" && n >= 2 && v[0] >= 0 && v[0] <= 500 && v[1] >= 1000 && v[1] <= 1300) {
        b.clavicleStretch = v[0] / 1000.0f; b.armStretch = v[1] / 1000.0f;
    } else if (what == "prior" && n >= 4 && v[0] >= 5 && v[0] <= 180 && v[1] >= -40 && v[1] <= 30 &&
               v[2] >= 3 && v[2] <= 90 && v[3] >= 3 && v[3] <= 180) {
        p.wrist.flexionSigmaDeg = static_cast<float>(v[0]); p.wrist.deviationRestDeg = static_cast<float>(v[1]);
        p.wrist.deviationSigmaDeg = static_cast<float>(v[2]); p.wrist.swivelSigmaDeg = static_cast<float>(v[3]);
    } else if (what == "rom" && n >= 4 && v[0] >= 10 && v[0] <= 120 && v[1] >= 10 && v[1] <= 120 &&
               v[2] >= 5 && v[2] <= 90 && v[3] >= 5 && v[3] <= 90) {
        p.wrist.flexionMaxDeg = static_cast<float>(v[0]); p.wrist.extensionMaxDeg = static_cast<float>(v[1]);
        p.wrist.radialMaxDeg = static_cast<float>(v[2]); p.wrist.ulnarMaxDeg = static_cast<float>(v[3]);
    } else if (what == "hold" && n >= 2 && v[0] >= 0 && v[0] <= 360 && v[1] >= 0 && v[1] <= 1000) {
        p.wrist.holdSigmaDeg = static_cast<float>(v[0]); p.poleTau = v[1] / 1000.0f;
    } else if (what == "torso" && n >= 2 && v[0] >= 0 && v[0] <= 180 && v[1] >= 0 && v[1] <= 10000) {
        SetBodyTorso(v[0], v[1]);   // one torso: BodyFollow owns the filter's settings
    } else if (what == "knee" && n >= 1 && v[0] >= 500 && v[0] <= 1000) {
        p.armKnee = v[0] / 1000.0f;
    } else {
        return 1;
    }
    return 0;
}

DWORD SetArmLengthCm(int cm)
{
    const int value[1] = {cm};
    return SetArmSetting("length", value, 1) == 0 ? 0 : ERROR_INVALID_PARAMETER;
}

int ArmLengthCm() { return static_cast<int>(std::lround(armpose::ArmLength(ReadPoseSettings().body) * 100.0f)); }

DWORD SetHandPoseStretchPercent(int percent)
{
    if (percent < 90 || percent > 125) { return 1; }
    std::lock_guard lock(gPoseSettingsMutex);
    gPoseSettings.stretch = percent / 100.0f;
    return 0;
}

DWORD SetHandPoseWeaponRoll(unsigned int enabled)
{
    if (enabled > 1) { return 1; }
    std::lock_guard lock(gPoseSettingsMutex);
    gPoseSettings.weaponRoll = enabled != 0;
    return 0;
}

DWORD SetHandPoseReach(unsigned int mode, int kneePercent)
{
    if (mode > 1 || kneePercent < 50 || kneePercent > 99) { return 1; }
    std::lock_guard lock(gPoseSettingsMutex);
    gPoseSettings.reachMode = mode;
    gPoseSettings.knee = kneePercent / 100.0f;
    return 0;
}

DWORD SetAnimIkWideSupport(unsigned enabled)
{
    if (enabled > 1) { return 1; }
    gWideSupport.store(enabled != 0, std::memory_order_relaxed);
    return 0;
}

DWORD SetAnimIkLongReach(int enabled, int comfortMm, int maxBackMm, int supportBackMm, int twistDeg)
{
    if (enabled < 0 || enabled > 1 || comfortMm < 100 || comfortMm > 1500 || maxBackMm < 0 || maxBackMm > 400 ||
        supportBackMm < 0 || supportBackMm > 300 || twistDeg < 0 || twistDeg > 60) { return ERROR_INVALID_PARAMETER; }
    gLongOn.store(enabled != 0); gLongComfortMm.store(comfortMm); gLongMaxBackMm.store(maxBackMm);
    gLongSupportBackMm.store(supportBackMm); gLongTwistDeg.store(twistDeg);
    return 0;
}

DWORD SetAnimIkLongReachEnabled(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gLongOn.store(enabled != 0);
    return 0;
}

unsigned AnimIkLongReachEnabled() { return gLongOn.load() ? 1u : 0u; }

DWORD SetAnimIkSupportLatch(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gSupportLatchOn.store(enabled != 0, std::memory_order_relaxed);
    return 0;
}

unsigned AnimIkSupportLatch() { return gSupportLatchOn.load(std::memory_order_relaxed) ? 1u : 0u; }

unsigned AnimIkWideSupport() { return gWideSupport.load(std::memory_order_relaxed) ? 1u : 0u; }

unsigned long long AnimIkOwnerCharacter() { return gOwnerCharacter.load(); }

unsigned long long AnimIkOwnerGeneration() { return gOwnerGeneration.load(); }

unsigned long long AnimIkPoseSequence() { return gUsedSequence.load(); }

unsigned long long AnimIkNoOwner() { return gNoOwner.load(); }

DWORD SetAnimIkFreeArms(unsigned enabled)
{
    gFreeArms.store(enabled != 0 ? 1u : 0u, std::memory_order_release);
    Log(std::string("result=0 detail=free_arms value=") + (enabled != 0 ? "1" : "0"));
    return 0;
}

unsigned AnimIkFreeArms() { return gFreeArms.load(); }

DWORD SetAnimIkFingerCurl(unsigned enabled)
{
    gFingerCurl.store(enabled != 0 ? 1u : 0u, std::memory_order_release);
    Log(std::string("result=0 detail=finger_curl value=") + (enabled != 0 ? "1" : "0"));
    return 0;
}

unsigned AnimIkFingerCurl() { return gFingerCurl.load(); }

bool AnimIkFreeArmsBound() { return gFreeArms.load() != 0 && gFreeBound.load(); }

unsigned long long AnimIkBusy() { return gBusy.load(); }

unsigned int AnimIkMode() { return gMode.load(std::memory_order_relaxed); }

unsigned int AnimIkHooked() { return gInstalled.load(std::memory_order_relaxed) ? 1u : 0u; }

unsigned long long AnimIkCalls() { return gCalls.load(std::memory_order_relaxed); }

unsigned long long AnimIkMatched() { return gMatched.load(std::memory_order_relaxed); }

unsigned long long AnimIkRigSkeleton() { return gRigSkeleton.load(std::memory_order_relaxed); }

unsigned int AnimIkRigJoints() { return gRigJoints.load(std::memory_order_relaxed); }

unsigned int AnimIkGate() { return gGate.load(std::memory_order_relaxed); }

int AnimIkCvar() { return gCvar.load(std::memory_order_relaxed); }

int AnimIkTargetJoint(unsigned int hand) { return hand < 2 ? gTargetJoint[hand].load(std::memory_order_relaxed) : -1; }

int AnimIkWeightJoint(unsigned int hand) { return hand < 2 ? gWeightJoint[hand].load(std::memory_order_relaxed) : -1; }

int AnimIkLimbEnd(unsigned int hand) { return hand < 2 ? gLimbEnd[hand].load(std::memory_order_relaxed) : -1; }

unsigned int AnimIkLimbTag(unsigned int hand) { return hand < 2 ? gLimbTagValue[hand].load(std::memory_order_relaxed) : 0; }

unsigned long long AnimIkWritten(unsigned int hand) { return hand < 2 ? gWritten[hand].load(std::memory_order_relaxed) : 0; }

unsigned long long AnimIkNoPose() { return gNoPose.load(std::memory_order_relaxed); }

unsigned long long AnimIkClamped() { return gClamped.load(std::memory_order_relaxed); }

unsigned int AnimIkCalibrated(unsigned int hand) { return hand < 2 && gCalibrated[hand].load(std::memory_order_relaxed) ? 1u : 0u; }

int AnimIkLocationMillimetres(unsigned int axis) { return axis < 3 ? gLocMm[axis].load(std::memory_order_relaxed) : 0; }

int AnimIkLocationYawMilliDegrees() { return gLocYawMilli.load(std::memory_order_relaxed); }

int AnimIkLastGoalMillimetres(unsigned int axis) { return axis < 3 ? gLastGoalMm[axis].load(std::memory_order_relaxed) : 0; }

} // namespace preyvr::dll
