#include "fvr/interaction/AmmoSupply.h"
#include <cstring>

namespace fvr::interaction {
namespace {
using namespace reload_insertion_detail;
ManualReloadOwner RequestOwner(const AmmoSupplyIdentity& i)noexcept {
    return {i.owner.actor,i.owner.actorGeneration,i.weapon.id,i.owner.equipGeneration,i.owner.space};
}
ReloadOperation Operation(ReloadInsertionFamily f)noexcept {
    return f==ReloadInsertionFamily::SingleShell?ReloadOperation::InsertRound:ReloadOperation::SeatMagazine;
}
bool SameOriginal(const AmmoSupplySample& a,const AmmoSupplySample& b)noexcept {
    const auto& x=a.input;const auto& y=b.input;
    return x.owner==y.owner&&x.sequence==y.sequence&&x.observedNs==y.observedNs&&x.deadlineNs==y.deadlineNs&&
        x.focused==y.focused&&x.tracked==y.tracked&&x.released==y.released&&a.source==b.source&&
        a.geometrySequence==b.geometrySequence&&a.trackingEpoch==b.trackingEpoch&&a.gripPressed==b.gripPressed&&
        a.intent==b.intent&&!std::memcmp(&a.bodyFromHand,&b.bodyFromHand,sizeof(a.bodyFromHand));
}
}
bool AmmoSupply::ConfigValid()const noexcept {
    if((config_.hand!=InteractionHand::Left&&config_.hand!=InteractionHand::Right)||!config_.itemNamespace||
        !Key(config_.pouch)||!std::isfinite(config_.pouchRadiusMeters)||config_.pouchRadiusMeters<=0||
        config_.maxSourceLifetimeNs<=0)return false;
    for(float v:config_.pouchCenterMeters)if(!std::isfinite(v))return false;
    if(config_.alternateContact){const auto& c=*config_.alternateContact;
        if(!std::isfinite(c.radiusMeters)||c.radiusMeters<=0)return false;
        for(float v:c.centerMeters)if(!std::isfinite(v))return false;}
    return true;
}
bool AmmoSupply::SourceValid(const AmmoSupplySource& s,std::int64_t now)const noexcept {
    const auto& i=s.identity;
    return s.verified&&Owner(i.owner)&&Key(i.weapon)&&Key(i.profile)&&Key(i.pool)&&i.trackingEpoch&&
        i.weapon.id!=config_.itemNamespace&&s.sequence&&s.objectUnits&&
        ((s.family==ReloadInsertionFamily::SingleShell&&s.objectUnits==1)||s.family==ReloadInsertionFamily::Magazine)&&
        s.observedNs>0&&s.observedNs<=now&&s.deadlineNs>now&&
        s.deadlineNs-s.observedNs<=config_.maxSourceLifetimeNs;
}
bool AmmoSupply::Budget(const AmmoSupplySource& s,std::uint32_t units,HandInteractionKey item)const noexcept {
    if(pending_&&pending_->identity!=s.identity)return false;
    const std::uint64_t pendingCost=pending_&&pending_->item!=item?pending_->units:0;
    return std::uint64_t(s.reserveUnits)>=pendingCost+units;
}
AmmoSupplyResult AmmoSupply::Drop(const HandInteractionSample& s,HandInteraction& hands,AmmoSupplyReason why)noexcept {
    auto out=Snapshot(why);
    if(held_){out.released=held_->item;hands.Release(s,held_->claim.token);held_.reset();}
    out.held.reset();armed_=false;return out;
}
AmmoSupplyResult AmmoSupply::Cancel(const HandInteractionSample& s,HandInteraction& hands)noexcept {
    blocked_=true;return Drop(s,hands,AmmoSupplyReason::Explicit);
}
AmmoSupplyResult AmmoSupply::Update(const AmmoSupplySample& s,HandInteraction& hands)noexcept {
    auto out=UpdateCurrent(s,hands);
    if(!held_||!out.held||s.input.owner!=input_.owner||s.input.sequence!=input_.sequence||
        s.input.observedNs!=input_.observedNs||s.input.deadlineNs!=input_.deadlineNs||
        !s.gripPressed||s.geometrySequence!=s.input.sequence||!Rigid(s.bodyFromHand)||
        held_->claim.inputSequence!=s.input.sequence||held_->claim.deadlineNs<=s.input.nowNs)return out;
    // An already retained packet is immutable even if native reserve was polled
    // again between controller packets. This is evidence, not a rolling pose.
    for(const auto& entry:history_)if(entry&&entry->sample.input.owner==s.input.owner&&entry->sample.input.sequence==s.input.sequence)return out;
    const auto other=config_.hand==InteractionHand::Left?InteractionHand::Right:InteractionHand::Left;
    const auto gun=hands.Current(other);
    if(!gun||gun->token.owner!=held_->identity.owner||gun->token.kind!=HandClaimKind::GunHold||
        gun->token.item!=held_->identity.weapon||gun->inputSequence!=s.input.sequence||gun->deadlineNs<=s.input.nowNs)return out;
    history_[historyNext_]=RetainedEvidence{s,*held_,*gun};historyNext_=(historyNext_+1)%history_.size();return out;
}
AmmoSupplyResult AmmoSupply::UpdateCurrent(const AmmoSupplySample& s,HandInteraction& hands,bool allowAcquire)noexcept {
    const auto& in=s.input;
    const auto fail=[&](AmmoSupplyReason why){blocked_=true;return Drop(in,hands,why);};
    if(!ConfigValid())return fail(AmmoSupplyReason::InvalidConfig);
    const auto h=static_cast<std::size_t>(config_.hand);
    if(!Owner(in.owner)||!in.sequence||in.observedNs<=0||in.observedNs>in.nowNs||
        in.deadlineNs<=in.nowNs||in.deadlineNs-in.observedNs>150000000||in.nowNs<lastNow_)
        return fail(AmmoSupplyReason::InvalidInput);
    lastNow_=in.nowNs;
    const bool newOwner=!seen_||input_.owner!=in.owner;
    const bool fresh=!seen_||newOwner||in.sequence>input_.sequence;
    const bool gap=seen_&&fresh&&in.observedNs>=input_.deadlineNs;
    if(seen_&&(in.observedNs<input_.observedNs||(newOwner&&in.observedNs==input_.observedNs)||
        (!newOwner&&in.sequence<input_.sequence)||(!fresh&&(in.observedNs!=input_.observedNs||in.deadlineNs!=input_.deadlineNs))))
        return fail(AmmoSupplyReason::StaleInput);
    if(fresh){input_=in;seen_=true;blocked_=false;packetGripPressed_=s.gripPressed;}
    // Duplicate packets may cancel, never recover safety or create a grip edge.
    else {input_.focused&=in.focused;input_.tracked[h]=input_.tracked[h]&&in.tracked[h];input_.released[h]=input_.released[h]||in.released[h];}
    if(blocked_||!input_.focused||!input_.tracked[h]||s.trackingEpoch==0)
        return fail(AmmoSupplyReason::TrackingLost);
    if(!SourceValid(s.source,in.nowNs)||s.source.observedNs<terminalCutoffNs_||s.source.identity.owner!=in.owner||s.source.identity.trackingEpoch!=s.trackingEpoch)
        return fail(AmmoSupplyReason::InvalidSource);
    const bool changed=source_.identity!=s.source.identity||source_.family!=s.source.family||source_.objectUnits!=s.source.objectUnits;
    if(!fresh&&changed)return fail(AmmoSupplyReason::SourceChanged);
    if(!changed&&(s.source.sequence<source_.sequence||s.source.observedNs<source_.observedNs||
        (s.source.sequence==source_.sequence&&s.source!=source_)))return fail(AmmoSupplyReason::InvalidSource);
    // An old pre-completion source cannot replenish spent supply credit.
    if(settled_.identity==s.source.identity&&(s.source.sequence<settled_.sequence||s.source.observedNs<settled_.observedNs||
        (s.source.sequence==settled_.sequence&&s.source!=settled_)))return fail(AmmoSupplyReason::InvalidSource);
    auto out=Snapshot();
    if(changed||gap)out=Drop(in,hands,changed?AmmoSupplyReason::SourceChanged:AmmoSupplyReason::TrackingLost);
    source_=s.source;
    if(!s.gripPressed||input_.released[h]){
        if(held_)out=Drop(in,hands,AmmoSupplyReason::Released);
        if(fresh&&!s.gripPressed)armed_=true;
        else if(!fresh&&packetGripPressed_)armed_=false; // Cannot invent neutral proof.
        return out;
    }
    if(pending_&&pending_->identity!=source_.identity)return fail(AmmoSupplyReason::PendingOtherOwner);
    if(held_){
        if(!Budget(source_,held_->units,held_->item))return fail(AmmoSupplyReason::ReserveExhausted);
        // Geometry may leave the pouch after acquisition; the carried object is
        // still owned by its exact claim, not by a repeated contact/grab test.
        // Source freshness/budget above authorizes this update; the hand's
        // physical grip has the original tracked-input lifetime. Reservations
        // and old geometry still require their own unextended source evidence.
        const auto lease=in.deadlineNs;
        const auto renewed=hands.Renew(in,held_->claim.token,{config_.pouch,in.sequence,
            fresh?lease:std::min(lease,held_->claim.deadlineNs),true});
        if(!renewed.accepted||!renewed.claim)return fail(AmmoSupplyReason::ClaimLost);
        held_->claim=*renewed.claim;return Snapshot();
    }
    if(!allowAcquire)return Snapshot();
    if(!fresh)return Snapshot(AmmoSupplyReason::NeedNeutral);
    const bool edge=armed_;armed_=false;
    if(!edge)return Snapshot(AmmoSupplyReason::NeedNeutral);
    if(s.geometrySequence!=in.sequence||!Rigid(s.bodyFromHand))return Snapshot(AmmoSupplyReason::InvalidInput);
    const auto inside=[&](const std::array<float,3>& center,float radius){
        double distance2=0;for(unsigned i=0;i<3;++i){const double v=s.bodyFromHand.values[3][i]-center[i];distance2+=v*v;}
        return distance2<=double(radius)*radius;};
    if(!inside(config_.pouchCenterMeters,config_.pouchRadiusMeters)&&
       (!config_.alternateContact||!inside(config_.alternateContact->centerMeters,config_.alternateContact->radiusMeters)))
        return Snapshot(AmmoSupplyReason::OutsidePouch);
    if(!Budget(source_,source_.objectUnits))return Snapshot(AmmoSupplyReason::ReserveExhausted);
    if(nextItem_==std::numeric_limits<std::uint64_t>::max())return Snapshot(AmmoSupplyReason::CounterExhausted);
    const HandInteractionKey item{config_.itemNamespace,++nextItem_};
    const auto acquired=hands.Acquire(in,{in.owner,config_.hand,HandClaimKind::AmmoObject,item,
        {config_.pouch,in.sequence,in.deadlineNs,true},s.intent,0});
    if(!acquired.accepted||!acquired.claim)return Snapshot(AmmoSupplyReason::HandUnavailable);
    held_=AmmoSupplyObject{source_.identity,item,*acquired.claim,source_.family,source_.objectUnits};
    out=Snapshot();out.acquired=item;return out;
}
std::optional<AmmoSupplyReservation> AmmoSupply::Reserve(const AmmoSupplySample& s,HandInteraction& hands,
    const ReloadInsertionSeat& seat,const ManualReloadRequest& request,std::uint64_t cycle)noexcept {
    // Refresh current safety/leases, but never turn a seat into native success.
    Update(s,hands);
    if(seat.inputSequence!=s.input.sequence)return {};
    return ReserveCurrent(s.input,hands,seat,request,cycle);
}
std::optional<AmmoSupplyReservation> AmmoSupply::ReserveFrom(const AmmoSupplySample& current,HandInteraction& hands,
    const AmmoSupplySample& original,const ReloadInsertionSeat& seat,const ManualReloadRequest& request,std::uint64_t cycle)noexcept {
    Update(current,hands);
    if(!held_||original.input.owner!=current.input.owner||original.input.sequence>current.input.sequence||
        original.input.observedNs>current.input.observedNs||original.input.deadlineNs<=current.input.nowNs||
        original.source.deadlineNs<=current.input.nowNs||original.trackingEpoch!=current.trackingEpoch||
        original.source.identity!=held_->identity||seat.inputSequence!=original.input.sequence)return {};
    for(const auto& entry:history_)if(entry&&SameOriginal(entry->sample,original)){
        if(entry->item.item!=held_->item||entry->item.claim.token!=held_->claim.token||
            entry->item.claim.token!=seat.itemClaim||entry->gun.token!=seat.weaponClaim||
            entry->item.claim.deadlineNs<=current.input.nowNs||entry->gun.deadlineNs<=current.input.nowNs)return {};
        return ReserveCurrent(current.input,hands,seat,request,cycle);
    }
    return {};
}
std::optional<AmmoSupplyReservation> AmmoSupply::ReserveCurrent(const HandInteractionSample& in,HandInteraction& hands,
    const ReloadInsertionSeat& seat,const ManualReloadRequest& request,std::uint64_t cycle)noexcept {
    if(pending_||!held_||!cycle||!seat.id||seat.id<=lastSeat_||!request.id||request.id<=lastRequest_||
        seat.identity.owner!=held_->identity.owner||
        seat.identity.weapon!=held_->identity.weapon||seat.identity.item!=held_->item||
        seat.identity.trackingEpoch!=held_->identity.trackingEpoch||seat.profile!=held_->identity.profile||
        seat.itemClaim!=held_->claim.token||seat.operation!=Operation(held_->family)||
        request.owner!=RequestOwner(held_->identity)||request.operation!=seat.operation)return {};
    const auto gun=hands.Current(seat.weaponClaim.hand);
    if(!gun||gun->token!=seat.weaponClaim||gun->token.kind!=HandClaimKind::GunHold||
        gun->token.owner!=held_->identity.owner||gun->token.item!=held_->identity.weapon||
        gun->token.hand==held_->claim.token.hand||gun->inputSequence!=in.sequence||gun->deadlineNs<=in.nowNs)return {};
    pending_=AmmoSupplyReservation{held_->identity,held_->item,held_->claim.token,seat.id,request.id,cycle,
        source_.sequence,in.nowNs,held_->units,source_.reserveUnits,seat.operation};
    lastSeat_=seat.id;lastRequest_=request.id;return pending_;
}
bool AmmoSupply::ReleaseSubmitted(const HandInteractionSample& safety,HandInteraction& hands,
    const AmmoSupplyReservation& reservation)noexcept {
    if(!pending_||*pending_!=reservation||!held_||held_->item!=reservation.item||
        held_->claim.token!=reservation.claim||safety.owner!=reservation.identity.owner||
        safety.nowNs<lastNow_)return false;
    Drop(safety,hands,AmmoSupplyReason::Released);
    return true;
}
AmmoSupplyResolution AmmoSupply::Resolve(const HandInteractionSample& safety,HandInteraction& hands,
    const AmmoSupplyReceipt& receipt)noexcept {
    AmmoSupplyResolution out;
    if(!pending_||safety.nowNs<lastNow_||receipt.reservation!=*pending_||!receipt.verified||!receipt.nativeEvent||
        receipt.observedNs<pending_->startedNs||receipt.observedNs>safety.nowNs||receipt.deadlineNs<=safety.nowNs||
        receipt.deadlineNs-receipt.observedNs>config_.maxSourceLifetimeNs||
        !SourceValid(receipt.currentReserve,safety.nowNs)||receipt.currentReserve.identity!=pending_->identity||
        receipt.currentReserve.objectUnits!=pending_->units||Operation(receipt.currentReserve.family)!=pending_->operation||
        receipt.currentReserve.sequence<=pending_->sourceSequence||receipt.currentReserve.observedNs<receipt.observedNs)return out;
    const auto& ack=receipt.acknowledgement;
    if(ack.request!=pending_->request||ack.owner!=RequestOwner(pending_->identity)||ack.operation!=pending_->operation||
        (ack.status!=ReloadAcknowledgement::Applied&&ack.status!=ReloadAcknowledgement::Rejected))return out;
    if(ack.status==ReloadAcknowledgement::Applied&&
        std::uint64_t(receipt.currentReserve.reserveUnits)+pending_->units>pending_->reserveBefore)return out;
    out.accepted=true;out.releasedReservation=pending_;
    if(ack.status==ReloadAcknowledgement::Applied){
        out.consumed=pending_;
        // The original claim may already have ended. Never release a replacement.
        hands.Release(safety,pending_->claim);
        if(held_&&held_->item==pending_->item&&held_->claim.token==pending_->claim)held_.reset();
        armed_=false;
    }
    settled_=receipt.currentReserve;lastNow_=safety.nowNs;pending_.reset();return out;
}
AmmoSupplyResolution AmmoSupply::SettleTerminal(const HandInteractionSample& safety,HandInteraction& hands,
    const AmmoSupplyTerminalReceipt& r)noexcept {
    AmmoSupplyResolution out;
    if(!pending_||r.reservation!=*pending_||!r.nativeFinalVerified||!r.event||r.event<=lastRetirement_||
       safety.nowNs<lastNow_||r.completedNs<pending_->startedNs||r.completedNs>safety.nowNs||
       r.reserveBefore!=pending_->reserveBefore||r.acknowledgement.request!=pending_->request||
       r.acknowledgement.owner!=RequestOwner(pending_->identity)||r.acknowledgement.operation!=pending_->operation)return out;
    const auto status=r.acknowledgement.status;
    if(status==ReloadAcknowledgement::Applied){
        if(r.reserveBefore<r.reserveAfter||r.reserveBefore-r.reserveAfter!=pending_->units)return out;
        out.consumed=pending_;
    }else if(status!=ReloadAcknowledgement::Rejected||r.reserveAfter!=r.reserveBefore)return out;
    out.accepted=true;out.releasedReservation=pending_;
    hands.Release(safety,pending_->claim);
    if(held_&&held_->item==pending_->item&&held_->claim.token==pending_->claim)held_.reset();
    pending_.reset();armed_=false;blocked_=true;lastRetirement_=r.event;lastNow_=safety.nowNs;
    terminalCutoffNs_=std::max(terminalCutoffNs_,r.completedNs);
    return out;
}
AmmoSupplyResolution AmmoSupply::Rebaseline(const HandInteractionSample& safety,HandInteraction& hands,
    const AmmoSupplyRebaseline& evidence)noexcept {
    AmmoSupplyResolution out;
    const auto& fresh=evidence.currentReserve;
    if(!pending_||evidence.reservation!=*pending_||!evidence.verifiedNativeCycleDrained||
        !evidence.retirement||evidence.retirement<=lastRetirement_||safety.nowNs<lastNow_||
        evidence.observedNs<pending_->startedNs||evidence.observedNs>safety.nowNs||evidence.deadlineNs<=safety.nowNs||
        evidence.deadlineNs-evidence.observedNs>config_.maxSourceLifetimeNs||
        !SourceValid(fresh,safety.nowNs)||fresh.identity.owner!=safety.owner||fresh.observedNs<evidence.observedNs||
        (fresh.identity==pending_->identity&&fresh.sequence<=pending_->sourceSequence)||
        (fresh.identity==source_.identity&&(fresh.sequence<source_.sequence||fresh.observedNs<source_.observedNs||
            (fresh.sequence==source_.sequence&&fresh!=source_))))return out;
    out.accepted=true;out.releasedReservation=pending_; // deliberately no consumed event
    const bool originalHeld=held_&&held_->item==pending_->item&&held_->claim.token==pending_->claim;
    pending_.reset();lastRetirement_=evidence.retirement;settled_=fresh;lastNow_=safety.nowNs;
    // Original ambiguous resource retires; replacement can survive only against
    // the freshly reconciled exact source and its remaining authoritative count.
    if(held_&&(originalHeld||held_->identity!=fresh.identity||held_->family!=fresh.family||
        held_->units!=fresh.objectUnits||held_->units>fresh.reserveUnits))Drop(safety,hands,AmmoSupplyReason::SourceChanged);
    armed_=false;return out;
}
} // namespace fvr::interaction
