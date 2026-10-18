#pragma once
#include "preyvr/ContactQuery.h"
#include "preyvr/VrMath.h"
#include <array>
#include <cstdint>

// The free-swing wrench (docs/FREE-SWING-WRENCH-SCOPE-2026-10-18.md): what makes a
// contact a strike, and how hard. Pure, testable offline; the DLL half is
// SwingLane.cpp.
namespace preyvr::swing {
using contact::Box;

// The wrench's head: the `length` of the box's longest axis at the end farther
// from the grip. A flick turns the head farther than the box's centre moves, so
// the head gets a sweep of its own.
Box HeadBox(const Box& box, Vec3 grip, float length = .12f);

struct StrikeSettings {
    float minSpeed = 1.2f;     // m/s at the contact point: slower is a touch, not a strike
    float heavySpeed = 7.0f;   // m/s: from here, Prey's charged hit (PreyVR-36: strikes landed at 1.5-13 m/s)
    float fullSpeed = 10.0f;   // m/s: the charged hit's whole charge bonus
    float minScale = .5f;      // a normal hit's damage scale at minSpeed (1 at heavySpeed)
    float lockSeconds = .15f;  // a struck target is struck again once the wrench has been off it this long
};
// How hard a strike at `speed` is (user decision 3): Prey's normal hit, scaled
// minScale..1 from minSpeed to heavySpeed; from heavySpeed its charged hit,
// 1 + chargeBonus by fullSpeed (the weapon's own bonus, SwingNative.h ChargeBonus).
// No strike below minSpeed.
struct Tier { bool strike = false, heavy = false; float scale = 0; };
Tier Classify(float speed, float chargeBonus, const StrikeSettings& settings);

// What a contact did this frame, in the order the lane decides it.
enum class Decision : std::uint8_t { strike, locked, tooSlow, drained, blocked };
const char* DecisionName(Decision decision);

// The speed of the contact point into the surface: positive when approaching.
// `normal` points out of the struck object, as the engine reports it.
float Approach(Vec3 velocity, Vec3 normal);

// One hit per target per pass. A pass runs from a target's first frame of
// contact until the wrench has been off it for lockSeconds; `struck` says it hit.
class Passes {
public:
    static constexpr unsigned kCapacity = 16;
    struct Pass {
        const void* target = nullptr;
        std::uint64_t first = 0, last = 0;
        bool struck = false;
        std::uint8_t noted = 0;   // the decisions already logged in this pass, by bit
    };
    // The target's pass, touched now; `begun` when this frame starts it. Null when
    // all kCapacity passes are live.
    Pass* Touch(const void* target, std::uint64_t nowNs, float lockSeconds, bool& begun);
    // Ends the passes the wrench has been off for lockSeconds.
    void Expire(std::uint64_t nowNs, float lockSeconds);
    void Clear() { passes_ = {}; }
    unsigned Active() const;

private:
    std::array<Pass, kCapacity> passes_{};
};

} // namespace preyvr::swing
