# The starfield at the sky's depth (2026-10-25)

Branch `custom`. TODO: "The starfield skybox displays at the wrong depth (?) or something to that effect
(gives double vision)." Found statically: Ghidra over the Steam DLL, plus the shader source and sky assets
read offline from the game's paks. Fixed in `src/dll/StarfieldStereo.cpp` (`stars.depth`, default on).
**Headset, 2026-10-25 (PreyVR-83): confirmed working by the wearer.**

## The defect

In the headset, every star of the space sky was drawn **1 m from the head**, while its depth was the far
plane. So the scenery hid the stars, but their stereo said "arm's length": about IPD / 1 m of crossed
disparity, 4° at a 69 mm IPD. The eyes cannot fuse that, so each star doubled. The Milky Way haze, Earth,
Moon and sun were right.

It happens only in the both-eyes frame (DualSceneStereo mode 3, the headset default since 2026-10-10). The
frame is culled and prepared once from the head, and each eye is then drawn from half the IPD beside it
([STEREO-DUAL-SUBMISSION](STEREO-DUAL-SUBMISSION-2026-10-10.md)). The alternate-eye path prepares each eye
from its own camera, so it never had the problem.

## Why: the chain

| Step | Where | What |
|---|---|---|
| Sky render | `0x21F2D0` (RenderSkyBox) | Without the HDR sky: rebuilds the space sky (`0xBB720`), then submits it (`0xBAC90`). |
| Head-centred object | `0xBAC90` | A temp render object (renderer `+0x440`). Its position is copied from the pass camera (`[pass+0x18]` `+0xC/+0x1C/+0x2C`, stored at `0xBAEAA/B0/B6`), so in mode 3 it sits at the head. It is sorted last (`m_fSort` 100000). The stars' render element (engine `+0x378`) is added with it (renderer `+0x460`); the element's `+0x40` is set to the space sky `+0x170`, the star block. |
| Star block | `0xBB720` (main thread) | StarRotation's 3×3 at space sky `+0x178..+0x198` (block `+0x08..+0x28`). |
| Unit stars | `0x1002AB0` (`Libs/Sky/stars.dat`, `'STAR'`, v`0x10001`) | Each star's RA/Dec becomes a unit vector, written six times (one per quad corner) through `Vec3f16` `0xF88B0`, which stores **w = 1.0** (`0x1C7EB28`). |
| Draw | `0xEFBD90` (element draw, vtable `0x1DB9130`) → `0xEFB4B0` (ArkStars) | Sets the shader's `StarRotation` from the block and draws technique `ArkStars` of shader `Stars`. |
| Shader | `Stars.cfx` `ArkStarsVS` | `pos.xyz = mul((float3x3)StarRotation, pos.xyz); HPosition = _pos_Common(pos); OUT.HPosition = HPosition.xyww;` The quad corner is added in clip space times `w`. |
| Camera-relative | `ModificatorVT.cfi` `_pos_Common` | The instance matrix's translation minus `g_VS_WorldViewPos` (this view's camera, the eye), then `g_VS_ViewProjZeroMatr`. |

So each star is drawn at `(head − eye) + StarRotation·d`, with `|d| = 1`. Flat, `head == eye`. In mode 3
the offset is ±IPD/2 against a 1 m star.

Not affected:
- **Milky Way** (`objects/sky/milky_way_a.cgf`, shader `ArkSpaceObject`): it shares the object, but the mesh
  is ±5200 m (z ±3256), so the error is 0.0007°.
- **Earth and Moon**: placed at `(Distance + radius) × 1000` (`0x1C7FA28`), thousands of km out.
- **Sun** (`ArkSun`): drawn from `float4(0,1,0,0)`, a direction, so position is ignored.
- **Space backdrop** (`ArkSpace`): solid black.

Not checked: the stock CryEngine sky (`SkyHDR`, its moon and its own `Stars` technique). It has the same
head-centred object, but Prey's space levels take the Ark path above.

## The fix

`ArkStarsVS` scales nothing but the rotation. So if StarRotation is scaled by 10,000, the stars are drawn
10 km out. What remains of the error is the eye's offset over that distance: 0.0002°. Nothing else in the
image changes:
- a star's size is set in clip space (its corner offset × `w`)
- its depth is forced to the far plane (`xyww`)
- its brightness has no distance term
- a flat frame draws the same pixels (`starfield_depth` models the shader to check this)

**How it is applied, without writing Prey's memory.** The ArkStars draw is the hook point. Its whole body
(1423 bytes) reads its element at four places only, all reads:
- `+0x48` the star mesh (nothing is drawn without one)
- `+0x40` the star block
- `+0x50` the falloff texture's id
- `+0x54` passed to that texture's sampler

The element is never handed to a callee. Each eye's call is therefore given a stack copy of the element's
first 0x58 bytes, with `+0x40` pointing at a copy of the block whose rotation is scaled. Prey's element and
block are only read, and the game thread's per-frame writes to both are untouched.

- Gate: the draw's whole body is hashed (`preyvr/StarfieldNative.h`), with no relocations in it. Its one
  caller is `0xEFBE76`, and no table holds its address.
- Install: at VR start (`VrMode`, after the marker depth). A failure is logged and leaves Prey's stars.
- An element or block that cannot be read gets Prey's own draw (`unread`).

## Commands and diagnostics

- `stars.depth [0|1]`: 1 by default. 0 draws them as Prey does, for A/B. Bare: report only.
- `PREYVR_STAR_DEPTH=0` at launch leaves the draw unhooked.
- The once-a-minute status line carries
  `stars={depth hooks gate draws far native unread distanceM}`. `draws` is about two per frame with stars in
  view (one per eye), and `far` should equal it.
- Logged once: `preyvr_starfield result=0 detail=hook_installed`, then `detail=first_far` at the first far draw.

## Headset check

1. Somewhere with stars in view (a window to space, or outside the station): the stars should fuse at
   infinity like the Milky Way haze behind them, not double.
2. `stars.depth 0`: the doubling should come back; `stars.depth 1` removes it again.
3. Flat on the desktop mirror nothing should change: same stars, same size, same brightness.
4. The status line: `hooks=1 gate=ok`, and `far` climbing with `draws`.

## Reproduce

- Bytes and call routes: `python tools/re/verify_starfield.py` (run by ctest `re_verifiers`).
- Tests: `starfield_depth` (the far block and the geometry it is for, against a model of `ArkStarsVS` and
  `_pos_Common`) and `starfield_stereo` (the detour with Prey replaced: the copy handed on, Prey's element
  and block untouched, the gate, the installer and the launch switch).
- The shaders: `shaders/hwscripts/cryfx/{stars,arkspace,arkspaceobject,skyhdr}.cfx` and `modificatorvt.cfi`
  are in `Engine/Shaders.pak`. The Milky Way mesh and material are in `GameSDK/Objects-part5.pak`, and
  `materials/sky/space.mtl` in `GameData.pak`. All are read with the `Pak` reader in
  `tools/re/extract_native_hud.py` (its deps in `build/re-tools` are for Python 3.10, which lacks
  `hashlib.file_digest`). Read them to a local folder; do not redistribute them.
