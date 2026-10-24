#pragma once

#include "preyvr/EngineMap.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace preyvr::test {

// A mapped image of a supported PreyDll as far as the plans can tell: every
// EngineMap landmark's expected bytes at its RVA, 0xCC everywhere else.
inline std::vector<std::uint8_t> BuildSyntheticSupportedImage()
{
    std::size_t size = 0;
    for (const auto& landmark : preyvr::engine::Landmarks()) {
        size = std::max(size, static_cast<std::size_t>(landmark.rva) + landmark.expected.size());
    }
    std::vector<std::uint8_t> image(size, 0xCC);
    for (const auto& landmark : preyvr::engine::Landmarks()) {
        std::copy(
            landmark.expected.begin(),
            landmark.expected.end(),
            image.begin() + static_cast<std::size_t>(landmark.rva));
    }
    return image;
}

} // namespace preyvr::test
