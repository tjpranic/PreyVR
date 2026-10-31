# PreyVR

> Read `CLAUDE.md` in this folder once as well; the pair is one instruction set.
> Current user scope and the project-specific restrictions below apply throughout.

VR modding project for Prey (2017). Target: `Prey.exe` (64-bit, CryEngine/Arkane, D3D11).

## Engine source for this target

**CryEngine source is on this machine:** `D:\Dev Debug\source code\CRYENGINE`. Four CryEngine trees are on this machine; **`CryGame` is CryEngine 3 and closest to Prey**. `CCamera::SetAsymmetry` gives the engine native asymmetric frusta, and `IStereoRenderer.h`/`D3DStereo.cpp` show how Crytek did per-eye rendering. Prey is an Arkane fork, so this is structure and vocabulary, never offsets. See playbook ch00 `#local-engine-sources`. **Read ch00 `#cryengine-asymmetry-contract` first:** the source builds the projection as `Frustum(wL + GetAsymL(), ...)` - the same `wL/wR/wB/wT` your RE named `fWL/fWR/fWB/fWT`. It also shows the near/viewmodel pass rescaling every shift by `DRAW_NEAREST_MIN / nearPlane`, and `CD3D9Renderer::RT_EndFrame` - the function you hook - is declared in `RenderDll/Common/Renderer.h`.

## RE workflow and tool discovery

Read [the shared RE workflow](<D:/Dev Debug/VR Modding/AGENT_RE_WORKFLOW.md>) once when choosing an RE approach.
Use `vr-re-workflow` for the method and `re-mcp-toolkit` for the selected tool's
preflight and caveats. Read their `SKILL.md` files directly if there is no skill invoker.
The session's discoverable tools and schemas determine what is callable; a configured
server, a responding host and the correct open program/process are separate checks.

Useful tool families: Ghidra for static contracts; ReGenny / `cheatengine` for live
layouts; Frida / x64dbg or x32dbg for execution; RenderDoc / apitrace for graphics
evidence. `local-llm` is optional bounded assistance: probe once, fall back if offline,
and verify its output against the original evidence. A second model is not proof.
Choose tools by the question, not by a fixed server count or a two-tool quota.

Respect current user scope and exclusive runtime ownership. A closed app or missing
launch script is not proof that automation is impossible: check the existing runbook,
launcher and supported CLI/API within that scope. Keep static work moving when live
work is unavailable. Static contracts can be proved from bytes; runtime acceptance
and headset quality require their own evidence.

## xr-sim and xr-tape

`D:/Dev Debug/xr-sim` supplies a headset-free OpenXR runtime;
`D:/Dev Debug/xr-tape` records submitted poses, FOVs, layers and timing. Read their
current launcher scripts for process-scoped setup; this target needs the **x64**
loader/layer. Select runtime and layer per process, preserving shared machine state.
Use existing project launch/injection paths within the user's runtime authorization.
A trace checks the submission contract; it does not prove the game rendered those
views, accepted an input event, or looks correct in a headset. Correlate frames with
mod logs and graphics evidence.

`preyvr_xr_session_probe` and `HEADLESS_TESTING.md` are starting points. Quote the current trace and applicable checks, not a historical pass count.

## Target tool constraints

`Prey.exe` is Prey 2017, **x64 / CryEngine-derived / D3D11**. Use x64dbg only
within runtime scope. D3D11 support alone does not establish safe capture: read the
toolkit's Prey NVAPI/RenderDoc failure notes before a new capture attempt. Existing
captures and offline byte verifiers remain useful independently of that failure.
For static work, [the investigation guide](docs/RE-INVESTIGATION-GUIDE.md) and its
H-021 examples cover receiver bases, concrete virtual callees, writers and consumers.

## Fleet prior art

Before choosing a seam or repeating an experiment, route through
`D:/Dev Debug/VR Modding/docs/bottleneck-map.md` for this project's gate, or
`failure-atlas.md` / `symptom-index.md` for its symptom. Read only the relevant row
and linked section. The project's current receipts outrank a stale fleet summary.
State hypothesis, control, variable and decision rule before experimental code.
Record transferable results in the playbook when they change the route.

To find sibling documents use `cross-project-index.md`, scoped `rg`, and sibling
failure registries. Cross-project links are leads, not confirmation for this target.
External prior art is indexed in the playbook's `sources.yml` / `docs/coverage.md`.


<!-- vr-research-integration:start -->
## Research receipts and playbook contributions

For a substantial VR research finding or failed approach, read
`D:/Dev Debug/VR Modding/docs/research-receipts.md` and use the
`vr-research-receipts` skill when available. `tools/research_receipt.py` in the
playbook creates/validates a project-owned receipt and queues a candidate for review.
Keep validity, fact verdict, baseline health, evidence grade and observation environment
separate. Raw evidence stays in this project; intake never promotes claims or clears
bottlenecks. Use the numerical camera-mapping recipe and `tools/research_checks.py`
when that is the cheapest relevant proof. Existing runtime/launch restrictions apply.
<!-- vr-research-integration:end -->
