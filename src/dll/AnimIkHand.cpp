#include "AnimIkTakeover.h"
#include "AnimIkState.h"

// DriveHand: each hand's IK target from its controller, through the anchor the
// hands are placed with (kIkAnchor) -- on a held weapon's barrel, as a free hand,
// or calibrated -- with the reach shaped to the arm; and the fingers.

namespace preyvr::dll {
using namespace ik;

namespace {

// The wrist rotation (engine world) that puts the game hand's anatomical frame
// on the player's, given the controller's grip orientation in engine world.
Quaternion AnatomicalWrist(unsigned hand, const Quaternion& gripWorld, const PoseSettings& settings)
{
    const auto side = hand == 1 ? handpose::Side::left : handpose::Side::right;
    const Quaternion handInGrip = handpose::FrameToFrame(gHandRig.frame[hand],
                                                         handpose::GripHandFrame(side, settings.tubeLean));
    return Normalize(Multiply(Multiply(gripWorld, stereo::kOpenXrToEngine), handInGrip));
}

// The grip point (middle of the fist) in the wrist's local frame, model units.
Vec3 GripPointLocal(unsigned hand, const PoseSettings& settings)
{
    return handpose::GripPointInWrist(gHandRig.frame[hand], gHandRig.knuckle[hand], settings.point);
}

std::uint32_t ReadVtableRva(std::uintptr_t object)
{
    const auto base = PreyBase();
    __try {
        const std::uintptr_t vtable = object ? *reinterpret_cast<const std::uintptr_t*>(object) : 0;
        return base && vtable > base && vtable - base < 0x10000000ull ? static_cast<std::uint32_t>(vtable - base) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

} // namespace

namespace ik {

void DriveHand(unsigned int hand, const OwnedPose& owned, const GameplayPoseFrame& frame,
               const EquippedRig& owner, const PoseSettings& pose, PrimaryHand* primaryOut,
               const PrimaryHand* primaryIn, FingerPlan* fingers, ArmPlan* arm)
{
    std::uint8_t* const relative = owned.relative;
    std::uint8_t* const absolute = owned.absolute;
    const unsigned poseCount = owned.count;
    const animik::Location& location = owned.location;
    // The support hand follows the primary wrist only once it was written on the weapon.
    const Vec3* const supportPrimary = primaryIn != nullptr && primaryIn->written ? &primaryIn->goal : nullptr;
    const Quaternion* const supportPrimaryRotation =
        primaryIn != nullptr && primaryIn->written ? &primaryIn->rotation : nullptr;
    const int target = gTargetJoint[hand].load(std::memory_order_relaxed);
    const int weight = gWeightJoint[hand].load(std::memory_order_relaxed);
    const int handJoint = gHandJoint[hand].load(std::memory_order_relaxed);
    if (hand < 2) { gMarkGoal[hand].set = false; }
    Vec3 markRaw{};
    if (poseCount>kMaxJoints || !animik::ValidJoint(target, poseCount) ||
        !animik::ValidJoint(weight, poseCount) ||
        !animik::ValidJoint(handJoint, poseCount)) { return; }

    QuatT wrist{};
    if (!ReadQuatT(absolute, poseCount, handJoint, wrist)) { return; }

    Vec3 goal{wrist.t[0], wrist.t[1], wrist.t[2]};
    Quaternion rotation{wrist.q[0], wrist.q[1], wrist.q[2], wrist.q[3]};
    bool writeRotation = false;
    bool calibrating = false;
    bool aligned = false;
    Quaternion candidateOffset{};
    SupportGripGeometry supportGeometry{};
    Vec3 supportCandidate{};
    bool supportInBounds=false;
    float holdBack=0.0f;   // metres the weapon sits back along the barrel (long weapons)
    float appliedRoll = 0.0f;   // pose.roll about the barrel this pass (radians)
    const bool twoHand=frame.weaponGeneration==owner.generation&&frame.twoHand.blend>0&&TwoHandedAimEnabled();
    const bool supportLocked=hand==1&&twoHand&&supportPrimary;
    // pose.mode 1 on a rig whose hand was identified.
    const bool posed = pose.mode == 1 && gHandRig.valid[hand] && gHandRig.skeleton == gRigSkeleton.load();
    // The native two-handed hold, carried with the written right wrist.
    bool carry = false;
    Quaternion carryRotation{};
    Vec3 carryGoal{};
    if (posed && supportLocked && supportPrimaryRotation) {
        QuatT nativeRight{};
        if (ReadQuatT(absolute, poseCount, gHandJoint[0].load(std::memory_order_relaxed), nativeRight)) {
            const Quaternion turn = Normalize(Multiply(*supportPrimaryRotation,
                Conjugate(Quaternion{nativeRight.q[0], nativeRight.q[1], nativeRight.q[2], nativeRight.q[3]})));
            carryRotation = Normalize(Multiply(turn, Quaternion{wrist.q[0], wrist.q[1], wrist.q[2], wrist.q[3]}));
            carryGoal = Add(*supportPrimary, Rotate(turn, Vec3{wrist.t[0] - nativeRight.t[0],
                                                                wrist.t[1] - nativeRight.t[1],
                                                                wrist.t[2] - nativeRight.t[2]}));
            carry = true;
        }
    }
    bool posedFree = false;   // the free hand's anatomical goal was computed

    // Hoisted purely so the trace can see them: the controller values are built
    // inside the drive branch, while the reach maths that consumes the goal sits
    // outside it. Zero when the branch did not run, which the trace's own
    // validity flag already distinguishes from a real zero.
    Vec3 traceGrip{}, traceControllerWorld{};
    if (gDrive.load(std::memory_order_acquire)) {
        const auto& state = frame.tracking.hands[static_cast<unsigned int>(hand == 0 ? Hand::right : Hand::left)];
        // Without a play-space yaw the world placement is unknown, and the
        // camera-relative fallback is the defect body yaw exists to remove.
        if (!frame.headYawUsable) {
            gNoPose.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!IsPoseUsable(state.gripPose, state.gripValidity, kMaxPoseAgeNs)) {
            gNoPose.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const Hand drivenHand = hand == 0 ? Hand::right : Hand::left;
        // Surface stop (SurfaceStopLane.h): where fixed scenery holds the hand
        // back, the hand -- and the weapon it holds -- is drawn there instead.
        // The hands' anchor (kIkAnchor): a frame without a camera is refused,
        // never placed on the reticle origin instead.
        Pose world{};
        if (!HandWorld(frame, drivenHand, ControllerPose::grip, kIkAnchor, Placement::drawn, world)) { return; }
        const Vec3 stopped = SurfaceOffset(frame, drivenHand);
        if(supportLocked) world.orientation=frame.twoHand.supportOrientation;
        traceGrip = state.gripPose.position;
        traceControllerWorld = world.position;
        goal = animik::WorldToModel(location, world.position);
        // A grenade has no barrel to align (status missing_helper, and the hand
        // then stayed where the native animation holds it, 10-20 cm off the
        // controller): the hand holding it is driven like a free hand, the
        // grenade in the native fingers (GrenadeLane.h).
        if (hand == 0 && gAlignWeapon && owner.weapon != 0 && !GrenadeHeldByHand(owner.weapon)) {
            gAlignGeneration = owner.generation;
            gWeaponVtableRva.store(ReadVtableRva(owner.weapon), std::memory_order_relaxed);
            gAlignSequence = frame.tracking.sequence;
            gAlignBasis = {};
            if (!IsPoseUsable(state.aimPose, state.aimValidity, kMaxPoseAgeNs)) {
                gAlignStatus = weaponrig::Status::staleAim;
                ++gAlignRefused;
                return;
            }
            gAlignStatus = weaponrig::ReadBasis({nullptr, ReadAlignmentMemory},
                PreyBase(), owner,
                reinterpret_cast<std::uintptr_t>(absolute), poseCount, handJoint, gAlignBasis);
            if (gAlignStatus != weaponrig::Status::ready) { ++gAlignRefused; return; }
            if (gHelperDump.exchange(false)) { DumpWeaponFrames(owner); }
            // Both wrist poses are still native here. Convert their separation
            // into the actual barrel basis, never into a controller-at-equip basis.
            QuatT nativeLeft{};
            if(gAlignBasis.source!=weaponrig::Source::wrenchModel&&
               ReadQuatT(absolute,poseCount,gHandJoint[1].load(),nativeLeft)) {
                const Quaternion nativeBarrel=Normalize(Multiply(Multiply(
                    Quaternion{wrist.q[0],wrist.q[1],wrist.q[2],wrist.q[3]},
                    gAlignBasis.weaponInWrist),gAlignBasis.barrelInWeapon));
                // With pose.mode 1 a controller stands for the PALM, so the
                // support region is where the native left palm is, not its wrist.
                Vec3 nativeLeftPoint{nativeLeft.t[0], nativeLeft.t[1], nativeLeft.t[2]};
                if (posed && gHandRig.valid[1]) {
                    nativeLeftPoint = Add(nativeLeftPoint, Rotate(
                        Quaternion{nativeLeft.q[0], nativeLeft.q[1], nativeLeft.q[2], nativeLeft.q[3]},
                        GripPointLocal(1, pose)));
                }
                const Vec3 delta{(nativeLeftPoint.x-wrist.t[0])*location.s,
                                 (nativeLeftPoint.y-wrist.t[1])*location.s,
                                 (nativeLeftPoint.z-wrist.t[2])*location.s};
                const Vec3 socket=Rotate(Quaternion{-nativeBarrel.x,-nativeBarrel.y,-nativeBarrel.z,nativeBarrel.w},delta);
                gSupportSocketMm[0].store(static_cast<int>(socket.x*1000.f),std::memory_order_relaxed);
                gSupportSocketMm[1].store(static_cast<int>(socket.y*1000.f),std::memory_order_relaxed);
                gSupportSocketMm[2].store(static_cast<int>(socket.z*1000.f),std::memory_order_relaxed);
                // A foregrip ahead of the grip, near the barrel, or a handgun's cup
                // beside it (twohand::ClassifySocket). Wide (ik.widesupport,
                // default): the Q-Beam's front handle is on the side of a 1.25 m
                // weapon, native left palm at (375, 616, 198) mm in the barrel frame
                // (measured 2026-10-07); the solver's own ClosestGrip still bounds
                // the reach to 0.8 m and requires it ahead of the grip.
                preyvr::twohand::Grip kind{};
                supportCandidate=socket;
                supportInBounds=preyvr::twohand::ClassifySocket(socket,gWideSupport.load(std::memory_order_relaxed),kind);
            }
            // The handle does not move on the weapon; the native palm does
            // (firing/cooling animations). One settled socket per weapon
            // (twohand::RegionLatch); ik.supportlatch 0 = the raw palm, for A/B.
            Vec3 socket{};
            bool haveSocket=false;
            if(gSupportLatchOn.load(std::memory_order_relaxed)) {
                std::lock_guard lock(gSupportLatchMutex);
                haveSocket=gSupportLatch.Update(owner.generation,supportInBounds,supportCandidate,socket);
            } else if(supportInBounds) { socket=supportCandidate;haveSocket=true; }
            // The kind of support, from the socket in use: a latched one keeps its weapon's.
            preyvr::twohand::Grip grip{};
            haveSocket=haveSocket&&preyvr::twohand::ClassifySocket(socket,gWideSupport.load(std::memory_order_relaxed),grip);
            // A long weapon (its foregrip beyond reach) is held back by the
            // difference, eased in once its socket is known; 0 for the others.
            constexpr preyvr::twohand::LongReach reachSettings{};
            const float holdTarget=haveSocket&&grip==preyvr::twohand::Grip::foregrip
                ?preyvr::twohand::HoldBack(socket,reachSettings):0.0f;
            {
                const std::uint64_t now=preyvr::timing::MonotonicNanoseconds();
                if(gHoldBackOwner!=owner.generation){gHoldBackOwner=owner.generation;gHoldBackEased=0.0f;gHoldBackNs=0;}
                const float dt=gHoldBackNs&&now>gHoldBackNs?std::min(static_cast<float>(now-gHoldBackNs)*1e-9f,.1f):0.0f;
                gHoldBackNs=now;
                gHoldBackEased+=(holdTarget-gHoldBackEased)*ExpAlpha(dt,8.0f);
                if(std::fabs(holdTarget-gHoldBackEased)<.0005f)gHoldBackEased=holdTarget;
                holdBack=gHoldBackEased;
                gHoldBackMm.store(static_cast<int>(holdBack*1000.0f),std::memory_order_relaxed);
                gLongGeneration.store(holdTarget>0?owner.generation:0,std::memory_order_relaxed);
            }
            if(haveSocket) {
                supportGeometry.region=preyvr::twohand::SupportRegion(socket,holdTarget,reachSettings,grip);
                supportGeometry.owner=owner.generation;
                supportGeometry.recenter=frame.recenterEpoch;
                supportGeometry.epoch=frame.tracking.epoch;
            }
            Pose aimWorld{};
            HandWorld(frame, drivenHand, ControllerPose::aim, kIkAnchor, Placement::controller, aimWorld);
            if(twoHand) aimWorld.orientation=frame.twoHand.orientation;
            if (!weaponrig::SolveWrist(location.q, aimWorld.orientation, gAlignBasis, rotation)) {
                gAlignStatus = weaponrig::Status::invalidBasis;
                ++gAlignRefused;
                return;
            }
            aligned = true;
            writeRotation = true;
            if (posed) {
                // The weapon's grip sets the wrist's rotation; the palm goes to
                // the controller's grip point (the IK target is the wrist).
                Quaternion wristWorld = Normalize(Multiply(location.q, rotation));
                if (pose.weaponRoll) {
                    // **The barrel stays on the aim ray; its ROLL is the hand's.**
                    // The roll above comes from the muzzle helper's authored
                    // frame, which is the weapon artist's, not the hand's: with
                    // the Huntress Boltcaster it put the right palm facing out
                    // (measured 166 deg from the real palm) although the game's
                    // own idle holds it palm-in. Roll the weapon and hand about
                    // the barrel until the drawn hand lies like the player's.
                    const Vec3 axis = Rotate(aimWorld.orientation, Vec3{0.0f, 1.0f, 0.0f});
                    const Quaternion real = AnatomicalWrist(hand, world.orientation, pose);
                    const Vec3 from[2] = {Rotate(wristWorld, gHandRig.frame[hand].palm),
                                          Rotate(wristWorld, gHandRig.frame[hand].thumb)};
                    const Vec3 to[2] = {Rotate(real, gHandRig.frame[hand].palm),
                                        Rotate(real, gHandRig.frame[hand].thumb)};
                    const float roll = handpose::RollToMatch(axis, from, to, 2);
                    appliedRoll = roll;
                    wristWorld = Normalize(Multiply(handpose::AxisAngle(axis, roll), wristWorld));
                    rotation = animik::WorldToModel(location, wristWorld);
                    gWeaponRollMilliDeg.store(static_cast<int>(roll * 57295.78f), std::memory_order_relaxed);
                }
                // A long weapon sits back in the hand: the drawn trigger hand
                // stays on its grip, both move back along the barrel.
                const Vec3 held = holdBack > 0.0f
                    ? Sub(world.position, Scale(Rotate(aimWorld.orientation, Vec3{0.0f, 1.0f, 0.0f}), holdBack))
                    : world.position;
                goal = animik::WorldToModel(location, Sub(held,
                    Rotate(wristWorld, Scale(GripPointLocal(hand, pose), location.s))));
            }
        } else if (posed) {
            // The free hand: the game hand's anatomical frame on the player's.
            // The raw controller, not a two-handed support orientation: the
            // support hold is the native one, blended in below.
            Pose raw{};
            HandWorld(frame, drivenHand, ControllerPose::grip, kIkAnchor, Placement::controller, raw);
            const Quaternion wristWorld = AnatomicalWrist(hand, raw.orientation, pose);
            rotation = animik::WorldToModel(location, wristWorld);
            goal = animik::WorldToModel(location, Sub(Add(raw.position, stopped),
                Rotate(wristWorld, Scale(GripPointLocal(hand, pose), location.s))));
            writeRotation = true;
            posedFree = true;
        } else {
            const Quaternion controllerModel = animik::WorldToModel(location, world.orientation);
            calibrating = gCalibration.Pending(hand);
            candidateOffset = calibrating ? animik::CalibrateRotationOffset(controllerModel, rotation)
                                          : gCalibration.offsets[hand];
            if (calibrating || (gCalibration.calibrated & (1u << hand))) {
                rotation = animik::ApplyRotationOffset(controllerModel, candidateOffset);
                writeRotation = true;
            }
        }
    } else {
        const Vec3 test{gTestMm[0].load(std::memory_order_relaxed) * 0.001f,
                        gTestMm[1].load(std::memory_order_relaxed) * 0.001f,
                        gTestMm[2].load(std::memory_order_relaxed) * 0.001f};
        if (test.x == 0.0f && test.y == 0.0f && test.z == 0.0f) { return; }
        goal = Vec3{goal.x + test.x, goal.y + test.y, goal.z + test.z};
    }

    // Reach: the authored bone lengths are the relative translations of the mid
    // and end joints, rebuilt from animation every frame before this runs.
    const int upper = gLimbUpper[hand].load(std::memory_order_relaxed);
    const int mid = gLimbMid[hand].load(std::memory_order_relaxed);
    const int end = gLimbEnd[hand].load(std::memory_order_relaxed);
    if (upper < 0 || mid < 0 || end < 0) { return; }
    {
        QuatT upperAbs{}, midRel{}, endRel{};
        if (ReadQuatT(absolute, poseCount, upper, upperAbs) && ReadQuatT(relative, poseCount, mid, midRel) &&
            ReadQuatT(relative, poseCount, end, endRel)) {
            const float reach = 0.995f * (std::sqrt(midRel.t[0]*midRel.t[0] + midRel.t[1]*midRel.t[1] + midRel.t[2]*midRel.t[2]) +
                                          std::sqrt(endRel.t[0]*endRel.t[0] + endRel.t[1]*endRel.t[1] + endRel.t[2]*endRel.t[2]));
            if (!std::isfinite(reach) || reach <= 1e-6f) { return; }
            const Vec3 shoulder{upperAbs.t[0], upperAbs.t[1], upperAbs.t[2]};
            const Vec3 goalRaw = goal;
            markRaw = goalRaw;
            // The character's arm (0.516 m shoulder -> wrist) is shorter than a
            // player's; pose.stretch lets the native two-bone solve lengthen it
            // (it stretches the forearm, up to 1.25x natively) instead of
            // leaving the hand short of the controller at full extension.
            // With the arm on (arm.mode 1) the reach is the player's arm,
            // measured from the player's shoulder: the arm is rewritten from
            // there after the pass, so the animated shoulder no longer limits it.
            const bool anatomical = posed && arm != nullptr && arm->valid;
            const Vec3 reachFrom = anatomical ? arm->shoulderModel : shoulder;
            const float limit = anatomical ? arm->reachModel : (posed ? reach * pose.stretch : reach);
            if (anatomical) { arm->reach = Dist3(goalRaw, reachFrom) * location.s; }
            // Compress the player's reach into the character's BEFORE clamping,
            // so a longer-armed player keeps continuous motion instead of dead
            // travel at full extension.
            //
            // **This mixes the shoulder into the goal**, which is a dependency
            // the trace exists to expose: with k = 0.65, a stationary controller
            // still moves the goal by 35% of any shoulder movement. Zero clamping
            // does not mean zero shoulder contribution.
            const unsigned int reachPercent = gReachPercent.load(std::memory_order_relaxed);
            if (posed && pose.reachMode == 1) {
                // Exact to the knee, then a soft approach to the arm's reach.
                goal = handpose::SoftReach(reachFrom, goal, limit, anatomical ? pose.armKnee : pose.knee);
            } else {
                goal = animik::ScaleReach(shoulder, goal, reachPercent / 100.0f);
            }
            if (posed && hand == 1) {
                // Free hand -> the native two-handed hold, by the support blend.
                const float blend = carry ? frame.twoHand.blend : 0.0f;
                if (carry) {
                    goal = {goal.x + (carryGoal.x - goal.x) * blend, goal.y + (carryGoal.y - goal.y) * blend,
                            goal.z + (carryGoal.z - goal.z) * blend};
                    rotation = posedFree ? handpose::Slerp(rotation, carryRotation, blend) : carryRotation;
                    writeRotation = true;
                }
                if (fingers) { fingers->apply = posedFree; fingers->weight = 1.0f - blend; fingers->support = blend; }
            } else if (posed && hand == 0 && fingers) {
                // No weapon: the right hand holds nothing either.
                fingers->apply = posedFree; fingers->weight = 1.0f; fingers->support = 0.0f;
            } else if(supportLocked) {
                // The weapon follows the already compressed primary wrist.
                // Compressing the support arm independently would pull it off the gun.
                const Vec3 worldOffset=Rotate(frame.twoHand.orientation,frame.twoHand.socket);
                const Quaternion inverseLocation{-location.q.x,-location.q.y,-location.q.z,location.q.w};
                const Vec3 modelOffset=Rotate(inverseLocation,worldOffset);
                const Vec3 attached{supportPrimary->x+modelOffset.x/location.s,
                                    supportPrimary->y+modelOffset.y/location.s,
                                    supportPrimary->z+modelOffset.z/location.s};
                const float blend=frame.twoHand.blend;
                goal={goal.x+(attached.x-goal.x)*blend,goal.y+(attached.y-goal.y)*blend,goal.z+(attached.z-goal.z)*blend};
            }
            const Vec3 goalScaled = goal;
            const Vec3 clamped = animik::ClampToReach(reachFrom, goal, limit);
            const bool didClamp =
                clamped.x != goal.x || clamped.y != goal.y || clamped.z != goal.z;
            if (didClamp) {
                gClamped.fetch_add(1, std::memory_order_relaxed);
                goal = clamped;
            }
            if (gTraceEnabled.load(std::memory_order_acquire) && hand < 2) {
                auto& t = gTrace[hand];
                t.stamp = preyvr::timing::MonotonicNanoseconds();
                t.trackingSequence = frame.tracking.sequence;
                t.publishedNs = frame.publishedNs;
                t.owner = frame.player;
                // The anchor the hands were placed with; the reticle origin with no
                // camera, which only the test-offset path reaches.
                AnchorPoint(frame, Anchor::cameraElseReticle, t.anchor);
                t.yaw = frame.yaw;
                t.head = frame.tracking.head;
                t.grip.position = traceGrip;
                t.controllerWorld = traceControllerWorld;
                t.characterLocation = location.t;
                t.shoulderModel = shoulder;
                t.goalRaw = goalRaw;
                t.goalScaled = goalScaled;
                t.goalClamped = goal;
                t.reachLimit = reach;
                t.reachPercent = reachPercent;
                t.clamped = didClamp;
                t.calibrated = (gCalibration.calibrated & (1u << hand)) != 0;
                t.wroteRotation = writeRotation;
                t.valid = true;
            }
        } else { return; }
    }
    if (!std::isfinite(goal.x) || !std::isfinite(goal.y) || !std::isfinite(goal.z)) { return; }

    if (aligned) {
        EquippedRig current{};
        // Basis reads can straddle an equip/recenter/focus transition. Refuse
        // the old goal if its ownership/reference sample is no longer current.
        GameplayPoseFrame latest{};
        if (!TryGetEquippedRig(frame.player, current) || current.generation != owner.generation ||
            !SameRigBinding(owner, current, owner.itemId) || !TryGetGameplayPoseFrame(latest, true) ||
            latest.player != frame.player || latest.referenceGeneration != frame.referenceGeneration ||
            latest.tracking.epoch != frame.tracking.epoch) {
            gAlignStatus = weaponrig::Status::invalidOwner;
            ++gAlignRefused;
            return;
        }
    }

    // Target: absolute position (and rotation once calibrated); weight: relative X = 1.
    weaponrig::ContactGeometry contactGeometry{};
    const bool contactReady=hand==0&&aligned&&PhysicalGeometryWanted()&&
        weaponrig::ReadContactGeometry({nullptr,ReadAlignmentMemory},
            PreyBase(),owner,
            reinterpret_cast<std::uintptr_t>(absolute),poseCount,handJoint,contactGeometry);
    auto* const targetAt = absolute + static_cast<std::size_t>(target) * kQuatTStride;
    auto* const weightAt = reinterpret_cast<float*>(relative + static_cast<std::size_t>(weight) * kQuatTStride + 0x10);
    if (!WriteGoal(targetAt, weightAt, goal, rotation, writeRotation)) {
        if (aligned) { gAlignStatus = weaponrig::Status::writeFailed; ++gAlignRefused; }
        gMode.store(0);
        gCalibration = {};
        for (auto& flag : gCalibrated) { flag.store(false); }
        Log("result=fault detail=target_write_disarmed");
        return;
    }
    if (aligned) { gAlignStatus = weaponrig::Status::applied; ++gAlignApplied; }
    if (primaryOut) { primaryOut->rotation = rotation; }
    if(hand==0&&aligned) {
        if(contactReady)PublishPhysicalWeapon(frame,owner,location,Pose{rotation,goal},contactGeometry);
        if(primaryOut) { primaryOut->goal=goal; primaryOut->written=true; }
        const Vec3 actual=animik::ModelToWorld(location,goal);
        // In play space, the frame's basis taken out (SupportGripGeometry).
        supportGeometry.primaryOffsetPlay=Rotate(Conjugate(PlaySpaceRotation(frame)),
            Vec3{actual.x-traceControllerWorld.x,actual.y-traceControllerWorld.y,actual.z-traceControllerWorld.z});
        supportGeometry.publishedNs=preyvr::timing::MonotonicNanoseconds();
        // **The region is in the NATIVE barrel frame; the solver reads it in the
        // aim frame.** They agree only while the barrel's roll is the native
        // one. pose.roll turns weapon and hand about the barrel to fit the real
        // hand (Q-Beam 174.9 deg, measured 2026-10-07), so the native foregrip
        // turns with them: without this the region sat mirrored across the
        // barrel, away from the drawn left hand. Same rotation, about the aim's
        // forward (+Y) axis.
        if (supportGeometry.owner && appliedRoll != 0.0f) {
            const Quaternion turn = handpose::AxisAngle(Vec3{0.0f, 1.0f, 0.0f}, appliedRoll);
            supportGeometry.region.start = Rotate(turn, supportGeometry.region.start);
            supportGeometry.region.end = Rotate(turn, supportGeometry.region.end);
        }
        PublishSupportGripGeometry(supportGeometry); // owner=0 actively withdraws an unsupported region
    }
    if (calibrating) {
        gCalibration.Commit(hand, candidateOffset);
        gCalibrated[hand].store(true);
        Log("result=0 detail=calibrated hand=" + std::to_string(hand));
    }
    gUsedSequence.store(frame.tracking.sequence);
    gWritten[hand].fetch_add(1, std::memory_order_relaxed);
    if (hand < 2) { gMarkGoal[hand] = {true, markRaw, goal}; }
    if (arm != nullptr) { arm->goal = goal; arm->goalSet = true; arm->rotation = rotation; arm->rotationSet = writeRotation; }
    if (hand == 0) {
        gLastGoalMm[0].store(static_cast<int>(goal.x * 1000.0f), std::memory_order_relaxed);
        gLastGoalMm[1].store(static_cast<int>(goal.y * 1000.0f), std::memory_order_relaxed);
        gLastGoalMm[2].store(static_cast<int>(goal.z * 1000.0f), std::memory_order_relaxed);
    }
}

// Before the native pass: a free hand's finger joints take `shape` -- each
// finger its own flexion and closure -- closed at least by `carryFist` around a
// carried object, blended with the animation's by `weight` (1 = the shape).
// Relative rotations only; their offsets (bone lengths) are the rig's.
bool ApplyRelaxedFingers(std::uint8_t* relative, unsigned count, float weight, const handpose::FingerShape& shape,
                         int side, float carryFist)
{
    if (weight <= 0.0f) { return false; }
    const auto hand = side == 0 ? handpose::Side::right : handpose::Side::left;
    for (int j = 0; j < handpose::kJointCount; ++j) {
        const int index = gHandRig.joint[side][j];
        QuatT rel{};
        if (!ReadQuatT(relative, count, index, rel)) { return false; }
        const Quaternion native{rel.q[0], rel.q[1], rel.q[2], rel.q[3]};
        const int finger = handpose::FingerOf(j);
        const Quaternion shaped =
            handpose::ShapeRelative(hand, j, shape.curl[finger], std::max(shape.fist[finger], carryFist));
        const Quaternion target = handpose::Slerp(native, shaped, weight);
        const float q[4] = {target.x, target.y, target.z, target.w};
        if (!WriteFloatsGuarded(relative + static_cast<std::size_t>(index) * kQuatTStride, q, 4)) { return false; }
    }
    return true;
}

// After the native pass, which placed the wrist: the finger joints' absolute
// poses from their (rewritten) relatives, parents first. Idempotent with the
// pass's own re-propagation; makes the result independent of whether it did.
void PropagateFingers(std::uint8_t* relative, std::uint8_t* absolute, unsigned count, const HandRig& rig, int side)
{
    for (int j = 0; j < handpose::kJointCount; ++j) {
        const int role = handpose::ParentRole(j);
        const int parent = role < 0 ? rig.wrist[side] : rig.joint[side][role];
        QuatT p{}, r{};
        if (!ReadQuatT(absolute, count, parent, p) || !ReadQuatT(relative, count, rig.joint[side][j], r)) { return; }
        const Quaternion pq{p.q[0], p.q[1], p.q[2], p.q[3]};
        const Quaternion q = Normalize(Multiply(pq, Quaternion{r.q[0], r.q[1], r.q[2], r.q[3]}));
        const Vec3 t = Add(Vec3{p.t[0], p.t[1], p.t[2]}, Rotate(pq, Vec3{r.t[0], r.t[1], r.t[2]}));
        const float out[7] = {q.x, q.y, q.z, q.w, t.x, t.y, t.z};
        if (!WriteFloatsGuarded(absolute + static_cast<std::size_t>(rig.joint[side][j]) * kQuatTStride, out, 7)) { return; }
    }
}

} // namespace ik

} // namespace preyvr::dll
