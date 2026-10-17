"""Offline check of CullCameraWatch's code gate against the supported PreyDll.

Reads the gate arrays out of src/dll/CullCameraWatch.cpp, so the C++ stays the
one definition, and confirms each span, the C3DEngine +0x90 vtable slot and a
relocation-free hook entry in the image. No game access.
Usage: verify_cull_camera_watch.py [path to PreyDll.dll]
"""
import re
import sys
from pathlib import Path

from preydll import DEFAULT_MODULE as MODULE, Image, byte_arrays
SOURCE = Path(__file__).resolve().parents[2] / "src" / "dll" / "CullCameraWatch.cpp"
RESET_RVA, ENGINE_VTABLE_RVA, RESET_SLOT = 0x2105A0, 0x1C912A0, 0x90


def gate_from_source(text):
    arrays = byte_arrays(text)
    table = re.search(r"kCode\{\{(.*?)\}\};", text, re.S).group(1)
    spans = []
    for rva, name in re.findall(r"\{(0x[0-9A-Fa-f]+|kResetRva), (k\w+Code), sizeof", table):
        spans.append((RESET_RVA if rva == "kResetRva" else int(rva, 16), name, arrays[name]))
    if len(spans) != 8:
        raise ValueError(f"Expected 8 gate spans in {SOURCE.name}, found {len(spans)}")
    return spans


def verify(path):
    image = Image(path)
    read = image.at
    spans = gate_from_source(SOURCE.read_text(encoding="utf-8"))
    for rva, name, expected in spans:
        if read(rva, len(expected)) != expected:
            raise ValueError(f"Gate mismatch: {name} at 0x{rva:X}")
    slot = image.vtable_target(ENGINE_VTABLE_RVA + RESET_SLOT)
    if slot != RESET_RVA:
        raise ValueError(f"C3DEngine +0x90 holds RVA 0x{slot:X}, not the hooked function")
    # MinHook copies the instructions its 5-byte jump overwrites: push rbx;
    # sub rsp, 0x30 -- six bytes, nothing RIP-relative or branching.
    if read(RESET_RVA, 6) != bytes.fromhex("40534883EC30"):
        raise ValueError("Hook entry is not the expected relocation-free prologue")
    return {"sha256": image.sha256, "spans": [f"{name}@0x{rva:X}" for rva, name, _ in spans]}


if __name__ == "__main__":
    result = verify(Path(sys.argv[1]) if len(sys.argv) > 1 else MODULE)
    print(f"cull camera watch gate verified: {len(result['spans'])} spans, slot and entry; sha256 {result['sha256']}")
