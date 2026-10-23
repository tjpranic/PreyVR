#pragma once

#include "preyvr/CameraEdit.h"

#include <Windows.h>

#include <array>
#include <cstdint>

// One stereo eye's camera, built from the head camera: the eye offset, and the
// projection the eye renders with -- synthetic, Prey's own, or the headset's.
// The main render hook (MainRenderHook.h) builds an eye with it every frame
// stereo is armed, and the render thread does for each eye of a both-eyes frame
// (BuildDualEyeCamera). What the built eye actually projects with is published
// here too, because nothing outside the process can see it.
namespace preyvr::dll {

// Turns `edited` (CCamera bytes, the head camera) into eye `eye`, half the IPD
// along the camera's own right axis, and writes its projection by the policy
// below. False when no projection could be built; `edited` may then be part
// written. Does not rebuild the frustum or publish the eye record: the caller
// does both.
bool BuildSyntheticEye(
    std::array<std::uint8_t, cameraedit::kCameraSize>& edited,
    int eye,
    float ipdMetres,
    float halfFovDegrees);

// **Render the frustum the headset asked for, instead of Prey's.** Measured on
// this machine: 41.25% of the submitted pixel rectangle lands inside the
// runtime's requested frustum, and the request is fully covered -- so the other
// 59% is rasterised and thrown away by the compositor (R-121).
//
// **This alone does not make anything faster.** At an unchanged target size it
// spends the reclaimed area on detail: the same pixels now cover a narrower
// field, so angular density rises. The speed comes from ALSO shrinking the
// target -- 1719x1857 carries the saved session's density against 2688x2880 --
// and the target is latched at session creation, so that is a launch argument,
// not a runtime one.
//
// **Off by default, and it should stay that way until the near pass follows.**
// The weapon model is drawn with its own FOV (`r_DrawNearFoV`), tuned against
// the old frustum; changing the scene without it leaves the weapon at the wrong
// scale. Culling and weapon scale need checking together, which is why this is
// a prototype behind an explicit opt-in rather than a new default.
//
// Takes precedence over `xr.native`. When the runtime frustum is not known --
// before `xrLocateViews` has answered, or if it answered something degenerate --
// this falls through to the previous behaviour and counts the miss, rather than
// inventing a frustum that would then be submitted as if it were the headset's.
DWORD SetRuntimeFrustum(unsigned int enabled);
unsigned long long RuntimeFrustumUsedCount();
unsigned long long RuntimeFrustumMissingCount();

// Scales how asymmetric the synthetic per-eye frustum is. 1.0 makes both eyes
// symmetric, so the *only* difference between them is the eye offset.
//
// Needed because the first A2 run could not be judged. With the default 1.1 the
// two eyes' frusta differ by ten degrees, which at 2560 px across a 100-degree
// field is about 256 px of uniform sideways shear -- and that completely masked
// the 64 mm eye separation it was supposed to be measuring. Two effects in one
// image is one effect too many.
//
// Set 1.0 to measure the eye offset alone; restore 1.1 to exercise the asymmetry
// path separately.
DWORD SetStereoAsymmetry(float outerScale);

// Builds the per-eye camera by translation alone, inheriting Prey's own
// projection rather than overwriting it with a synthetic one.
//
// **This is what makes the declared frustum honest.** PreyVR cannot change what
// Prey renders, so the frustum it declares to OpenXR has to be the one Prey drew
// with -- measured 2026-09-02 as 120 degrees horizontal by 88.507 vertical, zero
// asymmetry, confirmed against the player's FOV slider. Overwriting the eye
// camera's projection with a synthetic half-FOV breaks that correspondence, and
// breaks it invisibly: on a monitor the image merely looks a little wide, and it
// is only in a headset that it reads as wrong depth.
//
// Both eyes therefore share one frustum and differ by translation, which is also
// the only difference A2b ever measured.
//
// Off by default, so every measurement taken before this existed still means what
// it meant when it was taken.
DWORD SetNativeProjection(unsigned int enabled);
unsigned int NativeProjectionEnabled();

// The frustum the eye camera we last handed the renderer actually projects with,
// as tangent half-extents. False before any eye has been built.
//
// **The third quantity.** xr-tape can see what the runtime located and what we
// declared; it explicitly cannot see this, because the engine's projection never
// crosses the OpenXR boundary. The mod is the only place all three coexist, which
// is why the declared-vs-rendered assert has to live in-process.
bool RenderedEyeTangents(float& tanLeft, float& tanRight, float& tanUp, float& tanDown);

} // namespace preyvr::dll
