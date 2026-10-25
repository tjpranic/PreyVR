#pragma once
#include <cstddef>
#include <cstdint>
// The player's body drawn in the world instead of Prey's near pass. Steam SHA
// 7d6e322f...11a7. Static contracts, checked by tools/re/verify_body_depth.py.
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
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0x973400,5837,0x760EBD81A8B80FB9ull}, // SlotRender
};
}
