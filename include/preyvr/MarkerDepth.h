#pragma once

#include "preyvr/VrMath.h"

#include <array>

// World markers (DanielleMarkers: objectives, repair and danger icons, enemy
// awareness, points of interest) at the depth of what they mark.
//
// **The defect.** Prey places every world marker on the main thread, in
// CArkUIHUD::OnPreRender (0x1667440): it builds one view-projection from the
// frame's camera -- the head, with both eyes rendered from one frame -- and
// projects each marker's world point through 0x16659F0 to a normalized screen
// position, which the movie gets in 1920x1080 canvas units (`_x = X`). The
// movie is then drawn into both eyes at the same pixels, so every marker has
// zero disparity and reads as infinitely far.
//
// **The fix's arithmetic, here.** Each eye is the head moved half the IPD
// along the camera's right axis, with the same projection, so a point at view
// depth `w` moves sideways in that eye by `-offset * P00 / (2 * w)` in
// normalized units (P00 = 1 / tan of the half-width angle) and not at all
// vertically. The movie is drawn once per eye, so each marker's clip is moved
// by that much, in canvas units, for its eye's draw.
//
// **Which depth.** On screen, the target's own view depth. A marker whose
// target is off screen (or behind) is pinned by Prey near the screen edge, not
// over its target; it is shown at the target's DISTANCE, along the direction it
// is drawn in (decided 2026-10-21). One formula covers both: the depth a point
// at the target's distance has in the marker's displayed direction, which on
// screen is exactly the view depth. Markers behind walls keep their true depth.
//
// The HUD matrix (RVA 0x228CAA0) is 16 floats, column-major: clip row r is
// m[r], m[4+r], m[8+r], m[12+r]. 0x16659F0 uses rows 0 (x), 1 (y) and 3 (w,
// forward depth, positive in front): nx = (x/|w| + 1)/2, ny = 0.5 - y/(2|w|),
// clamped to [0.05, 0.95] for markers that ask for it. The projection is
// symmetric (LookAt times a perspective from m_fov and the projection ratio),
// so rows 0 and 1 are the camera's right and up axes scaled by P00 and P11.
//
// Free of engine and Windows types, so it is decided and tested headless.
namespace preyvr::markers {

using HudMatrix = std::array<float, 16>;

// Nearest a marker is drawn, metres: the world reticle's floor.
inline constexpr float kNearestMetres = 0.25f;

// The projection's horizontal and vertical scale (1 / tan of the half angles).
struct HudScale {
    float p00 = 0;
    float p11 = 0;
};
// False when the matrix is not a usable projection.
bool ScaleOf(const HudMatrix& m, HudScale& out);

// The point in the head camera's view space: x right, y up, z = w, the forward
// depth (negative behind).
Vec3 ViewPoint(const HudMatrix& m, const HudScale& scale, Vec3 world);

// The depth a marker is shown at, metres, from its target's view-space point
// and the normalized position Prey drew it at (after any edge clamp). Never
// below kNearestMetres. False for a degenerate point.
bool DisplayedDepth(const HudScale& scale, Vec3 view, float nx, float ny, float& depth);

// Canvas units a marker at `depth` moves for each metre its eye sits to the
// right of the head (left eye: -IPD/2, right eye: +IPD/2). `canvasPerUnit` is
// the canvas width one normalized screen unit spans (Prey's own conversion,
// 0x16657C0: canvas(1) - canvas(0)).
float CanvasShiftPerMetre(const HudScale& scale, float depth, float canvasPerUnit);

} // namespace preyvr::markers
