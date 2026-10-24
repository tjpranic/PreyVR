# The body following the player

The one document for the player's body turning, and once moving, with the player: how it works
now, the engine facts it rests on, what was tried and removed, and the one-frame flash it caused
when a smooth turn stopped. Consolidated on 2026-10-25 from three documents, now removed:
`BODY-YAW-2026-10-11.md` (turning), `ROOMSCALE-2026-10-11.md` (moving) and
`TURN-STOP-FLASH-2026-10-24.md` (the flash).

Related, and kept separate because their main subject is something else:
- [MOVE-STOP-2026-10-24.md](MOVE-STOP-2026-10-24.md): movement stops, cause A of which was
  room-scale's `SetPos` (the engine's teleport).
- [TURN-LEAN-ARMS-2026-10-19.md](TURN-LEAN-ARMS-2026-10-19.md): leaning push-back (also
  room-scale's) and the arms.
- [COMFORT-CONTROLS-2026-09-19.md](COMFORT-CONTROLS-2026-09-19.md): snap and smooth turning.

All engine addresses are RVAs in the Steam `PreyDll.dll` (SHA-256 `7D6E322F...05311A7`), from
`0x180000000`. RTTI is stripped, so the names are by shape, not by symbol.

## Where things stand (2026-10-25)

**The body turns to face your torso; it does not move with you.**

| When you turn | What turns the body | When |
|---|---|---|
| With the stick (smooth turn) | Prey, as always | Prey's own update |
| By a snap turn | The mod, once, to the torso | The frame the snap lands, at `ArkPlayer::Update`'s entry |
| Physically, in the room | The mod, every frame, at most 360°/s | After the frame's render; never while Prey is turning the player, nor for 150 ms after |

- **Walking in the room leaves the body behind.** Room-scale was removed on 2026-10-24
  ([Room-scale](#room-scale-removed-2026-10-24)).
- **Code:** `src/dll/TorsoLane.{h,cpp}` (the lane), `include/preyvr/BodyTurn.h` and
  `src/common/BodyTurn.cpp` (the arithmetic and `NativeTurnWatch`), `HeadTrackingHook`
  (`TurnHeadTrackingReferenceWithNative`, `SetSnapTurnsAboutBody`, `UseNativeViewYawOnce`),
  `FrameSchedule::AfterEngineRender` (calls `ServiceBodyFollow`).
- **Tests:** `tests/BodyTurnTests.cpp` (`body_turn`), including `TestSnapKeepsTheCameraOnTheBody`
  and `TestSmoothTurnPausesTheBody`. **Static contracts:** `tools/re/verify_view_rotation.py`
  (27 checks).
- **Commands** ([TUNING-COMMANDS.md](TUNING-COMMANDS.md), Body): `body.follow 0|1` (physical
  following), `body.followafter 0|1` (when it turns the body), `body.torso` / `arm.torso` (the
  torso filter, 35° and 600 ‰/s), `body.report`. None is saved.

**Do not:**
- **Turn the body while Prey turns the player** (a smooth turn). Every such frame shows the view
  wrong by Prey's body lag (below): the jitter that removed Body Follows You.
- **Turn the body before the frame's render** in the follow. That frame's pose was made for the
  old facing.
- **Bring back room-scale's plain `SetPos`.** It is the engine's teleport (MOVE-STOP, A).

## The four yaws

| | Meaning | Owner |
|---|---|---|
| **G** | native player yaw: what the body mesh, capsule and arms rig face | engine |
| **R** | reference yaw (`room.yaw` / `gReferenceYaw`), tracking space | mod |
| **H** | head yaw, tracking space | headset |
| **P = G − R** | world yaw of the tracking space | derived each frame |

The view is `P + H` (`HeadTrackingHook.cpp`, `UpdateViewObserved`); the body faces `G = P + R`.
Physical turning changes only H, a snap turn only R, a recenter sets R = H once, and Prey's stick
turn changes G. So without help the body faces wherever you faced at the last recenter. That
was the original report (2026-10-11): turn round physically or snap 180°, look down, and you see
the character's back.

**The design: R follows the torso yaw T, and G moves with it.** Each turn computes
`d = wrap(T − R)` and turns G and R by d together, so **P, and with it the view, the aim, the
hands and the panels, does not move**. The rules that follow from it:

- **Relative, not absolute.** The turn is `level(Rz(d) · current G)`, on top of whatever the game
  did; an absolute `G = P + T` would fight every native rotation.
- **No generation bump.** About 25 consumers reset on a reference-generation change (IK
  calibration, the two-hand hold, gestures, panel fits, the render contract). The generation
  announces a change of P; this never changes P. Snaps and recenters keep their bumps.
- **Level.** The written rotation is yaw only: native pitch bends the torso mesh, and nothing in
  VR reads it.
- **One torso.** `armpose::TorsoYaw` (the head's yaw behind a 35° deadzone, relaxing at 0.6/s),
  estimated once a frame by TorsoLane, is what the body, the arm solver's shoulders and the
  holster and medkit slots all read. A recenter resets it.

**What each consumer does** (from a survey of every yaw-coupled site, 2026-10-11):

| Consumer | Frame it uses | Effect of a body turn |
|---|---|---|
| View, aim, shot ray, interaction, scene query, psi, grenades, physical, wrist, debug overlay | P | None |
| IK world goals; the rig's model space from the ADIK `location` | P; G | Re-expressed in the turned rig; the shoulders sit on the visible body |
| Head-relative movement | `H − R` | Relative to the torso the body faces |
| Heavy carry | native target (G) plus a P-based shift | The shift's neutral offset turns with `P + R` (= G), the frame the native target turns in |
| Holster and medkit slots | the shared torso | They follow the torso while the hand is outside them, so a turn cannot drag a reached slot away |

## How the body is turned: the engine's own setter

No code writes G directly. **ArkPlayer vtable slot 52** (`+0x1A0`, `0x1584CC0`) has the
`IActor::SetViewRotation(const Quat&)` shape and does three things:
- copies q to ArkPlayer `+0x13F0`, which is `ArkPlayerCamera::m_rotation` (camera `+0x150`,
  the camera being ArkPlayer `+0x12A0`), the rotation save games store as `"rotation"`;
- calls the player entity's (`+0x38`) virtual `+0x118` with `(q, 0)`, `SetRotation`;
- hands q to a component at `+0x1468`, which **keeps its own copy** at `+0x08` (`0x1327050`).

Slot 53 (`0x157CE80`) is the matching getter. An entity rotation event (`0x1580AE4`) copies the
entity rotation back into `+0x13F0`. **Use the setter, not a field write:** writing `+0x13F0`
alone leaves the component's copy and the entity stale. At install TorsoLane compares the
setter's, the getter's and the component copy's complete bodies, and slots 52 and 53.

The component at `+0x1468` is the HUD helmet's: its update (`0x1327070`) eases its copy toward the
live view camera and rotates the character's `helmet` attachment (slots 0 and 5) by the
difference, the HUD's sway, with creak sounds from the same angle.

**Other routes, and why not:** posting look input (`xi_thumbrx`) gives a rate scaled by an
unmeasured turn speed, not an angle; hiding the body mesh removes the body too.

## When the body is turned, and why

Each rule below was paid for in a headset.

1. **A snap turns the body at once, and the view about the body.** The snap is queued where it is
   decided and applied at `ArkPlayer::Update`'s entry (`0x1584FE0`, vtable slot 19), TorsoLane's
   hook; TorsoLane then turns the body to the torso all the way. While it can,
   `SetSnapTurnsAboutBody(true)` makes the snap skip the pivot about the head, so the snap and
   the body's turn are one turn about the capsule, as a smooth turn's is. Pivoting about the head
   kept the camera fixed in the world while the body turned under it, and after a step away from
   the origin you could see into the model (PreyVR-74).
2. **The physical follow never turns the body while Prey is turning the player**, nor for 150 ms
   after (`bodyturn::NativeTurnWatch`: the native yaw moved more than 0.02° since the lane last
   left it). A physical turn made during a stick turn is caught up after it, at most 360°/s.
3. **The physical follow turns the body after the frame's render** (`ServiceBodyFollow`, from
   `FrameSchedule::AfterEngineRender`). The decision is still made at `ArkPlayer::Update`'s entry;
   the turn waits until the world render has taken the body's transform, before the next frame's
   animation starts. `body.followafter 0` puts it back at the entry, for an A/B.
4. **On a frame where the body lagged the view when it was turned, the view takes Prey's look yaw
   from `m_rotation`** (`UseNativeViewYawOnce`), not from the view Prey composed; never while
   Prey's focus camera has the view. With rule 3 the follow no longer needs this; it covers snap
   turns and `body.followafter 0`.

Rules 2 to 4 all come from one engine behaviour, measured in the next section.

## Prey's body lag, and the one-frame flash (2026-10-24)

**The report.** With smooth turning, the moment a stick turn stopped, the headset showed one bad
frame, "like the camera is rendering an old frame". Snap turns did not. `body.follow 0` removed
it. After the first fix the world was clean but the player's own body still showed it.

### What Prey does during a stick turn

Prey's eye (`0x1489010`, from `UpdateView` `0x148B820`, camera mode 0) is composed every frame,
with no smoothing between frames, from:
- the **entity's rotation**, read live (IEntity vt `+0x150`);
- a roll offset from movement (player `+0x58`, `0x159C4A0`; no yaw);
- the **`#camera` attachment's model-relative transform**: character slot 0 (vt `+0x2D8`), its
  attachment manager (`+0x48`), the attachment by name (`+0x38`), its transform (`+0x68`). `#camera`
  sits on head joint 98;
- lean, then view effects (`0x117B4D0`: time-based sway, independent of turning).

**During a stick turn Prey lets the body (the entity) lag the view** (`m_rotation`), and its
animation turns the head by the lag, so entity × `#camera` stays on `m_rotation`. Measured just
before a catch-up:

| `m_rotation` | Entity | `#camera` | Prey's view |
|---|---|---|---|
| 56.755° | 26.688° | +30.067° | 56.755° |

### Why the turn made a bad frame

The character's animation for a frame starts in the pre-physics pass, before `ArkPlayer::Update`.
The setter then takes the entity all the way to `m_rotation` at once. So on that frame:
- **the view** was the entity (now on `m_rotation`) plus a head still turned by the old lag: the
  lag counted twice;
- **the body** was drawn turned, with a pose made for the old facing: Prey's look pose turning the
  head and upper body by the lag, and the arm IK, which maps the hands into the model with the
  location the animation started with (ADIK params `+0x14`, `kParamsLocQ`).

On the next frame the animation is posed for no lag and everything is right. Every catch-up
measured (PreyVR-79, run `player-20261024-210526`):

| Body lag | `#camera` yaw | View error, that frame | Next frame |
|---|---|---|---|
| 30.067° | 30.067° | 30.067° | 0 |
| 45.638° | 45.638° | 45.638° | 0 |
| −32.318° | −32.317° | −32.317° | 0 |
| −10.605° | −10.605° | −9.877° | 0 |
| −18.823° | −18.824° | −17.564° | 0 |

The catch-up turns themselves were 0.3° to 2°; the error's sign followed the stick turn before it
(PreyVR-78, run `player-20261024-205536`, 7 of 7). This explains the rest:
- **Snap turns were fine:** they never use Prey's turn, so the body never lags.
- **Ordinary follow turns were fine:** the body is already on the view.
- **Body Follows You jittered** (inferred, not measured with it): it called the setter during
  stick turns, every frame, so every frame carried the lag twice.

### The frame order it rests on

`CSystem::Update` (`0xDF5640`) calls the entity system's `PrePhysicsUpdate` (vtable `0x1D34798`,
slot `+0x18`, `0x923BF0`), then physics, then `EntitySystem::Update` (slot `+0x20`, `0x926520`),
where `ArkPlayer::Update` runs. Inside the player's update (`0x15806E0`): the stick's turn rate
(`0x158F5B0`, zero at once below the deadzone, then aim assist `0x12DFC20`), the helmet component
(`0x1327070`), then `ArkPlayerCamera`'s update (`0x148A250`). The view (`UpdateView`) and the
render come after.

`CCharInstance::StartAnimationProcessing` (`0x82F910`, character vtable `0x1D22200` slot `+0x90`)
copies the caller's location to `+0xA90` for the job (`CSkeletonAnim::ProcessAnimations`
`0x839500`). The player uses CryAction's AnimatedCharacter. With the turn made after the render
(PreyVR-81, run `player-20261024-213107`, 16 of 16), the next frame's animation started 7-8 ms
later, on the main thread, before the view and the render, posed for exactly the turned body,
and Prey's view equalled `m_rotation`.

### The two fixes

1. **The world (PreyVR-80):** `TorsoLane::TurnToTorso` reads the entity's rotation before the
   setter. When the body lagged by more than 0.05°, that frame's view takes the yaw from
   `m_rotation` (camera `+0x150`), unless Prey's focus camera supplies the view: player `+0x9B0`
   with its state at `+0x98` nonzero (`0x15ABD20` returns at once when it is 0). Worn: the world
   was clean; the body was not.
2. **The body (PreyVR-81):** the follow turns the body after the frame's render (rule 3). Worn:
   fixed, world and body. The cleaned build, PreyVR-82, passed a sanity check.

### Ruled out on the way

| Candidate | Why not |
|---|---|
| Compositor frame synthesis (SteamVR at 120 Hz, Prey at about 63 fps) | `body.follow 0` removed the flash |
| The old cull lag ([CULL-CAMERA-CRASH-2026-10-17.md](CULL-CAMERA-CRASH-2026-10-17.md)) | No lag in 20 runs |
| The stick turn lane (MoveLane) | Posts on change, always posts the zero |
| The hands' frame | Previous camera minus the head yaw: independent of R |
| Motion blur, temporal AA | `r_MotionBlur 0`, `r_AntialiasingMode 1`, both read back |
| View effects (`0x117B4D0`) | Time-based, independent of turning |
| The helmet's copy | The error was in `#camera`; the helmet only sways with the body |
| The weapon | The flash showed with it holstered |

The investigation used read-only frame-probe steps and a hook on `0x82F910` in three diagnostic
builds (PreyVR-78, 79 and 81); all of it was removed once the fix was worn.

## Room-scale (removed 2026-10-24)

**What it did.** The rendered eye is the native eye plus `Yaw(P) · (head − origin)`, and physical
walking grows that offset without bound: the head walks through walls, the arms reach from a body
up to 1.8 m away, and line-of-sight checks start beyond the wall. Room-scale was the positional
twin of the turn: each frame at `ArkPlayer::Update`'s entry it moved the capsule toward the head
with the entity's `SetPos`, swept first with `RayWorldIntersection` (rays at 0.45, 0.9 and 1.35 m,
centre and edges), and moved the origin by the applied displacement, so the view never moved. It
had a head-in-wall layer: a fade to black, with the trigger and use button held off, while the
segment from the body to the head hit geometry. It was the Comfort option **Body Follows You**:
Off, Turn Only, Turn and Move.

**What it taught, for anyone who revisits it:**
- **Snap-back does not happen:** a 20 cm `SetPos` stayed put for 300 frames (`nudgeDriftMaxMm=0`).
- **Follow the neck, not the eyes.** With a 12 cm deadzone the body lurched after you; with 3 cm
  it chased the eyes, which swing several centimetres whenever the head turns or tips. Following a
  point 7.5 cm below and 8 cm behind the eyes, levelled to the torso, fixed both.
- **`SetPos` is the engine's teleport:** velocity zeroed, airborne, ground released. That stalled
  walking on turns and pushed a leaning player back out of furniture. The move that keeps the
  motion (`pe_params_pos` with `bRecalcBounds = 0x21` first) is in
  [MOVE-STOP-2026-10-24.md](MOVE-STOP-2026-10-24.md), A.5;
  [TURN-LEAN-ARMS-2026-10-19.md](TURN-LEAN-ARMS-2026-10-19.md) has the push-back.
- **A fade layer needs a 640×480 texture or more:** `InventorySwapchain` silently refuses smaller
  ones.
- **Runtime tracking-space changes come in bursts** (up to 11 a second around taking the headset
  off), each forcing a recenter.

**Why it went.** With Body Follows You on, smooth turning jittered when spinning; with it off it
did not (your A/B, run `player-20261024-180831`, Turn Only and Turn and Move both jittered). It
was removed whole: the follower, room-scale, the fade layer, four options and nine commands. The
shared torso and the snap seam stayed in TorsoLane.

**Entity seams it used** (still pinned by `verify_view_rotation.py`): the concrete `CEntity`
vtable `0x1D30BD0` (the hash-gated `GetPhysics` `0x9042B0` at `+0x228`); `GetPos` `+0x110` →
`0x10EA250` (`&m_vPos`); `SetPos(pos, whyFlags, recalcPhys, force)` `+0x108` → `0x90AF40` (stores
`m_vPos`, `flags |= 2`, tail-calls the transform update `+0x350`); `SetRotation` `+0x118` →
`0x90B220` (the same with `|= 4`); `GetWorldTM` `+0xE8` → `0x10EA260`. The game's own small move,
the stance change (`0x157B980`), uses the same `GetPos`, ceiling ray, `SetPos` pattern.

## History

| Date | Package | What changed | Result |
|---|---|---|---|
| 10-11 | `PreyVR-body-follow-wip` | **Body Follows Head:** the body follows the torso every frame through the setter | "A definite improvement"; `readbackMaxMdeg=0`, the view never moved for a body turn |
| 10-11 | `PreyVR-roomscale-wip` to `-3-wip` | **Room-scale** in three stages: follow, then smaller steps and the fade, then the neck model and standing aside | "Everything works as expected" |
| 10-19 | PreyVR-42, 43 | Room-scale moves keep the motion (MOVE-STOP, A) | Worn OK |
| 10-24 | PreyVR-73 | Smooth turning jittered with Body Follows You on: **all of it removed** | Worn OK, but snaps no longer turned the body |
| 10-24 | PreyVR-74 | One body turn to the torso after each snap | The camera moved against the model |
| 10-24 | PreyVR-75 | Snaps turn about the body | Worn OK |
| 10-24 | PreyVR-76 (`68bfc1d`) | Physical turns turn the body again, paused while Prey turns the player | Worn OK; one bad frame when a stick turn stopped |
| 10-24 | PreyVR-78, 79 | Diagnostics: the view error, then its terms | The cause, above |
| 10-24 | PreyVR-80 | The view takes the yaw from `m_rotation` on a lagged turn | World fixed; the body still flickered |
| 10-24 | PreyVR-81 | The follow turns the body after the render | Fixed |
| 10-24 | PreyVR-82 | Diagnostics removed | Sanity check passed |

## Open

- **Snap turns still turn the body at the frame's start** (rule 1). If Prey had turned the player
  just before (aim assist, a script), the snap's body turn could show the same one-frame pose as
  the flash; the snap's own view jump would hide most of it. Not seen. The fix would be the same as
  the follow's.
- **On a catch-up the body still turns by Prey's lag in one frame**, up to about 45°. Only visible
  looking down at it.
- **A torso from the controllers** (the hands' average yaw) rather than the head alone, for
  looking sideways while holding a weapon forward. Not tried.
