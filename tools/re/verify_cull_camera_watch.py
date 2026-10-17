"""Offline check of CullCameraWatch's code gate against the supported PreyDll.

Reads the gate arrays out of src/dll/CullCameraWatch.cpp, so the C++ stays the
one definition, and confirms each span, the C3DEngine +0x90 vtable slot and a
relocation-free hook entry in the image. No game access.
Usage: verify_cull_camera_watch.py [path to PreyDll.dll]
"""
import hashlib
import re
import struct
import sys
from pathlib import Path

from verify_h005_skinning import DEFAULT_MODULE as MODULE  # PREYVR_PREYDLL or the Steam library holding Prey
SHA256 = "7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7"
SOURCE = Path(__file__).resolve().parents[2] / "src" / "dll" / "CullCameraWatch.cpp"
RESET_RVA, ENGINE_VTABLE_RVA, RESET_SLOT = 0x2105A0, 0x1C912A0, 0x90


def gate_from_source(text):
    arrays = {name: bytes(int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})\b", body))
              for name, body in re.findall(r"constexpr std::uint8_t (k\w+Code)\[\] = \{(.*?)\};", text, re.S)}
    table = re.search(r"kCode\{\{(.*?)\}\};", text, re.S).group(1)
    spans = []
    for rva, name in re.findall(r"\{(0x[0-9A-Fa-f]+|kResetRva), (k\w+Code), sizeof", table):
        spans.append((RESET_RVA if rva == "kResetRva" else int(rva, 16), name, arrays[name]))
    if len(spans) != 8:
        raise ValueError(f"Expected 8 gate spans in {SOURCE.name}, found {len(spans)}")
    return spans


def verify(path):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        raise ValueError(f"Unsupported image: {digest}")
    u16 = lambda offset: struct.unpack_from("<H", data, offset)[0]
    u32 = lambda offset: struct.unpack_from("<I", data, offset)[0]
    pe = u32(0x3C)
    opt = pe + 24
    if u16(pe + 4) != 0x8664 or u16(opt) != 0x20B:
        raise ValueError("Expected x64 PE32+")
    image_base = struct.unpack_from("<Q", data, opt + 24)[0]
    sections = []
    for index in range(u16(pe + 6)):
        off = opt + u16(pe + 20) + index * 40
        sections.append((u32(off + 12), u32(off + 16), u32(off + 20)))

    def read(rva, size):
        section = next((s for s in sections if s[0] <= rva and rva + size <= s[0] + s[1]), None)
        if section is None:
            raise ValueError(f"Unmapped RVA 0x{rva:X}")
        start, _, raw = section
        return data[raw + rva - start:raw + rva - start + size]

    spans = gate_from_source(SOURCE.read_text(encoding="utf-8"))
    for rva, name, expected in spans:
        if read(rva, len(expected)) != expected:
            raise ValueError(f"Gate mismatch: {name} at 0x{rva:X}")
    slot = struct.unpack("<Q", read(ENGINE_VTABLE_RVA + RESET_SLOT, 8))[0]
    if slot != image_base + RESET_RVA:
        raise ValueError(f"C3DEngine +0x90 holds 0x{slot:X}, not the hooked function")
    # MinHook copies the instructions its 5-byte jump overwrites: push rbx;
    # sub rsp, 0x30 -- six bytes, nothing RIP-relative or branching.
    if read(RESET_RVA, 6) != bytes.fromhex("40534883EC30"):
        raise ValueError("Hook entry is not the expected relocation-free prologue")
    return {"sha256": digest, "spans": [f"{name}@0x{rva:X}" for rva, name, _ in spans]}


if __name__ == "__main__":
    result = verify(Path(sys.argv[1]) if len(sys.argv) > 1 else MODULE)
    print(f"cull camera watch gate verified: {len(result['spans'])} spans, slot and entry; sha256 {result['sha256']}")
