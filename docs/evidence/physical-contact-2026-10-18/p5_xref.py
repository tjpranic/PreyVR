"""Phase 5: functions that cite a string (rip-relative lea), and vtables that hold those functions."""
import struct, sys
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\docs\evidence\physical-contact-2026-10-18")
from re_disasm import img, FUNCS, chunks, md
base = img.base
targets = [int(a, 16) for a in sys.argv[1:]]
roots = set()
for b, e in FUNCS:
    if not (0xAF0000 <= b < 0xC80000):
        continue
    for ins in md.disasm(img.at(b, e - b), b):
        if ins.mnemonic == "lea" and "rip" in ins.op_str:
            disp = int(ins.op_str.split("rip")[1].split("]")[0].replace(" ", "").replace("+", "") or "0", 16)
            t = ins.address + ins.size + disp
            if t in targets:
                r = chunks(b)[0][0]
                roots.add(r)
                print(f"{t:#x} cited at {ins.address:#x} in {r:#x}")
rdata = img.section(".rdata")
blob = img.at(rdata.rva, rdata.raw_size)
for r in sorted(roots):
    for off in range(0, len(blob) - 8, 8):
        if struct.unpack_from("<Q", blob, off)[0] == base + r:
            print(f"  {r:#x} at .rdata {rdata.rva + off:#x}")
