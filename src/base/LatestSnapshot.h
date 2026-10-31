#pragma once

#include "base/MonotonicClock.h"

#include <cstdint>
#include <mutex>
#include <shared_mutex>

namespace preyvr {

// All payload access is synchronized. A seqlock around ordinary C++ objects
// still has undefined behavior when a reader overlaps a writer. Readers here
// refuse contention instead of waiting on a producer in an engine callback.
//
// Readers share the lock. With an exclusive mutex, seven animation jobs
// reading the same snapshot refused each other, not just the publisher:
// measured live 2026-09-07 as ikBusy ~105/s against ~93 owner matches/s on a
// ~132 fps game. A reader now fails only while the publisher is writing.
template<class T> class LatestSnapshot {
public:
    void Publish(const T& value)
    {
        std::lock_guard lock(mutex_);
        value_ = value;
        present_ = true;
    }
    bool TryRead(T& out) const
    {
        std::shared_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock() || !present_) { return false; }
        out = value_;
        return true;
    }
    void Clear()
    {
        std::lock_guard lock(mutex_);
        present_ = false;
    }
private:
    mutable std::shared_mutex mutex_;
    T value_{};
    bool present_ = false;
};

} // namespace preyvr
