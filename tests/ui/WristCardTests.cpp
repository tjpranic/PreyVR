#include "ui/hud/WristCard.h"
#include "TestSupport.h"
#include <iostream>
#include <cstdlib>
using namespace preyvr;
using namespace preyvr::wrist;
using preyvr::test::Check;
int main(){
 Pose eye{},card{};card.position={0,0,-.5f};
 Check(WristVisible(eye,card,false),"front face in gaze visible");
 card.orientation={0,1,0,0};Check(!WristVisible(eye,card,true),"back of wrist hidden");
 card={};card.position={0,0,-.1f};Check(!WristVisible(eye,card,true),"too near face hidden");
 card.position={.7f,0,-.1f};Check(!WristVisible(eye,card,false),"outside inspection gaze hidden");
 Pose grip{};grip.position={-.2f,-.35f,-.35f};const auto panel=WristPose(grip);
 Check(std::fabs(panel.position.z+.24f)<.0001f&&std::fabs(panel.position.y+.305f)<.0001f,"wrist anchor follows grip toward arm");
 std::cout<<"Wrist card placement and visibility passed\n";
}
