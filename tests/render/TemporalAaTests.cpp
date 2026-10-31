// The arithmetic of Prey's temporal-AA state (render/taa/TemporalAa.h) against a
// model of the engine as its bytes describe it (R-136): one counter picks the
// history slot written and read, the previous-camera ring slot and the jitter
// sample; post-processing advances it. Rendered twice a frame, each eye reads
// the other's; with a counter per eye two apart and each eye's output kept, as
// TemporalAaLane does it, each eye reads only its own.
#include "render/taa/TemporalAa.h"
#include "TestSupport.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <set>

namespace {
namespace taa = preyvr::taa;
using preyvr::test::Check;

int Tag(int eye, int frame) { return frame * 2 + eye; }

// The engine's resolve and the end of its post-processing, for one submission.
struct Engine {
    std::uint32_t counter = 0;
    std::uint32_t stamp = 0;
    std::array<int, 2> history{-1, -1};
    std::array<int, 4> ring{-1, -1, -1, -1};

    struct Seen {
        bool fresh;
        int history;    // what the resolve blended in: its history, or this frame without one
        int ring;       // the previous camera it reprojected with
        unsigned jitter;
        std::uint32_t counter;
    };
    Seen Submit(int tag)
    {
        const std::uint32_t c = counter;
        Seen seen{taa::HistoryFresh(c, stamp), tag, ring[taa::RingIn(c)], c & 3u, c};
        if (seen.fresh) { seen.history = history[taa::HistoryIn(c)]; }
        history[taa::HistoryOut(c)] = tag;
        ring[taa::RingOut(c)] = tag;
        stamp = c;
        counter = taa::Advance(c);
        return seen;
    }
};

// TemporalAaLane's part: the eye's counter in, its own output back into the
// slot its resolve reads (or a stale stamp when it has none), its output kept.
struct Lane {
    std::array<std::uint32_t, 2> counter{};
    std::array<int, 2> saved{-1, -1};
    std::array<bool, 2> fresh{};

    void Start(const Engine& engine)
    {
        const auto counters = taa::StartCounters(engine.counter);
        counter = {counters.first, counters.second};
        fresh = {false, false};
    }
    Engine::Seen Submit(Engine& engine, int eye, int tag)
    {
        const std::uint32_t in = counter[eye];
        engine.counter = in;
        engine.stamp = fresh[eye] ? taa::FreshStamp(in) : taa::StaleStamp(in);
        if (fresh[eye]) { engine.history[taa::HistoryIn(in)] = saved[eye]; }
        const auto seen = engine.Submit(tag);
        counter[eye] = taa::Advance(in);
        if (engine.stamp == in) {
            saved[eye] = engine.history[taa::HistoryOut(in)];
            fresh[eye] = true;
        }
        return seen;
    }
};

void TestArithmetic()
{
    for (const std::uint32_t c : {0u, 5u, 0xFFFFFFu}) {
        Check(!taa::HistoryFresh(c, taa::StaleStamp(c)), "a stale stamp makes the resolve start afresh");
        Check(taa::HistoryFresh(c, taa::FreshStamp(c)), "a fresh stamp makes it use its history");
        Check(taa::HistoryFresh(c, c - 1) && taa::HistoryFresh(c, c + 1) && taa::HistoryFresh(c, c - 2),
              "the other eye's stamp, a count either side, leaves history in use");
    }
    Check(!taa::RunsResolve(0) && !taa::RunsResolve(1) && !taa::RunsResolve(-1), "no AA and SMAA 1X: no resolve");
    Check(taa::RunsResolve(2) && taa::RunsResolve(3) && taa::RunsResolve(4) && taa::RunsResolve(5) &&
              taa::RunsResolve(9),
          "SMAA 1TX, 2TX, TSAA, FXAA (and above 5, clamped): the resolve runs");
    Check(taa::StartCounters(0xFFFFFF).first == 0xFFFFFF && taa::StartCounters(0xFFFFFF).second == 1,
          "the second eye two ahead, in 24 bits");
    Check(taa::Advance(0xFFFFFF) == 0, "the counter wraps at 24 bits, as the post driver keeps it");
}

void TestEngineAlone()
{
    Engine engine;
    engine.counter = 100;
    for (int frame = 0; frame < 6; ++frame) {
        const auto first = engine.Submit(Tag(0, frame));
        const auto second = engine.Submit(Tag(1, frame));
        if (frame == 0) { continue; }
        Check(first.history == Tag(1, frame - 1) && first.ring == Tag(1, frame - 1),
              "alone, the first eye blends and reprojects the second eye's last frame");
        Check(second.history == Tag(0, frame) && second.ring == Tag(0, frame),
              "and the second eye the first eye's, from this frame");
        Check(first.jitter % 2 == 0 && second.jitter % 2 == 1, "each eye gets half the 4-sample pattern");
    }
}

// Without the lane's own stamps, the first eye at 0xFFFFFF reads the second's
// wrapped stamp as stale: one frame without history. The lane's stamps remove it.
void TestWrappedStamp()
{
    Check(!taa::HistoryFresh(0xFFFFFF, taa::Advance(0xFFFFFF, 1)), "the other eye's wrapped stamp reads as stale");
    Check(taa::HistoryFresh(0xFFFFFF, taa::FreshStamp(0xFFFFFF)), "the lane's own stamp does not");
}

void TestLane(std::uint32_t startCounter)
{
    Engine engine;
    engine.counter = startCounter;
    Lane lane;
    lane.Start(engine);
    std::array<std::set<unsigned>, 2> jitter;
    for (int frame = 0; frame < 12; ++frame) {
        for (int eye = 0; eye < 2; ++eye) {
            const auto seen = lane.Submit(engine, eye, Tag(eye, frame));
            if (frame == 0) {
                Check(!seen.fresh && seen.history == Tag(eye, frame), "a start blends no history");
                continue;
            }
            Check(seen.fresh, "after the start each eye's resolve uses its history");
            Check(seen.history == Tag(eye, frame - 1), "its own last output");
            Check(seen.ring == Tag(eye, frame - 1), "and its own previous camera");
            if (frame <= 4) { jitter[eye].insert(seen.jitter); }
        }
        Check(lane.counter[1] == taa::Advance(lane.counter[0], taa::kEyeLead), "the second eye stays two ahead");
    }
    Check(jitter[0].size() == 4 && jitter[1].size() == 4, "each eye walks the whole 4-sample pattern");
}

} // namespace

int main()
{
    TestArithmetic();
    TestEngineAlone();
    TestWrappedStamp();
    for (const std::uint32_t start : {100u, 0u, 0xFFFFFDu, 0xFFFFFEu, 0xFFFFFFu}) { TestLane(start); }
    std::cout << "temporal_aa: one counter, two eyes: each eye's own history, previous camera and jitter\n";
}
