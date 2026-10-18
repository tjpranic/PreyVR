import sys
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\docs\evidence\physical-contact-2026-10-18")
from re_disasm import img, md, _note
def show(start, end):
    for ins in md.disasm(img.at(start, end - start), start):
        print(f"{ins.address:08x} {ins.bytes.hex():24s} {ins.mnemonic} {ins.op_str}{_note(ins)}")
for a in sys.argv[1:]:
    s, e = (int(x, 16) for x in a.split('-'))
    print(f"---- {s:#x}"); show(s, e)
