#include "BodyFollow.h"
#include "BodyFollowState.h"
#include "AtomicMax.h"

#include "HeadTrackingHook.h"
#include "XrInput.h"
#include "XrSessionHost.h"
#include "preyvr/BodyFollow.h"

#include <cmath>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>

// BodyFollow's frame-order probes -- where the view, the IK and the render hook
// fall against the follower, and how far each view's play-space yaw stepped --
// and body.report. The view's probe also hands room-scale that yaw
// (ViewPlaySpaceYaw) and reads back where its move left the capsule.
namespace preyvr::dll {
using namespace body;

void NoteBodyFollowView(float playSpaceYaw, unsigned long long referenceGeneration)
{
    const auto sequence = gSequence.fetch_add(1) + 1;
    const auto previousView = gViewSeq.exchange(sequence);
    if (!gActive.load(std::memory_order_acquire)) { return; }
    (gFollowSeq.load() > previousView ? gViewAfterFollow : gViewWithoutFollow).fetch_add(1, std::memory_order_relaxed);
    const auto follower = gFollowThread.load(std::memory_order_relaxed);
    if (follower != 0 && follower != GetCurrentThreadId()) { gThreadMismatch.fetch_add(1, std::memory_order_relaxed); }
    if (std::isfinite(playSpaceYaw)) {
        gViewP.store(playSpaceYaw, std::memory_order_release);
        gViewPGeneration.store(referenceGeneration, std::memory_order_release);
        gViewPNs.store(MonotonicNanoseconds(), std::memory_order_release);
    }
    const bool turned = gTurnedSinceView.exchange(false, std::memory_order_acq_rel);
    std::lock_guard lock(gViewMutex);
    // Where the entity is now against where SetPos left it this frame, and
    // after a nudge for the next 300 views: a physics step computed from the
    // old position would put it back (docs/ROOMSCALE-2026-10-11.md, risk 1).
    const GetPosFn getPos = gGetPos.load(std::memory_order_acquire);
    if ((gMovedPending || gNudgeWatch > 0) && getPos != nullptr && gMovedEntity != nullptr) {
        Vec3 now{};
        if (CallGetPos(getPos, gMovedEntity, now)) {
            if (gMovedPending) {
                const int drift = Mm(std::hypot(now.x - gMovedPos.x, now.y - gMovedPos.y));
                StoreMax(gViewDriftMaxMm, drift);
                if (drift > 5) { gViewDrift.fetch_add(1, std::memory_order_relaxed); }
            }
            if (gNudgeWatch > 0) {
                StoreMax(gNudgeDriftMaxMm, Mm(std::hypot(now.x - gNudgePos.x, now.y - gNudgePos.y)));
                --gNudgeWatch;
            }
        }
    }
    gMovedPending = false;
    if (gHavePrevious && referenceGeneration == gPreviousGeneration && std::isfinite(playSpaceYaw)) {
        const int step = std::abs(Mdeg(bodyfollow::Wrap(playSpaceYaw - gPreviousP)));
        if (turned) {
            StoreMax(gStepFollowedMaxMdeg, step);
            gStepFollowed.fetch_add(1, std::memory_order_relaxed);
        } else {
            StoreMax(gStepIdleMaxMdeg, step);
            gStepIdle.fetch_add(1, std::memory_order_relaxed);
        }
    }
    gHavePrevious = std::isfinite(playSpaceYaw);
    gPreviousP = playSpaceYaw;
    gPreviousGeneration = referenceGeneration;
}

void NoteBodyFollowIk()
{
    if (!gActive.load(std::memory_order_acquire)) { return; }
    const auto follow = gFollowSeq.load(), view = gViewSeq.load(), render = gRenderSeq.load();
    // After the follower and before the view: this frame's IK saw the turned
    // body. After the view: still this frame's turned body. After the render
    // hook: either this frame's IK running late or the next frame's running
    // before its follower -- the case a snap turn shows as a hand flash.
    if (follow > view && follow > render) { gIkBetween.fetch_add(1, std::memory_order_relaxed); }
    else if (view > follow && view > render) { gIkAfterView.fetch_add(1, std::memory_order_relaxed); }
    else { gIkAfterRender.fetch_add(1, std::memory_order_relaxed); }
}

void NoteBodyFollowRender() { gRenderSeq.store(gSequence.fetch_add(1) + 1); }

std::string BodyFollowReport()
{
    std::ostringstream out;
    out << " active=" << gActive.load() << " follow=" << gEnabled.load() << " level=" << gLevel.load()
        << " hooked=" << gInstalled.load() << " fault=" << gFault.load()
        << " torsoDeadzoneDeg=" << gDeadzoneMilliDeg.load() / 1000.0f
        << " torsoRelax=" << gRelaxPerMille.load() / 1000.0f
        << " ticks=" << gTicks.load() << " applied=" << gApplied.load() << " turns=" << gTurns.load()
        << " turnedDeg=" << gTurnedMdeg.load() / 1000 << " lastDeltaMdeg=" << gLastDeltaMdeg.load()
        << " readbackMaxMdeg=" << gReadbackMaxMdeg.load()
        << " headDeg=" << gHeadMdeg.load() / 1000.0f << " torsoDeg=" << gTorsoMdeg.load() / 1000.0f
        << " referenceDeg=" << HeadTrackingReferenceYaw() * kDegreesPerRadian
        << " notPlayer=" << gNotPlayer.load() << " noHead=" << gNoHead.load()
        << " notGameplay=" << gNotGameplay.load() << " cameraMode=" << gCameraMode.load()
        << " noReference=" << gNoReference.load() << " busy=" << gBusy.load()
        << " nativeRefused=" << gNativeRefused.load() << " faults=" << gFaults.load()
        << " viewAfterFollow=" << gViewAfterFollow.load() << " viewWithoutFollow=" << gViewWithoutFollow.load()
        << " threadMismatch=" << gThreadMismatch.load()
        << " stepFollowed=" << gStepFollowed.load() << " stepFollowedMaxMdeg=" << gStepFollowedMaxMdeg.load()
        << " stepIdle=" << gStepIdle.load() << " stepIdleMaxMdeg=" << gStepIdleMaxMdeg.load()
        << " ikBetween=" << gIkBetween.load() << " ikAfterView=" << gIkAfterView.load()
        << " ikAfterRender=" << gIkAfterRender.load()
        << " roomReadFallbacks=" << HeadTrackingRoomReadFallbacks()
        << " roomScale=" << gRoomScale.load() << " roomAvailable=" << gRoomAvailable.load()
        << " zoneMm=" << gStartMm.load() << '/' << gStopMm.load() << " followMs=" << gFollowMs.load()
        << " radiusMm=" << gRadiusMm.load()
        << " moved=" << gMoved.load() << " movedM=" << gMovedMm.load() / 1000.0
        << " lastMoveMm=" << gLastMoveMm.load() << " residualMm=" << gResidualMm.load()
        << " blocked=" << gBlocked.load() << " sweepRefused=" << gSweepRefused.load()
        << " roomBusy=" << gRoomBusy.load() << " positionOff=" << gPositionOff.load()
        << " noView=" << gNoView.load() << " noPose=" << gNoPose.load() << " jumps=" << gJumps.load()
        << " viewDrift=" << gViewDrift.load() << " viewDriftMaxMm=" << gViewDriftMaxMm.load()
        << " nudges=" << gNudges.load() << " nudgeDriftMaxMm=" << gNudgeDriftMaxMm.load()
        << " pendingTurnDeg=" << gTurnRequestMdeg.load() / 1000 << " pendingNudge=" << gNudgePending.load()
        << " headOut=" << BodyHeadOut() << " headOutEvents=" << gHeadOutEvents.load()
        << " headOutFrames=" << gHeadOutFrames.load() << " headHits=" << gHeadHits.load()
        << " fadeFrames=" << XrFadeFrames() << " nativeMoving=" << gNativeMoving.load()
        << " standAside=" << gStandAside.load() << " physics=" << gPhysics.load()
        << " physicsAvailable=" << gPhysicsAvailable.load() << " livingUnread=" << gLivingUnread.load()
        << " fitChecks=" << gFitChecks.load() << " fitShortened=" << gFitShortened.load()
        << " fitVertical=" << gFitVertical.load() << " fitStartBlocked=" << gFitStartBlocked.load()
        << " keptMotion=" << gKeptMotion.load() << " restMoves=" << gRestMoves.load()
        << " stepSettling=" << gStepSettling.load() << " pushOut=" << gPushOut.load()
        << " pushOutMaxMm=" << gPushOutMaxMm.load() << " deferredBusy=" << gDeferredBusy.load()
        << " bodyLagMaxMm=" << gBodyLagMaxMm.load();
    return out.str();
}

} // namespace preyvr::dll
