"""Independent byte check of the living entity's teleport branch (agent's claim)."""
import struct
import sys

sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\tools\re")
from preydll import Image  # noqa: E402

img = Image()
checks = {
    # rva: (expected hex, meaning)
    0xB499D6: ("f6473c20", "test byte [rdi+3Ch],20h  (bRecalcBounds & 0x20 skips)"),
    0xB499F2: ("f30f108bcc010000", "movss xmm1,[rbx+1CCh]  (sizeCollider.z)"),
    0xB49A52: ("e8", "call 0xB49390 (release ground colliders)"),
    0xB49A57: ("838bc002000001", "or dword [rbx+2C0h],1  (bFlying)"),
    0xB49A5E: ("f683c002000008", "test byte [rbx+2C0h],8"),
    0xB49A6F: ("3d77770000", "cmp eax,7777h"),
    0xB49A76: ("4889b36c010000", "mov [rbx+16Ch],rsi  (vel.y,z = 0)"),
    0xB49A7D: ("89b368010000", "mov [rbx+168h],esi   (vel.x = 0)"),
    0xB49A83: ("89b3d0010000", "mov [rbx+1D0h],esi   (smoothing offset = 0)"),
}
ok = True
for rva, (hexs, what) in checks.items():
    want = bytes.fromhex(hexs)
    got = img.at(rva, len(want))
    match = got == want
    ok &= match
    print(f"{rva:#x} {'OK ' if match else 'BAD'} {got.hex()} {what}")
call = img.at(0xB49A52, 5)
print("call target", hex(0xB49A52 + 5 + struct.unpack('<i', call[1:])[0]))
print("constant 0x1D2C190 =", struct.unpack('<d', img.at(0x1D2C190, 8))[0])
# Living vtable SetParams slot (+0x20)
print("living vtable +0x20 ->", hex(img.vtable_target(0x1D81580 + 0x20)))
print("all OK" if ok else "MISMATCH")
