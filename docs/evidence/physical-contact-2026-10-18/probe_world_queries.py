"""Static probe: which IPhysicalWorld slots does Prey's own code call with which name tags?

Reads the Steam PreyDll.dll on disk (hash-checked by preydll.Image). Finds each
name-tag string, every `lea r64,[rip+disp32]` that loads it, and the first
`call qword ptr [reg+disp]` after it: the slot that consumes the tag.
"""
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(sys.argv[1]) / "tools" / "re"))
from preydll import Image

img = Image()
text = img.section(".text")
tbytes = img.at(text.rva, text.raw_size)


def find_strings(pattern):
    out = []
    for section in img.sections:
        if section.name not in (".rdata", ".data"):
            continue
        blob = img.at(section.rva, section.raw_size)
        for m in re.finditer(pattern, blob):
            start = blob.rfind(b"\0", 0, m.start()) + 1
            end = blob.find(b"\0", m.end())
            out.append((section.rva + start, blob[start:end].decode("latin1")))
    return sorted(set(out))


# Every rip-relative lea in .text, indexed by target.
leas = defaultdict(list)
for i in range(len(tbytes) - 7):
    rex = tbytes[i]
    if rex in (0x48, 0x4C) and tbytes[i + 1] == 0x8D and (tbytes[i + 2] & 0xC7) == 0x05:
        disp = struct.unpack_from("<i", tbytes, i + 3)[0]
        rva = text.rva + i
        leas[rva + 7 + disp].append(rva)


def next_vcall(rva, window=96):
    """First `call [reg+disp8/disp32]` after rva, as (call rva, displacement)."""
    blob = img.at(rva, window)
    for j in range(len(blob) - 6):
        rex = 1 if blob[j] in (0x41,) else 0
        k = j + rex
        if blob[k] != 0xFF:
            continue
        modrm = blob[k + 1]
        if (modrm >> 3) & 7 != 2 or (modrm & 7) == 4:
            continue
        mod = modrm >> 6
        if mod == 2:
            return rva + j, struct.unpack_from("<i", blob, k + 2)[0]
        if mod == 1:
            return rva + j, struct.unpack_from("<b", blob, k + 2)[0]
    return None


for pattern in (rb"PrimitiveWorldIntersection", rb"RayWorldIntersection", rb"GetEntitiesInBox",
                rb"CollideEntityWith", rb"RayTraceEntity"):
    strings = find_strings(pattern)
    print(f"== {pattern.decode()}: {len(strings)} strings")
    slots = Counter()
    for srva, s in strings:
        refs = leas.get(srva, [])
        for ref in refs:
            call = next_vcall(ref + 7)
            if call:
                slots[call[1]] += 1
        print(f"  {srva:#x} refs={len(refs):3d} {s[:90]}")
    print("  consuming slots:", {f"{k:#x}": v for k, v in slots.most_common(8)})

# Every call through the world global's vtable: `mov rcx/rdi,[rip->0x224D9C8]` then the next vcall.
WORLD = 0x224D9C8
movs = []
for i in range(len(tbytes) - 7):
    if tbytes[i] in (0x48, 0x4C) and tbytes[i + 1] == 0x8B and (tbytes[i + 2] & 0xC7) == 0x05:
        disp = struct.unpack_from("<i", tbytes, i + 3)[0]
        if text.rva + i + 7 + disp == WORLD:
            movs.append(text.rva + i)
slots = Counter()
examples = defaultdict(list)
for m in movs:
    call = next_vcall(m + 7, 160)
    if call:
        slots[call[1]] += 1
        if len(examples[call[1]]) < 4:
            examples[call[1]].append(call[0])
print(f"== world global loads: {len(movs)}")
for slot, n in sorted(slots.items()):
    target = img.vtable_target(0x1D82E70 + slot) if 0 <= slot < 0x400 else None
    print(f"  slot {slot:#05x} calls={n:4d} target={target:#x} e.g. {[hex(e) for e in examples[slot]]}")
