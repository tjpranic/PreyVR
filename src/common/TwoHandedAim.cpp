#include "preyvr/TwoHandedAim.h"
#include <algorithm>
#include <cmath>

namespace preyvr::twohand {
namespace {
bool Valid(Quaternion q) { const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w; return std::isfinite(n)&&n>.81f&&n<1.21f; }
Quaternion Mix(Quaternion a,Quaternion b,float t) {
    float d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(d<0) { b={-b.x,-b.y,-b.z,-b.w};d=-d; }
    float x=1-t,y=t;
    if(d<.9995f) { const float angle=std::acos(std::clamp(d,0.f,1.f)); const float s=std::sin(angle);
        x=std::sin((1-t)*angle)/s;y=std::sin(t*angle)/s; }
    return Normalize({a.x*x+b.x*y,a.y*x+b.y*y,a.z*x+b.z*y,a.w*x+b.w*y});
}
Quaternion Swing(Vec3 a,Vec3 b) {
    a=Scale(a,1/std::sqrt(Dot(a,a))); b=Scale(b,1/std::sqrt(Dot(b,b)));
    const float d=std::clamp(Dot(a,b),-1.f,1.f);
    if(d<-.9999f) {
        // Stable 180-degree branch; no world-up singularity for vertical aim.
        const Vec3 axis=std::fabs(a.x)<.8f?Vec3{1,0,0}:Vec3{0,0,1};
        const Vec3 perpendicular=Sub(axis,Scale(a,Dot(axis,a)));
        return Normalize({perpendicular.x,perpendicular.y,perpendicular.z,0});
    }
    return Normalize({a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x,1+d});
}
bool ValidInput(const Input& i) {
    return i.usable&&i.owner&&Finite(i.primary)&&Finite(i.support.position)&&
        Finite(i.visualPrimaryOffset)&&Valid(i.aim.orientation)&&Valid(i.support.orientation)&&
        std::isfinite(i.squeeze)&&i.squeeze>=0&&i.squeeze<=1&&std::isfinite(i.dt)&&i.dt>0&&i.dt<=.2f;
}
// A handgun's cup: ahead of the trigger wrist, within kHandgunReach, never on it.
bool HandgunCup(Vec3 s) { const float length=Dot(s,s); return s.y>0&&length>=.0016f&&length<=kHandgunReach*kHandgunReach; }
}
bool ClassifySocket(Vec3 s,bool wide,Grip& grip) {
    if(!Finite(s)) return false;
    const float side=wide?.45f:.25f, ahead=wide?.8f:.7f;
    if(s.y>=.16f&&s.y<=ahead&&std::fabs(s.x)<side&&std::fabs(s.z)<side) { grip=Grip::foregrip;return true; }
    if(HandgunCup(s)) { grip=Grip::handgun;return true; }
    return false;
}
bool ClosestGrip(const Input& i,Vec3& point) {
    if(!ValidInput(i)||!Finite(i.region.start)||!Finite(i.region.end)||
       !std::isfinite(i.region.radius)||i.region.radius<=0||i.region.radius>.25f) return false;
    const Vec3 local=Rotate(Conjugate(i.aim.orientation),Sub(i.support.position,Add(i.primary,i.visualPrimaryOffset)));
    const Vec3 segment=Sub(i.region.end,i.region.start);
    const float n=Dot(segment,segment);
    const float t=n>1e-8f?std::clamp(Dot(Sub(local,i.region.start),segment)/n,0.f,1.f):0;
    point=Add(i.region.start,Scale(segment,t));
    const bool inside=Dot(Sub(local,point),Sub(local,point))<=i.region.radius*i.region.radius;
    if(i.region.grip==Grip::handgun) return inside&&HandgunCup(point);
    // Long-weapon support only. A native offhand beside/behind the grip is not a foregrip.
    const float length=Dot(point,point);
    return point.y>=.12f&&length>=.0144f&&length<=.64f&&inside;
}
float HoldBack(Vec3 socket,const LongReach& reach) {
    if(!Finite(socket)||!std::isfinite(reach.comfortForward)||!std::isfinite(reach.maxHoldBack)) return 0;
    return std::clamp(socket.y-reach.comfortForward,0.f,std::max(reach.maxHoldBack,0.f));
}
Region SupportRegion(Vec3 socket,float holdBack,const LongReach& reach,Grip grip) {
    Region r{};r.grip=grip;
    if(grip==Grip::handgun) { r.start=r.end=socket;r.radius=kHandgunGrab;return r; }
    const float back=.04f+(holdBack>0&&std::isfinite(reach.supportBack)?std::clamp(reach.supportBack,0.f,.3f):0.f);
    r.start={socket.x,socket.y-back,socket.z};r.end={socket.x,socket.y+.04f,socket.z};
    return r;
}
bool RegionLatch::Update(std::uint64_t owner,bool candidateValid,Vec3 candidate,Vec3& socket) {
    if(owner!=owner_) { *this={};owner_=owner; }
    if(!owner) return false;
    if(!candidateValid||!Finite(candidate)) steady_=0;
    else {
        const Vec3 d=Sub(candidate,anchor_);
        if(steady_>0&&Dot(d,d)<=kSettleMetres*kSettleMetres) ++steady_;
        else { anchor_=candidate;steady_=1; }
        if(!latched_&&steady_>=kSettleSamples) { latched_=true;socket_=anchor_; }
    }
    if(latched_) socket=socket_;
    return latched_;
}
// The range auto-snap attaches from: the grab region at the attach distance.
bool InSnapRange(const Input& i,Vec3& point) {
    Input near=i;near.region.radius=i.snapAttach;
    return ClosestGrip(near,point);
}
Output Solver::Update(const Input& i) {
    Output out{};out.orientation=i.aim.orientation;
    const bool changed=i.owner!=owner_||i.reference!=reference_||i.epoch!=epoch_||i.toggleGrip!=toggleGrip_||
        i.autoSnap!=autoSnap_;
    if(changed||!ValidInput(i)) {
        *this={}; owner_=i.owner;reference_=i.reference;epoch_=i.epoch;
        toggleGrip_=i.toggleGrip;autoSnap_=i.autoSnap;
        // A weapon switch, modal transition or tracking loss requires a new squeeze.
        armed_=ValidInput(i)&&i.squeeze<.45f;
        return out;
    }
    const auto attach=[&] {
        held_=true;
        handgun_=i.region.grip==Grip::handgun;
        supportInAim_=Multiply(Conjugate(i.aim.orientation),i.support.orientation);
    };
    const bool wasHeld=held_;
    if(i.squeeze<.45f) { if(!i.toggleGrip&&!byProximity_)held_=false;armed_=true; }
    else if(armed_&&i.squeeze>=.65f) {
        armed_=false;
        if(held_&&i.toggleGrip) held_=false;
        else if(held_) byProximity_=false;   // the squeeze takes the hold over
        else if(ClosestGrip(i,socket_)) attach();
    }
    const Vec3 line=Sub(i.support.position,Add(i.primary,i.visualPrimaryOffset));
    // A handgun's hands sit closer than this; only a steering foregrip spins.
    if(held_&&!handgun_&&Dot(line,line)<.0144f) { held_=false;armed_=false;blend_=0;swing_={}; }
    // A support hand gone from a handgun lets go, and blends out. A snapped hold
    // goes by the break distance instead, on any weapon.
    if(held_&&handgun_&&!byProximity_&&Dot(line,line)>kHandgunBreak*kHandgunBreak) { held_=false;armed_=false; }
    if(held_&&byProximity_) {
        const Vec3 off=Sub(Rotate(Conjugate(i.aim.orientation),line),socket_);
        const float limit=std::max(i.snapBreak,i.snapAttach+kAutoSnapMargin);
        if(Dot(off,off)>limit*limit) held_=false;
    }
    if(wasHeld&&!held_) { byProximity_=false;waitForExit_=true; }
    if(i.autoSnap&&!held_) {
        Vec3 point{};
        const bool inRange=InSnapRange(i,point);
        if(!inRange) waitForExit_=false;
        dwell_=inRange&&!waitForExit_&&i.squeeze<.45f?dwell_+i.dt:0;
        if(dwell_>=kAutoSnapDwell) { dwell_=0;socket_=point;byProximity_=true;attach(); }
    }
    if(held_&&!handgun_) {
        // Raw controllers steer. The displayed support hand never feeds back here.
        const Vec3 localLine=Rotate(Conjugate(i.aim.orientation),line);
        swing_=Swing(socket_,localLine);
    }
    const float alpha=ExpAlpha(i.dt,25.f);
    blend_+=(held_?1-blend_:-blend_)*alpha;
    if(!held_&&blend_<.001f) { blend_=0;swing_={}; }
    const Quaternion local=Mix({},swing_,blend_);
    out.orientation=Normalize(Multiply(i.aim.orientation,local));
    out.correction=Normalize(Multiply(out.orientation,Conjugate(i.aim.orientation)));
    out.supportOrientation=Mix(i.support.orientation,Normalize(Multiply(out.orientation,supportInAim_)),blend_);
    out.socket=socket_;out.blend=blend_;out.held=held_;out.snapped=byProximity_;
    return out;
}
} // namespace preyvr::twohand
