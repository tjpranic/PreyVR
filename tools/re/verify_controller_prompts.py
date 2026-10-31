"""Offline check of ControllerPrompts' frame check and everything it relies on, in the supported PreyDll.

Reads the spans and field offsets out of src/controls/input/ControllerPrompts.cpp, so the
C++ stays the one definition, then confirms in the image:
- the four spans, each at its RVA;
- the setter: reads and stores [rcx+0x28] = edx, returning early when equal;
- the cvar callback: [gGame]+0x1A8 -> +0x28, a scheme of 1/2 -> 5 when input
  vtable +0x1A8 finds a device, else 3, then a tail jump to the setter;
- the tracker: [gGameCVars]+0x924 gates its keyboard/mouse call (scheme 2),
  its pad branch calls the setter with 3, and mouse events are skipped in 5;
- hud_forceControllerGlyphs: registered at +0x924, default 0, with the callback;
- CGame::Init stores the new CUIManager at +0x1A8 before CUIManager::Init;
- why the callback picks 5: input vtable +0x1A8 returns the first device whose
  +0xD8 is true, the Steam device's +0xD8 is `mov al,1` and the mouse's
  `xor al,al`.
No game access. Usage: verify_controller_prompts.py [path to PreyDll.dll]
"""
import re
import sys
from pathlib import Path

from preydll import DEFAULT_MODULE, Image, byte_arrays

SOURCE = Path(__file__).resolve().parents[2] / "src" / "controls" / "input" / "ControllerPrompts.cpp"
INPUT_GLOBAL_RVA = 0x224D9D8  # gEnv->pInput, gEnv+0x58 (R-080)
REGISTER_RVA = 0x1720516      # mov rax,[rcx]; lea rdx,callback ... call [rax+0x38]
CGAME_INIT_RVA = 0x16FE5F7    # call CUIManager ctor; ...; mov [rdi+0x1A8],rax; call CUIManager::Init
UI_CTOR_RVA, UI_INIT_RVA = 0x182BA40, 0x182C1C0
INPUT_VTABLES = (0x1D5C4F0, 0x1D5D208)  # Steam-controller and DirectInput CBaseInput subclasses
FIND_DEVICE_RVA = 0x9D6750
STEAM_DEVICE_VTABLE, MOUSE_VTABLE = 0x1D5C050, 0x1D5CC38


def contract_from_source(text):
    arrays = {name[:-4]: code for name, code in byte_arrays(text).items() if name.endswith("Code")}
    values = {name: int(value, 16) for name, value in
              re.findall(r"constexpr std::uintptr_t (k\w+) = (0x[0-9A-Fa-f]+);", text)}
    spans = [(values[f"{name}Rva"], f"{name}Code", code) for name, code in arrays.items()]
    if len(spans) != 4:
        raise ValueError(f"Expected 4 spans in {SOURCE.name}, found {len(spans)}")
    return spans, values


def verify(path):
    image = Image(path)
    spans, k = contract_from_source(SOURCE.read_text(encoding="utf-8"))
    checks = []
    for rva, name, expected in spans:
        if image.at(rva, len(expected)) != expected:
            raise ValueError(f"Span mismatch: {name} at 0x{rva:X}")
        checks.append(f"{name}@0x{rva:X}")
    setter, ui_scheme = k["kSetSchemeRva"], k["kUiScheme"]

    # The setter: mov ebx,[rcx+0x28]; mov esi,edx; ...; cmp ebx,edx; je out; ...; mov [rcx+0x28],edx.
    if image.at(setter + 0x0A, 3) != bytes([0x8B, 0x59, ui_scheme]) or image.at(setter + 0x12, 2) != b"\x3b\xda":
        raise ValueError("The setter does not compare [rcx+0x28] with the new scheme")
    if image.at(k["kSetSchemeStoreRva"], 3) != bytes([0x89, 0x51, ui_scheme]):
        raise ValueError("The setter does not store the scheme at +0x28")
    checks.append("setter [rcx+0x28]=edx")

    # The callback, by instruction offset in its span.
    callback = k["kGlyphCallbackRva"]
    for at in (0x0E, 0x46):
        if image.rip_target(callback + at, 7, 3) != k["kGameRva"]:
            raise ValueError(f"The callback's load at +0x{at:X} is not gGame")
    if image.u32(callback + 0x18) != k["kGameUiManager"] or image.u32(callback + 0x50) != k["kGameUiManager"]:
        raise ValueError("The callback does not read CUIManager at gGame+0x1A8")
    if image.at(callback + 0x1E, 1)[0] != ui_scheme:
        raise ValueError("The callback does not read the scheme at +0x28")
    if image.rip_target(callback + 0x26, 7, 3) != INPUT_GLOBAL_RVA or image.u32(callback + 0x32) != 0x1A8:
        raise ValueError("The callback does not ask the input system's vtable +0x1A8")
    if image.u32(callback + 0x37) != 3 or image.u32(callback + 0x3C) != 5:
        raise ValueError("The callback's schemes are not 3 and 5")
    if image.at(callback + 0x58, 1) != b"\xe9" or image.rip_target(callback + 0x58, 5, 1) != setter:
        raise ValueError("The callback does not end in the setter")
    checks.append("callback 1/2->5|3 via setter")

    # The tracker.
    force = k["kTrackerForceRva"]
    if image.rip_target(force, 7, 3) != k["kGameCVarsRva"] or image.u32(force + 9) != k["kForceGlyphs"]:
        raise ValueError("The tracker's gate is not [gGameCVars]+0x924")
    if image.call_target(force + 0x19) != setter or image.u32(force + 0x11) != 2:
        raise ValueError("The tracker's keyboard/mouse branch does not set scheme 2")
    if image.at(0x182D40C, 5) != bytes.fromhex("BA03000000") or image.call_target(0x182D411) != setter:
        raise ValueError("The tracker's pad branch does not set scheme 3")
    if image.at(0x182D436, 6) != bytes.fromhex("837B1805" "7425"):
        raise ValueError("The tracker does not skip mouse events in scheme 5")
    checks.append("tracker gate +0x924, pad=3, mouse skipped in 5")

    # hud_forceControllerGlyphs: mov rax,[rcx]; lea rdx,callback; mov byte [rsp+0x38],1;
    # lea r8,[rdi+0x924]; mov [rsp+0x30],rdx; xor r9d,r9d (default 0); lea rdx,help;
    # mov [rsp+0x28],rdx; lea rdx,name; mov [rsp+0x20],r15d; call [rax+0x38].
    block = image.at(REGISTER_RVA, 0x39)
    shape = [(0x00, "488B01"), (0x03, "488D15"), (0x0A, "C644243801"), (0x0F, "4C8D87"), (0x16, "4889542430"),
             (0x1B, "4533C9"), (0x1E, "488D15"), (0x25, "4889542428"), (0x2A, "488D15"), (0x31, "44897C2420"),
             (0x36, "FF5038")]
    for offset, hexbytes in shape:
        if block[offset:offset + len(hexbytes) // 2] != bytes.fromhex(hexbytes):
            raise ValueError(f"hud_forceControllerGlyphs registration differs at +0x{offset:X}")
    if image.rip_target(REGISTER_RVA + 0x03, 7, 3) != callback or image.u32(REGISTER_RVA + 0x12) != k["kForceGlyphs"]:
        raise ValueError("The registration's callback or storage differs")
    if image.cstring(image.rip_target(REGISTER_RVA + 0x2A, 7, 3)) != "hud_forceControllerGlyphs":
        raise ValueError("The registered name is not hud_forceControllerGlyphs")
    if not image.cstring(image.rip_target(REGISTER_RVA + 0x1E, 7, 3), 256).startswith("If this is set then we won't switch"):
        raise ValueError("Unexpected help text for hud_forceControllerGlyphs")
    checks.append("hud_forceControllerGlyphs +0x924 default=0 callback")

    # CGame::Init: call ctor; ...; mov rcx,rax; mov [rdi+0x1A8],rax; call CUIManager::Init.
    if image.call_target(CGAME_INIT_RVA) != UI_CTOR_RVA or image.call_target(CGAME_INIT_RVA + 0x14) != UI_INIT_RVA:
        raise ValueError("CGame::Init does not construct and initialise CUIManager here")
    if image.at(CGAME_INIT_RVA + 0x0D, 7) != bytes([0x48, 0x89, 0x87]) + k["kGameUiManager"].to_bytes(4, "little"):
        raise ValueError("CGame::Init does not store CUIManager at +0x1A8")
    checks.append("CGame+0x1A8=CUIManager")

    # Why 5: every input class's +0x1A8 is the device walk, which returns the first
    # device answering +0xD8; the Steam device always answers.
    for vtable in INPUT_VTABLES:
        if image.vtable_target(vtable + 0x1A8) != FIND_DEVICE_RVA:
            raise ValueError(f"Input vtable 0x{vtable:X} +0x1A8 is not the device walk")
    if image.at(FIND_DEVICE_RVA, 0x40).find(bytes.fromhex("FF90D8000000")) < 0:
        raise ValueError("The device walk does not ask each device's +0xD8")
    steam, mouse = (image.vtable_target(v + 0xD8) for v in (STEAM_DEVICE_VTABLE, MOUSE_VTABLE))
    if image.at(steam, 3) != b"\xb0\x01\xc3" or image.at(mouse, 3) != b"\x32\xc0\xc3":
        raise ValueError("The Steam device's +0xD8 is not true, or the mouse's not false")
    checks.append("steam device +0xD8=1")
    return {"sha256": image.sha256, "checks": checks}


if __name__ == "__main__":
    module = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_MODULE
    if not module.is_file():
        raise SystemExit("PreyDll.dll not found; pass its path")
    result = verify(module)
    print(f"controller prompts verified: {', '.join(result['checks'])}; sha256 {result['sha256']}")
