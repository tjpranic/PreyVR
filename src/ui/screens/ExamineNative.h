#pragma once
#include <cstddef>
#include <cstdint>
// Prey's examination camera: the glide up to a keypad or terminal you use.
// Steam SHA 7d6e322f...11a7. See docs/KEYPAD-CAMERA-SCOPE-2026-10-26.md. Static
// contracts, checked by tools/re/verify_examine_camera.py.
namespace preyvr::examine::native {
// The fit (RCX the examination, RDX the Vec3 the camera goes to, R8 the Quat it
// faces, R9 the entity examined): for a world UI it puts the camera where the
// screen and `viewingBuffer` fill the view camera's FOV, facing the screen. AL
// nonzero is success; a world UI always succeeds. Two callers, both handing it
// the examination's +0x68: the begin (state 1, 0x15AB5E6) and every frame of an
// examination (0x15ABDA5). Hooked: a world UI's camera is moved back.
inline constexpr std::uintptr_t FitView=0x15AA660;
// Where each call returns: the begin's, with the view camera still the eye, and
// every frame's.
inline constexpr std::uintptr_t BeginReturn=0x15AB5EB,FrameReturn=0x15ABDAA;
// ArkPlayer +0x9B0, the examination (ArkExaminationMode).
inline constexpr std::size_t PlayerExamination=0x9B0;
// Of the examination: the camera's target position (+0x68, a Vec3, world),
// viewingBuffer (+0x7C), the type (+0x94: 1 world UI, 2 telescope, 3 other), the
// state (+0x98: 0 off, 1 on, 2 leaving) and the entity id (+0x9C). The begin
// sets the type and the entity before it calls the fit.
inline constexpr std::size_t ExamTarget=0x68,ExamViewingBuffer=0x7C,ExamType=0x94,ExamState=0x98,ExamEntity=0x9C;
inline constexpr int TypeWorldUi=1;
// State 1: what makes ArkPlayer::UpdateCachedReticleViewPosAndDir (0x1585320)
// build the player's reticle ray from the examination's cursor (+0x60), the ray
// a world UI's cursor is cast from (aim/WorldUiNative.h).
inline constexpr int StateOn=1;
struct Contract {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
inline constexpr Contract Contracts[]={
    {0x15AA660,1999,0x9C0A571B21F26875ull}, // ExamineFitView
    {0x15AB280,2394,0xF6A2F6E0342753D2ull}, // ExamineSetState
    {0x15ABD20,3874,0x2A5E9C69EEFABE70ull}, // ExamineUpdateView
};
}
