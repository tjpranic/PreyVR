import re, sys, collections
path = sys.argv[1]
lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
windows = []
cur = None
for line in lines:
    m = re.search(r"preyvr_weapon_sound window what=(\w+) class=(\S+) item=(\d+) n=(\d+)", line)
    if m:
        cur = {"what": m.group(1), "cls": m.group(2), "item": m.group(3), "n": int(m.group(4)), "clips": [], "exec": [], "res": []}
        windows.append(cur)
        continue
    if cur is None:
        continue
    if "preyvr_weapon_sound window end" in line:
        cur = None
        continue
    m = re.search(r"preyvr_weapon_sound clip tMs=(\d+) entity=(\S+) start=(\S+) stop=(\S+) fireAndForget=(\d) soundFlags=(\d+) joint=(\S+) blendMs=(-?\d+) durationMs=(-?\d+)", line)
    if m:
        cur["clips"].append(dict(t=int(m.group(1)), ent=m.group(2), start=m.group(3), stop=m.group(4), ff=m.group(5), flags=m.group(6), joint=m.group(7), blend=m.group(8), dur=m.group(9)))
        continue
    m = re.search(r"preyvr_weapon_sound resolve tMs=(\d+) name=(\S+) id=(\S+) found=(\d)", line)
    if m:
        cur["res"].append(dict(t=int(m.group(1)), name=m.group(2), id=m.group(3), found=m.group(4)))
        continue
    m = re.search(r"preyvr_weapon_sound execute tMs=(\d+) id=(\S+) entity=(\S+) proxy=(\d+)", line)
    if m:
        cur["exec"].append(dict(t=int(m.group(1)), id=m.group(2), ent=m.group(3), proxy=m.group(4)))

names = {}
for w in windows:
    for r in w["res"]:
        if r["found"] == "1":
            names[r["id"]] = r["name"]

groups = collections.defaultdict(list)
for w in windows:
    groups[(w["cls"], w["what"])].append(w)

for (cls, what), ws in sorted(groups.items()):
    print("\n== %s %s  windows=%d  lengths(clips)=%s" % (cls, what, len(ws), [len(w["clips"]) for w in ws]))
    # clips by start trigger
    agg = collections.defaultdict(list)
    for i, w in enumerate(ws):
        for c in w["clips"]:
            agg[(c["start"], c["ent"], c["stop"], c["ff"], c["flags"], c["joint"], c["blend"], c["dur"])].append((i, c["t"]))
    for key, hits in sorted(agg.items(), key=lambda kv: min(t for _, t in kv[1])):
        winset = sorted({i for i, _ in hits})
        print("  clip %-44s in %d/%d windows t=%s ent=%s stop=%s ff=%s flags=%s joint=%s blend=%s dur=%s" % (
            key[0], len(winset), len(ws), [t for _, t in hits][:8], key[1], key[2], key[3], key[4], key[5], key[6], key[7]))
    ex = collections.defaultdict(list)
    for i, w in enumerate(ws):
        for e in w["exec"]:
            ex[(e["id"], e["ent"], e["proxy"])].append((i, e["t"]))
    for key, hits in sorted(ex.items(), key=lambda kv: min(t for _, t in kv[1])):
        winset = sorted({i for i, _ in hits})
        print("  exec %-44s in %d/%d windows t=%s ent=%s proxy=%s" % (names.get(key[0], key[0]), len(winset), len(ws), [t for _, t in hits][:8], key[1], key[2]))
