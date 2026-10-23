# Tests

These are deterministic tests that need no running game and no headset. `ctest` runs 93 of them (as of 2026-10-23).

**What they cover:**
- **Pure logic:** tests over `preyvr_core` (`src/common`).
- **DLL sources:** these tests `#include` a `src/dll/*.cpp`, or list it in `SOURCES`, and stub its engine, input and hook boundaries.
  - They are registered with `preyvr_add_test(... DLL ...)`, which gives them the DLL's lifecycle include and compile defines.
  - They call file-local names in those modules, so renaming inside a module can break its test.
- **Fixtures and replays:** the captured aim, wrench-query and interaction-query fixtures, and the offline VR scheme replays in `tools/re/vr_scheme_review`.
- **Fail-closed load:** `dll_load_fail_closed` and `smoke_log_parser` load the real DLL in an isolated host. With no Prey present, it must refuse and log that it did.
- **Scripts:** the PowerShell launcher suites (`Launcher*.Tests.ps1`), and `build_doctor`, which checks the installed Prey build when one is present.

**Shared helpers:** `TestSupport.h` (checks; it has no project includes) and `TestGeometry.h`.

**Not in ctest:** `test_extract_native_hud.py`. The receipt-pinned extractor it tests needs Python 3.11+ (`hashlib.file_digest`) and pycryptodome.

**Running them:** `./tools/Run-Headless.ps1` runs the whole loop: configure, build, then ctest. Runtime validation is described in `docs/TEST_PLAN.md`.
