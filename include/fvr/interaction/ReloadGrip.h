#pragma once
#include "fvr/interaction/HandInteraction.h"
#include <cmath>

namespace fvr::interaction {
// Grab at firm pressure; retain an already owned reload object until the real
// release threshold. This selects input only and never creates/renews a claim.
inline bool ReloadGripActive(float squeeze,const HandInteractionSample& input,
    const std::optional<HandClaim>& held)noexcept {
    if(!std::isfinite(squeeze)||squeeze<0||squeeze>1||!input.focused||!input.tracked[0]||
       input.released[0]||!input.sequence||input.observedNs<=0||input.observedNs>input.nowNs||
       input.deadlineNs<=input.nowNs||squeeze<=.35f)return false;
    if(squeeze>=.75f)return true;
    return held&&held->token.id&&held->token.owner==input.owner&&held->token.hand==InteractionHand::Left&&
        (held->token.kind==HandClaimKind::Mechanism||held->token.kind==HandClaimKind::AmmoObject)&&
        held->inputSequence<=input.sequence&&held->deadlineNs>input.nowNs;
}
}
