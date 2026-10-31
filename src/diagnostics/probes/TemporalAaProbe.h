#pragma once

#include <string>

// The temporal-AA probe, phase 0 of docs/TEMPORAL-AA-SCOPE-2026-10-30.md.
//
// Around each top-level scene submission it reads the state a per-eye temporal
// history would have to separate: Prey's post-processing counter, the
// renderer's previous view-projection, the AA stage's last-resolve stamp, the
// pipeline's render view, the jitter, and the two history textures. It writes
// nothing to the engine, so it changes no pixel. Gated on the six bodies the
// reads rest on (render/taa/TemporalAaNative.h); other bytes leave it idle and say so.
namespace preyvr::dll {

// Which submission of a frame: one alone, or the first or second of a
// both-eyes frame (DualSceneStereo.h).
enum class TaaSubmission : int { single = 0, first = 1, second = 2 };

// Render thread, from RenderSceneDetour, around a top-level submission only.
void TaaProbeBefore(const void* renderer, const void* view, TaaSubmission kind);
void TaaProbeAfter(const void* renderer, const void* view, TaaSubmission kind);

// "taa={...}": taa.report and the status line. Any thread.
std::string TaaProbeReport();

} // namespace preyvr::dll
