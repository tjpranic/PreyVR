#pragma once

// A float out of a byte buffer, by its index in floats, without aliasing the bytes.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace preyvr {

inline float ReadFloat(std::span<const std::uint8_t> bytes, std::size_t index)
{
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + index * sizeof(float), sizeof(float));
    return value;
}

} // namespace preyvr
