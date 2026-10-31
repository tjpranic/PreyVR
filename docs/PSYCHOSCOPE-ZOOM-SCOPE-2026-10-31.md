# Psychoscope zoom: cause and fix scope (2026-10-31)

Branch `custom`. TODO: "Psychoscope zoom is broken". Found statically with Ghidra over the Steam DLL
(SHA `7d6e322f…11a7`), plus the FOV recorded in the headset run logs.

**Route A was chosen and built on 2026-10-31.** Headset, PreyVR-119: the zoom works as expected. The wearer found the
reticle and the aim laser separating as the zoom deepened, and that the psychoscope stops on its own, which made the
Zoom Limit option unnecessary. PreyVR-120 fixes the first and removes the option. **Headset, 2026-10-31 (PreyVR-120):
confirmed working by the wearer.** See [What was built](#what-was-built).

## The defect

Zooming with the psychoscope does not magnify anything. The whole view, overlay included, shrinks towards
the middle of the lens, and everything outside it is black.

## Why

**Prey zooms by narrowing the camera's FOV, and the mod tells the headset the true FOV of every frame.** On a
monitor a narrower FOV on the same screen is a magnification. In a headset the compositor draws the image over
exactly the angles the layer declares, so the world stays the same size and the image simply covers less of
the lens. The rest of the lens has no layer on it, which is the black.

| Step | Where | What |
|---|---|---|
| Zoom requests | zoom manager at **ArkPlayer +0x1898** | A list of `{id, hfov, priority}` (12 bytes each), the highest priority wins. Push `0x1490E40`, pop `0x1490B70`, modify `0x14910F0`, read `0x1490C30`. |
| Blend | `0x1490F60` | Moves the current value (**+0x18**) towards the winner at a fixed rate, set by the duration given to the push. |
| Final hfov | `0x1490BE0` | `max(1, current - 85.5656 + cl_hfov)`, with `cl_hfov` at `[0x2C16830]+8`. A zoom is a **delta on the slider**. 85.5656 is the constant at `0x1E014B4`. |
| FOV | end of UpdateView `0x148B820` | `viewParams.fov = 2·atan(tan(hfov/2) · H / W)`, at `param_2[0xC]`, byte +0x30. **`0x148C640` is the only call to `0x1490BE0`.** The eye buffer is square, so this vertical FOV equals hfov. |
| Mod: contract | `MainRenderHook.cpp:158` | `renderContract.tangents` are read from the camera just built. Under `native_projection` (on in VR, `VrMode.cpp:112`) that camera keeps Prey's FOV. |
| Mod: declaration | `XrEyePair.cpp:255`, `XrSessionHost.cpp:385` | The tangents become `gHost.eyeFov`, then `projViews[eye].fov`. |

This honest declaration is correct for normal play, and it is what keeps world scale right
(`docs/SUBMISSION_CONTRACT.md`). Zoom is the one case where a flat game relies on the image *not* matching
the declared angles.

### What is measured

`near_fov=follow` logs the vertical FOV of the rendered eye (`VrMode.cpp:356`), which gives the rendered FOV
over time. Slider at 110°:

| Event | Rendered FOV | Image size on the lens | Runs |
|---|---|---|---|
| Open the scope | 110° to 104.4° in about 0.15 s | 90% | every run (so `DefaultZoomHFOV` = 80) |
| Hold D-pad up | 104° to 49.4° at a steady 28°/s, where it stops | 32% | `player-20261031-101537`, t=772 s |
| Lock a target (Y) | 104° to 67.9 / 39.4 / 39.4 / 24.8 / 27.9° | 15% at the deepest | `-104739`, `-110410`, `-121500` |

Image size is the ratio of the tangents, `tan(rendered/2) / tan(110°/2)`. The declared FOV itself is not
logged. That it equals the rendered FOV comes from the code above, and it matches what the wearer saw.

The lock zooms are told apart from the D-pad by their timing. They all take 0.47–0.51 s to very different end
values, which is a fixed-duration blend, while the D-pad runs at a constant rate. The D-pad's stop is
`FullyZoomedHFOV`: PreyVR-119's log reads `wanted=3.10` at the end of every held zoom, the same 49.4°. A lock has
no such stop, as it fits the target, so it can go deeper: 6.5× in these runs.

### Everything that narrows the view

The bug is not limited to the psychoscope. All of these push on the same manager:

| Source | Where | Priority | Target |
|---|---|---|---|
| Open the scope | `0x1272D10`, `0x1272AB0`, `0x1272E00` | 4 | `DefaultZoomHFOV` (+0x10), duration +0x14 |
| D-pad `zoom_in`/`zoom_out` | handler `0x1272B90`, step `0x1272F80` | 4 | `current ∓ ZoomDelta(+0x08) × tick(+0x60)`, clamped to `[FullyZoomedHFOV(+0x0C), +0x10]` |
| Scan lock | `0x15D97E0` | 5 | Fits the target: `2·atan(tan(θ)·ratio)`, θ = half its height over its distance; duration config +0x3C |
| Examination | `0x15ABF2E` | 3 | Config +0x98, duration 0 |
| Psi power | `0x15B5FEA` | 3 | A lerp between object +0x268 and +0x26C (identified from nearby strings only) |
| Flow node `ArkCameraFOV` | `0x1178FD0` | 1 | `desiredHFOV` |
| Not identified | `0x1263798`, `0x126730A` | | Two player functions, not read; could be sprint or weapon zoom (`HfovDuringSprint`, `WeaponZoomedHFOV` exist) |

Changes that *widen* the view are harmless: the extra image falls outside the lens. The logs show brief rises
to about 115°.

### No gameplay dependency found

`0x1490BE0` has one caller, the view. `0x1490C60` scales a speed by how far the zoom has progressed (callers
`0x1693970`, `0x1695140`). Scan range and scan rate have their own config (`MaxDistanceScanRateMultiplier` and
others). So zoom looks purely visual: nothing found ties a scan to it. Scan target selection was not read in
full, so check a scan at each zoom level in the headset.

Not verified: the lock also installs a camera function (`0x126BB60`, camera mode 2) and there is a
`FanfareCameraSpeed` setting, so Prey probably turns the camera onto the target. VR's head view replaces that
rotation, so in the headset the target stays where the head left it.

## What can be done

| Route | Result | Cost and risk |
|---|---|---|
| **A. Declare the unzoomed FOV, keep rendering Prey's** (chosen) | A real, sharp, full-view zoom: 105 pixels per degree at 24.8° against 16 at 110°. No black, and the overlay keeps its size. Fixes every source above in one place. | Small. World is no longer locked to the head: at 6.5× a 1° head movement moves the image 6.5°. Comfort must be judged in the headset. |
| B. Second view in a scope window | The classic VR scope: the world stays normal around a zoomed circle. | A third scene submission in DualSceneStereo, with its own temporal AA history, about one more eye of GPU. The largest job here, and unproven. |
| C. Crop and enlarge the existing image in a window | No extra render. | Blurry: 2.5 pixels per degree at 6.5×. Not worth it past about 2×. |
| D. Pin the FOV at the slider | Black gone, nothing to tune, no comfort issue. | There is no zoom any more, though no ability found depends on it. A small fallback. |

## What was built

- `include/preyvr/ViewZoom.h`: the math. `viewzoom::BaseTangent` reads the unzoomed frustum from the final hfov
  against `cl_hfov`, as a ratio of tangents so it holds for any eye shape. `Magnification` gives a frame's zoom from
  its own tangent. `ShownPoint` gives where a zoomed frame shows a point in the world.
- `include/preyvr/ViewZoomNative.h`: the body contract on `0x1490BE0`, whose bytes also pin the cvars pointer.
- `src/dll/ZoomLane.cpp`: runs after Prey's UpdateView, next to `ApplyNearClip`. It reads, and writes nothing to the
  game: it notes the unzoomed tangent (the base) each frame. It stops at the gate on other bytes, and with VR not
  driving the view; frames are then declared as they are.
- **No option.** PreyVR-119 had a Zoom Limit in VR Options, held by narrowing the view no further. Prey's own zooms
  stop on their own, so it went the same day. `zoom_limit` is on the retired list, because `Parse` refuses a whole
  file on an unknown key and a saved file has it.
- **`zoom.report`**, and one log line at the end of each zoom with how deep it went.
- `tools/re/verify_view_zoom.py` and the `view_zoom`, `zoom_lane` and `render_contract` tests.

How the frame stays consistent:

- **One number, from the frame's own camera.** `ViewZoomOf(tan)` is the base over that camera's tangent, so a
  frame's FOV, eyes and declaration cannot disagree even though they are built on two threads a frame apart. The
  base only changes with the slider.
- **Declaration.** `DeclaredTangents` is `tangents × zoom`, used at both `gHost.eyeFov` writes. A zoomed frame is
  therefore always declared at exactly the base, and that does not move during a zoom. `tangents` stay the
  rendered ones: DebugOverlay draws the aim lasers with them.
- **Eye separation.** `BuildDualEyeCamera` and the alternate-eye path divide the IPD by the zoom. The near pass
  divides by the zoom in the eye's record (`BuiltEye::zoom`), and MarkerStereo uses `RenderedEyeIpdMetres`. The
  poses given to the compositor keep the real IPD.
- **What is in the image, and what is beside it.** A zoom magnifies the projection layer and nothing else, so each
  thing the wearer sees had to be sorted into one of the two:
  - *In the image:* the world, the body, Prey's overlay, the markers, and the aim lasers. The lasers are drawn into
    it afterwards, from the eye poses, so `RenderedEyePose` gives them the eyes that were really rendered.
  - *Beside it, in the world:* the world reticle and the wrist card are their own quad layers, placed at a point in
    tracking space. Left there, they stay at the true angle while the image moves the same point out by the zoom.
    That is what the wearer saw in PreyVR-119: the reticle and the laser parting, further the deeper the zoom.
    `ShownPoint` moves them out by the zoom at the depth they have, from the head the image was rendered from
    (`gHost.eyeHead`), not the live one, whose difference a zoom would multiply.
  - *Beside it, on the head:* the HUD panel, the wheel and the text panels are tied to the head and need nothing.
    The exception is the reticle's fallback on the HUD panel, whose tangents are multiplied by the zoom
    (`HudLayerReticle`, `ReticleFollow.cpp`).
- **Near FOV.** `FollowNearFov` refused tangents under 0.2 (about 22.6°), close to where Prey's zooms go. It now
  takes anything over 0.02, so hands stay matched to the world.

Unaffected, by reading the code: the reticle's screen position for Prey (`ReticleFollow.cpp:117`) is computed
through the live camera's tangents, so it stays on what the hand points at. Hands are magnified with the world.

Not built, as the zoom wore well without it: scaling head rotation by 1/M from where the zoom began, so the image
moves 1:1 with the head. It would matter if deep scan locks prove unsteady, as they are no longer limited.

### Headset checks

PreyVR-119 passed 1 and 2. The list PreyVR-120 was worn against; the wearer confirmed the build working as a whole:

1. Open the scope: no black ring and no shrink. Note how the 1.11× step feels.
2. Hold D-pad zoom: smooth magnification, overlay unchanged, both eyes fuse at every level.
3. **The reticle and the aim laser stay together at every zoom**, on a near target and a far one, in the middle of
   the view and towards its edge, and while the head turns.
4. Lock a target off-centre with Y: does it stay in view? A lock can now go past 3× (6.5× in the logs): is it steady?
5. Raise the left wrist while zoomed: the card stays on the forearm.
6. Close the scope mid-zoom, and sprint: both behave as before.
7. VR Options > Interface has no Zoom Limit, and every saved setting is still as it was.
8. A run's log has `preyvr_view_zoom result=0 detail=ready` and, for each zoom, `detail=zoom peak= frames=`.
