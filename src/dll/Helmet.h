#pragma once

#include <string>

// The zero-G helmet on the headset (preyvr/Helmet.h,
// docs/ZERO-G-SCOPE-2026-10-30.md).
//
// Prey's helmet rides its eye joint, which the headset never moves, so it stays
// level and faces the body. While VR drives the view, its model transform is
// rewritten before the render, so it is drawn on the headset's eye at Prey's
// mount, without the sway. VR Options' Space Helmet: Hidden hides it from the
// render alone and gives Prey's hide bits back after, so Prey's own state is
// never changed.
namespace preyvr::dll {

// VR start: checks the gate and hooks the character render for the size,
// once. A refusal leaves Prey's helmet and is reported, never fatal; a failed
// hook leaves Prey's size. PREYVR_HELMET=0: Prey's helmet, untouched.
void EnsureHelmet();
// VR stop: Prey's helmet again.
void StopHelmet();

// VR Options' Space Helmet: 1 shown on the head (the default), 0 hidden.
void SetHelmetShown(unsigned shown);
unsigned HelmetShown();
// VR Options' Helmet Height and Forward / Back: the helmet moved along the
// eyes' own axes, millimetres (+ up, + forward). Helmet Size: percent, scaled
// around its centre by the size hook on the character render.
void SetHelmetHeightMm(int mm);
int HelmetHeightMm();
void SetHelmetForwardMm(int mm);
int HelmetForwardMm();
void SetHelmetSizePercent(unsigned percent);
unsigned HelmetSizePercent();

// Main thread, before Prey's render (FrameSchedule): the helmet on this frame's
// view, or hidden for the render.
void PlaceHelmetBeforeRender();
// Main thread, after Prey's render: the hide bits given back; the frame's view
// is spent.
void EndHelmetFrame();

std::string HelmetReport();

} // namespace preyvr::dll
