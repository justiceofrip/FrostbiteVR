#include "fvr/interaction/DetachableMagazine.h"
namespace fvr::interaction {
namespace {
using namespace reload_insertion_detail;
ManualReloadConfig TransactionConfig(const DetachableMagazineConfig& c)noexcept {
    ManualReloadConfig out;out.steps[0]={ReloadOperation::UnseatMagazine,1};out.steps[1]={ReloadOperation::SeatMagazine,1};out.stepCount=2;
    out.maxSampleGapNs=c.insertion.maxSampleGapNs;out.ackTimeoutNs=c.ackTimeoutNs;out.transactionTimeoutNs=c.transactionTimeoutNs;return out;
}
ManualReloadOwner RequestOwner(HandInteractionOwner owner,HandInteractionKey weapon)noexcept {
    return {owner.actor,owner.actorGeneration,weapon.id,owner.equipGeneration,owner.space};
}
math::Matrix4 Attached(const ReloadInsertionProfile& p)noexcept {
    return Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,p.travelMeters),p.weaponFromEntry));
}
bool SameGeometryEvidence(const HandInteractionSample& a,const HandInteractionSample& b)noexcept {
    return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
        a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
}
}
DetachableMagazine::DetachableMagazine(DetachableMagazineConfig c)noexcept:config_(c),insertion_(c.insertion),transaction_(TransactionConfig(c)){}
bool DetachableMagazine::ValidConfig()const noexcept {
    const auto& c=config_;
    return ValidateReloadInsertionProfile(c.insertion)&&c.insertion.family==ReloadInsertionFamily::Magazine&&
        c.insertion.orientation==ReloadInsertionOrientation::Keyed&&Key(c.removalContact)&&
        (c.hand==InteractionHand::Left||c.hand==InteractionHand::Right)&&
        std::isfinite(c.grabRadiusMeters)&&c.grabRadiusMeters>0&&std::isfinite(c.pullMeters)&&c.pullMeters>0&&
        std::isfinite(c.pullReleaseMeters)&&c.pullReleaseMeters>c.grabRadiusMeters&&
        std::isfinite(c.maxPullStepMeters)&&c.maxPullStepMeters>0&&c.maxPullStepMeters<c.pullMeters&&transaction_.ValidConfig();
}
DetachableMagazineResult DetachableMagazine::Snapshot()const noexcept {
    auto out=cached_;out.transaction.request.reset();out.transaction.acknowledged=out.transaction.completed=out.transaction.cancelled=false;
    out.insertion.seat.reset();out.insertion.captured=out.insertion.cancelled=false;out.insertion.haptic=ReloadInsertionHaptic::None;
    out.seat.reset();out.originalSeat.reset();out.removalGrabbed=out.physicallyRemoved=out.cancelNativeCycle=false;out.cancelledRequest=0;return out;
}
DetachableMagazineResult DetachableMagazine::Reject(const HandInteractionSample& in,HandInteraction& hands,DetachableMagazineReason why)noexcept {
    if(removal_)hands.Release(in,removal_->token);removal_.reset();insertion_.Reset();
    const auto result=transaction_.Reset();
    if(pending_)cancelledRequest_=pending_->id;else if(result.cancelledRequest)cancelledRequest_=result.cancelledRequest;
    pending_.reset();originalInsertion_.reset();originalSeat_.reset();neutral_=false;cancelledNs_=in.nowNs;
    cached_={};cached_.phase=DetachableMagazinePhase::Cancelled;cached_.reason=why;cached_.nativeCycle=cycle_;
    cached_.cancelNativeCycle=gateAccepted_||cycle_||cancelledRequest_;cached_.cancelledRequest=cancelledRequest_;cached_.transaction=result;return cached_;
}
DetachableMagazineResult DetachableMagazine::Cancel(const HandInteractionSample& in,HandInteraction& hands)noexcept {
    return Reject(in,hands,DetachableMagazineReason::Explicit);
}
bool DetachableMagazine::ResetAttached()noexcept {
    if(cached_.phase!=DetachableMagazinePhase::Attached||pending_||removal_||gateAccepted_||cycle_)return false;
    transaction_.Reset();insertion_.Reset();original_.reset();originalInsertion_.reset();originalSeat_.reset();cached_={};seen_=neutral_=false;lastGeometry_=0;history_={};historyNext_=0;return true;
}
bool DetachableMagazine::Reconfigure(DetachableMagazineConfig config)noexcept {
    if(!DetachableMagazine(config).ValidConfig()||!ResetAttached())return false;
    // ResetAttached has established idle transaction/insertion state. Neither
    // reconfiguration reconstructs the coordinator or recycles an event ID.
    if(!transaction_.Reconfigure(TransactionConfig(config))||!insertion_.Reconfigure(config.insertion))return false;
    config_=config;return true;
}
bool DetachableMagazine::RejectUnstarted(const HandInteractionSample& in,HandInteraction& hands,const ManualReloadRequest& r)noexcept {
    if(cached_.phase!=DetachableMagazinePhase::PreparingRemoval||!pending_||pending_->id!=r.id||pending_->owner!=r.owner||
       pending_->operation!=ReloadOperation::UnseatMagazine||r.operation!=pending_->operation||gateAccepted_||cycle_||in.nowNs<lastNow_)return false;
    if(removal_)hands.Release(in,removal_->token);removal_.reset();transaction_.Reset();insertion_.Reset();
    pending_.reset();original_.reset();originalInsertion_.reset();originalSeat_.reset();cached_={};seen_=neutral_=ejectMode_=pulled_=gateAccepted_=false;
    cycle_=cancelledRequest_=lastGeometry_=0;history_={};historyNext_=0;return true;
}
bool DetachableMagazine::Rebaseline(const HandInteractionSample& in,HandInteraction& hands,const MagazineRebaseline& r)noexcept {
    if((cached_.phase!=DetachableMagazinePhase::Cancelled&&cached_.phase!=DetachableMagazinePhase::Complete)||!r.verified||!r.retirement||r.retirement<=lastRetirement_||
       r.retiredOwner!=owner_||r.retiredWeapon!=weapon_||r.retiredCycle!=cycle_||r.cancelledRequest!=cancelledRequest_||
       r.observedNs<cancelledNs_||r.observedNs>in.nowNs||r.deadlineNs<=in.nowNs||r.deadlineNs-r.observedNs>200000000||
       !Owner(in.owner)||!in.sequence||!in.focused||in.observedNs<=0||in.observedNs>in.nowNs||in.deadlineNs<=in.nowNs)return false;
    if(removal_)hands.Release(in,removal_->token);removal_.reset();transaction_.Reset();insertion_.Reset();
    lastRetirement_=r.retirement;original_.reset();originalInsertion_.reset();originalSeat_.reset();cached_={};seen_=neutral_=ejectMode_=pulled_=gateAccepted_=false;
    pending_.reset();cycle_=cancelledRequest_=lastGeometry_=0;history_={};historyNext_=0;return true;
}
bool DetachableMagazine::CompleteOriginalReturn(const HandInteractionSample& in,HandInteraction& hands,
 const OriginalMagazineReturnReceipt& r)noexcept {
 if(cached_.phase!=DetachableMagazinePhase::AwaitingOriginalReturn||!original_||!originalSeat_||!r.verified||
    r.original!=*original_||r.cycle!=cycle_||r.seat!=originalSeat_->id||!r.retirement||r.retirement<=lastRetirement_||
    r.observedNs<originalSeatNs_||r.observedNs>in.nowNs||r.deadlineNs<=in.nowNs||r.deadlineNs-r.observedNs>200000000||
    in.owner!=owner_||!in.focused||!in.tracked[0]||!in.tracked[1]||in.released[1]||!in.sequence||
    in.observedNs<=0||in.observedNs>in.nowNs||in.deadlineNs<=in.nowNs||in.deadlineNs-in.observedNs>200000000)return false;
 if(removal_)hands.Release(in,removal_->token);removal_.reset();transaction_.Reset();insertion_.Reset();
 lastRetirement_=r.retirement;original_.reset();originalInsertion_.reset();originalSeat_.reset();cached_={};
 seen_=neutral_=ejectMode_=pulled_=gateAccepted_=false;pending_.reset();cycle_=cancelledRequest_=lastGeometry_=0;
 history_={};historyNext_=0;return true;
}
MagazinePropTarget DetachableMagazine::Target(const DetachableMagazineSample& s,const HandClaim& gun,MagazinePropRole role,
    const math::Matrix4& item,const math::Matrix4& hand,std::optional<HandClaim> claim)const noexcept {
    MagazinePropTarget out;out.role=role;out.owner=s.input.owner;out.weapon=s.weapon;out.profile={config_.insertion.id,config_.insertion.revision};
    out.gunClaim=gun.token;out.trackingEpoch=s.trackingEpoch;out.inputSequence=s.input.sequence;out.nativeCycle=cycle_;
    // Rendering retains only the original controller geometry and exact claim bounds.
    // Native observation remains independently mandatory for interaction authority.
    out.observedNs=s.input.observedNs;out.deadlineNs=std::min(s.input.deadlineNs,gun.deadlineNs);
    if(claim){out.handClaim=claim->token;out.item=claim->token.item;out.deadlineNs=std::min(out.deadlineNs,claim->deadlineNs);out.handTarget=true;}
    out.originalMagazine=original_;out.weaponFromItemMeters=item;out.weaponFromHandMeters=hand;return out;
}
DetachableMagazineResult DetachableMagazine::Update(const DetachableMagazineSample& s,HandInteraction& hands)noexcept {
    const auto& in=s.input;const auto fail=[&](DetachableMagazineReason why){return Reject(in,hands,why);};
    const auto failNative=[&](MagazineNativeFailureCheck check){
        fail(DetachableMagazineReason::UnverifiedNative);cached_.nativeFailureCheck=check;return cached_;
    };
    const auto failMotion=[&](DetachableMagazineReason reason,MagazineMotionFailureCheck check,double measured=0,double limit=0){
        const MagazineMotionFailure evidence{check,s.gripPressed,in.released[static_cast<unsigned>(config_.hand)],pulled_,s.geometrySequence,measured,limit};
        fail(reason);cached_.motionFailure=evidence;return cached_;
    };
    if(!ValidConfig())return fail(DetachableMagazineReason::InvalidConfig);
    if(cached_.phase==DetachableMagazinePhase::Cancelled){auto out=Snapshot();out.reason=DetachableMagazineReason::NeedsReconciliation;return out;}
    if(!Owner(in.owner)||!Key(s.weapon)||!s.trackingEpoch||!in.sequence||in.observedNs<=0||in.observedNs>in.nowNs||
       in.deadlineNs<=in.nowNs||in.deadlineNs-in.observedNs>config_.insertion.maxSampleGapNs||in.nowNs<lastNow_)
        return fail(DetachableMagazineReason::InvalidInput);
    if(seen_&&(in.owner!=owner_||s.weapon!=weapon_||s.trackingEpoch!=epoch_))return fail(DetachableMagazineReason::IdentityChanged);
    const bool fresh=!seen_||in.sequence>sequence_;
    if(seen_&&(in.sequence<sequence_||in.observedNs<observed_||
       (!fresh&&(in.observedNs!=observed_||in.deadlineNs!=deadline_))||(fresh&&in.observedNs>=deadline_)))return fail(DetachableMagazineReason::StaleInput);
    lastNow_=in.nowNs;
    if(!in.focused||!in.tracked[0]||!in.tracked[1])return fail(DetachableMagazineReason::TrackingLost);
    if(s.cancel)return fail(DetachableMagazineReason::Explicit);
    const auto& native=s.native;
    if(!native.bindingsVerified||native.owner!=in.owner||native.weapon!=s.weapon||(!s.nativeObservationDeferred&&(native.observedNs<=0||native.observedNs>in.nowNs||
       native.deadlineNs<=in.nowNs||native.deadlineNs-native.observedNs>200000000)))return failNative(MagazineNativeFailureCheck::Observation);
    if(gateAccepted_&&native.cycle!=cycle_)return fail(DetachableMagazineReason::NativeCycleChanged);
    const auto other=config_.hand==InteractionHand::Left?InteractionHand::Right:InteractionHand::Left;
    const auto gun=hands.Current(other);
    if(!gun||gun->token.kind!=HandClaimKind::GunHold||gun->token.owner!=in.owner||gun->token.item!=s.weapon||
       gun->inputSequence!=in.sequence||gun->deadlineNs<=in.nowNs)return fail(DetachableMagazineReason::GunClaimLost);
    if(s.nativeObservationDeferred){
        // Poll only the existing command's safety and ORIGINAL timeout anchors.
        // No gesture, acknowledgement, or synthetic neutral advances its plan.
        ManualReloadSample waiting;waiting.owner=RequestOwner(in.owner,s.weapon);waiting.sequence=in.sequence;waiting.nowNs=in.nowNs;
        waiting.focused=in.focused;waiting.tracked=in.tracked[0]&&in.tracked[1];waiting.bindingsVerified=native.bindingsVerified;
        const auto transaction=transaction_.Update(waiting);
        if(transaction.cancelled||transaction.reason!=ManualReloadCancel::None){
            fail(DetachableMagazineReason::NativeRejected);cached_.transaction=transaction;return cached_;
        }
    }
    if(!fresh)return Snapshot(); // Real safety above can cancel; duplicates cannot move or acknowledge.
    owner_=in.owner;weapon_=s.weapon;epoch_=s.trackingEpoch;sequence_=in.sequence;observed_=in.observedNs;deadline_=in.deadlineNs;seen_=true;
    if(s.nativeObservationDeferred){
        // Only existing exact hand ownership may follow a fresh input packet.
        // No geometry/history/transaction/insertion/presentation evidence advances.
        if(removal_){
            if(!s.gripPressed||in.released[static_cast<unsigned>(config_.hand)]){
                if(!pulled_)return failMotion(DetachableMagazineReason::PullAbandoned,MagazineMotionFailureCheck::GripReleased);
                hands.Release(in,removal_->token);removal_.reset();
            }else{
                const auto renewed=hands.Renew(in,removal_->token,{config_.removalContact,in.sequence,in.deadlineNs,true});
                if(!renewed.accepted||!renewed.claim)return fail(DetachableMagazineReason::ClaimLost);
                removal_=renewed.claim;
            }
        }
        cached_.removalClaim=removal_;return Snapshot();
    }
    const auto historySlot=historyNext_;history_[historySlot]=GeometryEvidence{in,*gun,s.replacement,removal_};historyNext_=(historyNext_+1)%history_.size();
    const auto& geometryInput=s.geometryInput?*s.geometryInput:in;
    const GeometryEvidence* geometry=nullptr;
    for(const auto& record:history_)if(record&&SameGeometryEvidence(record->input,geometryInput)){geometry=&*record;break;}
    if(!geometry||s.geometrySequence!=geometryInput.sequence||geometryInput.owner!=in.owner||geometryInput.observedNs>in.observedNs||
       geometryInput.deadlineNs<=in.nowNs||geometry->gun.token!=gun->token||geometry->gun.deadlineNs<=in.nowNs||!Rigid(s.weaponFromHandMeters))geometry=nullptr;
    const bool freshGeometry=geometry&&s.geometrySequence>lastGeometry_;
    auto geometrySample=s;if(geometry)geometrySample.input=geometryInput;
    auto out=Snapshot();out.reason=DetachableMagazineReason::None;out.prop.reset();out.removalClaim.reset();
    if(cached_.prop&&cached_.prop->deadlineNs>in.nowNs)out.prop=cached_.prop;
    ManualReloadSample physical;physical.owner=RequestOwner(owner_,weapon_);physical.sequence=in.sequence;physical.nowNs=in.nowNs;
    physical.focused=in.focused;physical.tracked=in.tracked[0]&&in.tracked[1];physical.bindingsVerified=true;
    physical.neutral=!s.gripPressed&&!s.ejectPressed;
    if(native.acknowledgementVerified)physical.acknowledgement=native.acknowledgement;
    const auto phase=cached_.phase;const auto attached=Attached(config_.insertion);
    const auto attachedHand=Multiply(config_.insertion.itemFromHand,attached);
    const auto gesture=[&](ReloadOperation op){
        if(nextGesture_==std::numeric_limits<std::uint64_t>::max())return false;
        physical.gesture={++nextGesture_,op};physical.neutral=false;return true;
    };
    bool pulledNow=false;
    if(phase==DetachableMagazinePhase::Attached){
        out.prop=Target(s,*gun,MagazinePropRole::Attached,attached,attachedHand);
        if(!s.removalPermitted){neutral_=physical.neutral;}
        else if(physical.neutral)neutral_=true;
        else if(neutral_){
            neutral_=false;ejectMode_=s.ejectPressed;pulled_=ejectMode_;
            if(s.original){const auto& o=*s.original;
                if(o.owner!=in.owner||o.weapon!=s.weapon||o.profile!=HandInteractionKey{config_.insertion.id,config_.insertion.revision}||
                   !Key(o.item)||o.item==s.weapon||!Key(o.pool)||o.trackingEpoch!=s.trackingEpoch||!o.sourceSequence||
                   o.observedNs<=0||o.observedNs>in.nowNs||o.deadlineNs<=in.nowNs||o.deadlineNs-o.observedNs>200000000||
                   !o.capacity||o.rounds>o.capacity)return fail(DetachableMagazineReason::UnverifiedNative);
                original_=o;
            }
            if(!ejectMode_){
                if(!geometry){out.reason=DetachableMagazineReason::InvalidInput;cached_=out;return out;}
                if(Distance(s.weaponFromHandMeters,attachedHand)>config_.grabRadiusMeters){out.reason=DetachableMagazineReason::OutsideMagazine;cached_=out;return out;}
                const auto acquired=hands.AcquireFrom(in,geometryInput,{in.owner,config_.hand,HandClaimKind::Mechanism,s.weapon,
                    {config_.removalContact,geometryInput.sequence,geometryInput.deadlineNs,true},s.intent,gun->token.id});
                if(!acquired.accepted||!acquired.claim){out.reason=DetachableMagazineReason::HandUnavailable;cached_=out;return out;}
                removal_=acquired.claim;grabRaw_=previousRaw_=s.weaponFromHandMeters;
                itemFromGrip_=Multiply(s.weaponFromHandMeters,*InverseRigid(attached));out.removalGrabbed=true;
                if(original_){auto profile=config_.insertion;profile.itemFromHand=itemFromGrip_;originalInsertion_.emplace(profile);}
            }
            if(!gesture(ReloadOperation::UnseatMagazine))return fail(DetachableMagazineReason::CounterExhausted);
            out.phase=DetachableMagazinePhase::PreparingRemoval;
        }else out.reason=DetachableMagazineReason::NeedNeutral;
    }
    if(removal_){
        if(!s.gripPressed||in.released[static_cast<unsigned>(config_.hand)]){
            if(!pulled_)return failMotion(DetachableMagazineReason::PullAbandoned,MagazineMotionFailureCheck::GripReleased);
            hands.Release(in,removal_->token);removal_.reset();
        }else{
            const auto expected=*removal_;const auto current=hands.Current(config_.hand);
            // Physical grip ownership follows this original tracked input.
            // Native observation expiry still gates progress and presentation;
            // it must not pre-expire the hand before a fresh native read arrives.
            const auto renewed=hands.Renew(in,removal_->token,{config_.removalContact,in.sequence,in.deadlineNs,true});
            if(!renewed.accepted||!renewed.claim){
                fail(DetachableMagazineReason::ClaimLost);
                cached_.claimFailure=MagazineClaimFailure{renewed.reason,expected,current,s.geometrySequence,native.deadlineNs};
                return cached_;
            }
            removal_=renewed.claim;
            // Rail continuity gates extraction only. Once removed, ordinary
            // tracked carry/rotation must not abort the native reload cycle.
            // Reinsertion keeps its own capture, continuity and seat checks.
            if(freshGeometry&&!pulled_){
                const auto step=Distance(s.weaponFromHandMeters,previousRaw_);
                const auto turn=Angle(s.weaponFromHandMeters,previousRaw_);
                if(step>config_.maxPullStepMeters)return failMotion(DetachableMagazineReason::PoseJump,MagazineMotionFailureCheck::TranslationStep,step,config_.maxPullStepMeters);
                if(turn>config_.insertion.maxStepRadians)return failMotion(DetachableMagazineReason::PoseJump,MagazineMotionFailureCheck::RotationStep,turn,config_.insertion.maxStepRadians);
            }
            history_[historySlot]->removal=removal_;if(freshGeometry)previousRaw_=s.weaponFromHandMeters;
            const auto axisPose=Multiply(TravelPose(config_.insertion,1),config_.insertion.weaponFromEntry);
            double along=0,square=0;
            for(unsigned n=0;n<3;++n){const double axis=axisPose.values[3][n]-config_.insertion.weaponFromEntry.values[3][n];
                const double d=s.weaponFromHandMeters.values[3][n]-grabRaw_.values[3][n];along-=d*axis;square+=d*d;}
            const double radial=std::sqrt(std::max(0.,square-along*along));
            if(freshGeometry&&!pulled_&&radial>config_.pullReleaseMeters)return failMotion(DetachableMagazineReason::PullAbandoned,MagazineMotionFailureCheck::OffAxis,radial,config_.pullReleaseMeters);
            if(freshGeometry&&!pulled_&&along<-config_.pullReleaseMeters)return failMotion(DetachableMagazineReason::PullAbandoned,MagazineMotionFailureCheck::ReversePull,along,-config_.pullReleaseMeters);
            if(freshGeometry&&!pulled_&&along>=config_.pullMeters){pulled_=true;pulledNow=true;}
            if(freshGeometry){const auto item=Multiply(*InverseRigid(itemFromGrip_),s.weaponFromHandMeters);
                out.prop=Target(geometrySample,geometry->gun,MagazinePropRole::Removed,item,s.weaponFromHandMeters,removal_);}
            out.removalClaim=removal_;
        }
    }
    const bool matchingAck=pending_&&physical.acknowledgement.request==pending_->id&&physical.acknowledgement.owner==pending_->owner&&
        physical.acknowledgement.operation==pending_->operation;
    if(matchingAck&&physical.acknowledgement.status==ReloadAcknowledgement::Applied&&pending_->operation==ReloadOperation::UnseatMagazine&&
       (!native.allThreeHeld||!native.cycle))return failNative(MagazineNativeFailureCheck::UnseatAcknowledgement);
    if(gateAccepted_&&phase!=DetachableMagazinePhase::AwaitingSeat&&phase!=DetachableMagazinePhase::AwaitingOriginalReturn&&phase!=DetachableMagazinePhase::Complete&&!native.allThreeHeld)
        return failNative(MagazineNativeFailureCheck::HeldCycle);
    if(phase==DetachableMagazinePhase::AwaitingSeat){
        // Physical seating transfers presentation to the gun, so releasing the
        // loading hand cannot hide the inserted magazine during native reload.
        // Every new target still needs this fresh exact input/native owner and
        // GunHold; the original seat/request never gains a new lifetime or an
        // ammunition acknowledgement from this attachment.
        out.prop=Target(s,*gun,MagazinePropRole::Attached,attached,attachedHand);
    }
    if(gateAccepted_&&pulled_&&phase!=DetachableMagazinePhase::AwaitingSeat&&phase!=DetachableMagazinePhase::AwaitingOriginalReturn&&phase!=DetachableMagazinePhase::Complete){
        out.phase=removal_?DetachableMagazinePhase::RemovedHeld:DetachableMagazinePhase::WellEmpty;
        if(removal_&&original_&&originalInsertion_&&geometry&&geometry->removal&&
           geometry->removal->token==removal_->token&&geometry->removal->deadlineNs>in.nowNs){
            ReloadInsertionSample contact;contact.identity={owner_,weapon_,original_->item,epoch_};contact.profile=original_->profile;
            contact.itemClaim=*geometry->removal;contact.weaponClaim=geometry->gun;contact.sequence=contact.geometrySequence=geometryInput.sequence;
            contact.observedNs=geometryInput.observedNs;contact.deadlineNs=std::min(geometryInput.deadlineNs,native.deadlineNs);contact.nowNs=in.nowNs;
            contact.focused=in.focused;contact.itemTracked=contact.weaponTracked=true;contact.held=s.gripPressed;contact.eligible=true;
            contact.itemSource=ReloadInsertionItemSource::RetainedMagazine;contact.weaponFromHand=s.weaponFromHandMeters;
            out.insertion=originalInsertion_->Update(contact);
            if(out.insertion.rawItem){const auto item=out.insertion.guidedItem.value_or(*out.insertion.rawItem);
                out.prop=Target(geometrySample,geometry->gun,MagazinePropRole::Removed,item,Multiply(itemFromGrip_,item),geometry->removal);}
            if(out.insertion.seat){originalSeatNs_=in.nowNs;originalSeat_=out.insertion.seat;out.originalSeat=originalSeat_;out.original=original_;
                out.phase=DetachableMagazinePhase::AwaitingOriginalReturn;
                hands.Release(in,removal_->token);removal_.reset();out.removalClaim.reset();
                out.prop=Target(s,*gun,MagazinePropRole::Attached,attached,attachedHand);
            }
        }
        if(!removal_&&out.phase!=DetachableMagazinePhase::AwaitingOriginalReturn){
            out.prop=Target(s,*gun,MagazinePropRole::Hidden,attached,attachedHand);
            if(s.replacement){
                const auto& item=*s.replacement;const auto claim=hands.Current(config_.hand);
                if(item.family!=ReloadInsertionFamily::Magazine||!item.units||item.identity.owner!=owner_||item.identity.weapon!=weapon_||
                   item.identity.profile!=HandInteractionKey{config_.insertion.id,config_.insertion.revision}||item.identity.trackingEpoch!=epoch_||
                   !claim||claim->token!=item.claim.token||claim->token.item!=item.item||claim->token.kind!=HandClaimKind::AmmoObject||
                   claim->token.hand!=config_.hand||claim->inputSequence!=in.sequence||claim->deadlineNs<=in.nowNs)
                    return fail(DetachableMagazineReason::ReplacementRejected);
                if(!geometry||!geometry->replacement||geometry->replacement->item!=item.item||
                   geometry->replacement->claim.token!=claim->token||geometry->replacement->claim.deadlineNs<=in.nowNs){
                    out.phase=DetachableMagazinePhase::ReplacementHeld;
                    if(cached_.prop&&cached_.prop->handClaim==claim->token&&cached_.prop->deadlineNs>in.nowNs){
                        out.prop=cached_.prop;if(cached_.phase==DetachableMagazinePhase::Guided)out.phase=cached_.phase;}
                }else{
                ReloadInsertionSample contact;contact.identity={owner_,weapon_,item.item,epoch_};contact.profile=item.identity.profile;
                contact.itemClaim=geometry->replacement->claim;contact.weaponClaim=geometry->gun;contact.sequence=contact.geometrySequence=geometryInput.sequence;
                contact.observedNs=geometryInput.observedNs;contact.deadlineNs=std::min(geometryInput.deadlineNs,native.deadlineNs);contact.nowNs=in.nowNs;
                contact.focused=in.focused;contact.itemTracked=contact.weaponTracked=true;contact.held=s.gripPressed;contact.eligible=true;
                contact.weaponFromHand=s.weaponFromHandMeters;out.insertion=insertion_.Update(contact);
                if(out.insertion.rawItem){const auto itemPose=out.insertion.guidedItem.value_or(*out.insertion.rawItem);
                    const auto wrist=Multiply(config_.insertion.itemFromHand,itemPose);
                    out.prop=Target(geometrySample,geometry->gun,MagazinePropRole::Replacement,itemPose,wrist,geometry->replacement->claim);
                    out.phase=out.insertion.phase==ReloadInsertionPhase::Free?DetachableMagazinePhase::ReplacementHeld:DetachableMagazinePhase::Guided;
                    if(out.insertion.phase==ReloadInsertionPhase::Free){
                        const auto raw=Multiply(Multiply(config_.insertion.itemFromInsertion,*out.insertion.rawItem),*InverseRigid(config_.insertion.weaponFromEntry));
                        physical.neutral=OutsideEntryApproach(raw,config_.insertion)&&!s.ejectPressed;
                    }
                }
                if(out.insertion.seat){out.seat=out.insertion.seat;
                    if(!gesture(ReloadOperation::SeatMagazine))return fail(DetachableMagazineReason::CounterExhausted);}
                }
            }else insertion_.Reset();
        }
    }
    if(phase==DetachableMagazinePhase::AwaitingOriginalReturn){out.prop=Target(s,*gun,MagazinePropRole::Attached,attached,attachedHand);
        out.nativeCycle=cycle_;out.original=original_;cached_=out;return out;}
    out.transaction=transaction_.Update(physical);
    if(out.transaction.cancelled||out.transaction.reason!=ManualReloadCancel::None)return fail(DetachableMagazineReason::NativeRejected);
    if(out.transaction.request){pending_=out.transaction.request;
        if(pending_->operation==ReloadOperation::SeatMagazine)out.phase=DetachableMagazinePhase::AwaitingSeat;}
    if(out.transaction.acknowledged){
        const auto operation=pending_?pending_->operation:ReloadOperation::None;pending_.reset();
        if(operation==ReloadOperation::UnseatMagazine){gateAccepted_=true;cycle_=native.cycle;
            out.phase=pulled_?(removal_?DetachableMagazinePhase::RemovedHeld:DetachableMagazinePhase::WellEmpty):DetachableMagazinePhase::Pulling;
            out.physicallyRemoved=pulled_;}
        if(operation==ReloadOperation::SeatMagazine){out.phase=DetachableMagazinePhase::Complete;cancelledNs_=in.nowNs;
            out.prop=Target(s,*gun,MagazinePropRole::Attached,attached,attachedHand);}
    }
    if(gateAccepted_&&pulledNow)out.physicallyRemoved=true;
    if(out.phase==DetachableMagazinePhase::WellEmpty)out.prop=Target(s,*gun,MagazinePropRole::Hidden,attached,attachedHand);
    if(freshGeometry)lastGeometry_=s.geometrySequence;
    out.nativeCycle=cycle_;out.original=original_;cached_=out;return out;
}
} // namespace fvr::interaction
