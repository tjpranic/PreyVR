// The weapon box's publication (PhysicalInteractions.cpp): published only while a
// lane wants it, read back in the world and in tracking space with its grip and
// weapon, never replaced by an older sample, its bounds logged once per equip.
#include "../src/dll/PhysicalInteractions.cpp"
#include "TestSupport.h"
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
std::vector<std::string> logLines;
namespace preyvr::lifecycle {void Log(std::string_view line){logLines.emplace_back(line);}}
namespace {
unsigned Count(const char* a){unsigned n=0;for(const auto& l:logLines)n+=l.find(a)!=std::string::npos;return n;}
bool wanted=false;
}
namespace preyvr::dll {
unsigned PhysicalContactEnabled(){return 0;}
bool SwingLaneWanted(){return wanted;}
}
int main(){
    using namespace preyvr;using namespace preyvr::dll;using preyvr::test::Check;
    GameplayPoseFrame f{};f.player=1;f.cameraCentreValid=true;f.headYawUsable=true;f.referenceGeneration=2;f.cameraCentre={10,20,1.7f};
    f.tracking.epoch=1;f.tracking.sequence=1;f.tracking.displayTime=1000000000;f.tracking.publishedNs=MonotonicNanoseconds();
    f.tracking.hands[1].gripValidity={true,true,true,true,0};f.tracking.headValidity={true,true,true,true,0};
    f.tracking.hands[1].gripPose.position={.2f,-.3f,-.4f};
    RigIdentity rig{0x1234,10,11,12,1,42};
    animik::Location location{};location.s=1;
    weaponrig::ContactGeometry shape{};shape.wrench=true;shape.minimum={-.06f,-.06f,-.1f};shape.maximum={.06f,.06f,.34f};
    WeaponBox box{};
    PublishPhysicalWeapon(f,rig,location,Pose{{},{0,0,0}},shape);
    Check(!TryGetWeaponBox(box),"no lane wants it: nothing published");
    wanted=true;
    PublishPhysicalWeapon(f,rig,location,Pose{{},{0,0,0}},shape);
    Check(TryGetWeaponBox(box)&&box.wrench&&box.weapon==0x1234&&box.rig==1&&box.sequence==1&&box.reference==2&&box.epoch==1,
          "published, with its weapon, equip and sample");
    Check(std::fabs(box.points[0].z-box.local[0].z-1.7f)<.5f&&box.local.size()==9,"world points and tracking-space points");
    const auto toWorld=physical::Transform(box.worldFromReference,box.local[3]);
    Check(std::fabs(toWorld.x-box.points[3].x)<1e-4f&&std::fabs(toWorld.y-box.points[3].y)<1e-4f&&std::fabs(toWorld.z-box.points[3].z)<1e-4f,
          "the map takes tracking space to the world");
    Check(std::fabs(physical::Length(physical::Sub(box.points[8],box.points[1]))-std::sqrt(.12f*.12f*2+.44f*.44f))<1e-3f,
          "the bounds' diagonal survives the trip");
    Check(Count("preyvr_physical geometry rig=1 wrench=1")==1,"bounds logged once per equip");
    f.tracking.sequence=0;
    PublishPhysicalWeapon(f,rig,location,Pose{{},{.5f,0,0}},shape);
    WeaponBox again{};TryGetWeaponBox(again);
    Check(again.sequence==1&&again.points[0].x==box.points[0].x,"an older sample never replaces a newer one");
    f.tracking.sequence=2;
    PublishPhysicalWeapon(f,rig,location,Pose{{},{.5f,0,0}},shape);
    TryGetWeaponBox(again);
    Check(again.sequence==2&&Count("preyvr_physical geometry")==1,"a newer one does; the same equip logs nothing more");
    std::cout<<"Weapon box publication passed\n";
}
