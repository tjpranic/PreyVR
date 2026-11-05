#pragma once
#include <cstddef>
#include <cstdint>
// What Prey's CryAction AnimatedCharacter does with the player's animation while
// Prey stops the player's clock (the Favorites Wheel, aiming a psi power). Steam
// SHA 7d6e322f...11a7. See docs/TIME-STOP-SCOPE-2026-11-05.md. Static
// contracts, checked by tools/re/verify_time_stop.py.
namespace preyvr::timestop::native {
// CAnimatedCharacter::PrePhysicsUpdate (RCX the animated character), called
// only from the game object's pre-physics update (0x2C3050). It returns at once
// while the frame time (+0x1D0) is 0 or less, before PrepareAndStartAnimProc.
// Hooked: the player's animation is started there anyway.
inline constexpr std::uintptr_t PrePhysicsUpdate=0x4F8570;
// PrepareAndStartAnimProc(this, const QuatT* location): starts the character
// (+0x710) and its shadow (+0x728) on the location, once a renderer frame
// (+0x840) and only while +0x847 is set; true (AL) when it started them. Called.
inline constexpr std::uintptr_t StartAnimation=0x4F9A80;
// CAnimatedCharacter::UpdateTime: the frame time from the player's clock
// (timer 3) for the player, the game's (timer 1) for any other, and 0.0001 s
// for any other whose clock reads 0 or less. The player keeps 0. Why the hook
// is needed; not called.
inline constexpr std::uintptr_t UpdateTime=0x4FDD10;
// Of the animated character: the frame time (a double), the start's location
// (a QuatT: q xyzw, then t), and a byte that moves the start to the transform
// event instead (PrePhysicsUpdate starts the animation only while it is 0).
inline constexpr std::size_t FrameTime=0x1D0,Location=0x21C,StartsOnTransform=0x844;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0x4F8570,497,0xE9DF51400325E6ECull}, // PrePhysicsUpdate
    {0x4F9A80,424,0x7AAD1C355019C937ull}, // PrepareAndStartAnimProc
    {0x4FDD10,292,0x04E486933DCD9EC8ull}, // UpdateTime
    {0x157C990,48,0xB0D3EB821FBC9AB5ull}, // PlayerSingleton
};
}
