#include "CommandChannel.h"
#include "VrMode.h"
#include "VrOptionsRuntime.h"
#include "Psychoscope.h"
#include "BodyEquipment.h"
#include "BodyFollow.h"

#include "AimTakeover.h"
#include "AnimIkTakeover.h"
#include "DebugOverlay.h"
#include "NearFovOverride.h"
#include "CameraEditHook.h"
#include "ConsoleBridgeWin32.h"
#include "CullCameraWatch.h"
#include "HotkeyBridge.h"
#include "preyvr/ConsolePolicy.h"
#include "FrameObserverHook.h"
#include "HandRigTakeover.h"
#include "HeadTrackingHook.h"
#include "Logger.h"
#include "NearViewStereo.h"
#include "DualSceneStereo.h"
#include "DualOverlay.h"
#include "NearParticleStereo.h"
#include "RenderFrame.h"
#include "ReticleFollow.h"
#include "SceneQuery.h"
#include "ShotRay.h"
#include "InteractionLane.h"
#include "CarryLane.h"
#include "GrenadeLane.h"
#include "ArmsLane.h"
#include "PhysicalInteractions.h"
#include "PsiMedkit.h"
#include "Haptics.h"
#include "HudBridge.h"
#include "HudLayer.h"
#include "WristLane.h"
#include "UiPointer.h"
#include "FrameCaptureWin32.h"
#include "InputPost.h"
#include "MoveLane.h"
#include "WeaponAttachment.h"
#include "XrInput.h"
#include "XrSessionHost.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_channel " + line);
}

std::atomic<bool> gRunning{false};
std::atomic<unsigned long long> gProcessed{0};
std::atomic<unsigned long long> gRejected{0};

std::filesystem::path CommandPath()
{
    return lifecycle::LogPath().parent_path() / L"commands.txt";
}

std::filesystem::path ResultPath()
{
    return lifecycle::LogPath().parent_path() / L"results.txt";
}

std::vector<std::string> Split(const std::string& line)
{
    std::vector<std::string> out;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) {
        out.push_back(token);
    }
    return out;
}

// Integer arguments are parsed defensively: a malformed number becomes the
// fallback rather than whatever `atoi` decides, so a typo cannot arm something
// with an unintended value.
bool ParseInt(const std::string& text, int& out)
{
    try {
        std::size_t consumed = 0;
        const int value = std::stoi(text, &consumed, 0);
        if (consumed != text.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseU64(const std::string& text, unsigned long long& out)
{
    try {
        std::size_t consumed = 0;
        const unsigned long long value = std::stoull(text, &consumed, 0);
        if (consumed != text.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

// The controller values the lanes actually read, in thousandths. Without this
// a stick that never arrives and a stick that arrives centred produce the same
// counters, and this session spent a run unable to tell them apart.
std::string ControllerStickReport()
{
    std::ostringstream out;
    for (const auto hand : {Hand::left, Hand::right}) {
        ControllerState state{};
        const bool have = TryGetControllerState(hand, state);
        out << (hand == Hand::left ? " stickL=" : " stickR=");
        if (!have) {
            out << "unavailable";
            continue;
        }
        out << static_cast<int>(state.thumbstickX * 1000.0f) << ","
            << static_cast<int>(state.thumbstickY * 1000.0f)
            << (hand == Hand::left ? " trigL=" : " trigR=")
            << static_cast<int>(state.triggerValue * 1000.0f);
        // **The grip POSITION, per hand, in millimetres.**
        //
        // A wearer reported the left hand mirroring the right. A fixed test goal
        // with no controller in the loop raised the correct hand, so the chain
        // and the joint indices are right and the fault is in the pose feeding
        // the goal. Thumbsticks are already proved separate -- the left stick
        // walks and the right turns -- but those come from `xrGetActionState*`
        // with a subaction path, while the poses come from `xrLocateSpace` on a
        // per-hand action SPACE. Different mechanism, separately fallible.
        //
        // If these two positions read the same while the controllers are held
        // apart, both spaces resolve to one controller and that is the bug. It
        // is one line to check and it was not checkable at all before.
        out << (hand == Hand::left ? " gripL=" : " gripR=")
            << static_cast<int>(state.gripPose.position.x * 1000.0f) << ","
            << static_cast<int>(state.gripPose.position.y * 1000.0f) << ","
            << static_cast<int>(state.gripPose.position.z * 1000.0f)
            << (hand == Hand::left ? " gripLok=" : " gripRok=")
            << (state.gripValidity.positionValid ? 1 : 0);
    }
    return out.str();
}

void WriteReport(std::ostringstream& out)
{
    out << "smoke=" << "n/a"
        << " observerFrames=" << ObservedFrameCount()
        << " xrSession=" << XrSessionStatusValue()
        << " xrFrames=" << XrSubmittedFrameCount()
        << " fovAgreed=" << DeclaredFovAgreeCount()
        << " fovDiverged=" << DeclaredFovDivergeCount()
        << " projectionPolicy=" << (NativeProjectionEnabled() ? "prey_native" : "synthetic")
        << " cameraEdit=" << CameraEditStatusValue()
        << " lastEye=" << LastRenderedEye()
        << " viewObserved=" << ViewHookObservedCount()
        << " viewApplied=" << ViewHookAppliedCount()
        << " viewPoseFallback=" << HeadTrackingPoseReadFallbacks()
        << " posApplied=" << ViewPositionAppliedCount()
        << " posRefused=" << ViewPositionRefusedCount()
        << " posOffsetMm=" << ViewPositionOffsetMillimetres()
        << " nearApplied=" << NearViewAppliedCount()
        << " nearRefused=" << NearViewRefusedCount()
        << " nearNoEye=" << NearViewNoEyeCount()
        << " nearReentered=" << NearViewReenteredCount()
        << " nearAlreadyOffset=" << NearViewAlreadyOffsetCount()
        << " nearNoProvenance=" << NearViewNoProvenanceCount()
        << " eyeLookupMiss=" << BuiltEyeLookupMissCount()
        << " nearDeltaUm=" << NearViewLastDeltaMicrometres()
        << " " << NearParticleStereoReport()
        << " handMatched=" << HandRigMatchedCount()
        << " handSkipped=" << HandRigSkippedCount()
        << " handApplied=" << HandRigAppliedCount()
        << " handRefused=" << HandRigRefusedCount()
        << " handSubtree=" << HandRigLastSubtreeSize()
        << " handRightMm=" << HandRigLastRightMillimetres()
        << " handLeftMm=" << HandRigLastLeftMillimetres()
        << " handNoPose=" << HandRigNoPoseCount()
        << " handJoints=" << HandRigJointCount()
        << " handMode=" << HandRigMode()
        << " handDriveArmed=" << HandRigControllerDriveArmed()
        << " handCalibrated=" << HandRigCalibrationDone()
        << " handRightJoint=" << HandRigSelectedRightJoint()
        << " handLeftJoint=" << HandRigSelectedLeftJoint()
        << " handWristArmed=" << HandRigWristDriveArmed()
        << " handWristApplied=" << HandRigWristAppliedCount()
        << " handTurnYaw=" << HandRigTurnYawDeciDegrees()
        << " ikMode=" << AnimIkMode()
        << " ikHooked=" << AnimIkHooked()
        << " ikOwner=0x" << std::hex << AnimIkOwnerCharacter() << std::dec
        << " ikEquipGen=" << AnimIkOwnerGeneration() << " ikPoseSeq=" << AnimIkPoseSequence()
        << " ikNoOwner=" << AnimIkNoOwner() << " ikBusy=" << AnimIkBusy()
        << " ikCalls=" << AnimIkCalls()
        << " ikMatched=" << AnimIkMatched()
        << " ikRig=0x" << std::hex << AnimIkRigSkeleton() << std::dec
        << " ikJoints=" << AnimIkRigJoints()
        << " ikGate=" << AnimIkGate()
        << " ikCvar=" << AnimIkCvar()
        << " ikTargetR=" << AnimIkTargetJoint(0) << " ikWeightR=" << AnimIkWeightJoint(0)
        << " ikLimbEndR=" << AnimIkLimbEnd(0) << " ikLimbTagR=0x" << std::hex << AnimIkLimbTag(0) << std::dec
        << " ikTargetL=" << AnimIkTargetJoint(1) << " ikWeightL=" << AnimIkWeightJoint(1)
        << " ikLimbEndL=" << AnimIkLimbEnd(1)
        << " ikWrittenR=" << AnimIkWritten(0) << " ikWrittenL=" << AnimIkWritten(1)
        << " ikNoPose=" << AnimIkNoPose() << " ikClamped=" << AnimIkClamped()
        << " ikReach=" << AnimIkReachPercent()
        << " ikCalR=" << AnimIkCalibrated(0) << " ikCalL=" << AnimIkCalibrated(1)
        << " ikLocMm=" << AnimIkLocationMillimetres(0) << "," << AnimIkLocationMillimetres(1)
        << "," << AnimIkLocationMillimetres(2)
        << " ikLocYawMdeg=" << AnimIkLocationYawMilliDegrees()
        << " ikGoalMm=" << AnimIkLastGoalMillimetres(0) << "," << AnimIkLastGoalMillimetres(1)
        << "," << AnimIkLastGoalMillimetres(2)
        << " handZeroRightMm=" << HandRigZeroRightMm(0) << "," << HandRigZeroRightMm(1)
        << "," << HandRigZeroRightMm(2)
        << " handWorldRightMm=" << HandRigWorldRightMm(0) << "," << HandRigWorldRightMm(1)
        << "," << HandRigWorldRightMm(2)
        << " handCalibYawMdeg=" << HandRigCalibrationYawMilli()
        << " handLastYawMdeg=" << HandRigLastYawMilli()
        << " handLastCharacter=0x" << std::hex << HandRigLastCharacter() << std::dec
        << " weaponAttachment=0x" << std::hex << WeaponAttachmentPointer() << std::dec
        << " weaponBone=" << WeaponAttachmentJointIndex()
        << " weaponSim=0x" << std::hex << WeaponAttachmentSimulationFlags() << std::dec
        << WeaponMuzzleAlignmentReport()
        << ShotRayReport()
        << " aimOriginApplied=" << AimOriginAppliedCount()
        << " aimSamples=" << AimSamplePublishedCount()
        << " aimOriginMode=" << AimOriginMode()
        << " aimMuzzleApplied=" << AimMuzzleOriginAppliedCount()
        << " aimMuzzleUnavailable=" << AimMuzzleUnavailableCount()
        << " barrelCalibrations=" << WeaponBarrelCalibrations()
        << " barrelOffsetMm=" << WeaponBarrelOffsetMillimetres(0) << ","
        << WeaponBarrelOffsetMillimetres(1) << "," << WeaponBarrelOffsetMillimetres(2)
        << " reticleApplied=" << ReticleFollowAppliedCount()
        << " reticleOffScreen=" << ReticleFollowOffScreenCount()
        << " reticleXY=" << ReticleFollowLastX() << "," << ReticleFollowLastY()
        << " reticleCanvasXY=" << ReticleCanvasX() << "," << ReticleCanvasY()
        << " reticleCanvasMode=" << ReticleCanvasMode()
        << " reticleNativeCanvas=" << ReticleNativeCanvasCount()
        << " reticleNativeCanvasFailed=" << ReticleNativeCanvasFailedCount()
        << " reticleDispatched=" << ReticleDispatchedCount()
        << " reticleDispatchFailed=" << ReticleDispatchFailedCount()
        << ReticleProjectionReport()
        << " aimDirMilli=" << AimDirectionMilli(0) << "," << AimDirectionMilli(1) << ","
        << AimDirectionMilli(2)
        << " aimRawQMilli=" << AimRawOrientationMilli(0) << "," << AimRawOrientationMilli(1)
        << "," << AimRawOrientationMilli(2)
        << " recenters=" << MoveLaneRecenterCount()
        << " reticleOriginMm=" << ReticleOriginOffsetMillimetres()
        << " reticleConvergeMm=" << ReticleConvergenceMillimetres()
        << " hudElement=0x" << std::hex << HudElementPointer() << std::dec
        << " hudCalls=" << HudCallCount() << " hudRefused=" << HudRefusedCount()
        << " captures=" << CompletedFrameCaptureCount()
        << " aimBodyYaw=" << AimBodyYawEnabled()
        << " camYawMdeg=" << AimCameraYawMilliDegrees()
        << " headYawMdeg=" << AimHeadYawMilliDegrees()
        << " playYawMdeg=" << AimPlaySpaceYawMilliDegrees()
        << " eyeMm=" << AimNativeEyeMillimetres(0) << "," << AimNativeEyeMillimetres(1)
        << "," << AimNativeEyeMillimetres(2)
        << " headMm=" << AimTrackedHeadMillimetres(0) << "," << AimTrackedHeadMillimetres(1)
        << "," << AimTrackedHeadMillimetres(2)
        << " yawHeld=" << AimHeadYawHeldCount()
        << " yawUnavailable=" << AimHeadYawUnavailableCount()
        << " weaponMountMm=" << WeaponMountPositionMillimetres(0)
        << "," << WeaponMountPositionMillimetres(1)
        << "," << WeaponMountPositionMillimetres(2)
        << " weaponApplied=" << WeaponOffsetAppliedCount()
        << " weaponRefused=" << WeaponOffsetRefusedCount()
        << " weaponRotApplied=" << WeaponRotationAppliedCount()
        << " weaponRotNoPose=" << WeaponRotationNoPoseCount()
        << " weaponRotDrive=" << WeaponRotationDriveArmed()
        << " weaponRotCalib=" << WeaponRotationCalibrated()
        << " weaponBaseline=" << WeaponBaselineCaptured()
        << " weaponAimUsable=" << WeaponAimPoseUsable()
        << " aimApplied=" << AimTakeoverAppliedCount()
        << " aimNoPose=" << AimTakeoverRejectedNoPose()
        << " aimNoPlayer=" << AimTakeoverRejectedNoPlayer()
        << " aimCompose=" << AimTakeoverRejectedCompose()
        << " aimNativeMagMilli=" << AimTakeoverNativeDirectionMagnitude()
        << " frameCaptures=" << RenderFrameCaptureCount()
        << " frameTracked=" << RenderFrameTrackedCharacters()
        << " frameOverrideApplied=" << RenderFrameOverrideAppliedCount()
        << " frameOverrideRefused=" << RenderFrameOverrideRefusedCount()
        << " menuActions=" << MenuNavigationActionCount()
        << " inputPosted=" << InputPostCount()
        << " menuDiscarded=" << InputMenuDiscardedCount()
        << " moveMode=" << MoveLaneMode()
        << " moveHooked=" << MoveLaneHooked()
        << " moveOursX=" << MoveLaneOursX() << " moveOursY=" << MoveLaneOursY()
        << " moveNative=" << MoveLaneNative()
        << " movePosted=" << MoveLanePosted() << " moveDropped=" << MoveLaneDropped()
        << " moveInputObj=0x" << std::hex << MoveLaneInputObject() << std::dec
        << " moveAxisMilli=" << MoveLaneAxisMilli(0) << "," << MoveLaneAxisMilli(1)
        << " moveCinematic=" << MoveLaneCinematicGate()
        << " turnEnabled=" << TurnLaneEnabled()
        << " turnPosted=" << TurnLanePosted() << " turnRefused=" << TurnLaneRefused()
        << " fireEnabled=" << FireLaneEnabled()
        << " firePressed=" << FireLanePressed() << " fireReleased=" << FireLaneReleased()
        << " fireHold=" << FireLaneHold() << " fireHoldPosted=" << FireLaneHoldPosted()
        << " fireRefused=" << FireLaneRefused()
        << " mirrorFovPct=" << MirrorFovPercent()
        << " xrSyncs=" << XrInputSyncCount()
        << " xrNotFocused=" << XrInputNotFocusedCount()
        << " xrLocatedL=" << XrInputLocatedCount(Hand::left)
        << " xrLocatedR=" << XrInputLocatedCount(Hand::right)
        << ControllerStickReport()
        << " inputDropped=" << InputQueueDroppedCount()
        << " inputDrainThread=" << InputDrainThreadId()
        << " channelProcessed=" << gProcessed.load(std::memory_order_relaxed)
        << " channelRejected=" << gRejected.load(std::memory_order_relaxed);
}

// **A status is not a result code, and printing one as the other is how a
// working session gets reported as a failure.** `xr.start` returns
// `XrSessionStatus`, where 1 means *running*; `observer` returns
// `FrameObserverRuntimeStatus`, where 2 means *enabled*. Both were read as
// errors during a live run, and a correction had to be issued for each. These
// print the name alongside the number so the reading needs no lookup.
const char* XrStatusName(DWORD value)
{
    switch (value) {
        case 0: return "idle";
        case 1: return "running";
        case 2: return "adapter_mismatch";
        case 3: return "unavailable";
        case 4: return "failed";
        case 5: return "stopped";
        default: return "unknown";
    }
}

const char* ObserverStatusName(DWORD value)
{
    switch (value) {
        case 0: return "unavailable";
        case 1: return "ready";
        case 2: return "enabled";
        case 3: return "failed";
        default: return "unknown";
    }
}

// **A verb given no value reports and changes nothing** (docs/TUNING-COMMANDS.md).
// A setter verb used to set something when bare -- its default, or "on", or for
// ik.mode and move.mode "off" -- so asking what was set overwrote it, the VR
// Options menu's saved choice included. Each now calls its setter only when
// Valued, and then shows what is set where its lane exposes that, or says that
// nothing changed (NoValue).
bool Valued(const std::vector<std::string>& args) { return args.size() > 1; }
const char* NoValue(const std::vector<std::string>& args)
{
    return Valued(args) ? "" : " note=no_value_nothing_changed";
}

// The hand-pose verbs (jordi/hand-pose). Kept out of Execute's chain, which is
// at MSVC's block-nesting limit (C1061).
bool ExecutePose(const std::vector<std::string>& args, std::ostringstream& out)
{
    const std::string& verb = args[0];
    const auto arg = [&](std::size_t i, int fallback) {
        int value = fallback;
        if (i < args.size()) {
            ParseInt(args[i], value);
        }
        return value;
    };
    if (verb == "use.report") {
        out << "use.report result=0" << UseReport();
    } else if (verb == "use.find" && args.size() > 1) {
        out << "use.find result=0" << UseFind(static_cast<std::uint32_t>(arg(1, 0)));
    } else if (verb == "use.hand" || verb == "use.hold" || verb == "use.trace" || verb == "use.back") {
        // Bare verbs report; a value sets. use.hand 1 = left hand points and
        // uses, right grip reloads (default); 0 = right grip, weapon ray.
        DWORD result = 0;
        if (Valued(args)) {
            const unsigned v = static_cast<unsigned>(arg(1, 1));
            result = verb == "use.hand" ? SetUseHand(v) : verb == "use.hold" ? SetUseHold(v)
                   : verb == "use.trace" ? SetUseTrace(v) : SetUseBackMillimetres(v);
        }
        out << verb << " result=" << result << UseReport();
    } else if (verb == "use.filter") {
        // use.filter <0|1> [minCutoffCentiHz 100] [betaHundredths 200] [backlashCentiDegrees 75]:
        // One Euro then a dead band on the pointer.
        DWORD result = 0;
        if (Valued(args)) {
            result = SetUseFilter(static_cast<unsigned>(arg(1, 1)), static_cast<float>(arg(2, 100)) * .01f,
                                  static_cast<float>(arg(3, 200)) * .01f, static_cast<float>(arg(4, 75)) * .01f);
        }
        out << "use.filter result=" << result << UseReport();
    } else if (verb == "ik.skel") {
        out << "ik.skel result=" << RequestAnimIkSkeletonDump();
    } else if (verb == "ik.widesupport") {
        out << "ik.widesupport result=" << (Valued(args) ? SetAnimIkWideSupport(static_cast<unsigned>(arg(1, 1))) : 0)
            << " wide=" << AnimIkWideSupport();
    } else if (verb == "ik.supportlatch") {
        out << "ik.supportlatch result=" << (Valued(args) ? SetAnimIkSupportLatch(static_cast<unsigned>(arg(1, 1))) : 0)
            << " latch=" << AnimIkSupportLatch();
    } else if (verb == "ik.longreach") {
        // ik.longreach [on comfortMm maxBackMm supportBackMm twistDeg]; bare reports.
        DWORD result = 0;
        if (Valued(args)) {
            result = SetAnimIkLongReach(arg(1, 1), arg(2, 450), arg(3, 200), arg(4, 100), arg(5, 30));
        }
        out << "ik.longreach result=" << result << AnimIkLongReachReport();
    } else if (verb == "ik.helpers") {
        out << "ik.helpers result=" << RequestAnimIkHelperDump() << " (see PreyVR.log: preyvr_anim_ik helper)";
    } else if (verb == "pose.mode") {
        out << "pose.mode result=" << (Valued(args) ? SetHandPoseMode(static_cast<unsigned>(arg(1, 1))) : 0)
            << HandPoseReport();
    } else if (verb == "pose.curl") {
        out << "pose.curl result=" << (Valued(args) ? SetHandPoseCurlPercent(arg(1, 100)) : 0) << HandPoseReport();
    } else if (verb == "pose.grip") {
        out << "pose.grip result="
            << (args.size() > 4 ? SetHandPoseGrip(arg(1, 550), arg(2, 35), arg(3, 0), arg(4, 150)) : 0)
            << HandPoseReport();
    } else if (verb == "pose.reach") {
        out << "pose.reach result="
            << (args.size() > 2 ? SetHandPoseReach(static_cast<unsigned>(arg(1, 1)), arg(2, 85)) : 0)
            << HandPoseReport();
    } else if (verb == "near.follow") {
        out << "near.follow result=" << (Valued(args) ? SetNearFovFollow(static_cast<unsigned>(arg(1, 1))) : 0)
            << " followedDeci=" << NearFovFollowed();
    } else if (verb == "pose.roll") {
        out << "pose.roll result=" << (Valued(args) ? SetHandPoseWeaponRoll(static_cast<unsigned>(arg(1, 1))) : 0)
            << HandPoseReport();
    } else if (verb == "pose.stretch") {
        out << "pose.stretch result=" << (Valued(args) ? SetHandPoseStretchPercent(arg(1, 120)) : 0)
            << HandPoseReport();
    } else if (verb == "arm.report") {
        out << "arm.report result=0" << ArmReport();
    } else if (verb == "body.report") {
        // docs/BODY-YAW-2026-10-11.md: the body turned to the torso.
        out << "body.report result=0" << BodyFollowReport();
    } else if (verb == "body.follow") {
        out << "body.follow result=" << (Valued(args) ? SetBodyFollow(static_cast<unsigned>(arg(1, 1))) : 0)
            << BodyFollowReport();
    } else if (verb == "body.level") {
        out << "body.level result=" << (Valued(args) ? SetBodyFollowLevel(static_cast<unsigned>(arg(1, 1))) : 0)
            << BodyFollowReport();
    } else if (verb == "body.torso") {
        // body.torso <deadzoneDeg> <relax per mille per second>; arm.torso sets the same filter.
        out << "body.torso result=" << (args.size() > 2 ? SetBodyTorso(arg(1, 35), arg(2, 600)) : 0)
            << BodyFollowReport();
    } else if (verb == "body.turn") {
        // body.turn <degrees> [delayS=5]: one native turn with the reference --
        // the body turns, the view must not. Waits for gameplay and the delay.
        out << "body.turn result=" << RequestBodyTurn(arg(1, 0), arg(2, 5)) << BodyFollowReport();
    } else if (verb == "body.roomscale") {
        // docs/ROOMSCALE-2026-10-11.md: the capsule follows the head.
        out << "body.roomscale result=" << (Valued(args) ? SetRoomScale(static_cast<unsigned>(arg(1, 1))) : 0)
            << BodyFollowReport();
    } else if (verb == "body.zone") {
        // body.zone <startMm> <stopMm> [followMs]: idle until the head is start
        // away, then glide to stop with that time constant.
        out << "body.zone result=" << (args.size() > 2 ? SetRoomScaleZone(arg(1, 30), arg(2, 5), arg(3, -1)) : 0)
            << BodyFollowReport();
    } else if (verb == "body.radius") {
        out << "body.radius result=" << (Valued(args) ? SetRoomScaleRadius(arg(1, 350)) : 0) << BodyFollowReport();
    } else if (verb == "body.nudge") {
        // body.nudge <rightCm> <forwardCm> [delayS=5]: one swept move with the
        // origin -- the body moves, the view must not. Waits like body.turn.
        out << "body.nudge result=" << RequestBodyNudge(arg(1, 0), arg(2, 0), arg(3, 5)) << BodyFollowReport();
    } else if (verb.rfind("arm.", 0) == 0) {
        int values[4]{};
        unsigned count = 0;
        for (std::size_t i = 1; i < args.size() && count < 4; ++i) { values[count++] = arg(i, 0); }
        out << verb << " result=" << (count > 0 ? SetArmSetting(verb.substr(4), values, count) : 0) << ArmReport();
    } else if (verb == "pose.report") {
        out << "pose.report result=0" << HandPoseReport();
    } else if (verb == "pose.marks") {
        out << "pose.marks result=0\n" << HandPoseMarks();
    } else if (verb == "dbg.draw") {
        // Bare `dbg.draw` only reports. `on`/`all` = every layer, `off` = 0,
        // otherwise a layer mask (see DebugOverlayScene.h).
        DWORD result = 0;
        if (Valued(args)) {
            const std::string& value = args[1];
            int mask = -1;
            if (value == "on" || value == "all") { mask = 255; }
            else if (value == "off") { mask = 0; }
            else if (!ParseInt(value, mask) || mask < 0 || mask > 255) { mask = -1; }
            result = mask >= 0 ? SetDebugOverlay(static_cast<unsigned>(mask)) : ERROR_INVALID_PARAMETER;
        }
        out << "dbg.draw result=" << result << DebugOverlayReport();
    } else if (verb == "move.firehold") {
        out << "move.firehold result=" << (Valued(args) ? SetFireLaneHold(static_cast<unsigned>(arg(1, 1))) : 0)
            << " hold=" << FireLaneHold() << " posted=" << FireLaneHoldPosted();
    } else if (verb == "aim.shotprobe") {
        out << "aim.shotprobe result=" << (Valued(args) ? SetShotProbe(static_cast<unsigned>(arg(1, 0))) : 0)
            << ShotProbeReport();
    } else if (verb == "aim.shot") {
        // Bare `aim.shot` only reports (and installs the observers, so a shot
        // with the lane off is measured too). 1 = the shot leaves the muzzle
        // along the aim; 0 = the native eye-origin query.
        DWORD result = 0;
        if (Valued(args)) {
            const int value = arg(1, -1);
            result = value == 0 || value == 1 ? SetShotRay(static_cast<unsigned>(value)) : ERROR_INVALID_PARAMETER;
        }
        out << "aim.shot result=" << result << ShotRayReport();
    } else if (verb == "aim.beam") {
        // Q-Beam: the last beam (damage ray + drawn beam) against the muzzle,
        // the aim and the amber marker. Observation only.
        out << "aim.beam result=0" << BeamReport();
    } else if (verb == "near.fx") {
        // Bare `near.fx` only reports. 1 = DrawNear particles in the weapon's
        // camera space (default), 0 = native, for A/B.
        DWORD result = 0;
        if (Valued(args)) {
            const int value = arg(1, -1);
            result = value == 0 || value == 1 ? SetNearParticleStereo(static_cast<unsigned>(value))
                                              : ERROR_INVALID_PARAMETER;
        }
        out << "near.fx result=" << result << " " << NearParticleStereoReport();
    } else if (verb == "near.fxbridge") {
        out << "near.fxbridge result=" << (Valued(args) ? SetNearParticleBridge(static_cast<unsigned>(arg(1, 1))) : 0)
            << " " << NearParticleStereoReport();
    } else if (verb == "near.fxhide") {
        out << "near.fxhide result=" << (Valued(args) ? SetNearParticleHide(static_cast<unsigned>(arg(1, 1))) : 0)
            << NoValue(args);
    } else if (verb == "dbg.report") {
        out << "dbg.report result=0" << DebugOverlayReport();
    } else if (verb == "dbg.marks") {
        out << "dbg.marks result=0\n" << DebugOverlayMarks();
    } else {
        return false;
    }
    return true;
}

// The world reticle (ReticleFollow.h). Kept out of Execute's chain for the same
// C1061 reason as ExecutePose.
bool ExecuteReticle(const std::vector<std::string>& args, std::ostringstream& out)
{
    const std::string& verb = args[0];
    const auto arg = [&](std::size_t i, int fallback) {
        int value = fallback;
        if (i < args.size()) { ParseInt(args[i], value); }
        return value;
    };
    if (verb == "aim.world") {
        // Prey's crosshair at the depth it marks (1), or on the HUD panel (0).
        // Saved by VR Options (reticle_world).
        const auto result = Valued(args) ? SetReticleInWorld(static_cast<unsigned>(arg(1, 1))) : 0;
        out << "aim.world result=" << result << ReticleWorldReport() << " scene:" << SceneQueryReport();
    } else if (verb == "aim.reticlesize") {
        const auto result = Valued(args) ? SetReticleSizePercent(static_cast<unsigned>(arg(1, 100))) : 0;
        out << "aim.reticlesize result=" << result << ReticleWorldReport();
    } else {
        return false;
    }
    return true;
}

// Rung 1b, the RT_RenderScene double submission (DualSceneStereo.h). Kept out
// of Execute's chain for the same C1061 reason as ExecutePose.
bool ExecuteDualCommand(const std::vector<std::string>& args, std::ostringstream& out)
{
    const std::string& verb = args[0];
    const auto arg = [&](std::size_t i, int fallback) {
        int value = fallback;
        if (i < args.size()) {
            ParseInt(args[i], value);
        }
        return static_cast<unsigned int>(value);
    };
    if (verb == "dual.observe") {
        // Pass-through logging of RT_RenderScene submissions.
        out << "dual.observe result=" << (Valued(args) ? SetDualSceneObserve(arg(1, 1)) : 0) << DualSceneReport();
    } else if (verb == "dual.mode") {
        // 0 off, 1 the same-camera control, 2 per-eye cameras. Second argument:
        // frame budget.
        out << "dual.mode result=" << (Valued(args) ? SetDualSceneMode(arg(1, 0), arg(2, 0)) : 0) << DualSceneReport();
    } else if (verb == "dual.ipd") {
        out << "dual.ipd result=" << (Valued(args) ? SetDualSceneIpdMillimetres(arg(1, 64)) : 0) << DualSceneReport();
    } else if (verb == "dual.vr") {
        // Headset A/B: 1 both eyes per frame, 0 the alternate-eye path. Ordered so
        // no both-eyes contract is ever published without its second submission.
        DWORD result = 0;
        if (Valued(args) && arg(1, 1) != 0) {
            result = SetDualSceneMode(3, 0);
            if (result == 0) SetDualStereoContract(1);
        } else if (Valued(args)) {
            SetDualStereoContract(0);
            result = SetDualSceneMode(0, 0);
        }
        out << "dual.vr result=" << result << DualSceneReport();
    } else if (verb == "dual.capture") {
        out << "dual.capture result=" << RequestDualSceneCapture(arg(1, 0));
    } else if (verb == "dual.overlay") {
        // Step 2 A/B: 1 repeats the after-world overlays into the held eye, 0
        // leaves them in the second eye only. No argument: report.
        out << "dual.overlay result=" << (Valued(args) ? SetDualOverlayRedraw(arg(1, 1)) : 0)
            << DualOverlayReport();
    } else if (verb == "dual.report") {
        out << "dual.report result=0" << DualSceneReport() << DualOverlayReport();
    } else if (verb == "cull.report") {
        // docs/CULL-CAMERA-CRASH-2026-10-17.md: the read-only pass-camera reset watch.
        out << "cull.report result=0" << CullCameraWatchReport();
    } else if (verb == "cull.watch") {
        out << "cull.watch result=" << (Valued(args) ? SetCullCameraWatch(arg(1, 1)) : 0)
            << CullCameraWatchReport();
    } else {
        return false;
    }
    return true;
}

// One verb, one operation. Deliberately not a name-to-export lookup: that would
// be a call-anything primitive whose argument is a text file.
void Execute(const std::vector<std::string>& args, std::ostringstream& out)
{
    const std::string& verb = args[0];
    const auto arg = [&](std::size_t i, int fallback) {
        int value = fallback;
        if (i < args.size()) {
            ParseInt(args[i], value);
        }
        return value;
    };

    if (ExecutePose(args, out) || ExecuteCarryCommand(args, out) || ExecuteGrenadeCommand(args, out) || ExecuteArmsCommand(args, out) ||
        ExecuteWristCommand(args, out) || ExecuteDualCommand(args, out) || ExecuteReticle(args, out)) {
        return;
    }
    if (verb == "vr.options") {
        if(Valued(args))RequestVrOptions(arg(1,0)!=0);
        out << "vr.options" << VrOptionsReport() << PsychoscopeReport() << BodyEquipmentReport();
    } else if (verb == "vr.enable") {
        EnableVrMode(); out << VrModeReport();
    } else if (verb == "vr.disable") {
        DisableVrMode(); out << VrModeReport();
    } else if (verb == "vr.status") {
        out << VrModeReport();
    } else if (verb == "vr.recenter") {
        out << "vr.recenter result=" << RecenterHeadTracking();
    } else if (verb == "hud.var") {
        // hud.var [path ...]       -- queue reads; with no path, the last result
        // hud.setvar <path> <num>  -- queue a write (SV_Normal), then a read
        DWORD result = 0;
        for (std::size_t i = 1; i < args.size() && result == 0; ++i) { result = QueueHudVariable(args[i], false, 0); }
        out << "hud.var result=" << result << ' ' << HudVariableReport();
    } else if (verb == "hud.setvar" && args.size() >= 3) {
        out << "hud.setvar result=" << QueueHudVariable(args[1], true, std::atof(args[2].c_str()));
    } else if (verb == "hud.layer") {
        const auto result=Valued(args)?SetHudLayerEnabled(arg(1,0)):0;
        out << "hud.layer result=" << result << ' ' << HudLayerReport();
    } else if (verb == "input.hotkeys") {
        // **Nothing started this thread.** SetHotkeysEnabled was reachable only
        // through an exported entry point that the startup path never calls, so
        // every keyboard binding in the bridge -- including the ~ console key --
        // was dead in the player package. Building a binding and never checking
        // the path that arms it is the same mistake twice in one session.
        out << "input.hotkeys result=" << (Valued(args)?SetHotkeysEnabled(arg(1,1)):0)
            << " consoleToggles=" << HotkeyConsoleToggles();
    } else if (verb == "ui.stereo") {
        out << "ui.stereo result=" << (Valued(args)?SetInventoryStereoEnabled(arg(1,0)):0)
            << ' ' << InventoryStereoReport();
    } else if (verb == "ui.depth") {
        out << "ui.depth result=" << (Valued(args)?SetInventoryDepthPercent(arg(1,25)):0)
            << ' ' << InventoryStereoReport();
    } else if (verb == "ui.inventory") {
        // Opt-in separate inventory layer; native redirection is additionally
        // gated on a compatible, recently serviced XR swapchain consumer.
        out << "ui.inventory result=" << (Valued(args)?SetInventoryCaptureEnabled(arg(1,0)):0)
            << ' ' << HudLayerReport()
            // Frames actually submitted through the separate inventory swapchain.
            // captured= above says the redirect happened; this says the layer
            // reached xrEndFrame -- the two can differ, and that gap is the
            // upload-refusal fallback.
            << " layerFrames=" << InventoryLayerFrameCount();
    } else if (verb == "ui.panel") {
        out << "ui.panel result=" << (Valued(args) ? SetUiPanelMode(arg(1,1)) : 0)
            << " mode=" << UiPanelMode() << ' ' << VrModeReport();
    } else if (verb == "ui.pointer") {
        out << "ui.pointer result=" << (Valued(args)?SetUiPointerHand(arg(1,1)):0) << ' ' << UiPointerReport();
    } else if (verb == "ui.scale") {
        // The stored percent was already echoed here; `margin` and `guide` join
        // it because they are what actually decide the panel's size. A wearer
        // reading "percent=160" while the panel refused to grow had no way to
        // see that the fit margin was shrinking it straight back off, which is
        // the whole reason ui.scale looked as though it capped at 130.
        if (Valued(args)) { out << "ui.scale result=" << SetUiScalePercent(arg(1, 100)); }
        else { out << "ui.scale result=0"; }
        out << " percent=" << UiScalePercent()
            << " margin=" << UiFitMarginPercent()
            << " guide=" << UiGuideEnabled();
    } else if (verb == "ui.text") {
        // Text panel distance in cm (100..200, default 140), at the HUD panel's
        // physical size. No argument: report.
        out << "ui.text result=" << (Valued(args) ? SetTextPanelDistanceCm(arg(1, 140)) : 0)
            << " cm=" << TextPanelDistanceCm();
    } else if (verb == "ui.distance") {
        // Native menu panel distance in cm (100..400, default 200), at the same
        // apparent size. The VR Options menu saves it. No argument: report.
        out << "ui.distance result=" << (Valued(args) ? SetUiDistanceCm(arg(1, 200)) : 0)
            << " cm=" << UiDistanceCm();
    } else if (verb == "ui.offset") {
        // Native menu panel offset in whole degrees: right (-90..90), up
        // (-45..45). Both or neither: one value, or one that does not parse,
        // is refused rather than guessed. The VR Options menu saves it. No
        // argument: report.
        DWORD result = 0;
        if (Valued(args)) {
            int right = 0, up = 0;
            result = args.size() == 3 && ParseInt(args[1], right) && ParseInt(args[2], up)
                ? SetUiOffsetDegrees(right, up) : ERROR_INVALID_PARAMETER;
        }
        out << "ui.offset result=" << result
            << " right=" << UiOffsetRightDegrees() << " up=" << UiOffsetUpDegrees();
    } else if (verb == "ui.margin") {
        // The real size ceiling. ui.scale above ~130 was being absorbed by the
        // fit loops shrinking the panel back inside 72% of the reported frustum.
        out << "ui.margin result=" << (Valued(args) ? SetUiFitMarginPercent(arg(1, 72)) : 0)
            << " percent=" << UiFitMarginPercent();
    } else if (verb == "ui.guide") {
        // Reports when given no argument. It used to default the argument to 1,
        // which was harmless while the card was on by default and would now mean
        // a bare `ui.guide` silently switched it back on.
        out << "ui.guide result=" << (Valued(args) ? SetUiGuideEnabled(arg(1, 0)) : 0)
            << " enabled=" << UiGuideEnabled();
    } else if (verb == "ui.curve") {
        out << "ui.curve result=" << (Valued(args) ? SetUiCurveDegrees(arg(1,35)) : 0)
            << " degrees=" << UiCurveDegrees();
    } else if (verb == "observer") {
        const DWORD status = Valued(args) ? SetFrameObserverEnabled(arg(1, 1)) : FrameObserverStatus();
        out << "observer status=" << ObserverStatusName(status) << "(" << status << ")";
    } else if (verb == "xr.runtime" && args.size() >= 2) {
        out << "xr.runtime result=" << SetXrRuntimeManifest(args[1].c_str());
    } else if (verb == "xr.srgb") {
        out << "xr.srgb result=" << (Valued(args) ? SetXrPreferSrgbFormat(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "xr.start") {
        const DWORD status = StartXrSession();
        out << "xr.start status=" << XrStatusName(status) << "(" << status << ")";
    } else if (verb == "xr.native") {
        out << "xr.native result=" << (Valued(args) ? SetNativeProjection(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "xr.stereo") {
        // Millimetres and degrees, so the file never carries a float.
        const float ipd = static_cast<float>(arg(1, 64)) / 1000.0f;
        const float halfFov = static_cast<float>(arg(2, 50));
        out << "xr.stereo result=" << (Valued(args) ? SetSyntheticStereo(ipd, halfFov) : 0) << NoValue(args);
    } else if (verb == "xr.mirrorfov") {
        // How much of the headset's view the flat mirror's image spans, as a
        // percentage of Prey's own declared field. 100 is the honest projection.
        // Only affects frames with no held eye pair -- the menu, and anything
        // else not rendered as alternating eyes.
        out << "xr.mirrorfov result=" << (Valued(args) ? SetMirrorFovPercent(static_cast<unsigned int>(arg(1, 100))) : 0)
            << " percent=" << MirrorFovPercent();
    } else if (verb == "xr.submit") {
        if (Valued(args)) {
            const DWORD status = SetXrStereoSubmission(arg(1, 1));
            out << "xr.submit status=" << XrStatusName(status) << "(" << status << ")";
        } else {
            out << "xr.submit result=0" << NoValue(args);
        }
    } else if (verb == "view.observe") {
        out << "view.observe result=" << (Valued(args) ? SetViewHookObserving(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "view.calibrate") {
        out << "view.calibrate result=" << RecenterHeadTracking(true) << ' ' << HeadHeightReport();
    } else if (verb == "view.height") {
        out << "view.height " << HeadHeightReport();
    } else if (verb == "view.recenter") {
        out << "view.recenter result=" << RecenterHeadTracking();
    } else if (verb == "view.apply") {
        out << "view.apply result=" << (Valued(args) ? SetViewHookApplying(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "view.position") {
        out << "view.position result=" << (Valued(args) ? SetViewPositionApplying(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "near.fov") {
        // Bare command reports without changing/rearming a faulted override.
        // Decidegrees: `near.fov 1234` is 123.4 degrees, explicit 0 disables.
        DWORD result = 0;
        if (Valued(args)) {
            int value = 0;
            // An explicit value is a choice: stop following the world FOV.
            SetNearFovFollow(0);
            result = args.size() == 2 && ParseInt(args[1], value) && value >= 0
                ? SetNearFovDeciDegrees(static_cast<unsigned int>(value)) : 1u;
        }
        out << "near.fov result=" << result
            << " deciDegrees=" << NearFovDeciDegrees()
            << " applied=" << NearFovAppliedCount()
            << " refused=" << NearFovRefusedCount()
            << " observedDeciDegrees=" << NearFovObservedDeciDegrees()
            << " observedValid=" << NearFovObservedValid()
            << " faulted=" << NearFovFaulted()
            << " thread=" << NearFovThreadId();
    } else if (verb == "near.enable") {
        out << "near.enable result=" << (Valued(args) ? SetNearViewStereo(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "near.halfipd") {
        out << "near.halfipd result=" << (Valued(args) ? SetNearViewHalfIpdMillimetres(arg(1, 32)) : 0)
            << NoValue(args);
    } else if (verb == "near.lineage") {
        out << "near.lineage result=" << (Valued(args) ? ArmNearViewLineage(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "near.dump") {
        out << "near.dump result=" << DumpNearViewLineage();
    } else if (verb == "near.zero") {
        out << "near.zero result=" << (Valued(args) ? SetNearViewZeroDeltaControl(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "ik.mode") {
        // 0 off, 1 observe (identify the rig, read its ADIK table, write
        // nothing), 2 apply. Mutually exclusive with hand.mode 2.
        out << "ik.mode result=" << (Valued(args) ? SetAnimIkMode(arg(1, 0)) : 0);
        const unsigned int mode = AnimIkMode();
        out << " mode=" << mode
            << (mode == 0 ? "(off)" : mode == 1 ? "(observe)" : mode == 2 ? "(apply)" : "(unknown)");
    } else if (verb == "ik.test") {
        out << "ik.test result=" << (Valued(args) ? SetAnimIkTestOffsetMillimetres(arg(1, 0), arg(2, 0), arg(3, 0)) : 0)
            << NoValue(args);
    } else if (verb == "ik.drive") {
        out << "ik.drive result=" << (Valued(args) ? SetAnimIkControllerDrive(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "ik.calibrate") {
        out << "ik.calibrate result=" << CalibrateAnimIk();
    } else if (verb == "ik.align") {
        DWORD result = 0;
        if (Valued(args)) {
            int value = 0;
            result = args.size() == 2 && ParseInt(args[1], value) && value >= 0
                ? SetAnimIkWeaponAlignment(static_cast<unsigned int>(value)) : 1u;
        }
        out << "ik.align result=" << result << AnimIkWeaponAlignmentReport();
    } else if (verb == "ik.joints") {
        out << "ik.joints result=" << (Valued(args) ? SetAnimIkJointSignature(arg(1, 101)) : 0) << NoValue(args);
    } else if (verb == "ik.reach") {
        out << "ik.reach result=" << (Valued(args) ? SetAnimIkReachPercent(arg(1, 100)) : 0)
            << " percent=" << AnimIkReachPercent();
    } else if (verb == "ik.hands") {
        out << "ik.hands result=" << (Valued(args) ? SetAnimIkHands(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "ik.anchor") {
        out << "ik.anchor result=" << (Valued(args) ? SetAnimIkCameraAnchor(arg(1, 1)) : 0) << NoValue(args)
            << " camera=" << AnimIkAnchorCameraCount()
            << " reticle=" << AnimIkAnchorReticleCount()
            << " missing=" << AnimIkAnchorMissingCount();
    } else if (verb == "ik.trace") {
        if (Valued(args)) { out << "ik.trace result=" << SetAnimIkTrace(arg(1, 1)); }
        else { out << "ik.trace result=0"; }
        const std::string trace = AnimIkTraceReport();
        // Persisted: results.txt is overwritten by the next command, and a
        // per-frame transformation chain that only exists there cannot be
        // compared across a sweep.
        lifecycle::Log("preyvr_ik_trace" + trace);
        out << trace;
    } else if (verb == "ik.dump") {
        out << "ik.dump result=" << DumpAnimIk();
    } else if (verb == "hand.mode") {
        out << "hand.mode result=" << (Valued(args) ? SetHandRigTakeoverMode(arg(1, 0)) : 0);
        // Named, because "1" reads like "on" and is actually the control that
        // deliberately changes nothing.
        const unsigned int mode = HandRigMode();
        const char* name = mode == 0 ? "off"
                         : mode == 1 ? "passthrough_changes_nothing"
                         : mode == 2 ? "apply"
                                     : "unknown";
        out << " mode=" << mode << "(" << name << ")";
    } else if (verb == "hand.joint") {
        out << "hand.joint result=" << (Valued(args) ? SetHandRigJoint(arg(1, 0)) : 0) << NoValue(args);
    } else if (verb == "hand.offset") {
        out << "hand.offset result="
            << (Valued(args) ? SetHandRigOffsetMillimetres(arg(1, 0), arg(2, 0), arg(3, 0)) : 0) << NoValue(args);
    } else if (verb == "hand.right") {
        out << "hand.right result=" << (Valued(args) ? SetHandRigRightJoint(arg(1, 0)) : 0) << NoValue(args);
    } else if (verb == "hand.left") {
        out << "hand.left result=" << (Valued(args) ? SetHandRigLeftJoint(arg(1, 0)) : 0) << NoValue(args);
    } else if (verb == "hand.drive") {
        out << "hand.drive result=" << (Valued(args) ? SetHandRigControllerDrive(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "hand.wrist") {
        out << "hand.wrist result=" << (Valued(args) ? SetHandRigWristDrive(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "hand.turnyaw") {
        // Tenths of a degree, so 1800 is a half turn. Reported back so a wearer
        // reading the channel can see what is actually set.
        out << "hand.turnyaw result=" << (Valued(args) ? SetHandRigTurnYaw(arg(1, 0)) : 0)
            << " deciDegrees=" << HandRigTurnYawDeciDegrees();
    } else if (verb == "hand.calibrate") {
        out << "hand.calibrate result=" << CalibrateHandRig();
    } else if (verb == "hand.scale") {
        out << "hand.scale result=" << (Valued(args) ? SetHandRigScalePercent(arg(1, 100)) : 0) << NoValue(args);
    } else if (verb == "hand.character" && args.size() >= 2) {
        unsigned long long pointer = 0;
        if (!ParseU64(args[1], pointer)) {
            gRejected.fetch_add(1, std::memory_order_relaxed);
            out << "hand.character result=rejected detail=unparsable";
            return;
        }
        out << "hand.character result="
            << SetHandRigCharacterPtr(reinterpret_cast<void*>(pointer));
    } else if (verb == "console" && args.size() >= 2) {
        // Rebuilt from the tokens, then through the existing fail-closed
        // allowlist. This channel adds no reach to the console.
        std::string command = args[1];
        for (std::size_t i = 2; i < args.size(); ++i) {
            command += " " + args[i];
        }
        // **The console queue holds exactly one command.** Firing them back to
        // back returns `busy` for every one after the first, which is what
        // happened on this channel's first run: motion blur landed and the two
        // commands behind it were silently dropped with a code nobody read.
        //
        // The observer drains the queue once per frame, so retrying briefly is
        // enough. Bounded, because a game that has stopped rendering will never
        // drain it and this must not spin forever.
        DWORD result = QueueConsoleCommand(command.c_str());
        for (int attempt = 0; attempt < 40 && result == 4u; ++attempt) {
            Sleep(25);
            result = QueueConsoleCommand(command.c_str());
        }
        out << "console result=" << result
            << " command=\"" << command << "\"";
    } else if (verb == "psi.target") {
        const auto result=Valued(args)?SetPsiTargetMode(arg(1,0)):0;
        out<<"psi.target result="<<result<<PsiMedkitReport();
    } else if (verb == "medkit.slot") {
        const auto result=Valued(args)?SetMedkitSlot(arg(1,0)):0;
        out<<"medkit.slot result="<<result<<PsiMedkitReport();
    } else if (verb == "physical") {
        out<<"physical"<<PhysicalInteractionsReport();
    } else if (verb == "aim.scene") {
        if(Valued(args))SetSceneReticle(arg(1,0));
        out<<"aim.scene"<<SceneQueryReport();

    } else if (verb == "haptics") {
        if(Valued(args))SetHapticsEnabled(arg(1,1));
        if(args.size()>2)SetHapticStrength(arg(2,70));
        out<<"haptics"<<HapticsReport();
    } else if (verb == "aim.reticle") {
        // Moves Prey's own crosshair to where the controller points, so the
        // symbol a player aims with agrees with the ray gameplay uses. One
        // screen position exists, not two, so it is projected through the
        // current view rather than per eye; that is a real limitation, not an
        // oversight, and a genuinely stereo reticle needs a mod-drawn one.
        out << "aim.reticle result=" << (Valued(args) ? SetReticleFollowEnabled(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "aim.reticleconverge") {
        // The distance the crosshair marks along the firing ray, in mm. Only
        // matters once a barrel calibration puts the origin off the eye: until
        // then reticleOriginMm reads 0 and the setting changes nothing, which is
        // the honest reading rather than a knob that appears to do something.
        out << "aim.reticleconverge result="
            << (Valued(args) ? SetReticleConvergenceMillimetres(arg(1, 10000)) : 0)
            << " mm=" << ReticleConvergenceMillimetres();
    } else if (verb == "aim.reticledispatch") {
        // Separates the two halves of the reticle write. Off writes only the
        // cached field; on also dispatches the movie call the engine's own reset
        // makes. If the crosshair does not move, this says which half to blame.
        out << "aim.reticledispatch result=" << (Valued(args) ? SetReticleDispatchEnabled(arg(1, 1)) : 0);
        if (Valued(args)) { out << " value=" << arg(1, 1); } else { out << NoValue(args); }
    } else if (verb == "aim.reticlecanvas") {
        // aim.reticlecanvas 0|1|2 -- raw viewport / R-118 model / native
        out << "aim.reticlecanvas result=" << (Valued(args) ? SetReticleCanvasMode(arg(1, 1)) : 0)
            << " mode=" << ReticleCanvasMode()
            << " native=" << ReticleNativeCanvasCount()
            << " nativeFailed=" << ReticleNativeCanvasFailedCount();
    } else if (verb == "aim.reticlestage") {
        out << "aim.reticlestage result=" << (Valued(args) ? SetReticleStageScaleMode(arg(1, 0)) : 0) << NoValue(args);
    } else if (verb == "aim.twohand") {
        out << "aim.twohand result=" << (Valued(args)?SetTwoHandedAim(arg(1,1)):0)
            << TwoHandedAimStatus();
    } else if (verb == "aim.bodyyaw") {
        // 1 rotates head-relative offsets by the BODY yaw (camera - head), 0 by
        // the camera yaw. 0 makes the hand and weapon swing with the headset.
        out << "aim.bodyyaw result=" << (Valued(args) ? SetAimBodyYaw(arg(1, 1)) : 0)
            << " value=" << AimBodyYawEnabled();
    } else if (verb == "aim.origin") {
        // Diagnostic controller-aim origin; this does not establish a muzzle
        // transform or remove projectile convergence from the authored helper.
        out << "aim.origin result=" << (Valued(args) ? SetAimOriginFromHand(arg(1, 1)) : 0)
            << " mode=" << AimOriginMode()
            << (AimOriginMode() == 2 ? "(muzzle)" : AimOriginMode() == 1 ? "(hand)" : "(native)");
    } else if (verb == "aim.calibratebarrel") {
        out << "aim.calibratebarrel result=" << CalibrateWeaponBarrel()
            << " offsetMm=" << WeaponBarrelOffsetMillimetres(0) << ","
            << WeaponBarrelOffsetMillimetres(1) << "," << WeaponBarrelOffsetMillimetres(2);
    } else if (verb == "aim.enable") {
        // The detached-aim lane, reachable from the channel at last. H-004 is
        // reproduced against two native consumers -- the wrench contact moved
        // 0.127165 units and the interaction selector committed a different
        // entity -- but both used synthetic angle offsets driven from a
        // debugger. This is what lets a *controller* drive it.
        out << "aim.enable result=" << (Valued(args) ? SetAimTakeoverEnabled(arg(1, 1)) : 0) << NoValue(args)
            << " applied=" << AimTakeoverAppliedCount()
            << " nativeMagMilli=" << AimTakeoverNativeDirectionMagnitude();
    } else if (verb == "weapon.observe") {
        out << "weapon.observe result=" << (Valued(args) ? SetWeaponAttachmentObserving(arg(1, 1)) : 0)
            << NoValue(args);
    } else if (verb == "weapon.offset") {
        out << "weapon.offset result="
            << (Valued(args) ? SetWeaponOffsetMillimetres(arg(1, 0), arg(2, 0), arg(3, 0)) : 0) << NoValue(args);
    } else if (verb == "weapon.apply") {
        out << "weapon.apply result=" << (Valued(args) ? SetWeaponOffsetEnabled(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "weapon.rotate") {
        out << "weapon.rotate result=" << (Valued(args) ? SetWeaponRotationDrive(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "weapon.calibrate") {
        out << "weapon.calibrate result=" << CalibrateWeaponRotation();
    } else if (verb == "capture.hud") {
        out << "capture.hud result=" << RequestFrameCapture(static_cast<std::uint32_t>(arg(1,900)),true);
    } else if (verb == "capture.inventory") {
        out << "capture.inventory result=" << RequestInventoryPairCapture(static_cast<std::uint32_t>(arg(1,1000)));
    } else if (verb == "capture") {
        // A one-shot readback of the backbuffer -- which is exactly the image
        // submitted to the headset -- so "is X visible in the HMD" becomes a
        // file to look at rather than something a wearer has to be asked.
        out << "capture result=" << RequestFrameCapture(static_cast<std::uint32_t>(arg(1, 1)))
            << " completed=" << CompletedFrameCaptureCount();
    } else if (verb == "hud.call") {
        // hud.call <function> <x> [y] -- queue a main-thread HUD dispatch.
        if (args.size() < 3) {
            out << "hud.call result=rejected detail=need_function_and_value";
        } else {
            const float x = static_cast<float>(std::atof(args[2].c_str()));
            const float y = args.size() >= 4 ? static_cast<float>(std::atof(args[3].c_str())) : 0.0f;
            const DWORD result = CallHudFunction(args[1].c_str(), x, y, args.size() >= 4);
            out << "hud.call result=" << result << " queued=" << (result == 0 ? 1 : 0)
                << " fn=" << args[1];
        }
    } else if (verb == "hud.fit" && args.size() >= 2) {
        // hud.fit 1 -- fit the HUD canvas inside the frame (clear bMax)
        // hud.fit 0 -- restore the engine's cover fit (set bMax)
        const bool fitInside = arg(1, 1) != 0;
        out << "hud.fit result=" << QueueHudFit(!fitInside) << " queued=1"
            << " fitInside=" << (fitInside ? 1 : 0);
    } else if (verb == "xr.frustum") {
        out << "xr.frustum result=" << (Valued(args) ? SetRuntimeFrustum(arg(1, 1)) : 0) << NoValue(args)
            << " used=" << RuntimeFrustumUsedCount()
            << " missing=" << RuntimeFrustumMissingCount()
            << XrCoverageReport();
    } else if (verb == "xr.timing") {
        // xr.timing            -- report
        // xr.timing 1 [hz]     -- arm and reset, default 90 Hz deadline
        if (Valued(args)) {
            out << "xr.timing result="
                << SetXrTimingEnabled(arg(1, 1), static_cast<unsigned int>(arg(2, 90)));
        } else {
            out << "xr.timing result=0";
        }
        const std::string report = XrTimingReport();
        // **Persisted, because results.txt is overwritten by the next command.**
        // A quantitative run whose numbers exist only in a file the poller
        // clobbers is a run that cannot be re-read or audited.
        lifecycle::Log("preyvr_timing" + report);
        out << report;
    } else if (verb == "xr.coverage") {
        out << "xr.coverage result=0" << XrCoverageReport();
    } else if (verb == "move.act") {
        out << "move.act result=" << (Valued(args) ? SetInteractionEnabled(arg(1, 1)) : 0)
            << InteractionReport();
    } else if (verb == "move.bind" && args.size() >= 3) {
        // move.bind <slot 0-3> <keyId>  -- 0 interact, 1 inventory, 2 jump, 3 crouch
        const unsigned int slot = static_cast<unsigned int>(arg(1, 0));
        const int keyId = static_cast<int>(arg(2, 0));
        out << "move.bind result=" << SetInteractionBinding(slot, keyId)
            << InteractionReport();
    } else if (verb == "menu.gate") {
        if (Valued(args)) {
            SetMenuNavigationGate(arg(1, 1));
            out << "menu.gate result=0 value=" << arg(1, 1);
        } else {
            out << "menu.gate result=0" << NoValue(args);
        }
        out << " menuOpen=" << (HudMenuIsOpen() ? 1 : 0)
            << " samples=" << HudMenuStateSampleCount()
            << " suppressed=" << MenuNavigationSuppressedCount();
    } else if (verb == "hud.probe") {
        // hud.probe            -- report the last probe
        // hud.probe <x> <y>    -- queue one at that viewport fraction
        if (args.size() >= 3) {
            const float x = static_cast<float>(std::atof(args[1].c_str()));
            const float y = static_cast<float>(std::atof(args[2].c_str()));
            out << "hud.probe result=" << QueueHudProbe(x, y) << " queued=1";
        } else {
            out << "hud.probe result=0 " << HudLastProbe();
        }
    } else if (verb == "frame.capture") {
        out << "frame.capture result=" << (Valued(args) ? SetRenderFrameCapture(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "frame.character" && args.size() >= 2) {
        unsigned long long pointer = 0;
        if (!ParseU64(args[1], pointer)) {
            out << "frame.character result=2 detail=parse";
        } else {
            out << "frame.character result=" << SetRenderFrameOverrideCharacter(pointer);
        }
    } else if (verb == "frame.offset") {
        out << "frame.offset result="
            << (Valued(args) ? SetRenderFrameOffsetMillimetres(arg(1, 0), arg(2, 0), arg(3, 0)) : 0) << NoValue(args);
    } else if (verb == "frame.apply") {
        out << "frame.apply result=" << (Valued(args) ? SetRenderFrameOverrideEnabled(arg(1, 1)) : 0) << NoValue(args)
            << " applied=" << RenderFrameOverrideAppliedCount()
            << " refused=" << RenderFrameOverrideRefusedCount();
    } else if (verb == "frame.propagate") {
        out << "frame.propagate result="
            << (Valued(args) ? SetRenderFrameOverridePropagates(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "frame.near") {
        // Dumps the capture table with RenderCHR's own near verdict per slot
        // (R-089), so identifying the near character no longer needs an injector
        // attached to the running game.
        out << "frame.near result=0";
        unsigned int listed = 0;
        for (unsigned int i = 0; i < 8; ++i) {
            unsigned long long character = 0;
            bool nearest = false;
            if (!RenderFrameSlot(i, &character, &nearest)) {
                continue;
            }
            out << " slot" << i << "=0x" << std::hex << character << std::dec
                << ",near=" << (nearest ? 1 : 0);
            ++listed;
        }
        out << " listed=" << listed;
    } else if (verb == "xr.resolution") {
        // The measured chain: what the runtime asks for, what Prey renders, what
        // is held, and what is submitted. Ratios are given so "soft" has a
        // number; the scene's own render target is NOT covered here.
        const DWORD recW = XrResolutionChain(0), recH = XrResolutionChain(1);
        const DWORD backW = XrResolutionChain(4), backH = XrResolutionChain(5);
        out << "xr.resolution result=0"
            << " recommended=" << recW << "x" << recH
            << " max=" << XrResolutionChain(2) << "x" << XrResolutionChain(3)
            << " backbuffer=" << backW << "x" << backH
            << " heldEye=" << XrResolutionChain(6) << "x" << XrResolutionChain(7)
            << " submitted=" << XrResolutionChain(8) << "x" << XrResolutionChain(9)
            << " recommendedSamples=" << XrResolutionChain(10)
            << " viewsDiffer=" << XrResolutionChain(11)
            << " heldFormat=" << XrResolutionChain(12)
            << " views=" << XrResolutionChain(13)
            << " sizeMismatch=" << XrResolutionChain(14);
        if (recW != 0 && recH != 0 && backW != 0 && backH != 0) {
            // Per-eye pixels the runtime wants against per-eye pixels it gets.
            const double want = static_cast<double>(recW) * recH;
            const double got = static_cast<double>(backW) * backH;
            out << " pixelRatioPercent=" << static_cast<int>(got / want * 100.0 + 0.5)
                << " widthRatioPercent=" << static_cast<int>(
                       static_cast<double>(backW) / recW * 100.0 + 0.5)
                << " heightRatioPercent=" << static_cast<int>(
                       static_cast<double>(backH) / recH * 100.0 + 0.5);
        }
    } else if (verb == "move.mode") {
        // 0 off, 1 observe (hook the analog handlers, attribute every call,
        // post nothing), 2 apply. Observe first: it proves the handlers fire
        // and measures the player's own hardware before we add to it.
        out << "move.mode result=" << (Valued(args) ? SetMoveLaneMode(arg(1, 0)) : 0);
        const unsigned int mode = MoveLaneMode();
        out << " mode=" << mode
            << (mode == 0 ? "(off)" : mode == 1 ? "(observe)" : mode == 2 ? "(apply)" : "(unknown)");
    } else if (verb == "move.snap") {
        out << "move.snap result=" << (Valued(args)?SetSnapTurnDegrees(arg(1,SnapTurnDegrees())):0) << " degrees=" << SnapTurnDegrees();
    } else if (verb == "move.headrelative") {
        out << "move.headrelative result=" << (Valued(args)?SetHeadRelativeMovement(arg(1,2)):0)
            << " enabled=" << HeadRelativeMovementEnabled();
    } else if (verb == "move.scales") {
        out << "move.scales result=" << (Valued(args) ? SetMovementAxisScales(arg(1,100),arg(2,100)) : 0)
            << " strafe=" << MovementStrafePercent() << " backward=" << MovementBackwardPercent();
    } else if (verb == "move.turn") {
        out << "move.turn result=" << (Valued(args) ? SetTurnLaneEnabled(arg(1, 1)) : 0)
            << " enabled=" << TurnLaneEnabled();
    } else if (verb == "move.turndeadzone") {
        out << "move.turndeadzone result=" << (Valued(args) ? SetTurnLaneDeadzone(arg(1, 15)) : 0)
            << " hundredths=" << TurnLaneDeadzone();
    } else if (verb == "move.turnscale") {
        out << "move.turnscale result=" << (Valued(args) ? SetTurnLaneScale(arg(1, 100)) : 0) << NoValue(args);
    } else if (verb == "move.fire") {
        out << "move.fire result=" << (Valued(args) ? SetFireLaneEnabled(arg(1, 1)) : 0)
            << " enabled=" << FireLaneEnabled();
    } else if (verb == "move.all") {
        // The whole control scheme in one command: left stick moves, right stick
        // turns, right trigger fires, grip and face buttons interact.
        if (Valued(args)) {
            const unsigned int on = static_cast<unsigned int>(arg(1, 1)) != 0u ? 1u : 0u;
            const DWORD m = SetMoveLaneMode(on ? 2u : 0u);
            const DWORD t = SetTurnLaneEnabled(on);
            const DWORD f = SetFireLaneEnabled(on);
            const DWORD a = SetInteractionEnabled(on);
            out << "move.all result=" << (m | t | f | a)
                << " move=" << m << " turn=" << t << " fire=" << f << " act=" << a;
        } else {
            out << "move.all result=0" << NoValue(args);
        }
        out << InteractionReport();
    } else if (verb == "move.deadzone") {
        out << "move.deadzone result=" << (Valued(args) ? SetMoveLaneDeadzone(arg(1, 15)) : 0)
            << " hundredths=" << MoveLaneDeadzone();
    } else if (verb == "input.post") {
        out << "input.post result=" << (Valued(args) ? SetInputPostEnabled(arg(1, 1)) : 0) << NoValue(args);
    } else if (verb == "input.force") {
        out << "input.force result=" << (Valued(args) ? SetInputPostForce(static_cast<unsigned int>(arg(1, 1))) : 0)
            << NoValue(args);
    } else if (verb == "menu") {
        // Menu navigation through the engine's own input layer. `arg(1)` is the
        // MenuAction index; 0=up 1=down 2=left 3=right 4=accept 5=cancel 6=start.
        // A non-zero result means refused -- it can never mean "the menu did not
        // move", because PostInputEvent returns void and the binding that would
        // consume it is a candidate rather than a proven active bind (R-089).
        const DWORD result = PostMenuAction(static_cast<unsigned int>(arg(1, 0)));
        // `result=0` means **queued**. Delivery happens on the engine thread and
        // acceptance is not observable from here at all: PostInputEvent returns
        // void and the binding is a candidate (R-089). Read the screen.
        out << "menu result=" << result << " action=" << arg(1, 0)
            << " queued=" << InputQueueDepthEstimate()
            << " posted=" << InputPostCount()
            << " refused=" << InputPostRefusedCount()
            << " dropped=" << InputQueueDroppedCount()
            << " drainThread=" << InputDrainThreadId();
    } else if (verb == "input.key") {
        const DWORD result = PostRawInput(arg(1, 0), static_cast<unsigned int>(arg(2, 1)),
                                          arg(3, 1000));
        out << "input.key result=" << result << " keyid=" << arg(1, 0)
            << " queued=" << InputQueueDepthEstimate()
            << " posted=" << InputPostCount()
            << " refused=" << InputPostRefusedCount()
            << " dropped=" << InputQueueDroppedCount()
            << " drainThread=" << InputDrainThreadId();
    } else if (verb == "menu.nav") {
        // Arms the controller -> menu binding. Separate from `input.post` on
        // purpose: one authorises posting at all, the other decides whether a
        // controller may produce it.
        if (Valued(args)) {
            SetMenuNavigation(static_cast<unsigned int>(arg(1, 1)));
            out << "menu.nav result=0 value=" << arg(1, 1);
        } else {
            out << "menu.nav result=0" << NoValue(args);
        }
        out << " actions=" << MenuNavigationActionCount();
    } else if (verb == "report") {
        WriteReport(out);
        // results.txt is replaced by the next command. Preserve each requested
        // snapshot so a headset sweep survives beyond its final sample. This
        // runs on the command thread, not on every render frame.
        lifecycle::Log("preyvr_report " + out.str());
    } else if (console::IsAllowlistedCvar(verb)) {
        // **A bare allowlisted name routes to the console.**
        //
        // Without this, typing exactly what the allowlist contains --
        // `ConsoleShow`, `hud_tutorials 0` -- answered "rejected", because the
        // channel's own verb table does not contain them and only the `console`
        // prefix reached the bridge. That is a dead end with no hint in it, and
        // it cost a live test.
        //
        // This is dispatch, not a widening: the identical fail-closed policy
        // still classifies the line, so nothing becomes reachable that was not
        // already reachable through `console <name>`.
        std::string command = verb;
        for (std::size_t i = 1; i < args.size(); ++i) {
            command += " " + args[i];
        }
        DWORD result = QueueConsoleCommand(command.c_str());
        for (int attempt = 0; attempt < 40 && result == 4u; ++attempt) {
            Sleep(25);
            result = QueueConsoleCommand(command.c_str());
        }
        out << "console result=" << result << " command=\"" << command << "\"";
    } else {
        // Reported, not ignored. A typo that silently does nothing is
        // indistinguishable from a mechanism that does not work.
        gRejected.fetch_add(1, std::memory_order_relaxed);
        out << "result=rejected verb=\"" << verb << "\""
            << " detail=not_a_channel_verb_and_not_allowlisted";
        return;
    }
    gProcessed.fetch_add(1, std::memory_order_relaxed);
}

DWORD WINAPI PollThread(LPVOID)
{
    const auto commands = CommandPath();
    const auto results = ResultPath();
    Log("result=0 detail=started commands=\"" + commands.string() +
        "\" results=\"" + results.string() + "\"");
    while (gRunning.load(std::memory_order_acquire)) {
        // Once, as soon as the module is pinned: VR or not (CullCameraWatch.h).
        ServiceCullCameraWatch();
        TickVrMode();
        ServiceVrOptions();
        // Keys and startup progress are serviced at 50 Hz; disk polling stays 5 Hz.
        static unsigned poll=0;
        if (++poll%10!=0) { Sleep(20); continue; }
        std::error_code error;
        if (std::filesystem::exists(commands, error) &&
            std::filesystem::file_size(commands, error) > 0) {
            std::vector<std::string> lines;
            {
                std::ifstream input(commands);
                std::string line;
                while (std::getline(input, line)) {
                    lines.push_back(line);
                }
            }
            // Truncate **before** executing, so a command that crashes or hangs
            // is not replayed on the next poll and on every poll after it.
            { std::ofstream clear(commands, std::ios::trunc); }

            std::ostringstream out;
            for (const std::string& line : lines) {
                const auto args = Split(line);
                if (args.empty() || args[0].empty() || args[0][0] == '#') {
                    continue;
                }
                Execute(args, out);
                out << '\n';
            }
            std::ofstream(results, std::ios::trunc) << out.str();
        }
        Sleep(20);
    }
    return 0;
}

} // namespace

DWORD StartCommandChannel()
{
    if (gRunning.exchange(true, std::memory_order_acq_rel)) {
        return 0;   // already running
    }
    const HANDLE thread = CreateThread(nullptr, 0, &PollThread, nullptr, 0, nullptr);
    if (thread == nullptr) {
        gRunning.store(false, std::memory_order_release);
        Log("result=failed detail=create_thread");
        return 1;
    }
    CloseHandle(thread);
    return 0;
}

unsigned long long CommandChannelProcessedCount()
{
    return gProcessed.load(std::memory_order_relaxed);
}

unsigned long long CommandChannelRejectedCount()
{
    return gRejected.load(std::memory_order_relaxed);
}

DWORD CommandChannelRunning() { return gRunning.load(std::memory_order_acquire) ? 1u : 0u; }

} // namespace preyvr::dll
