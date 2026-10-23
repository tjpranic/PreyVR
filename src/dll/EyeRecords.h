#pragma once

#include "preyvr/RenderContract.h"
#include "preyvr/StereoCamera.h"

#include <cstdint>
#include <string>

// What each stereo eye and each frame was built as, kept for the threads that
// draw them later. The main render hook (MainRenderHook.h) and BuildDualEyeCamera
// publish; the render thread (XrSessionHost, DualSceneStereo) and the near-pass
// fixes (NearViewStereo, NearParticleStereo) read them back. Each
// consumer identifies its eye from the data being rendered rather than from a
// mutable flag, which is a frame ahead of the render thread (H-022).
namespace preyvr::dll {

// One immutable record of a built eye: where that eye's camera ended up, the
// right axis it was offset along, and which eye it is.
//
// **Why content and not a tag.** `LastRenderedEye()` is a mutable game-thread
// global, and the render thread is about a frame behind it, so a consumer that
// reads it may be packing a view built for the *other* eye while every counter
// stays valid (H-022). Matching a view-info's own camera position against these
// records identifies the eye from the data being rendered, which cannot go stale
// because it IS the thing being rendered.
struct BuiltEye {
    float position[3];
    float right[3];
    // The camera this eye was offset FROM: the cyclops the game built the frame
    // around. Camera-space content (the near pass) is relative to it.
    float centre[3];
    int eye;
    unsigned long long serial;
};
// Records an eye as its camera is built, by the thread that built it, so the
// record holds the exact bits the render view will carry. `centre` is the camera
// it was offset from.
void PublishBuiltEye(const stereo::Matrix34& camera, const stereo::Matrix34& centre, int eye);

// Finds the record whose camera position matches `position`. False when no eye
// matches, which a caller must treat as "refuse", never as "assume the latest".
bool FindBuiltEyeByPosition(const float position[3], BuiltEye& out);
unsigned long long BuiltEyeLookupMissCount();

// Records a both-eyes frame's head camera, on the main thread before the frame
// renders.
void PublishFrameCentre(const stereo::Matrix34& camera);

// True when `position` is the camera of a recent both-eyes frame: the head
// camera the engine prepares it from, left on the frame before either eye is
// built. Content built from it then (DrawNear particle geometry) is relative to
// the cyclops already.
bool IsDualFrameCentre(const float position[3]);

// Queues this frame's render contract: which eye, its pose and its frustum. The
// main render hook publishes one per native render while stereo is armed,
// refusals and flat frames included, so the queue stays in step with the frames.
void PublishRenderContract(const RenderContract& contract);

// The render contract of the frame the render thread has just finished -- which
// eye it is, and its pose -- or false if the game thread has not published one.
//
// The camera hook publishes the eye it built alongside each frame, and this pops
// them in order. The engine's MT/RT double buffer delays work without reordering
// it, so the eye arrives with its frame and submission never has to wait to find
// out which one it has -- which is what let the dwell go, and with it the 11 Hz
// per-eye refresh that made the first stereo test judder.
//
// **Not always 1:1.** Through a level-transition load Prey's main loop keeps
// rendering while the render thread ends the frame once, so a both-eyes frame
// whose camera was noted (NoteDualFrameCamera) takes the record carrying that
// camera and drops the older ones (RenderContractQueue). Any other frame takes
// the oldest.
//
// Call exactly once per finished frame. Consuming twice would advance past a
// frame and swap the eyes from then on.
bool ConsumeRenderedContract(RenderContract& out);

// The head camera (CCamera bytes, Matrix34 first) of the both-eyes frame the
// render thread is drawing, noted from its own frame info; the next
// ConsumeRenderedContract identifies the frame's record by it. Render thread.
void NoteDualFrameCamera(const std::uint8_t* camera);

// Clears the queue. Call when arming, so a stale backlog from a previous run
// cannot decide the first few frames' eyes.
void ResetEyeHandoff();

// Records queued -- the pipeline depth in frames. It should sit at a small
// constant; a load can raise it until the next identified frame drops the backlog.
unsigned long long EyeHandoffLag();

// Frames finished with nothing queued -- the render thread ran ahead.
unsigned long long EyeHandoffStarvedCount();

// Records dropped, oldest first, because the queue was full.
unsigned long long EyeHandoffDroppedCount();

// " handoff=" queued/pushed/matched/resyncs/skipped/unmatched/starved/dropped.
std::string EyeHandoffReport();

} // namespace preyvr::dll
