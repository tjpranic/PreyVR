#pragma once

#include <array>

// Mapping Prey's inventory from its own 3D units into headset metres.
//
// RE-INVENTORY-DEPTH-2026-09-11 established, from a live authorized probe of
// PID 18188, that the inventory is not a flat picture: 403 unique matrices per
// Display frame resolve, after the shared tilt is projected out, onto eight
// planes at these relative offsets in native UI units --
//
//   0, -1000, -2000, -2500, -5000, -7000, -9000, -15000
//
// -- identically in Display frames 1, 61 and 121, with a genuine 3D camera
// transform at Z of about -36882.86 native UI units. A tilted-plane control
// confirmed that translating within the panel does not manufacture a false
// extra plane.
//
// **These units are not metres and there is no recorded conversion.** The one
// relation available is the camera: the native camera sits `cameraUnits` from
// the zero plane and views the same content we place `panelMetres` away, so one
// native unit is `panelMetres / |cameraUnits|` metres.
//
// Deliberately free of engine and OpenXR types so the arithmetic is decided and
// tested headless, the same way ConsolePolicy is. Nothing here reads or writes
// game memory; it converts numbers a caller has already obtained.
namespace preyvr::inventory {

// Post-transform stereo correction for the Steam CalcTransMat3D output:
// row-major Matrix44, column vectors, row 3 contains homogeneous clip W.
// eyeOnPanel is the signed eye displacement in panel metres (left negative).
// Xclip += 2*eyeOnPanel/panelWidth * depthScale * (W-cameraUnits).
// This leaves the zero-depth plane fixed and preserves native clipping/masks.
// It avoids SetPerspective3D: Steam explicitly zeros its horizontal shifts.
// Returns false without modifying output on invalid input. Binocular panel
// prototype only; this is not a head-motion reprojection or a cylinder mapping.
bool ApplyStereoParallax(std::array<float,16>& clip, float cameraUnits,
                         float panelWidth, float eyeOnPanel, float depthScale);

} // namespace preyvr::inventory
