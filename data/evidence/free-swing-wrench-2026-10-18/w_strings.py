import sys, re
sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\tools\re")
from preydll import Image
img = Image()
def to_rva(off):
    for s in img.sections:
        if s.raw_offset <= off < s.raw_offset + s.raw_size:
            return s.rva + off - s.raw_offset, s.name
    return None, None
pats = [p.encode() for p in sys.argv[1:]]
for m in re.finditer(rb"[\x20-\x7e]{4,120}\x00", img.data):
    t = m.group()[:-1]
    tl = t.lower()
    if any(p in tl for p in pats):
        rva, sec = to_rva(m.start())
        print(f"{rva:#x} {sec} {t.decode()}")
