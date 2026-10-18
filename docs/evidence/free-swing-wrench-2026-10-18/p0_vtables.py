"""Find .rdata qwords pointing at given function RVAs (vtable slots), and dump chosen slots of those vtables."""
import sys, struct
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\tools\re")
from preydll import Image
img = Image(); base = img.base
rd = img.section('.rdata'); data = img.data
targets = {int(a, 16) + base: int(a, 16) for a in sys.argv[1].split(',')}
slots = [int(s, 16) for s in sys.argv[2].split(',')] if len(sys.argv) > 2 else []
for off in range(rd.raw_offset, rd.raw_offset + rd.raw_size - 8, 8):
    q = struct.unpack_from('<Q', data, off)[0]
    if q in targets:
        rva = rd.rva + off - rd.raw_offset
        print(f"{targets[q]:#x} at {rva:#x}")
        # walk back to the table start: previous qwords that are code pointers into .text
        text = img.section('.text')
        start = rva
        while True:
            p = struct.unpack('<Q', img.at(start - 8, 8))[0] - base
            if text.rva <= p < text.rva + text.virtual_size: start -= 8
            else: break
        print(f"   table starts {start:#x}; slot of target +{rva-start:#x}")
        for s in slots:
            p = struct.unpack('<Q', img.at(start + s, 8))[0] - base
            print(f"   +{s:#x} -> {p:#x}")
