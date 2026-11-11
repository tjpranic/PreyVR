#pragma once

#include "base/VrMath.h"

#include <windows.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// The status hologram on the left forearm (`wrist.*`; pure rules in
// ui/hud/WristHolo.h). Three threads, one card:
//
//   animation  PublishWristArm: the left forearm and the right hand as skinned
//              for a frame, in OpenXR space, keyed by that frame's display time
//   game       UpdateWristLane: while the card is a candidate, scene segments
//              from the camera to the card -- the arm in a wall hides it
//   XR         DecideWrist: the arm of the frame being submitted, the gaze
//              test, the fade; what the session host composites
//
// The card is placed from the arm of the SAME frame as the eye images it is
// composited over, so it stays on the sleeve however fast the arm moves.
namespace preyvr::dll {
struct GameplayPoseFrame;
struct TrackingFrame;

struct WristArmFrame {
    long long displayTime = 0;
    std::uint64_t publishedNs = 0;
    bool left = false;          // the left forearm below is valid
    Vec3 wrist{}, elbow{}, palm{};
    bool right = false;         // the right hand below is valid
    Vec3 rightWrist{}, rightGrip{};
    Vec3 rightAim{};            // unit, the right controller's aim direction
    bool weapon = false;        // the right hand holds a weapon (not the free arms)
};
// Animation thread (AnimIkTakeover), once per owned frame.
void PublishWristArm(const WristArmFrame& frame);

// Main thread, once per gameplay frame.
void UpdateWristLane(const GameplayPoseFrame& frame, bool tracking);

struct WristDecision {
    bool active = false;     // composite a card this frame
    Pose pose{};             // OpenXR quad pose (app space): the frame's centre
    float width = 0, height = 0;
    float alpha = 0;         // fade, applied to the texture
    bool capture = false;    // ask the HUD for the native status this frame
};
// XR thread, once per submitted frame. `eligible` carries the host's gates
// (gameplay input, no menu/panel, not aiming two-handed, tracking usable);
// `submittedTime` is the display time the eye images were rendered for. The
// card's size is the wrist frame's at Wrist Display Size (wrist::Size), and its
// centre is placed so the meters sit where the placement puts them.
WristDecision DecideWrist(bool eligible, const TrackingFrame& latest, long long submittedTime,
                          long long frameTime);
// Legacy grip-anchored card (Funk's prototype) instead of the hologram.
bool WristLegacyAnchor();
// XR thread: the host's gate as bits (1 option, 2 views, 4 rendered, 8 no menu
// panel, 16 no VR options, 32 gameplay input, 64 not two-handed, 128 no
// recentre in flight, 256 head and left hand tracked), for wrist.report.
void NoteWristGate(unsigned bits);
// XR thread: a capture's content box as the texture read it back
// (NativeWristTexture::Content), for wrist.report content= and outside=.
void NoteWristContent(bool empty, const float (&box)[4], const float (&frame)[4], unsigned long long serial);
// Whether the wrist shows the psi meter, as Prey would (NativeWristNative.h
// PsiShown, read on the main thread): 1, 0, or -1 while it cannot be read
// (shown then, as before). Any thread.
int WristPsiShown();
// The status meters leave the gameplay HUD while the wrist shows them.
bool WristMovesStatusOffFace();
// wrist.haptic: the faint tick as the display appears.
DWORD SetWristTick(unsigned enabled);
unsigned WristTickEnabled();
// wrist.place, one value each: along the forearm (mm, -50..200), above it
// (mm, 0..150), and the most it turns toward the eyes (degrees, 0..90). Its
// size is Wrist Display Size's alone.
DWORD SetWristAlongMm(int mm);
DWORD SetWristHoverMm(int mm);
DWORD SetWristTiltDegrees(int degrees);
int WristAlongMm();
int WristHoverMm();
int WristTiltDegrees();
void ResetWristLane();

std::string WristReport();
// wrist.* verbs; false if the verb is not one of them.
bool ExecuteWristCommand(const std::vector<std::string>& args, std::ostringstream& out);
}  // namespace preyvr::dll
