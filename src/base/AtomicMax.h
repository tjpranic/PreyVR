#pragma once

// Raises a peak counter to `value` when that is higher. Relaxed: a peak is a
// diagnostic and orders nothing else.
//
// Header-only, with no preyvr/ includes, because some tests compile a lane's
// .cpp directly.
#include <atomic>
#include <type_traits>

namespace preyvr::dll {

template <class T>
void StoreMax(std::atomic<T>& peak, std::type_identity_t<T> value)
{
    T seen = peak.load(std::memory_order_relaxed);
    while (value > seen && !peak.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
    }
}

} // namespace preyvr::dll
