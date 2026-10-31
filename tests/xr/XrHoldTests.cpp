#include "xr/XrHold.h"
#include "TestSupport.h"
#include <cstdlib>
#include <iostream>

using preyvr::xrhold::StallEnd;
using preyvr::xrhold::StallTracker;

namespace {
using preyvr::test::Require;
constexpr std::uint64_t kMs = 1000000;
constexpr std::uint64_t kThreshold = 100 * kMs;
}

int main()
{
    StallTracker tracker;
    std::optional<StallEnd> ended;

    Require(!tracker.Observe(5000 * kMs, 0, kThreshold, ended) && !ended, "no frame yet is never a stall");
    Require(!tracker.Observe(1050 * kMs, 1000 * kMs, kThreshold, ended) && !tracker.InStall(), "inside the threshold: no hold");
    Require(!tracker.Observe(990 * kMs, 1000 * kMs, kThreshold, ended), "a clock behind the stamp is not a stall");

    Require(tracker.Observe(1100 * kMs, 1000 * kMs, kThreshold, ended) && tracker.InStall() && !ended,
            "at the threshold a stall begins and a frame is held");
    tracker.CountHeldFrame();
    Require(tracker.Observe(1400 * kMs, 1000 * kMs, kThreshold, ended) && !ended, "it continues while no frame comes");
    tracker.CountHeldFrame();

    Require(!tracker.Observe(1405 * kMs, 1402 * kMs, kThreshold, ended), "a newer frame stops the holding");
    Require(ended && ended->durationNs == 402 * kMs && ended->heldFrames == 2, "and reports the stall: 402 ms, 2 frames");
    Require(!tracker.InStall(), "the stall is over");
    Require(!tracker.Observe(1406 * kMs, 1402 * kMs, kThreshold, ended) && !ended, "it is reported once");

    Require(tracker.Observe(2000 * kMs, 1402 * kMs, kThreshold, ended), "a second stall from that frame");
    tracker.CountHeldFrame();
    // Polled late: one frame came and the next is already overdue.
    Require(tracker.Observe(2300 * kMs, 2100 * kMs, kThreshold, ended), "a frame that is itself followed by a stall holds again");
    Require(ended && ended->durationNs == 698 * kMs && ended->heldFrames == 1, "after ending the first stall at that frame");
    Require(!tracker.Observe(2310 * kMs, 2305 * kMs, kThreshold, ended) && ended && ended->heldFrames == 0,
            "and the new stall counts its own frames");

    tracker.Observe(3000 * kMs, 2305 * kMs, kThreshold, ended);
    Require(!tracker.Observe(3001 * kMs, 0, kThreshold, ended) && !tracker.InStall() && !ended,
            "a session that ends drops the stall without a report");

    std::cout << "hold frame stall tracking passed\n";
    return 0;
}
