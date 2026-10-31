#pragma once
#include "base/VrMath.h"

namespace preyvr::twohand {
// The support a weapon's native offhand gives it (ClassifySocket).
//
// **A handgun's support hand braces; it does not steer.** The native offhand of
// the pistol and of the Disruptor cups the grip 89 mm from the trigger wrist
// (-44,73,-24 mm in the barrel frame, both, measured 2026-10-28). Steering from
// a line that short, and across the barrel rather than along it, would turn a
// 1 cm wobble between the hands into 6-7 degrees of aim (1.5 on the GLOO's
// 0.39 m) and could barely pitch the weapon at all. Held, a handgun keeps the
// trigger hand's aim and the support hand is drawn in the native cup; the
// steadying is the player's own, one controller braced on the other.
enum class Grip : std::uint8_t { foregrip, handgun };
// A handgun's native cup lies ahead of the trigger wrist and within this of it.
inline constexpr float kHandgunReach = .15f;
// A squeeze this near the cup grabs it: two controllers cannot both be where
// the drawn hands are, so the grab reaches past the cup.
inline constexpr float kHandgunGrab = .15f;
// Hands this far apart let go of a handgun, so the drawn hand does not stay on
// the gun after the real one left.
inline constexpr float kHandgunBreak = .30f;
// **Auto-snap** (Input::autoSnap): one more way to attach, beside the squeeze.
// The hand, its grip at rest, attaches once it has stayed within Input::snapAttach
// of the grip for kAutoSnapDwell, so a hand passing through does not. With no grip
// to let go of, a hold made this way ends when the hand is Input::snapBreak from
// where it attached, in the barrel's frame, on any weapon: 25 cm is about 30
// degrees of steering on a foregrip 45 cm out. The break stays kAutoSnapMargin
// beyond the attach distance, or a hand would let go as it attached. A squeeze
// during it makes it an ordinary hold, and Foregrip Input decides as ever. After
// any release the hand must leave the attach distance before it snaps again.
inline constexpr float kAutoSnapDwell = .1f;
inline constexpr float kAutoSnapMargin = .05f;
// Engine axes, metres. Endpoints are relative to the primary wrist in the
// authored barrel frame. The animation observer supplies these, not XR poses.
struct Region { Vec3 start{}, end{}; float radius = .10f; Grip grip = Grip::foregrip; };
struct Input {
    Pose aim{}, support{};
    Vec3 primary{}, visualPrimaryOffset{};
    Region region{};
    float squeeze = 0, dt = 0;
    // `reference` changes with a recenter, which ends a hold. A snap turn turns
    // the hands and the weapon together and keeps it (TwoHandHold.cpp).
    std::uint64_t owner = 0, reference = 0, epoch = 0;
    bool usable = false;
    bool toggleGrip = false;
    bool autoSnap = false;
    float snapAttach = .10f, snapBreak = .25f;
};
struct Output {
    Quaternion orientation{}, correction{}, supportOrientation{};
    Vec3 socket{};
    float blend = 0;
    bool held = false;
    bool snapped = false;   // held by auto-snap, not by a squeeze
};
class Solver {
public:
    Output Update(const Input& input);
private:
    std::uint64_t owner_ = 0, reference_ = 0, epoch_ = 0;
    bool held_ = false, armed_ = false;
    bool toggleGrip_ = false;
    bool autoSnap_ = false;
    bool byProximity_ = false;   // this hold was made by auto-snap
    bool waitForExit_ = false;   // released: no snap until the hand has left the range
    bool handgun_ = false;
    float dwell_ = 0;
    float blend_ = 0;
    Vec3 socket_{};
    Quaternion swing_{}, supportInAim_{};
};
// The support socket of one weapon, from the native offhand. The foregrip is a
// rigid part of the model but the native left palm is animated: firing, cooling
// and reload animations take it off the handle (Q-Beam, measured 2026-10-07:
// 375,616,198 mm at rest; 580,763,168 / 129,473,96 / 280,647,433 mm a few
// seconds into a sustained beam). Read raw, the region wandered with the
// animation and, the first frame it left the acceptance bounds, was withdrawn,
// which released a held foregrip mid-fight. The socket is taken once the palm
// has rested within kSettleMetres for kSettleSamples consecutive in-bounds
// samples, and kept for that weapon: an animated or out-of-bounds palm neither
// moves nor withdraws it. A new owner (re-equip) starts over.
class RegionLatch {
public:
    static constexpr int kSettleSamples = 20;
    static constexpr float kSettleMetres = .01f;
    // `candidateValid`: the palm was read and lies inside the acceptance bounds.
    // Returns true with the weapon's socket once one is latched for `owner`.
    bool Update(std::uint64_t owner, bool candidateValid, Vec3 candidate, Vec3& socket);
    bool Latched() const { return latched_; }
    Vec3 Socket() const { return socket_; }
private:
    std::uint64_t owner_ = 0;
    bool latched_ = false;
    int steady_ = 0;
    Vec3 anchor_{}, socket_{};
};
// **Long weapons are held further back in VR.** A flat-screen viewmodel is
// posed for a camera, not for an arm: the Q-Beam's native foregrip is 0.58 m
// ahead of the trigger wrist, 0.36 m beside it and 0.23 m below (2026-10-07),
// 0.73-0.92 m from a player's left shoulder when the right hand is anywhere
// natural -- beyond any arm (~0.60 m to the palm). VR games put a real-size
// foregrip within reach and snap the support hand onto it from a grab radius;
// here the weapon (with the drawn trigger hand on its own grip) moves back along
// the barrel by however much its foregrip lies beyond `comfortForward`, at most
// `maxHoldBack`, and the grab region reaches `supportBack` behind the foregrip.
// A weapon whose foregrip is within reach (GLOO) is not touched. Metres.
struct LongReach {
    float comfortForward = .45f;
    float maxHoldBack = .20f;
    float supportBack = .10f;
};
// Two hands on a held-back weapon turn the drawn torso this far, so the drawn
// support arm reaches the foregrip.
inline constexpr float kLongTwistDegrees = 30.f;
// `socket` is the foregrip in the barrel frame from the trigger wrist (+Y ahead).
float HoldBack(Vec3 socket, const LongReach& reach);
// The grab region along the barrel: from `back` behind the foregrip to 4 cm ahead.
// A handgun's: its cup, grabbed from kHandgunGrab around it.
Region SupportRegion(Vec3 socket, float holdBack, const LongReach& reach, Grip grip = Grip::foregrip);
// The support a settled native offhand gives the weapon, `socket` in the barrel
// frame from the trigger wrist: a foregrip ahead of the grip near the barrel
// (wide, ik.widesupport: the Q-Beam's handle on the side of a 1.25 m weapon), or
// a handgun's cup beside the grip. False for a hand off the weapon.
bool ClassifySocket(Vec3 socket, bool wide, Grip& grip);
// Also useful for diagnostics/authoring. Refuses malformed regions and poses.
bool ClosestGrip(const Input& input, Vec3& localPoint);
} // namespace preyvr::twohand
