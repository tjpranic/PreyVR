#pragma once

// What runs every frame, on which thread, and in what order. Each seam is one
// engine hook that calls the functions below; the lists themselves, and the
// reasons for their order, are in FrameSchedule.cpp.
//
// **The main thread's frame**, in the order BodyFollow's frame-order probes
// record it:
//   1. ArkPlayer::Update (BodyFollow.cpp PlayerUpdateDetour): a snap turn queued
//      last frame lands, then the body follows.
//   2. ArkPlayerCamera::UpdateView (HeadTrackingHook.cpp): the snap turn's
//      fallback seam, then head tracking into the view.
//   3. The arm IK callback (AnimIkTakeover.cpp). Its place varies, and
//      BodyFollow counts which.
//   4. The gameplay seam (AimTakeover.cpp): PrepareGameplayFrame, the publish,
//      TickGameplayLanes.
//   5. CSystem::Render (MainRenderHook.cpp): TickMainRender, the stereo camera
//      edit, then BeforeEngineRender / Prey's render / AfterEngineRender. The
//      move lane queues the next snap turn here, and applies it late if no
//      earlier seam took it.
// Meanwhile the render thread runs TickRenderThread for the previous frame, and
// the worker thread runs TickWorker on its own at 50 Hz.
//
// **Not scheduled here, on purpose.** The snap-turn delivery and frame-order
// probes in 1-3, and each stereo fix's own render hook (NearViewStereo,
// NearParticleStereo, MarkerStereo, DualSceneStereo, DualOverlay,
// LoadingScreen), run where they do because their placement is the point.
#include "GameplayFrame.h"

#include <cstdint>

namespace preyvr::dll::schedule {

// Main thread, the gameplay seam, before the frame is published: the stages
// that change it.
void PrepareGameplayFrame(GameplayPoseFrame& frame, bool allowed);
// The same seam, after the frame is published: the gameplay lanes, in order.
// Only a published frame reaches them. `allowed` is tracking and the aim
// takeover both on; `tracking` alone gates the overlay.
void TickGameplayLanes(const GameplayPoseFrame& frame, bool allowed, bool tracking);

// Main thread, CSystem::Render, before the camera edit: the drains and the input
// lanes. Runs in menus and with stereo off.
void TickMainRender();
// Around Prey's own Render: the cull guard and the frame probe. Before takes the
// system Render was called with, and may be given null.
struct EngineRenderBracket {
    std::uint32_t handshake = 0;
};
EngineRenderBracket BeforeEngineRender(void* system);
void AfterEngineRender(const EngineRenderBracket& bracket);

// Render thread, RT_EndFrame, before the original: the services that need a
// frame boundary or Prey's backbuffer.
void TickRenderThread(void* renderer, unsigned long long frame);

// Worker thread (WorkerThread.h), every pass.
void TickWorker();

} // namespace preyvr::dll::schedule
