#pragma once

#include <cstdint>

// Turning a thumbstick into the analog axis events Prey's input layer already
// understands.
//
// **This is the value layer only.** It decides *what* to post -- key ids, axis
// values, device and state -- and knows nothing about memory layout. The 0x38
// byte `SInputEvent`, its `keyName` at `+0x10`, `keyId` at `+0x18` and `value` at
// `+0x20`, and the `PostInputEvent` vtable slot all live on the DLL side, where
// they can be gated behind the landmark check (R-080, R-089). Keeping the
// arithmetic here is what makes it testable without the game.
//
// R-070 established that Prey did not replace CryEngine's input layer, so a
// synthesised event reaches every consumer a real stick does. **That cuts both
// ways:** the playbook's shot-redirection warning applies to input too, so the
// caller must scope its post to the player rather than broadcasting.
namespace preyvr::locomotion {

// R-089. `xi_` names are the gamepad axes; device 3 is the gamepad, and state 8
// is the analog "changed" state the stick handlers respond to.
inline constexpr int kKeyThumbLX = 0x210;
inline constexpr int kKeyThumbLY = 0x211;
// Right stick X: the axis Prey already turns the character with, so posting it
// moves mesh, capsule, aim origin and movement direction together. Inferred
// from the PDB enum block whose neighbours are confirmed live; see InputEvent.h.
inline constexpr int kKeyThumbRX = 0x216;
inline constexpr int kDeviceGamepad = 3;
inline constexpr int kStateChanged = 8;

struct AxisEvent {
    int keyId = 0;
    const char* keyName = nullptr;
    float value = 0.0f;
    int deviceType = kDeviceGamepad;
    int state = kStateChanged;
};

struct StickPolicy {
    // **Radial**, not per-axis. A per-axis deadzone leaves the diagonals live
    // while the cardinals are dead, and a per-axis clamp makes a diagonal
    // sqrt(2) times faster than straight ahead -- which reads as the character
    // sprinting only when moving cornerwise.
    float deadzone = 0.15f;
    // Below this, a change is stick noise rather than intent. Suppressing it
    // keeps a queue that holds one command at a time from being flooded.
    float changeEpsilon = 0.002f;
};

// Converts a stick reading into the axis events that should be posted for it.
//
// Stateful on purpose: it emits only what changed. The state is what makes the
// **release** case correct -- see `Update`.
class StickAxis {
public:
    StickAxis() = default;
    explicit StickAxis(StickPolicy policy) : policy_(policy) {}

    // Writes up to two events into `out` and returns how many.
    //
    // **Returning to centre always emits a zero.** The obvious implementation
    // -- emit only while the stick is outside the deadzone -- walks the player
    // forever the moment they let go, because the last thing the engine was
    // told was "0.8 forward" and nothing ever contradicts it. The deadzone is
    // applied to produce the *value*, and the emit decision is made on the
    // value, so entering the deadzone produces a zero and sends it.
    //
    // Refuses a non-finite reading without touching its state, so a runtime
    // glitch cannot latch a NaN into the engine's stored axis.
    unsigned int Update(float x, float y, AxisEvent out[2]);

    // Forgets what was last sent, so the next Update re-emits. For a session
    // boundary, where the engine's stored axis is no longer ours to reason about.
    void Reset();

    // Changing shaping must preserve the values the engine still holds.
    void SetPolicy(StickPolicy policy) { policy_ = policy; }

    float LastSentX() const { return lastX_; }
    float LastSentY() const { return lastY_; }

private:
    StickPolicy policy_{};
    float lastX_ = 0.0f;
    float lastY_ = 0.0f;
    bool primed_ = false;
};

// The deadzone and clamp, exposed because it is the part worth checking against
// a commanded stick. Writes the shaped reading to `outX`/`outY`; false when the
// input is not usable, in which case the outputs are untouched.
bool ShapeStick(float x, float y, float deadzone, float* outX, float* outY);

// The value the engine stores for an axis posted in thousandths -- the same
// conversion the post makes, so a read-back compares exactly.
inline float HeldAxisValue(int milli) { return static_cast<float>(milli) / 1000.0f; }

// **What Prey's re-read of a stick axis should find** (docs/MOVE-STOP-2026-10-24.md).
// On an action-map or filter event Prey re-reads the move and yaw axes from the
// physical pad's symbols and stores them, and the lane's posts never set those
// symbols: the re-read found 0 and stopped the player. It finds instead what a
// real pad's symbol would hold: the pad's own value when the pad is pushed or the
// lane holds nothing, 0 when the action is filtered (as the engine's read
// returns), otherwise the value the lane delivered.
inline float RereadValue(float native, int heldMilli, bool filtered)
{
    if (native != 0.0f || heldMilli == 0 || filtered) { return native; }
    return HeldAxisValue(heldMilli);
}

// **A short gap in the controller samples keeps the last stick.** A refused or
// stale tracking read (or an untracked head, for head-relative movement) used to
// post a zero for a frame -- a one-frame stop -- and the value again the next.
// Within `limit` of the last good sample the lane posts nothing, so the engine
// keeps what it holds; past it, the stick is released as before.
class SampleHold {
public:
    void Sampled(std::uint64_t now) { last_ = now; }
    bool Bridges(std::uint64_t now, std::uint64_t limit) const
    {
        return last_ != 0 && limit != 0 && now >= last_ && now - last_ <= limit;
    }
    void Reset() { last_ = 0; }

private:
    std::uint64_t last_ = 0;
};

} // namespace preyvr::locomotion
