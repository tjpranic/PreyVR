# Receipts

Research records from the project's first two months, all from phunkaeg (2026-09-09 to 2026-10-04). Each JSON file is one finding: its question, claim, evidence grade, verdict and the SHA-256 of the files behind it. They fed a shared VR modding playbook that this fork does not use. `docs/receipts/` holds one more.

**A receipt describes the commit that added it.** Its paths and hashes are history, not a rule about the current tree, and nothing in this repo reads them. To replay one, find that commit with `git log --diff-filter=A -- <receipt>` and hash the file there with `git show <commit>:<path>`. All 19 code and tool pins replayed that way when this was checked on 2026-10-31.

What protects the engine contracts today is `tools/re/run_verifiers.py` (ctest `re_verifiers`), which checks the tables against PreyDll.dll.

**Moved by the source restructure, 2026-10-31:**

| Pinned path | Now | Contents |
|---|---|---|
| `include/preyvr/EquipmentNative.h` | `src/equipment/body/EquipmentNative.h` | Unchanged |
| `include/preyvr/NativeWristNative.h` | `src/ui/hud/NativeWristNative.h` | Unchanged |
| `include/preyvr/PsiMedkitNative.h` | `src/equipment/psi/PsiMedkitNative.h` | Unchanged |
| `include/preyvr/PsychoscopeNative.h` | `src/equipment/psi/PsychoscopeNative.h` | Unchanged |
| `src/dll/NativeHudValue.h` | `src/ui/hud/NativeHudValue.h` | One include line |
| `tools/re/verify_body_equipment.py` | Not moved | One path line |
| `tools/re/verify_native_wrist.py` | Not moved | One path line |
| `tools/re/verify_psychoscope.py` | Not moved | One path line |

The five other pinned tools and tests are untouched and still match their receipts. Six more code pins had already gone stale through ordinary edits after their receipts.
