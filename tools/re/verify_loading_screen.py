"""Offline check of LoadingScreen's gate and everything it relies on, in the supported PreyDll.

Reads the gate arrays out of src/dll/LoadingScreen.cpp, so the C++ stays the one
definition, then confirms in the image:
- both gate spans: the loadtime present's entry through its swapchain Present,
  and the loading loop's lock, render and present;
- renderer vtable +0x8E0 holds the present, and the loop is the only caller of
  that slot in .text;
- the present and RT_EndFrame present the same swapchain field (+0xAE88);
- ProcessCommands defers RT_EndFrame while the loop runs, and the loop runs it
  once after;
- gfx_loadtimethread is registered with default 1, so the loop is the default;
- FlashUI's loading element is named "DanielleLoadingScreen", the name HudBridge
  asks for.
No game access. Usage: verify_loading_screen.py [path to PreyDll.dll]
"""
import re
import sys
from pathlib import Path

from preydll import DEFAULT_MODULE, Image, byte_arrays

SOURCE = Path(__file__).resolve().parents[2] / "src" / "dll" / "LoadingScreen.cpp"
HUD_SOURCE = Path(__file__).resolve().parents[2] / "src" / "dll" / "HudBridge.cpp"
PRESENT_RVA, VTABLE_RVA, PRESENT_SLOT, END_FRAME_SLOT = 0xF7E820, 0x1DD2E08, 0x8E0, 0x8D0
END_FRAME_RVA = 0xF7E210
# RT_EndFrame's Present: mov rcx,[rdi+0xAE88] ... call [r9+0x40].
END_FRAME_PRESENT_RVA = 0xF7E48A
# ProcessCommands, case 8: while SRenderThread+0xA0 (the loop's mode) is set, flag
# +0x44 instead of calling vtable +0x8D0.
DEFERRAL_RVA = 0xFE9D0A
# After the loop: if +0x44, clear it and call vtable +0x8D0 once.
REPLAY_RVA = 0xFE9A1E
GFX_LOADTIMETHREAD_RVA = 0x2D40A1  # lea rdx,"gfx_loadtimethread"; the help text and default precede it


def gate_from_source(text):
    arrays = byte_arrays(text)
    rvas = {name: int(value, 16) for name, value in re.findall(r"constexpr std::uintptr_t (k\w+)Rva = (0x[0-9A-Fa-f]+)", text)}
    spans = [(rvas[name[:-4]], name, code) for name, code in arrays.items() if name.endswith("Code")]
    if len(spans) != 2:
        raise ValueError(f"Expected 2 gate spans in {SOURCE.name}, found {len(spans)}")
    if rvas.get("kPresent") != PRESENT_RVA:
        raise ValueError(f"{SOURCE.name} names a different present")
    if f"kRendererVtableRva = 0x{VTABLE_RVA:X}" not in text or f"kPresentSlot = 0x{PRESENT_SLOT:X}" not in text:
        raise ValueError(f"{SOURCE.name} names a different vtable slot")
    return spans


def slot_callers(image, slot):
    """Every call [reg+slot] (FF 90..97 disp32) in .text."""
    _, start, size, raw, _ = image.section(".text")
    code = image.data[raw:raw + size]
    pattern = re.compile(rb"\xff[\x90-\x97]" + re.escape(slot.to_bytes(4, "little")))
    return [start + match.start() for match in pattern.finditer(code)]


def verify(path):
    image = Image(path)
    checks = []
    for rva, name, expected in gate_from_source(SOURCE.read_text(encoding="utf-8")):
        if image.at(rva, len(expected)) != expected:
            raise ValueError(f"Gate mismatch: {name} at 0x{rva:X}")
        checks.append(f"{name}@0x{rva:X}")

    if image.vtable_target(VTABLE_RVA + PRESENT_SLOT) != PRESENT_RVA:
        raise ValueError("Renderer vtable +0x8E0 does not hold the loadtime present")
    if image.vtable_target(VTABLE_RVA + END_FRAME_SLOT) != END_FRAME_RVA:
        raise ValueError("Renderer vtable +0x8D0 is not RT_EndFrame")
    callers = slot_callers(image, PRESENT_SLOT)
    if callers != [0xFE99B7]:
        raise ValueError(f"Expected one call of vtable +0x8E0 (0xFE99B7), found {[hex(c) for c in callers]}")
    checks.append("slot+0x8E0=0xF7E820 only_caller=0xFE99B7")

    swapchain = bytes([0x48, 0x8B, 0x8B, 0x88, 0xAE, 0x00, 0x00])  # mov rcx,[rbx+0xAE88]
    if image.at(PRESENT_RVA + 0x52, 7) != swapchain or image.at(END_FRAME_PRESENT_RVA, 7) != bytes(
            [0x48, 0x8B, 0x8F, 0x88, 0xAE, 0x00, 0x00]):
        raise ValueError("The two presents do not read the same swapchain field")
    checks.append("swapchain=+0xAE88 both")

    # cmp [r14+0xA0],0; jnz +0x18 | ... mov rax,[r10]; call [rax+0x8D0] | ...
    # call; mov byte [r14+0x44],1
    deferral = image.at(DEFERRAL_RVA, 0x2C)
    if (deferral[:10] != bytes([0x41, 0x83, 0xBE, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x75, 0x18])
            or deferral[0x14:0x1D] != bytes([0x49, 0x8B, 0x02, 0xFF, 0x90, 0xD0, 0x08, 0x00, 0x00])
            or deferral[0x22] != 0xE8
            or deferral[0x27:0x2C] != bytes([0x41, 0xC6, 0x46, 0x44, 0x01])):
        raise ValueError("ProcessCommands' RT_EndFrame deferral differs")
    replay = image.at(REPLAY_RVA, 0x1A)
    if replay[:4] != bytes([0x44, 0x38, 0x7F, 0x44]) or replay[0x14:0x1A] != bytes([0xFF, 0x90, 0xD0, 0x08, 0x00, 0x00]):
        raise ValueError("The loop's one RT_EndFrame afterwards differs")
    checks.append("end_frame=deferred_then_once")

    lea = image.at(GFX_LOADTIMETHREAD_RVA, 7)
    if lea[:3] != b"\x48\x8d\x15" or image.cstring(image.rip_target(GFX_LOADTIMETHREAD_RVA, 7, 3)) != "gfx_loadtimethread":
        raise ValueError("gfx_loadtimethread registration moved")
    # mov r9d,1 (the default) before it.
    if image.at(GFX_LOADTIMETHREAD_RVA - 0x0B, 6) != bytes([0x41, 0xB9, 0x01, 0x00, 0x00, 0x00]):
        raise ValueError("gfx_loadtimethread's default is not 1")
    checks.append("gfx_loadtimethread=1")

    if image.cstring(0x1CB49E0) != "DanielleLoadingScreen":
        raise ValueError("The loading element's name moved")
    if '"DanielleLoadingScreen",' not in HUD_SOURCE.read_text(encoding="utf-8"):
        raise ValueError("HudBridge does not ask for DanielleLoadingScreen")
    checks.append("element=DanielleLoadingScreen")
    return checks


if __name__ == "__main__":
    module = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_MODULE
    try:
        for line in verify(module):
            print("ok", line)
    except ValueError as error:
        print("FAIL", error)
        sys.exit(1)
    print("loading screen gate verified:", module)
