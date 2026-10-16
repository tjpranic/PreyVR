#include "preyvr/XrHold.h"

namespace preyvr::xrhold {
bool StallTracker::Observe(std::uint64_t nowNs, std::uint64_t lastServiceNs, std::uint64_t thresholdNs,
                           std::optional<StallEnd>& ended)
{
    ended.reset();
    if (lastServiceNs == 0) {
        Reset();
        return false;
    }
    // A newer frame ends the stall, even when the next one is already late:
    // that is a new stall, timed from the newer frame.
    if (from_ != 0 && lastServiceNs != from_) {
        ended = StallEnd{lastServiceNs > from_ ? lastServiceNs - from_ : 0, heldFrames_};
        Reset();
    }
    if (nowNs < lastServiceNs || nowNs - lastServiceNs < thresholdNs) {
        return false;
    }
    if (from_ == 0) {
        from_ = lastServiceNs;
    }
    return true;
}

void StallTracker::Reset()
{
    from_ = 0;
    heldFrames_ = 0;
}
} // namespace preyvr::xrhold
