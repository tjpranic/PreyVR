#include "preyvr/BodyTurn.h"

#include "preyvr/MotionController.h"
#include "preyvr/StereoCamera.h"

#include <cmath>

namespace preyvr::bodyturn {

std::optional<float> EngineYawOf(const Quaternion& engineRotation)
{
    const Quaternion& q = engineRotation;
    const float lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.5f || lengthSquared > 2.0f) {
        return std::nullopt;
    }
    const Quaternion unit = Normalize(q);
    // Engine forward is +Y with Z up.
    const Vec3 f = Rotate(unit, Vec3{0.0f, 1.0f, 0.0f});
    const Vec3 u = Rotate(unit, Vec3{0.0f, 0.0f, 1.0f});
    // Looking down, the up axis points the way the face is turned; looking up,
    // away from it. Weighted by the pitch, the sum stays horizontal-forward
    // through +-90 degrees.
    float hx = f.x - f.z * u.x;
    float hy = f.y - f.z * u.y;
    if (hx * hx + hy * hy < 1e-8f) {
        hx = f.x;
        hy = f.y;
    }
    if (!std::isfinite(hx) || !std::isfinite(hy) || hx * hx + hy * hy < 1e-8f) {
        return std::nullopt;
    }
    return std::atan2(-hx, hy);
}

std::optional<Quaternion> LevelledTurn(const Quaternion& current, float delta)
{
    if (!std::isfinite(delta)) { return std::nullopt; }
    const auto yaw = EngineYawOf(current);
    if (!yaw) { return std::nullopt; }
    return stereo::YawQuaternion(controller::WrapAngle(*yaw + delta));
}

Step TorsoStep(float referenceYaw, float torsoYaw, float minimum)
{
    if (!std::isfinite(referenceYaw) || !std::isfinite(torsoYaw) || !std::isfinite(minimum)) {
        return {};
    }
    const float delta = controller::WrapAngle(torsoYaw - referenceYaw);
    return {delta, std::fabs(delta) > minimum};
}

} // namespace preyvr::bodyturn
