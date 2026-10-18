"""Phase 5: the living entity's vtable, its GetStatus and the pe_status_living branch."""
import struct, sys
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\docs\evidence\physical-contact-2026-10-18")
from re_disasm import img, function_of, chunks, dis_function
base = img.base
LIV = 0x1D81580
text = img.section(".text")
lo, hi = base + text.rva, base + text.rva + text.raw_size
for i in range(0, 0x48, 8):
    q = struct.unpack("<Q", img.at(LIV + i, 8))[0]
    f = q - base
    size = sum(e - b for b, e in chunks(f)) if lo <= q < hi and function_of(f) else 0
    print(f"slot {i:#05x} -> {f:#x} size {size}")
status = struct.unpack("<Q", img.at(LIV + 0x30, 8))[0] - base
open(sys.argv[1], "w").write(dis_function(status))
print("GetStatus", hex(status))
