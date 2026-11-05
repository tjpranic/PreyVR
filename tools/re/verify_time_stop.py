"""Read-only Steam time-stop contracts: why the player's hands and body freeze while Prey stops the player's clock
(CAnimatedCharacter::UpdateTime leaves the player's frame time at 0, and PrePhysicsUpdate then returns before
PrepareAndStartAnimProc), and what the hook relies on (PrePhysicsUpdate's one caller and its test, the start's own
guards, the character's own clock). docs/TIME-STOP-SCOPE-2026-11-05.md. Never attaches.

Usage: verify_time_stop.py [report.json]
"""
import json
import re
import struct
import sys
from pathlib import Path

from preydll import DEFAULT_MODULE, Image, fnv1a64, native_rows

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'src/engine/fixes/TimeStopAnimationNative.h'
PRE, START, UPDATE, PLAYER = 0x4F8570, 0x4F9A80, 0x4FDD10, 0x157C990
CHARACTER_VTABLE, TIMER_VTABLE = 0x1D22200, 0x1DA6D08
ENTITY_SYSTEM, TIMER = 0x224DA38, 0x224DA18


def header_constant(name):
    """`inline constexpr ... name=0x...` from TimeStopAnimationNative.h."""
    text = HEADER.read_text(encoding='utf-8')
    for part in text.replace(';', ',').split(','):
        part = part.strip()
        if '=' in part and part.split('=')[0].split()[-1:] == [name]:
            return int(part.split('=')[1].strip(), 0)
    raise KeyError(name)


def f32(image, rva):
    return struct.unpack('<f', image.at(rva, 4))[0]


def rel32_callers(image, target):
    """Every call/jmp rel32 in .text that lands on target."""
    _, start, size, raw, _ = image.section('.text')
    code = image.data[raw:raw + size]
    found = []
    for match in re.finditer(rb'[\xe8\xe9]', code):
        at = match.start()
        if at + 5 <= len(code) and start + at + 5 + struct.unpack_from('<i', code, at + 1)[0] == target:
            found.append(start + at)
    return found


def verify():
    image = Image(DEFAULT_MODULE)
    at = image.at
    checks = []

    rows = native_rows(HEADER)
    assert [rva for rva, _, _, _ in rows] == [PRE, START, UPDATE, PLAYER], rows
    relocs = image.relocations()
    for rva, size, expected, name in rows:
        body = at(rva, size)
        assert fnv1a64(body) == expected, name
        assert not any(rva <= p < rva + size for p in relocs), name + ' relocates in memory'
        corrupt = bytearray(body)
        corrupt[len(body) // 2] ^= 1
        assert fnv1a64(corrupt) != expected, name + ' mutation control'
        if rva != PLAYER:
            assert at(rva + size, 1) == b'\xcc', name + ' is the whole function'
    checks.append('PrePhysicsUpdate, PrepareAndStartAnimProc, UpdateTime: complete bodies, no base relocations, '
                  'mutation refused')
    shared = [row for row in native_rows(ROOT / 'src/controls/input/StickRereadNative.h') if row[0] == PLAYER]
    assert shared and shared[0][:3] == rows[3][:3]
    checks.append("GetArkPlayerInstance: the row StickRereadNative.h already gates")
    for name, value in {'PrePhysicsUpdate': PRE, 'StartAnimation': START, 'UpdateTime': UPDATE,
                        'FrameTime': 0x1D0, 'Location': 0x21C, 'StartsOnTransform': 0x844}.items():
        assert header_constant(name) == value, name
    checks.append("TimeStopAnimationNative.h's constants are the ones witnessed below")

    def witness(group, table):
        for name, (rva, code) in table.items():
            expected = bytes.fromhex(code)
            assert at(rva, len(expected)) == expected, group + ': ' + name
            checks.append(group + ': ' + name)

    # The hook: one direct caller, no vtable holding it, and a prologue MinHook can move.
    assert rel32_callers(image, PRE) == [0x2C3073], [hex(x) for x in rel32_callers(image, PRE)]
    for section in ('.rdata', '.data'):
        s = image.section(section)
        assert struct.pack('<Q', image.base + PRE) not in at(s.rva, s.raw_size), section
    checks.append('PrePhysicsUpdate: called only from 0x2C3073 (the game object pre-physics update), held by no table')
    witness('PrePhysicsUpdate 0x4F8570', {
        'entry: push rbx; sub rsp,0xF0; xorps; mov rbx,rcx (no RIP-relative bytes)': (0x4F8570, '40534881ecf00000000f57c0488bd9'),
        'comisd 0,[rcx+0x1D0]; jnc: a frame time of 0 or less leaves': (0x4F857F, '660f2f81d00100000f83cb010000'),
        'the epilogue it leaves by': (0x4F8758, '4881c4f00000005bc3'),
        'only while +0x844 is 0, PrepareAndStartAnimProc(this, this+0x21C)':
            (0x4F872A, '80bb4408000000898360010000750f488d931c020000488bcbe838130000'),
    })
    assert image.rip_target(0x4F8587, 6, 2) == 0x4F8758 and image.call_target(0x4F8743) == START
    assert rel32_callers(image, START) == [0x4F8743, 0x4F89FD], [hex(x) for x in rel32_callers(image, START)]
    checks.append('the test jumps past the start; the start is called there and from the transform event (0x4F89FD) only')

    witness('UpdateTime 0x4FDD10', {
        'the player? (entity system +0x338, of the entity at +0x38)': (0x4FDD22, '488b0d0ffdd401488b5738488b01ff9038030000'),
        'timer 3 for the player, else 1, through GetFrameTime (+0x38)':
            (0x4FDD36, '488b0ddbfcd401ba010000000fb6d8b80300000084db4c8b010f45d041ff5038'),
        'the player skips the substitute (test bl,bl; jnz)': (0x4FDD68, '84db75110f2f05e14678017708f30f10350b2f7801'),
        'the frame time stored at +0x1D0': (0x4FDDCB, 'f20f1187d0010000'),
    })
    assert image.rip_target(0x4FDD22, 7, 3) == ENTITY_SYSTEM and image.rip_target(0x4FDD36, 7, 3) == TIMER
    floor, substitute = image.rip_target(0x4FDD6C, 7, 3), image.rip_target(0x4FDD75, 8, 4)
    assert struct.unpack('<I', at(floor, 4))[0] == 0x80000000 and abs(f32(image, substitute) - 1e-4) < 1e-9
    checks.append('UpdateTime: any other character at 0 or less gets 0.0001 s; the player keeps 0')

    witness('PrepareAndStartAnimProc 0x4F9A80', {
        'a character (+0x710), not yet this frame (+0x840), Prey\'s own flag (+0x847)':
            (0x4F9AA7, '4883bf10070000008bf00f845c0100003987400800000f845001000080bf4708000000'),
        'StartAnimationProcessing (+0x90) on the shadow, then the character; true, and this frame':
            (0x4F9BD5, '488b01488d542430ff9090000000488b8f10070000488d542430488b01ff9090000000b00189b740080000'),
        'false otherwise': (0x4F9C18, '32c0'),
    })
    overrides = [rva for rva in range(START, START + 424 - 8)
                 if at(rva, 3) == b'\xf3\x0f\x10' and image.rip_target(rva, 8, 4) == 0x1C7EB40]
    assert overrides and f32(image, 0x1C7EB40) == -1.0
    checks.append('PrepareAndStartAnimProc: no override delta (-1), so the character\'s own clock gives it')

    assert image.vtable_target(CHARACTER_VTABLE + 0x90) == 0x82F910
    assert image.vtable_target(CHARACTER_VTABLE + 0x1C0) == 0x82EC90
    witness('the character\'s clock', {
        'StartAnimationProcessing: GetFrameTime of the character\'s timer (+0xAE0)':
            (0x82FAEC, '488b0d25dfa1010fb693e00a0000488b01ff5038'),
        'a new character is on timer 1': (0x82BF28, '41c686e00a000001'),
        'the setter, character vtable +0x1C0': (0x82EC90, '8891e00a0000c3'),
        'ArkPlayer: its character, slot 0 (entity vtable +0x2D8)': (0x157FDE0, '498b042433d2498bccff90d8020000'),
        'ArkPlayer: puts it on timer 3': (0x157FE1C, '4c8b00b203488bc841ff90c0010000'),
    })
    assert image.rip_target(0x82FAEC, 7, 3) == TIMER
    checks.append('the player\'s character runs on timer 3, so its animation delta is 0 while that clock is')

    assert image.rip_target(0xE142F5, 7, 3) == TIMER_VTABLE and image.vtable_target(TIMER_VTABLE + 0x38) == 0xE14650
    witness('CTimer', {
        'GetFrameTime: 0 while the timer is paused (+0x1C4), else its frame time (+0x1C), 0x1C0 a timer':
            (0xE14650, '80b92807000000741b0fb6c24869d0c001000080bc0ac4010000007507f30f10440a1cc30f57c0c3'),
        'SetTimeScale: a positive scale is at least the floor': (0xE14BE1, '0f2fd77608f30f5f156a0fe700'),
    })
    assert abs(f32(image, image.rip_target(0xE14BE6, 8, 4)) - 0.3) < 1e-6
    checks.append('CTimer: slowed means 0.3 or more, so only an exact 0 stops the player\'s animation')
    return {'target': str(DEFAULT_MODULE), 'sha256': image.sha256, 'environment': 'static', 'game_launched': False,
            'checks': checks,
            'limits': "Proves native bytes and call routes. Not static facts: the player's +0x844 at runtime (A's path "
                      "needs 0), the time-scale stats' values (encrypted paks), and the hook in a running game or how "
                      "the hands look in a headset."}


if __name__ == '__main__':
    result = verify()
    if len(sys.argv) > 1:
        Path(sys.argv[1]).write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(f"time stop verified: {len(result['checks'])} checks; sha256 {result['sha256']}")
