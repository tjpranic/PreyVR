#include "equipment/body/BodyEquipment.h"
#include "TestSupport.h"
#include <iostream>
#include <cstdlib>
using namespace preyvr;
using namespace preyvr::equipment;
using preyvr::test::Check;
int main(){
 Pose head{};head.position={0,1.65f,0};
 auto world=[&](Vec3 p){auto v=Rotate(head.orientation,p);return Vec3{v.x+head.position.x,v.y+head.position.y,v.z+head.position.z};};
 HolsterGesture g;
 auto tick=[&](Vec3 p,bool grip,bool valid=true,float dt=.01f){return g.Update(head,p,grip,valid,dt);};
 const auto hip=world(Zones[0]),chest=world(Zones[1]);
 Check(tick(hip,true)==-1,"held on entry cannot stow");
 tick(hip,false);Check(tick(hip,true)==0&&g.OwnsGrip(),"fresh hip grip claims use/reload");
 Check(tick(chest,true)==-1&&g.OwnsGrip(),"held grip cannot activate second slot or leak use");
 tick(chest,false);Check(tick(chest,true)==1,"fresh chest grip selects distinct slot");
 tick(hip,false);Check(tick({0,1.5f,-.7f},true)==-1&&!g.OwnsGrip(),"ordinary reload grip untouched");
 Check(tick(hip,true)==-1,"drift into holster with held grip ignored");
 tick(hip,false);tick(hip,true);tick(hip,true,false);
 Check(tick(hip,true)==-1&&!g.OwnsGrip(),"tracking/modal cancellation needs release");
 tick(hip,false);Check(tick(hip,true,true,.3f)==-1,"frame stall disarms");
 Check(tick(hip,true)==-1,"stall cannot turn held grip into fresh grab");
 g.Reset();head.orientation={0,.70710678f,0,.70710678f};
 auto turnedHip=world(Zones[0]);tick(turnedHip,false);Check(tick(turnedHip,true)==0,"world yaw rotates body zones");
 head.orientation={0,0,0,1};g.Reset();tick(hip,false);
 // A small glance changes head yaw but does not move the reached hip zone.
 head.orientation={0,std::sin(.3f),0,std::cos(.3f)};
 Check(tick(hip,true)==0,"head glance does not drag reached zone");
 // The shared torso (TorsoLane): the slots sit on the torso the shoulders do,
 // whatever the head is doing, and still never move under a reaching hand.
 {
  head.orientation={0,0,0,1};g.Reset();
  const float left=1.5707963f;
  const auto torsoHip=[&](float yaw){auto v=Rotate(Quaternion{0,std::sin(yaw/2),0,std::cos(yaw/2)},Zones[0]);
   return Vec3{v.x+head.position.x,v.y+head.position.y,v.z+head.position.z};};
  g.Update(head,{0,1.5f,-.7f},false,true,.01f,&left);
  g.Update(head,torsoHip(left),false,true,.01f,&left);
  Check(g.Update(head,torsoHip(left),true,true,.01f,&left)==0,"slots follow the shared torso, not the head");
  g.Reset();const float ahead=0,turned=.5f;
  g.Update(head,hip,false,true,.01f,&ahead);
  Check(g.Update(head,hip,true,true,.01f,&turned)==0,"a reached slot is not dragged by the torso");
  const float none=NAN;g.Reset();g.Update(head,hip,false,true,.01f,&none);
  Check(g.Update(head,hip,true,true,.01f,&none)==0,"a non-finite torso falls back to the head estimate");
 }
 Check(DisplayValue(753.f,.1f)==76&&DisplayValue(0)==0,"HUD rounding and real zero preserved");
 Check(DisplayValue(NAN)==-1&&DisplayValue(-1)==-1,"unknown value distinct from zero");
 Pose eye{},card{};card.position={0,0,-.5f};
 Check(WristVisible(eye,card,false),"front face in gaze visible");
 card.orientation={0,1,0,0};Check(!WristVisible(eye,card,true),"back of wrist hidden");
 card={};card.position={0,0,-.1f};Check(!WristVisible(eye,card,true),"too near face hidden");
 card.position={.7f,0,-.1f};Check(!WristVisible(eye,card,false),"outside inspection gaze hidden");
 Pose grip{};grip.position={-.2f,-.35f,-.35f};const auto panel=WristPose(grip);
 Check(std::fabs(panel.position.z+.24f)<.0001f&&std::fabs(panel.position.y+.305f)<.0001f,"wrist anchor follows grip toward arm");
 std::cout<<"Holster neutral/ownership, torso geometry, wrist visibility and HUD conversion passed\n";
}
