// The weapon box's geometry (physics/contact/PhysicalInteractions.h) and the short scene
// query the free-swing wrench's line of sight uses.
#include "physics/contact/PhysicalInteractions.h"
#include "TestSupport.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace preyvr;using namespace preyvr::physical;
using preyvr::test::Check;
int main(){
    const auto p=Bounds({-.05f,0,-.05f},{.05f,.6f,.05f});
    Check(p[0].y==.3f&&p[0].x==0&&p[1].x==-.05f&&p[2].x==.05f&&p[3].y==.6f&&p[5].z==.05f,"centre, then corners by bit");
    const Pose map{{0,0,std::sin(.5f),std::cos(.5f)},{1,2,3}};
    const Vec3 v{.3f,-.2f,.1f};
    const auto back=Transform(Inverse(map),Transform(map,v));
    Check(std::fabs(back.x-v.x)<1e-5f&&std::fabs(back.y-v.y)<1e-5f&&std::fabs(back.z-v.z)<1e-5f,"a map and its inverse");
    scene::Params params{};scene::Hit h{};void* skip[]={reinterpret_cast<void*>(1),reinterpret_cast<void*>(2)};
    Check(scene::Build(params,{}, {1,0,0},h,skip,1,.125f)&&params.delta.x==.125f&&params.objectTypes==scene::AllObjects,
          "short query is not the reticle's 200m ray");
    Check(!scene::Build(params,{}, {1,0,0},h,skip,1,std::numeric_limits<float>::quiet_NaN()),"invalid range rejected");
    Check(scene::Build(params,{}, {0,0,1},h,skip,2,1.5f,scene::SceneryObjects)&&params.objectTypes==0x101&&params.skipCount==2,
          "scenery alone, skipping the player and the target");
    std::cout<<"Weapon box geometry and short queries passed\n";
}
