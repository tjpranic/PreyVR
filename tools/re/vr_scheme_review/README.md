# Offline VR scheme integration replay

These executables include the production `MoveLane.cpp`, `AimTakeover.cpp`,
`ReticleFollow.cpp` and `HudBridge.cpp`. External game/input boundaries are stubbed. They do not launch,
attach to, inject into, or post input to Prey. The movement hook fixture checks
all five arguments and the native return; the native byte verifier independently
establishes why that signature is required.

They are CTest tests in the root build (`scheme_review`, `aim_review`,
`hud_queue_review`, `reticle_review`), so they run with the rest of the suite:

```powershell
cmake --build build/headless --config Release
ctest --test-dir build/headless -C Release -R _review --output-on-failure
python -B tools/re/verify_vr_scheme_native.py
```

Each stubs only the functions its lane calls into. When a lane starts calling
another, the link error names the stub to add.

The frame-contract checks model the skipped-copy control flow. They do not mock
the whole OpenXR host or assert compositor behavior. HUD queue tests prove thread
handoff and argument lifetime using a missing-module positive control; they do
not claim Scaleform accepted a movie function. Aim tests assert that the installed
gameplay ray, published sample, and reticle input use the same origin in all six
mode/calibration combinations. They do not test actual movie pixel placement.

The aim replay also provides a synthetic CSystem camera to the real camera-yaw
reader, holds a LOCAL controller fixed and sweeps head yaw through 60 degrees.
Camera-relative mode is the positive control. Reticle replay supplies camera
bytes to the production projection and checks field writes/native-call arguments
against analytic perspective, including finite origin, eye offset and asymmetry.
It emits coherent `rp*` reports usable by `tools/re/analyze_aim_projection.py`.
The movement replay feeds the lane one `TrackingFrame` per tick, through its
`play` actions, with snap turn and head-relative movement off so each stick
axis is one posted key. It includes the recentre boundary and expects the
digital trigger event added in `2c48633`, as well as the four analog axes.
