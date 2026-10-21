#include "PhysicalInteractions.h"
#include "AimTakeover.h"
#include "ContactLane.h"
#include "SurfaceStopLane.h"
#include "SwingLane.h"
#include "Logger.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/TextFormat.h"
#include "preyvr/PhysicalInteractions.h"
#include "preyvr/StereoCamera.h"
#include <atomic>
#include <cmath>
#include <mutex>

// The weapon box, published from the animation takeover (AnimIkTakeover.cpp) for
// the drawn weapon, read by the contact lane and the free-swing wrench. The
// Physical Wrench that once lived here (a gesture gate, nine rays and Prey's own
// attack) was replaced by the free-swing wrench (SwingLane.cpp) on 2026-10-18.
namespace preyvr::dll {
namespace {
using namespace physical;
struct Geometry {Sample sample{};RigIdentity rig{};std::uint64_t published=0;bool wrench=false;Vec3 correction{};};
LatestSnapshot<Geometry> geometry;
std::mutex publicationMutex;
std::atomic<std::uint64_t> loggedRig{0};
void Log(const std::string& line){lifecycle::Log("preyvr_physical "+line);}
}
void PublishPhysicalWeapon(const GameplayPoseFrame& frame,const RigIdentity& rig,const animik::Location& location,
                           Pose wrist,const weaponrig::ContactGeometry& shape){
    if(!PhysicalGeometryWanted()||!frame.cameraCentreValid||!frame.headYawUsable||
       !std::isfinite(frame.yaw)||(frame.referenceGeneration&1)||
       !IsPoseUsable(frame.tracking.head,frame.tracking.headValidity,100000000)||
       !IsPoseUsable(frame.tracking.hands[1].gripPose,frame.tracking.hands[1].gripValidity,100000000)||
       frame.tracking.displayTime<=0||!std::isfinite(location.s)||location.s<=0||location.s>2)return;
    const Pose worldFromReference{{0,0,std::sin(frame.yaw*.5f),std::cos(frame.yaw*.5f)}, {}};
    Geometry g{};g.rig=rig;g.wrench=shape.wrench;g.correction=SurfaceOffset(frame,Hand::right);
    auto& s=g.sample;s.worldFromReference=worldFromReference;
    s.worldFromReference.position=Sub(frame.cameraCentre,Rotate(worldFromReference.orientation,stereo::ToEngineSpace(frame.tracking.head.position)));
    const auto referenceFromWorld=Inverse(s.worldFromReference);
    const Pose model=Compose(wrist,shape.weaponInWrist);
    const Pose world{Multiply(location.q,model.orientation),animik::ModelToWorld(location,model.position)};
    const auto local=Bounds(shape.minimum,shape.maximum);
    for(unsigned i=0;i<local.size();++i)s.points[i]=Transform(referenceFromWorld,Transform(world,Scale(local[i],location.s)));
    s.grip=stereo::ToEngineSpace(frame.tracking.hands[1].gripPose);
    s.sequence=frame.tracking.sequence;s.stamp=static_cast<std::uint64_t>(frame.tracking.displayTime);
    s.owner=rig.generation;s.epoch=frame.tracking.epoch;s.reference=frame.referenceGeneration;
    g.published=frame.tracking.publishedNs;
    // Once per equip: the bounds the box uses for this weapon.
    if(auto seen=loggedRig.load();seen!=rig.generation&&loggedRig.compare_exchange_strong(seen,rig.generation))
        Log("geometry rig="+std::to_string(rig.generation)+" wrench="+std::to_string(shape.wrench)+" min="+Point(shape.minimum)+
            " max="+Point(shape.maximum)+" scale="+Fixed(location.s,2));
    std::lock_guard lock(publicationMutex);
    Geometry previous{};
    if(geometry.TryRead(previous)&&previous.sample.owner==s.owner&&previous.sample.epoch==s.epoch&&
       previous.sample.reference==s.reference&&previous.sample.sequence>=s.sequence)return;
    geometry.Publish(g);
}
bool PhysicalGeometryWanted(){return PhysicalContactEnabled()||SwingLaneWanted()||(SurfaceStopEnabled()&&SurfaceStopWeapon());}
bool TryGetWeaponBox(WeaponBox& out){
    out={};Geometry g{};
    if(!geometry.TryRead(g))return false;
    for(unsigned i=0;i<g.sample.points.size();++i)out.points[i]=Transform(g.sample.worldFromReference,g.sample.points[i]);
    const auto q=g.sample.worldFromReference.orientation;
    out.yaw=2*std::atan2(q.z,q.w);
    out.rig=g.sample.owner;out.sequence=g.sample.sequence;out.reference=g.sample.reference;out.epoch=g.sample.epoch;
    out.publishedNs=g.published;out.stamp=g.sample.stamp;out.wrench=g.wrench;
    out.local=g.sample.points;out.worldFromReference=g.sample.worldFromReference;out.grip=g.sample.grip.position;
    out.gripPose=g.sample.grip;out.weapon=g.rig.weapon;out.correction=g.correction;return true;
}
void TrackedWeaponBox(WeaponBox& box){
    const Vec3 local=Rotate(Conjugate(box.worldFromReference.orientation),box.correction);
    for(auto& p:box.points)p=Sub(p,box.correction);
    for(auto& p:box.local)p=Sub(p,local);
    box.correction={};
}
}
