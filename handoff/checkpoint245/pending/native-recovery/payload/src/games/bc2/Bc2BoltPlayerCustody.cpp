#include "Bc2BoltPlayerCustody.h"
namespace fvr::bc2 {
using namespace interaction;
void Bc2BoltPlayerCustody::Invalidate()noexcept {
    if(debt_&&references_)recoveryReferences_=references_;
    references_.reset();candidate_.reset();stableAt_=0;lastRaw_=0;previous_={};returnPress_=0;neutral_=0;rightArmed_=rightHeld_=false;
    if(debt_)retirementRequired_=true;
}
void Bc2BoltPlayerCustody::Capture(const Bc2BoltPlayerInput& in)noexcept {
    using namespace reload_insertion_detail;
    if(references_||debt_||retirementRequired_)return;
    const auto& r=in.contact;const auto& a=in.ammunition;const auto& n=in.native;
    const auto now=in.safety.nowNs;
    const bool idle=a&&a->verified&&a->identity.owner==in.owner&&a->sequence&&a->allThreeIdle&&a->loaded>0&&
        a->loaded<=a->capacity&&a->reserve>=0&&weapon_cycle_detail::Window(a->observedNs,a->deadlineNs,now)&&
        n&&!n->blocksFire&&!n->held&&!n->ready&&
        (n->phase==Bc2NativeCyclePhase::Watching||n->phase==Bc2NativeCyclePhase::Complete);
    if(!idle||!r.nativePartValid||!Rigid(r.nativePartInWeapon)||
       Distance(r.nativePartInWeapon,calibration_->profile.closedContact)>.005f||
       Angle(r.nativePartInWeapon,calibration_->profile.closedContact)>.02f){candidate_.reset();stableAt_=0;return;}
    if(r.raw.input.sequence<=lastRaw_)return;
    lastRaw_=r.raw.input.sequence;
    for(const auto& wrist:r.nativeWristInWeapon)if(!Rigid(wrist)){candidate_.reset();stableAt_=0;return;}
    bool stable=bool(candidate_);
    if(candidate_)for(unsigned hand=0;hand<2;++hand)
        stable=stable&&Distance((*candidate_)[hand],r.nativeWristInWeapon[hand])<=.003f&&
            Angle((*candidate_)[hand],r.nativeWristInWeapon[hand])<=.015f;
    if(!stable){candidate_=r.nativeWristInWeapon;stableAt_=r.raw.input.observedNs;return;}
    // Both waiting periods refer to actual fresh observations; no elapsed-only
    // promotion of one old contact frame, and moving cycle poses never seed it.
    if(r.raw.input.observedNs-stableAt_>=150000000&&now-selectedAt_>=600000000)
        references_=candidate_;
}
std::optional<Bc2PhysicalBoltSample> Bc2BoltPlayerCustody::Prepare(const Bc2BoltPlayerInput& in,
    const HandInteraction& hands,std::uint64_t& intent)noexcept {
    const auto& s=in.safety;const auto* controls=in.controls;
    if(!calibration_||!BoltCalibrationValid(*calibration_)||!controls)return {};
    if(in.native&&in.native->blocksFire)debt_=true;
    const bool identity=owner_==in.owner&&item_==in.item&&units_==controls->worldUnitsPerMeter;
    if(!identity){Invalidate();recoveryReferences_.reset();leftCustody_.reset();owner_=in.owner;item_=in.item;units_=controls->worldUnitsPerMeter;selectedAt_=s.nowNs;}
    const bool exact=in.asset==calibration_->asset&&in.mesh==calibration_->mesh&&
        in.contact.raw.nativeOwner==in.owner&&in.contact.raw.rigFingerprint==calibration_->rigFingerprint&&
        in.owner.actorGeneration==s.owner.actorGeneration&&in.owner.space==s.owner.space&&
        s.owner.actor==((std::uint64_t(in.owner.weak)<<32)|in.owner.soldier)&&
        in.item.id==in.owner.weapon&&in.item.generation==s.owner.equipGeneration;
    const bool source=controls->generation==s.sequence&&controls->spaceGeneration==s.owner.space&&
        controls->focused==s.focused&&controls->hands[0].gripTracked==s.tracked[0]&&controls->hands[1].gripTracked==s.tracked[1]&&
        (controls->hands[0].active&Squeeze)&&(controls->hands[1].active&Squeeze)&&
        s.released[0]==(controls->hands[0].squeeze<=.35f)&&s.released[1]==(controls->hands[1].squeeze<=.35f);
    if(!exact||!source||s.nowNs<lastNow_||!s.focused||!s.tracked[0]||!s.tracked[1]||
       (previous_.sequence&&s.owner!=previous_.owner)){
        Invalidate();lastNow_=std::max(lastNow_,s.nowNs);return {};
    }
    lastNow_=s.nowNs;
    const auto recovering=in.recovery&&in.recovery->native.owner==in.owner&&in.recovery->authority?
        in.recovery->authority:std::nullopt;
    const auto left=hands.Current(InteractionHand::Left);
    if(retirementRequired_&&recovering&&debtLease_&&recoveryReferences_&&leftCustody_&&left&&
       left->token==*leftCustody_&&left->deadlineNs>s.nowNs&&left->token.owner==s.owner&&left->token.item==in.item&&
       recovering->owner==debtLease_->owner&&recovering->item==debtLease_->item&&
       recovering->mechanism==debtLease_->mechanism&&recovering->cycle==debtLease_->cycle&&recovering->shot==debtLease_->shot&&
       recovering->owner==s.owner&&recovering->item==in.item&&
       weapon_cycle_detail::Lease(*recovering,s.nowNs)){
        references_=recoveryReferences_;retirementRequired_=false;custody_=BoltCustodyPhase::Manipulating;
        submitted_=neutral_=returnPress_=0;
    }
    const auto wrists=MapBoltTrackedWrists(in.contact,*controls,s,in.body);
    if(!wrists)return {};
    Capture(in);
    const bool fresh=s.sequence>previous_.sequence;
    const bool release=fresh&&previous_.sequence&&previous_.owner==s.owner&&!previous_.released[1]&&s.released[1];
    const bool press=fresh&&rightArmed_&&controls->hands[1].squeeze>=.7f;
    if(fresh){
        if(s.released[1]){rightArmed_=true;rightHeld_=false;}
        else if(press){rightArmed_=false;rightHeld_=true;}
    }
    if(submitted_&&fresh&&s.sequence>submitted_&&s.released[1]){neutral_=s.sequence;returnPress_=0;}
    if(neutral_&&press&&s.sequence>neutral_)returnPress_=s.sequence;
    Bc2PhysicalBoltSample out;out.nativeOwner=in.owner;out.input=s;out.item=in.item;out.asset=in.asset;out.mesh=in.mesh;
    out.raw=in.contact.raw;out.wristWorld=*wrists;out.grip=rightHeld_;
    const bool held=in.native&&in.native->held&&in.native->native.owner==in.owner&&
        in.native->held->owner==s.owner&&in.native->held->item==in.item&&
        in.native->held->mechanism==HandInteractionKey{calibration_->profile.id,in.item.generation}&&
        weapon_cycle_detail::Lease(*in.native->held,s.nowNs);
    if(references_&&!retirementRequired_){
        const bool enter=release&&held&&custody_!=BoltCustodyPhase::Manipulating;
        const bool back=custody_==BoltCustodyPhase::Manipulating&&returnPress_&&!s.released[1]&&
            in.contact.raw.input.sequence>=returnPress_&&!in.contact.raw.input.released[1];
        if(enter||back){
            out.custodyTransfer=BuildBoltCustodyTransfer(s,in.item,in.contact,*references_,hands,enter,back,intent,enter);
            if(out.custodyTransfer&&enter){
                const auto weapon=ReprojectBoltCustodyWeapon(in.contact,*wrists,InteractionHand::Left);
                if(weapon)out.weaponWorld=*weapon;else out.custodyTransfer.reset();
            }
        }
    }
    for(unsigned hand=0;hand<2;++hand){
        const auto claim=hands.Current(static_cast<InteractionHand>(hand));
        if(claim)out.gunContacts[hand]={claim->token.contact,s.sequence,s.deadlineNs,true};
    }
    if(out.custodyTransfer){const auto& request=out.custodyTransfer->nextGun.request;
        out.gunContacts[weapon_cycle_detail::HandIndex(request.hand)]={request.contact.key,s.sequence,s.deadlineNs,true};}
    if(fresh)previous_=s;
    if(retirementRequired_)out.cancel=true;
    return out;
}
void Bc2BoltPlayerCustody::Observe(const Bc2PhysicalBoltResult& result)noexcept {
    // Startup admission/backpressure can block Fire before a shot exists.
    // Only actual held/gesture/custody evidence may create physical debt.
    if(result.tracking.held)debtLease_=result.tracking.held;
    if(result.tracking.gun&&result.tracking.gun->token.hand==InteractionHand::Left&&
       result.tracking.gun->token.kind==HandClaimKind::GunHold)leftCustody_=result.tracking.gun->token;
    if(result.tracking.held||result.tracking.recovery||result.settled||result.recovered||
       result.tracking.custody==BoltCustodyPhase::Manipulating)debt_=true;
    if(result.tracking.enabled){
        custody_=result.tracking.custody;
        if(result.tracking.mechanismPhase==WeaponCyclePhase::AwaitingNative&&!submitted_)
            submitted_=result.tracking.input.sequence;
    }
    if(result.custodyChange&&result.custodyChange->transaction.accepted&&custody_==BoltCustodyPhase::Returned)
        returnPress_=0;
    if(result.settled||result.recovered)ready_=true;
    if(ready_&&custody_==BoltCustodyPhase::Returned&&!retirementRequired_){
        debt_=false;ready_=false;submitted_=0;neutral_=0;returnPress_=0;debtLease_.reset();leftCustody_.reset();recoveryReferences_.reset();
    }
}
}
