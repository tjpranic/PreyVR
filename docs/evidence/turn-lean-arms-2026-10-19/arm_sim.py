"""How the drawn arm's elbow bend compares with the player's real one.

Mirrors src/common/ArmPose.cpp: the drawn shoulder is ShoulderTarget(eye) =
eye + (-9 fwd, +-18.5 out, -23 up) in the torso frame; the drawn arm has
fixed segments 28.5 / 27.5 cm (stretch to 1.10) and the wrist goal is the
player's real wrist, softly pulled in past 92% of the stretched reach
(handpose::SoftReach). The eye is the HMD centre eye, which swings about the
neck when the head pitches (the mod's own kNeckFromEye: 7.5 cm down, 8.05 cm
back).

The "true" player: shoulder where the model says when the head is level,
arm segments scaled to an arm length L (shoulder -> wrist, straight).
Frame: x forward, y out (to this arm's side), z up. Metres.
"""
import math

NECK_DOWN, NECK_BACK = 0.075, 0.0805
SH = (-0.09, 0.185, -0.23)          # shoulder from eye (fwd, out, up)
UPPER, FORE, STRETCH, KNEE = 0.285, 0.275, 1.10, 0.92


def eye_offset(pitch_deg):
    """Where the eye moves (fwd, up) when the head pitches down about the neck."""
    t = math.radians(pitch_deg)
    f0, u0 = NECK_BACK, NECK_DOWN   # eye relative to the neck pivot: forward, up
    f = f0 * math.cos(t) + u0 * math.sin(t)
    u = u0 * math.cos(t) - f0 * math.sin(t)
    return f - f0, u - u0


def soft_reach(d, limit, knee):
    """handpose::SoftReach on a distance: exact to knee*limit, then approaches limit."""
    k = knee * limit
    if d <= k:
        return d
    span = limit - k
    return k + span * (1.0 - math.exp(-(d - k) / span))


def elbow_angle(d, upper=UPPER, fore=FORE):
    """Interior elbow angle (deg; 180 = straight) for shoulder-wrist distance d."""
    reach = 0.995 * (upper + fore)
    s = max(1.0, d / reach)
    l1, l2 = upper * s, fore * s
    c = (l1 * l1 + l2 * l2 - d * d) / (2 * l1 * l2)
    return math.degrees(math.acos(max(-1.0, min(1.0, c))))


def run():
    # Wrist positions relative to the TRUE shoulder (fwd, out, up), as fractions
    # of the true arm length L where it matters.
    poses = {
        "hands at belly (relaxed hold)": lambda L: (0.25, -0.05, -0.30),
        "ready / weapon at chest":       lambda L: (0.38, -0.08, -0.12),
        "arm straight forward":          lambda L: (L, 0.0, 0.0),
        "arm straight, 30 deg down":     lambda L: (L * math.cos(math.radians(30)), 0.0, -L * math.sin(math.radians(30))),
    }
    print("Drawn vs real elbow angle (deg, 180 = straight). Drawn arm 28.5+27.5 cm.")
    print(f"{'pose':32s} {'L cm':>5s} {'pitch':>5s} {'real':>6s} {'drawn':>6s} {'d real':>7s} {'d drawn':>7s}")
    limit = 0.995 * (UPPER + FORE) * STRETCH
    for name, fn in poses.items():
        for L in (0.50, 0.56, 0.62):
            for pitch in (0, 30, 45, 60):
                w = fn(L)
                d_real = math.dist((0, 0, 0), w)
                # true arm segments proportional to L
                real = elbow_angle(min(d_real, 0.995 * L), UPPER * L / 0.56, FORE * L / 0.56)
                df, du = eye_offset(pitch)
                # drawn shoulder moves with the eye; wrist goal is the real wrist
                rel = (w[0] - df, w[1], w[2] - du)
                d_drawn = soft_reach(math.dist((0, 0, 0), rel), limit, KNEE)
                drawn = elbow_angle(d_drawn)
                print(f"{name:32s} {L*100:5.0f} {pitch:5d} {real:6.0f} {drawn:6.0f} {d_real*100:7.1f} {d_drawn*100:7.1f}")
        print()
    print("Eye (and so drawn-shoulder) shift when looking down, cm (fwd, up):")
    for pitch in (15, 30, 45, 60):
        df, du = eye_offset(pitch)
        print(f"  {pitch:2d} deg: {df*100:+.1f}, {du*100:+.1f}")


if __name__ == "__main__":
    run()
