"""The Steam PreyDll.dll the static verifiers here read, and its bytes by RVA.

Reads the file on disk; never a process. One copy of what each verifier used to
carry its own of: where the module is, the PE32+ header and section walk, base
relocations, imports, RIP-relative targets, the FNV-1a 64 body hash, and the
`{0xRVA,size,0xHASHull}, // Name` rows of the `*Native.h` tables.

Some verifiers keep their own copies; they reach DEFAULT_MODULE and
EXPECTED_SHA256 through verify_h005_skinning.
"""
import hashlib
import os
import re
import struct
from collections import namedtuple
from pathlib import Path

EXPECTED_SHA256 = "7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7"
_PREYDLL = Path("steamapps/common/Prey/Binaries/Danielle/x64/Release/PreyDll.dll")
_UPSTREAM_LIBRARY = Path(r"D:\SteamLibrary")


def resolve_module():
    """PREYVR_PREYDLL if set, else the first Steam library holding Prey.

    The upstream library is tried first, so a machine that has it resolves as
    before; a fork installed elsewhere is found through Steam's own library list.
    """
    override = os.environ.get("PREYVR_PREYDLL")
    if override:
        return Path(override)
    libraries = [_UPSTREAM_LIBRARY]
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
            steam = Path(winreg.QueryValueEx(key, "SteamPath")[0])
        folders = (steam / "steamapps" / "libraryfolders.vdf").read_text(encoding="utf-8", errors="replace")
        libraries += [Path(path.replace("\\\\", "\\")) for path in re.findall(r'"path"\s+"([^"]+)"', folders)]
    except (ImportError, OSError):
        pass
    for library in libraries:
        if (library / _PREYDLL).is_file():
            return library / _PREYDLL
    return _UPSTREAM_LIBRARY / _PREYDLL


DEFAULT_MODULE = resolve_module()

# Header order: raw_size and raw_offset bound what is file-backed.
Section = namedtuple("Section", "name rva raw_size raw_offset virtual_size")


class Image:
    """An x64 PE32+ image, addressed by RVA through its file-backed sections.

    Refuses any other build unless `expected_sha256` is None (a reference image
    from another store, say).
    """

    def __init__(self, path=None, expected_sha256=EXPECTED_SHA256):
        self.path = Path(path) if path is not None else DEFAULT_MODULE
        self.data = self.path.read_bytes()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        if expected_sha256 is not None and self.sha256 != expected_sha256:
            raise ValueError(f"Unsupported module SHA-256: {self.sha256}")
        u16 = lambda offset: struct.unpack_from("<H", self.data, offset)[0]
        u32 = lambda offset: struct.unpack_from("<I", self.data, offset)[0]
        pe = u32(0x3C)
        self.optional_header = opt = pe + 24
        if (self.data[:2] != b"MZ" or self.data[pe:pe + 4] != b"PE\0\0"
                or u16(pe + 4) != 0x8664 or u16(opt) != 0x20B):
            raise ValueError("Expected an x64 PE32+ image")
        self.base = struct.unpack_from("<Q", self.data, opt + 24)[0]
        self.sections = []
        for index in range(u16(pe + 6)):
            header = opt + u16(pe + 20) + 40 * index
            name = self.data[header:header + 8].rstrip(b"\0").decode("ascii", "replace")
            self.sections.append(Section(name, u32(header + 12), u32(header + 16),
                                         u32(header + 20), u32(header + 8)))

    def at(self, rva, size):
        for section in self.sections:
            if section.rva <= rva and rva + size <= section.rva + section.raw_size:
                offset = section.raw_offset + rva - section.rva
                result = self.data[offset:offset + size]
                if len(result) != size:
                    raise ValueError(f"Truncated section at RVA {rva:#x}")
                return result
        raise ValueError(f"Unmapped file-backed RVA {rva:#x}")

    def u32(self, rva):
        return struct.unpack("<I", self.at(rva, 4))[0]

    def i32(self, rva):
        return struct.unpack("<i", self.at(rva, 4))[0]

    def u64(self, rva):
        return struct.unpack("<Q", self.at(rva, 8))[0]

    def cstring(self, rva, limit=128):
        return self.at(rva, limit).split(b"\0")[0].decode()

    def section(self, name):
        return next(section for section in self.sections if section.name == name)

    def rip_target(self, rva, length, disp_offset):
        """Where the RIP-relative operand of the `length`-byte instruction at `rva` points."""
        return rva + length + self.i32(rva + disp_offset)

    def call_target(self, rva):
        """The target of the `call rel32` at `rva`, or None if it is not one."""
        return self.rip_target(rva, 5, 1) if self.at(rva, 1) == b"\xe8" else None

    def vtable_target(self, slot):
        """The function a vtable slot holds, as an RVA."""
        return self.u64(slot) - self.base

    def directory(self, index):
        """(rva, size) of data directory `index`: 1 imports, 3 exceptions, 5 relocations."""
        return struct.unpack_from("<II", self.data, self.optional_header + 112 + 8 * index)

    def relocations(self):
        """Every RVA a base relocation rewrites."""
        rva, size = self.directory(5)
        found, cursor = set(), 0
        while cursor < size:
            page, block = struct.unpack("<II", self.at(rva + cursor, 8))
            if block < 8:
                raise ValueError(f"Malformed relocation block at {rva + cursor:#x}")
            for offset in range(8, block, 2):
                entry = struct.unpack("<H", self.at(rva + cursor + offset, 2))[0]
                if entry >> 12:
                    found.add(page + (entry & 0xFFF))
            cursor += block
        return found

    def import_slots(self):
        """{slot RVA: (DLL upper case, function name)} for every import by name."""
        slots, descriptor = {}, self.directory(1)[0]
        while True:
            original, _, _, name, first = struct.unpack("<IIIII", self.at(descriptor, 20))
            if name == 0:
                return slots
            dll, thunk, index = self.cstring(name), original or first, 0
            while True:
                value = struct.unpack("<Q", self.at(thunk + 8 * index, 8))[0]
                if value == 0:
                    break
                if not value & (1 << 63):
                    slots[first + 8 * index] = (dll.upper(), self.cstring((value & 0x7FFFFFFF) + 2))
                index += 1
            descriptor += 20


def fnv1a64(data):
    """FNV-1a 64, as the DLL's complete-body contracts hash a function."""
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def native_rows(header):
    """[(rva, size, hash, name)] from a `*Native.h` contract table."""
    text = Path(header).read_text()
    return [(int(rva, 16), int(size), int(value, 16), name) for rva, size, value, name in
            re.findall(r"\{0x([0-9A-F]+),(\d+),0x([0-9A-F]+)ull\}, // (\w+)", text)]


def byte_arrays(text):
    """{name: bytes} for each `constexpr std::uint8_t kName[] = {...};` in C++ source."""
    return {name: bytes(int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})\b", body))
            for name, body in re.findall(r"constexpr std::uint8_t (k\w+)\[\] = \{(.*?)\};", text, re.S)}
