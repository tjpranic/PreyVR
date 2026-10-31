import struct, re
from re_disasm import img, function_of, chunks, dis_function, dis
base = img.base
text = img.section(".text")
# 1. Every vtable-like run in .rdata whose slot +8 is "mov eax, N; ret" with N in 1..10 (IPhysicalEntity::GetType).
rdata = img.section(".rdata")
blob = img.at(rdata.rva, rdata.raw_size)
lo, hi = base + text.rva, base + text.rva + text.raw_size
found = {}
for off in range(0, len(blob) - 0x40, 8):
    a, b = struct.unpack_from("<QQ", blob, off)
    if not (lo <= a < hi and lo <= b < hi):
        continue
    if off >= 8 and lo <= struct.unpack_from("<Q", blob, off - 8)[0] < hi:
        continue  # not the start of a run
    code = img.at(b - base, 6)
    if code[0] == 0xB8 and code[5] == 0xC3:
        n = struct.unpack_from("<I", code, 1)[0]
        if 1 <= n <= 10:
            found.setdefault(n, []).append(rdata.rva + off)
for n in sorted(found):
    print("GetType", n, [hex(v) for v in found[n][:12]], "count", len(found[n]))
