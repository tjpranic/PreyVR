#pragma once
#include "base/VrMath.h"
#include <algorithm>
#include <cmath>

// **Prey's zooms magnify in the headset.**
//
// Prey zooms by narrowing the view's FOV: the player's zoom manager takes the
// psychoscope's, a scan's, a telescope's and others', and UpdateView turns the
// blended value into SViewParams' FOV. On a monitor that magnifies, because the
// picture stays the size of the screen. A headset's compositor shows an image
// over the angles its layer declares, so a narrow view declared as it is keeps
// the world's size and covers less of the lens, with black around it.
//
// So while Prey narrows the view, the frame stays declared at the FOV slider's
// frustum (the base). The image fills the lens and what it shows is magnified by
// base over rendered, as far as Prey's own zooms go. A view wider than the base,
// a sprint's, is declared as it is. Prey's overlay is in the image, so it keeps
// its size. The magnification also multiplies disparity, so the eyes are moved
// closer by the same factor and keep the separation the world has without a zoom.
// The poses given to the compositor stay the real eyes'. What is placed in the
// world beside the image is moved to where the image shows it (ShownPoint). See
// docs/PSYCHOSCOPE-ZOOM-SCOPE-2026-10-31.md.
namespace preyvr::viewzoom {

// Below this it is rounding between the view's FOV and the camera's copy of it,
// not a zoom: an unzoomed frame must stay exactly as it was.
inline constexpr float kNoZoomBelow = 1.001f;

// How much narrower a view with this tan(FOV/2) is than the base: 1 for a view at
// the base or wider, and without a base.
inline float Magnification(float tangent, float baseTangent)
{
    if (!(tangent > 0.0f) || !(baseTangent > 0.0f)) { return 1.0f; }
    const float magnification = baseTangent / tangent;
    return std::isfinite(magnification) && magnification >= kNoZoomBelow ? magnification : 1.0f;
}

// tan(FOV/2) of the view at the FOV slider. `fov` the view's vertical FOV, radians,
// as Prey's UpdateView made it from `hfov`, degrees; `slider` cl_hfov, degrees.
// UpdateView's fov is 2*atan(tan(hfov/2)*H/W), so this comes out for any eye
// shape: only the ratio of the two tangents is needed. False for values that are
// no view.
inline bool BaseTangent(float fov, float hfov, float slider, float& out)
{
    constexpr float kHalfDegree = 3.14159265358979f / 360.0f;
    const bool plausible = std::isfinite(fov) && std::isfinite(hfov) && std::isfinite(slider) && fov > 0.0f &&
                           fov < 3.14159f && hfov > 0.0f && hfov < 180.0f && slider > 0.0f && slider < 180.0f;
    if (!plausible) { return false; }
    out = std::tan(fov * 0.5f) * std::tan(slider * kHalfDegree) / std::tan(hfov * kHalfDegree);
    return true;
}

// Where a zoomed frame shows a point in the world: `head` the head it was rendered
// from, tracking space, axes as OpenXR's (-Z forward). The image is magnified about
// the view's axis, so the point moves out from it by the zoom at the depth it has:
// what is placed beside the image, a separate layer, stays on what it marks.
inline Vec3 ShownPoint(const Vec3& point, const Pose& head, float zoom)
{
    if (zoom == 1.0f) { return point; }
    const Quaternion& q = head.orientation;
    Vec3 local = Rotate(Quaternion{-q.x, -q.y, -q.z, q.w}, Sub(point, head.position));
    local.x *= zoom;
    local.y *= zoom;
    return Add(head.position, Rotate(q, local));
}

} // namespace preyvr::viewzoom
