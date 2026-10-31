"""Runs every verify_*.py here against the installed PreyDll.dll (ctest re_verifiers).

The verifiers read the game's file on disk, never a process (preydll.py says
which file). Each runs in its own interpreter from the repository root, as it
is run by hand; none is edited or imported.

Exit 0 when every verifier that could run passed, 1 when one failed, and 77
(the test's skip code) when there is no PreyDll.dll of the supported build. A
verifier that needs a Python module this machine lacks is skipped by name.
"""
import hashlib
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SKIP = 77
sys.path.insert(0, str(HERE))
from preydll import DEFAULT_MODULE, EXPECTED_SHA256  # noqa: E402


def main():
    if not DEFAULT_MODULE.is_file():
        print(f're_verifiers: skipped, no PreyDll.dll at {DEFAULT_MODULE} (PREYVR_PREYDLL names another)')
        return SKIP
    digest = hashlib.sha256(DEFAULT_MODULE.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        print(f're_verifiers: skipped, {DEFAULT_MODULE} is another build (SHA-256 {digest})')
        return SKIP
    print(f're_verifiers: {DEFAULT_MODULE}')
    passed, failed, skipped = [], [], []
    for script in sorted(HERE.glob('verify_*.py')):
        start = time.monotonic()
        try:
            run = subprocess.run([sys.executable, str(script)], cwd=ROOT, capture_output=True, text=True,
                                 timeout=300)
        except subprocess.TimeoutExpired:
            failed.append(script.name)
            print(f'FAIL {script.name}: still running after 300 s')
            continue
        missing = re.search(r"ModuleNotFoundError: No module named '([^']+)'", run.stderr)
        if run.returncode != 0 and missing:
            skipped.append(script.name)
            print(f'skip {script.name}: needs the {missing.group(1)} module')
        elif run.returncode != 0:
            failed.append(script.name)
            print(f'FAIL {script.name} (exit {run.returncode}):\n{(run.stdout + run.stderr)[-3000:]}')
        else:
            passed.append(script.name)
            print(f'ok   {script.name} ({time.monotonic() - start:.1f} s)')
    print(f're_verifiers: {len(passed)} passed, {len(failed)} failed, {len(skipped)} skipped')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
