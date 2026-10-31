#pragma once
#include "base/VrMath.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <mutex>
namespace preyvr {
// eye 0/1: one eye rendered this frame, `pose` is that eye.
// eye kRenderContractBothEyes: both eyes rendered this frame (rung 1b), `pose` is
// the head and `eyePoses` the left and right eyes, all sampled for the same
// display time. Both eyes share the declared tangents.
constexpr int kRenderContractBothEyes = 2;
struct RenderContract {
    int eye = -1;
    Pose pose{};
    std::array<Pose,2> eyePoses{};
    std::array<float,4> tangents{}; // left, right, up, down, as rendered
    // How much narrower Prey's view is than the FOV slider's (render/camera/ViewZoom.h),
    // 1 without a zoom: the frame is declared this much wider than it was rendered.
    float zoom = 1.f;
    long long displayTime = 0;
    unsigned long long reference = 0;
    // The tracking space the poses are in (HeadTrackingSpaceEpoch); 0 is none.
    unsigned long long spaceEpoch = 0;
    // The head camera this both-eyes frame was rendered from (CCamera Matrix34,
    // engine space), as the main thread wrote it. The render thread finds the
    // frame's record by it (RenderContractQueue::PopMatching).
    std::array<float,12> camera{};
    bool haveCamera = false;
    bool valid = false;
};
// The same camera: the render thread's copy of the matrix the main thread
// wrote, element by element (IsDualFrameCentre's tolerance).
inline bool SameRenderCamera(const std::array<float,12>& a, const std::array<float,12>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!(std::abs(a[i] - b[i]) <= 1e-4f)) return false;
    }
    return true;
}
inline bool ValidContractPose(const Pose& pose) {
    const auto& q=pose.orientation; const auto& p=pose.position;
    const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(n) && std::abs(n-1.f)<=.01f &&
           std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
inline bool ValidRenderContract(const RenderContract& c) {
    if (!c.valid || c.eye < 0 || c.eye > kRenderContractBothEyes || c.displayTime <= 0) return false;
    if (!ValidContractPose(c.pose)) return false;
    if (c.eye == kRenderContractBothEyes &&
        (!ValidContractPose(c.eyePoses[0]) || !ValidContractPose(c.eyePoses[1]))) return false;
    for(float t:c.tangents) if(!std::isfinite(t)) return false;
    if(!std::isfinite(c.zoom) || c.zoom<1.f) return false;
    return c.tangents[0]<0 && c.tangents[1]>0 && c.tangents[2]>0 && c.tangents[3]<0;
}
// The frustum the compositor is told a frame's image covers: the rendered one,
// unless Prey has zoomed, when it is the unzoomed one and the image is magnified.
inline std::array<float,4> DeclaredTangents(const RenderContract& c) {
    return {c.tangents[0]*c.zoom, c.tangents[1]*c.zoom, c.tangents[2]*c.zoom, c.tangents[3]*c.zoom};
}
// Where a both-eyes frame's eye was rendered from: eyePoses, which the compositor
// is given, are the real eyes, and a zoom's eyes sit closer to the head by its
// magnification. For what is drawn into the image afterwards.
inline Pose RenderedEyePose(const RenderContract& c, int eye) {
    Pose pose=c.eyePoses[eye];
    if(c.zoom==1.f) return pose;
    const Vec3& head=c.pose.position;
    pose.position={head.x+(pose.position.x-head.x)/c.zoom, head.y+(pose.position.y-head.y)/c.zoom,
                   head.z+(pose.position.z-head.z)/c.zoom};
    return pose;
}
// Whether a frame can still be submitted once it reaches the XR thread, a frame
// after it was built. Its eye poses are in tracking space, so only a change of
// that space -- a runtime rebase or a reset -- strands it. A snap turn or a
// recenter changes how the world maps onto the space, not the space: the pixels
// and their poses still agree, and the frame in flight is shown, not dropped.
// It is NOT compared with the live reference generation: that dropped the frame
// in flight on every snap. An odd reference was never a settled one.
inline bool SubmittableRenderContract(const RenderContract& c, unsigned long long spaceEpoch) {
    return ValidRenderContract(c) && c.spaceEpoch != 0 && c.spaceEpoch == spaceEpoch && c.reference % 2 == 0;
}
// One record per main-thread render, taken at RT_EndFrame. The two are not
// always 1:1: through a level-transition load Prey's main loop keeps rendering
// while its render thread runs the loadtime loop, which ends the frame once
// (2026-10-22: ~330 renders in 0.28 s against one RT_EndFrame, and the old
// poisoned queue left the headset black for good). So a both-eyes frame takes
// its record by the camera it was rendered from (PopMatching), dropping the
// records of frames that never ended. Overflow drops the oldest record. A
// positional Pop after an overflow is still refused -- it would be a plausible
// wrong eye -- until a match resynchronises the queue.
class RenderContractQueue {
    std::mutex mutex_;
    std::array<RenderContract,64> slots_{};
    std::size_t read_=0, count_=0;
    bool overflowed_=false;
public:
    // False when the queue was full and the oldest record was dropped for this one.
    bool Push(const RenderContract& c) {
        std::lock_guard lock(mutex_);
        bool kept=true;
        if(count_==slots_.size()) {
            read_=(read_+1)%slots_.size(); --count_; overflowed_=true; kept=false;
        }
        slots_[(read_+count_)%slots_.size()]=c; ++count_; return kept;
    }
    // The oldest record, for a frame that cannot be identified by its camera.
    bool Pop(RenderContract& c) {
        c={}; std::lock_guard lock(mutex_);
        if(overflowed_ || !count_) return false;
        c=slots_[read_];read_=(read_+1)%slots_.size();--count_;return true;
    }
    // The oldest record rendered from `camera`, discarding the `skipped` records
    // queued before it. Leaves the queue untouched when none matches.
    bool PopMatching(const std::array<float,12>& camera, RenderContract& c, std::size_t& skipped) {
        c={}; skipped=0; std::lock_guard lock(mutex_);
        for(std::size_t i=0;i<count_;++i) {
            const RenderContract& candidate=slots_[(read_+i)%slots_.size()];
            if(!candidate.haveCamera || !SameRenderCamera(candidate.camera,camera)) continue;
            c=candidate; skipped=i;
            read_=(read_+i+1)%slots_.size(); count_-=i+1; overflowed_=false;
            return true;
        }
        return false;
    }
    // The smallest largest-element difference between `camera` and a queued
    // record's camera, for diagnosing a frame no record matched; -1 with none.
    float NearestCameraDifference(const std::array<float,12>& camera) {
        std::lock_guard lock(mutex_);
        float best=-1.f;
        for(std::size_t i=0;i<count_;++i) {
            const RenderContract& candidate=slots_[(read_+i)%slots_.size()];
            if(!candidate.haveCamera) continue;
            float worst=0.f;
            for(std::size_t k=0;k<camera.size();++k) worst=std::max(worst,std::abs(candidate.camera[k]-camera[k]));
            if(best<0.f || worst<best) best=worst;
        }
        return best;
    }
    std::size_t Size() {std::lock_guard lock(mutex_);return count_;}
    void Reset() {std::lock_guard lock(mutex_);read_=count_=0;overflowed_=false;}
};
}
