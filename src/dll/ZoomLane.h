#pragma once

#include <cstdint>
#include <string>

// Prey's zooms in the headset (preyvr/ViewZoom.h).
namespace preyvr::dll {

// After Prey's UpdateView, on the main thread: `playerCamera` the camera it was
// given, `fov` the view's FOV. Notes the unzoomed frustum. `driving`: VR drives
// the view; otherwise there is no zoom to make, and every frame is declared as it
// is.
void NoteViewZoom(void* playerCamera, float fov, bool driving);

// The magnification of a view with this tan(FOV/2): 1 when it is unzoomed. Any
// thread: taken from the frame's own camera, so a frame's FOV, eyes and
// declaration agree whichever thread is building them.
float ViewZoomOf(float tangent);

// "zoom={gate= base= now= zooms= zoomed= unread=}": the magnification now, the
// zooms so far, the frames magnified, and those whose values could not be read.
std::string ZoomLaneReport();

} // namespace preyvr::dll
