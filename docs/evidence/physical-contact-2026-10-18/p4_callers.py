"""Phase 4: direct callers of the given functions inside the physics code."""
import sys
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\docs\evidence\physical-contact-2026-10-18")
from re_disasm import img, FUNCS, chunks, function_of, md
targets = set(int(a, 16) for a in sys.argv[1:])
hits = {t: [] for t in targets}
for b, e in FUNCS:
    if not (0xAF0000 <= b < 0xC80000):
        continue
    for ins in md.disasm(img.at(b, e - b), b):
        if ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
            t = int(ins.op_str, 16)
            if t in hits:
                hits[t].append((ins.address, chunks(b)[0][0]))
for t in sorted(hits):
    print(f"{t:#x}:", ", ".join(f"{a:#x} in {r:#x}" for a, r in hits[t]))
