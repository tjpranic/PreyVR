#pragma once
#include <cstdint>
// The arithmetic of Prey's temporal-AA state (R-136,
// docs/TEMPORAL-AA-SCOPE-2026-10-30.md), pure so the per-eye lane
// (TemporalAaLane.h) can be tested against a model of the engine.
//
// One counter c picks everything: the resolve writes history [c & 1] and reads
// [(c + 1) & 1]; it reads the previous camera from ring slot (c - 1) & 3 and
// writes slot c & 3; the jitter sample is c mod N. Post-processing advances c by
// one, kept in 24 bits.
namespace preyvr::taa {

inline constexpr std::uint32_t kCounterMask = 0xFFFFFF;
// The second eye runs two counts ahead of the first. Each eye advances by one a
// frame, so with a ring of four each eye's (c - 1) & 3 is the slot it wrote
// the frame before.
inline constexpr std::uint32_t kEyeLead = 2;
// The resolve uses its history while counter - stamp < 10 (0xF9A89D).
inline constexpr std::int32_t kFreshness = 10;

constexpr std::uint32_t Advance(std::uint32_t c, std::uint32_t by = 1) { return (c + by) & kCounterMask; }
constexpr unsigned HistoryOut(std::uint32_t c) { return c & 1u; }
constexpr unsigned HistoryIn(std::uint32_t c) { return (c + 1) & 1u; }
constexpr unsigned RingIn(std::uint32_t c) { return (c - 1) & 3u; }
constexpr unsigned RingOut(std::uint32_t c) { return c & 3u; }

// Whether the resolve at counter c uses its history, the last resolve having
// stamped `stamp`: the engine's own signed difference.
constexpr bool HistoryFresh(std::uint32_t c, std::uint32_t stamp)
{
    return static_cast<std::int32_t>(c - stamp) < kFreshness;
}
// The stamp each eye's resolve is given, so whether it uses history is the
// lane's decision alone: one count behind (use it), or kFreshness behind (start
// afresh from the current frame). Left to the other eye's stamp, the first eye
// at 0xFFFFFF would see the second's wrapped one as stale.
constexpr std::uint32_t FreshStamp(std::uint32_t c) { return c - 1; }
constexpr std::uint32_t StaleStamp(std::uint32_t c) { return c - static_cast<std::uint32_t>(kFreshness); }

// Whether AA mode `mode` runs the temporal resolve: its flags 1 << min(mode, 5)
// (0xFE3760) against the resolve's 0x3C (0xF9C82D), so modes 2 to 5.
constexpr bool RunsResolve(int mode)
{
    const int clamped = mode > 5 ? 5 : mode;
    return clamped >= 0 && ((1u << clamped) & 0x3Cu) != 0;
}

// Each eye's counter for a start from the engine's counter c.
struct EyeCounters {
    std::uint32_t first, second;
};
constexpr EyeCounters StartCounters(std::uint32_t c) { return {c & kCounterMask, Advance(c, kEyeLead)}; }

} // namespace preyvr::taa
