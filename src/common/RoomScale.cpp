#include "preyvr/RoomScale.h"

#include "preyvr/StereoCamera.h"

#include <algorithm>
#include <cmath>

namespace preyvr::roomscale {
namespace {

bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
float Horizontal(const Vec3& v) { return std::hypot(v.x, v.y); }

// The sweep's heights above the feet: shins clear of a low step, waist, chest.
constexpr std::array<float, 3> kHeights{0.5f, 1.0f, 1.5f};

} // namespace

Vec3 HeadOffsetWorld(const Vec3& headXr, const Vec3& originXr, float playSpaceYaw, float scale)
{
    const Vec3 local{(headXr.x - originXr.x) * scale, (headXr.y - originXr.y) * scale,
                     (headXr.z - originXr.z) * scale};
    return Rotate(stereo::YawQuaternion(playSpaceYaw), stereo::ToEngineSpace(local));
}

Vec3 OriginShift(const Vec3& appliedWorld, float playSpaceYaw, float scale)
{
    if (!Finite(appliedWorld) || !std::isfinite(playSpaceYaw) || !(scale > 0.0f)) { return {}; }
    const Vec3 engine = Rotate(stereo::Conjugate(stereo::YawQuaternion(playSpaceYaw)), appliedWorld);
    const Vec3 xr = Rotate(stereo::Conjugate(stereo::kOpenXrToEngine), engine);
    return {xr.x / scale, xr.y / scale, xr.z / scale};
}

Vec3 Follower::Update(const Vec3& headOffsetWorld, float dtSeconds, const Settings& settings)
{
    jumped_ = false;
    if (!Finite(headOffsetWorld) || !std::isfinite(dtSeconds)) {
        Reset();
        return {};
    }
    const Vec3 offset{headOffsetWorld.x, headOffsetWorld.y, 0.0f};
    // A tracking-space change moves the head metres in one frame. Not a step:
    // refuse the frame; a recenter re-baselines the origin.
    if (havePrevious_ && Horizontal(Vec3{offset.x - previous_.x, offset.y - previous_.y, 0.0f}) > settings.maxJump) {
        jumped_ = true;
        following_ = false;
    }
    previous_ = offset;
    havePrevious_ = true;
    if (jumped_ || dtSeconds <= 0.0f || dtSeconds > 0.25f) { return {}; }
    const float distance = Horizontal(offset);
    if (!following_) {
        if (distance <= settings.startRadius) { return {}; }
        following_ = true;
    }
    if (distance <= settings.stopRadius) {
        following_ = false;
        return {};
    }
    const float step = std::min(distance, settings.maxSpeed * dtSeconds);
    return {offset.x / distance * step, offset.y / distance * step, 0.0f};
}

void Follower::Reset()
{
    following_ = false;
    jumped_ = false;
    havePrevious_ = false;
    previous_ = {};
}

bool SweepRays(const Vec3& feet, const Vec3& move, const Settings& settings, Rays& out)
{
    const float length = Horizontal(move);
    if (!Finite(feet) || !Finite(move) || length < 1e-4f || std::fabs(move.z) > 1e-4f ||
        !(settings.radius > 0.0f) || !(settings.skin >= 0.0f)) {
        return false;
    }
    const Vec3 u{move.x / length, move.y / length, 0.0f};
    const Vec3 side{u.y, -u.x, 0.0f};   // to the right of the move
    const float r = settings.radius;
    std::size_t n = 0;
    for (float height : kHeights) {
        for (float lateral : {-r, 0.0f, r}) {
            // The capsule's surface at this lateral offset is `lead` ahead of
            // its centre line: r at the centre, 0 at the sides.
            const float lead = std::sqrt(std::max(0.0f, r * r - lateral * lateral));
            Ray& ray = out[n++];
            ray.lead = lead;
            ray.from = {feet.x + side.x * lateral, feet.y + side.y * lateral, feet.z + height};
            const float reach = lead + length + settings.skin;
            ray.to = {ray.from.x + u.x * reach, ray.from.y + u.y * reach, ray.from.z};
        }
    }
    return true;
}

float AllowedLength(float moveLength, const Rays& rays, const std::array<float, kRays>& hitDistances,
                    const Settings& settings)
{
    if (!std::isfinite(moveLength) || moveLength <= 0.0f) { return 0.0f; }
    float allowed = moveLength;
    for (std::size_t i = 0; i < kRays; ++i) {
        const float hit = hitDistances[i];
        if (!std::isfinite(hit)) { return 0.0f; }
        if (hit < 0.0f) { continue; }   // clear
        allowed = std::min(allowed, hit - rays[i].lead - settings.skin);
    }
    return std::max(0.0f, allowed);
}

} // namespace preyvr::roomscale
