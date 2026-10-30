#pragma once

#include <windows.h>

#include <string>

// Per-eye temporal AA, phase 1 of docs/TEMPORAL-AA-SCOPE-2026-10-30.md.
//
// Prey keeps one temporal-AA timeline, and the both-eyes path runs it twice a
// frame, so each eye's resolve reads the other eye's history and previous
// camera (the "mirage" of H-022b). This gives each eye a timeline of its own:
// - a post-processing counter, the second eye two counts ahead, which also
//   gives each eye the whole jitter pattern;
// - its last resolve output, copied out after its submission and back into the
//   slot its next resolve reads;
// - its previous camera (CRenderView::SetPreviousFrameCamera) and the
//   renderer's previous view-projection.
//
// It acts only in a both-eyes frame, while a temporal AA mode is set
// (r_AntialiasingMode 2-5). Gated on the bodies in preyvr/TemporalAaNative.h.
// A frame that is not the one after the last, new history textures, or a frame
// that stopped after its first eye start both eyes afresh (one frame without
// history).
namespace preyvr::dll {

// Render thread, DualSceneStereo's both-eyes path, in this order: FrameBegin;
// for the first eye, then the second, EyeBegin (once the eye's camera is
// installed) and EyeEnd; then FrameEnd, also when a frame stops after its
// first eye (`complete` false).
void TaaLaneFrameBegin(void* renderer, void* view, unsigned long long frame);
void TaaLaneEyeBegin(void* renderer, void* view, int eye, const void* eyeCamera);
void TaaLaneEyeEnd(void* renderer, int eye, const void* eyeCamera);
void TaaLaneFrameEnd(void* view, bool complete);

// 1 on (the default, the menu's Temporal), 0 off. Any thread; the next frame takes
// it. The menu and taa.vr reach it through SetTemporalAntialiasing (VrMode.h),
// which also switches the AA mode.
DWORD SetTaaLane(unsigned enabled);
bool TaaLaneEnabled();

// "taaLane={...}": taa.vr, taa.report and the status line. Any thread.
std::string TaaLaneReport();

} // namespace preyvr::dll
