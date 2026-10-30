#pragma once

// The zero-G helmet on the headset (docs/ZERO-G-SCOPE-2026-10-30.md, "The
// zero-G helmet").
//
// Prey hangs the helmet off its eye joint (camera_jnt) as a bone attachment,
// as it does the eye, `#camera`. Every frame it writes the helmet's sway as its
// relative default: a rotation and no offset (0x1327070). Without the sway the
// helmet is the joint turned by its K, so its mount on the eye is fixed. Put on
// the headset's eye at that mount, the helmet sits on the head as it sits on
// Prey's eye when nothing sways it.
#include "preyvr/HeadLamp.h"

#include <cmath>

namespace preyvr::helmet {

// The helmet on the eye with no sway: both on one joint.
inline Pose Mount(const Pose& eyeOnJoint, Quaternion helmetK)
{
    return headlamp::Mount(eyeOnJoint, headlamp::OnJoint(Pose{}, helmetK));
}

// Where a model transform is in the world, for a character drawn at `location`
// with `scale`. Prey's world placement does the same (0x7A2250).
inline Pose InWorld(const Pose& location, float scale, const Pose& model)
{
    return {Multiply(location.orientation, model.orientation),
            Add(location.position, Rotate(location.orientation, Scale(model.position, scale)))};
}

// The headset's eye in the character's model space: Prey's eye as the body
// draws it (`#camera`'s model transform), moved by the view's `offset` from
// Prey's eye and turned to the view's `rotation`, both in world axes, for a
// character drawn with `location`'s rotation and `scale`. Only differences
// are used: in first person Prey's view position is relative to the player
// entity (UpdateView 0x148B820 writes the entity's rotation times `#camera`;
// CView::Update 0x3D91C0 adds the entity's world position after it).
inline Pose EyeInModel(Quaternion location, float scale, const Pose& eyeModel, Quaternion rotation, Vec3 offset)
{
    const Quaternion back = Conjugate(Normalize(location));
    return {Normalize(Multiply(back, rotation)), Add(eyeModel.position, Scale(Rotate(back, offset), 1.0f / scale))};
}

// The helmet's model transform on that eye, at its mount, moved by `shift`
// along the eye's own axes (model units: x right, y forward, z up).
inline Pose OnEye(const Pose& eyeModel, const Pose& mount, Vec3 shift = {})
{
    return Compose(eyeModel, Compose(Pose{Quaternion{}, shift}, mount));
}

// A Matrix34 (row-major, the translation in the last column) as a pose and a
// uniform scale. False for one with no usable rotation or position.
inline bool FromMatrix(const float (&m)[12], Pose& pose, float& scale)
{
    const Vec3 x{m[0], m[4], m[8]}, y{m[1], m[5], m[9]}, z{m[2], m[6], m[10]};
    scale = Length(x);
    if (!std::isfinite(scale) || scale < 1e-3f) { return false; }
    const float inverse = 1.0f / scale;
    pose = {QuaternionFromBasis(Scale(x, inverse), Scale(y, inverse), Scale(z, inverse)), {m[3], m[7], m[11]}};
    return Finite(pose.position) && std::isfinite(pose.orientation.w);
}

} // namespace preyvr::helmet
