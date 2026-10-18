import sys
from re_disasm import chunks, dis_function
for name, rva in (("collide_0xba1d80", 0xBA1D80), ("boxlist_0xba8560", 0xBA8560), ("boxlist_inner_0xba6830", 0xBA6830), ("pwi_0xbaa8b0", 0xBAA8B0)):
    cs = chunks(rva)
    open(f"p0/{name}.asm.txt", "w").write(dis_function(rva))
    print(name, [(hex(b), e - b) for b, e in cs])
