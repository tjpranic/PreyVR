# Source

- `dll/`: PreyVR.dll, the mod itself. It holds the bootstrap and its landmark gate, the MinHook detours, the OpenXR session and composition layers, stereo, the gameplay lanes, the VR Options menu and the command channel. It is one flat folder for now; [the 2026-10-22 refactor audit](../docs/REFACTOR-AUDIT-2026-10-22.md) proposes subfolders by subsystem.
- `common/`: pure logic with no Windows, D3D11, OpenXR or live engine pointers. That covers math, eye-camera construction, IK, hand and body pose, option specs, UI panel fitting, input event encoding and the like. It is built into `preyvr_core` and unit-tested in `tests/`. Its headers are in `include/preyvr/`.
- `lifecycle/`: the log every module writes through (`lifecycle::Log`).

Engine addresses live in three places:
- `include/preyvr/EngineMap.h`: the landmarks the bootstrap checks at startup, and layouts several modules share.
- The per-feature `include/preyvr/*Native.h` contract tables.
- File-local gates inside some `dll/` modules.
