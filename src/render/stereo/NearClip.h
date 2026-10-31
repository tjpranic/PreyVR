#pragma once
#include <algorithm>

// **Near Clip Distance: the view's near plane while VR drives the view.**
//
// CView::Update (0x3D91C0) builds the camera (CCamera::SetFrustum 0x2167E0) with
// its view's near plane, SViewParams +0x2C, when that is at least 1 cm
// (0x1C7F774), and otherwise with the view system's default, 10 cm
// (CViewSystem+0x90, set by its constructor 0x3DAB30). Prey's player view never
// sets one, so it is always 10 cm. Prey drew the body in its near pass at 3 cm;
// drawn in the world (render/stereo/BodyDepth.h), the body is cut at the view's near
// plane, so looking down at 10 cm shows the inside of the neck.
//
// The field persists in the view, so each frame decides from what is there: our
// own value is replaced or given back, a near plane Prey set itself is kept.
namespace preyvr::nearclip {

inline constexpr unsigned kMinCm = 1, kMaxCm = 10, kDefaultCm = 3;
// Below this CView::Update uses the view system's default.
inline constexpr float kEngineMinimum = 0.01f;

inline unsigned ClampCm(unsigned cm) { return std::clamp(cm, kMinCm, kMaxCm); }
inline float Metres(unsigned cm) { return 0.01f * static_cast<float>(ClampCm(cm)); }

enum class Action {
    write,   // set `value`: VR drives the view
    keep,    // Prey's own near plane: left as it is
    clear,   // ours, and VR no longer drives the view: back to the default (0)
    none,    // nothing of ours there, and nothing to do
};
struct Step {
    Action action = Action::none;
    float value = 0.0f;
};

// `current` the view's near plane after Prey's UpdateView, `written` the last
// one written (0 for none), `driving` whether VR drives the view.
inline Step Next(float current, float written, bool driving, unsigned cm)
{
    const bool ours = written != 0.0f && current == written;
    if (current >= kEngineMinimum && !ours) { return {Action::keep, current}; }
    if (!driving) { return ours ? Step{Action::clear, 0.0f} : Step{Action::none, current}; }
    return {Action::write, Metres(cm)};
}

} // namespace preyvr::nearclip
