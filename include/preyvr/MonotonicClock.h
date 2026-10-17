#pragma once

// The clock every tracking, aim and frame sample is stamped with, and the age
// past which a sample or a tracked pose is stale. One definition, so two call
// sites cannot disagree about the epoch, the unit or the limit.
#include <chrono>
#include <cstdint>

namespace preyvr {

// 0.2 s. Lanes that need a tighter bound pass their own.
inline constexpr std::uint64_t kMaxPoseAgeNs = 200000000ull;

inline std::uint64_t MonotonicNanoseconds()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline bool FreshSample(std::uint64_t now, std::uint64_t stamp,
                        std::uint64_t maximumAge = kMaxPoseAgeNs) {
    return stamp != 0 && now >= stamp && now - stamp <= maximumAge;
}

} // namespace preyvr
