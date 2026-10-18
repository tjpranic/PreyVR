// Physical contact's native table (phase 0): every named function has a hashed
// row, no row is listed twice, and the rigid rows it relies on are PhysicalNative's.
#include "preyvr/ContactNative.h"
#include "preyvr/NativeContract.h"
#include "preyvr/PhysicalNative.h"
#include "TestSupport.h"
#include <iostream>
using namespace preyvr;
using preyvr::test::Check;
namespace cn = preyvr::contact::native;
static_assert(contract::Gated(cn::Contracts,cn::GetEntitiesInBox)&&
              contract::Gated(cn::Contracts,cn::CollideEntityWithPrimitive)&&
              contract::Gated(cn::Contracts,cn::GetSurfaceParameters)&&
              contract::Gated(cn::Contracts,cn::RigidGetStatus)&&
              contract::Gated(cn::Contracts,cn::ArticulatedGetType)&&
              contract::Gated(cn::Contracts,cn::ArticulatedGetStatus)&&
              contract::Gated(cn::Contracts,cn::ArticulatedAction));
// Set velocity and impulses on a rigid body go through the rigid Action and
// GetType that the physical-melee gate already hashes.
static_assert(cn::RigidVtable==physical::native::RigidVtable&&
              contract::Gated(physical::native::Contracts,physical::native::RigidAction)&&
              contract::Gated(physical::native::Contracts,physical::native::RigidGetType));
int main(){
    for(const auto& a:cn::Contracts){
        unsigned seen=0;
        for(const auto& b:cn::Contracts)seen+=a.rva==b.rva;
        Check(seen==1&&a.size>0,"each body is one row");
        Check(!contract::Gated(physical::native::Contracts,a.rva),"no body hashed by two tables");
    }
    Check(cn::MovableObjects==(2|4),"broadphase asks for sleeping and awake rigid bodies only");
    Check(cn::SurfaceCount==512,"surface tables bounded as the getter bounds them");
    std::cout<<"contact_native: rows gated and unique\n";
}
