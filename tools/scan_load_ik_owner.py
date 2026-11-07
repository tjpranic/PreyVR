"""Scan PreyVR run logs for the IK owner going stale after a level load.

docs/LOAD-IK-OWNER-SCOPE-2026-11-07.md. After a load the player's arms are a new
character; the IK hook (AnimIkTakeover.cpp) passes every character but the one it
cached straight through until the weapon equip count changes. A healthy IK
re-identifies its rig after every snap turn (the head reference changes, Bind
rebinds), so snaps after a load with no `rig_identified` mean the owner is stale.

For each real load (loading_screen ended with uploads>0) prints: the weapon state
before it (last weapon_draw finish = drawn, stow = holstered), seconds to the first
rig_identified, seconds to the first weapon draw, how many snap turns went by
without a rebind, and a verdict.

    py -3 -I tools/scan_load_ik_owner.py "%LOCALAPPDATA%\\PreyVR\\runs" [run-name-substring]
"""
import datetime
import os
import re
import sys

LINE = re.compile(r"^(\d{4}-\d\d-\d\dT[\d:.]+)Z (\S+) (.*)$")


def stamp(text):
    return datetime.datetime.fromisoformat(text)


def scan(path):
    events = []
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = LINE.match(line)
            if not m:
                continue
            t, tag, rest = stamp(m.group(1)), m.group(2), m.group(3)
            if tag == "preyvr_xr_session" and rest.startswith("loading_screen ended"):
                up = re.search(r"uploads=(\d+)", rest)
                events.append((t, "load", int(up.group(1)) if up else 0))
            elif tag == "preyvr_anim_ik" and "detail=rig_identified" in rest:
                events.append((t, "rebind", None))
            elif tag == "preyvr_anim_ik" and "detail=owner_character_changed" in rest:
                events.append((t, "character", None))   # PreyVR-138 on: the fix rebinding
            elif tag == "preyvr_head_tracking" and "detail=snap_turn" in rest:
                events.append((t, "snap", None))
            elif tag == "preyvr_weapon_draw" and rest.startswith(("finish ", "skip ")):
                events.append((t, "draw", None))
            elif tag == "preyvr_weapon_draw" and rest.startswith("stow "):
                events.append((t, "stow", None))
    return events


def report(run, events):
    weapon = "unknown"   # weapon_draw lines exist from PreyVR-126 (2026-10-31)
    rows = []
    open_until = None    # a load whose rebind is still to come: later loads join its episode
    for i, (t, kind, value) in enumerate(events):
        if kind == "draw":
            weapon = "drawn"
        elif kind == "stow":
            weapon = "holstered"
        elif kind == "load" and value > 0:
            if open_until is not None and t < open_until:
                continue
            later = events[i + 1:]
            rebind = next((e for e in later if e[1] == "rebind"), None)
            draw = next((e for e in later if e[1] == "draw"), None)
            nxt = next((e for e in later if e[1] == "load" and e[2] > 0), None)
            changed = next((e for e in later if e[1] == "character" and (nxt is None or e[0] < nxt[0])), None)
            until = rebind[0] if rebind else (later[-1][0] if later else t)
            open_until = until
            snaps = sum(1 for e in later if e[1] == "snap" and e[0] < until)
            to_rebind = (rebind[0] - t).total_seconds() if rebind else None
            to_draw = (draw[0] - t).total_seconds() if draw else None
            # The rebind is the draw's when it lands within 0.2 s after one. With a weapon
            # drawn, Prey re-equips it as the load ends, so that draw is not the player's.
            by_draw = rebind is not None and draw is not None and 0 <= (rebind[0] - draw[0]).total_seconds() < 0.2
            if rebind is None:
                verdict = "never_rebound"
            elif by_draw and weapon != "drawn" and to_draw > 1.0:
                verdict = "STALE_until_draw"
            elif not by_draw and snaps > 0:
                verdict = "STALE_then_rebound_without_draw"
            else:
                verdict = "ok"
            to_changed = (changed[0] - t).total_seconds() if changed else None
            rows.append((run, t.strftime("%H:%M:%S"), weapon, to_rebind, to_draw, snaps, verdict, to_changed))
    return rows


def main():
    root = sys.argv[1]
    only = sys.argv[2] if len(sys.argv) > 2 else ""
    fmt = lambda s: "-" if s is None else f"{s:.1f}s"
    for run in sorted(os.listdir(root)):
        path = os.path.join(root, run, "PreyVR.log")
        if only not in run or not os.path.isfile(path):
            continue
        for r in report(run, scan(path)):
            print(f"{r[0]} load_end={r[1]} weapon={r[2]} rebind={fmt(r[3])} draw={fmt(r[4])} "
                  f"character_changed={fmt(r[7])} snaps_without_rebind={r[5]} {r[6]}")


if __name__ == "__main__":
    main()
