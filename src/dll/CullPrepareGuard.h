#pragma once
#include <windows.h>

#include <cstdint>
#include <string>

namespace preyvr::dll {
// The cause of the cull lag behind the PreyDll+0x27EC40 crash, and its fix
// (docs/CULL-CAMERA-CRASH-2026-10-17.md, "The handshake").
//
// The occlusion consumer job starts from a two-party handshake on CCullThread
// (CObjManager+0x200, state at +4). C3DEngine::PrepareOcclusion kicks the
// z-buffer jobs (state 1, then 2); CullStart, from RenderWorld, parks in 3 when
// it comes first. Whichever comes second launches the job (4). CryAction's
// PostUpdate calls PrepareOcclusion(m_ViewCamera) and then CSystem::Render. Both
// skip while the gEnv+0x2AB no-world flag is set (a menu that hides the world,
// such as the pause menu, sets it) or the camera is within 5 cm of the origin,
// and each reads them at its own time. In the field it is the flag that changes
// in between: on the frame such a menu closes it is still set at the prepare and
// clear by the render, about 4 ms later (docs, "Field result"). When a frame's
// prepare is skipped and its render is not, CullStart waits for a prepare that
// never comes and that frame's cull batch stays queued. Every consumer run
// drains exactly one batch, so the culler then stays a frame behind for the rest
// of the session.
//
// The guard runs on the main thread, in CSystem::Render's detour before the
// original. If no prepare is in flight (state not 1 or 2) although
// PrepareOcclusion's own gates pass for the camera Render is about to use, it
// calls PrepareOcclusion itself: the engine's own call, a little later in the
// same frame. The observation side, which stays on with the guard off: a
// read-only hook on PrepareOcclusion records what it decided each frame, and the
// state around each render counts a batch orphaned (state 3 with no prepare in
// flight).
//
// Installed by the command worker once the module is pinned, VR or not.
// PREYVR_CULL_PREPARE=0 starts with the guard off; cull.prepare 0|1 switches it.
void ServiceCullPrepareGuard();
// cull.prepare: 1 guards, 0 only observes. Returns 0, 1 for a bad value, or the
// install failure code.
DWORD SetCullPrepareGuard(unsigned int enabled);
// CSystem::Render detour, main thread, just before the original (after the
// camera edit): `viewCamera` is CSystem::m_ViewCamera, the camera Render tests.
void CullPrepareBeforeRender(const void* viewCamera);
// The handshake state now, or kCullStateUnknown before install or when unreadable.
constexpr std::uint32_t kCullStateUnknown = 0xFF;
std::uint32_t CullThreadState();
// After the original: the states read just before and just after it.
void CullPrepareAfterRender(std::uint32_t before, std::uint32_t after);
std::string CullPrepareReport();
} // namespace preyvr::dll
