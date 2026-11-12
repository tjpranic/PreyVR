#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace preyvr::abilities::native {
// Steam PreyDll 7D6E322F...B05311A7. Sizes are complete bodies, not the first
// unwind fragment. Target-side decompiles and instruction receipts live in docs.
inline constexpr std::uintptr_t Update=0x15BC840,Activate=0x15B5D90,
 Select=0x15B8E00,Reticle=0x157CBB0,
 GetPower=0x157CB80,GetPlayer=0x157C990,Candidate=0x15BDA40;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
// Bodies the hooks rely on, called or not. Prey's psi input (FocusInputConsumer,
// with its FocusStart/FocusStop) selects, casts and stops what the hooks aim.
inline constexpr std::array<Contract,14> Contracts{{
 {0x15BC840,999,0x3E788181B6743410ull}, // PsiUpdate
 {0x15B5D90,1020,0xEE9DD9CC7F0C9A4Dull}, // PsiActivate
 {0x15B8E00,547,0x3C4034396569B343ull}, // PsiSelect
 {0x157CBB0,57,0x0A12AD3064071EE1ull}, // ReticleGetter
 {0x124DE50,266,0x1B17083941D00D7Eull}, // FocusStart
 {0x124DF60,359,0x9C1A238ED9C83EE0ull}, // FocusStop
 {0x157CB80,28,0x5C3294A8009DE842ull}, // PlayerPsiAccessor
 {0x157C990,48,0xB0D3EB821FBC9AB5ull}, // PlayerSingleton
 {0x10839F0,4,0xCBB562DA69740364ull}, // PsiAccessor
 {0x12C3370,5,0x86200411BFD324A4ull}, // EmbeddedPower
 {0x15AFE50,856,0xED80BA083E6DF07Eull}, // AreaTargetConsumer
 {0x15BDCA0,937,0xA0B14136606833C6ull}, // IndividualTargetConsumer
 {0x124D8F0,1050,0x87423E8631548DDFull}, // FocusInputConsumer
 {0x15BDA40,595,0x2EC7248F356447C2ull}, // IndividualCandidate
}};
}
