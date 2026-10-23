#pragma once

#include "preyvr/EngineMap.h"
#include "preyvr/VrMath.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// Turning an OpenXR head pose into a CCamera the engine will accept.
//
// This is the arithmetic that 6DoF and stereo both rest on, kept pure so it can
// be tested without a headset, a GPU, or a running game -- which matters,
// because a basis-change error here does not crash, it just renders a world that
// is subtly wrong in a way that is very hard to diagnose while wearing a
// headset.
namespace preyvr::stereo {

// ---------------------------------------------------------------------------
// Coordinate systems
// ---------------------------------------------------------------------------
//
// OpenXR is right-handed, **Y up**, with **-Z forward**.
// CryEngine is right-handed, **Z up**, with **+Y forward**.
//
// Derived rather than guessed. Writing the engine basis vectors in OpenXR terms:
//
//     engine X (right)   =  openxr X
//     engine Y (forward) = -openxr Z
//     engine Z (up)      =  openxr Y
//
// As a matrix whose rows are those expressions:
//
//     [ 1  0  0 ]
//     [ 0  0 -1 ]
//     [ 0  1  0 ]
//
// which is exactly Rx(+90 degrees) -- a proper rotation with determinant +1, not
// a mirror. That matters: a handedness flip would invert every cross product
// downstream, and `CCamera::UpdateFrustum` reacts to a non-proper basis by
// negating all six frustum plane normals.
//
// So the whole conversion is one rotation, and it composes with everything else
// using the quaternion algebra already in VrMath.
inline constexpr Quaternion kOpenXrToEngine{0.70710678118f, 0.0f, 0.0f, 0.70710678118f};

using preyvr::Conjugate;

Vec3 ToEngineSpace(Vec3 openXr);

// A rotation changes basis by conjugation, q_B * q * q_B^-1 -- not by rotating
// the axis vector, which is the easy mistake and produces a result that looks
// plausible until the head tilts.
Quaternion ToEngineSpace(Quaternion openXr);

Pose ToEngineSpace(const Pose& openXr);

// ---------------------------------------------------------------------------
// Matrix34
// ---------------------------------------------------------------------------
//
// Row-major 3x4 with the translation in column 3, matching CameraLayout and the
// layout CCamera::UpdateFrustum walks:
//
//     m[0] m[1] m[2]  m[3]        columns 0,1,2 are the basis vectors
//     m[4] m[5] m[6]  m[7]        column 3 is the position
//     m[8] m[9] m[10] m[11]
//
// Column 0 is right, column 1 is forward, column 2 is up. Confirmed by the
// engine's own e_CameraRotationSpeed yaw, which rotates columns 0 and 1 while
// leaving column 2 alone -- exactly what a Z-up yaw does.
using Matrix34 = std::array<float, 12>;

Matrix34 MatrixFromPose(const Pose& pose);

// The inverse. Needed to ask "where is the engine's camera right now?" so a
// per-eye offset can be composed onto it rather than replacing it -- which is
// what lets the eye construction be exercised against the game's own camera,
// including its pitch and roll, instead of a synthetic one.
//
// Branches on the largest diagonal term rather than using the single-branch
// trace form. That is not premature caution: the naive version loses precision
// as the trace approaches zero, which happens at exactly the 180-degree
// orientations a player reaches by turning around. The columns are (right,
// forward, up), matching MatrixFromPose's layout.
using preyvr::QuaternionFromBasis;

Pose PoseFromMatrix(const Matrix34& matrix);

// Composes an offset expressed in the camera's own frame -- +X right, +Y
// forward, +Z up -- onto a pose. Half the IPD along the camera's right axis is
// exactly a synthetic eye.
Pose OffsetInLocalFrame(const Pose& pose, const Vec3& localOffset);

Vec3 RightOf(const Matrix34& matrix);
Vec3 ForwardOf(const Matrix34& matrix);
Vec3 UpOf(const Matrix34& matrix);
Vec3 PositionOf(const Matrix34& matrix);

// ---------------------------------------------------------------------------
// Per-eye construction
// ---------------------------------------------------------------------------

// Where the play space sits in the game world, and how it is oriented. The
// position normally comes from the game's own eye point and the yaw from
// recentering, so that turning in the room and turning with the stick compose
// instead of fighting.
struct ReferenceFrame {
    Vec3 worldPosition{};
    float yawRadians = 0.0f; // about engine Z (up)
};

Quaternion YawQuaternion(float radians);

// **An engine-world point in tracking (OpenXR) space**, by the mapping one
// gameplay frame defines: the engine point `anchor` is where the tracked `head`
// sits, and `yawRadians` turns play space into the engine world
// (ReferenceFrame). The inverse of how the aim lane carries a tracked pose into
// the world, so a point along the aim ray lands where the controller points.
Vec3 EngineToTracking(const Vec3& world, const Vec3& anchor, float yawRadians, const Pose& head);
// Its inverse: a tracking-space point in the engine world. The position half of
// animik::ControllerWorldFromHead.
Vec3 TrackingToEngine(const Vec3& tracking, const Vec3& anchor, float yawRadians, const Pose& head);
// The same mapping for directions, which need only the yaw.
Vec3 TrackingDirToEngine(const Vec3& direction, float yawRadians);
Vec3 EngineDirToTracking(const Vec3& direction, float yawRadians);
// The tracking origin as a pose in the engine world, in engine axes: what maps
// ToEngineSpace(p) for a tracking point p to TrackingToEngine(p).
Pose TrackingOriginInWorld(float yawRadians, const Vec3& anchor, const Pose& head);

// The head-tracking reference generation (HeadTrackingHook) is odd while a
// recenter is under way, and a sample taken against another generation than the
// live one was made against a reference that has since moved.
inline bool Recentering(std::uint64_t generation) { return (generation & 1u) != 0; }
inline bool ReferenceCurrent(std::uint64_t sample, std::uint64_t live) { return sample == live && !Recentering(sample); }

// Which way an eye sits from the centre along the camera's right axis: eye 0
// (left) at -1, any other (right) at +1. Times half the IPD, its offset.
inline constexpr float EyeSign(int eye) { return eye == 0 ? -1.0f : 1.0f; }

// The yaw a recenter should store, extracted from a live head pose.
//
// **This is the FAIL-CAM-019 seam, and the method is chosen to make the bug
// unrepresentable rather than merely avoided.** The rule is that the stored
// reference is yaw-only while the live head pose stays full: strip roll from
// both and head tilt stops working; strip it from neither and a headset that was
// crooked at the moment of capture tilts the world for the rest of the session.
//
// Yaw is taken by rotating the engine's forward axis and projecting it onto the
// horizontal plane. That projection is **inherently immune to both roll and
// pitch** -- roll turns about the forward axis and cannot move it, and pitch
// moves it within the vertical plane that already contains it, so neither
// changes its horizontal direction. The immunity is a property of the
// construction, not an extra step that could be forgotten, and the tests pin it
// so that a later "simplification" to Euler decomposition fails loudly.
//
// Returns nothing when the head is near-vertical. Looking straight up or down
// leaves almost no horizontal component to take a direction from, so the answer
// would be noise amplified by `atan2`. The playbook's rule is to **reject rather
// than invent**: a caller that cannot get a yaw should keep the reference it
// already has, which is what a player looking at the ceiling expects anyway.
//
// Takes an OpenXR-space pose, matching EyePoseInWorld, so callers stay in one
// space and the conversion happens in exactly one place.
std::optional<float> RecenterYawFromHeadPose(const Pose& openXrHeadPose);

// The yaw a camera matrix is facing, about engine Z. Same construction as
// RecenterYawFromHeadPose -- the forward axis projected onto the horizontal plane
// -- so the two cannot disagree about what yaw means.
float CameraYawOf(const Matrix34& matrix);

// The one function the render hook needs: an OpenXR eye pose plus the reference
// frame, in engine space, ready to become a Matrix34.
//
// Composition order is reference-then-eye. The eye pose is expressed in the play
// space, so it must be rotated and translated *by* the reference frame, never
// the other way round -- reversing this yields a camera that orbits the world
// origin when the player walks, which is a spectacular and very confusing bug.
Pose EyePoseInWorld(const ReferenceFrame& reference, const Pose& openXrEyePose);

// The single eye point gameplay systems expect, from the two rendered eyes.
//
// Prey's own code (ArkPlayerCamera::UpdateView, R-009) produces one eye pose,
// and every consumer downstream -- aim rays, interaction queries, target
// selection -- assumes there is exactly one. Handing them the midpoint keeps
// those systems consistent with what is being rendered rather than biased toward
// whichever eye happened to be written last.
//
// The orientation is taken from the left eye rather than being interpolated:
// the two eye orientations are identical in every runtime we care about, and
// slerping two identical quaternions to produce a third is a way to introduce
// error rather than remove it. If they ever differ, that is worth noticing
// rather than averaging away.
Pose CyclopsPose(const Pose& leftEye, const Pose& rightEye);

// ---------------------------------------------------------------------------
// Writing into a live CCamera
// ---------------------------------------------------------------------------

inline constexpr std::size_t kCameraSize = engine::CameraLayout::size;

// Writes only the twelve matrix floats. Everything else in the structure --
// fov, dimensions, near/far, the asymmetry shifts, and all the cached derived
// state -- is left exactly as it was, because a per-eye write should change the
// smallest thing that produces the intended effect.
//
// The caller is still responsible for calling CCamera::UpdateFrustum afterwards;
// this function cannot, being pure. Skipping that leaves the cached planes
// describing the previous frame's camera.
bool WriteMatrix(std::span<std::uint8_t> camera, const Matrix34& matrix);

Matrix34 ReadMatrix(std::span<const std::uint8_t> camera);

} // namespace preyvr::stereo
