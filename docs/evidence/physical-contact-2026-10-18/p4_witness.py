"""Phase 4: instruction bytes for the verifier's witnesses, and new contract rows."""
import sys
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\tools\re")
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\docs\evidence\physical-contact-2026-10-18")
from re_disasm import img, md, chunks, window
from preydll import fnv1a64

def ins_at(rva):
    for ins in md.disasm(img.at(rva, 16), rva):
        return ins

WIT = [0xb1f289, 0xb1f29d, 0xb1f3e6, 0xb1f3ed, 0xb1f3f7, 0xb1f403, 0xb1f416, 0xb1f425, 0xb1f42c, 0xb1f443, 0xb1f44f,
       0xb1f456, 0xb1f47f if False else 0xb1f479, 0xb1f4fa, 0xb1fc50, 0xb1fc5d, 0xb1fc85,
       0xb2c86d, 0xb2c8b8, 0xb2c8c4, 0xb2cb51, 0xb2cb58, 0xb2cb5d, 0xb2cb64, 0xb2cba9, 0xb2ce5c, 0xb2cf1b, 0xb2cf3c,
       0xb2b0a1, 0xb2b12a, 0xb2b131,
       0xb3c26d, 0xb3c276, 0xb38f45, 0xb39a8f, 0xb39a98]
for a in WIT:
    i = ins_at(a)
    print(f"{a:#x} {i.bytes.hex():24s} {i.mnemonic} {i.op_str}")

print()
relocs = img.relocations()
for name, rva in [("ArticulatedGetParams", 0xB2AA00), ("ArticulatedStepJoints", 0xB3C000),
                  ("ArticulatedStep", 0xB37960), ("ArticulatedStepBack", 0xB398A0)]:
    cs = chunks(rva)
    lo, hi = min(b for b, e in cs), max(e for b, e in cs)
    total = sum(e - b for b, e in cs)
    contiguous = total == hi - lo and lo == rva
    rel = [p for p in relocs if rva <= p < hi]
    print(f"{name} {rva:#x} span {hi - lo} chunks-total {total} contiguous {contiguous} relocs {len(rel)} "
          f"hash {fnv1a64(img.at(rva, hi - rva)):#018x}")

print()
print(window(0xba2a71, 0x60, 0x10))
