#pragma once

#include <cstdint>

// IArkPlayer::GetReticleViewPositionAndDir 0x157CBB0 (R-013): RCX = player+0x40,
// RDX = 6 floats out (origin, direction), returns RDX. Every native reader of the
// cached ray calls it, and two lanes rewrite what it returns: the shot scope
// (ShotRay) and psi targeting (PsiMedkit). A third hands a world UI's cursor
// raycast the crosshair's ray (AimTakeover).
//
// **One detour, several filters.** MinHook allows one hook per target. While each
// lane hooked the getter itself, whichever installed second failed: psi's
// body hash saw the shot lane's jump, or the shot lane's prologue check saw
// psi's. This owns the only detour; each lane registers a filter, and every
// filter runs after the native getter on the caller's thread.
namespace preyvr::dll {

enum class ReticleRayClient : unsigned { shot, psi, worldUi };

// receiver = RCX, out = RDX, result = the native return value, caller = the
// return address into the engine.
using ReticleRayFilter = void (*)(std::uintptr_t receiver, void* out, void* result, std::uintptr_t caller);

// Idempotent. Checks the whole 57-byte body against the `aim.get_cached_ray`
// landmark before patching it, so it gates exactly what psi's contract hash
// did. False when the body differs or MinHook refuses.
bool EnsureReticleRayHook();

// True once installed. The body was verified first and now starts with the
// detour's jump, so a later byte or hash check of it must not run.
bool ReticleRayHookInstalled();

// nullptr unregisters. Filters run in client order: shot, psi, then the world
// UI's, which only acts on its own caller.
void SetReticleRayFilter(ReticleRayClient client, ReticleRayFilter filter);

} // namespace preyvr::dll
