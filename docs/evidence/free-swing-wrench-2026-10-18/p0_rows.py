import sys
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\tools\re")
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\docs\evidence\physical-contact-2026-10-18")
from preydll import fnv1a64
from re_disasm import img, chunks, md
relocs = img.relocations()
def extent(rva):
    cs = chunks(rva)
    lo = min(b for b, e in cs); hi = max(e for b, e in cs)
    return lo, hi, sum(e - b for b, e in cs), cs
def leaf(rva, limit=200):
    for ins in md.disasm(img.at(rva, limit), rva):
        if ins.mnemonic in ("ret", "jmp"): return ins.address + ins.size - rva
rows = []
for name, rva in (("CollisionEventBuilder", 0x16AEF20), ("AddDeferredEvent", 0xB9F370), ("SendSignalPackage", 0x1608860),
                  ("StaminaAvailable", 0x12618E0), ("StaminaCharge", 0x1260D10), ("GetSurfaceType", 0x24E270)):
    lo, hi, total, cs = extent(rva)
    assert lo == rva, (name, hex(lo))
    size = hi - lo
    rel = [p for p in relocs if lo <= p < hi]
    rows.append((rva, size, fnv1a64(img.at(rva, size)), name))
    print(f"{name:24s} {rva:#x} size {size} chunks {len(cs)} contiguous {(hi-lo)==total} relocs {len(rel)} {[hex(c[0]) for c in cs]}")
for name, rva in (("MaterialManagerGetter", 0x20A540), ("MatManSurfaceTypes", 0x8332A0), ("MatManGetSurfaceType", 0x248610),
                  ("SurfaceTypeName", 0x1170A30), ("SurfaceTypeBreakability", 0x24DD40), ("SurfaceTypeFlags", 0x24DFE0),
                  ("PlayerStaminaAccessor", 0x1587C20)):
    size = leaf(rva)
    rel = [p for p in relocs if rva <= p < rva + size]
    print(f"{name:24s} {rva:#x} leaf {size} relocs {len(rel)} bytes {img.at(rva, size).hex()}")
for rva, size, h, name in rows:
    print(f"    {{{rva:#X},{size},{h:#018X}ull}}, // {name}".replace("0X", "0x", 1))
