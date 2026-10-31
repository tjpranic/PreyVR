"""Contract rows {rva, size, fnv} for room-scale's physics calls, sized by .pdata.

Checks the method against ContactNative.h's LivingGetStatus row first.
"""
import struct
import sys

sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\tools\re")
from preydll import Image, fnv1a64  # noqa: E402

img = Image()
rva, size = img.directory(3)
raw = img.at(rva, size)
triples = [struct.unpack_from("<III", raw, i) for i in range(0, size - 11, 12)]
starts = {b: e for b, e, _ in triples}
unwind = {b: u for b, _, u in triples}
relocs = img.relocations()


def parent(begin):
    """The chunk a chained RUNTIME_FUNCTION continues (UNW_FLAG_CHAININFO), else None."""
    info = unwind[begin]
    head = img.at(info, 4)
    if not (head[0] >> 3) & 4:
        return None
    count = head[2]
    return struct.unpack("<III", img.at(info + 4 + ((count + 1) & ~1) * 2, 12))[0]


def root(begin):
    seen = set()
    while (p := parent(begin)) is not None and p not in seen:
        seen.add(begin)
        begin = p
    return begin


def extent(at):
    """End of the function's chunks laid out contiguously after its entry."""
    end = starts[at]
    while end in starts and root(end) == at:
        end = starts[end]
    return end


def row(name, at):
    assert at in starts, f"{name}: no RUNTIME_FUNCTION starts at {at:#x}"
    n = extent(at) - at
    body = img.at(at, n)
    inside = [p for p in relocs if at <= p < at + n]
    print(f"    {{0x{at:X},{n},0x{fnv1a64(body):016X}ull}}, // {name}   relocs={len(inside)}")
    return n


assert row("LivingGetStatus(check)", 0xB475D0) == 2231, "method disagrees with ContactNative.h"
for name, at in (("LivingSetParams", 0xB49610), ("PhysicalSetParams", 0xB88AA0),
                 ("LivingStanceOverlap", 0xB56900), ("PhysicsProxyXForm", 0x96BE00), ("PhysicsChangeRequest", 0xB1BCD0)):
    row(name, at)
