#pragma once
#include <cstdint>
#include <optional>

namespace preyvr::xrhold {
// **When the mod submits frames itself** (XrSessionHost's hold thread,
// docs/LOADING-LASER-2026-10-17.md). Prey's render thread stamps each frame it
// services. A gap past the threshold is a stall: Prey is loading, or hitched,
// and hold frames keep the laser following the hand until a newer stamp
// arrives, which ends the stall.
struct StallEnd {
    std::uint64_t durationNs = 0;      // from the last frame before it to the first after
    unsigned long long heldFrames = 0; // frames the mod submitted during it
};

class StallTracker {
public:
    // `lastServiceNs` 0 is "no frame yet": never a stall. Returns whether to
    // hold a frame now; `ended` is set when the stall in progress has just ended.
    bool Observe(std::uint64_t nowNs, std::uint64_t lastServiceNs, std::uint64_t thresholdNs,
                 std::optional<StallEnd>& ended);
    void CountHeldFrame() { ++heldFrames_; }
    bool InStall() const { return from_ != 0; }
    void Reset();

private:
    std::uint64_t from_ = 0;
    unsigned long long heldFrames_ = 0;
};
} // namespace preyvr::xrhold
