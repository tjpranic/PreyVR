#pragma once

// The read-only hook on R-071 CreateGeneralPassRenderingInfo, which CSystem::Render
// calls with `&m_ViewCamera`. It forwards every call unchanged and then hands the
// pass to the frame probe (FrameProbe.h ProbePassCreated). MinHook allows one
// detour per target, so this file stays the only owner of that function.
//
// It used to also compare the pass camera's forward axis with the one the camera
// edit wrote (the cull-camera question in CAM-002); that comparison was removed
// on 2026-10-23, unused.
namespace preyvr::dll {

// Installs the hook, gated on the `pass.create_general` landmark. Safe to call
// repeatedly; false when the landmark or its bytes do not match.
bool EnsurePassCreationHook();

} // namespace preyvr::dll
