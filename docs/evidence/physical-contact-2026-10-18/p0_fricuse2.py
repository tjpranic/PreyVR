from re_disasm import window
for r in (0xbe62bc, 0xb59b19):
    print("=====", hex(r)); print(window(r, before=0x70, after=0x40))
