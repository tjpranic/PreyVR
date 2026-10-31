from re_disasm import img, function_of, dis
for name, rva in (("slot 0x1d0", 0xBA1D80), ("slot 0x110", 0xBA8560)):
    f = function_of(rva)
    print(f"=== {name} {rva:#x} chunk {f[0]:#x}-{f[1]:#x} size {f[1]-f[0]}")
    print("\n".join(dis(rva, min(f[1]-rva, 0x1c0)).splitlines()[:70]))
