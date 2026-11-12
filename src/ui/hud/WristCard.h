#pragma once
#include "base/VrMath.h"
#include <cmath>

// **The wrist card on the controller**: where the status display sits when the
// hologram has no drawn forearm to stand on (WristHolo.h places it on the arm),
// and whether the wearer is looking at it. Pure, so every rule is tested offline.
namespace preyvr::wrist {
// Grip-local card: over the inner left wrist, extending back towards the arm.
// +Z is the visible face of an OpenXR quad. Palm-up inspection faces it at eyes.
inline Pose WristPose(Pose grip){return Compose(grip,Pose{{-.70710678f,0,0,.70710678f},{0,.045f,.11f}});}
// The three numbers the visibility test compares: eye distance, how squarely
// the card faces the eye, and how close to the gaze centre it sits (cosines).
struct WristView {float distance=0,facing=0,viewing=0;};
inline WristView MeasureWrist(Pose head,Pose wrist){
 const Vec3 toEye{head.position.x-wrist.position.x,head.position.y-wrist.position.y,head.position.z-wrist.position.z};
 const float d=std::sqrt(toEye.x*toEye.x+toEye.y*toEye.y+toEye.z*toEye.z);
 if(!std::isfinite(d)||d<=0)return {d,0,0};
 const auto face=Rotate(wrist.orientation,{0,0,1});
 const auto look=Rotate(head.orientation,{0,0,-1});
 return {d,(face.x*toEye.x+face.y*toEye.y+face.z*toEye.z)/d,-(look.x*toEye.x+look.y*toEye.y+look.z*toEye.z)/d};
}
inline constexpr float WristFacingNeeded(bool wasVisible){return wasVisible?.35f:.55f;}
inline constexpr float WristViewingNeeded(bool wasVisible){return wasVisible?.5f:.65f;}
inline bool WristVisible(Pose head,Pose wrist,bool wasVisible){
 const auto m=MeasureWrist(head,wrist);
 if(!std::isfinite(m.distance)||m.distance<.23f||m.distance>1.f)return false;
 return m.facing>WristFacingNeeded(wasVisible)&&m.viewing>WristViewingNeeded(wasVisible);
}
}
