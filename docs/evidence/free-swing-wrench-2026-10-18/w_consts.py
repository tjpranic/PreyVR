import sys, struct
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\docs\evidence\physical-contact-2026-10-18")
from re_disasm import img, dis, window, chunks, function_of
for a in (0x1C7EB28,0x1C7F7A8,0x1C8B724,0x1C7EB24,0x1C80790,0x1C7EB70,0x1C7F7B4,0x1C7F7C0,0x1C86464,0x1C7F77C,0x1C7F7A4,0x1C7EB20,0x1C7F7A0,0x1C7EB60):
    print(hex(a), struct.unpack('<f', img.at(a,4))[0], hex(struct.unpack('<I', img.at(a,4))[0]))
print("--- WeaponHit call to component OnHit")
print(dis(0x16B2BC0, 0x16B2C26-0x16B2BC0))
print("--- OnHit: the event build and dispatch call")
print(window(0x13BFE4A, 0x60, 0x10))
print("--- getters")
for f in (0x157C990, 0x1587C20, 0x157C9C0):
    fn = function_of(f); print(hex(f), fn and hex(fn[1]-fn[0]))
    print(dis(f, min(0x40, fn[1]-f) if fn else 0x30))
