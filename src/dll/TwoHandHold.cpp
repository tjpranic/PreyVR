#include "AimTakeover.h"
#include "DebugOverlay.h"
#include "HandWorld.h"
#include "Haptics.h"
#include "UiMode.h"
#include "PsiMedkit.h"
#include "WeaponAttachment.h"
#include "XrInput.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/TwoHandedAim.h"

#include <atomic>
#include <sstream>
#include <string>

// The two-handed hold (preyvr/TwoHandedAim.h): the aim hook solves it on each
// gameplay frame before the frame is published, so the IK and the lanes see the
// same hold (SolveTwoHandedFrame, AimTakeover.h).
namespace preyvr::dll {
namespace {

LatestSnapshot<SupportGripGeometry> gSupportGeometry;
LatestSnapshot<twohand::Input> gSupportInput;
std::atomic<unsigned> gTwoHandGates{0}, gTwoHandGatesSeen{0};
std::atomic<bool> gTwoHandEnabled{true};
std::atomic<bool> gTwoHandToggle{false},gTwoHandSnap{true};
std::atomic<unsigned int> gTwoHandReset{0};
std::atomic<bool> gTwoHandHeld{false}, gTwoHandRegionReady{false}, gSupportNear{false};
std::atomic<unsigned long long> gTwoHandFrames{0};
thread_local twohand::Solver gTwoHandSolver;
thread_local unsigned int gTwoHandSeenReset=0;
thread_local std::uint64_t gTwoHandTime=0;

} // namespace

void SolveTwoHandedFrame(GameplayPoseFrame& frame, bool tracking)
{
    const auto& right=frame.tracking.hands[static_cast<unsigned int>(Hand::right)];
    const auto& left=frame.tracking.hands[static_cast<unsigned int>(Hand::left)];
    twohand::Input input{};
    Pose primary{};
    HandWorld(frame,Hand::right,ControllerPose::aim,Anchor::cameraElseReticle,Placement::controller,input.aim);
    HandWorld(frame,Hand::right,ControllerPose::grip,Anchor::cameraElseReticle,Placement::controller,primary);
    HandWorld(frame,Hand::left,ControllerPose::grip,Anchor::cameraElseReticle,Placement::controller,input.support);
    input.primary=primary.position;
    input.squeeze=left.squeezeValue;
    input.toggleGrip=gTwoHandToggle.load();
    input.reference=frame.referenceGeneration;input.epoch=frame.tracking.epoch;
    EquippedRig owner{};
    if(TryGetEquippedRig(frame.player,owner)) input.owner=owner.generation;
    frame.weaponGeneration=input.owner;
    const auto now=MonotonicNanoseconds();
    input.dt=gTwoHandTime && now>gTwoHandTime ? static_cast<float>(now-gTwoHandTime)*1e-9f : .011f;
    gTwoHandTime=now;
    const auto reset=gTwoHandReset.load();
    if(reset!=gTwoHandSeenReset) { gTwoHandSolver={};gTwoHandSeenReset=reset; }
    // Keep a young geometry sample across publication contention. A missed
    // try-lock is not tracking loss and must not unlatch a held foregrip.
    thread_local SupportGripGeometry geometry{};
    SupportGripGeometry incoming{};
    if(gSupportGeometry.TryRead(incoming)) geometry=incoming;
    const bool region=geometry.owner==input.owner&&
        geometry.reference==input.reference&&geometry.epoch==input.epoch&&
        FreshSample(now,geometry.publishedNs);
    if(region) { input.region=geometry.region;input.visualPrimaryOffset=geometry.primaryOffsetWorld; }
    gTwoHandRegionReady.store(region);
    // Each gate as a bit, for aim.twohand: which one released a held foregrip.
    const unsigned gates=(region?0u:1u)|(tracking?0u:2u)|(MedkitOwnsGrip()?4u:0u)|
        (frame.headYawUsable?0u:8u)|(frame.cameraCentreValid?0u:16u)|
        (AimTakeoverEnabled()&&gTwoHandEnabled.load()?0u:32u)|(HudGameplayInputAllowed()?0u:64u)|
        (IsPoseUsable(right.aimPose,right.aimValidity,kMaxPoseAgeNs)&&
         IsPoseUsable(right.gripPose,right.gripValidity,kMaxPoseAgeNs)&&
         IsPoseUsable(left.gripPose,left.gripValidity,kMaxPoseAgeNs)?0u:128u);
    gTwoHandGates.store(gates);
    if(gates) gTwoHandGatesSeen.fetch_or(gates);
    input.usable=gates==0;
    frame.twoHand=gTwoHandSolver.Update(input);
    {
        Vec3 point{};
        // The left hand inside the foregrip region: a squeeze there is the
        // two-hand hold, never a use (InteractionUse.h).
        gSupportNear.store(region&&input.usable&&twohand::ClosestGrip(input,point));
    }
    frame.twoHand.snapSupport=gTwoHandSnap.load();
    DebugOverlayForegrip(frame,input,frame.twoHand,region);
    gSupportInput.Publish(input);
    const bool wasHeld=gTwoHandHeld.exchange(frame.twoHand.held);
    if(frame.twoHand.held&&!wasHeld)
        QueueHaptic(Hand::left,haptics::Event::ForegripAttached,frame.tracking);
    if(frame.twoHand.held) ++gTwoHandFrames;
}

void PublishSupportGripGeometry(const SupportGripGeometry& geometry) { gSupportGeometry.Publish(geometry); }
DWORD SetTwoHandedAim(unsigned int enabled) {
    if(enabled>1) return 1;
    gTwoHandEnabled.store(enabled!=0);++gTwoHandReset;
    return 0;
}
unsigned int TwoHandedAimEnabled() { return gTwoHandEnabled.load()?1u:0u; }
DWORD SetTwoHandGripToggle(unsigned enabled) {
    if(enabled>1)return ERROR_INVALID_PARAMETER;
    if(gTwoHandToggle.exchange(enabled!=0)!=(enabled!=0))++gTwoHandReset;
    return 0;
}
DWORD SetTwoHandSupportSnap(unsigned enabled) {
    if(enabled>1)return ERROR_INVALID_PARAMETER;
    gTwoHandSnap.store(enabled!=0);return 0;
}
bool TwoHandGripToggle(){return gTwoHandToggle.load();}
bool TwoHandSupportSnap(){return gTwoHandSnap.load();}
bool TwoHandedAimHeld(){return gTwoHandEnabled.load()&&gTwoHandHeld.load();}
bool SupportGripNear(){return gTwoHandEnabled.load()&&gSupportNear.load();}
std::string TwoHandedAimStatus() {
    std::ostringstream out;
    out<<" enabled="<<TwoHandedAimEnabled()<<" held="<<gTwoHandHeld.load()
       <<" regionReady="<<gTwoHandRegionReady.load()<<" frames="<<gTwoHandFrames.load()
       // 1 region 2 tracking 4 medkit 8 headYaw 16 camera 32 disabled 64 hud 128 poses
       <<" gates="<<gTwoHandGates.load()<<" gatesSeen="<<gTwoHandGatesSeen.exchange(0);
    SupportGripGeometry geometry{};
    if(gSupportGeometry.TryRead(geometry)) out<<" owner="<<geometry.owner
        <<" supportMm="<<geometry.region.start.x*1000<<","<<geometry.region.start.y*1000<<","<<geometry.region.start.z*1000;
    twohand::Input input{};
    if(gSupportInput.TryRead(input)) {
        Vec3 point{};
        out<<" usable="<<input.usable<<" near="<<twohand::ClosestGrip(input,point)<<" squeeze="<<input.squeeze<<" dt="<<input.dt;
        const Quaternion inverse{-input.aim.orientation.x,-input.aim.orientation.y,-input.aim.orientation.z,input.aim.orientation.w};
        const Vec3 local=Rotate(inverse,Vec3{input.support.position.x-input.primary.x-input.visualPrimaryOffset.x,
            input.support.position.y-input.primary.y-input.visualPrimaryOffset.y,input.support.position.z-input.primary.z-input.visualPrimaryOffset.z});
        out<<" handLocalMm="<<local.x*1000<<","<<local.y*1000<<","<<local.z*1000;
    }
    return out.str();
}

} // namespace preyvr::dll
