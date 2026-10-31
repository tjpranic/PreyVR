#include "body/BodyTurn.h"

#include "aim/MotionController.h"
#include "render/camera/StereoCamera.h"

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

namespace {
bool Usable(const Quaternion& q)
{
    const float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return std::isfinite(n) && n > 0.5f && n < 2.0f;
}
} // namespace

std::optional<Quaternion> LocalTurn(const Quaternion& current, float delta)
{
    if (!Usable(current) || !std::isfinite(delta)) { return std::nullopt; }
    return Normalize(Multiply(Normalize(current), stereo::YawQuaternion(delta)));
}

std::optional<float> LocalYawBetween(const Quaternion& from, const Quaternion& to)
{
    if (!Usable(from) || !Usable(to)) { return std::nullopt; }
    const Quaternion d = Multiply(Conjugate(Normalize(from)), Normalize(to));
    if (std::fabs(d.z) < 1e-9f && std::fabs(d.w) < 1e-9f) { return 0.0f; }   // a half turn about a level axis
    return controller::WrapAngle(2.0f * std::atan2(d.z, d.w));
}

Step TorsoStep(float referenceYaw, float torsoYaw, float minimum)
{
    if (!std::isfinite(referenceYaw) || !std::isfinite(torsoYaw) || !std::isfinite(minimum)) {
        return {};
    }
    const float delta = controller::WrapAngle(torsoYaw - referenceYaw);
    return {delta, std::fabs(delta) > minimum};
}

Step Limited(Step step, float maxStep)
{
    if (maxStep > 0.0f && std::fabs(step.delta) > maxStep) { step.delta = std::copysign(maxStep, step.delta); }
    return step;
}

bool NativeTurnWatch::Update(float yaw, std::uint64_t nowNs, float epsilon, std::uint64_t holdNs)
{
    if (!std::isfinite(yaw)) { return true; }
    if (valid_ && std::fabs(controller::WrapAngle(yaw - left_)) > epsilon) { untilNs_ = nowNs + holdNs; }
    Left(yaw);
    return nowNs < untilNs_;
}

bool NativeRotationWatch::Update(const Quaternion& rotation, std::uint64_t nowNs, float epsilon, std::uint64_t holdNs)
{
    if (!Usable(rotation)) { return true; }
    if (valid_) {
        const Quaternion a = Normalize(left_), b = Normalize(rotation);
        const float dot = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
        const float angle = 2.0f * std::acos(std::fmin(1.0f, dot));
        if (angle > epsilon) { untilNs_ = nowNs + holdNs; }
    }
    Left(rotation);
    return nowNs < untilNs_;
}

} // namespace preyvr::bodyturn
