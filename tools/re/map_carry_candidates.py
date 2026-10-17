"""Generate cross-build carry leads from decoded, relocation-masked code.

Candidates are not promoted contracts. No process is opened or game launched.
"""
import hashlib, json, re
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from preydll import DEFAULT_MODULE, Image

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = ROOT / '[WIN] Prey [2021-08-19]' / 'PreyDll.dll'
ENTRIES = {
    'constructor': 0x122D6B0, 'drop': 0x122DAD0,
    'direction': 0x122DB50, 'target': 0x122DFD0,
    'start': 0x122FC50, 'stop': 0x12307F0,
    'throw': 0x1231B50, 'update': 0x1231C80,
    'populate': 0x1566EE0,
    'entity_from_physics': 0x922E30, 'script_release': 0xD122A0,
}

def main():
    # The reference is another store's build: no hash to hold it to.
    reference_image, target_image = Image(REFERENCE, expected_sha256=None), Image(DEFAULT_MODULE)
    reference, target, digest = reference_image.data, target_image.data, target_image.sha256
    ref_base = reference_image.base
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    matches = {}
    for name, rva in ENTRIES.items():
        code = reference_image.at(rva, 256)
        decoded = list(decoder.disasm(code, ref_base + rva))
        patterns = []
        for length in (96, 48, 24):
            selected = [i for i in decoded if i.address - ref_base - rva < length]
            if not selected:
                continue
            count = selected[-1].address + selected[-1].size - ref_base - rva
            mask = [False] * count
            for i in selected:
                at = i.address - ref_base - rva
                if 'rip' in i.op_str:
                    for p in range(i.disp_offset, i.disp_offset + i.disp_size):
                        mask[at+p] = True
                if i.mnemonic.startswith('j') or i.mnemonic == 'call':
                    for p in range(i.imm_offset, i.imm_offset + i.imm_size):
                        mask[at+p] = True
            expression = b''.join(b'.' if mask[i] else re.escape(bytes([code[i]])) for i in range(count))
            found = []
            for section, start, size, raw, _ in target_image.sections:
                if section != '.text':
                    continue
                for hit in re.finditer(expression, target[raw:raw+size], re.DOTALL):
                    found.append(hex(start + hit.start()))
            patterns.append({'bytes': count, 'candidates': found[:30], 'total': len(found)})
        matches[name] = {'referenceRva': hex(rva), 'patterns': patterns}
    print(json.dumps({'targetSHA256': digest, 'referenceSHA256': hashlib.sha256(reference).hexdigest(),
        'classification': 'cross-build candidates only; inspect Steam bodies and callers before use', 'matches': matches}, indent=2))

if __name__ == '__main__':
    main()
