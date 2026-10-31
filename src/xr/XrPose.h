#pragma once

#include "base/VrMath.h"

#include <windows.h>

#include <openxr/openxr.h>

// The project's Pose and OpenXR's XrPosef, field for field (both are a unit
// quaternion x, y, z, w and a position in metres), and the quad layer every
// panel of the session is submitted as. Header-only: plain copies.
namespace preyvr::dll {

inline XrPosef ToXrPose(const Pose& p)
{
    return {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
            {p.position.x, p.position.y, p.position.z}};
}

inline Pose FromXrPose(const XrPosef& p)
{
    return Pose{Quaternion{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
                Vec3{p.position.x, p.position.y, p.position.z}};
}

// A quad layer both eyes see: in `space` at `pose`, `size` metres, showing
// `rect` of `swapchain`.
inline void FillQuad(XrCompositionLayerQuad& layer, XrSpace space, const Pose& pose, XrExtent2Df size,
                     XrSwapchain swapchain, const XrRect2Di& rect, XrCompositionLayerFlags flags)
{
    layer.space = space;
    layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    layer.layerFlags = flags;
    layer.pose = ToXrPose(pose);
    layer.size = size;
    layer.subImage.swapchain = swapchain;
    layer.subImage.imageRect = rect;
}

} // namespace preyvr::dll
