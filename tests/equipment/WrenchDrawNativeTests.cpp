// The wrench draw's native table: every named function has a hashed row, no row
// is listed twice or by another table, and what it uses that other tables
// already hash (SelectItem and the weapon lookup, EquipmentNative.h's) stays theirs.
#include "physics/contact/ContactNative.h"
#include "equipment/body/EquipmentNative.h"
#include "engine/NativeContract.h"
#include "physics/contact/PhysicalNative.h"
#include "physics/contact/SceneQueryNative.h"
#include "physics/swing/SwingNative.h"
#include "equipment/wrench/WrenchDrawNative.h"
#include "equipment/wrench/WrenchSoundNative.h"
#include "TestSupport.h"
#include <iostream>
using namespace preyvr;
using preyvr::test::Check;
namespace wn = preyvr::wrenchdraw::native;
namespace ws = preyvr::wrenchsound::native;
static_assert(contract::Gated(wn::Contracts,wn::PlayAction)&&contract::Gated(wn::Contracts,wn::OnEquip)&&
              contract::Gated(wn::Contracts,wn::SelectActionCtor)&&contract::Gated(wn::Contracts,wn::ActionBaseCtor)&&
              contract::Gated(wn::Contracts,wn::SelectEnter)&&contract::Gated(wn::Contracts,wn::SelectExit)&&
              contract::Gated(wn::Contracts,wn::SelectBegun)&&contract::Gated(wn::Contracts,wn::SelectDone)&&
              contract::Gated(wn::Contracts,wn::SelectFinished));
static_assert(contract::Gated(equipment::native::Contracts,wn::SelectItem)&&
              contract::Gated(equipment::native::Contracts,wn::WeaponLookup));
int main(){
    for(const auto& a:wn::Contracts){
        unsigned seen=0;
        for(const auto& b:wn::Contracts)seen+=a.rva==b.rva;
        Check(seen==1&&a.size>0,"each body is one row");
        Check(!contract::Gated(equipment::native::Contracts,a.rva)&&!contract::Gated(physical::native::Contracts,a.rva)&&
              !contract::Gated(swing::native::Contracts,a.rva)&&!contract::Gated(contact::native::Contracts,a.rva)&&
              !contract::Gated(scene::native::Contracts,a.rva),"no body hashed by two tables");
    }
    Check(wn::SelectBegunSlot==0x100&&wn::SelectDoneSlot==0x108&&wn::SelectFinishedSlot==0x110,"the weapon's select slots");
    Check(wn::EnterSlot==0x10&&wn::ExitSlot==0x20&&wn::ActionReleaseSlot==0xD0,"the action's Enter, Exit and release slots");
    Check(wn::ActionRefCount==0x50&&wn::ActionItemId==0x80&&wn::ActionSelectFlag==0x88&&wn::WeaponItemId==0x38,
          "the fields the skip reads");
    // The sound capture's table (WrenchSoundNative.h).
    for(const auto& a:ws::Contracts){
        unsigned seen=0;
        for(const auto& b:ws::Contracts)seen+=a.rva==b.rva;
        Check(seen==1&&a.size>0,"each sound body is one row");
        Check(!contract::Gated(wn::Contracts,a.rva)&&!contract::Gated(equipment::native::Contracts,a.rva)&&
              !contract::Gated(physical::native::Contracts,a.rva)&&!contract::Gated(swing::native::Contracts,a.rva)&&
              !contract::Gated(contact::native::Contracts,a.rva)&&!contract::Gated(scene::native::Contracts,a.rva),
              "no sound body hashed by two tables");
    }
    Check(ws::TriggerIdSlot==0x40&&ws::ExecuteTriggerSlot==0xD8&&ws::ProxyEntity==0x40,"the audio slots the capture checks");
    Check(ws::StartTrigger==0x08&&ws::StopTrigger==0x10&&ws::FireAndForget==0x20&&ws::SoundFlags==0x28&&ws::ClipEntity==0x08,
          "the PlaySound fields the capture reads");
    std::cout<<"wrench_draw_native: rows gated and unique\n";
}
