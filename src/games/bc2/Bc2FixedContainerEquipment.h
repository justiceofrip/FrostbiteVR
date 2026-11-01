#pragma once
#include "../../../profiles/weapons46-special/FixedContainer.generated.h"
#include "../../../profiles/weapons71-underbarrel-reload/FixedContainer.generated.h"
#include "Bc2AmmoResourceService.h"
#include "Bc2WeaponMode.h"
#include "fvr/interaction/AmmoSupplyVisual.h"

namespace fvr::bc2 {
using FixedContainerEquipmentProfile=generated::FixedContainerEquipmentProfile;
inline const FixedContainerEquipmentProfile* KnownFixedContainerEquipment(const FixedContainerEquipmentProfile* p)noexcept {
    for(const auto& e:generated::FixedContainerEquipment)if(p==&e)return p;
    for(const auto& e:generated::UnderbarrelFixedContainerEquipment)if(p==&e)return p;
    return nullptr;
}
inline const FixedContainerEquipmentProfile* FindFixedContainerEquipment(std::string_view config,std::string_view mesh,std::uint64_t rig)noexcept {
    const FixedContainerEquipmentProfile* found=nullptr;
    for(const auto profiles:{std::span<const FixedContainerEquipmentProfile>(generated::FixedContainerEquipment),
                             std::span<const FixedContainerEquipmentProfile>(generated::UnderbarrelFixedContainerEquipment)})
        for(const auto& e:profiles)if(e.configurationPath==config&&e.mesh==mesh&&e.rigFingerprint==rig){
            if(found)return nullptr;found=&e;
        }
    return found;
}
inline bool FixedContainerHandOwner(const AmmoResourceBinding& b,const interaction::HandInteractionOwner& h)noexcept {
    return h.actor==((std::uint64_t(b.owner.weak)<<32)|b.owner.soldier)&&h.actorGeneration==b.owner.actorGeneration&&
        h.equipGeneration&&h.space==b.owner.space;
}
struct FixedContainerBodyAmmo {
    const FixedContainerEquipmentProfile* equipment=nullptr;
    AmmoResourceBinding binding{};
    interaction::AmmunitionSnapshot ammunition{};
    struct FamilyBinding {
        ReloadStateOwner owner{};
        WeaponModeCommand mode{};
        std::int64_t observedNs=0,deadlineNs=0;
    };
    std::optional<FamilyBinding> family;
};
// Physical custody follows the verified linked item; ammunition stays with the
// selected native weapon. Preserve the resolver's original 500 ms cache lease.
inline std::optional<interaction::HandInteractionKey> FixedContainerHandItem(const FixedContainerBodyAmmo& f,
    const interaction::HandInteractionOwner& hand,std::int64_t now)noexcept {
    const auto* e=KnownFixedContainerEquipment(f.equipment);const auto& b=f.binding;
    if(!e||!FixedContainerHandOwner(b,hand)||!AmmoResourceBindingFresh(b,b.owner,now)||
       f.ammunition.context!=b.context)return {};
    if(!f.family)return interaction::HandInteractionKey{b.context.resource.weapon,hand.equipGeneration};
    const auto& proof=*f.family;const auto& m=proof.mode;
    const WeaponModeFamilyProfile* family=nullptr;
    for(const auto& candidate:WeaponModeFamilies)if(m.family==&candidate){family=&candidate;break;}
    if(!family||!WeaponModeFamilyEligible(*family)||proof.owner!=b.owner||!m.selectedSecondary||
       proof.observedNs<=0||proof.observedNs>now||proof.deadlineNs<=now||proof.deadlineNs-proof.observedNs>500000000||
       m.inventory!=b.inventory||!m.persistent||m.persistent!=b.persistence||m.persistent==b.owner.weapon||
       !m.targetWeapon||m.targetWeapon==b.owner.weapon||m.targetWeapon==m.persistent||m.targetSlot>=9||
       !m.action||m.action!=m.primaryAction||family->secondaryConfiguration!=e->configurationPath||
       family->secondaryAsset!=e->asset||family->persistentId.empty())return {};
    return interaction::HandInteractionKey{m.persistent,hand.equipGeneration};
}
inline std::optional<interaction::AmmoSupplySource> FixedContainerBodySource(const FixedContainerBodyAmmo& f,
    const interaction::HandInteractionSample& input,std::int64_t now)noexcept {
    const auto* e=KnownFixedContainerEquipment(f.equipment);const auto& a=f.ammunition;
    const auto item=FixedContainerHandItem(f,input.owner,now);
    if(!e||!item||
       a.context!=f.binding.context||!a.coherentIdleVerified||!a.sequence||a.observedNs<=0||a.observedNs>now||a.deadlineNs<=now||
       a.deadlineNs-a.observedNs>e->interaction.maxAgeNs||a.counts.capacity!=e->interaction.capacity||a.counts.loaded<0||
       a.counts.loaded>=a.counts.capacity||a.counts.reserve<=0||a.counts.reserve>1000000)return {};
    interaction::AmmoSupplySource s;
    s.identity={input.owner,*item,
        {e->interaction.id,e->interaction.revision},{f.binding.inventory,a.context.resource.weaponGeneration},input.owner.space};
    s.family=interaction::ReloadInsertionFamily::Magazine;s.reserveUnits=unsigned(a.counts.reserve);
    s.objectUnits=unsigned((std::min)({a.counts.reserve,a.counts.capacity-a.counts.loaded,e->interaction.roundsPerInsertion}));
    s.sequence=a.sequence;s.observedNs=(std::max)(a.observedNs,f.binding.observedNs);
    s.deadlineNs=(std::min)(a.deadlineNs,f.binding.deadlineNs);
    if(f.family)s.deadlineNs=(std::min)(s.deadlineNs,f.family->deadlineNs);
    s.verified=true;return s;
}
}
