#pragma once
#include "Bc2BodyInventory.h"
#include "Bc2EquipmentIdentity.h"

namespace fvr::bc2 {
// A selected ordinary weapon whose physical hand key is its native item. This
// is deliberately not the persistent XM8/launcher alias and proves no reload,
// geometry, visibility or magazine configuration capability.
struct SelectedCarriedWeaponEvidence {
 ReloadStateOwner owner{};
 interaction::HandInteractionOwner physical{};
 interaction::HandInteractionKey weapon{};
 WeaponEquipmentIdentity equipment{};
 std::uint32_t inventory=0,switching=0,selectedSlot=0,category=20;
 std::uint64_t sequence=0;
 std::int64_t observedNs=0,deadlineNs=0;
 bool verified=false;
};
inline bool SelectedCarriedWeaponFresh(const SelectedCarriedWeaponEvidence& e,
 const ReloadStateOwner& o,const interaction::HandInteractionOwner& physical,
 interaction::HandInteractionKey weapon,std::int64_t now)noexcept {
 return e.verified&&e.owner==o&&e.physical==physical&&e.weapon==weapon&&e.sequence&&
  e.observedNs>0&&e.observedNs<=now&&e.deadlineNs>now&&e.deadlineNs-e.observedNs<=200000000&&
  o.player>=0x10000&&o.soldier>=0x10000&&o.weak>=0x10000&&o.weapon>=0x10000&&
  o.actorGeneration&&o.equipGeneration&&o.space&&
  physical.actor==((std::uint64_t(o.weak)<<32)|o.soldier)&&physical.actorGeneration==o.actorGeneration&&
  physical.space==o.space&&physical.equipGeneration&&weapon.generation==physical.equipGeneration&&weapon.id==o.weapon&&
  e.equipment.weapon==o.weapon&&e.equipment.data>=0x10000&&!e.equipment.Asset().empty()&&
  e.inventory>=0x10000&&e.switching>=0x10000&&e.selectedSlot<32&&e.category<=20;
}
// Caller must have already verified the native selector/layout binding before
// opting into these existing callback-only readers. Default remains disabled.
// The original input deadline is retained; neither read nor later use renews it.
inline std::optional<SelectedCarriedWeaponEvidence> ReadSelectedCarriedWeapon(
 const WeaponModeMemory& memory,const ReloadStateOwner& owner,
 const interaction::HandInteractionSample& input,interaction::HandInteractionKey weapon,
 bool bindingVerified=false)noexcept {
 if(!bindingVerified||!input.sequence||!input.focused||!input.tracked[0]||!input.tracked[1]||input.released[1]||
    input.observedNs<=0||input.observedNs>input.nowNs||input.deadlineNs<=input.nowNs||
    input.deadlineNs-input.observedNs>200000000||weapon.id!=owner.weapon||
    weapon.generation!=input.owner.equipGeneration)return {};
 const auto before=ReadBodyInventory(memory,owner,true);
 if(before.status!=BodyReadStatus::Okay||!before.snapshot)return {};
 const auto equipment=ReadWeaponEquipmentIdentity(memory,owner.weapon);
 if(!equipment)return {};
 const auto after=ReadBodyInventory(memory,owner,true);
 if(after.status!=BodyReadStatus::Okay||!after.snapshot||*before.snapshot!=*after.snapshot)return {};
 const auto equipmentAfter=ReadWeaponEquipmentIdentity(memory,owner.weapon);
 if(!equipmentAfter||*equipmentAfter!=*equipment)return {};
 const auto& inv=*after.snapshot;
 if(inv.selected>=inv.count)return {};
 const auto& selected=inv.items[inv.selected];
 if(selected.weapon!=owner.weapon||selected.data!=equipment->data||selected.persistence!=equipment->persistence||
    selected.slot!=inv.selected)return {};
 SelectedCarriedWeaponEvidence out{owner,input.owner,weapon,*equipment,inv.inventory,inv.switching,
  inv.selected,selected.category,input.sequence,input.observedNs,input.deadlineNs,true};
 return SelectedCarriedWeaponFresh(out,owner,input.owner,weapon,input.nowNs)?std::optional{out}:std::nullopt;
}
} // namespace fvr::bc2
