"""Offline check of FocusHold's gate and everything it relies on, in the supported PreyDll.

Reads the gate arrays out of src/engine/fixes/FocusHold.cpp, so the C++ stays the one
definition, then confirms in the image:
- both throttle spans, and that the call's rel32 lands on the import slot;
- the slot is USER32!GetActiveWindow, and nothing else in .text reads it;
- the renderer's window: CreateWindowExA("CryENGINE") stored at +0xAC18, and
  renderer vtable +0x6C0 (the GetHWND the throttle compares with) returning it;
- g_pauseOnLoseFocus: registered with default 1, flags 0 and its help text;
- the audio dispatcher's manager call and table, and that manager types 0x10 and
  0x20 read the trigger IDs hashed from "lose_focus" and "get_focus".
No game access. Usage: verify_focus_hold.py [path to PreyDll.dll]
"""
import re
import struct
import sys
from pathlib import Path

from preydll import DEFAULT_MODULE, Image, byte_arrays

SOURCE = Path(__file__).resolve().parents[2] / "src" / "engine" / "fixes" / "FocusHold.cpp"
THROTTLE_CALL_RVA, SLOT_RVA = 0xDF58F0, 0x1C73A10
RENDERER_VTABLE_RVA, GET_HWND_SLOT, GET_HWND_RVA = 0x1DD2E08, 0x6C0, 0xF50A50
REGISTER_RVA = 0x18281D4  # lea rdx, help ... call [rax+0x38]


def gate_from_source(text):
    arrays = {name[:-4]: code for name, code in byte_arrays(text).items() if name.endswith("Code")}
    rvas = {name: int(value, 16) for name, value in re.findall(r"constexpr std::uintptr_t (k\w+)Rva = (0x[0-9A-Fa-f]+);", text)}
    # Every kXCode is checked at its kXRva: the throttle's two spans and the audio's five.
    spans = [(rvas[name], f"{name}Code", code) for name, code in arrays.items()]
    if len(spans) != 7:
        raise ValueError(f"Expected 7 gate spans in {SOURCE.name}, found {len(spans)}")
    if rvas.get("kActiveWindowSlot") != SLOT_RVA:
        raise ValueError(f"{SOURCE.name} names a different import slot")
    return spans


def rip_readers(image, target):
    """Every call/jmp [rip] and mov r64,[rip] in .text that resolves to target."""
    _, start, size, raw, _ = image.section(".text")
    code = image.data[raw:raw + size]
    found = []
    for match in re.finditer(rb"\xff[\x15\x25]|[\x48\x4c]\x8b[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", code):
        at = match.end()
        if at + 4 <= len(code) and start + at + 4 + struct.unpack_from("<i", code, at)[0] == target:
            found.append(start + match.start())
    return found


def verify(path):
    image = Image(path)
    checks = []
    for rva, name, expected in gate_from_source(SOURCE.read_text(encoding="utf-8")):
        if image.at(rva, len(expected)) != expected:
            raise ValueError(f"Gate mismatch: {name} at 0x{rva:X}")
        checks.append(f"{name}@0x{rva:X}")
    call = image.at(THROTTLE_CALL_RVA, 6)
    if call[:2] != b"\xff\x15" or THROTTLE_CALL_RVA + 6 + struct.unpack("<i", call[2:])[0] != SLOT_RVA:
        raise ValueError("The throttle call does not read the GetActiveWindow slot")
    if image.import_slots().get(SLOT_RVA) != ("USER32.DLL", "GetActiveWindow"):
        raise ValueError(f"Slot 0x{SLOT_RVA:X} is {image.import_slots().get(SLOT_RVA)}")
    readers = rip_readers(image, SLOT_RVA)
    if readers != [THROTTLE_CALL_RVA]:
        raise ValueError("Other readers of the slot: " + ", ".join(f"0x{r:X}" for r in readers))
    checks += ["slot=USER32!GetActiveWindow", "slot_readers=1"]

    get_hwnd = image.vtable_target(RENDERER_VTABLE_RVA + GET_HWND_SLOT)
    if get_hwnd != GET_HWND_RVA or image.at(GET_HWND_RVA, 8) != bytes.fromhex("488B8118AC0000C3"):
        raise ValueError("Renderer vtable +0x6C0 is not mov rax,[rcx+0xAC18]; ret")
    # lea rdx,"CryENGINE" ... call [CreateWindowExA]; mov [rbx+0xAC18],rax
    if image.cstring(0xF541CA + 7 + struct.unpack("<i", image.at(0xF541CA + 3, 4))[0]) != "CryENGINE":
        raise ValueError("The renderer window's class is not CryENGINE")
    if image.at(0xF541DF, 7) != bytes.fromhex("48898318AC0000"):
        raise ValueError("CreateWindowExA's result is not stored at +0xAC18")
    checks += ["GetHWND=+0xAC18", "class=CryENGINE"]

    # Register("g_pauseOnLoseFocus", &var, default r9d = rdi + 1, flags [rsp+0x20] = edi, help).
    # rdi is zeroed at 0x182810D and, callee-saved, only stored from until here.
    if image.at(0x182810D, 2) != b"\x33\xff":
        raise ValueError("The registering function does not zero edi at 0x182810D")
    block = image.at(REGISTER_RVA, 0x2F)
    expected_shape = [(0x00, "488D15"), (0x07, "C644243801"), (0x0C, "448D4F01"), (0x10, "48897C2430"),
                      (0x15, "4C8D05"), (0x1C, "4889542428"), (0x21, "488D15"), (0x28, "897C2420"), (0x2C, "FF5038")]
    for offset, hexbytes in expected_shape:
        if block[offset:offset + len(hexbytes) // 2] != bytes.fromhex(hexbytes):
            raise ValueError(f"g_pauseOnLoseFocus registration differs at +0x{offset:X}")
    rel = lambda offset: REGISTER_RVA + offset + 7 + struct.unpack_from("<i", block, offset + 3)[0]
    if image.cstring(rel(0x21)) != "g_pauseOnLoseFocus":
        raise ValueError("The registered name is not g_pauseOnLoseFocus")
    if image.cstring(rel(0x00)) != "Pause the game whenever the window loses focus":
        raise ValueError("Unexpected help text for g_pauseOnLoseFocus")
    checks += ["g_pauseOnLoseFocus default=1 flags=0"]

    # The audio: the dispatcher's class-1 call is the manager handler; the table
    # routes 0x10 to the lose-focus case; the two cases read the IDs that the
    # initialiser hashes from "lose_focus" and "get_focus".
    if image.at(0xD5BBC0, 1) != b"\xe8" or image.rip_target(0xD5BBC0, 5, 1) != 0xD5A630:
        raise ValueError("The dispatcher's manager call is not 0xD5A630")
    if image.u32(0xD5AE68 + 0x10 * 4) != 0xD5A6EA:
        raise ValueError("Manager type 0x10 is not routed to 0xD5A6EA")
    reads = {"lose_focus": image.rip_target(0xD5A6FC, 6, 2), "get_focus": image.rip_target(0xD5A7C0, 6, 2)}
    for name, lea, store in (("lose_focus", 0xD6AEB9, 0xD6AF11), ("get_focus", 0xD6AF0A, 0xD6AF61)):
        if image.cstring(image.rip_target(lea, 7, 3)) != name or image.at(store, 3) != b"\x44\x89\x15":
            raise ValueError(f"The {name} trigger ID initialiser differs")
        if image.rip_target(store, 7, 3) != reads[name]:
            raise ValueError(f"The {name} case does not read the {name} trigger ID")
    # MinHook relocates the entry it overwrites: two rsp-relative stores, no RIP use.
    if image.at(0xD5B950, 10) != bytes.fromhex("48895C241048896C2418"):
        raise ValueError("The audio dispatcher's entry is not the expected relocation-free prologue")
    checks += ["audio 0x10=lose_focus 0x20=get_focus", "audio_entry=relocatable"]
    return {"sha256": image.sha256, "checks": checks}


if __name__ == "__main__":
    module = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_MODULE
    if not module.is_file():
        raise SystemExit("PreyDll.dll not found; pass its path")
    result = verify(module)
    print(f"focus hold verified: {', '.join(result['checks'])}; sha256 {result['sha256']}")
