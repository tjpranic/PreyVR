"""Check H-021 instruction/vtable receipts against the supported disk image.

No process access, injection, or execution of game code. This verifies binary
landmarks, not runtime reachability or successful hand/weapon tracking.
"""
import json
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from preydll import DEFAULT_MODULE, Image


def verify(path, spec_name="h021_static_landmarks.json"):
    spec = json.loads(Path(__file__).with_name(spec_name).read_text())
    image = Image(path, spec["image_sha256"])
    digest, base, at = image.sha256, image.base, image.at

    checks = []
    for name, (rva, hexbytes) in spec["anchors"].items():
        expected = bytes.fromhex(hexbytes)
        if at(rva, len(expected)) != expected:
            raise ValueError(f"Instruction mismatch: {name} at {rva:#x}")
        checks.append(name)
    for name, (slot, target) in spec["vtables"].items():
        if struct.unpack("<Q", at(slot, 8))[0] != base + target:
            raise ValueError(f"Vtable mismatch: {name} at {slot:#x}")
        checks.append(name)
    return {"module": str(path), "sha256": digest,
            "static_checks_passed": len(checks), "checks": checks,
            "runtime_tested": False}


if __name__ == "__main__":
    try:
        print(json.dumps(verify(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_MODULE), indent=2))
    except (OSError, ValueError, struct.error) as error:
        print(json.dumps({"error": str(error), "runtime_tested": False}), file=sys.stderr)
        sys.exit(1)
