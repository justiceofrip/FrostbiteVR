#pragma once
#include "HandInteraction.h"
#include "SupportGrip.h"
#include <cmath>

namespace fvr::interaction {
// A lease can expire between native gathers while the existing support policy
// still accepts a held grip. This predicate authorizes only an ATTEMPT to create
// a new claim from newer, fresh geometry. It neither restores the expired token
// nor changes the SupportGrip grasp token, pose, original deadline or native gun.
// Always finish with AcquireFrom(current, evidence, request); its recorded-input
// and invalidation checks remain authoritative. Never Transfer for continuation.
// DependencyLost is intentionally excluded: that path invalidates contact at the
// current sequence. A native acknowledgement exception is not geometry evidence.
inline bool SupportLeaseContinuationEligible(
    const std::optional<HandClaim>& previousSupport,
    const HandInteractionResult& lifecycle,
    const HandInteractionSample& current,
    HandInteractionKey physicalItem,
    std::uint64_t previousGraspToken,
    const SupportGripResult& proposed,
    const std::optional<HandClaim>& currentLeft,
    const std::optional<HandClaim>& currentGun,
    const HandInteractionSample& evidence,
    const HandContactProof& proof,
    bool cancelling=false) noexcept {
    if(!previousSupport||!lifecycle.inputValid||lifecycle.accepted||lifecycle.claim||
       lifecycle.reason!=HandInteractionReason::None||cancelling||currentLeft||!currentGun||
       !previousGraspToken||!proposed.holding||proposed.engaged||proposed.released||
       proposed.reason!=SupportRelease::None||proposed.token!=previousGraspToken)return false;
    if(!current.owner.actor||!current.owner.actorGeneration||!current.owner.equipGeneration||!current.owner.space||
       !current.sequence||current.observedNs<=0||current.nowNs<current.observedNs||
       current.deadlineNs<=current.nowNs||!current.focused||!current.tracked[0]||!current.tracked[1]||
       current.released[0]||current.released[1]||!physicalItem.id||!physicalItem.generation)return false;
    const auto& input=proposed.input;
    if(!ValidInput(input)||input.generation!=current.sequence||input.spaceGeneration!=current.owner.space||
       !input.focused||!input.headValid||!input.hands[0].gripTracked||!input.hands[1].gripTracked||
       !input.hands[1].aimTracked||!(input.hands[0].active&Squeeze)||
       !std::isfinite(input.hands[0].squeeze)||input.hands[0].squeeze<=.35f)return false;
    const auto& old=*previousSupport;const auto& support=old.token;const auto& gun=currentGun->token;
    if(!support.id||support.hand!=InteractionHand::Left||support.kind!=HandClaimKind::WeaponSupport||
       support.owner!=current.owner||support.item!=physicalItem||!support.prerequisiteClaim||
       !support.contact.id||!support.contact.generation||!old.inputSequence||old.deadlineNs<=0||
       old.deadlineNs>current.nowNs||!gun.id||gun.hand!=InteractionHand::Right||
       gun.kind!=HandClaimKind::GunHold||gun.owner!=current.owner||gun.item!=physicalItem||
       gun.prerequisiteClaim||!gun.contact.id||!gun.contact.generation||
       currentGun->deadlineNs<=current.nowNs)return false;
    bool expiredSupport=false,expiredParent=false;
    for(const auto& released:lifecycle.released){
        if(!released)continue;
        if(released->token==support){
            if(expiredSupport||released->reason!=HandInteractionReason::LeaseExpired)return false;
            expiredSupport=true;
        }else{
            const auto& parent=released->token;
            if(expiredParent||parent.id!=support.prerequisiteClaim||parent.hand!=InteractionHand::Right||
               parent.kind!=HandClaimKind::GunHold||parent.owner!=support.owner||parent.item!=support.item||
               parent.prerequisiteClaim||released->reason!=HandInteractionReason::LeaseExpired)return false;
            expiredParent=true;
        }
    }
    if(!expiredSupport||(!expiredParent&&gun.id!=support.prerequisiteClaim)||
       (expiredParent&&gun.id==support.prerequisiteClaim))return false;
    // A still-fresh duplicate of the expired lease cannot exist; require an
    // explicitly newer original publication, with all original provenance.
    return evidence.owner==current.owner&&evidence.sequence>old.inputSequence&&
        evidence.sequence<=current.sequence&&evidence.observedNs>0&&evidence.observedNs<=current.observedNs&&
        evidence.deadlineNs>evidence.observedNs&&evidence.deadlineNs>current.nowNs&&
        evidence.focused&&evidence.tracked[0]&&evidence.tracked[1]&&!evidence.released[0]&&!evidence.released[1]&&
        proof.eligible&&proof.key==support.contact&&proof.inputSequence==evidence.sequence&&
        proof.deadlineNs==evidence.deadlineNs;
}
}

