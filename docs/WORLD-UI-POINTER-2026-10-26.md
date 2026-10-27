# Pointer off on terminals and keypads: cause and fix (2026-10-26)

Branch `custom`. TODO: "Interacting with computers and screen is off (High Priority) -- Depth/pointer offset
issues". Found statically with Ghidra over the Steam DLL (SHA `7d6e322f…11a7`), then corrected by one headset
run. The wearer, Steam Frame: the crosshair sits on the screen, but **the button that highlights is somewhere
else, always offset the same way**. They aim with the right hand and press X.

**Status: PreyVR-96 worn OK 2026-10-26.** The wearer confirmed it fixed. Run `player-20261026-202146`:
- 25 uses of screens (`class=ArkWorldUI result=1`), none zoomed in (`begins=0`).
- Every read by a screen was handed the crosshair's ray (`reads=17366 steered=17366`).
- Prey's own ray had run 362, 227 and 125 mm beside it at the three status lines, at 0°.

**The examined-screen path: worn OK 2026-10-27 on PreyVR-97**, run `player-20261027-181857`. There were 2 zoomed
examinations (`begins=2`), with 307 frames pointed from the hand (`frames=307 refused=0`) and `steered=20151`.

PreyVR-95 fixed the wrong case (below). With the fix confirmed, the report's diagnostic fields (`reads`, `offMm`,
`offMdeg`, `backMm`) were removed afterwards.

## The cause

A screen's cursor -- what it highlights, and what a use presses -- is **where the player's cached reticle ray meets
the screen**. The crosshair is not drawn from that ray. It is drawn from the aim lane's published sample, and with
a gun drawn that sample starts at the **muzzle** (`aim.shot`, ShotRay.h). The cached ray keeps Prey's **eye**
origin, which interaction needs. The two run parallel, as far apart as the muzzle is from the eyes. At a screen
half a metre away that offset (20-40 cm) is bigger than a keypad. At 5 m in normal play it is a few degrees. With
no gun drawn, both rays start at the eye and agree.

This is not a regression of a refactor. The eye-origin cached ray dates from the M2 aim takeover (`0bfca30`,
09-03). The muzzle-origin crosshair dates from `aim.shot` (10-06). Moving use to the left hand (`e9bdc54`, 10-06)
ran a getter probe that found three per-frame readers of the cached ray. It did not find the screen's, because
that one only reads with a screen in range.

| Step | Where | What |
|---|---|---|
| The ray | `0x1585320` UpdateCachedReticleViewPosAndDir | Unprojects the crosshair's screen point `+0x17EC`, or the examination's cursor `player+0xA10` while `player+0xA48 == 1` (`CMP` `0x158536C`, `CMOVNZ` `0x15853B6`). It writes `+0x17D4` (origin, a near-plane point at the eye) and `+0x17E0` (direction). |
| The mod | AimTakeover, right after it | Writes the right hand's direction to `+0x17E0` and keeps the origin. The published sample starts at the muzzle with a gun drawn, so the crosshair and the right laser do too. |
| The read | `0x3017C0`, a FlashUI element's cursor raycast | `gEnv->pGame` (`0x224DA28`) vtable `+0x198` returns the IArkPlayer. Its slot 0, the getter `0x157CBB0`, is called at `0x30190F` (returning to `0x301911`) and copies `player+0x17D4..+0x17EB` into a 24-byte buffer. Cross-check: slots 1 and 2 are one-byte setters, which is how the other `pGame+0x198` callers use them. |
| The cast | same | `distance` (element `+0x1E8`) long, `RayWorldIntersection(Action)` with types 7 and flags `0x40F`. It must hit the element's screen entity, an `ArkWorldUI` entity named `"%s Screen"`. |
| The cursor | `0x301A60`, then the element update `0x3010C0` | The hit point goes through the screen's world matrix and local bounds into pixels of the Flash player's own viewport (`+0x200/+0x204`; element vtable `+0x1E8` forwards to the player's `+0x38`). That point is sent as a cursor move (player `+0x1C0` ScreenToClient subtracts the viewport origin, then `+0xF0`). The hardware mouse is not sent to an enabled world UI (`0x2CEF30`). |

The hit-to-highlight path is consistent in itself and does not depend on the render size. The offset is entirely
in the ray it is given.

### PreyVR-95 fixed the wrong case

PreyVR-95 gave the cached ray the hand's line only while the player examined a screen (the zoom). Run
`player-20261026-200026` shows four uses of `WorkstationBianca Screen` (`class=ArkWorldUI result=1`) with no
examination at all (`begins=0`), so that path never ran (`screenAim frames=0`). The watcher saw 3385 reads by the
screen. A gun was drawn throughout (`rig=4`, `wrench=0`, `weaponFrames=3423`).

## The fix (PreyVR-96)

**The screen's cursor raycast reads the crosshair's ray.** The shared getter detour gets a third client,
`ReticleRayClient::worldUi`, which acts only on reads returning to `0x301911`. It runs only after `0x3017C0`'s
whole body matched (`WorldUiNative.h`). It replaces the 24 bytes the getter returned with the current published
aim sample's origin and direction: the muzzle with a gun drawn, the eye without one, the hand's line at an examined
screen. So the screen highlights what the crosshair sits on, with or without the zoom. Every other reader of the
cached ray (the target selector, firing, markers) keeps Prey's eye-origin ray. A sample that is not usable (stale,
another weapon, a recentre) leaves Prey's own ray.

PreyVR-95's examination path stays. While a screen is examined, the cached ray and the sample start on the right
hand's line, level with the eye (`examine::ScreenRayOrigin`), placed against the camera centre (`cameraAnchored`).
The edit is undone before the next rebuild, and it wins over `aim.origin`. The shot ray is not applied there. The
gate is `ExaminingScreen`, which needs the examination's three body contracts. That check is latched before the
fit is hooked, and also with `PREYVR_EXAMINE_CAMERA=0`.

Files:
- `src/dll/AimTakeover.cpp/.h`: `WorldUiRay`, `EnsureWorldUiRay`, `AimAtScreen`, `ScreenAimReport`; covered by
  `tools/re/vr_scheme_review/aim_review.cpp`.
- `include/preyvr/WorldUiNative.h`: the contract; `tools/re/verify_world_ui_cursor.py` (27 checks, run by
  `re_verifiers`).
- `include/preyvr/ExamineRay.h`: the examination math; `tests/ExamineRayTests.cpp`.
- `src/dll/ExamineCamera.cpp/.h`: `ExaminingScreen`; covered by `tests/ExamineCameraTests.cpp`.
- `include/preyvr/WeaponAim.h` (`cameraAnchored`), `src/dll/ShotRay.cpp`, `src/dll/DebugOverlay.cpp`,
  `src/dll/ReticleFollow.cpp`, `src/dll/ReticleRayHook.cpp/.h`, `src/dll/VrMode.cpp`.

## Reading it in the headset

The status line (every minute) carries `screenAim={frames refused worldUi={gate steered}}`:

- `gate=ok`: the screen's raycast is steered. `gate=refused` means the bytes did not match (a game update), and
  the screen keeps Prey's ray.
- `steered`: the screen's reads handed the crosshair's ray. It grows while a screen is in range.
- `frames`: frames at an examined screen pointed from the hand. It grows only while zoomed in. The `refused`
  counter beside it counts frames where that could not be done (no camera that frame).

Checks:
1. A workstation used without the zoom, with a gun drawn and with none: the button under the crosshair highlights,
   and X presses it.
2. A keypad and a workstation that zoom in: the same, under the right laser. (Worn OK 2026-10-27, PreyVR-97.)
3. After leaving the screen, aiming, shooting and using things are as before.
