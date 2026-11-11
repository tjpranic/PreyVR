#pragma once
#include <cstdint>
#include <cstddef>
namespace preyvr::hud::native {
inline constexpr std::uintptr_t SpriteDisplay=0x18BABA0,GetVariable=0x18ABE10;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
 {0x18BABA0,799,0x9F87BBA79DD8FDB1ull}, // SpriteDisplay
 {0x18ABE10,236,0x834AA285258F2231ull}, // GetVariable
 {0x183BDE0,155,0x33DA6756774D21F1ull}, // ReleaseValue
 {0x183F680,654,0xB433B16833B799AAull}, // ConvertValue
 {0x1894A30,448,0x59B5592ECBE9CBB5ull}, // CharacterHandle
 {0x1894530,300,0xE3153B243DAFBA7Full}, // ConstructHandle
 {0x189E270,680,0xC2BEAEEB8B2DE361ull}, // DisplayList
 {0x18BF0C0,217,0x805D7CA10465ABEEull}, // SpriteSetVisible
 {0x18B8ED0,8,0xD06ECF8F398DB06Dull}, // SpriteOwner
};
// **Whether Prey shows the psi meter** (WristLane, main thread; verify_native_wrist.py).
// The HUD refresh that opens the meters together (0x1726B50) and the psi refresh
// 0x15D8540 send psiOpen only while this predicate is true, and psiClose
// otherwise. Called with the player's psi owner (*(player + 0x678)), it is true
// when the owner's byte +0x39A is set or one of its 16 power slots (owner + 8 +
// 0x40) is learned (vtable +0x18 above -1, listed by 0x15B8140): not before the
// first neuromod. Two other paths open the meter without it (0x15D5D70 from the
// power code 0x15BB290 and from the setter 0x15D8FF0), so the wrist also keeps
// it while Prey has it open (_root.psiM.isOpen).
inline constexpr std::uintptr_t GetPlayer=0x157C990,PsiShown=0x15AD110;
inline constexpr std::size_t PlayerPsiOwner=0x678;
inline constexpr Contract PsiShownContracts[]={
 {0x157C990,48,0xB0D3EB821FBC9AB5ull}, // GetPlayer
 {0x15AD110,170,0x3161DCF404EDB0A3ull}, // PsiShown
 {0x15B8140,119,0x54C34CC86BD6EC8Dull}, // PowerList
};
}
