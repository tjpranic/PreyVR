import sys, re
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\docs\evidence\physical-contact-2026-10-18")
from re_disasm import img, md, chunks
seen = []
for b, e in chunks(0x13BF730):
    for ins in md.disasm(img.at(b, e - b), b):
        m = re.search(r"\[(r\w+) \+ 0x([0-9a-f]+)\]", ins.op_str)
        if m and m.group(1) not in ("rsp", "rbp", "rip"):
            seen.append(f"{ins.address:#x} {ins.mnemonic} {ins.op_str}")
        if ins.mnemonic == "call":
            seen.append(f"{ins.address:#x} CALL {ins.op_str}")
print("\n".join(seen))
