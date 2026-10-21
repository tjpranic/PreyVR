"""Offline check of the cull watch's, frame probe's and occlusion-prepare guard's
code gates against the supported PreyDll.

Reads the gate arrays out of src/dll/CullCameraWatch.cpp, FrameProbe.cpp and
CullPrepareGuard.cpp, so the C++ stays the one definition. Confirms each span,
the C3DEngine +0x90 and +0x40 vtable slots, that the probed consumer job calls
the gated consumer loop, that the watch's hook entries are relocation-free, and
the guard's handshake wiring: what PrepareOcclusion reads and jumps to, both
CCullThread thunks, RenderWorld's call into CullStart, and the 0.05 epsilon.
No game access.
Usage: verify_cull_camera_watch.py [path to PreyDll.dll]
"""
import struct
import re
import sys
from pathlib import Path

from preydll import DEFAULT_MODULE as MODULE, Image, byte_arrays
SOURCE = Path(__file__).resolve().parents[2] / "src" / "dll" / "CullCameraWatch.cpp"
PROBE = SOURCE.with_name("FrameProbe.cpp")
RESET_RVA, ENGINE_VTABLE_RVA, RESET_SLOT = 0x2105A0, 0x1C912A0, 0x90
CONSUMER_JOB_RVA, CONSUMER_LOOP_RVA, CONSUMER_CALL = 0x23F1B0, 0x23EBD0, 0x30
GUARD = SOURCE.with_name("CullPrepareGuard.cpp")
PREPARE_RVA, PREPARE_SLOT, END_OCCLUSION_SLOT = 0x20DA10, 0x40, 0x48
PREPARE_THUNK_RVA, CULL_START_THUNK_RVA, END_THUNK_RVA = 0x281E40, 0x281E20, 0x281E30
PREPARE_CULLBUFFER_RVA, CULL_START_RVA, END_OCCLUSION_RVA = 0x23FFB0, 0x23F020, 0x23EFE0
JMP_REL32 = bytes.fromhex("E9")


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
    names = [f"{name}@0x{rva:X}" for rva, name, _ in spans]

    # The frame probe's consumer job: the whole function, and its one call lands
    # on the consumer loop the watch gates (kConsumerFrameCode at 0x23EBD0).
    job = byte_arrays(PROBE.read_text(encoding="utf-8"))["kConsumerJobCode"]
    if read(CONSUMER_JOB_RVA, len(job)) != job:
        raise ValueError(f"Gate mismatch: kConsumerJobCode at 0x{CONSUMER_JOB_RVA:X}")
    if image.call_target(CONSUMER_JOB_RVA + CONSUMER_CALL) != CONSUMER_LOOP_RVA:
        raise ValueError("The consumer job does not call the consumer loop")
    # sub rsp, 0x68; movups xmm0, [rcx+8] -- eight bytes, nothing RIP-relative.
    if read(CONSUMER_JOB_RVA, 8) != bytes.fromhex("4883EC680F104108"):
        raise ValueError("Consumer job entry is not the expected relocation-free prologue")
    names.append(f"kConsumerJobCode@0x{CONSUMER_JOB_RVA:X}")
    names += verify_guard(image)
    return {"sha256": image.sha256, "spans": names}


def verify_guard(image):
    """The occlusion-prepare guard (CullPrepareGuard.cpp): its 8 spans, then the
    wiring they only imply."""
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
    # MinHook relocates the RIP-relative load at the observation hook's entry.
    if read(PREPARE_RVA, 3) != bytes.fromhex("488B05"):
        raise ValueError("PrepareOcclusion's entry is not the expected mov rax,[rip+disp32]")
    return [f"{name}@0x{rva:X}" for rva, name in spans]


if __name__ == "__main__":
    result = verify(Path(sys.argv[1]) if len(sys.argv) > 1 else MODULE)
    print(f"cull camera watch, frame probe and prepare guard gates verified: {len(result['spans'])} spans, "
          f"slots, calls, jumps, reads and entries; "
          f"sha256 {result['sha256']}")
