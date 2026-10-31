"""Keeps pure code pure (ctest pure_boundary).

Pure code and DLL code share folders under src/, so nothing about a path keeps
them apart; this does. A file is DLL code when CMakeLists.txt lists it in a DLL
target or it declares namespace preyvr::dll or preyvr::lifecycle. Everything
else under src/ is pure: it must build into preyvr_core or be header-only, it
must include only pure files, and it must not reach Windows, D3D11, OpenXR or
MinHook, so it can be tested without the game.

Exit 0 when that holds, 1 with one line per violation when it does not.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'src'
DLL_NAMESPACE = re.compile(r'^\s*namespace\s+preyvr::(?:dll|lifecycle)\b', re.M)
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^">]+)[">]', re.M)
PLATFORM = re.compile(r'^(?:windows|winnt|winternl|psapi|shlobj|wrl|intrin|d3d|dxgi|openxr|minhook|imgui)', re.I)


def target_sources(cmake, target):
    block = re.search(rf'add_library\({target}\s+\w+\s+(.*?)\)', cmake, re.S)
    if not block:
        raise SystemExit(f'pure_boundary result=fail reason=no_target target={target}')
    return {ROOT / word for word in block[1].split() if word.startswith('src/')}


def main():
    cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
    core = target_sources(cmake, 'preyvr_core')
    listed_dll = target_sources(cmake, 'preyvr_dll_objects') | target_sources(cmake, 'preyvr_win32_bootstrap')
    files = {path: path.read_text(encoding='utf-8', errors='replace')
             for path in SRC.rglob('*') if path.suffix in ('.h', '.cpp')}
    dll = {path for path, text in files.items() if path in listed_dll or DLL_NAMESPACE.search(text)}
    pure = set(files) - dll

    problems = [f'{path.relative_to(ROOT).as_posix()}: in preyvr_core but it is DLL code' for path in core & dll]
    for path in sorted(pure):
        name = path.relative_to(ROOT).as_posix()
        if path.suffix == '.cpp' and path not in core:
            problems.append(f'{name}: pure source outside preyvr_core')
        for kind, include in INCLUDE.findall(files[path]):
            if kind == '<' and PLATFORM.match(include):
                problems.append(f'{name}: includes the platform header <{include}>')
            elif kind == '"' and SRC / include not in pure:
                reason = 'DLL file' if SRC / include in dll else 'file outside src/'
                problems.append(f'{name}: includes the {reason} "{include}"')

    for problem in problems:
        print(problem)
    print(f'pure_boundary result={"fail" if problems else "pass"} pure={len(pure)} dll={len(dll)}')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
