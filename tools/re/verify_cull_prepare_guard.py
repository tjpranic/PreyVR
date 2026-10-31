"""Offline check of the occlusion-prepare guard's code gate against the supported
PreyDll (docs/ENGINE-INSTABILITY.md, "The guard").

Reads the gate arrays out of src/engine/fixes/CullPrepareGuard.cpp, so the C++ stays the
one definition. Confirms each span, the C3DEngine +0x40 and +0x48 vtable slots,
and the handshake wiring the spans only imply: what PrepareOcclusion reads and
jumps to, both CCullThread thunks, RenderWorld's call into CullStart, and the
0.05 epsilon. No game access.

Until 2026-10-27 this also checked the cull watch's and the frame probe's
gates (verify_cull_camera_watch.py); both left with the watch.
Usage: verify_cull_prepare_guard.py [path to PreyDll.dll]
"""
import struct
import re
import sys
from pathlib import Path

from preydll import DEFAULT_MODULE as MODULE, Image, byte_arrays
GUARD = Path(__file__).resolve().parents[2] / "src" / "engine" / "fixes" / "CullPrepareGuard.cpp"
ENGINE_VTABLE_RVA = 0x1C912A0
PREPARE_RVA, PREPARE_SLOT, END_OCCLUSION_SLOT = 0x20DA10, 0x40, 0x48
PREPARE_THUNK_RVA, CULL_START_THUNK_RVA, END_THUNK_RVA = 0x281E40, 0x281E20, 0x281E30
PREPARE_CULLBUFFER_RVA, CULL_START_RVA, END_OCCLUSION_RVA = 0x23FFB0, 0x23F020, 0x23EFE0
JMP_REL32 = bytes.fromhex("E9")


def verify(path):
    """The occlusion-prepare guard (CullPrepareGuard.cpp): its 8 spans, then the
    wiring they only imply."""
    image = Image(path)
    read = image.at
    text = GUARD.read_text(encoding="utf-8")
    arrays = byte_arrays(text)
    table = re.search(r"kCode\{\{(.*?)\}\};", text, re.S).group(1)
    spans = [(PREPARE_RVA if rva == "kPrepareRva" else int(rva, 16), name)
             for rva, name in re.findall(r"\{(0x[0-9A-Fa-f]+|kPrepareRva), (k\w+Code), sizeof", table)]
    if len(spans) != 8:
        raise ValueError(f"Expected 8 gate spans in {GUARD.name}, found {len(spans)}")
    for rva, name in spans:
        if read(rva, len(arrays[name])) != arrays[name]:
            raise ValueError(f"Gate mismatch: {name} at 0x{rva:X}")
    if image.vtable_target(ENGINE_VTABLE_RVA + PREPARE_SLOT) != PREPARE_RVA:
        raise ValueError("C3DEngine +0x40 is not PrepareOcclusion")
    # What PrepareOcclusion reads: the cvars, the gEnv flag, the epsilon, CObjManager.
    reads = {
        "cvars": (image.rip_target(PREPARE_RVA, 7, 3), 0x243A688),
        "gEnv+0x2AB": (image.rip_target(PREPARE_RVA + 0x10, 7, 2), 0x224DC2B),
        "epsilon": (image.rip_target(PREPARE_RVA + 0x26, 8, 4), 0x1C7F77C),
        "CObjManager": (image.rip_target(PREPARE_RVA + 0x50, 7, 3), 0x243A698),
    }
    for what, (found, expected) in reads.items():
        if found != expected:
            raise ValueError(f"PrepareOcclusion reads {what} at 0x{found:X}, not 0x{expected:X}")
    epsilon = struct.unpack("<f", read(0x1C7F77C, 4))[0]
    if abs(epsilon - 0.05) > 1e-7:
        raise ValueError(f"PrepareOcclusion's epsilon is {epsilon}, not 0.05")
    # The jumps: PrepareOcclusion -> thunk -> PrepareCullbufferAsync; RenderWorld ->
    # thunk -> CullStart; C3DEngine +0x48 -> thunk -> EndOcclusion.
    jumps = {
        "PrepareOcclusion tail": (PREPARE_RVA + 0x57, PREPARE_THUNK_RVA),
        "prepare thunk": (PREPARE_THUNK_RVA + 7, PREPARE_CULLBUFFER_RVA),
        "CullStart thunk": (CULL_START_THUNK_RVA + 7, CULL_START_RVA),
        "EndOcclusion thunk": (END_THUNK_RVA + 7, END_OCCLUSION_RVA),
    }
    for what, (at, expected) in jumps.items():
        if read(at, 1) != JMP_REL32 or image.rip_target(at, 5, 1) != expected:
            raise ValueError(f"The {what} does not jump to 0x{expected:X}")
    if image.call_target(0x21E6F0) != CULL_START_THUNK_RVA:
        raise ValueError("RenderWorld's 0x21E570 does not call the CullStart thunk at 0x21E6F0")
    end = image.vtable_target(ENGINE_VTABLE_RVA + END_OCCLUSION_SLOT)
    if (read(end, 7)[:3] != bytes.fromhex("488B0D") or image.rip_target(end, 7, 3) != 0x243A698
            or read(end + 7, 1) != JMP_REL32 or image.rip_target(end + 7, 5, 1) != END_THUNK_RVA):
        raise ValueError("C3DEngine +0x48 does not reach EndOcclusion through CObjManager")
    if read(END_THUNK_RVA, 7) != bytes.fromhex("4881C100020000"):
        raise ValueError("The EndOcclusion thunk does not add CCullThread's offset")
    # The state-1 store sits in PrepareCullbufferAsync, after its call into 0x281E00.
    if image.call_target(0x24078B) != 0x281E00:
        raise ValueError("PrepareCullbufferAsync's state store is not after its 0x281E00 call")
    return {"sha256": image.sha256, "spans": [f"{name}@0x{rva:X}" for rva, name in spans]}


if __name__ == "__main__":
    result = verify(Path(sys.argv[1]) if len(sys.argv) > 1 else MODULE)
    print(f"cull prepare guard gate verified: {len(result['spans'])} spans, slots, calls, jumps and reads; "
          f"sha256 {result['sha256']}")
