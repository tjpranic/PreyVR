import sys, re
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\docs\evidence\physical-contact-2026-10-18")
from re_disasm import img, FUNCS, md, root_of
# writes to +0x507 (weapon) or +0x3f (component) as byte stores
pat = re.compile(r"byte ptr \[(r\w+) \+ 0x(507|3f)\], ")
for b, e in FUNCS:
    if not (0x1390000 <= b < 0x16C0000): continue
    for ins in md.disasm(img.at(b, e - b), b):
        if ins.mnemonic == 'mov' and pat.search(ins.op_str):
            print(f"{ins.address:#x} in {root_of(b):#x}: {ins.mnemonic} {ins.op_str}")
