#include "preyvr/RoomScale.h"

#include "preyvr/StereoCamera.h"

#include <algorithm>
#include <cmath>

namespace preyvr::roomscale {
namespace {

float Level(const Vec3& v) { return std::hypot(v.x, v.y); }

} // namespace

Vec3 HeadOffsetWorld(const Vec3& headXr, const Vec3& originXr, float playSpaceYaw, float scale)
{
    const Vec3 local{(headXr.x - originXr.x) * scale, (headXr.y - originXr.y) * scale,
                     (headXr.z - originXr.z) * scale};
    return stereo::TrackingDirToEngine(local, playSpaceYaw);
}

Vec3 OriginShift(const Vec3& appliedWorld, float playSpaceYaw, float scale)
{
    if (!Finite(appliedWorld) || !std::isfinite(playSpaceYaw) || !(scale > 0.0f)) { return {}; }
    const Vec3 xr = stereo::EngineDirToTracking(appliedWorld, playSpaceYaw);
    return {xr.x / scale, xr.y / scale, xr.z / scale};
}

float LeanAllowance(float drop, const Settings& s)
{
    // A trunk of length L hinged at the hips by a: the head comes down
    // L(1 - cos a) and forward L sin a = sqrt(2 L d - d^2) for a drop d.
    const float d = std::isfinite(drop) ? std::clamp(drop - s.dropBand, 0.0f, s.trunk) : 0.0f;
    const float lean = std::sqrt(std::max(0.0f, 2.0f * s.trunk * d - d * d));
    const float bent = std::max(0.0f, s.deadzone) + std::clamp(lean, 0.0f, std::max(0.0f, s.maxLean));
    return std::max(std::max(0.0f, s.leanZone), bent);
}

float LeanDrop(float lead, const Settings& s)
{
    const float trunk = std::max(0.0f, s.trunk);
    const float l = std::isfinite(lead) ? std::clamp(lead, 0.0f, trunk) : 0.0f;
    return trunk - std::sqrt(std::max(0.0f, trunk * trunk - l * l));
}

Vec3 Follower::Update(const Vec3& offsetWorld, float drop, float dtSeconds, const Settings& s)
{
    jumped_ = false;
    if (!Finite(offsetWorld) || !std::isfinite(dtSeconds)) {
        Reset();
        return {};
    }
    const Vec3 offset{offsetWorld.x, offsetWorld.y, 0.0f};
    // A tracking-space change moves the head metres in one frame. Not a step:
    // refuse the frame; a recenter re-baselines the origin.
    if (havePrevious_ && Level(Sub(offset, previous_)) > s.maxJump) { jumped_ = true; }
    const bool first = !havePrevious_;
    previous_ = offset;
    havePrevious_ = true;
    const bool sane = dtSeconds > 0.0f && dtSeconds <= 0.25f;
    // Still: within stillRadius of where the head rests, which trails it.
    if (first || jumped_) {
        rest_ = offset;
        restDrop_ = std::isfinite(drop) ? drop : 0.0f;
        stillSeconds_ = 0.0f;
        settling_ = false;
    } else if (sane) {
        const float k = std::min(1.0f, dtSeconds / std::max(s.restSeconds, 1e-3f));
        rest_ = {rest_.x + (offset.x - rest_.x) * k, rest_.y + (offset.y - rest_.y) * k, 0.0f};
        if (std::isfinite(drop)) { restDrop_ += (drop - restDrop_) * k; }
        if (Level(Sub(offset, rest_)) > s.stillRadius) {
            stillSeconds_ = 0.0f;
            settling_ = false;
        } else {
            stillSeconds_ += dtSeconds;
        }
    }
    if (jumped_ || !sane) { return {}; }
    const bool still = stillSeconds_ >= s.settleDelay;
    const float distance = Level(offset);
    // Standing still over the body: that is upright.
    if (still && distance <= 2.0f * s.deadzone && std::isfinite(drop)) {
        upright_ += (restDrop_ - upright_) * std::min(1.0f, dtSeconds / std::max(s.uprightSeconds, 1e-3f));
    }
    float step = 0.0f;
    const float excess = distance - LeanAllowance(drop, s);
    if (excess > s.jitter) {
        // Out of the lean zone: followed at once.
        settling_ = false;
        step = excess;
    } else {
        // At rest, a head lowered as a lean that far out lowers it is a lean,
        // and held; an upright one was a step. Begun, the glide runs until the
        // capsule is under the head or the head moves on.
        if (still && !settling_ && distance > s.deadzone + s.jitter) {
            const float lowered = std::isfinite(drop) ? restDrop_ - upright_ : 0.0f;
            settling_ = lowered < 0.5f * LeanDrop(distance, s) + s.uprightSlack;
        }
        if (settling_) {
            step = std::min(distance - s.deadzone, std::max(0.0f, s.settleSpeed) * dtSeconds);
            if (!(step > s.jitter)) { settling_ = false; }
        }
    }
    if (!(step > s.jitter)) { return {}; }
    step = std::min(step, s.maxSpeed * dtSeconds);
    return {offset.x / distance * step, offset.y / distance * step, 0.0f};
}

void Follower::Applied(const Vec3& applied)
{
    if (!havePrevious_ || !Finite(applied)) { return; }
    previous_ = {previous_.x - applied.x, previous_.y - applied.y, 0.0f};
    rest_ = {rest_.x - applied.x, rest_.y - applied.y, 0.0f};
}

void Follower::Reset()
{
    jumped_ = false;
    havePrevious_ = false;
    previous_ = {};
    rest_ = {};
    restDrop_ = 0.0f;
    stillSeconds_ = 0.0f;
    settling_ = false;
}

bool NoDeeper(float depthThere, float depthHere)
{
    if (!std::isfinite(depthThere)) { return false; }
    if (depthThere <= 0.0f) { return true; }
    // From clear, any overlap refuses; from an overlap, only a deeper one.
    return std::isfinite(depthHere) && depthHere > 0.0f && depthThere <= depthHere + kDepthSlack;
}

bool BodyMoving(const Vec3& velocity, const Vec3& requested)
{
    // A non-finite reading counts as moving: keeping the motion is the safe side.
    return !Finite(velocity) || !Finite(requested) || Length(velocity) > kMovingSpeed ||
           Length(requested) > kMovingSpeed;
}

} // namespace preyvr::roomscale
