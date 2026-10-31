#pragma once

#include <string>

// The flashlight and the psychoscope's light on the headset's eye, like a head
// lamp (equipment/head/HeadLamp.h, docs/FLASHLIGHT-HEAD-SCOPE-2026-10-29.md).
//
// Prey places each light every frame where its attachment on the eye joint is
// (CEntityAttachment::ProcessAttachment). While VR drives the view, the player's
// two lights are placed on the headset's eye instead, at Prey's mount, with the
// view the headset is shown. Where the attachment pass falls in the frame
// varies: when it comes before the frame's view, the light keeps Prey's
// placement until the view, and is put on it before the render.
namespace preyvr::dll {

// VR start: installs the hook, gated on the three bodies the placement rests on.
// A failure leaves Prey's lights and is reported, never fatal.
// PREYVR_HEAD_LAMP=0: not hooked.
void EnsureHeadLamp();
// VR stop: Prey's placement again (the hook stays and passes through).
void StopHeadLamp();

// Main thread, before Prey's render (FrameSchedule): the lights the attachment
// pass met before this frame's view, put on it.
void PlaceHeadLampsBeforeRender();
// Main thread, after Prey's render: the frame's view is spent.
void EndHeadLampFrame();

std::string HeadLampReport();

} // namespace preyvr::dll
