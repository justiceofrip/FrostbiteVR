#pragma once
#include "../../../profiles/weapons46-special/FixedContainer.generated.h"
#include "Bc2AmmoResourceService.h"
#include "fvr/interaction/AmmoSupplyVisual.h"

namespace fvr::bc2 {
using FixedContainerEquipmentProfile=generated::FixedContainerEquipmentProfile;
inline const FixedContainerEquipmentProfile* KnownFixedContainerEquipment(const FixedContainerEquipmentProfile* p)noexcept {
    for(const auto& e:generated::FixedContainerEquipment)if(p==&e)return p;return nullptr;
}
inline const FixedContainerEquipmentProfile* FindFixedContainerEquipment(std::string_view config,std::string_view mesh,std::uint64_t rig)noexcept {
    return generated::FindFixedContainerEquipment(config,mesh,rig);
}
inline bool FixedContainerHandOwner(const AmmoResourceBinding& b,const interaction::HandInteractionOwner& h)noexcept {
    return h.actor==((std::uint64_t(b.owner.weak)<<32)|b.owner.soldier)&&h.actorGeneration==b.owner.actorGeneration&&
        h.equipGeneration&&h.space==b.owner.space;
}
struct FixedContainerBodyAmmo {
    const FixedContainerEquipmentProfile* equipment=nullptr;
    AmmoResourceBinding binding{};
    interaction::AmmunitionSnapshot ammunition{};
};
inline std::optional<interaction::AmmoSupplySource> FixedContainerBodySource(const FixedContainerBodyAmmo& f,
    const interaction::HandInteractionSample& input,std::int64_t now)noexcept {
    const auto* e=KnownFixedContainerEquipment(f.equipment);const auto& a=f.ammunition;
    if(!e||!FixedContainerHandOwner(f.binding,input.owner)||!AmmoResourceBindingFresh(f.binding,f.binding.owner,now)||
       a.context!=f.binding.context||!a.coherentIdleVerified||!a.sequence||a.observedNs<=0||a.observedNs>now||a.deadlineNs<=now||
       a.deadlineNs-a.observedNs>e->interaction.maxAgeNs||a.counts.capacity!=e->interaction.capacity||a.counts.loaded<0||
       a.counts.loaded>=a.counts.capacity||a.counts.reserve<=0||a.counts.reserve>1000000)return {};
    interaction::AmmoSupplySource s;
    s.identity={input.owner,{a.context.resource.weapon,input.owner.equipGeneration},
        {e->interaction.id,e->interaction.revision},{f.binding.inventory,a.context.resource.weaponGeneration},input.owner.space};
    s.family=interaction::ReloadInsertionFamily::Magazine;s.reserveUnits=unsigned(a.counts.reserve);
    s.objectUnits=unsigned((std::min)({a.counts.reserve,a.counts.capacity-a.counts.loaded,e->interaction.roundsPerInsertion}));
    s.sequence=a.sequence;s.observedNs=(std::max)(a.observedNs,f.binding.observedNs);
    s.deadlineNs=(std::min)(a.deadlineNs,f.binding.deadlineNs);s.verified=true;return s;
}
}
