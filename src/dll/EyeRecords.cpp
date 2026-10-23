#include "EyeRecords.h"

#include "Logger.h"

#include <array>
#include <atomic>
#include <cmath>
#include <sstream>

namespace preyvr::dll {
namespace {

// A small ring of recently built eyes, matched by camera position. Sized well
// past the game/render thread separation so a view in flight can still find the
// record that produced it.
constexpr unsigned int kBuiltEyeSlots = 16;
std::array<BuiltEye, kBuiltEyeSlots> gBuiltEyes{};
std::atomic<unsigned long long> gBuiltEyeSerial{0};
std::atomic<unsigned long long> gBuiltEyeCount{0};
std::atomic<unsigned long long> gBuiltEyeMisses{0};
// The head cameras of recent both-eyes frames, the camera the engine prepares
// those frames from (IsDualFrameCentre). Written on the main thread before the
// frame renders; a few frames deep for the 3D engine's jobs.
struct FrameCentre {
    float position[3];
    unsigned long long serial;
};
constexpr unsigned int kFrameCentreSlots = 8;
std::array<FrameCentre, kFrameCentreSlots> gFrameCentres{};
std::atomic<unsigned long long> gFrameCentreCount{0};

// ---------------------------------------------------------------------------
// Eye handoff: which eye is in the frame the render thread is finishing
// ---------------------------------------------------------------------------
//
// **The problem this replaces.** The submission path used to identify the eye by
// asking and waiting: set the lock, hold it for N frames so the game thread's
// camera edit could reach the render thread, then take the image. That works,
// and it is why the first stereo test was unambiguous -- but the wait is the
// whole cost. A full left-right cycle took 2*N rendered frames, so at a dwell of
// 4 each eye refreshed at about 11 Hz. That is the slideshow.
//
// **Why a queue is correct here.** The engine's MT/RT double buffer *delays*
// work but does not *reorder* it: the camera built for frame N is rendered
// before the camera built for frame N+1. So the eye does not need to be
// searched for or waited on -- it only needs to be carried alongside the frame.
// The game thread pushes the eye it just built, the render thread pops one per
// finished frame, and identity is exact with no dwell at all. Each eye then
// refreshes every 2 frames instead of every 2*N.
//
// **Order is an assumption, so it is measured rather than trusted.** `Lag()` is
// pushes minus pops, which is the pipeline depth in frames. It should sit at a
// small constant. If it drifts, the 1:1 correspondence this relies on is not
// holding, and the caller can say so instead of quietly rendering the wrong eye
// into the wrong socket -- which looks like broken stereo rather than like a bug
// in a queue, and would be miserable to diagnose from inside a headset.
namespace eyehandoff {

RenderContractQueue gQueue;
std::atomic<unsigned long long> gPushed{0}, gPopped{0}, gStarved{0}, gDropped{0};
std::atomic<unsigned long long> gMatched{0}, gResyncs{0}, gSkipped{0}, gUnmatched{0};
// The head camera of the frame the render thread doubled (NoteDualFrameCamera),
// taken by the Consume at that frame's RT_EndFrame. Render thread only.
std::array<float,12> gRenderedCamera{};
bool gHaveRenderedCamera = false;
void Publish(const RenderContract& contract) {
    ++gPushed;
    if(!gQueue.Push(contract)) ++gDropped;
}
bool Consume(RenderContract& contract) {
    const bool identified = gHaveRenderedCamera;
    gHaveRenderedCamera = false;
    if (identified) {
        std::size_t skipped = 0;
        if (gQueue.PopMatching(gRenderedCamera, contract, skipped)) {
            ++gPopped; ++gMatched;
            if (skipped != 0) {
                gSkipped += skipped;
                const auto resyncs = ++gResyncs;
                if (resyncs <= 8 || resyncs % 100 == 0) {
                    lifecycle::Log("preyvr_camera_edit eye_handoff resync skipped=" + std::to_string(skipped) +
                                   " resyncs=" + std::to_string(resyncs) +
                                   " dropped=" + std::to_string(gDropped.load()));
                }
            }
            return true;
        }
        // No record carries this frame's camera: the position fallback below,
        // as before identities, and logged so a systematic miss shows.
        const auto unmatched = ++gUnmatched;
        if (unmatched <= 4 || unmatched % 1000 == 0) {
            lifecycle::Log("preyvr_camera_edit eye_handoff unmatched=" + std::to_string(unmatched) +
                           " queued=" + std::to_string(gQueue.Size()) +
                           " nearestDiff=" + std::to_string(gQueue.NearestCameraDifference(gRenderedCamera)) +
                           " camera=" + std::to_string(gRenderedCamera[3]) + "," +
                           std::to_string(gRenderedCamera[7]) + "," + std::to_string(gRenderedCamera[11]));
        }
    }
    if(!gQueue.Pop(contract)) {++gStarved;return false;}
    ++gPopped;return true;
}
void Reset() {
    gQueue.Reset();gPushed=0;gPopped=0;gStarved=0;gDropped=0;
    gMatched=0;gResyncs=0;gSkipped=0;gUnmatched=0;
}

} // namespace eyehandoff

} // namespace

// Publishes an eye WITH the camera it produced, so a downstream consumer can
// identify it from content rather than from a mutable global. H-022: a global is
// a frame ahead of the render thread and every tag it carries is individually
// valid. Called by whichever thread built the camera, immediately, so the record
// holds the exact bits the render view will carry.
void PublishBuiltEye(const stereo::Matrix34& m, const stereo::Matrix34& centre, int eye)
{
    BuiltEye record{};
    record.position[0] = m[3];  record.position[1] = m[7];  record.position[2] = m[11];
    record.right[0] = m[0];     record.right[1] = m[4];     record.right[2] = m[8];
    record.centre[0] = centre[3]; record.centre[1] = centre[7]; record.centre[2] = centre[11];
    record.eye = eye;
    record.serial = gBuiltEyeSerial.fetch_add(1, std::memory_order_relaxed) + 1;
    gBuiltEyes[record.serial % kBuiltEyeSlots] = record;
    gBuiltEyeCount.store(record.serial, std::memory_order_release);
}

void PublishFrameCentre(const stereo::Matrix34& m)
{
    const unsigned long long serial = gFrameCentreCount.load(std::memory_order_relaxed) + 1;
    FrameCentre& slot = gFrameCentres[serial % kFrameCentreSlots];
    slot.position[0] = m[3]; slot.position[1] = m[7]; slot.position[2] = m[11];
    slot.serial = serial;
    gFrameCentreCount.store(serial, std::memory_order_release);
}

bool FindBuiltEyeByPosition(const float position[3], BuiltEye& out)
{
    const unsigned long long count = gBuiltEyeCount.load(std::memory_order_acquire);
    if (count == 0) { gBuiltEyeMisses.fetch_add(1, std::memory_order_relaxed); return false; }
    // Exact-ish: the render view holds a by-value copy of the camera we built,
    // so the bits should be identical. A small tolerance covers a copy that
    // passed through a float conversion without admitting a different eye --
    // the two eyes are an IPD apart, which is orders of magnitude wider.
    constexpr float kTolerance = 1e-4f;
    const unsigned long long newest = count;
    const unsigned long long oldest = newest > kBuiltEyeSlots ? newest - kBuiltEyeSlots + 1 : 1;
    for (unsigned long long serial = newest; serial >= oldest; --serial) {
        const BuiltEye& candidate = gBuiltEyes[serial % kBuiltEyeSlots];
        if (candidate.serial != serial) { continue; }
        if (std::fabs(candidate.position[0] - position[0]) <= kTolerance &&
            std::fabs(candidate.position[1] - position[1]) <= kTolerance &&
            std::fabs(candidate.position[2] - position[2]) <= kTolerance) {
            out = candidate;
            return true;
        }
        if (serial == 1) { break; }
    }
    gBuiltEyeMisses.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool IsDualFrameCentre(const float position[3])
{
    const unsigned long long newest = gFrameCentreCount.load(std::memory_order_acquire);
    // Same tolerance as FindBuiltEyeByPosition: a by-value copy of the camera.
    constexpr float kTolerance = 1e-4f;
    const unsigned long long oldest = newest > kFrameCentreSlots ? newest - kFrameCentreSlots + 1 : 1;
    for (unsigned long long serial = newest; serial >= oldest && serial != 0; --serial) {
        const FrameCentre& candidate = gFrameCentres[serial % kFrameCentreSlots];
        if (candidate.serial == serial &&
            std::fabs(candidate.position[0] - position[0]) <= kTolerance &&
            std::fabs(candidate.position[1] - position[1]) <= kTolerance &&
            std::fabs(candidate.position[2] - position[2]) <= kTolerance) {
            return true;
        }
    }
    return false;
}

unsigned long long BuiltEyeLookupMissCount()
{
    return gBuiltEyeMisses.load(std::memory_order_relaxed);
}

bool ConsumeRenderedContract(RenderContract& out) { return eyehandoff::Consume(out); }

void NoteDualFrameCamera(const std::uint8_t* camera)
{
    std::memcpy(eyehandoff::gRenderedCamera.data(), camera, sizeof(eyehandoff::gRenderedCamera));
    eyehandoff::gHaveRenderedCamera = true;
}

void ResetEyeHandoff()
{
    eyehandoff::Reset();
}

unsigned long long EyeHandoffLag()
{
    return eyehandoff::gQueue.Size();
}

std::string EyeHandoffReport()
{
    std::ostringstream out;
    out << " handoff=" << eyehandoff::gQueue.Size() << "q/" << eyehandoff::gPushed.load() << "pushed/"
        << eyehandoff::gMatched.load() << "matched/" << eyehandoff::gResyncs.load() << "resyncs/"
        << eyehandoff::gSkipped.load() << "skipped/" << eyehandoff::gUnmatched.load() << "unmatched/"
        << eyehandoff::gStarved.load() << "starved/" << eyehandoff::gDropped.load() << "dropped";
    return out.str();
}

unsigned long long EyeHandoffStarvedCount()
{
    return eyehandoff::gStarved.load(std::memory_order_relaxed);
}

unsigned long long EyeHandoffDroppedCount()
{
    return eyehandoff::gDropped.load(std::memory_order_relaxed);
}

void PublishRenderContract(const RenderContract& contract) { eyehandoff::Publish(contract); }

} // namespace preyvr::dll
