#pragma once

#include "preyvr/RenderContract.h"
#include "preyvr/SlotFeedback.h"
#include "preyvr/TwoHandedAim.h"
#include "preyvr/WeaponAim.h"

#include <windows.h>

#include <array>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

// In-game debug overlay (`dbg.draw`): each hand's ray, the body slots, the
// wrist card test and the foregrip region, drawn into the submitted eye images.
//
// **Drawn into the eye image, with the contract that rendered it.** Every
// primitive is in the app's OpenXR space and is projected with the same eye
// pose and frustum the image is submitted with, at the moment that image is
// copied out of Prey's backbuffer. So in the headset each line lands on the
// physical point it describes, both eyes agree, and any capture of the
// submitted images (the mock, xr-tape) shows exactly what the wearer saw. It
// needs no engine debug renderer and changes no game state.
//
// The debug layers are off by default; the aim lasers (two of its layers) are
// switched on at VR start. With nothing drawn every producer returns after one
// atomic load and the renderer is never reached.
namespace preyvr::dll {
struct GameplayPoseFrame;
struct TrackingFrame;

// Settings and diagnostics (command channel). The debug layers and the aim
// lasers are separate switches; what is drawn is both (dd::DrawnLayers).
DWORD SetDebugOverlay(unsigned mask);
unsigned DebugOverlayMask();        // the debug layers alone, without the lasers
DWORD SetAimLasers(unsigned enabled);
unsigned AimLasersEnabled();
std::string DebugOverlayReport();   // counters and the status text
std::string DebugOverlayMarks();    // primitive positions, OpenXR space and last projected pixels

// Producers. All return immediately when the overlay is off.
// Main thread, from the aim takeover, after its sample is final.
void DebugOverlayAim(const GameplayPoseFrame& frame, const aim::Sample& sample);
// Main thread, once per gameplay frame (left ray).
void DebugOverlayGameFrame(const GameplayPoseFrame& frame, bool tracking);
// Main thread, from the two-hand solver.
void DebugOverlayForegrip(const GameplayPoseFrame& frame, const twohand::Input& input,
                          const twohand::Output& output, bool regionReady);
// Holster input lane. `centres`/`valid` describe the gesture's torso estimate.
void DebugOverlayHolsters(bool live, const std::array<Vec3, 2>& centres, Vec3 hand, bool gripPressed,
                          bool armed, bool owned);
void DebugOverlayHolsterSlots(bool hipStored, bool chestStored);
// Medkit lane.
void DebugOverlayMedkit(bool live, Vec3 centre, Vec3 hand, bool gripPressed, bool armed, bool owned,
                        bool triggerArmed);
// Any slot notice, whether or not belt hints are shown.
void DebugOverlayNotice(equipment::SlotNotice notice);
// Physical-contact sweep (PhysicalInteractions), main thread, once
// per gameplay frame while DebugOverlayWantsPhysical(). Engine world space.
struct PhysicalOverlayState {
    bool box = false;                 // `points` is the published weapon box
    bool open = false;                // every gate passed: a swing can query
    std::array<Vec3, 9> points{};     // centre, then the eight corners
    bool swept = false;               // this frame queried from[i] -> to[i]
    std::array<Vec3, 9> from{}, to{};
    bool contact = false, acted = false;
    Vec3 point{}, normal{};
    const char* gate = "";
    const char* result = "";
    std::string status;
};
bool DebugOverlayWantsPhysical();
void DebugOverlayPhysical(const GameplayPoseFrame& frame, const PhysicalOverlayState& state);
// Physical contact (ContactLane), same thread and condition. Engine world space.
// Its box is drawn when the wrench's is not; its contacts always.
struct ContactOverlayState {
    bool box = false, open = false;
    std::array<Vec3, 9> points{};
    // The hands' boxes, right then left, when they push (physical::Bounds order).
    std::array<bool, 2> handBox{};
    std::array<std::array<Vec3, 9>, 2> hands{};
    // The probe under the player's feet, when it ran; `standing` when it or the
    // engine found something underfoot.
    bool feet = false, standing = false;
    std::array<Vec3, 9> feetBox{};
    struct Mark { Vec3 point{}, normal{}; float dist = 0; bool sweep = false, articulated = false; Vec3 push{}; bool pushed = false; };
    std::array<Mark, 16> marks{};
    unsigned count = 0;
    const char* gate = "";
    std::string status;
};
void DebugOverlayContact(const GameplayPoseFrame& frame, const ContactOverlayState& state);
// Surface stop (SurfaceStopLane), same thread and condition. Engine world space.
// Each hand's real grip and where it is drawn, by Hand; the surfaces met.
struct SurfaceOverlayState {
    struct HandMark { bool active = false, held = false; Vec3 real{}, drawn{}; };
    std::array<HandMark, 2> hands{};
    struct Mark { Vec3 point{}, normal{}; };
    std::array<Mark, 16> marks{};
    unsigned count = 0;
    std::string status;
};
void DebugOverlaySurface(const GameplayPoseFrame& frame, const SurfaceOverlayState& state);
// XR frame thread: the wrist layer's own decision this frame.
void DebugOverlayWristDecision(bool gate, bool visible);
// XR frame thread: the forearm hologram's card and its look test (WristLane);
// drawn instead of the grip card while it is fresh.
void DebugOverlayWristCard(bool gate, bool visible, const Pose& card, float width, float height, float distance,
                           float facing, float viewing, float facingNeeded, float viewingNeeded);

// Render thread, inside the XR frame: draws into `eyeImage` (which must be a
// render target) using the contract that produced it. `shownTangents` (left,
// right, up, down; may be null) is the frustum the runtime displays, which
// keeps text inside what the wearer can see. Restores every piece of pipeline
// state it touches.
void DrawDebugOverlay(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* eyeImage,
                      const RenderContract& contract, const float* shownTangents);
// Render thread, at XR teardown.
void ReleaseDebugOverlay();
}  // namespace preyvr::dll
