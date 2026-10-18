import re
from re_disasm import img, function_of, md
text = img.section(".text"); tb = img.at(text.rva, text.raw_size)
hits = {}
for disp, name in ((b"\xf0\x18\x00\x00", "friction"), (b"\xf0\x20\x00\x00", "dynfriction")):
    for m in re.finditer(re.escape(disp), tb):
        rva = text.rva + m.start()
        # try instruction starts up to 8 bytes back
        for back in range(3, 9):
            start = rva - back
            ins = next(md.disasm(img.at(start, 15), start), None)
            if ins and ins.address + ins.size >= rva + 4 and "0x" + ("18f0" if name=="friction" else "20f0") in ins.op_str and "*4" in ins.op_str:
                f = function_of(start)
                hits.setdefault(name, []).append((start, f[0] if f else None, f"{ins.mnemonic} {ins.op_str}"))
                break
for name, lst in hits.items():
    print(name, len(lst))
    for s, f, t in lst[:40]:
        print(f"  {s:#x} in {f:#x}: {t}" if f else f"  {s:#x}: {t}")
