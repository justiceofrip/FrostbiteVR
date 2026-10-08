#pragma once
#include "Bc2AmmoResourceService.h"
#include "Bc2MagazineInteraction.h"

namespace fvr::bc2 {
// Validates BC2's actual namespaces: compound physical actor, optional rifle /
// launcher alias, independent hand equip generation, and stable native item.
// The original native receipt is never rewritten into a hand identity.
inline std::optional<interaction::MagazineResourceOwnerBinding> BindMagazineResourceOwners(
    const AmmoResourceView& v,const MagazineFamilyEvidence& family,
    const interaction::HandInteractionOwner& hand,interaction::HandInteractionKey weapon,std::int64_t now)noexcept {
    if(!AmmoResourceViewFresh(v,now)||!MagazineFamilyFresh(family,v.identity.owner,hand,weapon,now)||
       family.binding.inventory!=v.binding.inventory||
       (family.carried&&(family.binding.equipment.data!=v.binding.data||family.binding.equipment.persistence!=v.binding.persistence)))return {};
    const auto observed=std::min(v.binding.observedNs,family.observedNs);
    const auto deadline=std::min({v.binding.deadlineNs,family.deadlineNs,observed+100000000ll});
    if(deadline<=now)return {};
    return interaction::MagazineResourceOwnerBinding{hand,weapon,v.snapshot.context,observed,deadline,true};
}
inline std::optional<interaction::MagazineResourceObservation> MagazineResourceObservationFor(
    const AmmoResourceView& v,const MagazineFamilyEvidence& family,
    const interaction::HandInteractionOwner& hand,interaction::HandInteractionKey weapon,std::int64_t now)noexcept {
    const auto map=BindMagazineResourceOwners(v,family,hand,weapon,now);if(!map)return {};
    return interaction::MagazineResourceObservation{v.snapshot,v.original,v.receipt,map,
        v.phase==interaction::AmmunitionLedgerPhase::Ready,v.wellEmpty,v.originalState};
}
inline std::optional<Bc2AmmoReserveLease> MagazineResourceReserve(const AmmoResourceView& v,std::int64_t now)noexcept {
    if(!AmmoResourceViewFresh(v,now))return {};
    const auto& s=v.snapshot;
    // This is an idle-count publication, never an animation-held lease or an
    // empty-reload-inhibit receipt. Those capabilities are kept separate.
    return Bc2AmmoReserveLease{v.identity,s.sequence,s.observedNs,s.deadlineNs,s.counts.loaded,s.counts.reserve,s.counts.capacity,true,false,true,false};
}
inline std::optional<AmmoResourceRequest> MagazineResourceRequestFor(
    const AmmoResourceView& v,const MagazineFamilyEvidence& family,const interaction::HandInteractionSample& input,
    interaction::HandInteractionKey weapon,std::uint64_t event,interaction::AmmunitionOperation operation,
    bool discard=false)noexcept {
    using namespace interaction;
    if(!BindMagazineResourceOwners(v,family,input.owner,weapon,input.nowNs)||!event||!input.sequence||
       !input.focused||!input.tracked[0]||!input.tracked[1]||input.released[1]||input.observedNs<=0||
       input.observedNs>input.nowNs||input.deadlineNs<=input.nowNs||input.deadlineNs-input.observedNs>100000000||
       v.phase!=AmmunitionLedgerPhase::Ready)return {};
    const auto original=discard||operation==AmmunitionOperation::ReturnMagazine?v.original:std::nullopt;
    return AmmoResourceRequest{v.binding,{v.snapshot.context,event,input.observedNs,
        std::min(input.deadlineNs,v.snapshot.deadlineNs),operation,original},discard};
}
// Render custody has native inventory evidence, but no authority to dispatch or
// finish an operation. A rail-confirmed submitted seat may draw attached while
// counts are still converging; an unseated well cannot draw the stock magazine.
struct MagazineResourcePresentation {
    AmmoResourceView resource{};
    interaction::MagazineResourceOwnerBinding owners{};
    std::uint64_t seatedRequest=0;
    // Accepted by the request channel after a real rail seat. This grants
    // drawing only while server admission is pending, never ammo completion.
    std::optional<AmmoResourceRequest> submittedSeat;
};
inline bool MagazineResourceSeatInFlight(const AmmoResourceView& v)noexcept {
    using namespace interaction;
    return v.command&&v.command->context==v.snapshot.context&&v.command->before.loaded==0&&
        (v.command->operation==AmmunitionOperation::ReturnMagazine||v.command->operation==AmmunitionOperation::RefillMagazine)&&
        (v.requestState==AmmoResourceRequestState::Queued||v.requestState==AmmoResourceRequestState::Dispatched);
}
inline bool MagazineResourcePresentationFresh(const MagazineResourcePresentation& p,const MagazineFamilyEvidence& family,
    const Bc2AmmoReserveLease& reserve,const interaction::HandInteractionOwner& hand,interaction::HandInteractionKey weapon,
    std::int64_t now)noexcept {
    const auto map=BindMagazineResourceOwners(p.resource,family,hand,weapon,now);
    const auto counts=MagazineResourceReserve(p.resource,now);
    return map&&counts&&p.owners.verified&&p.owners.physical==map->physical&&p.owners.weapon==map->weapon&&
        p.owners.native==map->native&&p.owners.observedNs==map->observedNs&&p.owners.deadlineNs==map->deadlineNs&&
        counts->identity==reserve.identity&&counts->sequence==reserve.sequence&&counts->observedNs==reserve.observedNs&&
        counts->deadlineNs==reserve.deadlineNs&&counts->loaded==reserve.loaded&&counts->reserve==reserve.reserve&&counts->capacity==reserve.capacity&&
        (!p.resource.wellEmpty||reserve.loaded==0||
         (p.seatedRequest&&p.resource.request==p.seatedRequest&&MagazineResourceSeatInFlight(p.resource)&&
          p.resource.requestState==AmmoResourceRequestState::Dispatched&&p.resource.snapshot.counts==p.resource.command->after))&&
        p.resource.phase!=interaction::AmmunitionLedgerPhase::NeedsReconciliation;
}
inline bool MagazineResourceRoleAllowed(const MagazineResourcePresentation& p,interaction::MagazinePropRole role,std::uint64_t cycle,std::int64_t now)noexcept {
    using namespace interaction;const auto& v=p.resource;
    if(role==MagazinePropRole::Attached){
        if(!v.wellEmpty)return true;
        if(p.seatedRequest&&v.request==p.seatedRequest&&MagazineResourceSeatInFlight(v))return true;
        if(!p.submittedSeat||!p.seatedRequest||v.request>=p.seatedRequest||v.phase!=AmmunitionLedgerPhase::Ready)return false;
        const auto& r=*p.submittedSeat;const auto& b=r.binding;const auto& i=r.intent;
        if(r.discard||i.id!=p.seatedRequest||i.context!=v.snapshot.context||b.context!=i.context||
           !AmmoResourceBindingFresh(b,v.binding.owner,now)||b.inventory!=v.binding.inventory||b.switching!=v.binding.switching||
           b.data!=v.binding.data||b.persistence!=v.binding.persistence||i.observedNs<=0||i.observedNs>now||
           i.deadlineNs<=now||i.deadlineNs-i.observedNs>100000000)return false;
        return (i.operation==AmmunitionOperation::ReturnMagazine&&v.originalState==MagazineResourceState::Held&&
                i.original&&i.original==v.original)||
            (i.operation==AmmunitionOperation::RefillMagazine&&v.originalState==MagazineResourceState::Discarded&&!i.original);
    }
    if(!v.wellEmpty||!cycle||!v.original||cycle!=v.original->id)return false;
    if(role==MagazinePropRole::Removed)return v.originalState==MagazineResourceState::Held;
    if(role==MagazinePropRole::Replacement)return v.originalState==MagazineResourceState::Discarded;
    return role==MagazinePropRole::Hidden;
}
}
