#pragma once
#include "Bc2BodyAmmo.h"
#include "Bc2MagazineReload.h"

namespace fvr::bc2 {
// Display-only projection of one final magazine publication. Interaction route
// selection is not evidence that the player's reserve appeared/disappeared.
// The caller must reject occupied/quarantined supply state. This function has
// no cached fallback, native reads, hand acquisition or ammunition authority.
inline std::optional<interaction::AmmoSupplyVisualSample> BuildMagazineBodyAmmo(
    const MagazineTracking& tracking,const std::optional<interaction::HandClaim>& gun,
    const interaction::AmmoSupplyContact& contact,interaction::SupplyAnchorFrame frame,
    std::int64_t now,const ReloadMagazineLease* held=nullptr)noexcept {
    using namespace interaction;
    const auto& reserve=tracking.reserve;
    if(!gun||!MagazineTrackingFresh(tracking,now)||tracking.detach||
       (tracking.target&&(tracking.target->handTarget||
        (tracking.target->role!=MagazinePropRole::Attached&&tracking.target->role!=MagazinePropRole::Hidden)))||
       reserve.loaded<0||reserve.loaded>reserve.capacity||reserve.reserve<=0)return {};
    if(tracking.resource){
        // MagazineTrackingFresh already checked the coherent resource view.
        // A stable detached well is idle; it never needs allThreeHeld.
        if(held||tracking.resource->resource.phase!=AmmunitionLedgerPhase::Ready)return {};
    }else if(tracking.cycle){
        if(!held||!held->nativeBindingVerified||!held->allThreeHeld||!held->sequence||
           held->identity!=reserve.identity||held->cycle!=tracking.cycle||
           held->loaded!=reserve.loaded||held->reserve!=reserve.reserve||held->capacity!=reserve.capacity||
           held->observedNs<=0||held->observedNs>now||held->deadlineNs<=now||
           held->deadlineNs-held->observedNs>200000000)return {};
    }else if(held||(!reserve.reloadInputReady&&!reserve.allThreeIdle))return {};
    const auto& input=tracking.inputEvidence;
    const auto owners=BindMagazineOwners(input.owner,tracking.family.binding.weapon,reserve,tracking.cycle,now,tracking.family);
    const auto source=owners?MagazineBodySupply(*owners,reserve,input.owner.space,now):std::nullopt;
    if(!source)return {};
    AmmoSupplyVisualSample visual;visual.enabled=true;visual.source=*source;
    visual.input=input;visual.gun=*gun;visual.contact=contact;visual.frame=frame;
    // Preserve exact native/input evidence and its original deadlines. The
    // renderer validates this same typed source against each eye's tracking.
    if(!BodyAmmoFresh({visual,tracking,{}},now))return {};
    return visual;
}
} // namespace fvr::bc2
