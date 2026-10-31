# Source

Everything is grouped by feature. A feature's pure logic, its DLL side, its engine contract table and its headers sit together in one folder.

| Folder | What it holds |
|---|---|
| `base/` | Math, clocks, snapshots, the log and the small helpers every module uses. |
| `app/` | The DLL's entry, the bootstrap and its landmark gate, VR start, the worker thread, the frame schedules and the exports. |
| `engine/` | Landmarks, contract checks and engine access. `hooks/`: hook install and the frame seams. `console/`: console commands and verified settings. `fixes/`: the cull guard and focus hold. |
| `xr/` | The OpenXR session, swapchains, composition layers, hold frames, frame timing and FOV coverage. |
| `render/` | `camera/`: head tracking, eye cameras and zoom. `stereo/`: both-eyes rendering and what needs its own stereo (near pass, body depth, particles, markers, stars, video). `taa/`: per-eye temporal AA. |
| `controls/` | `input/`: action sets, input posting, prompts and haptics. `menus/`: menu navigation. `locomotion/`: moving, turning and zero-G. |
| `body/` | Hands, arms, torso, body turn and room-scale. `ik/`: the animation-driven IK lane. |
| `aim/` | Controller aim, the gameplay frame, shots from the muzzle, weapon alignment and the two-handed hold. |
| `physics/` | `contact/`: contact, surface stop and scene queries. `swing/`: the free-swing wrench. `hands/`: carrying, throwing and use. |
| `equipment/` | `body/`: holsters and the medkit slot. `psi/`: psi, medkit and psychoscope. `head/`: head lamp and helmet. `weapon/`: the draw skip and its sound. |
| `ui/` | `hud/`: the HUD, wrist hologram and reticle. `screens/`: panels, the UI pointer, loading screens and keypads. `options/`: VR Options. |
| `diagnostics/` | `commands/`: the tuning channel and research verbs. `overlay/`: the debug overlay, which also draws the aim lasers. `probes/`: every probe. `capture/`: frame dumps, crash dumps and hardware breakpoints. |

**Includes.** `src/` is the one include root, so every include names its file from there, even inside one folder: `#include "physics/contact/ContactLane.h"`.

**Pure code and DLL code share folders.**
- **DLL code** is listed in a DLL target in `CMakeLists.txt`, or declares `namespace preyvr::dll` (`preyvr::lifecycle` for the log).
- **Pure code** is everything else: math, eye-camera construction, IK, hand and body pose, option specs, UI panel fitting, input event encoding and the like. It keeps Windows, D3D11, OpenXR and live game pointers out of its data and math, so each piece can be tested without the game. Its `.cpp` files build into `preyvr_core` and are unit-tested in `tests/`.
- **`pure_boundary`** (ctest, `tools/check_pure_boundary.py`) fails when pure code reaches DLL code or a platform header.
- When a feature has both halves under one name, the DLL half ends in `Lane`: `aim/ShotRay.h` is pure and `aim/ShotRayLane.h` is the DLL's.

**Engine addresses** live in three places:
- `engine/EngineMap.h`: the landmarks the bootstrap checks at startup, and layouts several modules share.
- Each feature's `*Native.h`: its RVAs, vtable slots and FNV body hashes. Each is the contract that feature checks (`engine/NativeContract.h`) before it hooks or calls anything. The `tools/re/verify_*.py` scripts check these tables against PreyDll.dll, and several of them parse the `{0x..,n,0x..ull}, // Name` rows, so keep that row format.
- File-local gates inside some DLL modules.

Some of these files were pinned by research receipts before they moved here; [the receipts' README](../receipts/README.md) says what that means now.
