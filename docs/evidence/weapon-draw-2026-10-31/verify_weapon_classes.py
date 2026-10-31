"""Offline proof of what docs/WEAPON-DRAW-SCOPE-2026-10-31.md says about every weapon class.

Reads the Steam PreyDll.dll on disk; never a process. It checks whether the wrench's draw and
stow skip can stand for every CArkWeapon, so tools/re/verify_wrench_draw.py can take these rows
when the lane is generalized. Run with `python -E -B`.
"""
import bisect
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "re"))
from preydll import Image  # noqa: E402

ON_EQUIP, ON_UNEQUIP = 0x1699800, 0x169A000
SELECT_BEGUN, SELECT_DONE, SELECT_FINISHED = 0x1692E60, 0x1692E70, 0x1692F50
NO_OP = 0x1706520
SELECT_ACTION, STOW_ACTION = 0x1E6D698, 0x1E6D770
EVENT_HANDLER = 0x169DD20          # Throw, Deploy, Reload, InterruptFire, GlowEnd, MuzzleFlash
EVENT_ACTIONS = (0x1E6DA90, 0x1E6E300)

# name, vtable, factory, create; then the five slots as they stand in the binary.
CLASSES = [
    ("ArkWeapon", 0x1E6C640, 0x1E95770, 0x172F800, ON_EQUIP, ON_UNEQUIP, SELECT_BEGUN, SELECT_DONE, SELECT_FINISHED),
    ("ArkWeaponDiscRifle", 0x1E308D0, 0x1E95788, 0x172F880, 0x13B7A10, 0x13B7B60, SELECT_BEGUN, SELECT_DONE, SELECT_FINISHED),
    ("ArkWeaponStunGun", 0x1E31290, 0x1E957D0, 0x172FB00, 0x13BB1D0, 0x13BC2B0, NO_OP, 0x13B9E80, SELECT_FINISHED),
    ("ArkWeaponGooGun", 0x1E6E390, 0x1E956C8, 0x172F900, 0x169F3F0, 0x169F6C0, SELECT_BEGUN, SELECT_DONE, SELECT_FINISHED),
    ("ArkWeaponInstalaser", 0x1E6E9F0, 0x1E95728, 0x172FA00, 0x16A29A0, 0x16A3530, SELECT_BEGUN, SELECT_DONE, 0x16A07A0),
    ("grenades (4 names)", 0x1E6F148, 0x1E957B8, 0x172F980, 0x16A5DE0, 0x16A5EA0, SELECT_BEGUN, 0x16A5300, SELECT_FINISHED),
    ("Pistol/Shotgun/ToyGun", 0x1E6F780, 0x1E95758, 0x172FA80, 0x16AA8C0, 0x16AAB20, SELECT_BEGUN, SELECT_DONE, SELECT_FINISHED),
    ("ArkWeaponDoubleWrench", 0x1E92A08, 0x1E957A0, 0x172F480, ON_EQUIP, 0x17392D0, SELECT_BEGUN, SELECT_DONE, SELECT_FINISHED),
    ("ArkWeaponWrench", 0x1E92F00, 0x1E957E8, 0x172F5E0, ON_EQUIP, 0x17392F0, SELECT_BEGUN, SELECT_DONE, SELECT_FINISHED),
]
NAMES = {"ArkWeapon", "ArkWeaponDiscRifle", "ArkWeaponDoubleWrench", "ArkWeaponEMPGrenade", "ArkWeaponGooGun",
         "ArkWeaponInstalaser", "ArkWeaponLureGrenade", "ArkWeaponNullwaveTransmitter", "ArkWeaponPistol",
         "ArkWeaponRecyclerGrenade", "ArkWeaponShotgun", "ArkWeaponStunGun", "ArkWeaponToyGun", "ArkWeaponWrench"}

img = Image()
text = img.section(".text")
code = img.data[text.raw_offset:text.raw_offset + text.raw_size]
pdata = img.section(".pdata")
raw = img.data[pdata.raw_offset:pdata.raw_offset + pdata.raw_size]
rows = sorted({struct.unpack_from("<III", raw, i) for i in range(0, len(raw) - 11, 12)})
starts = [row[0] for row in rows]
leas, calls, jumps = {}, {}, {}
for match in re.finditer(rb"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", code):
    at = match.start()
    leas.setdefault(text.rva + at + 7 + struct.unpack_from("<i", code, at + 3)[0], []).append(text.rva + at)
for match in re.finditer(rb"[\xe8\xe9]", code):
    at = match.start()
    table = calls if code[at] == 0xE8 else jumps
    table.setdefault(text.rva + at + 5 + struct.unpack_from("<i", code, at + 1)[0], []).append(text.rva + at)

checks = failures = 0


def check(ok, what):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("FAIL", what)


def enclosing(rva):
    return rows[bisect.bisect_right(starts, rva) - 1]


def within(sites, entry, limit=0x100):
    return any(entry <= site < entry + limit for site in sites)


def slot(table, offset):
    return img.vtable_target(table + offset)


# Every name the registration function 0x17327F0 gives a class, and the class each name reaches.
registered = set()
for rva, sites in leas.items():
    if not any(enclosing(site)[0] == 0x17327F0 for site in sites):
        continue
    try:
        name = img.cstring(rva, 48)
    except (ValueError, UnicodeDecodeError):
        continue
    if name.startswith("ArkWeapon") and name.isidentifier() and name in NAMES:
        registered.add(name)
check(registered == NAMES, f"registered names {sorted(NAMES ^ registered)}")

for name, vtable, factory, create, equip, unequip, begun, done, finished in CLASSES:
    check(slot(factory, 8) == create, f"{name}: factory {factory:#x}+8 is create {create:#x}")
    check(bool(leas.get(factory)), f"{name}: factory referenced")
    check(enclosing(leas[vtable][0])[0] is not None and bool(leas.get(vtable)), f"{name}: vtable written by a ctor")
    for offset, wanted in ((0x08, equip), (0x10, unequip), (0x100, begun), (0x108, done), (0x110, finished)):
        check(slot(vtable, offset) == wanted, f"{name} +{offset:#x} is {wanted:#x}, found {slot(vtable, offset):#x}")
    # A class's own OnEquip and OnUnequip go on to the shared ones.
    if equip != ON_EQUIP:
        check(within(calls.get(ON_EQUIP, []), equip), f"{name}: {equip:#x} calls the shared OnEquip")
    if unequip != ON_UNEQUIP:
        # Two of them reach it by a tail jump.
        check(within(calls.get(ON_UNEQUIP, []) + jumps.get(ON_UNEQUIP, []), unequip, 0x120),
              f"{name}: {unequip:#x} goes on to the shared OnUnequip")
    # A class's own finish and done go on to the shared ones.
    if finished != SELECT_FINISHED:
        check(finished in (0x16A07A0,) and code[finished + 7 - text.rva] == 0xE9
              and finished + 12 + struct.unpack_from("<i", code, finished + 8 - text.rva)[0] == SELECT_FINISHED,
              f"{name}: {finished:#x} clears +0x584 and jumps to SelectFinished")
    if done != SELECT_DONE:
        check(within(calls.get(SELECT_DONE, []), done, 0x20), f"{name}: {done:#x} calls SelectDone")

# All six OnEquip wrappers and nothing else; the select action has one builder.
check(len(calls[ON_EQUIP]) == 6, "OnEquip has six direct callers")
check(len(calls[0x169D6F0]) == 1 and enclosing(calls[0x169D6F0][0])[0] in (0x1699800, 0x169988D),
      "the select action is built by OnEquip alone")
check(len(calls[0x169D790]) == 1, "the plain stow action has one builder")

# The Q-Beam's busy byte: set by its OnEquip, cleared by its own finish.
check(img.at(0x16A07A0, 7) == bytes.fromhex("C6 81 84 05 00 00 00"), "Q-Beam +0x110 clears +0x584")

# The select and stow actions take no animation events: the handler sits in two other actions.
for table in EVENT_ACTIONS:
    # A vtable starts where a constructor's lea points.
    start = table
    while not leas.get(start) and table - start < 0x100:
        start -= 8
    check(bool(leas.get(start)), f"the vtable holding {table:#x} found")
    event_slot = table - start
    print(f"event handler at vtable {start:#x} slot +{event_slot:#x}")
    for action in (SELECT_ACTION, STOW_ACTION):
        check(slot(action, event_slot) != EVENT_HANDLER, f"{action:#x}+{event_slot:#x} is not the event handler")
        check(slot(action, event_slot) == NO_OP, f"{action:#x}+{event_slot:#x} is a no-op")
hits = [offset for offset in range(0, 0xD8, 8) for action in (SELECT_ACTION, STOW_ACTION)
        if slot(action, offset) == EVENT_HANDLER]
check(not hits, "neither action holds the event handler in any slot")

# PlayAction's float is a playback speed: OnEquip and OnUnequip pass 1.0, reload passes a stat.
check(struct.unpack("<f", img.at(0x1C7EB28, 4))[0] == 1.0, "DAT_181C7EB28 is 1.0f")

print(f"{checks} checks, {failures} failed")
sys.exit(1 if failures else 0)
