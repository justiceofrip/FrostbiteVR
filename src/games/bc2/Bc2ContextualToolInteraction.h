#pragma once
#include "Bc2ContextualTool.h"
#include "Bc2ContextualToolSource.h"
namespace fvr::bc2 {
// Shared body gestures stay suspended for Fire. The sole exception enters only
// already-held tool Controls; later attachment/aim/native/cache checks are still
// mandatory. This never creates a claim or admits an Equip/Return gesture.
inline bool ContextualToolInteractionCurrent(const BodyDrawSample& s,const ContextualToolObservation& o,
    bool held,bool revoked,std::uint64_t operation,std::uint64_t bodyOperation,
    const std::optional<interaction::HandClaimToken>& own,const interaction::HandInteraction& hands)noexcept {
    using namespace interaction;
    if(s.cancel||s.reloadBusy||!ContextualToolSourceCurrent(s.input,s.hand,s.hand.nowNs))return false;
    if(s.interaction==BodyInteractionState::Available)return true;
    if(!s.cancelOnlyForFire||!held||revoked||!operation||operation!=bodyOperation||!own||
       s.hand.released[1]||s.input.hands[1].squeeze<.65f||o.inventory.count<=4||o.inventory.count>o.inventory.items.size()||
       o.inventory.selected!=4||o.inventory.owner!=s.owner||s.owner.weapon!=o.tool.item.weapon||
       o.inventory.items[4]!=o.tool.item||o.tool.item.category!=12)return false;
    const auto current=hands.Current(InteractionHand::Right);
    return current&&current->token==*own&&own->owner==s.hand.owner&&own->hand==InteractionHand::Right&&
        own->kind==HandClaimKind::GunHold&&own->item.id==o.tool.item.weapon&&own->item.generation&&
        current->inputSequence&&current->inputSequence<=s.hand.sequence&&current->deadlineNs>s.hand.nowNs;
}
}
