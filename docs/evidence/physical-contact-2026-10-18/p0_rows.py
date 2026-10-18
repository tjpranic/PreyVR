import sys
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\tools\re")
from preydll import fnv1a64
from re_disasm import img, chunks, md
relocs = img.relocations()
def extent(rva):
    cs = chunks(rva)
    lo = min(b for b, e in cs); hi = max(e for b, e in cs)
    total = sum(e - b for b, e in cs)
    return lo, hi, total, cs
def leaf(rva, limit=200):
    for ins in md.disasm(img.at(rva, limit), rva):
        if ins.mnemonic == "ret": return ins.address + ins.size - rva
rows = []
for name, rva in (("GetEntitiesInBox", 0xBA8560), ("EntitiesInBoxGrid", 0xBA6830), ("CollideEntityWithPrimitive", 0xBA1D80),
                  ("BoxGeometryFromPrimitive", 0xC5F080), ("RigidGetStatus", 0xBE2BB0), ("ArticulatedGetStatus", 0xB2C660),
                  ("ArticulatedAction", 0xB1CF30), ("RigidContactFriction", 0xBE5FD0)):
    lo, hi, total, cs = extent(rva)
    assert lo == rva, (name, hex(lo))
    contiguous = (hi - lo) == total
    size = hi - lo
    rel = [p for p in relocs if lo <= p < hi]
    rows.append((rva, size, fnv1a64(img.at(rva, size)), name))
    print(f"{name:28s} {rva:#x} size {size} chunks {len(cs)} contiguous {contiguous} relocs {len(rel)}")
for name, rva in (("GetSurfaceParameters", 0xBA9810), ("ArticulatedGetType", 0xB2D060)):
    size = leaf(rva)
    rel = [p for p in relocs if rva <= p < rva + size]
    rows.append((rva, size, fnv1a64(img.at(rva, size)), name))
    print(f"{name:28s} {rva:#x} size {size} leaf relocs {len(rel)} bytes {img.at(rva, size).hex()}")
for rva, size, h, name in rows:
    print(f"    {{{rva:#X},{size},{h:#018X}ull}}, // {name}".replace("0X", "0x", 1).replace("ull", "ull"))
