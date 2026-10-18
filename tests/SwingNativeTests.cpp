// The free-swing wrench's native table (phase 0): every named function has a
// hashed row, no row is listed twice or by another table, and what it calls or
// hooks that other tables already hash stays theirs.
#include "preyvr/ContactNative.h"
#include "preyvr/NativeContract.h"
#include "preyvr/PhysicalNative.h"
#include "preyvr/SceneQueryNative.h"
#include "preyvr/SwingNative.h"
#include "TestSupport.h"
#include <iostream>
using namespace preyvr;
using preyvr::test::Check;
namespace sn = preyvr::swing::native;
namespace pn = preyvr::physical::native;
static_assert(contract::Gated(sn::Contracts,sn::CollisionEventBuilder)&&contract::Gated(sn::Contracts,sn::AddDeferredEvent)&&
              contract::Gated(sn::Contracts,sn::SendSignalPackage)&&contract::Gated(sn::Contracts,sn::StaminaAvailable)&&
              contract::Gated(sn::Contracts,sn::StaminaCharge)&&contract::Gated(sn::Contracts,sn::GetSurfaceType));
// The hit itself, GetHits, the hit append and the impulse wrapper are the
// physical-melee gate's rows; so are the attack the free swing blocks and WeaponHit.
static_assert(contract::Gated(pn::Contracts,sn::ComponentHit)&&contract::Gated(pn::Contracts,pn::GetHits)&&
              contract::Gated(pn::Contracts,pn::AppendHit)&&contract::Gated(pn::Contracts,pn::WeaponImpulse)&&
              contract::Gated(pn::Contracts,pn::Attack)&&contract::Gated(pn::Contracts,pn::WeaponHit));
int main(){
    for(const auto& a:sn::Contracts){
        unsigned seen=0;
        for(const auto& b:sn::Contracts)seen+=a.rva==b.rva;
        Check(seen==1&&a.size>0,"each body is one row");
        Check(!contract::Gated(pn::Contracts,a.rva)&&!contract::Gated(contact::native::Contracts,a.rva)&&
              !contract::Gated(scene::native::Contracts,a.rva),"no body hashed by two tables");
    }
    Check((sn::StrikeObjects&contact::native::MovableObjects)==contact::native::MovableObjects&&(sn::StrikeObjects&1),
          "the strike's broadphase holds the movable classes and static scenery");
    Check(sn::ComponentOffset==0x4C8&&sn::DodgeFlag==0x3F&&sn::FatiguePerHit==0x38,"the component and the fields the hit reads");
    Check(sn::CollisionSpeed==100.f&&sn::EventCollision==2,"the collision Prey's hit sends");
    std::cout<<"swing_native: rows gated and unique\n";
}
