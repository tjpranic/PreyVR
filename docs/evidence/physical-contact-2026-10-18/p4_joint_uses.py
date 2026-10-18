"""Phase 4: where the articulated code reads/writes joint fields.

joint (stride 0x330, array at entity +0x460): v +0x88, w +0x94, body.pos +0x1B0,
body.P +0x1CC, body.L +0x1D8, body.w +0x1E4, body.v +0x1F0, Pext +0x2B8, Lext +0x2C4.
Lists, per function reachable from the articulated vtable (direct calls inside
the articulated code range), each instruction touching those displacements.
"""
import re, struct, sys
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\docs\evidence\physical-contact-2026-10-18")
from re_disasm import img, function_of, chunks, md
base = img.base
ART = 0x1D803E8
LO, HI = 0xB10000, 0xB45000   # the articulated module's code
FIELDS = {0x88: "j.v", 0x8C: "j.v", 0x90: "j.v", 0x94: "j.w", 0x98: "j.w", 0x9C: "j.w",
          0x1E4: "b.w", 0x1E8: "b.w", 0x1EC: "b.w", 0x1F0: "b.v", 0x1F4: "b.v", 0x1F8: "b.v",
          0x1CC: "b.P", 0x1D8: "b.L", 0x2B8: "Pext", 0x2C4: "Lext", 0x1A0: "j.awake"}
want = set(int(a, 16) for a in sys.argv[1:]) if len(sys.argv) > 1 else None

slots = []
for i in range(83):
    q = struct.unpack("<Q", img.at(ART + i * 8, 8))[0] - base
    if LO <= q < HI:
        slots.append((i * 8, q))

seen, order = set(), []
def walk(f, depth):
    if not function_of(f):
        return
    root = chunks(f)[0][0]
    if root in seen or depth > 3:
        return
    seen.add(root)
    order.append(root)
    for b, e in chunks(root):
        for ins in md.disasm(img.at(b, e - b), b):
            if ins.mnemonic == "call" and ins.op_str.startswith("0x"):
                t = int(ins.op_str, 16)
                if LO <= t < HI and function_of(t):
                    walk(t, depth + 1)

for s, f in slots:
    walk(f, 0)
pat = re.compile(r"\[(r\w+) \+ (r\w+) \+ (0x[0-9a-f]+)\]")
for root in order:
    if want and root not in want:
        continue
    hits = []
    for b, e in chunks(root):
        for ins in md.disasm(img.at(b, e - b), b):
            m = pat.search(ins.op_str)
            if m and int(m.group(3), 16) in FIELDS:
                d = int(m.group(3), 16)
                store = ins.op_str.split(",")[0].find("[") >= 0
                hits.append(f"  {ins.address:08x} {'W' if store else 'r'} {FIELDS[d]:7s} {ins.mnemonic} {ins.op_str}")
    if hits:
        slot = [hex(s) for s, f in slots if f == root]
        size = sum(e - b for b, e in chunks(root))
        print(f"{root:#x} size {size} slot {slot}")
        kinds = {}
        for h in hits:
            k = h.split()[1] + " " + h.split()[2]
            kinds[k] = kinds.get(k, 0) + 1
        print("   ", dict(sorted(kinds.items())))
        if want:
            print("\n".join(hits))
