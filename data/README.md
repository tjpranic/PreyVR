# Data

Recorded results, kept apart from the notes in [`docs/`](../docs/) that interpret them. Nothing here is built or shipped.

| Folder | Use it for | In Git |
| --- | --- | --- |
| `evidence/` | The files behind one investigation: what makes a claim in `docs/` checkable. | Yes, except binary payloads. |
| `captures/` | Raw output from test runs: large, generated, and mostly local. | No, except small manifests added on purpose. |

## Evidence

Each folder backs one investigation and is named `<topic>-<date>`, for example `free-swing-wrench-2026-10-18/`. A few write the date as `YYYYMMDD`. The document in `docs/` that makes the claim links to its folder.

**What a folder holds:** decompiles and disassembly, byte and identity JSON, verifier output, build and CTest logs, run logs, and the scripts that produced them. `native-hud-2026-09-27/` and `native-wrist-2026-09-28/` have their own `README.md` describing that dataset. `vee-viewmodel-review-2026-09-09/upstream/` is a third-party copy.

**Evidence is frozen.** A folder records what was seen on that date, so do not tidy or regenerate it. Add a new folder instead.
- `.gitattributes` keeps several folders byte-exact across Windows checkouts. If a new folder has to stay byte-exact, add a rule there.
- Run logs and state dumps still name `D:\Dev Debug\PreyVR\docs\evidence\...`. That is where those runs wrote at the time, and they keep those paths on purpose.
- Scripts here are the investigation's own record. Several hard-code the checkout they were written in, so read one before running it. Checks that matter long term move to `tools/re/verify_*.py`, which ctest runs.

**What Git leaves out.** `.gitignore` keeps the text and drops the payloads: images and zips under `evidence/`, and crash dumps (`*.dmp`), xr-tape recordings (`*.ndjson`, `private-live-tape*/`) and raw texture dumps (`*.bgra`, `*.rgba`) anywhere. A few images are tracked anyway. The rest exist only on the machine that made them, so a link from a doc to one of them can be dead.

**Adding evidence:**
1. Create `data/evidence/<topic>-<YYYY-MM-DD>/`.
2. Keep the text that makes the claim checkable, and leave binaries out.
3. Say what the evidence does not prove. Byte checks and offline tests do not show that something works in a running game or looks right in a headset.
4. Link it from the document. From `docs/`, the link starts `../data/evidence/`.

## Captures

This folder is for large or generated output from test runs, and `.gitignore` excludes all of it. Preserve small textual capture manifests when they support a documented finding: add them with `git add -f`. The reports in `traces/` are tracked.

| Folder | Contents |
| --- | --- |
| `renderdoc/` | `.rdc` captures plus a sidecar markdown/json manifest. |
| `eyes/` | Final-eye, private-target, and depth visualization dumps. |
| `screenshots/` | Annotated flat/VR comparison screenshots. |
| `traces/` | Bounded API/input/timing traces. |
| `logs/` | Per-run mod logs and crash evidence. |

Use a timestamped test identifier in every filename, for example `2026-07-30T2215Z-flat-bridge-frame-0042-left.png`. Record the game build hash, mod commit, OpenXR runtime, headset, test scenario, and result in the paired manifest or [`docs/TEST_PLAN.md`](../docs/TEST_PLAN.md).

By default the mod and its launcher do not write here. Each run gets a folder under `%LOCALAPPDATA%\PreyVR\runs\`, and frame dumps go to `PREYVR_CAPTURE_DIR`, or beside the DLL if that is not set. Copy what is worth keeping into this folder.

Some older documents name folders such as `re/` and `audit-2026-09-06/`. Those were ignored local output and are not in this checkout.
