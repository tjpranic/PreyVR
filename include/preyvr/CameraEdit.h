#pragma once

#include "preyvr/EngineMap.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Reversible edits to a CCamera's 0x240 bytes.
//
// This is the policy half of the first write to Prey's render path. Everything
// before now has been read-only; the question this exists to answer is whether
// modifying CSystem::m_ViewCamera actually changes the rendered image, which is
// the precondition for every stereo design we have considered.
//
// Two deliberate constraints, both mirroring the wrench and interaction
// protocols that came before:
//
//   1. **Recorded and restored.** The caller captures all 0x240 bytes first and
//      puts them back afterwards, then verifies the restore byte for byte. A
//      restore that is not verified is a hope.
//   2. **Orthonormal.** This one is not style, it changes what the engine
//      renders. CCamera::UpdateFrustum calls an orthonormality check on the 3x3
//      and, if it fails, **negates all six frustum plane normals** -- so a
//      rotation that quietly denormalises the basis does not produce a slightly
//      wrong image, it inverts culling.
namespace preyvr::cameraedit {

inline constexpr std::size_t kCameraSize = engine::CameraLayout::size;

// The same orthonormality predicate the engine uses before deciding whether to
// flip its plane normals, so we can assert the property *before* writing rather
// than discover it from an inverted image. `epsilon` matches nothing in
// particular yet -- it is exposed so the value can be pinned once measured
// against a live camera.
bool RotationIsOrthonormal(std::span<const std::uint8_t> camera, float epsilon = 0.01f);

// **The engine's check has a hole, and this closes it.**
//
// An all-zero 3x3 satisfies every one of the engine's nine comparisons, because
// each one reduces to |0 - 0| <= epsilon. So `RotationIsOrthonormal` returning
// true does not mean the matrix is usable -- it means the engine would not flip
// its plane normals, which is a different and weaker statement. The tests pin
// both behaviours.
//
// Checked separately rather than folded in, because the value of the function
// above is precisely that it agrees with the engine byte for byte; adding our
// own conditions to it would destroy that.
bool RotationIsDegenerate(std::span<const std::uint8_t> camera, float minAxisLength = 0.5f);

// What a caller should actually gate a write on: orthonormal *and* not
// degenerate.
bool RotationIsSafeToWrite(std::span<const std::uint8_t> camera);

// A recorded copy of the bytes as they were, and the check that they came back.
struct RestorePoint {
    std::array<std::uint8_t, kCameraSize> bytes{};
    bool captured = false;
};

bool Capture(std::span<const std::uint8_t> camera, RestorePoint& out);

// Byte-exact. Not "close enough": the whole point of a bounded write protocol is
// that the game is left in precisely the state it was found in, and a partial
// restore is the failure mode that would poison every later measurement in the
// same session.
bool MatchesRestorePoint(std::span<const std::uint8_t> camera, const RestorePoint& point);

// Reads the translation column (m[3], m[7], m[11]) so a caller can log where the
// camera was without decoding the whole structure.
std::array<float, 3> PositionOf(std::span<const std::uint8_t> camera);

} // namespace preyvr::cameraedit
