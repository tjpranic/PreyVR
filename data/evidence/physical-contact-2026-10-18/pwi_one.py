import struct, sys
from re_disasm import img, function_of, window
from pwi_sites import game
for r in game:
    f = function_of(r)
    if f and 0x1259000 <= f[0] <= 0x125f000:
        print("=== ref", hex(r), "function", hex(f[0]), hex(f[1]))
        print(window(r, before=0x1a0, after=0x110))
        break
