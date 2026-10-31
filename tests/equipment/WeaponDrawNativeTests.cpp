// The weapon draw's native table: every named function has a hashed row, no row
// is listed twice or by another table, what it uses that other tables already
// hash (SelectItem, the weapon lookup and the player, EquipmentNative.h's) stays
// theirs, and every weapon class's slots lead to gated bodies.
#include "physics/contact/ContactNative.h"
#include "equipment/body/EquipmentNative.h"
#include "engine/NativeContract.h"
#include "physics/contact/PhysicalNative.h"
#include "physics/contact/SceneQueryNative.h"
#include "physics/swing/SwingNative.h"
#include "equipment/weapon/WeaponDrawNative.h"
#include "equipment/weapon/WeaponSoundNative.h"
#include "TestSupport.h"
#include <cstdint>
#include <iostream>
#include <iterator>
using namespace preyvr;
using preyvr::test::Check;
namespace wn = preyvr::weapondraw::native;
namespace ws = preyvr::weaponsound::native;
static_assert(contract::Gated(wn::Contracts,wn::PlayAction)&&contract::Gated(wn::Contracts,wn::OnEquip)&&
              contract::Gated(wn::Contracts,wn::SelectActionCtor)&&contract::Gated(wn::Contracts,wn::ActionBaseCtor)&&
              contract::Gated(wn::Contracts,wn::SelectEnter)&&contract::Gated(wn::Contracts,wn::SelectExit)&&
              contract::Gated(wn::Contracts,wn::SelectBegun)&&contract::Gated(wn::Contracts,wn::SelectDone)&&
              contract::Gated(wn::Contracts,wn::SelectFinished));
static_assert(contract::Gated(equipment::native::Contracts,wn::SelectItem)&&
              contract::Gated(equipment::native::Contracts,wn::WeaponLookup)&&
              contract::Gated(equipment::native::Contracts,wn::GetPlayer));
static_assert(contract::Gated(wn::Contracts,wn::OnUnequip)&&contract::Gated(wn::Contracts,wn::NoOp)&&
              contract::Gated(wn::Contracts,wn::QBeamFinished)&&contract::Gated(wn::Contracts,wn::DisruptorDone)&&
              contract::Gated(wn::Contracts,wn::GrenadeDone));
namespace {
std::uint32_t TriggerId(const char* name)   // the ATL's id: ~CRC32 of the lowercased name
{
    std::uint32_t crc=0xFFFFFFFFu;
    for(;*name;++name){
        crc^=static_cast<std::uint8_t>(*name>='A'&&*name<='Z'?*name+32:*name);
        for(int k=0;k<8;++k)crc=crc&1?(crc>>1)^0xEDB88320u:crc>>1;
    }
    return ~crc;
}
}
int main(){
    for(const auto& a:wn::Contracts){
        unsigned seen=0;
        for(const auto& b:wn::Contracts)seen+=a.rva==b.rva;
        Check(seen==1&&a.size>0,"each body is one row");
        Check(!contract::Gated(equipment::native::Contracts,a.rva)&&!contract::Gated(physical::native::Contracts,a.rva)&&
              !contract::Gated(swing::native::Contracts,a.rva)&&!contract::Gated(contact::native::Contracts,a.rva)&&
              !contract::Gated(scene::native::Contracts,a.rva),"no body hashed by two tables");
    }
    Check(std::size(wn::WeaponClasses)==9,"the nine classes Prey registers");
    for(const auto& a:wn::WeaponClasses){
        unsigned seen=0;
        for(const auto& b:wn::WeaponClasses)seen+=a.vtable==b.vtable;
        Check(seen==1,"each weapon class is one row");
        Check(contract::Gated(wn::Contracts,a.begun)&&contract::Gated(wn::Contracts,a.done)&&
              contract::Gated(wn::Contracts,a.finished),"each class's slots lead to gated bodies");
    }
    Check(wn::SelectBegunSlot==0x100&&wn::SelectDoneSlot==0x108&&wn::SelectFinishedSlot==0x110,"the weapon's select slots");
    Check(wn::EnterSlot==0x10&&wn::ExitSlot==0x20&&wn::ActionReleaseSlot==0xD0,"the action's Enter, Exit and release slots");
    Check(wn::ActionRefCount==0x50&&wn::ActionItemId==0x80&&wn::ActionSelectFlag==0x88&&wn::WeaponItemId==0x38,
          "the fields the skip reads");
    Check(wn::WeaponAttacking==0x2C9&&wn::WeaponHandAttachment==0x2B0&&wn::WeaponFragments==0x470&&
          wn::PlayerEquipment==0x14B8&&wn::EquipmentCurrent==0x58,"the fields the stow reads");
    // The sound capture's table (WeaponSoundNative.h).
    for(const auto& a:ws::Contracts){
        unsigned seen=0;
        for(const auto& b:ws::Contracts)seen+=a.rva==b.rva;
        Check(seen==1&&a.size>0,"each sound body is one row");
        Check(!contract::Gated(wn::Contracts,a.rva)&&!contract::Gated(equipment::native::Contracts,a.rva)&&
              !contract::Gated(physical::native::Contracts,a.rva)&&!contract::Gated(swing::native::Contracts,a.rva)&&
              !contract::Gated(contact::native::Contracts,a.rva)&&!contract::Gated(scene::native::Contracts,a.rva),
              "no sound body hashed by two tables");
    }
    Check(ws::TriggerIdSlot==0x40&&ws::ExecuteTriggerSlot==0xD8&&ws::ProxyEntity==0x40,"the audio slots the replay checks");
    Check(ws::StartTrigger==0x08&&ws::StopTrigger==0x10&&ws::FireAndForget==0x20&&ws::SoundFlags==0x28&&ws::ClipEntity==0x08,
          "the PlaySound fields a capture reads");
    // Each weapon's sounds: every trigger with the id the audio system computes.
    Check(std::size(ws::ClassSounds)==11,"the eleven classes the capture heard");
    for(const auto& c:ws::ClassSounds){
        Check(!c.draw.empty()&&!c.stow.empty(),"each class's draw and stow play something");
        for(const auto& t:c.draw)Check(TriggerId(t.name)==t.id,"each draw trigger's id is its name's");
        for(const auto& t:c.stow)Check(TriggerId(t.name)==t.id,"each stow trigger's id is its name's");
    }
    for(const auto& t:ws::DefaultDraw)Check(TriggerId(t.name)==t.id,"each default draw trigger's id is its name's");
    for(const auto& t:ws::DefaultStow)Check(TriggerId(t.name)==t.id,"each default stow trigger's id is its name's");
    std::cout<<"weapon_draw_native: rows gated and unique\n";
}
