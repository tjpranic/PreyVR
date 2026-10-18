"""Phase 4: the articulated vtable against the rigid one; sizes of overridden slots."""
import struct, sys
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\docs\evidence\physical-contact-2026-10-18")
from re_disasm import img, function_of, chunks
base = img.base
ART, RIG = 0x1D803E8, 0x1D83870
text = img.section(".text")
lo, hi = base + text.rva, base + text.rva + text.raw_size


def slots(vt, n=120):
    out = []
    for i in range(n):
        q = struct.unpack("<Q", img.at(vt + i * 8, 8))[0]
        if not (lo <= q < hi):
            break
        out.append(q - base)
    return out


a, r = slots(ART), slots(RIG)
print("articulated slots", len(a), "rigid slots", len(r))
for i, f in enumerate(a):
    total = sum(e - b for b, e in chunks(f)) if function_of(f) else 0
    same = i < len(r) and r[i] == f
    print(f"slot {i*8:#05x} {f:#x} size {total:6d} {'(rigid)' if same else ''}")
