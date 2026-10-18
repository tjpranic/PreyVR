"""Phase 5: physics-class vtables by GetType, with GetStatus/Action inside the physics module."""
import struct, sys
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\docs\evidence\physical-contact-2026-10-18")
from re_disasm import img, function_of, chunks
base = img.base
text = img.section(".text")
rdata = img.section(".rdata")
blob = img.at(rdata.rva, rdata.raw_size)
lo, hi = base + text.rva, base + text.rva + text.raw_size
PLO, PHI = 0xAF0000, 0xC80000


def size_of(f):
    return sum(e - b for b, e in chunks(f)) if function_of(f) else 0


for off in range(0, len(blob) - 0x48, 8):
    q = struct.unpack_from("<9Q", blob, off)
    if not all(lo <= v < hi for v in q):
        continue
    if off >= 8 and lo <= struct.unpack_from("<Q", blob, off - 8)[0] < hi:
        continue  # not the start of a run
    code = img.at(q[1] - base, 6)
    if not (code[0] == 0xB8 and code[5] == 0xC3):
        continue
    n = struct.unpack_from("<I", code, 1)[0]
    if not 1 <= n <= 10:
        continue
    status, action = q[6] - base, q[7] - base
    if PLO <= status < PHI and PLO <= action < PHI:
        print(f"vtable {rdata.rva + off:#x} GetType {n} GetStatus {status:#x} ({size_of(status)}) Action {action:#x} ({size_of(action)})")
