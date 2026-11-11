"""Offline Steam native wrist contract verification. Never opens a process."""
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
from verify_h005_skinning import DEFAULT_MODULE, EXPECTED_SHA256

ROOT = Path(__file__).resolve().parents[2]


def verify():
    data = DEFAULT_MODULE.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    assert digest == EXPECTED_SHA256, 'unsupported Steam DLL'
    u16 = lambda p: struct.unpack_from('<H', data, p)[0]
    u32 = lambda p: struct.unpack_from('<I', data, p)[0]
    pe = u32(0x3c)
    assert u16(pe+4) == 0x8664 and u16(pe+24) == 0x20b
    sections = []
    for n in range(u16(pe+6)):
        off = pe+24+u16(pe+20)+n*40
        sections.append((u32(off+12), u32(off+16), u32(off+20)))

    def at(rva, count):
        for va, size, raw in sections:
            if va <= rva and rva+count <= va+size:
                return data[raw+rva-va:raw+rva-va+count]
        raise ValueError(hex(rva))

    def fnv(block):
        h = 14695981039346656037
        for byte in block:
            h = ((h ^ byte)*1099511628211) & ((1 << 64)-1)
        return h

    text = (ROOT/'src/ui/hud/NativeWristNative.h').read_text()
    row = r'\{0x([0-9A-F]+),(\d+),0x([0-9A-F]+)ull\}, // (\w+)'
    def table(name):
        block = re.search(r'inline constexpr Contract ' + name + r'\[\]=\{(.*?)\};', text, re.S)
        assert block, name
        return re.findall(row, block.group(1))
    entries = table('Contracts')
    psi_entries = table('PsiShownContracts')
    assert len(entries) == 9 and len(psi_entries) == 3
    assert len(re.findall(row, text)) == 12, 'every row is in one of the two tables'
    entries += psi_entries
    checks = []
    for rva, size, expected, name in entries:
        body = at(int(rva, 16), int(size))
        assert fnv(body) == int(expected, 16), name
        assert fnv(bytes([body[0] ^ 1])+body[1:]) != int(expected, 16), name+' negative control'
        checks.append(name+' complete bytes and mutation refusal')
    for slot, rva in [(0x1EB57E8, 0x18BABA0), (0x1EB5898, 0x18BF0C0),
                      (0x1EB57A8, 0x18B8ED0), (0x1EB4BE8, 0x18ABE10)]:
        assert struct.unpack('<Q', at(slot, 8))[0] == 0x180000000+rva
    assert at(0x18B8ED0, 8).hex() == '488b8118010000c3'
    checks.append('concrete root/sprite vtables and sprite owner accessor')

    # The equipped psi power on the wrist (NativeHudIsolation.h, 2026-11-08):
    # PreyDll opens and closes power_mc only on whether a power is equipped,
    # the index at +0x234, 15 for none -- never on a timer.
    def names(site):
        assert at(site, 3) == bytes.fromhex('488d15'), hex(site)
        target = site + 7 + struct.unpack('<i', at(site + 3, 4))[0]
        return at(target, 16).split(b'\0')[0].decode('ascii')
    assert at(0x15B63A6, 10).hex() == 'c781340200000f000000' and names(0x15B63E2) == 'powerClose'
    assert at(0x15BCC7A, 7).hex() == '486383340200' + '00' and at(0x15BCC89, 3).hex() == '83f80f'
    assert names(0x15BCC9C) == 'powerClose'
    assert at(0x15B7915, 7).hex() == '83be340200000f' and names(0x15B796A) == 'powerOpen'
    checks.append('power_mc: powerClose on setting the equipped index to none (15) or finding none, '
                  'powerOpen on equipping one from none')

    # Whether the wrist shows psi_mc (NativeWristNative.h PsiShown, 2026-11-11).
    def call(site):
        assert at(site, 1) == bytes([0xE8]), hex(site)
        return site + 5 + struct.unpack('<i', at(site + 1, 4))[0]
    # The predicate: the owner's byte +0x39A, else 0x15B8140's list of the power
    # component (owner + 8) is non-empty.
    assert at(0x15AD11A, 7).hex() == '80b99a03000000' and at(0x15AD127, 4).hex() == '4883c108'
    assert call(0x15AD130) == 0x15B8140
    # The list: the 16 slots at +0x40..+0xC0, each kept when its vtable +0x18 is above -1.
    assert at(0x15B8151, 4).hex() == '488d5940' and at(0x15B8155, 7).hex() == '488db380000000'
    assert at(0x15B8176, 8).hex() == 'ff5018' + '83f8ff' + '7e1d'
    checks.append('PsiShown 0x15AD110: owner +0x39A, or a learned slot among the 16 at power +0x40 (0x15B8140)')
    # The HUD refresh that opens the meters together, and the psi refresh: each
    # asks the predicate of *(player + 0x678) and sends psiOpen only when it holds.
    assert call(0x1726BD4) == 0x15AD110 and at(0x1726BB7, 7).hex() == '4c8db078060000'
    assert names(0x1726CE5) == 'psiOpen' and names(0x1726CF1) == 'psiClose'
    assert at(0x1726CDB, 3).hex() == '4584ed' and at(0x1726CE3, 2).hex() == '740c'
    assert call(0x15D855C) == 0x157C990 and at(0x15D8561, 7).hex() == '488d8878060000'
    assert call(0x15D8568) == 0x10839F0 and call(0x15D8570) == 0x15AD110
    assert at(0x15D8585, 5).hex() == '4084ff' + '743c'
    assert names(0x15D85B5) == 'psiOpen' and names(0x15D85C9) == 'psiClose'
    checks.append('psiOpen at 0x1726CE5 and 0x15D85B5 only while PsiShown(*(player + 0x678)) holds')
    return dict(target_sha256=digest, checks=checks, game_launched=False,
                limit='Byte identity and inspected static contracts, not native replay execution or headset acceptance.')


if __name__ == '__main__':
    output = json.dumps(verify(), indent=2)+'\n'
    if len(sys.argv) > 1:
        Path(sys.argv[1]).write_text(output, encoding='utf-8', newline='\n')
    print(output)
