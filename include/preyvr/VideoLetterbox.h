#pragma once
#include <cstdint>

// **Prey's fullscreen videos in both eyes** (preyvr/FullscreenVideoNative.h).
//
// Prey draws a playing fullscreen Bink after the world, straight through D3D11,
// so in a both-eyes frame it reached the second eye only. When the draw
// letterboxes it first clears the whole target to black: the backbuffer then
// holds the video and nothing of the world, and a copy of it gives the held
// first eye exactly what the second eye has. A headset's eye image is never
// 16:9, so in VR the draw always letterboxes.
namespace preyvr::video {

// Whether the draw clears and letterboxes, decided as the draw decides it
// (0xEFAA2C..0xEFAA6B): a viewport of another size and another aspect, each
// size converted from a signed 32-bit int and divided in single precision. An
// unordered compare (a zero-size viewport) counts as the same aspect, as the
// draw's UCOMISS/JZ does.
inline bool Letterboxed(std::int32_t viewportWidth, std::int32_t viewportHeight, std::int32_t videoWidth,
                        std::int32_t videoHeight)
{
    if (viewportWidth == videoWidth && viewportHeight == videoHeight) { return false; }
    const float viewport = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
    const float video = static_cast<float>(videoWidth) / static_cast<float>(videoHeight);
    return viewport < video || viewport > video;
}

} // namespace preyvr::video
