# The desktop window: start minimised, sized by PreyVR.json

Status, 2026-10-17:
- **First headset run (`player-20261017-095157`).** The window minimised just after
  startup; the game loaded and played normally, with no pause, but **made no sound**.
- **Second headset run (`player-20261017-101027`), with the sound fix: accepted.** The
  window minimised, the game played and the sound played. The minute status line read
  `calls=7331 answered=7331 audioSwallowed=2 audioIssued=0`: every throttle call was
  answered, and two lose-focus requests were held off when the window went down.
- **Then the default changed, at the user's request:** `StartMinimized` is 0. The
  window stays open unless PreyVR.json asks, and the hold applies either way.

Steam PreyDll SHA-256 `7d6e322f…05311a7`.

## What was asked

- The desktop window should start minimised.
- PreyVR.json should set the window's size, separately from the render
  (backbuffer) size, which stays `Width`/`Height`.

## The window size: launcher only

Prey creates its window from `r_Width` x `r_Height` and centres it. The launcher
already refits it after the mod starts (`Set-PreyVRWindowFit`), and the backbuffer
does not follow the window (measured 2026-10-15: a 1263 x 1353 window still
captured 2688 x 2880). So the size is a launcher setting:
- `WindowWidth`, `WindowHeight`: a box for the picture. The window gets the largest
  eye-shaped picture inside it, kept on the work area, so the desktop view is never
  stretched. Both 0 (the default) is today's fit: as large as the screen allows,
  never larger than the render size.

## Minimising: Prey reacts to losing focus

A minimised window is not the active window. Read from the supported image:

| Where | What | Effect when unfocused |
|---|---|---|
| `0xDF58E0`-`0xDF59B1`, `CSystem::Update` | While `GetActiveWindow()` != `renderer->GetHWND()`: pump messages, `Sleep(5)`, up to 5 times | **Up to 25 ms added to every frame.** No cvar. |
| `0x18281F5`, registration | `g_pauseOnLoseFocus`, default 1, flags 0, "Pause the game whenever the window loses focus" | The game pauses. |
| `0xDF5D5A`, `CSystem::Update` | `pInput->Update(GetForegroundWindow() == m_hWnd)` | The input devices are told they are unfocused. Effect on the mod's posted input: untested. |
| `0xF77F43`, renderer | Foreground change sets or restores the gamma ramp | Cosmetic. |
| `0xD5B950` -> `0xD5A630`, audio thread | The audio layer's manager request 0x10 runs the `lose_focus` trigger and the middleware's `OnLoseFocus`; no `s_IgnoreWindowFocus` in the image | **Silence**: seen in the first headset run. See Sound. |
| `0xF7E4B1`, `RT_EndFrame` | `Present`'s HRESULT: only failures are acted on | `DXGI_STATUS_OCCLUDED` (minimised) is success: rendering continues. |

The window is created by the renderer: `CreateWindowExA("CryENGINE", …)` at
`0xF541D9`, stored at `renderer+0xAC18` (`0xF541DF`), then `ShowWindow(SW_SHOWNORMAL)`,
`SetFocus` and `SetForegroundWindow` (`0xF5425E`-`0xF54278`). Renderer vtable
`+0x6C0` (`0xF50A50`) is `mov rax,[rcx+0xAC18]; ret`: the throttle compares against
exactly that window. Prey foregrounds the window itself, so a minimised launch flag
(`STARTUPINFO`) would not stick. The window is minimised after the mod loads instead.

**Corrections:**
- [LIVE_CAMERA_A1_PROTOCOL.md](LIVE_CAMERA_A1_PROTOCOL.md) said no cvar disables the
  pause on focus loss. `g_pauseOnLoseFocus` does.
- [NEXT-SESSION.md](NEXT-SESSION.md) says defocused CryEngine "renders no frames". The
  throttle found here costs at most 25 ms a frame, so it alone does not stop frames.
  What stopped them in those sessions is not established; the headset check below
  covers it.

## The design

**`window.hold` (`src/dll/FocusHold.cpp`): while held, Prey runs as though focused.**
- It answers PreyDll's `USER32!GetActiveWindow` import slot (`0x1C73A10`, in `.rdata`)
  with the render window. The throttle loop is the slot's only reader in the image, so
  nothing else changes.
- It is gated on both throttle spans. It also requires the slot to hold the loader's
  `GetActiveWindow` binding; another tool's pointer is refused.
- The answer is the window read from `gEnv->pRenderer + 0xAC18` and confirmed by
  Windows: a live `CryENGINE` window of this process. If the window is destroyed, the
  hold waits and holds the new one.
- It sets `g_pauseOnLoseFocus 0` through the verified console path, once frames are
  being observed. Release puts back 1, only if the hold changed it.
- It keeps the audio's focus: see Sound.
- Left native: input focus, the gamma ramp, Windows' own activation and the cursor clip.
  Nothing filters window messages, so whatever Prey does with the cursor when focus
  goes still happens.
- It logs `state=held`, `pending` or `refused`, and every foreground/minimised change
  while held, with `answered=`, the number of throttle calls answered.

**The launcher:**
- It sends `window.hold 1` with the startup commands.
- It fits the window as before, to the `WindowWidth`/`WindowHeight` box when set.
- With `StartMinimized 1` (default 0), it minimises the window (`SW_MINIMIZE`) as soon
  as the log shows `preyvr_focus state=held`.
- On `state=refused`, or no report before VR is ready, it leaves the window open and
  says so.
- The window is still visible for the few seconds between Prey creating it and the mod
  loading.

The hold is sent whether or not the window is minimised: in a headset nobody is at the
desktop, and a notification or a click on the launcher console had the same effect.
The keyboard hotkeys (F10-F12) still need the window focused. Recenter and VR Options
have controller equivalents; F11 (retry VR) does not.

## Sound

**Found by the first headset run, not by the static pass.** Everything played except
the sound. Prey's audio translation layer (CryAudio's ATL) handles focus as requests:
- **The dispatcher.** `0xD5B950` (`CAudioTranslationLayer::ProcessRequest`) runs on the
  audio thread for every request: ATL in `rcx`, the request in `rdx`, status written at
  request `+0x20`.
- **The manager handler.** Class 1 (request data `+0x10`) goes to `0xD5A630`, which
  switches on the type at data `+0x18`.
- **Type `0x10`, through the table at `0xD5AE68`, lands at `0xD5A6EA`.** It runs the
  trigger whose ID (`0x2983664`) the initialiser hashes from `"lose_focus"`, then the
  middleware's `OnLoseFocus` (vtable `+0x30`).
- **Type `0x20` at `0xD5A79F`** runs `OnGetFocus` (`+0x38`), then the `"get_focus"`
  trigger (`0x2983668`).
- **Neither case reads more of the request than its type.**

The hold detours the dispatcher (MinHook, gated on all five spans and the table entry):
- **While held,** a lose-focus request is marked done (status 1) and not run.
- **The audio follows a simple rule.** While held it is focused; released, it is what
  the engine last asked for. When the two differ, the detour issues the missing
  get-focus or lose-focus through the game's own handler, on the audio thread, from a
  two-field stand-in request.
  - This undoes a loss from before the hold, such as a click elsewhere during
    startup. The starting state is taken from whether the window was in front when
    the hook went in.
  - A release while unfocused mutes, as the game would have.
- **Every other request passes through untouched,** including mute-all and pause-all.
- **It is not a condition of the hold.** If the hook is refused, the hold still runs
  and the log says `audio result=refused`.

**The throttle counters:** `calls=0 answered=0` in the second after minimising, in
both runs. The loop sits behind three guards (`0xDF58A1`-`0xDF58BB`), and the intro
movies may be one of them; that is not established. VrMode's once-a-minute status line
carries `focus={…}`. A minute after VR started in the second run it read
`calls=7331 answered=7331`, so the loop runs every frame in play.

## Verified offline

- `tests/FocusHoldTests.cpp` (CTest `focus_hold`): the production code on a fake image
  with a read-only slot page and real `CryENGINE` windows. Covers:
  - refusals for a changed loop and a foreign slot;
  - waiting for the pin and for the window;
  - the answer and its counter;
  - the cvar: busy queue retried, verified, a failure not retried, restored on release,
    an untouched value left alone;
  - a destroyed window replaced;
  - the audio gate's refusals;
  - the audio rule: a loss passes when not held and is swallowed while held, focus is
    given back once after an earlier loss, other manager requests pass, and a release
    while unfocused mutes.
- `tools/re/verify_focus_hold.py` passes against the supported image. It checks:
  - both spans, and that the call reads the slot;
  - the slot is `USER32!GetActiveWindow` with one reader;
  - `GetHWND` returns `+0xAC18`, and the class is `CryENGINE`;
  - the cvar's registration: name, help, default 1, flags 0;
  - the audio spans, the manager call and table entry, and that cases 0x10 and 0x20
    read the IDs hashed from `lose_focus` and `get_focus`.
- The launcher's window tests cover the box fits and the settings: saved, explicit, a
  JSON `true`, and refusals. A `-DryRun` preflight runs clean. The full suite passes
  (74 tests).

## Headset check

Run 1 (`player-20261017-095157`) passed steps 1, 2 and 3 except sound. Run 2
(`player-20261017-101027`) passed them with sound. Steps 4 and 5 are not reported.
Launch with `"StartMinimized": 1` in PreyVR.json. In PreyVR.log:
1. `preyvr_focus state=held window=0x…`, then `pause_on_lose_focus=0 result=0
   detail=verified_readback`. If it says `failed`, the console refused the cvar.
2. `preyvr_focus audio result=0 detail=hooked`, then `window foreground=0
   minimized=1`. The launcher prints "Desktop window: minimised".
3. In the headset: is the frame rate normal, does the game run without pausing, does
   audio play, and do the controller menus and gameplay input work?
   - `window.hold` in **Tune Prey VR.cmd** shows `answered=` climbing while minimised.
     That is the throttle being answered.
   - Sound should play throughout. `audioSwallowed=` counts the losses that were held
     off; it is 1 or more after the minimise.
   - If menu input is dead, the next step is the input-focus flag at `0xDF5D5A`.
4. Restore the window from the taskbar. It should have the configured size, and the log
   shows `foreground=1 minimized=0`.
5. Click the desktop. The game should not pause, and the log shows `foreground=0`.

Not yet established: whether Prey reacts to `WM_SIZE`/`SIZE_MINIMIZED` (0 x 0) by
resizing anything. The 2026-10-15 resize measurement suggests not. A
`render_size_timeout` or `backbuffer_resized` refusal in the log would mean it does.
