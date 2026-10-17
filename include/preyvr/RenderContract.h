#pragma once
#include "preyvr/VrMath.h"
#include <array>
#include <atomic>
#include <cmath>
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
    std::array<float,4> tangents{}; // left, right, up, down
    long long displayTime = 0;
    unsigned long long reference = 0;
    // The tracking space the poses are in (HeadTrackingSpaceEpoch); 0 is none.
    unsigned long long spaceEpoch = 0;
    bool valid = false;
};
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
    return c.tangents[0]<0 && c.tangents[1]>0 && c.tangents[2]>0 && c.tangents[3]<0;
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
// Do not skip records independently of pixels. Overflow poisons the
// handoff until explicitly reset; it must never turn into a plausible wrong eye.
class RenderContractQueue {
    std::mutex mutex_;
    std::array<RenderContract,64> slots_{};
    std::size_t read_=0, count_=0;
    std::atomic<bool> failed_{false};
public:
    bool Push(const RenderContract& c) {
        std::lock_guard lock(mutex_);
        if(failed_.load() || count_==slots_.size()) {
            failed_=true; return false;
        }
        slots_[(read_+count_)%slots_.size()]=c; ++count_; return true;
    }
    bool Pop(RenderContract& c) {
        c={}; std::lock_guard lock(mutex_);
        if(failed_.load() || !count_) return false;
        c=slots_[read_];read_=(read_+1)%slots_.size();--count_;return true;
    }
    void Reset() {std::lock_guard lock(mutex_);read_=count_=0;failed_=false;}
};
}
