"""Phase 5 fix: every store to +0x2C8 (the living ground collider) and reads of the flags
+0x2C0 near it, in the physics module; the living vtable slots that reach them."""
import re, struct, sys
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\docs\evidence\physical-contact-2026-10-18")
from re_disasm import img, FUNCS, chunks, md, window
base = img.base
LIV = 0x1D81580
slots = {}
for i in range(0, 0x2A0, 8):
    q = struct.unpack("<Q", img.at(LIV + i, 8))[0] - base
    slots.setdefault(q, []).append(i)
store = re.compile(r"^(qword|dword) ptr \[(r\w+) \+ 0x2c8\], ")
for b, e in FUNCS:
    if not (0xB40000 <= b < 0xB60000):
        continue
    root = chunks(b)[0][0]
    for ins in md.disasm(img.at(b, e - b), b):
        if ins.mnemonic in ("mov", "and", "xchg", "cmpxchg") and store.match(ins.op_str):
            print(f"{ins.address:#x} in {root:#x} slots {[hex(s) for s in slots.get(root, [])]}: {ins.mnemonic} {ins.op_str}")
for a in [int(x, 16) for x in sys.argv[1:]]:
    print(window(a, 0x90, 0x30))
