from re_disasm import img, chunks, dis_function
V = {"rigid": 0x1D83870, "articulated": 0x1D803E8, "articulated_alt": 0x1CBB160, "living": 0x1D842C0, "static": 0x1D82188}
for name, vt in V.items():
    row = []
    for slot in range(0, 0x60, 8):
        row.append(f"{slot:#x}:{img.vtable_target(vt + slot):#x}")
    print(name, " ".join(row))
for name, rva in (("rigid_getstatus", img.vtable_target(0x1D83870 + 0x30)), ("artic_getstatus", img.vtable_target(0x1D803E8 + 0x30)),
                  ("artic_action", img.vtable_target(0x1D803E8 + 0x38))):
    cs = chunks(rva)
    open(f"p0/{name}_{rva:#x}.asm.txt", "w").write(dis_function(rva))
    print(name, hex(rva), [(hex(b), e - b) for b, e in cs])
