#pragma once

// The flashlight on the headset's eye (docs/FLASHLIGHT-HEAD-SCOPE-2026-10-29.md).
//
// Prey hangs its lights off the eye joint (camera_jnt) as bone attachments, and
// the eye itself is another one there, `#camera`. Each rides the joint at its
// relative default, with its orientation turned by an extra quaternion K
// (Prey's bone update, 0x7A3390: model = joint * relative, then q * K). So the
// light's mount on the eye does not depend on the pose: carried over to the
// headset's eye, the light stays where Prey mounted it, and only the eye moves.
#include "base/VrMath.h"

namespace preyvr::headlamp {

// Compose(Inverse(p), Compose(p, child)) is child.
inline Pose Inverse(const Pose& p)
{
    const Quaternion inverse = Conjugate(Normalize(p.orientation));
    const Vec3 back = Rotate(inverse, p.position);
    return {inverse, {-back.x, -back.y, -back.z}};
}

// An attachment on its joint: its relative default, K on the orientation.
inline Pose OnJoint(const Pose& relative, Quaternion k)
{
    return {Multiply(relative.orientation, k), relative.position};
}

// The light on the eye, both on one joint: the joint cancels.
inline Pose Mount(const Pose& eyeOnJoint, const Pose& lightOnJoint)
{
    return Compose(Inverse(eyeOnJoint), lightOnJoint);
}

// Where Prey's eye is in the world, from where Prey placed the light. `scale`
// is the character's, which Prey's world placement applies to model offsets.
inline Vec3 EyeFromLight(const Pose& lightWorld, float scale, const Pose& mount)
{
    const Quaternion eye = Multiply(lightWorld.orientation, Conjugate(Normalize(mount.orientation)));
    return Sub(lightWorld.position, Scale(Rotate(eye, mount.position), scale));
}

// The light on an eye at `eye` looking `view` (world): the mount carried over.
inline Pose OnEye(Quaternion view, Vec3 eye, float scale, const Pose& mount)
{
    return {Multiply(view, mount.orientation), Add(eye, Scale(Rotate(view, mount.position), scale))};
}

} // namespace preyvr::headlamp
