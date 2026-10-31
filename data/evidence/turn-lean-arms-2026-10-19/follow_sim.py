"""Does turning alone make room-scale move the capsule?

Mirrors armpose::TorsoYaw (35 deg deadzone, relax 0.6/s), roomscale::FollowPoint
(neck 7.5 cm down, 8.05 cm back of the eyes) and roomscale::Follower (start 3 cm,
stop 0.5 cm, glide 50 ms, cap 3 m/s). The player stands still; only the head
turns about the neck, so the real neck never moves. Every frame the follower
moves is a SetPos on the player's entity.
"""
import math

FPS = 90.0
DEAD, RELAX = math.radians(35), 0.6
NECK_BACK = 0.0805
START, STOP, GLIDE, CAP = 0.03, 0.005, 0.05, 3.0


def wrap(a):
    while a > math.pi:
        a -= 2 * math.pi
    while a < -math.pi:
        a += 2 * math.pi
    return a


def torso_update(torso, head, dt):
    diff = wrap(head - torso)
    if diff > DEAD:
        torso = head - DEAD
    elif diff < -DEAD:
        torso = head + DEAD
    diff = wrap(head - torso)
    return torso + diff * min(1.0, RELAX * dt)


def follow_point(head_yaw, torso_yaw):
    # Horizontal only. The eye sits NECK_BACK ahead of the neck along the head's
    # facing; FollowPoint = eye + R_head*n - R_torso*n = neck + NECK_BACK*facing(torso).
    return (NECK_BACK * math.sin(torso_yaw), NECK_BACK * math.cos(torso_yaw))  # relative to the fixed neck


def simulate(name, head_profile, seconds):
    dt = 1.0 / FPS
    torso = 0.0
    capsule = follow_point(0.0, 0.0)
    following = False
    moves, moved = 0, 0.0
    first = last = None
    for i in range(int(seconds * FPS)):
        t = i * dt
        head = head_profile(t)
        torso = torso_update(torso, head, dt)
        fp = follow_point(head, torso)
        off = (fp[0] - capsule[0], fp[1] - capsule[1])
        dist = math.hypot(*off)
        if not following and dist > START:
            following = True
        if following and dist <= STOP:
            following = False
        if following:
            step = min(dist, CAP * dt, dist * min(1.0, dt / GLIDE))
            capsule = (capsule[0] + off[0] / dist * step, capsule[1] + off[1] / dist * step)
            moves += 1
            moved += step
            first = t if first is None else first
            last = t
    span = f"{first:.2f}-{last:.2f} s" if first is not None else "-"
    print(f"{name:44s} SetPos frames {moves:4d}  moved {moved*100:5.1f} cm  during {span}")


def ramp(deg, over):
    return lambda t: math.radians(deg) * min(1.0, t / over)


def glance(deg, hold):
    return lambda t: math.radians(deg) * (1.0 if 0.2 <= t < 0.2 + hold else 0.0)


if __name__ == "__main__":
    print(f"Head turns about a fixed neck at {FPS:.0f} fps; real body never moves.")
    simulate("physical turn 90 deg over 0.5 s", ramp(90, 0.5), 8)
    simulate("physical turn 180 deg over 1 s", ramp(180, 1.0), 10)
    simulate("physical turn 45 deg over 0.4 s", ramp(45, 0.4), 8)
    simulate("glance 30 deg held 2 s, back", glance(30, 2.0), 8)
    simulate("glance 60 deg held 1 s, back", glance(60, 1.0), 8)
