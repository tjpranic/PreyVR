#include "physics/swing/SwingStrike.h"

#include <algorithm>
#include <cmath>

namespace preyvr::swing {
namespace {
std::uint64_t Ns(float seconds) { return static_cast<std::uint64_t>(std::max(0.0f, seconds) * 1e9f); }
} // namespace

Box HeadBox(const Box& box, Vec3 grip, float length)
{
    const float half[3] = {box.half.x, box.half.y, box.half.z};
    unsigned k = 0;
    for (unsigned i = 1; i < 3; ++i) {
        if (half[i] > half[k]) { k = i; }
    }
    const float headHalf = std::clamp(length * .5f, 0.0f, half[k]);
    // The head is the end away from the grip.
    const float side = Dot(Sub(grip, box.centre), box.axis[k]) > 0 ? -1.0f : 1.0f;
    Box head = box;
    head.centre = Add(box.centre, Scale(box.axis[k], side * (half[k] - headHalf)));
    (k == 0 ? head.half.x : k == 1 ? head.half.y : head.half.z) = headHalf;
    return head;
}

Tier Classify(float speed, float chargeBonus, const StrikeSettings& s)
{
    Tier t{};
    if (!std::isfinite(speed) || speed < s.minSpeed) { return t; }
    t.strike = true;
    if (speed < s.heavySpeed) {
        const float span = std::max(s.heavySpeed - s.minSpeed, 1e-3f);
        t.scale = s.minScale + (1 - s.minScale) * (speed - s.minSpeed) / span;
        return t;
    }
    t.heavy = true;
    const float span = std::max(s.fullSpeed - s.heavySpeed, 1e-3f);
    const float bonus = std::isfinite(chargeBonus) ? std::max(0.0f, chargeBonus) : 0.0f;
    t.scale = 1 + bonus * std::clamp((speed - s.heavySpeed) / span, 0.0f, 1.0f);
    return t;
}

const char* DecisionName(Decision decision)
{
    switch (decision) {
    case Decision::strike: return "struck";
    case Decision::locked: return "locked";
    case Decision::tooSlow: return "too_slow";
    case Decision::drained: return "drained";
    case Decision::blocked: return "blocked";
    case Decision::failed: return "failed";
    }
    return "none";
}

float Approach(Vec3 velocity, Vec3 normal) { return -Dot(velocity, normal); }

Passes::Pass* Passes::Touch(const void* target, std::uint64_t now, float lockSeconds, bool& begun)
{
    begun = false;
    const auto lock = Ns(lockSeconds);
    Pass* free = nullptr;
    for (auto& p : passes_) {
        if (p.target == target) {
            if (now - p.last > lock) {   // the wrench was off it long enough: a new pass
                p = {target, now, now, false, 0};
                begun = true;
            }
            p.last = now;
            return &p;
        }
        if (!free && (!p.target || now - p.last > lock)) { free = &p; }
    }
    if (!free) { return nullptr; }
    *free = {target, now, now, false, 0};
    begun = true;
    return free;
}

void Passes::Expire(std::uint64_t now, float lockSeconds)
{
    const auto lock = Ns(lockSeconds);
    for (auto& p : passes_) {
        if (p.target && now - p.last > lock) { p = {}; }
    }
}

unsigned Passes::Active() const
{
    return static_cast<unsigned>(std::count_if(passes_.begin(), passes_.end(), [](const Pass& p) { return p.target != nullptr; }));
}

} // namespace preyvr::swing
