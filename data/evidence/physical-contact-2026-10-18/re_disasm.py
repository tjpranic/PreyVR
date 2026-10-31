"""Scratch disassembly helpers over the Steam PreyDll.dll (hash-checked by preydll.Image)."""
import struct
import sys
from bisect import bisect_right
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, r"C:\Users\SPEC06\Documents\GitHub\PreyVR\tools\re")
from preydll import Image
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

img = Image()
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = False

_rva, _size = img.directory(3)
_raw = img.at(_rva, _size)
_entries = [struct.unpack_from("<III", _raw, i) for i in range(0, _size - 11, 12)]
FUNCS = sorted({e[:2] for e in _entries})
_starts = [f[0] for f in FUNCS]
_unwind = {e[0]: e for e in _entries}


def function_of(rva):
    """(begin, end) of the RUNTIME_FUNCTION chunk containing rva."""
    i = bisect_right(_starts, rva) - 1
    if i >= 0 and FUNCS[i][0] <= rva < FUNCS[i][1]:
        return FUNCS[i]
    return None


def _parent(begin):
    """The chunk a chained RUNTIME_FUNCTION continues, or None for a root."""
    e = _unwind.get(begin)
    if not e:
        return None
    info = e[2]
    head = img.at(info, 4)
    flags = head[0] >> 3
    if not flags & 4:      # UNW_FLAG_CHAININFO
        return None
    count = head[2]
    off = info + 4 + ((count + 1) & ~1) * 2
    return struct.unpack("<III", img.at(off, 12))[0]


def root_of(begin):
    seen = set()
    while True:
        p = _parent(begin)
        if p is None or p in seen:
            return begin
        seen.add(begin)
        begin = p


_children = None


def chunks(rva):
    """Every chunk (begin, end) of the function containing rva, root first."""
    global _children
    if _children is None:
        _children = defaultdict(list)
        for b, e in FUNCS:
            _children[root_of(b)].append((b, e))
    f = function_of(rva)
    root = root_of(f[0])
    return sorted(_children[root], key=lambda c: (c[0] != root, c[0]))


def _note(ins):
    note = ""
    if "rip" in ins.op_str and "[" in ins.op_str:
        tail = ins.op_str.split("rip")[1].split("]")[0].replace(" ", "")
        try:
            disp = int(tail.replace("+", ""), 16) if tail else 0
            target = ins.address + ins.size + disp
            note = f"  ; -> {target:#x}"
            try:
                s = img.cstring(target, 80)
                if len(s) >= 4 and s.isprintable():
                    note += f' "{s}"'
            except Exception:
                pass
        except ValueError:
            pass
    if ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
        note = f"  ; {ins.mnemonic} {int(ins.op_str, 16):#x}"
    return note


def dis(rva, size, mark=None):
    code = img.at(rva, size)
    out = []
    for ins in md.disasm(code, rva):
        flag = " <==" if mark is not None and ins.address == mark else ""
        out.append(f"{ins.address:08x} {ins.mnemonic:6s} {ins.op_str}{_note(ins)}{flag}")
    return "\n".join(out)


def dis_function(rva):
    parts = []
    for b, e in chunks(rva):
        parts.append(f"; ---- chunk {b:#x}-{e:#x} ({e - b} bytes)")
        parts.append(dis(b, e - b))
    return "\n".join(parts)


def window(rva, before=0x120, after=0x80):
    """Disassemble from an instruction boundary up to `before` bytes ahead of rva, to `after` past it."""
    f = function_of(rva)
    start = rva - before
    if f:
        code = img.at(f[0], rva - f[0] + after)
        addrs = [ins.address for ins in md.disasm(code, f[0])]
        start = next((a for a in addrs if a >= rva - before), f[0])
    return dis(start, rva - start + after, mark=rva)
