"""Offline check of what the hacking minigame's panel relies on, in the supported PreyDll.

Confirms in the image:
- FlashUI's hacking element is named "DanielleHacking2", the last name HudBridge
  polls (its kHackingMask);
- CSystem::Render skips the world on the no-world flag;
- that flag's writers are the PDA's and hacking's and no others, so hacking is
  the only world-hiding screen the menu list lacked;
- hacking sets it at each of its entries and clears it on its way out;
- ArkHackingUI's stick and prompt actions are the ones the bindings evidence puts
  on xi_thumblx, xi_thumbly and X, Y, A, B: the keys the mod sends it.
No game access. Usage: verify_hacking_ui.py [path to PreyDll.dll]
"""
import re
import sys
from pathlib import Path

from preydll import DEFAULT_MODULE, Image

ROOT = Path(__file__).resolve().parents[2]
HUD_SOURCE = ROOT / "src" / "ui" / "hud" / "HudBridge.cpp"
BINDINGS = ROOT / "docs" / "evidence" / "bindings-20261017" / "gamepad-bindings.txt"

NO_WORLD_FLAG_RVA = 0x224DC2B
ELEMENT_NAME_RVA = 0x1E63F28
ELEMENT_LOOKUP_RVA = 0x1626418  # lea rdx,"DanielleHacking2", in the open 0x1626370
RENDER_FLAG_TEST_RVA = 0xE0BAC7  # cmp byte [rax+0x2AB],0
RENDER_FLAG_TEST = bytes.fromhex("80B8AB0200" "0000")

# mov byte [rip+flag],...: where, and which function holds it.
HACKING_WRITERS = {
    0x1623259: 0x1623100,  # the attempt, no hand animation
    0x16238A2: 0x1623820,  # the way out
    0x16238C9: 0x1623820,
    0x1623B9C: 0x1623B30,  # Acc_Hack_In finished
    0x1626863: 0x1626760,  # the attempt, no hand animation
}
PDA_WRITERS = {
    0x1636A97: 0x16369D0,  # destructor
    0x16374A3: 0x1637430,
    0x163BDDA: 0x163BCA0,
    0x163D115: 0x163CFF0,
    0x163D242: 0x163CFF0,
    0x163D282: 0x163CFF0,
}
# The action-name table's constructor 0x1706DA0: lea rdx,name; lea rcx,[rbx+slot].
ACTIONS = {
    0x1709C6C: ("hack_moveprompt_y", 0xA30, "xi_thumbly"),
    0x1709C7F: ("hack_moveprompt_x", 0xA38, "xi_thumblx"),
    0x1709CDE: ("hack_final1", 0xA60, "xi_x"),
    0x1709CF1: ("hack_final2", 0xA68, "xi_y"),
    0x1709D04: ("hack_final3", 0xA70, "xi_a"),
    0x1709D17: ("hack_final4", 0xA78, "xi_b"),
}


def flag_writers(image):
    """Every `mov byte [rip+flag],imm8` and `mov byte [rip+flag],r8` in .text."""
    _, start, size, raw, _ = image.section(".text")
    code = image.data[raw:raw + size]
    found = set()
    for match in re.finditer(rb"\xc6\x05", code):
        rva = start + match.start()
        if match.end() + 5 <= len(code) and image.rip_target(rva, 7, 2) == NO_WORLD_FLAG_RVA:
            found.add(rva)
    for match in re.finditer(rb"[\x40-\x47]?\x88[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", code):
        rva = start + match.start()
        length = match.end() - match.start() + 4
        if match.end() + 4 <= len(code) and image.rip_target(rva, length, length - 4) == NO_WORLD_FLAG_RVA:
            found.add(rva)
    return found


def main(argv):
    image = Image(argv[1] if len(argv) > 1 else DEFAULT_MODULE)
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    name = image.cstring(ELEMENT_NAME_RVA)
    check(name == "DanielleHacking2", f"element name is {name!r}")
    check(image.at(ELEMENT_LOOKUP_RVA, 3) == b"\x48\x8d\x15" and
          image.rip_target(ELEMENT_LOOKUP_RVA, 7, 3) == ELEMENT_NAME_RVA, "the open does not look up that name")
    source = HUD_SOURCE.read_text(encoding="utf-8")
    listed = re.search(r"kMenuElements\[\] = \{(.*?)\n\};", source, re.S)
    check(f'kHackingElement = "{name}"' in source, "HudBridge names another element")
    check(listed is not None and listed.group(1).rstrip().endswith("kHackingElement,"),
          "the hacking element is not last in kMenuElements")

    check(image.at(RENDER_FLAG_TEST_RVA, len(RENDER_FLAG_TEST)) == RENDER_FLAG_TEST,
          "CSystem::Render does not test gEnv+0x2AB there")

    writers = flag_writers(image)
    expected = set(HACKING_WRITERS) | set(PDA_WRITERS)
    check(writers == expected, f"flag writers differ: extra {sorted(map(hex, writers - expected))}, "
                               f"missing {sorted(map(hex, expected - writers))}")
    for rva in (0x1623259, 0x1623B9C, 0x1626863):
        check(image.at(rva, 2) == b"\xc6\x05" and image.at(rva + 6, 1) == b"\x01", f"{rva:#x} does not set the flag")
    for rva in (0x16238A2, 0x16238C9):
        check(image.at(rva, 3) == b"\x44\x88\x35", f"{rva:#x} does not store R14B")

    bindings = BINDINGS.read_text(encoding="utf-8")
    for rva, (action, slot, key) in ACTIONS.items():
        check(image.at(rva, 3) == b"\x48\x8d\x15" and image.cstring(image.rip_target(rva, 7, 3)) == action,
              f"{rva:#x} does not load {action}")
        check(image.at(rva + 7, 3) == b"\x48\x8d\x8b" and image.i32(rva + 10) == slot,
              f"{action} is not stored at +{slot:#x}")
        check(f"binding map=hacking action={action} key={key} " in bindings, f"{action} is not on {key}")

    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        return 1
    print(f"ok: {name} polled last; flag writers {len(HACKING_WRITERS)} hacking, {len(PDA_WRITERS)} PDA, "
          f"no others; {len(ACTIONS)} actions on the keys the mod sends")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
