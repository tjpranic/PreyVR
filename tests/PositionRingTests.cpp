// ring::FindNewest, the search under EyeRecords' two rings: newest first, the
// last N only, overwritten slots passed over, and the tolerance on each axis.
#include "preyvr/PositionRing.h"
#include "TestSupport.h"

#include <cstdint>
#include <iostream>

namespace {
using preyvr::test::Require;

struct Record {
    float position[3];
    unsigned long long serial;
    int tag;
};
constexpr std::size_t kSlots = 4;
using Ring = std::array<Record, kSlots>;

void Publish(Ring& ring, unsigned long long& newest, float x, int tag)
{
    ++newest;
    ring[newest % kSlots] = Record{{x, 1.0f, -2.0f}, newest, tag};
}

// The two loops EyeRecords had, for the comparison.
const Record* BuiltEyeLoop(const Ring& ring, unsigned long long count, const float p[3], float tolerance)
{
    if (count == 0) { return nullptr; }
    const unsigned long long oldest = count > kSlots ? count - kSlots + 1 : 1;
    for (unsigned long long serial = count; serial >= oldest; --serial) {
        const Record& c = ring[serial % kSlots];
        if (c.serial != serial) { continue; }
        if (std::fabs(c.position[0] - p[0]) <= tolerance && std::fabs(c.position[1] - p[1]) <= tolerance &&
            std::fabs(c.position[2] - p[2]) <= tolerance) {
            return &c;
        }
        if (serial == 1) { break; }
    }
    return nullptr;
}
const Record* FrameCentreLoop(const Ring& ring, unsigned long long newest, const float p[3], float tolerance)
{
    const unsigned long long oldest = newest > kSlots ? newest - kSlots + 1 : 1;
    for (unsigned long long serial = newest; serial >= oldest && serial != 0; --serial) {
        const Record& c = ring[serial % kSlots];
        if (c.serial == serial && std::fabs(c.position[0] - p[0]) <= tolerance &&
            std::fabs(c.position[1] - p[1]) <= tolerance && std::fabs(c.position[2] - p[2]) <= tolerance) {
            return &c;
        }
    }
    return nullptr;
}

void TestSearch()
{
    Ring ring{};
    unsigned long long newest = 0;
    const float at[3] = {0.5f, 1.0f, -2.0f};
    Require(preyvr::ring::FindNewest(ring, newest, at, 1e-4f) == nullptr, "nothing published, nothing found");
    Publish(ring, newest, 0.5f, 1);
    Publish(ring, newest, 0.7f, 2);
    Publish(ring, newest, 0.5f, 3);
    const Record* found = preyvr::ring::FindNewest(ring, newest, at, 1e-4f);
    Require(found && found->tag == 3, "the newest of two at the same place");
    const float nearby[3] = {0.5f + 5e-5f, 1.0f - 5e-5f, -2.0f};
    Require(preyvr::ring::FindNewest(ring, newest, nearby, 1e-4f) == found, "within the tolerance on every axis");
    const float off[3] = {0.5f, 1.0f, -2.0f + 2e-4f};
    Require(preyvr::ring::FindNewest(ring, newest, off, 1e-4f) == nullptr, "one axis out is no match");
    Publish(ring, newest, 0.9f, 4);
    Publish(ring, newest, 0.9f, 5);
    Publish(ring, newest, 0.9f, 6);
    found = preyvr::ring::FindNewest(ring, newest, at, 1e-4f);
    Require(found && found->tag == 3, "still found while among the last four");
    Publish(ring, newest, 0.9f, 7);
    Require(preyvr::ring::FindNewest(ring, newest, at, 1e-4f) == nullptr, "gone once overwritten");
    ring[(newest - 1) % kSlots].serial = 99;
    const float six[3] = {0.9f, 1.0f, -2.0f};
    found = preyvr::ring::FindNewest(ring, newest, six, 1e-4f);
    Require(found && found->tag == 7, "the newest match");
    ring[newest % kSlots].serial = 0;
    found = preyvr::ring::FindNewest(ring, newest, six, 1e-4f);
    Require(found && found->tag == 5, "a slot holding another serial is passed over");
    Require(preyvr::ring::FindNewest(ring, 2, six, 1e-4f) == nullptr, "only serials up to `newest` are searched");
}

void TestAgainstTheLoopsItReplaced()
{
    // Every count from empty past two laps, every slot state, every position.
    unsigned mismatches = 0;
    for (unsigned long long count = 0; count < 11; ++count) {
        Ring ring{};
        for (unsigned long long s = 1; s <= count; ++s) {
            ring[s % kSlots] = Record{{static_cast<float>(s % 3), 1.0f, -2.0f}, s, static_cast<int>(s)};
        }
        for (unsigned stale = 0; stale <= kSlots; ++stale) {
            Ring damaged = ring;
            if (stale < kSlots) { damaged[stale].serial += 7; }
            for (int x = 0; x < 4; ++x) {
                const float p[3] = {static_cast<float>(x), 1.0f, -2.0f};
                const Record* a = preyvr::ring::FindNewest(damaged, count, p, 1e-4f);
                mismatches += a != BuiltEyeLoop(damaged, count, p, 1e-4f) ? 1u : 0u;
                mismatches += a != FrameCentreLoop(damaged, count, p, 1e-4f) ? 1u : 0u;
            }
        }
    }
    Require(mismatches == 0, "the same record as both loops it replaced, in every case");
}

} // namespace

int main()
{
    TestSearch();
    TestAgainstTheLoopsItReplaced();
    std::cout << "position_ring tests passed\n";
    return 0;
}
