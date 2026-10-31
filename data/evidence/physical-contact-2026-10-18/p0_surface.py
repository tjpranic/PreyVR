from re_disasm import img, function_of, dis
VT = 0x1D82E70
for slot in range(0, 0x320, 8):
    t = img.vtable_target(VT + slot)
    f = function_of(t)
    size = (f[1] - f[0]) if f else 0
    if f and size <= 140:
        code = dis(t, size)
        if "0x200" in code or "0x1ff" in code or "0x1fe" in code or "0x3ff" in code:
            print(f"=== slot {slot:#x} -> {t:#x} ({size} bytes)")
            print(code)
