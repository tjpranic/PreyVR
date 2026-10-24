#include "CommandChannel.h"
#include "CommandArgs.h"
#include "VrMode.h"
#include "VrOptionsRuntime.h"
#include "Psychoscope.h"
#include "BodyEquipment.h"
#include "TorsoLane.h"

#include "AimTakeover.h"
#include "AnimIkTakeover.h"
#include "DebugOverlay.h"
#include "preyvr/DebugOverlayScene.h"
#include "NearFovOverride.h"
#include "MainRenderHook.h"
#include "StereoEyeCamera.h"
#include "EyeRecords.h"
#include "ConsoleBridgeWin32.h"
#include "CullCameraWatch.h"
#include "CullPrepareGuard.h"
#include "ResourceSetWatch.h"
#include "FrameProbe.h"
#include "FocusHold.h"
#include "ControllerPrompts.h"
#include "ResearchHotkeys.h"
#include "preyvr/ConsolePolicy.h"
#include "FrameObserverHook.h"
#include "HeadTrackingHook.h"
#include "Logger.h"
#include "NearViewStereo.h"
#include "DualSceneStereo.h"
#include "DualOverlay.h"
#include "NearParticleStereo.h"
#include "MarkerStereo.h"
#include "ReticleFollow.h"
#include "SceneQuery.h"
#include "ShotRay.h"
#include "InteractionLane.h"
#include "CarryLane.h"
#include "GrenadeLane.h"
#include "ArmsLane.h"
#include "MemoryCommands.h"
#include "PhysicalInteractions.h"
#include "ContactLane.h"
#include "SurfaceStopLane.h"
#include "SwingLane.h"
#include "WrenchDraw.h"
#include "WrenchSound.h"
#include "PsiMedkit.h"
#include "Haptics.h"
#include "HudBridge.h"
#include "HudLayer.h"
#include "WristLane.h"
#include "UiPointer.h"
#include "FrameCaptureWin32.h"
#include "InputPost.h"
#include "MoveLane.h"
#include "StickReread.h"
#include "ActionMaps.h"
#include "WeaponAttachment.h"
#include "XrInput.h"
#include "XrSessionHost.h"
#include "preyvr/VrOptions.h"

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
        << " focus={" << FocusHoldReport() << " }"
        << " observerFrames=" << ObservedFrameCount()
        << " xrSession=" << XrSessionStatusValue()
        << " xrFrames=" << XrSubmittedFrameCount()
        // Named before the render contract existed; the research log reads them so.
        << " fovAgreed=" << EyeContractsPublishedCount()
        << " fovDiverged=" << EyeContractsRefusedCount()
        << " projectionPolicy=" << (NativeProjectionEnabled() ? "prey_native" : "synthetic")
        << " cameraEdit=" << CameraEditStatusValue()
        << " lastEye=" << LastBuiltEye()
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
        << " weaponBaseline=" << WeaponBaselineCaptured()
        << " aimApplied=" << AimTakeoverAppliedCount()
        << " aimNoPose=" << AimTakeoverRejectedNoPose()
        << " aimNoPlayer=" << AimTakeoverRejectedNoPlayer()
        << " aimCompose=" << AimTakeoverRejectedCompose()
        << " aimNativeMagMilli=" << AimTakeoverNativeDirectionMagnitude()
        << " menuActions=" << MenuNavigationActionCount()
        << " inputPosted=" << InputPostCount()
        << " menuDiscarded=" << InputMenuDiscardedCount()
        << " menuHolds=" << InputMenuHoldCount()
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
        << SnapTurnReport()
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
// `<verb> log [0|1]`: a lane's per-event lines, on unless 0 follows. False, and
// `on` untouched, when the second word is not `log`.
bool EventLogSwitch(const std::vector<std::string>& args, bool& on)
{
    if (args.size() < 2 || args[1] != "log") { return false; }
    int value = 1;
    if (args.size() > 2) { ParseInt(args[2], value); }
    on = value != 0;
    return true;
}
const char* NoValue(const std::vector<std::string>& args)
{
    return Valued(args) ? "" : " note=no_value_nothing_changed";
}

using Args = std::vector<std::string>;

// A setter given some of its values but not all: refused rather than half set.
// Bare, it reports.
DWORD TooFewValues(const Args& args) { return Valued(args) ? ERROR_INVALID_PARAMETER : 0; }

// `Arg` (CommandArgs.h), unsigned: the dual, cull and hold verbs set unsigned values.
unsigned int ArgU(const Args& args, std::size_t i, int fallback)
{
    return static_cast<unsigned int>(Arg(args, i, fallback));
}

// **The console queue holds exactly one command.** Firing them back to back
// returns `busy` (4) for every one after the first, which is what happened on
// this channel's first run: motion blur landed and the two commands behind it
// were silently dropped with a code nobody read. The observer drains the queue
// once per frame, so retrying briefly is enough. Bounded, because a game that
// has stopped rendering will never drain it and this must not spin forever.
DWORD QueueConsoleRetrying(const std::string& command)
{
    DWORD result = QueueConsoleCommand(command.c_str());
    for (int attempt = 0; attempt < 40 && result == 4u; ++attempt) {
        Sleep(25);
        result = QueueConsoleCommand(command.c_str());
    }
    return result;
}

// use.hand / use.hold / use.trace / use.back.
void UseSetting(const Args& args, std::ostringstream& out)
{
    // Bare verbs report; a value sets. use.hand 1 = left hand points and
    // uses, right grip reloads (default); 0 = right grip, weapon ray.
    DWORD result = 0;
    if (Valued(args)) {
        const unsigned v = static_cast<unsigned>(Arg(args, 1, 1));
        result = args[0] == "use.hand" ? SetUseHand(v) : args[0] == "use.hold" ? SetUseHold(v)
               : args[0] == "use.trace" ? SetUseTrace(v) : SetUseBackMillimetres(v);
    }
    out << args[0] << " result=" << result << UseReport();
}
// arm.<setting> <values>: the arm solver's settings (ArmPose). A value that does
// not parse refuses the line: the channel has no default for each setting's
// values to fall back to.
void ArmSetting(const Args& args, std::ostringstream& out)
{
    int values[4]{};
    unsigned count = 0;
    bool parsed = true;
    for (std::size_t i = 1; i < args.size() && count < 4; ++i) { parsed = ParseInt(args[i], values[count++]) && parsed; }
    out << args[0] << " result="
        << (!parsed ? ERROR_INVALID_PARAMETER : count > 0 ? SetArmSetting(args[0].substr(4), values, count) : 0)
        << ArmReport();
}

// Who a verb is for (docs/TUNING-COMMANDS.md). Nothing reads it yet: it is what
// an instrumentation gate -- a build that leaves research out -- will read.
//  - player: what a player sets: the VR Options rows, the player-facing commands
//    beside them, and the VR session's own (start, stop, recenter);
//  - tuning: the rest of the settings and their reports;
//  - research: probes, dumps, captures, test switches, raw input.
enum class Audience : unsigned char { player, tuning, research };

// **The channel's verbs**, one row each: the name; who it is for; the words a
// line needs, the verb's own included (fewer are rejected as missing_arguments);
// and what it does. Looked up by name, then the lanes' own verbs (carry.,
// grenade., arms., mem., wrist.), then the console allowlist. Every line a row,
// a lane or the allowlist answers counts as processed, a refused value included;
// a word that is none of them, or a verb short of its words, as rejected.
struct Verb {
    const char* name;
    Audience audience;
    std::size_t minArgs;
    void (*run)(const Args& args, std::ostringstream& out);
};

constexpr Verb kVerbs[] = {
    // The hand-pose verbs and their neighbours (jordi/hand-pose): use, IK, pose,
    // body, the debug overlay, shots, near particles and markers.
    {"use.report", Audience::research, 0, [](const Args&, std::ostringstream& out) {
        out << "use.report result=0" << UseReport();
    }},
    {"use.find", Audience::research, 2, [](const Args& args, std::ostringstream& out) {
        out << "use.find result=0" << UseFind(static_cast<std::uint32_t>(Arg(args, 1, 0)));
    }},
    {"use.hand", Audience::tuning, 0, UseSetting},
    {"use.hold", Audience::tuning, 0, UseSetting},
    {"use.trace", Audience::tuning, 0, UseSetting},
    {"use.back", Audience::tuning, 0, UseSetting},
    {"use.filter", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // use.filter <0|1> [minCutoffCentiHz 100] [betaHundredths 200] [backlashCentiDegrees 75]:
        // One Euro then a dead band on the pointer.
        DWORD result = 0;
        if (Valued(args)) {
            result = SetUseFilter(static_cast<unsigned>(Arg(args, 1, 1)), static_cast<float>(Arg(args, 2, 100)) * .01f,
                                  static_cast<float>(Arg(args, 3, 200)) * .01f, static_cast<float>(Arg(args, 4, 75)) * .01f);
        }
        out << "use.filter result=" << result << UseReport();
    }},
    {"ik.skel", Audience::research, 0, [](const Args&, std::ostringstream& out) {
        out << "ik.skel result=" << RequestAnimIkSkeletonDump();
    }},
    {"ik.widesupport", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "ik.widesupport result=" << (Valued(args) ? SetAnimIkWideSupport(static_cast<unsigned>(Arg(args, 1, 1))) : 0)
            << " wide=" << AnimIkWideSupport();
    }},
    {"ik.supportlatch", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "ik.supportlatch result=" << (Valued(args) ? SetAnimIkSupportLatch(static_cast<unsigned>(Arg(args, 1, 1))) : 0)
            << " latch=" << AnimIkSupportLatch();
    }},
    {"ik.longreach", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // ik.longreach [on comfortMm maxBackMm supportBackMm twistDeg]; bare reports.
        DWORD result = 0;
        if (Valued(args)) {
            result = SetAnimIkLongReach(Arg(args, 1, 1), Arg(args, 2, 450), Arg(args, 3, 200), Arg(args, 4, 100), Arg(args, 5, 30));
        }
        out << "ik.longreach result=" << result << AnimIkLongReachReport();
    }},
    {"ik.helpers", Audience::research, 0, [](const Args&, std::ostringstream& out) {
        out << "ik.helpers result=" << RequestAnimIkHelperDump() << " (see PreyVR.log: preyvr_anim_ik helper)";
    }},
    {"pose.mode", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "pose.mode result=" << (Valued(args) ? SetHandPoseMode(static_cast<unsigned>(Arg(args, 1, 1))) : 0)
            << HandPoseReport();
    }},
    {"pose.curl", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        out << "pose.curl result=" << (Valued(args) ? SetHandPoseCurlPercent(Arg(args, 1, 100)) : 0) << HandPoseReport();
    }},
    {"pose.grip", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "pose.grip result="
            << (args.size() > 4 ? SetHandPoseGrip(Arg(args, 1, 550), Arg(args, 2, 35), Arg(args, 3, 0), Arg(args, 4, 150))
                                : TooFewValues(args))
            << HandPoseReport();
    }},
    {"pose.reach", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "pose.reach result="
            << (args.size() > 2 ? SetHandPoseReach(static_cast<unsigned>(Arg(args, 1, 1)), Arg(args, 2, 85)) : TooFewValues(args))
            << HandPoseReport();
    }},
    {"near.follow", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "near.follow result=" << (Valued(args) ? SetNearFovFollow(static_cast<unsigned>(Arg(args, 1, 1))) : 0)
            << " followedDeci=" << NearFovFollowed();
    }},
    {"pose.roll", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "pose.roll result=" << (Valued(args) ? SetHandPoseWeaponRoll(static_cast<unsigned>(Arg(args, 1, 1))) : 0)
            << HandPoseReport();
    }},
    {"pose.stretch", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "pose.stretch result=" << (Valued(args) ? SetHandPoseStretchPercent(Arg(args, 1, 120)) : 0)
            << HandPoseReport();
    }},
    {"arm.report", Audience::tuning, 0, [](const Args&, std::ostringstream& out) {
        out << "arm.report result=0" << ArmReport();
    }},
    // The arm solver's settings (SetArmSetting), as TUNING-COMMANDS.md lists them.
    {"arm.torso", Audience::tuning, 0, ArmSetting},
    {"arm.mode", Audience::tuning, 0, ArmSetting},
    {"arm.anchor", Audience::tuning, 0, ArmSetting},
    {"arm.shoulder", Audience::tuning, 0, ArmSetting},
    {"arm.len", Audience::tuning, 0, ArmSetting},
    {"arm.length", Audience::player, 0, ArmSetting},
    {"arm.lift", Audience::tuning, 0, ArmSetting},
    {"arm.stretch", Audience::tuning, 0, ArmSetting},
    {"arm.prior", Audience::tuning, 0, ArmSetting},
    {"arm.rom", Audience::tuning, 0, ArmSetting},
    {"arm.hold", Audience::tuning, 0, ArmSetting},
    {"arm.knee", Audience::tuning, 0, ArmSetting},
    {"body.report", Audience::tuning, 0, [](const Args&, std::ostringstream& out) {
        // The shared torso the shoulders and body slots sit on (TorsoLane.h).
        out << "body.report result=0" << TorsoLaneReport();
    }},
    {"body.follow", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // The body turning as you turn round in the room: 1 (default), 0 for an A/B.
        out << "body.follow result=" << (Valued(args) ? SetBodyFollow(static_cast<unsigned>(Arg(args, 1, 1))) : 0)
            << " enabled=" << BodyFollowEnabled();
    }},
    {"body.followafter", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // When the follow turns the body: 1 (default) after the frame's render, 0 at the frame's start, for an A/B.
        out << "body.followafter result="
            << (Valued(args) ? SetBodyFollowAfterRender(static_cast<unsigned>(Arg(args, 1, 1))) : 0)
            << " enabled=" << BodyFollowAfterRender();
    }},
    {"body.torso", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // body.torso <deadzoneDeg> <relax per mille per second>; arm.torso sets the same filter.
        out << "body.torso result=" << (args.size() > 2 ? SetBodyTorso(Arg(args, 1, 35), Arg(args, 2, 600)) : TooFewValues(args))
            << TorsoLaneReport();
    }},
    {"pose.report", Audience::tuning, 0, [](const Args&, std::ostringstream& out) {
        out << "pose.report result=0" << HandPoseReport();
    }},
    {"pose.marks", Audience::tuning, 0, [](const Args&, std::ostringstream& out) {
        out << "pose.marks result=0\n" << HandPoseMarks();
    }},
    {"dbg.draw", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // Bare `dbg.draw` only reports. `on`/`all` = every layer, `off` = 0,
        // otherwise a layer mask (see DebugOverlayScene.h).
        DWORD result = 0;
        if (Valued(args)) {
            const std::string& value = args[1];
            int mask = -1;
            if (value == "on" || value == "all") { mask = static_cast<int>(debugdraw::kAllLayers); }
            else if (value == "off") { mask = 0; }
            else if (!ParseInt(value, mask) || mask < 0 || mask > static_cast<int>(debugdraw::kAllLayers)) { mask = -1; }
            result = mask >= 0 ? SetDebugOverlay(static_cast<unsigned>(mask)) : ERROR_INVALID_PARAMETER;
        }
        out << "dbg.draw result=" << result << DebugOverlayReport();
    }},
    {"dbg.lasers", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // The aim lasers (VR Options "Aim lasers") for this session, beside
        // whatever dbg.draw shows; bare, it only reports.
        out << "dbg.lasers result=" << (Valued(args) ? SetAimLasers(static_cast<unsigned>(Arg(args, 1, 1))) : 0)
            << DebugOverlayReport();
    }},
    {"move.firehold", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "move.firehold result=" << (Valued(args) ? SetFireLaneHold(static_cast<unsigned>(Arg(args, 1, 1))) : 0)
            << " hold=" << FireLaneHold() << " posted=" << FireLaneHoldPosted();
    }},
    {"aim.shotprobe", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        out << "aim.shotprobe result=" << (Valued(args) ? SetShotProbe(static_cast<unsigned>(Arg(args, 1, 0))) : 0)
            << ShotProbeReport();
    }},
    {"aim.shot", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // Bare `aim.shot` only reports (and installs the observers, so a shot
        // with the lane off is measured too). 1 = the shot leaves the muzzle
        // along the aim; 0 = the native eye-origin query.
        DWORD result = 0;
        if (Valued(args)) {
            const int value = Arg(args, 1, -1);
            result = value == 0 || value == 1 ? SetShotRay(static_cast<unsigned>(value)) : ERROR_INVALID_PARAMETER;
        }
        out << "aim.shot result=" << result << ShotRayReport();
    }},
    {"aim.beam", Audience::research, 0, [](const Args&, std::ostringstream& out) {
        // Q-Beam: the last beam (damage ray + drawn beam) against the muzzle,
        // the aim and the amber marker. Observation only.
        out << "aim.beam result=0" << BeamReport();
    }},
    {"near.fx", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // Bare `near.fx` only reports. 1 = DrawNear particles in the weapon's
        // camera space (default), 0 = native, for A/B.
        DWORD result = 0;
        if (Valued(args)) {
            const int value = Arg(args, 1, -1);
            result = value == 0 || value == 1 ? SetNearParticleStereo(static_cast<unsigned>(value))
                                              : ERROR_INVALID_PARAMETER;
        }
        out << "near.fx result=" << result << " " << NearParticleStereoReport();
    }},
    {"near.fxbridge", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "near.fxbridge result=" << (Valued(args) ? SetNearParticleBridge(static_cast<unsigned>(Arg(args, 1, 1))) : 0)
            << " " << NearParticleStereoReport();
    }},
    {"markers.depth", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // Bare `markers.depth` only reports. 1 = world markers at the depth they
        // mark (default), 0 = where Prey draws them, for A/B.
        DWORD result = 0;
        if (Valued(args)) {
            const int value = Arg(args, 1, -1);
            result = value == 0 || value == 1 ? SetMarkerDepth(static_cast<unsigned>(value)) : ERROR_INVALID_PARAMETER;
        }
        out << "markers.depth result=" << result << " " << MarkerDepthReport();
    }},
    {"markers.size", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // Marker Size for this session (VR Options saves it): 50..200 percent.
        const DWORD result = Valued(args) ? SetMarkerSizePercent(static_cast<unsigned>(Arg(args, 1, 100))) : 0;
        out << "markers.size result=" << result << " " << MarkerDepthReport();
    }},
    {"near.fxhide", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        out << "near.fxhide result=" << (Valued(args) ? SetNearParticleHide(static_cast<unsigned>(Arg(args, 1, 1))) : 0)
            << NoValue(args);
    }},
    {"dbg.report", Audience::research, 0, [](const Args&, std::ostringstream& out) {
        out << "dbg.report result=0" << DebugOverlayReport();
    }},
    {"dbg.marks", Audience::research, 0, [](const Args&, std::ostringstream& out) {
        out << "dbg.marks result=0\n" << DebugOverlayMarks();
    }},
    // Rung 1b, the RT_RenderScene double submission (DualSceneStereo.h), the
    // frame's cull guards, and the window and hold frames.
    {"dual.observe", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        // Pass-through logging of RT_RenderScene submissions.
        out << "dual.observe result=" << (Valued(args) ? SetDualSceneObserve(ArgU(args, 1, 1)) : 0) << DualSceneReport();
    }},
    {"dual.mode", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // 0 off, 1 the same-camera control, 2 per-eye cameras. Second argument:
        // frame budget.
        out << "dual.mode result=" << (Valued(args) ? SetDualSceneMode(ArgU(args, 1, 0), ArgU(args, 2, 0)) : 0) << DualSceneReport();
    }},
    {"dual.ipd", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "dual.ipd result=" << (Valued(args) ? SetDualSceneIpdMillimetres(ArgU(args, 1, 64)) : 0) << DualSceneReport();
    }},
    {"dual.vr", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // Headset A/B: 1 both eyes per frame, 0 the alternate-eye path. Ordered so
        // no both-eyes contract is ever published without its second submission.
        DWORD result = 0;
        if (Valued(args) && ArgU(args, 1, 1) != 0) {
            result = SetDualSceneMode(3, 0);
            if (result == 0) SetDualStereoContract(1);
        } else if (Valued(args)) {
            SetDualStereoContract(0);
            result = SetDualSceneMode(0, 0);
        }
        out << "dual.vr result=" << result << DualSceneReport();
    }},
    {"dual.capture", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        out << "dual.capture result=" << RequestDualSceneCapture(ArgU(args, 1, 0));
    }},
    {"dual.overlay", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // Step 2 A/B: 1 repeats the after-world overlays into the held eye, 0
        // leaves them in the second eye only. No argument: report.
        out << "dual.overlay result=" << (Valued(args) ? SetDualOverlayRedraw(ArgU(args, 1, 1)) : 0)
            << DualOverlayReport();
    }},
    {"dual.report", Audience::research, 0, [](const Args&, std::ostringstream& out) {
        out << "dual.report result=0" << DualSceneReport() << DualOverlayReport();
    }},
    {"cull.report", Audience::research, 0, [](const Args&, std::ostringstream& out) {
        // docs/CULL-CAMERA-CRASH-2026-10-17.md: the read-only pass-camera reset watch.
        out << "cull.report result=0" << CullCameraWatchReport();
    }},
    {"cull.watch", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        out << "cull.watch result=" << (Valued(args) ? SetCullCameraWatch(ArgU(args, 1, 1)) : 0)
            << CullCameraWatchReport();
    }},
    {"cull.probe", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        // The frame-step recorder around panel closes and cull-lag changes (FrameProbe.h).
        out << "cull.probe result=" << (Valued(args) ? SetFrameProbe(ArgU(args, 1, 1)) : 0) << FrameProbeReport();
    }},
    {"cull.prepare", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // The occlusion-prepare guard that keeps the culler from falling a frame
        // behind (CullPrepareGuard.h): 1 guards, 0 only observes. No argument: report.
        out << "cull.prepare result=" << (Valued(args) ? SetCullPrepareGuard(ArgU(args, 1, 1)) : 0)
            << CullPrepareReport();
    }},
    {"rset.report", Audience::research, 0, [](const Args&, std::ostringstream& out) {
        // docs/CRASHES-2026-10-24.md: who rebuilds a render item's resource set mid-pass.
        out << "rset.report result=0" << ResourceSetWatchReport();
    }},
    {"rset.watch", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        out << "rset.watch result=" << (Valued(args) ? SetResourceSetWatch(ArgU(args, 1, 1)) : 0)
            << ResourceSetWatchReport();
    }},
    {"window.hold", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // docs/DESKTOP-WINDOW-2026-10-17.md: Prey runs as though focused. The
        // launcher sends 1 before vr.enable.
        out << "window.hold result=" << (Valued(args) ? SetFocusHold(ArgU(args, 1, 1)) : 0) << NoValue(args)
            << FocusHoldReport();
    }},
    {"xr.hold", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // docs/LOADING-LASER-2026-10-17.md: the mod's own frames while Prey's are
        // late, and D3D11 multithread protection with them. xr.hold <0|1> [ms].
        out << "xr.hold result=" << (Valued(args) ? SetXrHold(ArgU(args, 1, 1), ArgU(args, 2, 0)) : 0) << NoValue(args)
            << XrHoldReport();
    }},
    // The world reticle (ReticleFollow.h).
    {"aim.world", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // Prey's crosshair at the depth it marks (1), or on the HUD panel (0).
        // Saved by VR Options (reticle_world).
        const auto result = Valued(args) ? SetReticleInWorld(static_cast<unsigned>(Arg(args, 1, 1))) : 0;
        out << "aim.world result=" << result << ReticleWorldReport() << " scene:" << SceneQueryReport();
    }},
    {"aim.reticlesize", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        const auto result = Valued(args) ? SetReticleSizePercent(static_cast<unsigned>(Arg(args, 1, 100))) : 0;
        out << "aim.reticlesize result=" << result << ReticleWorldReport();
    }},
    // The menus' prompts (ControllerPrompts.h).
    {"ui.prompts", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        int value = 1;
        if (args.size() > 1) { ParseInt(args[1], value); }
        out << "ui.prompts result=" << (Valued(args) ? SetControllerPrompts(static_cast<unsigned>(value)) : 0)
            << " enabled=" << ControllerPromptsEnabled() << ControllerPromptsReport();
    }},
    // Physical contact (ContactLane.h): its report.
    {"contact", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // contact log [0|1]: the per-event begin, end and push lines.
        if (bool on = true; EventLogSwitch(args, on)) { SetContactEventLog(on); }
        out << "contact result=0" << ContactReport();
    }},
    // Surface stop (SurfaceStopLane.h): surface [0|1] [log 0|1] [weapon 0|1] [heavy kg].
    // 0/1 is the VR Options switch; log the per-event hold and let-go lines; weapon 0
    // stops the hands alone, not the weapon's box; heavy is the Heavy Objects mass
    // (a malformed one is its Spec default).
    {"surface", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        std::size_t i = 1;
        if (args.size() > 1 && (args[1] == "0" || args[1] == "1")) {
            SetSurfaceStop(args[1] == "1");
            i = 2;
        }
        for (; i < args.size(); i += 2) {
            const bool heavy = args[i] == "heavy";
            const int value = Arg(args, i + 1, heavy ? options::Default(options::HeavyMass) : 1);
            if (args[i] == "log") { SetSurfaceStopEventLog(value != 0); }
            else if (args[i] == "weapon") { SetSurfaceStopWeapon(value != 0); }
            else if (heavy && i + 1 < args.size() && value > 0) { SetSurfaceHeavyMass(static_cast<unsigned>(value)); }
        }
        out << "surface result=0" << SurfaceStopReport();
    }},
    {"swing", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // swing log [0|1]: the per-event begin, strike and swing lines.
        if (bool on = true; EventLogSwitch(args, on)) { SetSwingEventLog(on); }
        out << "swing result=0" << SwingReport();
    }},
    // docs/WRENCH-DRAW-ANIMATION-2026-10-20.md: wrench.draw [0|1]. 1 (default) takes
    // the wrench out and puts it away without Prey's draw and stow animations; 0 plays
    // them again.
    // wrench.sound [play 0|1]: the draw's and stow's sound after a skip (WrenchSound.h,
    // default 1).
    {"wrench.sound", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        for (std::size_t i = 1; i + 1 < args.size(); i += 2) {
            int on = 1;
            ParseInt(args[i + 1], on);
            if (args[i] == "play") { SetWrenchSoundPlay(on != 0); }
        }
        out << "wrench.sound result=0" << WrenchSoundReport();
    }},
    {"wrench.draw", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        if (args.size() > 1) {
            int on = 1;
            ParseInt(args[1], on);
            SetWrenchDrawSkip(on != 0);
        }
        out << "wrench.draw result=0" << WrenchDrawReport();
    }},
    // The session, menus and panels, the HUD, the view, the near pass, IK, aim,
    // capture, movement and input.
    {"vr.options", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        if(Valued(args))RequestVrOptions(Arg(args, 1,0)!=0);
        out << "vr.options result=0" << VrOptionsReport() << PsychoscopeReport() << BodyEquipmentReport();
    }},
    {"vr.enable", Audience::player, 0, [](const Args&, std::ostringstream& out) {
        EnableVrMode(); out << "vr.enable result=0 " << VrModeReport();
    }},
    {"vr.disable", Audience::player, 0, [](const Args&, std::ostringstream& out) {
        DisableVrMode(); out << "vr.disable result=0 " << VrModeReport();
    }},
    {"vr.status", Audience::player, 0, [](const Args&, std::ostringstream& out) {
        out << "vr.status result=0 " << VrModeReport();
    }},
    {"vr.recenter", Audience::player, 0, [](const Args&, std::ostringstream& out) {
        out << "vr.recenter result=" << RecenterHeadTracking();
    }},
    {"hud.var", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        // hud.var [path ...]       -- queue reads; with no path, the last result
        // hud.setvar <path> <num>  -- queue a write (SV_Normal), then a read
        DWORD result = 0;
        for (std::size_t i = 1; i < args.size() && result == 0; ++i) { result = QueueHudVariable(args[i], false, 0); }
        out << "hud.var result=" << result << ' ' << HudVariableReport();
    }},
    {"hud.setvar", Audience::research, 3, [](const Args& args, std::ostringstream& out) {
        double value = 0;
        out << "hud.setvar result="
            << (ParseNumber(args[2], value) ? QueueHudVariable(args[1], true, value) : ERROR_INVALID_PARAMETER);
    }},
    {"hud.layer", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        const auto result=Valued(args)?SetHudLayerEnabled(Arg(args, 1,options::Default(options::HudLayer))):0;
        out << "hud.layer result=" << result << ' ' << HudLayerReport();
    }},
    {"input.hotkeys", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        // **Nothing started this thread.** SetHotkeysEnabled was reachable only
        // through an exported entry point that the startup path never calls, so
        // every keyboard binding in the bridge -- including the ~ console key --
        // was dead in the player package. Building a binding and never checking
        // the path that arms it is the same mistake twice in one session.
        out << "input.hotkeys result=" << (Valued(args)?SetHotkeysEnabled(Arg(args, 1,1)):0)
            << " consoleToggles=" << HotkeyConsoleToggles();
    }},
    {"ui.stereo", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        out << "ui.stereo result=" << (Valued(args)?SetInventoryStereoEnabled(Arg(args, 1,0)):0)
            << ' ' << InventoryStereoReport();
    }},
    {"ui.depth", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        out << "ui.depth result=" << (Valued(args)?SetInventoryDepthPercent(Arg(args, 1,25)):0)
            << ' ' << InventoryStereoReport();
    }},
    {"ui.inventory", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // Opt-in separate inventory layer; native redirection is additionally
        // gated on a compatible, recently serviced XR swapchain consumer.
        out << "ui.inventory result=" << (Valued(args)?SetInventoryCaptureEnabled(Arg(args, 1,0)):0)
            << ' ' << HudLayerReport()
            // Frames actually submitted through the separate inventory swapchain.
            // captured= above says the redirect happened; this says the layer
            // reached xrEndFrame -- the two can differ, and that gap is the
            // upload-refusal fallback.
            << " layerFrames=" << InventoryLayerFrameCount();
    }},
    {"ui.panel", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "ui.panel result=" << (Valued(args) ? SetUiPanelMode(Arg(args, 1,1)) : 0)
            << " mode=" << UiPanelMode() << ' ' << VrModeReport();
    }},
    {"ui.pointer", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        out << "ui.pointer result=" << (Valued(args)?SetUiPointerHand(Arg(args, 1,1)):0) << ' ' << UiPointerReport();
    }},
    {"ui.scale", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // The stored percent was already echoed here; `margin` and `guide` join
        // it because they are what actually decide the panel's size. A wearer
        // reading "percent=160" while the panel refused to grow had no way to
        // see that the fit margin was shrinking it straight back off, which is
        // the whole reason ui.scale looked as though it capped at 130.
        if (Valued(args)) { out << "ui.scale result=" << SetUiScalePercent(Arg(args, 1, 100)); }
        else { out << "ui.scale result=0"; }
        out << " percent=" << UiScalePercent()
            << " margin=" << UiFitMarginPercent()
            << " guide=" << UiGuideEnabled();
    }},
    {"ui.text", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // Text panel distance in cm (100..200, default 140), at the HUD panel's
        // physical size. No argument: report.
        out << "ui.text result=" << (Valued(args) ? SetTextPanelDistanceCm(Arg(args, 1, 140)) : 0)
            << " cm=" << TextPanelDistanceCm();
    }},
    {"ui.distance", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // Native menu panel distance in cm (100..400, default 200), at the same
        // apparent size. The VR Options menu saves it. No argument: report.
        out << "ui.distance result=" << (Valued(args) ? SetUiDistanceCm(Arg(args, 1, 200)) : 0)
            << " cm=" << UiDistanceCm();
    }},
    {"ui.offset", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
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
    }},
    {"ui.margin", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // The real size ceiling. ui.scale above ~130 was being absorbed by the
        // fit loops shrinking the panel back inside 72% of the reported frustum.
        out << "ui.margin result=" << (Valued(args) ? SetUiFitMarginPercent(Arg(args, 1, 72)) : 0)
            << " percent=" << UiFitMarginPercent();
    }},
    {"ui.guide", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // Reports when given no argument. It used to default the argument to 1,
        // which was harmless while the card was on by default and would now mean
        // a bare `ui.guide` silently switched it back on.
        out << "ui.guide result=" << (Valued(args) ? SetUiGuideEnabled(Arg(args, 1, 0)) : 0)
            << " enabled=" << UiGuideEnabled();
    }},
    {"ui.curve", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        out << "ui.curve result=" << (Valued(args) ? SetUiCurveDegrees(Arg(args, 1,35)) : 0)
            << " degrees=" << UiCurveDegrees();
    }},
    {"observer", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        const DWORD status = Valued(args) ? SetFrameObserverEnabled(Arg(args, 1, 1)) : FrameObserverStatus();
        out << "observer status=" << ObserverStatusName(status) << "(" << status << ")";
    }},
    {"xr.runtime", Audience::tuning, 2, [](const Args& args, std::ostringstream& out) {
        out << "xr.runtime result=" << SetXrRuntimeManifest(args[1].c_str());
    }},
    {"xr.srgb", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "xr.srgb result=" << (Valued(args) ? SetXrPreferSrgbFormat(Arg(args, 1, 1)) : 0) << NoValue(args);
    }},
    {"xr.start", Audience::tuning, 0, [](const Args&, std::ostringstream& out) {
        const DWORD status = StartXrSession();
        out << "xr.start status=" << XrStatusName(status) << "(" << status << ")";
    }},
    {"xr.native", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "xr.native result=" << (Valued(args) ? SetNativeProjection(Arg(args, 1, 1)) : 0) << NoValue(args);
    }},
    {"xr.stereo", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // Millimetres and degrees, so the file never carries a float.
        const float ipd = static_cast<float>(Arg(args, 1, 64)) / 1000.0f;
        const float halfFov = static_cast<float>(Arg(args, 2, 50));
        out << "xr.stereo result=" << (Valued(args) ? SetSyntheticStereo(ipd, halfFov) : 0) << NoValue(args);
    }},
    {"xr.mirrorfov", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // How much of the headset's view the flat mirror's image spans, as a
        // percentage of Prey's own declared field. 100 is the honest projection.
        // Only affects frames with no held eye pair -- the menu, and anything
        // else not rendered as alternating eyes.
        out << "xr.mirrorfov result=" << (Valued(args) ? SetMirrorFovPercent(static_cast<unsigned int>(Arg(args, 1, 100))) : 0)
            << " percent=" << MirrorFovPercent();
    }},
    {"xr.submit", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        if (Valued(args)) {
            const DWORD status = SetXrStereoSubmission(Arg(args, 1, 1));
            out << "xr.submit status=" << XrStatusName(status) << "(" << status << ")";
        } else {
            out << "xr.submit result=0" << NoValue(args);
        }
    }},
    {"view.observe", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "view.observe result=" << (Valued(args) ? SetViewHookObserving(Arg(args, 1, 1)) : 0) << NoValue(args);
    }},
    {"view.calibrate", Audience::player, 0, [](const Args&, std::ostringstream& out) {
        out << "view.calibrate result=" << RecenterHeadTracking(true) << ' ' << HeadHeightReport();
    }},
    {"view.height", Audience::tuning, 0, [](const Args&, std::ostringstream& out) {
        out << "view.height result=0 " << HeadHeightReport();
    }},
    {"view.recenter", Audience::player, 0, [](const Args&, std::ostringstream& out) {
        out << "view.recenter result=" << RecenterHeadTracking();
    }},
    {"view.apply", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "view.apply result=" << (Valued(args) ? SetViewHookApplying(Arg(args, 1, 1)) : 0) << NoValue(args);
    }},
    {"view.position", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "view.position result=" << (Valued(args) ? SetViewPositionApplying(Arg(args, 1, 1)) : 0) << NoValue(args);
    }},
    {"near.fov", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // Bare command reports without changing/rearming a faulted override.
        // Decidegrees: `near.fov 1234` is 123.4 degrees, explicit 0 disables.
        DWORD result = 0;
        if (Valued(args)) {
            int value = 0;
            if (args.size() == 2 && ParseInt(args[1], value) && value >= 0) {
                // An explicit value is a choice: stop following the world FOV --
                // first, since following rewrites the value every frame -- and
                // follow again if the value is refused.
                const bool following = NearFovFollowing();
                SetNearFovFollow(0);
                result = SetNearFovDeciDegrees(static_cast<unsigned int>(value));
                if (result != 0 && following) { SetNearFovFollow(1); }
            } else {
                result = 1;
            }
        }
        out << "near.fov result=" << result
            << " deciDegrees=" << NearFovDeciDegrees()
            << " applied=" << NearFovAppliedCount()
            << " refused=" << NearFovRefusedCount()
            << " observedDeciDegrees=" << NearFovObservedDeciDegrees()
            << " observedValid=" << NearFovObservedValid()
            << " faulted=" << NearFovFaulted()
            << " thread=" << NearFovThreadId();
    }},
    {"near.enable", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "near.enable result=" << (Valued(args) ? SetNearViewStereo(Arg(args, 1, 1)) : 0) << NoValue(args);
    }},
    {"near.halfipd", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "near.halfipd result=" << (Valued(args) ? SetNearViewHalfIpdMillimetres(Arg(args, 1, 32)) : 0)
            << NoValue(args);
    }},
    {"near.lineage", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        out << "near.lineage result=" << (Valued(args) ? ArmNearViewLineage(Arg(args, 1, 1)) : 0) << NoValue(args);
    }},
    {"near.dump", Audience::research, 0, [](const Args&, std::ostringstream& out) {
        out << "near.dump result=" << DumpNearViewLineage();
    }},
    {"near.zero", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        out << "near.zero result=" << (Valued(args) ? SetNearViewZeroDeltaControl(Arg(args, 1, 1)) : 0) << NoValue(args);
    }},
    {"ik.mode", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // 0 off, 1 observe (identify the rig, read its ADIK table, write
        // nothing), 2 apply. Mutually exclusive with hand.mode 2.
        out << "ik.mode result=" << (Valued(args) ? SetAnimIkMode(Arg(args, 1, 0)) : 0);
        const unsigned int mode = AnimIkMode();
        out << " mode=" << mode
            << (mode == 0 ? "(off)" : mode == 1 ? "(observe)" : mode == 2 ? "(apply)" : "(unknown)");
    }},
    {"ik.test", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        out << "ik.test result=" << (Valued(args) ? SetAnimIkTestOffsetMillimetres(Arg(args, 1, 0), Arg(args, 2, 0), Arg(args, 3, 0)) : 0)
            << NoValue(args);
    }},
    {"ik.drive", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "ik.drive result=" << (Valued(args) ? SetAnimIkControllerDrive(Arg(args, 1, 1)) : 0) << NoValue(args);
    }},
    {"ik.calibrate", Audience::tuning, 0, [](const Args&, std::ostringstream& out) {
        out << "ik.calibrate result=" << CalibrateAnimIk();
    }},
    {"ik.align", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        DWORD result = 0;
        if (Valued(args)) {
            int value = 0;
            result = args.size() == 2 && ParseInt(args[1], value) && value >= 0
                ? SetAnimIkWeaponAlignment(static_cast<unsigned int>(value)) : 1u;
        }
        out << "ik.align result=" << result << AnimIkWeaponAlignmentReport();
    }},
    {"ik.joints", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "ik.joints result=" << (Valued(args) ? SetAnimIkJointSignature(Arg(args, 1, 101)) : 0) << NoValue(args);
    }},
    {"ik.reach", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "ik.reach result=" << (Valued(args) ? SetAnimIkReachPercent(Arg(args, 1, 100)) : 0)
            << " percent=" << AnimIkReachPercent();
    }},
    {"ik.hands", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "ik.hands result=" << (Valued(args) ? SetAnimIkHands(Arg(args, 1, 1)) : 0) << NoValue(args);
    }},
    {"ik.anchor", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "ik.anchor result=" << (Valued(args) ? SetAnimIkCameraAnchor(Arg(args, 1, 1)) : 0) << NoValue(args)
            << " camera=" << AnimIkAnchorCameraCount()
            << " reticle=" << AnimIkAnchorReticleCount()
            << " missing=" << AnimIkAnchorMissingCount();
    }},
    {"ik.trace", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        if (Valued(args)) { out << "ik.trace result=" << SetAnimIkTrace(Arg(args, 1, 1)); }
        else { out << "ik.trace result=0"; }
        const std::string trace = AnimIkTraceReport();
        // Persisted: results.txt is overwritten by the next command, and a
        // per-frame transformation chain that only exists there cannot be
        // compared across a sweep.
        lifecycle::Log("preyvr_ik_trace" + trace);
        out << trace;
    }},
    {"ik.dump", Audience::research, 0, [](const Args&, std::ostringstream& out) {
        out << "ik.dump result=" << DumpAnimIk();
    }},
    {"console", Audience::research, 2, [](const Args& args, std::ostringstream& out) {
        // Rebuilt from the tokens, then through the existing fail-closed
        // allowlist. This channel adds no reach to the console.
        std::string command = args[1];
        for (std::size_t i = 2; i < args.size(); ++i) {
            command += " " + args[i];
        }
        const DWORD result = QueueConsoleRetrying(command);
        out << "console result=" << result
            << " command=\"" << command << "\"";
    }},
    {"psi.target", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        const auto result=Valued(args)?SetPsiTargetMode(Arg(args, 1,0)):0;
        out<<"psi.target result="<<result<<PsiMedkitReport();
    }},
    {"medkit.slot", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        const auto result=Valued(args)?SetMedkitSlot(Arg(args, 1,0)):0;
        out<<"medkit.slot result="<<result<<PsiMedkitReport();
    }},
    {"aim.scene", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        if(Valued(args))SetSceneReticle(Arg(args, 1,0));
        out<<"aim.scene result=0"<<SceneQueryReport();

    }},
    {"haptics", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        if(Valued(args))SetHapticsEnabled(Arg(args, 1,1));
        if(args.size()>2)SetHapticStrength(Arg(args, 2,70));
        out<<"haptics result=0"<<HapticsReport();
    }},
    {"aim.reticle", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // Moves Prey's own crosshair to where the controller points, so the
        // symbol a player aims with agrees with the ray gameplay uses. One
        // screen position exists, not two, so it is projected through the
        // current view rather than per eye; that is a real limitation, not an
        // oversight, and a genuinely stereo reticle needs a mod-drawn one.
        out << "aim.reticle result=" << (Valued(args) ? SetReticleFollowEnabled(Arg(args, 1, 1)) : 0) << NoValue(args);
    }},
    {"aim.reticleconverge", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        // The distance the crosshair marks along the firing ray, in mm, when the
        // scene query is off or finds nothing within range: the VR option
        // RETICLE FALLBACK DISTANCE (reticle_fallback_mm).
        out << "aim.reticleconverge result="
            << (Valued(args) ? SetReticleConvergenceMillimetres(Arg(args, 1, 10000)) : 0)
            << " mm=" << ReticleConvergenceMillimetres();
    }},
    {"aim.reticledispatch", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // Separates the two halves of the reticle write. Off writes only the
        // cached field; on also dispatches the movie call the engine's own reset
        // makes. If the crosshair does not move, this says which half to blame.
        out << "aim.reticledispatch result=" << (Valued(args) ? SetReticleDispatchEnabled(Arg(args, 1, 1)) : 0);
        if (Valued(args)) { out << " value=" << Arg(args, 1, 1); } else { out << NoValue(args); }
    }},
    {"aim.reticlecanvas", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // aim.reticlecanvas 0|1|2 -- raw viewport / R-118 model / native
        out << "aim.reticlecanvas result=" << (Valued(args) ? SetReticleCanvasMode(Arg(args, 1, 1)) : 0)
            << " mode=" << ReticleCanvasMode()
            << " native=" << ReticleNativeCanvasCount()
            << " nativeFailed=" << ReticleNativeCanvasFailedCount();
    }},
    {"aim.reticlestage", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "aim.reticlestage result=" << (Valued(args) ? SetReticleStageScaleMode(Arg(args, 1, 0)) : 0) << NoValue(args);
    }},
    {"aim.twohand", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        out << "aim.twohand result=" << (Valued(args)?SetTwoHandedAim(Arg(args, 1,1)):0)
            << TwoHandedAimStatus();
    }},
    {"aim.bodyyaw", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // 1 rotates head-relative offsets by the BODY yaw (camera - head), 0 by
        // the camera yaw. 0 makes the hand and weapon swing with the headset.
        out << "aim.bodyyaw result=" << (Valued(args) ? SetAimBodyYaw(Arg(args, 1, 1)) : 0)
            << " value=" << AimBodyYawEnabled();
    }},
    {"aim.origin", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        // Diagnostic controller-aim origin; this does not establish a muzzle
        // transform or remove projectile convergence from the authored helper.
        out << "aim.origin result=" << (Valued(args) ? SetAimOriginFromHand(Arg(args, 1, 1)) : 0)
            << " mode=" << AimOriginMode()
            << (AimOriginMode() == 2 ? "(muzzle)" : AimOriginMode() == 1 ? "(hand)" : "(native)");
    }},
    {"aim.calibratebarrel", Audience::tuning, 0, [](const Args&, std::ostringstream& out) {
        out << "aim.calibratebarrel result=" << CalibrateWeaponBarrel()
            << " offsetMm=" << WeaponBarrelOffsetMillimetres(0) << ","
            << WeaponBarrelOffsetMillimetres(1) << "," << WeaponBarrelOffsetMillimetres(2);
    }},
    {"aim.enable", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // The detached-aim lane, reachable from the channel at last. H-004 is
        // reproduced against two native consumers -- the wrench contact moved
        // 0.127165 units and the interaction selector committed a different
        // entity -- but both used synthetic angle offsets driven from a
        // debugger. This is what lets a *controller* drive it.
        out << "aim.enable result=" << (Valued(args) ? SetAimTakeoverEnabled(Arg(args, 1, 1)) : 0) << NoValue(args)
            << " applied=" << AimTakeoverAppliedCount()
            << " nativeMagMilli=" << AimTakeoverNativeDirectionMagnitude();
    }},
    {"weapon.observe", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        out << "weapon.observe result=" << (Valued(args) ? SetWeaponAttachmentObserving(Arg(args, 1, 1)) : 0)
            << NoValue(args);
    }},
    {"capture.hud", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        out << "capture.hud result=" << RequestFrameCapture(static_cast<std::uint32_t>(Arg(args, 1,900)),true);
    }},
    {"capture.inventory", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        out << "capture.inventory result=" << RequestInventoryPairCapture(static_cast<std::uint32_t>(Arg(args, 1,1000)));
    }},
    {"capture", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        // A one-shot readback of the backbuffer -- which is exactly the image
        // submitted to the headset -- so "is X visible in the HMD" becomes a
        // file to look at rather than something a wearer has to be asked.
        out << "capture result=" << RequestFrameCapture(static_cast<std::uint32_t>(Arg(args, 1, 1)))
            << " completed=" << CompletedFrameCaptureCount();
    }},
    {"hud.call", Audience::research, 3, [](const Args& args, std::ostringstream& out) {
        // hud.call <function> <x> [y] -- queue a main-thread HUD dispatch.
        double x = 0, y = 0;
        const bool parsed = ParseNumber(args[2], x) && (args.size() < 4 || ParseNumber(args[3], y));
        const DWORD result = parsed ? CallHudFunction(args[1].c_str(), static_cast<float>(x), static_cast<float>(y),
                                                      args.size() >= 4)
                                    : ERROR_INVALID_PARAMETER;
        out << "hud.call result=" << result << " queued=" << (result == 0 ? 1 : 0)
            << " fn=" << args[1];
    }},
    {"hud.fit", Audience::research, 2, [](const Args& args, std::ostringstream& out) {
        // hud.fit 1 -- fit the HUD canvas inside the frame (clear bMax)
        // hud.fit 0 -- restore the engine's cover fit (set bMax)
        const bool fitInside = Arg(args, 1, 1) != 0;
        out << "hud.fit result=" << QueueHudFit(!fitInside) << " queued=1"
            << " fitInside=" << (fitInside ? 1 : 0);
    }},
    {"xr.frustum", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        out << "xr.frustum result=" << (Valued(args) ? SetRuntimeFrustum(Arg(args, 1, 1)) : 0) << NoValue(args)
            << " used=" << RuntimeFrustumUsedCount()
            << " missing=" << RuntimeFrustumMissingCount()
            << XrCoverageReport();
    }},
    {"xr.timing", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // xr.timing            -- report
        // xr.timing 1 [hz]     -- arm and reset, default 90 Hz deadline
        if (Valued(args)) {
            out << "xr.timing result="
                << SetXrTimingEnabled(Arg(args, 1, 1), static_cast<unsigned int>(Arg(args, 2, 90)));
        } else {
            out << "xr.timing result=0";
        }
        const std::string report = XrTimingReport();
        // **Persisted, because results.txt is overwritten by the next command.**
        // A quantitative run whose numbers exist only in a file the poller
        // clobbers is a run that cannot be re-read or audited.
        lifecycle::Log("preyvr_timing" + report);
        out << report;
    }},
    {"xr.coverage", Audience::tuning, 0, [](const Args&, std::ostringstream& out) {
        out << "xr.coverage result=0" << XrCoverageReport();
    }},
    {"move.act", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "move.act result=" << (Valued(args) ? SetInteractionEnabled(Arg(args, 1, 1)) : 0)
            << InteractionReport();
    }},
    {"move.bind", Audience::tuning, 3, [](const Args& args, std::ostringstream& out) {
        // move.bind <slot 0-12> <keyId> -- 0 interact, 1 inventory, 2 jump, 3 crouch,
        // 4 wheel, 5 Prey's Y, 6 sprint, 7 psychoscope, 8-10 quick slots left/right/down,
        // 11 torch, 12 psi (MoveLane's ActionSlot)
        const unsigned int slot = static_cast<unsigned int>(Arg(args, 1, 0));
        const int keyId = static_cast<int>(Arg(args, 2, 0));
        out << "move.bind result=" << SetInteractionBinding(slot, keyId)
            << InteractionReport();
    }},
    {"menu.gate", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        if (Valued(args)) {
            SetMenuNavigationGate(Arg(args, 1, 1));
            out << "menu.gate result=0 value=" << Arg(args, 1, 1);
        } else {
            out << "menu.gate result=0" << NoValue(args);
        }
        out << " menuOpen=" << (HudMenuIsOpen() ? 1 : 0)
            << " samples=" << HudMenuStateSampleCount()
            << " suppressed=" << MenuNavigationSuppressedCount();
    }},
    {"hud.probe", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        // hud.probe            -- report the last probe
        // hud.probe <x> <y>    -- queue one at that viewport fraction
        if (args.size() >= 3) {
            double x = 0, y = 0;
            if (ParseNumber(args[1], x) && ParseNumber(args[2], y)) {
                out << "hud.probe result=" << QueueHudProbe(static_cast<float>(x), static_cast<float>(y)) << " queued=1";
            } else {
                out << "hud.probe result=" << ERROR_INVALID_PARAMETER << " queued=0";
            }
        } else {
            out << "hud.probe result=0 " << HudLastProbe();
        }
    }},
    {"xr.resolution", Audience::tuning, 0, [](const Args&, std::ostringstream& out) {
        // The measured chain: what the runtime asks for, what Prey renders, what
        // is held, and what is submitted. Ratios are given so "soft" has a
        // number; the scene's own render target is NOT covered here.
        using enum XrResolution;
        const DWORD recW = XrResolutionChain(recommendedWidth), recH = XrResolutionChain(recommendedHeight);
        const DWORD backW = XrResolutionChain(backBufferWidth), backH = XrResolutionChain(backBufferHeight);
        out << "xr.resolution result=0"
            << " recommended=" << recW << "x" << recH
            << " max=" << XrResolutionChain(maxWidth) << "x" << XrResolutionChain(maxHeight)
            << " backbuffer=" << backW << "x" << backH
            << " heldEye=" << XrResolutionChain(heldEyeWidth) << "x" << XrResolutionChain(heldEyeHeight)
            << " submitted=" << XrResolutionChain(submittedWidth) << "x" << XrResolutionChain(submittedHeight)
            << " recommendedSamples=" << XrResolutionChain(recommendedSamples)
            << " viewsDiffer=" << XrResolutionChain(viewsDiffer)
            << " heldFormat=" << XrResolutionChain(heldFormat)
            << " views=" << XrResolutionChain(views)
            << " sizeMismatch=" << XrResolutionChain(sizeMismatch);
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
    }},
    {"move.mode", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // 0 off, 1 observe (hook the analog handlers, attribute every call,
        // post nothing), 2 apply. Observe first: it proves the handlers fire
        // and measures the player's own hardware before we add to it.
        out << "move.mode result=" << (Valued(args) ? SetMoveLaneMode(Arg(args, 1, 0)) : 0);
        const unsigned int mode = MoveLaneMode();
        out << " mode=" << mode
            << (mode == 0 ? "(off)" : mode == 1 ? "(observe)" : mode == 2 ? "(apply)" : "(unknown)");
    }},
    {"move.snap", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        out << "move.snap result=" << (Valued(args)?SetSnapTurnDegrees(Arg(args, 1,SnapTurnDegrees())):0) << " degrees=" << SnapTurnDegrees();
    }},
    {"move.headrelative", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        out << "move.headrelative result="
            << (Valued(args)?SetHeadRelativeMovement(Arg(args, 1,options::Default(options::HeadRelative))):0)
            << " enabled=" << HeadRelativeMovementEnabled();
    }},
    {"move.scales", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        out << "move.scales result=" << (Valued(args) ? SetMovementAxisScales(Arg(args, 1,100),Arg(args, 2,100)) : 0)
            << " strafe=" << MovementStrafePercent() << " backward=" << MovementBackwardPercent();
    }},
    {"move.turn", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "move.turn result=" << (Valued(args) ? SetTurnLaneEnabled(Arg(args, 1, 1)) : 0)
            << " enabled=" << TurnLaneEnabled();
    }},
    {"move.turndeadzone", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        out << "move.turndeadzone result=" << (Valued(args) ? SetTurnLaneDeadzone(Arg(args, 1, 15)) : 0)
            << " hundredths=" << TurnLaneDeadzone();
    }},
    {"move.turnscale", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        out << "move.turnscale result=" << (Valued(args) ? SetTurnLaneScale(Arg(args, 1, 100)) : 0) << NoValue(args);
    }},
    {"move.fire", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "move.fire result=" << (Valued(args) ? SetFireLaneEnabled(Arg(args, 1, 1)) : 0)
            << " enabled=" << FireLaneEnabled();
    }},
    {"move.all", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // The whole control scheme in one command: left stick moves, right stick
        // turns, right trigger fires, grip and face buttons interact.
        if (Valued(args)) {
            const unsigned int on = static_cast<unsigned int>(Arg(args, 1, 1)) != 0u ? 1u : 0u;
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
    }},
    {"move.deadzone", Audience::player, 0, [](const Args& args, std::ostringstream& out) {
        out << "move.deadzone result=" << (Valued(args) ? SetMoveLaneDeadzone(Arg(args, 1, 15)) : 0)
            << " hundredths=" << MoveLaneDeadzone();
    }},
    {"move.reread", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // docs/MOVE-STOP-2026-10-24.md: Prey's re-read of the sticks finds the
        // lanes' stick (1), or the idle pad's zero as Prey reads it (0, A/B).
        out << "move.reread result=" << (Valued(args) ? SetStickRereadFix(static_cast<unsigned>(Arg(args, 1, 1))) : 0)
            << StickRereadReport();
    }},
    {"move.hold", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "move.hold result=" << (Valued(args) ? SetStickSampleHoldMs(static_cast<unsigned>(Arg(args, 1, 100))) : 0)
            << " ms=" << StickSampleHoldMs();
    }},
    {"move.report", Audience::tuning, 0, [](const Args&, std::ostringstream& out) {
        out << "move.report result=0" << MoveLaneStatusReport() << StickRereadReport();
    }},
    {"input.post", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        out << "input.post result=" << (Valued(args) ? SetInputPostEnabled(Arg(args, 1, 1)) : 0) << NoValue(args);
    }},
    {"input.force", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        out << "input.force result=" << (Valued(args) ? SetInputPostForce(static_cast<unsigned int>(Arg(args, 1, 1))) : 0)
            << NoValue(args);
    }},
    {"menu", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        // Menu navigation through the engine's own input layer. `Arg(args, 1)` is the
        // MenuAction index; 0=up 1=down 2=left 3=right 4=accept 5=cancel 6=start,
        // 7/8 tabs, 9 secondary, 10 tertiary, 11/12 pages, 13 exit.
        // A non-zero result means refused -- it can never mean "the menu did not
        // move", because PostInputEvent returns void and the binding that would
        // consume it is a candidate rather than a proven active bind (R-089).
        const DWORD result = PostMenuAction(static_cast<unsigned int>(Arg(args, 1, 0)));
        // `result=0` means **queued**. Delivery happens on the engine thread and
        // acceptance is not observable from here at all: PostInputEvent returns
        // void and the binding is a candidate (R-089). Read the screen.
        out << "menu result=" << result << " action=" << Arg(args, 1, 0)
            << " queued=" << InputQueueDepthEstimate()
            << " posted=" << InputPostCount()
            << " refused=" << InputPostRefusedCount()
            << " dropped=" << InputQueueDroppedCount()
            << " drainThread=" << InputDrainThreadId();
    }},
    {"input.binds", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // Prey's live gamepad bindings into PreyVR.log (ActionMaps); `all`
        // logs the keyboard's and mouse's too. Read on the next frame.
        RequestBindingDump(args.size() >= 2 && args[1] == "all");
        out << "input.binds result=0 (next frame; see PreyVR.log: preyvr_bindings)";
    }},
    {"input.key", Audience::research, 0, [](const Args& args, std::ostringstream& out) {
        const DWORD result = PostRawInput(Arg(args, 1, 0), static_cast<unsigned int>(Arg(args, 2, 1)),
                                          Arg(args, 3, 1000));
        out << "input.key result=" << result << " keyid=" << Arg(args, 1, 0)
            << " queued=" << InputQueueDepthEstimate()
            << " posted=" << InputPostCount()
            << " refused=" << InputPostRefusedCount()
            << " dropped=" << InputQueueDroppedCount()
            << " drainThread=" << InputDrainThreadId();
    }},
    {"menu.nav", Audience::tuning, 0, [](const Args& args, std::ostringstream& out) {
        // Arms the controller -> menu binding. Separate from `input.post` on
        // purpose: one authorises posting at all, the other decides whether a
        // controller may produce it.
        if (Valued(args)) {
            SetMenuNavigation(static_cast<unsigned int>(Arg(args, 1, 1)));
            out << "menu.nav result=0 value=" << Arg(args, 1, 1);
        } else {
            out << "menu.nav result=0" << NoValue(args);
        }
        out << " actions=" << MenuNavigationActionCount();
    }},
    {"report", Audience::tuning, 0, [](const Args&, std::ostringstream& out) {
        // results.txt is replaced by the next command. Preserve each requested
        // snapshot so a headset sweep survives beyond its final sample. This
        // runs on the command thread, not on every render frame. The report
        // alone: `out` also holds the replies to the file's earlier lines.
        std::ostringstream report;
        WriteReport(report);
        lifecycle::Log("preyvr_report " + report.str());
        out << report.str();
    }},
};

// One verb, one operation. Deliberately not a name-to-export lookup: that would
// be a call-anything primitive whose argument is a text file.
void Execute(const std::vector<std::string>& args, std::ostringstream& out)
{
    const std::string& verb = args[0];
    // Reported, not ignored. A typo that silently does nothing is
    // indistinguishable from a mechanism that does not work.
    const auto reject = [&](const char* detail) {
        gRejected.fetch_add(1, std::memory_order_relaxed);
        out << "result=rejected verb=\"" << verb << "\" detail=" << detail;
    };
    for (const Verb& row : kVerbs) {
        if (verb == row.name) {
            if (args.size() < row.minArgs) {
                reject("missing_arguments");
                return;
            }
            row.run(args, out);
            gProcessed.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    if (ExecuteCarryCommand(args, out) || ExecuteGrenadeCommand(args, out) || ExecuteArmsCommand(args, out) ||
        ExecuteMemoryCommand(args, out) || ExecuteWristCommand(args, out)) {
        gProcessed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (console::IsAllowlistedCvar(verb)) {
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
        const DWORD result = QueueConsoleRetrying(command);
        out << "console result=" << result << " command=\"" << command << "\"";
        gProcessed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    reject("not_a_channel_verb_and_not_allowlisted");
}

} // namespace

void LogCommandChannelStarted()
{
    Log("result=0 detail=started commands=\"" + CommandPath().string() +
        "\" results=\"" + ResultPath().string() + "\"");
}

void ServiceCommandFile()
{
    const auto commands = CommandPath();
    const auto results = ResultPath();
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
}

} // namespace preyvr::dll
