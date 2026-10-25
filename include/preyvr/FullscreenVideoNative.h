#pragma once
#include <cstddef>
#include <cstdint>
// Prey's fullscreen videos (Bink: the first neuromod install, the ApexVision
// clips, the endings) in both eyes. Steam SHA 7d6e322f...11a7. See
// docs/STEREO-DUAL-SUBMISSION-2026-10-10.md. Static contracts, checked by
// tools/re/verify_fullscreen_video.py.
namespace preyvr::video::native {
// The render thread's draw of the playing fullscreen movie: render command 0x2E
// runs 0xEFA360, which jumps here with the Bink backend in RCX (RDX the movie,
// R8D handed on to the backend); nothing else reaches it. It asks the renderer
// for its viewport (GetViewport below, 0xEFAA0C). When the viewport has another
// size and another aspect than the video (0xEFAA2C..0xEFAA6B) it clears the
// whole target to black (0xEFAA8E: the colour clear runs on the engine's own
// view of its target, 0xF0DA40) and narrows the viewport to the video's aspect.
// The backend then draws the frame through D3D11 into the bound target
// (0xEFAB42), and the viewport is put back. Neither a 2D image nor a Flash
// movie, so a both-eyes frame had it in the second eye only. Hooked: after it,
// the backbuffer is copied into the held first eye.
inline constexpr std::uintptr_t FullscreenDraw=0xEFA9B0;
// CD3D9Renderer::GetViewport(this, int* x, int* y, int* w, int* h), on the
// static renderer the draw passes it (0xEFA9F9). The hook asks it too, before
// the draw, to know whether the draw will clear and letterbox.
inline constexpr std::uintptr_t GetViewport=0xF7B4B0,Renderer=0x2B24E80;
// The movie's Bink handle; a BINK starts Width, Height (U32), which the draw
// reads (0xEFAA11..0xEFAA22).
inline constexpr std::size_t MovieBink=0x08,BinkWidth=0x00,BinkHeight=0x04;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0xEFA9B0,742,0xD99CFA953105185Bull}, // FullscreenDraw
    {0xF7B4B0,146,0x3C5BAAC165C13172ull}, // GetViewport
};
}
