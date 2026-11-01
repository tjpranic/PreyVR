#pragma once

// The performance counter, for what is timed in QueryPerformanceFrequency ticks.
#include <windows.h>

#include <cstdint>

namespace preyvr::dll {

inline std::int64_t QpcNow()
{
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

} // namespace preyvr::dll
