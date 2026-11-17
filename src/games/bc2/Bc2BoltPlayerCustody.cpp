#include "Bc2BoltPlayerCustody.h"
namespace fvr::bc2 {
using namespace interaction;
bool Bc2BoltPlayerCustody::SuspendForDeparture(const Bc2NativeCycleDeparture& departure,
    const Bc2NativeCycleView& selected,const HandInteractionSample& input,HandInteraction& hands)noexcept {
    const auto& old=departure.previous.owner;
    if(!departure.slot||!selected.slot||departure.slot==selected.slot||selected.native!=departure.selected||
       !departure.callbackRevision||!departure.cycle||!departure.shot||!departure.guardEpoch||
       old.weapon==selected.native.owner.weapon||(owner_.player&&owner_!=old)||
       (debtLease_&&(debtLease_->cycle!=departure.cycle||debtLease_->shot!=departure.shot)))return false;
    // Claim identity includes the hand namespace. Native equip generation is
    // intentionally not substituted for the hand arbiter's independent epoch.
    const auto owns=[&](const HandClaim& claim){return claim.token.owner.actor==
        ((std::uint64_t(old.weak)<<32)|old.soldier)&&claim.token.owner.actorGeneration==old.actorGeneration&&
        claim.token.owner.space==old.space&&claim.token.item.id==old.weapon&&
        (!item_.generation||claim.token.item==item_);};
    for(const auto hand:{InteractionHand::Left,InteractionHand::Right}){
        const auto claim=hands.Current(hand);if(claim&&owns(*claim))hands.Release(input,claim->token);
    }
    for(const auto hand:{InteractionHand::Left,InteractionHand::Right}){
        const auto claim=hands.Current(hand);if(claim&&owns(*claim))return false;
    }
    Invalidate();recoveryReferences_.reset();debtLease_.reset();leftCustody_.reset();selectionReturn_.reset();
    custody_=BoltCustodyPhase::Idle;mechanismPhase_=WeaponCyclePhase::Idle;
    // The bank retains the obligation; this adapter is now presentation only.
    debt_=ready_=retirementRequired_=false;owner_={};item_={};submitted_=0;return true;
}
void Bc2BoltPlayerCustody::Invalidate()noexcept {
    if(debt_&&references_)recoveryReferences_=references_;
    references_.reset();candidate_.reset();lastContact_.reset();wristMapping_.reset();mappedCustody_.reset();stableAt_=0;lastRaw_=0;previous_={};returnPress_=0;neutral_=0;rightArmed_=rightHeld_=false;
    mappedAttachment_.reset();preparedWrists_.reset();preparedInput_={};reloadPress_.reset();
    if(debt_)retirementRequired_=true;
}
void Bc2BoltPlayerCustody::Capture(const Bc2BoltPlayerInput& in)noexcept {
    using namespace reload_insertion_detail;
    const auto& r=in.contact;const auto& a=in.ammunition;const auto& n=in.native;
    const auto now=in.safety.nowNs;
    const bool returned=selectionReturn_&&n&&n->selectionReturn&&n->selectionReturn->handBound&&
        n->selectionReturn->current==selectionReturn_->current&&n->selectionReturn->cycle==selectionReturn_->cycle&&
        n->selectionReturn->shot==selectionReturn_->shot&&n->selectionReturn->callbackRevision==selectionReturn_->callbackRevision&&
        n->retainedDebtOrigin&&n->debt&&n->native==selectionReturn_->current&&n->native.owner==in.owner&&
        n->debt->owner==in.safety.owner&&n->debt->item==in.item&&
        n->debt->mechanism==HandInteractionKey{calibration_->profile.id,in.item.generation}&&
        n->debt->cycle==selectionReturn_->cycle&&n->debt->shot==selectionReturn_->shot&&
        n->debt->guardEpoch==selectionReturn_->guardEpoch&&n->phase==Bc2NativeCyclePhase::Advancing&&
        n->blocksFire&&!n->ready&&!n->releasePending&&a&&a->identity==n->native&&
        a->loaded==n->debt->loaded&&a->reserve==n->debt->reserve&&a->capacity==n->debt->capacity&&
        r.raw.input.observedNs>=selectionReturn_->observedNs;
    if(references_||(debt_&&!returned)||retirementRequired_)return;
    const bool idle=a&&a->verified&&a->identity.owner==in.owner&&a->sequence&&a->allThreeIdle&&(a->loaded>0||(returned&&a->loaded==0))&&
        a->loaded<=a->capacity&&a->reserve>=0&&weapon_cycle_detail::Window(a->observedNs,a->deadlineNs,now)&&
        n&&(returned||(!n->blocksFire&&!n->held&&!n->ready&&
        (n->phase==Bc2NativeCyclePhase::Watching||n->phase==Bc2NativeCyclePhase::Complete)));
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
std::optional<Bc2PhysicalBoltSample> Bc2BoltPlayerCustody::Prepare(const Bc2BoltPlayerInput& incoming,
    const HandInteraction& hands,std::uint64_t& intent)noexcept {
    auto in=incoming;
    const auto& s=in.safety;const auto* controls=in.controls;
    if(!calibration_||!BoltCalibrationValid(*calibration_)||!controls)return {};
    const bool returned=!owner_.player&&in.native&&in.native->selectionReturn&&in.native->retainedDebtOrigin&&
        in.native->selectionReturn->current==in.native->native&&in.native->native.owner==in.owner&&
        in.native->selectionReturn->cycle==in.native->cycle&&in.native->selectionReturn->shot==in.native->shot&&
        !in.native->releasePending&&!in.native->ready;
    if(in.native&&in.native->blocksFire)debt_=true;
    const bool identity=owner_==in.owner&&item_==in.item&&units_==controls->worldUnitsPerMeter;
    if(!identity){Invalidate();recoveryReferences_.reset();leftCustody_.reset();selectionReturn_.reset();
        if(returned){selectionReturn_=in.native->selectionReturn;retirementRequired_=false;}
        owner_=in.owner;item_=in.item;units_=controls->worldUnitsPerMeter;selectedAt_=s.nowNs;}
    // Missing render publication is not a different gun. Established custody
    // can map CURRENT controllers through its immutable wrist mapping; it does
    // not reuse the old rendered pose. Before custody, cached raw remains bound
    // to its original lifetime. Explicit raw mismatches always reject.
    const bool missing=!in.contact.raw.valid&&!in.contact.mappingValid&&
        in.contact.raw.nativeOwner==ReloadStateOwner{}&&!in.contact.raw.input.sequence&&!in.contact.raw.rigFingerprint;
    const auto mappedGun=mappedCustody_?hands.Current(mappedCustody_->hand):std::nullopt;
    const bool established=wristMapping_&&mappedCustody_&&mappedGun&&mappedGun->token==*mappedCustody_&&
        mappedCustody_->kind==HandClaimKind::GunHold&&!mappedCustody_->prerequisiteClaim&&
        mappedCustody_->owner==s.owner&&mappedCustody_->item==in.item&&
        wristMapping_->nativeOwner==in.owner&&wristMapping_->owner==s.owner&&
        wristMapping_->rigFingerprint==calibration_->rigFingerprint&&wristMapping_->units==controls->worldUnitsPerMeter;
    if(missing&&!established&&lastContact_&&
       weapon_cycle_detail::Window(lastContact_->raw.input.observedNs,lastContact_->raw.input.deadlineNs,s.nowNs))in.contact=*lastContact_;
    const bool exact=in.asset==calibration_->asset&&in.mesh==calibration_->mesh&&
        ((missing&&established)||(in.contact.raw.nativeOwner==in.owner&&in.contact.raw.rigFingerprint==calibration_->rigFingerprint&&
          in.contact.raw.rig.soldier==in.owner.soldier&&in.contact.raw.rig.weak==in.owner.weak))&&
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
    // A render gap before new custody starts does not turn this proven return
    // into an unknown old actor. No cached pre-switch reference is restored.
    if(selectionReturn_&&in.native&&in.native->selectionReturn&&
       in.native->selectionReturn->callbackRevision==selectionReturn_->callbackRevision&&
       in.native->selectionReturn->current==selectionReturn_->current&&in.owner==selectionReturn_->current.owner&&
       !in.native->releasePending&&!in.native->ready&&!leftCustody_&&
       (custody_==BoltCustodyPhase::Idle||custody_==BoltCustodyPhase::AwaitingCustody)){
        retirementRequired_=false;recoveryReferences_.reset();
    }
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
    const bool late=established&&(missing||(in.contact.raw.input.owner==s.owner&&
        in.contact.raw.input.sequence<=s.sequence&&in.contact.raw.input.observedNs<=s.observedNs&&
        s.nowNs>=in.contact.raw.input.deadlineNs));
    const auto wrists=late?MapBoltCustodyWrists(*wristMapping_,*controls,s,in.body):
        MapBoltTrackedWrists(in.contact,*controls,s,in.body);
    if(!wrists)return {};
    if(!late){
        lastContact_=in.contact;
        wristMapping_=Bc2BoltWristMapping{in.contact.raw.nativeOwner,in.contact.raw.input.owner,
            in.contact.raw.rigFingerprint,in.contact.wristToGrip,in.contact.units};
        Capture(in);
    }
    const bool fresh=s.sequence>previous_.sequence;
    const bool release=fresh&&previous_.sequence&&previous_.owner==s.owner&&!previous_.released[1]&&s.released[1];
    const bool press=fresh&&rightArmed_&&controls->hands[1].squeeze>=.7f;
    if(fresh){
        if(s.released[1]){rightArmed_=true;rightHeld_=false;}
        else if(press){rightArmed_=false;rightHeld_=true;}
    }
    if(submitted_&&fresh&&s.sequence>submitted_&&s.released[1]){neutral_=s.sequence;returnPress_=0;}
    if(neutral_&&press&&s.sequence>neutral_)returnPress_=s.sequence;
    if(custody_!=BoltCustodyPhase::ReloadPaused||s.released[1]||
       (reloadPress_&&!weapon_cycle_detail::Window(reloadPress_->observedNs,reloadPress_->deadlineNs,s.nowNs)))reloadPress_.reset();
    if(custody_==BoltCustodyPhase::ReloadPaused&&press)reloadPress_=s;
    Bc2PhysicalBoltSample out;out.nativeOwner=in.owner;out.input=s;out.item=in.item;out.asset=in.asset;out.mesh=in.mesh;
    out.raw=in.contact.raw;out.wristWorld=*wrists;out.grip=rightHeld_;
    if(established)out.controllerCustody=mappedCustody_;
    const bool held=in.native&&in.native->held&&in.native->native.owner==in.owner&&
        in.native->held->owner==s.owner&&in.native->held->item==in.item&&
        in.native->held->mechanism==HandInteractionKey{calibration_->profile.id,in.item.generation}&&
        weapon_cycle_detail::Lease(*in.native->held,s.nowNs);
    if(references_&&!retirementRequired_){
        const bool paused=custody_==BoltCustodyPhase::ReloadPaused||custody_==BoltCustodyPhase::Reloading||
            custody_==BoltCustodyPhase::ReloadResumePending;
        const bool enter=release&&held&&custody_!=BoltCustodyPhase::Manipulating&&!paused;
        const bool back=custody_==BoltCustodyPhase::Manipulating&&returnPress_&&!s.released[1]&&
            in.contact.raw.input.sequence>=returnPress_&&!in.contact.raw.input.released[1];
        if(!late&&(enter||back)){
            out.custodyTransfer=BuildBoltCustodyTransfer(s,in.item,in.contact,*references_,hands,enter,back,intent,enter);
            if(out.custodyTransfer&&enter){
                const auto weapon=ReprojectBoltCustodyWeapon(in.contact,*wrists,InteractionHand::Left);
                if(weapon)out.weaponWorld=*weapon;else out.custodyTransfer.reset();
            }
        }
        if(custody_==BoltCustodyPhase::Manipulating&&mechanismPhase_==WeaponCyclePhase::Forward&&release)
            out.reloadIntent=BoltReloadCustodyIntent::Pause;
        if(!late&&custody_==BoltCustodyPhase::ReloadPaused&&reloadPress_&&!s.released[1]&&
           in.contact.raw.input.sequence>=reloadPress_->sequence&&!in.contact.raw.input.released[1]){
            out.reloadIntent=BoltReloadCustodyIntent::Pause;
            out.custodyTransfer=BuildBoltReloadCustodyTransfer(s,in.item,in.contact,*references_,hands,*out.reloadIntent,intent);
        }
        if(custody_==BoltCustodyPhase::Reloading&&release){
            out.reloadIntent=BoltReloadCustodyIntent::Resume;
            if(established&&mappedAttachment_)
                out.custodyTransfer=BuildBoltMappedReloadResume(s,in.item,*mappedCustody_,
                    Multiply(*mappedAttachment_,(*wrists)[weapon_cycle_detail::HandIndex(mappedCustody_->hand)]),
                    *wrists,*references_,hands,intent);
            else if(!late)out.custodyTransfer=BuildBoltReloadCustodyTransfer(s,in.item,in.contact,*references_,hands,*out.reloadIntent,intent);
        }
    }
    for(unsigned hand=0;hand<2;++hand){
        const auto claim=hands.Current(static_cast<InteractionHand>(hand));
        if(claim)out.gunContacts[hand]={claim->token.contact,s.sequence,s.deadlineNs,true};
    }
    if(out.custodyTransfer){const auto& request=out.custodyTransfer->nextGun.request;
        out.gunContacts[weapon_cycle_detail::HandIndex(request.hand)]={request.contact.key,s.sequence,s.deadlineNs,true};}
    if(fresh)previous_=s;
    preparedInput_=s;preparedWrists_=wrists;
    if(retirementRequired_)out.cancel=true;
    return out;
}
void Bc2BoltPlayerCustody::Observe(const Bc2PhysicalBoltResult& result)noexcept {
    // Startup admission/backpressure can block Fire before a shot exists.
    // Only actual held/gesture/custody evidence may create physical debt.
    if(result.tracking.weapon&&result.tracking.gun&&wristMapping_&&
       result.tracking.nativeOwner==owner_&&result.tracking.input.owner==wristMapping_->owner&&
       CurrentBoltCustodyWeaponTarget(*result.tracking.weapon,result.tracking.input,*result.tracking.gun))
    {
        const auto token=result.tracking.gun->token;
        if(!mappedCustody_||*mappedCustody_!=token)mappedAttachment_.reset();
        mappedCustody_=token;
        if(preparedWrists_&&preparedInput_.owner==result.tracking.input.owner&&
           preparedInput_.sequence==result.tracking.input.sequence&&preparedInput_.observedNs==result.tracking.input.observedNs){
            const auto inverse=InverseRigid((*preparedWrists_)[weapon_cycle_detail::HandIndex(token.hand)]);
            if(inverse)mappedAttachment_=Multiply(result.tracking.weapon->weaponInWorld,*inverse);
        }
    }
    if(result.tracking.held)debtLease_=result.tracking.held;
    if(result.tracking.gun&&result.tracking.gun->token.hand==InteractionHand::Left&&
       result.tracking.gun->token.kind==HandClaimKind::GunHold)leftCustody_=result.tracking.gun->token;
    if(result.tracking.held||result.tracking.recovery||result.settled||result.recovered||
       result.tracking.custody==BoltCustodyPhase::Manipulating||result.tracking.custody==BoltCustodyPhase::ReloadPaused||
       result.tracking.custody==BoltCustodyPhase::Reloading||result.tracking.custody==BoltCustodyPhase::ReloadResumePending)debt_=true;
    if(result.tracking.enabled){
        custody_=result.tracking.custody;
        if(custody_!=BoltCustodyPhase::ReloadPaused)reloadPress_.reset();
        mechanismPhase_=result.tracking.mechanismPhase;
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
