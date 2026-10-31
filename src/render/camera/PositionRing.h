#pragma once

#include <array>
#include <cmath>
#include <cstddef>

// **A ring of records found by where they are** (EyeRecords: the eyes built, and
// the head cameras of both-eyes frames). Each record carries `serial`, 1 for the
// first published and up from there, kept in slot `serial % N`, and
// `position[3]`. A slot holding another serial than the one it should was
// overwritten or never written, and is passed over.
namespace preyvr::ring {

// The newest of the last N records published (`newest` is the last serial, 0
// for none) within `tolerance` of `position` on every axis, or null.
template <class Record, std::size_t N>
const Record* FindNewest(const std::array<Record, N>& slots, unsigned long long newest, const float position[3],
                         float tolerance)
{
    const unsigned long long oldest = newest > N ? newest - N + 1 : 1;
    for (unsigned long long serial = newest; serial >= oldest && serial != 0; --serial) {
        const Record& candidate = slots[serial % N];
        if (candidate.serial == serial && std::fabs(candidate.position[0] - position[0]) <= tolerance &&
            std::fabs(candidate.position[1] - position[1]) <= tolerance &&
            std::fabs(candidate.position[2] - position[2]) <= tolerance) {
            return &candidate;
        }
    }
    return nullptr;
}

} // namespace preyvr::ring
