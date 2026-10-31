# Tests

These are deterministic tests that need no running game and no headset. `ctest` runs 117 of them (as of 2026-10-31).

C++ tests sit in a folder per category, the same categories as [`src/`](../src/README.md). The launcher suites and the shared helpers stay here at the top.

**What they cover:**
- **Pure logic:** tests over `preyvr_core`.
- **DLL sources:** these tests `#include` a DLL `.cpp` from `src/`, or list it in `SOURCES`, and stub its engine, input and hook boundaries.
  - They are registered with `preyvr_add_test(... DLL ...)`, which gives them the include root and compile defines.
  - They call file-local names in those modules, so renaming inside a module can break its test.
- **CommandChannel:** `command_channel` links the DLL's own objects (`preyvr_dll_objects`) and pins the reply and counting of about 300 command lines. Verbs that would start a session, a thread or a hook are excluded; the test lists them.
- **Replays:** the offline VR scheme replays in `tools/re/vr_scheme_review`.
- **Layout:** `pure_boundary` fails when pure code reaches DLL code or a platform header ([src/README.md](../src/README.md)).
- **Fail-closed load:** `dll_load_fail_closed` and `smoke_log_parser` load the real DLL in an isolated host. With no Prey present, it must refuse and log that it did.
- **Scripts:** the PowerShell launcher suites (`Launcher*.Tests.ps1`), and `build_doctor`, which checks the installed Prey build when one is present.

**Shared helpers:**
- `TestSupport.h`: checks; it has no project includes.
- `TestGeometry.h`.
- `DllTestSupport.h`, for DLL-source tests: the captured log with `Logged`/`Count`, and the `PREYVR_TEST_CAPTURE_LOG()`, `PREYVR_TEST_DROP_LOG()` and `PREYVR_TEST_STUB_MINHOOK()` stubs.

**Not in ctest:** `test_extract_native_hud.py`. The receipt-pinned extractor it tests needs Python 3.11+ (`hashlib.file_digest`) and pycryptodome.

**Running them:** `./tools/Run-Headless.ps1` runs the whole loop: configure, build, then ctest. Runtime validation is described in `docs/TEST_PLAN.md`.
