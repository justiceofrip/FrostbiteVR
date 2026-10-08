#pragma once
#include "HandInteraction.h"
#include <cmath>

namespace fvr::interaction {
// Eligibility only: not a new grab, geometry proof, native request or commit.
// The caller must separately require SightFlip's committedMode from the exact
// outstanding native acknowledgement, with the same verified physical family.
// Only then may it create a NEW strict-current hand reservation/token/deadline.
// Never relax historical-contact barriers, replay the original grab, or use
// this result to send another mode request.
inline bool SightLeaseContinuationEligible(
    const std::optional<HandClaim>& previousSight,
    const HandInteractionResult& lifecycle,
    const HandInteractionSample& current,
    HandInteractionKey currentPhysicalItem,
    bool cancelling,float squeeze) noexcept {
    if(!previousSight||!lifecycle.inputValid||lifecycle.accepted||lifecycle.claim||
       lifecycle.reason!=HandInteractionReason::None||cancelling||
       !std::isfinite(squeeze)||squeeze<=.35f||squeeze>1.f||
       !current.owner.actor||!current.owner.actorGeneration||!current.owner.equipGeneration||!current.owner.space||
       !current.sequence||current.observedNs<=0||current.nowNs<current.observedNs||
       current.deadlineNs<=current.nowNs||!current.focused||
       !current.tracked[0]||!current.tracked[1]||current.released[0]||current.released[1]||
       !currentPhysicalItem.id||!currentPhysicalItem.generation)return false;
    const auto& old=*previousSight;const auto& sight=old.token;
    if(!sight.id||sight.hand!=InteractionHand::Left||sight.kind!=HandClaimKind::Sight||
       sight.owner!=current.owner||sight.item!=currentPhysicalItem||
       !sight.contact.id||!sight.contact.generation||!sight.prerequisiteClaim||
       !old.inputSequence||old.inputSequence>current.sequence||old.deadlineNs<=0)return false;
    const HandClaimRelease* sightRelease=nullptr;
    const HandClaimRelease* parentRelease=nullptr;
    for(const auto& released:lifecycle.released){
        if(!released)continue;
        if(released->token==sight){
            if(sightRelease)return false;
            sightRelease=&*released;
        }else{
            const auto& parent=released->token;
            if(parentRelease||parent.id!=sight.prerequisiteClaim||
               parent.hand!=InteractionHand::Right||parent.kind!=HandClaimKind::GunHold||
               parent.owner!=sight.owner||parent.item!=sight.item||parent.prerequisiteClaim||
               !parent.contact.id||!parent.contact.generation||
               released->reason!=HandInteractionReason::LeaseExpired)return false;
            parentRelease=&*released;
        }
    }
    if(!sightRelease)return false;
    if(sightRelease->reason==HandInteractionReason::LeaseExpired)
        return old.deadlineNs<=current.nowNs;
    return sightRelease->reason==HandInteractionReason::DependencyLost&&parentRelease;
}
}
