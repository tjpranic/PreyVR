"""Phase 5: rows and witnesses for the player's ground collider (pe_status_living)."""
import struct, sys
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\tools\re")
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\docs\evidence\physical-contact-2026-10-18")
from re_disasm import img, md, chunks
from preydll import fnv1a64


def ins_at(rva):
    for ins in md.disasm(img.at(rva, 16), rva):
        return ins


for a in [0xb475e5, 0xb477a3, 0xb477b8, 0xb477c8, 0xb477ed, 0xb477fc, 0xb478e2, 0xb4799c, 0xb479a3, 0xb479a7,
          0xb44edf, 0xb45227]:
    i = ins_at(a)
    print(f"{a:#x} {i.bytes.hex():24s} {i.mnemonic} {i.op_str}")
relocs = img.relocations()
for name, rva in [("LivingGetStatus", 0xB475D0), ("StaticGetStatus", 0xB83040), ("LivingAction", 0xB44E60)]:
    cs = chunks(rva)
    lo, hi = min(b for b, e in cs), max(e for b, e in cs)
    total = sum(e - b for b, e in cs)
    rel = [p for p in relocs if rva <= p < hi]
    print(f"{name} {rva:#x} span {hi - lo} total {total} contiguous {total == hi - lo and lo == rva} relocs {len(rel)} "
          f"hash {fnv1a64(img.at(rva, hi - rva)):#018x}")
for slot in (0x08, 0x30, 0x38):
    print(f"living +{slot:#x} -> {struct.unpack('<Q', img.at(0x1D81580 + slot, 8))[0] - img.base:#x}")
print(f"static +0x30 -> {struct.unpack('<Q', img.at(0x1D82188 + 0x30, 8))[0] - img.base:#x}")
print(f"old 0x1D842C0 +0x30 -> {struct.unpack('<Q', img.at(0x1D842C0 + 0x30, 8))[0] - img.base:#x}")
