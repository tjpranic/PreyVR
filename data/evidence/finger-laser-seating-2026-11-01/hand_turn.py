# Option C: turn the drawn hand onto the aim ray about the grip point, instead of
# turning the ray. Reuses the replica in the evidence script, which matched the log.
import math, sys
src = open(sys.argv[1], encoding='utf-8').read()
exec(src[:src.index("for side,off in")])

for side, off in (('R', (1.6, -3.5, 7.4)), ('L', (-1.6, -3.5, 7.4))):
    L, T, P, d = frame(side)
    rel = qnorm((0.365, 0, 0, 0.931))
    aimdir = rot(qconj(rel), (0, 0, -1))
    aimpos = rot(qconj(rel), -np.array(off))
    gp = L * .55 * d + P * .035
    pts, qs = fk(side, .25, 0)
    print(f"== {side}")
    for lean in (15, 25, 30, 35, 38.8, 40):
        hig = f2f((L, T, P), grip_frame(side, math.radians(lean)))
        tip = rot(hig, pts[-1] - gp) * 100
        k = rot(hig, pts[1] - gp) * 100
        chord = unit(tip - k)
        wrist = rot(hig, -gp) * 100
        print(f"  lean {lean:5.1f}: finger vs aim {ang(chord, aimdir):5.1f} deg | tip in grip {np.round(tip, 1)} | wrist in grip {np.round(wrist, 1)}")
    # The exact turn: the shortest rotation taking the finger onto the aim ray.
    base = f2f((L, T, P), grip_frame(side, math.radians(15)))
    tip0 = rot(base, pts[-1] - gp) * 100
    k0 = rot(base, pts[1] - gp) * 100
    c0 = unit(tip0 - k0)
    axis = np.cross(c0, aimdir)
    angle = math.atan2(np.linalg.norm(axis), np.dot(c0, aimdir))
    axis = axis / np.linalg.norm(axis)
    turn = np.array([*(axis * math.sin(angle / 2)), math.cos(angle / 2)])
    tip1 = rot(turn, tip0)
    k1 = rot(turn, k0)
    w0 = rot(base, -gp) * 100
    w1 = rot(turn, w0)
    palm0 = rot(base, P)
    print(f"  exact turn {math.degrees(angle):.1f} deg about {np.round(axis, 3)} (grip frame); palm normal moves {ang(palm0, rot(turn, palm0)):.1f} deg")
    print(f"    tip moves {np.linalg.norm(tip1 - tip0):.1f} cm to {np.round(tip1, 1)}; wrist moves {np.linalg.norm(w1 - w0):.1f} cm")
    d_along = np.dot(tip1 - aimpos, aimdir)
    d_off = np.linalg.norm((tip1 - aimpos) - aimdir * d_along)
    print(f"    tip vs today's aim origin: {d_along:+.1f} cm along the ray, {d_off:.1f} cm off it")
    print(f"    finger vs aim after turn: {ang(unit(tip1 - k1), aimdir):.2f} deg")
