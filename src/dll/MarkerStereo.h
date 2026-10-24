#pragma once

#include <windows.h>

#include <string>

// `markers.depth`: world markers (DanielleMarkers -- objectives, repair and
// danger icons, enemy awareness, points of interest) at the depth of what they
// mark instead of infinitely far. The arithmetic and the decisions are in
// preyvr/MarkerDepth.h.
//
// Two halves:
//   - main thread, CArkUIHUD::OnPreRender: each marker's projection is
//     recorded with its depth, and paired with the marker's clip (the movie
//     names each clip after the ID the engine passes it). The table holds, per
//     clip, how far it moves per metre of eye offset.
//   - render thread, inside the movie's one display callback, where both eyes
//     are drawn (HudLayer RedrawDisplay): each clip's `_x` is moved for the eye
//     about to be drawn, then put back.
// A clip the table does not name is drawn where Prey put it.
namespace preyvr::dll {

// 1 (default) on; 0 leaves every marker where Prey draws it (A/B).
DWORD SetMarkerDepth(unsigned enabled);
unsigned MarkerDepthEnabled();
// Marker Size (VR Options, marker_size), 50..200 percent: each marker the
// table names, icon and label, scaled about the point it marks for both eyes'
// draws and put back after. 100 leaves them at Prey's size.
DWORD SetMarkerSizePercent(unsigned percent);
unsigned MarkerSizePercent();
// Installs the main-thread hooks. Called when VR starts; a failure leaves the
// markers native and is reported, never fatal.
void EnsureMarkerDepth();

// Render thread, inside DanielleMarkers' display callback, around its two
// draws. `eye` is the submission's eye camera: 0 the first (left), 1 the
// second (right). Begin reads each named clip's position; Apply moves them for
// that eye; Restore puts them back. Each is a no-op when Begin found nothing.
void BeginMarkerEyes(void* root);
void ApplyMarkerEye(void* root, int eye);
void RestoreMarkerEyes(void* root);

std::string MarkerDepthReport();

} // namespace preyvr::dll
