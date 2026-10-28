#pragma once
#include <cstddef>
#include <cstdint>
// The player's body at the world's depth: its near pass given the world's depth,
// or its slot drawn in the world (preyvr/BodyDepth.h). Steam SHA 7d6e322f...11a7.
// Static contracts, checked by tools/re/verify_body_depth.py.
namespace preyvr::bodydepth::native {
// CEntityObject::Render, an entity slot's draw: RCX the slot, R8 the
// SRendParams, stack +0x30 the SRenderingPassInfo (it hands that on to the
// character's Render in R9). Its only caller is the entity's draw loop
// (0x97CF2F), once per rendered slot and pass; no vtable holds it. Hooked: for
// the player's slot in the main view the near bits below are clear for the call.
//
// It ORs the slot's object flags into the params' (0x973452..0x973471) and puts
// the params' back before it returns (0x9748A7). It takes the near branch from
// the slot's flags (0x9734AF, bit 1), which builds a camera-relative matrix
// (translation less the camera's, or the slot's camera-space position); without
// it the matrix is the slot's world transform (slot +0x08). The character draws
// inside the call: its Render (vtable +0xC0, call 0x9747F6), then RenderCHR
// (0x81D0D0) and its attachments (0x821FD0, the skins at 0x7AABE0, the
// skeleton attachments at 0x7C74E0), each near when the params carry
// ObjectNearest or the character CS_FLAG_DRAW_NEAR.
inline constexpr std::uintptr_t SlotRender=0x973400;
// The slot: its character (ICharacterInstance*, the CCharInstance itself), its
// object flags (copied into each render object) and its own flags (word).
inline constexpr std::size_t SlotCharacter=0x80,SlotObjectFlags=0xA8,SlotFlags=0xAC;
// ENTITY_SLOT_RENDER_NEAREST. Every slot update sets SlotObjectFlags' bit from
// it (0x9763F0, 0x97D050, the render update 0x97CA40).
inline constexpr std::uint16_t SlotRenderNearest=0x2;
// SRendParams::dwFObjFlags (qword) and FOB_NEAREST, the near pass's object flag
// (RenderCHR tests it at 0x81D13A; nearfx::kObjectNearest is the same bit).
inline constexpr std::size_t ParamsObjectFlags=0x80;
inline constexpr std::uint32_t ObjectNearest=0x800000;

// --- the near pass at the world's depth (body.depth 1) ---------------------------
// A render pass's SetViewport (CSceneRenderPass): RCX the pass, RDX the viewport
// (x, y, w, h, then min and max depth at +0x10). It stores that viewport at
// +0x40, its depth at +0x50 (0xF903A6); a pass whose flags (+0xAC) carry
// PassNearDepth gets [0, r_DrawNearZRange] there instead (0xF903C0), [0.9, 1]
// with reverse depth (bit 1, 0xF903D4). The scene stage sets that bit on three of
// its passes each frame before it calls this (0xF9DE3C). Hooked: a near pass is
// given the depth range of its viewport, as the other passes are.
inline constexpr std::uintptr_t SetViewport=0xF90390;
inline constexpr std::size_t PassFlags=0xAC,PassViewportDepth=0x50,ViewportDepth=0x10;
inline constexpr std::uint32_t PassNearDepth=0x1;
// UpdateNearestChange: RCX the renderer, EDX the drawn object's FOB_NEAREST
// shifted to NearestEntering. Its one caller is FX_ObjectChange (0xF39EB8), the
// legacy pipeline's switch into and out of the near pass. Entering, it saves the
// renderer's camera at +0x5620, draws with a near camera, marks the renderer
// near (+0x4888, 0x10000) and makes the pending viewport's max depth
// r_DrawNearZRange (+0x94B4, dirty +0x94B8, applied by 0xF12D90); leaving, it
// puts back the saved camera and that camera's own max depth (+0x5820, CCamera
// zRangeMax). Hooked: entering for a main-view eye, the max depth is the saved
// camera's, as on leaving.
inline constexpr std::uintptr_t NearestChange=0xF43D70;
inline constexpr std::uint32_t NearestEntering=0x10000;
inline constexpr std::size_t RendererNearestFlags=0x4888,RendererSavedCamera=0x5620;
inline constexpr std::size_t RendererViewportMaxDepth=0x94B4,RendererViewportDirty=0x94B8;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0x973400,5837,0x760EBD81A8B80FB9ull}, // SlotRender
    {0xF90390,173,0x19FBDF2BDC14C7EBull}, // SetViewport
    {0xF43D70,646,0x2E95227946B86A25ull}, // NearestChange
};
}
