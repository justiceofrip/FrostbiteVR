#include "Bc2PhysicalReload.h"
#include <cstring>
#include <limits>
namespace fvr::bc2 {
namespace {
using namespace interaction;
using namespace reload_insertion_detail;
ManualReloadConfig Plan()noexcept {ManualReloadConfig c;c.steps[0]={ReloadOperation::InsertRound,1};c.stepCount=1;
    c.maxSampleGapNs=150000000;c.ackTimeoutNs=1500000000;c.transactionTimeoutNs=5000000000;return c;}
bool SameInput(const HandInteractionSample& a,const HandInteractionSample& b)noexcept {
    return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
        a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
}
bool Fresh(std::int64_t observed,std::int64_t deadline,std::int64_t now,std::int64_t maximum=200000000)noexcept {
    return observed>0&&observed<=now&&deadline>now&&deadline-observed<=maximum;
}
bool Outside(const ReloadRawContact& raw)noexcept {
    if(!raw.valid||!Rigid(raw.rawLeftWristWorldMeters)||!Rigid(raw.weaponWorldMeters))return false;
    const auto p=SpasReloadInsertionProfile();
    const auto wrist=Multiply(raw.rawLeftWristWorldMeters,*InverseRigid(raw.weaponWorldMeters));
    const auto item=Multiply(*InverseRigid(p.itemFromHand),wrist);
    const auto rail=Multiply(Multiply(p.itemFromInsertion,item),*InverseRigid(p.weaponFromEntry));
    const auto& v=rail.values[3];return OutsideEntryApproach(rail,p)||
        std::hypot(v[0],v[1],v[2])>p.travelMeters+p.releaseDistanceMeters;
}
}
std::optional<math::Matrix4> PhysicalReloadPouchPose(const interaction::InputFrame& input)noexcept {
    if(!interaction::ValidInput(input)||!input.focused||!input.headValid||!input.hands[0].gripTracked)return {};
    const auto body=interaction::UprightReference(input.head);if(!body)return {};
    const auto hand=math::MakeRelativePose(*body,input.hands[0].grip);if(!hand)return {};
    const auto view=math::MakeLhViewFromOpenXRPose(*hand);if(!view)return {};
    return interaction::InverseRigid(*view);
}
interaction::AmmoSupplyConfig Bc2PhysicalReload::DefaultPouch()noexcept {
    return {interaction::InteractionHand::Left,0x424332414d4d4full,{0x424332504f5543ull,1},{-.23f,-.55f,.02f},.18f,200000000};
}
Bc2PhysicalReload::Bc2PhysicalReload(bool enabled,PhysicalReloadApi api,interaction::AmmoSupplyConfig pouch)noexcept
    :enabled_(enabled),pouch_(pouch),api_(api),supply_(pouch),manual_(Plan()){}
std::int64_t Bc2PhysicalReload::Now(std::int64_t fallback)const noexcept{return api_.clock?api_.clock(api_.context):fallback;}
ReloadKeepAliveResult Bc2PhysicalReload::Keep(const ReloadCycleControl& c,std::int64_t now)noexcept {
 const auto result=api_.keepObserved?api_.keepObserved(api_.context,c):
  api_.keep(api_.context,c)?ReloadKeepAliveResult::Accepted:ReloadKeepAliveResult::Rejected;
 if(result==ReloadKeepAliveResult::Accepted)acceptedControlDeadline_=c.deadlineNs;
 if(result==ReloadKeepAliveResult::Deferred&&!ReloadKeepAliveDeferredWithinOriginalDeadline(result,now,acceptedControlDeadline_))return ReloadKeepAliveResult::Rejected;
 return result;
}
bool Bc2PhysicalReload::Api()const noexcept {return api_.reserve&&api_.identity&&api_.start&&api_.keep&&api_.lease&&api_.submit&&api_.ack&&api_.cancel&&api_.retire;}
void Bc2PhysicalReload::Journal(unsigned event,std::int64_t now,unsigned reason,std::uint64_t seat,std::uint64_t sourceSequence)noexcept {
    if(rowCount_==rows_.size()){++dropped_;return;}
    const auto* held=supply_.Held()?&*supply_.Held():nullptr;
    const auto* pending=supply_.Pending()?&*supply_.Pending():nullptr;
    rows_[rowCount_++]={event,reason,now,cycle_,held?held->item.generation:pending?pending->item.generation:0,
        pending?pending->request:0,held?held->item.id:pending?pending->item.id:0,held?held->claim.token.id:pending?pending->claim.id:0,seat,sourceSequence,event==2?startOrigin_:ReloadStartOrigin{}};
}
void Bc2PhysicalReload::Cancel(const interaction::HandInteractionSample& safety,interaction::HandInteraction& hands,
    PhysicalReloadCancelReason reason,unsigned sourceFlags)noexcept {
    if(!enabled_)return;
    if(active_){ObserveCancellation(safety,reason,sourceFlags);api_.cancel(api_.context);active_=false;retiring_=true;retirementDrained_=false;blocksCurrent_=true;++cancelled_;Journal(5,safety.nowNs,unsigned(reason));}
    bridge_.Cancel();supply_.Cancel(safety,hands);manual_.Reset();insertion_.Update({});
    pulseUntil_=0;lease_.reset();acknowledgement_.reset();completion_.reset();pendingTargets_.reset();deferredSeat_.reset();guidedPreview_.reset();
}
void Bc2PhysicalReload::Feedback(PhysicalReloadResult& out,interaction::FeedbackKind kind,
    const interaction::HandInteractionSample& input)noexcept {
    if(out.feedbackCount==out.feedback.size()||nextFeedback_==std::numeric_limits<std::uint64_t>::max()||
       !input.focused||!input.tracked[0]||!input.tracked[1]||input.observedNs<=0||
       input.observedNs>std::numeric_limits<std::int64_t>::max()-100000000)return;
    interaction::FeedbackEvent event{nextFeedback_+1,input.sequence,input.owner.space,input.observedNs,
        std::min(input.deadlineNs,input.observedNs+100000000),kind,0};
    if(!interaction::ValidFeedback(event,Now(input.nowNs)))return;
    ++nextFeedback_;out.feedback[out.feedbackCount++]=event;
    if(feedbackEventCount_<feedbackEvents_.size())feedbackEvents_[feedbackEventCount_++]=event;else ++feedbackEventDropped_;
}
const Bc2PhysicalReload::Evidence* Bc2PhysicalReload::Original(const PhysicalReloadSample& s)const noexcept {
    if(!s.raw.valid||s.raw.owner!=s.nativeOwner||s.raw.rigFingerprint!=SpasReloadRig||!s.originalHandEvidence||
        !SameInput(s.raw.inputEvidence,*s.originalHandEvidence)||s.raw.inputEvidence.deadlineNs<=s.input.nowNs)return nullptr;
    for(const auto& e:history_)if(e&&SameInput(e->supply.input,s.raw.inputEvidence)&&
        e->supply.source.identity.owner==s.input.owner&&e->supply.source.identity.weapon==s.weapon&&
        e->supply.trackingEpoch==s.trackingEpoch)return &*e;
    return nullptr;
}
Bc2ReloadInteractionSample Bc2PhysicalReload::Interaction(const PhysicalReloadSample& s,const Evidence* original,
    const ReloadRoundLease& lease,std::int64_t now)const noexcept {
    Bc2ReloadInteractionSample out;auto& i=out.insertion;
    const auto& in=original?original->supply.input:s.input;
    i.identity={s.input.owner,s.weapon,original?original->item.token.item:HandInteractionKey{},s.trackingEpoch};
    const auto p=SpasReloadInsertionProfile();i.profile={p.id,p.revision};
    if(original){i.itemClaim=original->item;i.weaponClaim=original->gun;}
    i.sequence=in.sequence;i.geometrySequence=original?in.sequence:0;i.observedNs=in.observedNs;i.deadlineNs=in.deadlineNs;i.nowNs=now;
    i.focused=s.input.focused;i.itemTracked=s.input.tracked[0];i.weaponTracked=s.input.tracked[1];
    i.held=original&&supply_.Held()&&supply_.Held()->claim.token==original->item.token;
    i.eligible=i.held&&bool(s.meshes)&&bool(FindSelectedMesh(*s.meshes,s.nativeOwner,SelectedMeshKind::Spas12,now));
    out.assetName=s.asset;out.meshPath=SpasReloadMesh;out.rigFingerprint=s.raw.rigFingerprint;out.selectedMeshIdentityVerified=i.eligible;
    out.rawLeftWristWorldMeters=s.raw.rawLeftWristWorldMeters;out.weaponWorldMeters=s.raw.weaponWorldMeters;
    if(const auto owners=BindBc2ReloadOwners(s.input.owner,s.weapon,lease,now))out.native=*ToBc2ReloadInteractionLease(*owners,lease,now);
    return out;
}
void Bc2PhysicalReload::Retire(const PhysicalReloadSample& s,interaction::HandInteraction& hands,
    const std::optional<interaction::AmmoSupplySource>& source)noexcept {
    if(!retiring_)return;
    if(retirement_&&!Fresh(retirement_->observedNs,retirement_->deadlineNs,s.input.nowNs))retirement_.reset();
    if(!retirement_)retirement_=api_.retire(api_.context,native_,cycle_);
    if(!retirement_||!retirement_->verified||retirement_->identity!=native_||retirement_->cycle!=cycle_||
       !retirement_->event||retirement_->observedNs<started_||
       !Fresh(retirement_->observedNs,retirement_->deadlineNs,Now(s.input.nowNs))){retirement_.reset();return;}
    retirementDrained_=true;
    if(!supply_.Pending()){retiring_=false;retirement_.reset();blocksCurrent_=false;return;}
    if(!source||source->observedNs<=retirement_->observedNs)return;
    auto safety=s.input;safety.nowNs=Now(safety.nowNs);
    const AmmoSupplyRebaseline evidence{*supply_.Pending(),*source,retirement_->event,retirement_->observedNs,retirement_->deadlineNs,true};
    if(supply_.Rebaseline(safety,hands,evidence).accepted){++reconciled_;Journal(6,safety.nowNs);retiring_=false;retirement_.reset();}
    else if(retirement_->deadlineNs<=safety.nowNs)retirement_.reset();
}
void Bc2PhysicalReload::RefreshRetirementBlock(const PhysicalReloadSample& s,bool safe)noexcept {
    blocksCurrent_=true;
    if(!retiring_&&!supply_.Pending()){blocksCurrent_=false;return;}
    if(!retirementDrained_||!safe||!s.meshes||s.meshes->owner!=s.nativeOwner||
       !s.meshes->sequence||
       !Fresh(s.meshes->observedNs,s.meshes->deadlineNs,s.input.nowNs,250000000))return;
    const bool directItem=s.weapon.id==s.nativeOwner.weapon;
    const bool mappedReplacement=!directItem&&s.replacementFamily&&
        MagazineFamilyFresh(*s.replacementFamily,s.nativeOwner,s.input.owner,s.weapon,s.input.nowNs)&&
        MagazineSelected(*s.meshes,s.nativeOwner,s.asset,*s.replacementFamily->binding.profile,s.input.nowNs);
    if(!directItem&&!mappedReplacement)return;
    const auto& old=native_.owner;const auto& now=s.nativeOwner;
    // A recenter or input rearm alone is not an equipment replacement. Both
    // independently captured native and physical identity domains must change.
    const bool differentNative=old.player!=now.player||old.soldier!=now.soldier||old.weak!=now.weak||
        old.weapon!=now.weapon||old.actorGeneration!=now.actorGeneration||old.equipGeneration!=now.equipGeneration;
    const bool differentPhysical=physical_.actor!=s.input.owner.actor||physical_.actorGeneration!=s.input.owner.actorGeneration||
        physical_.equipGeneration!=s.input.owner.equipGeneration;
    if(differentNative&&differentPhysical)blocksCurrent_=false;
    // This only removes the OLD cycle's action block. No shell is acquired,
    // pending reserve spent/cleared, native receipt renewed or pose authorized.
}

PhysicalReloadResult Bc2PhysicalReload::Tick(const PhysicalReloadSample& supplied,interaction::HandInteraction& hands,
    std::uint64_t& intent)noexcept {
    PhysicalReloadResult out;if(!enabled_)return out;
    if(retiring_||supply_.Pending())blocksCurrent_=true; // Every exemption needs fresh current safety.
    auto s=supplied;s.input.nowNs=Now(s.input.nowNs);
    std::optional<PhysicalReloadCancelReason> diagnosticCancel;
    std::optional<interaction::AmmoSupplyReason> diagnosticSupply;
    bool diagnosticSafe=false,diagnosticSelected=false,diagnosticSource=false;
    const auto record=[&]{ObserveAvailability(s,hands,diagnosticSafe,diagnosticSelected,diagnosticSource,diagnosticCancel,diagnosticSupply);};
    struct RecordExit {const decltype(record)& call;~RecordExit(){call();}} recordExit{record};
    const auto cancel=[&](PhysicalReloadCancelReason reason){diagnosticCancel=reason;Cancel(s.input,hands,reason,s.cancelFlags);};
    using CancelReason=PhysicalReloadCancelReason;
    if(!Api()||s.input.nowNs<lastNow_){cancel(CancelReason::ApiOrClock);return out;}lastNow_=s.input.nowNs;
    out.tracking={true,s.nativeOwner,s.input,{}};
    const auto gun=hands.Current(InteractionHand::Right);
    const bool gunSafe=ReloadTrackingFresh(out.tracking,s.input.nowNs)&&gun&&gun->token.kind==HandClaimKind::GunHold&&
        gun->token.owner==s.input.owner&&gun->token.item==s.weapon&&gun->inputSequence==s.input.sequence&&gun->deadlineNs>s.input.nowNs&&
        s.weapon.generation==s.input.owner.equipGeneration&&s.trackingEpoch==s.nativeOwner.space;
    // SPAS acquisition/hold retains its exact direct native item requirement.
    const bool safe=gunSafe&&s.weapon.id==s.nativeOwner.weapon;
    diagnosticSafe=safe;
    const bool cancelInput=!safe||s.cancel||s.asset!=SpasReloadAsset;
    if(cancelInput){cancel(!safe?CancelReason::UnsafeInput:s.cancel?CancelReason::RequestedInput:CancelReason::WrongAsset);out.tracking.preview.reset();}
    if(active_&&(native_.owner!=s.nativeOwner||physical_!=s.input.owner))cancel(CancelReason::OwnerChanged);
    const auto reserveObservation=api_.reserveObserved?api_.reserveObserved(api_.context):ReloadReserveObservation{ReloadObservationResult::Available,api_.reserve(api_.context)};
    const auto observedReserve=reserveObservation.lease;
    s.input.nowNs=Now(s.input.nowNs);
    if(reserveObservation.result==ReloadObservationResult::Deferred){
        if(active_&&ReloadKeepAliveDeferredWithinOriginalDeadline(ReloadKeepAliveResult::Deferred,s.input.nowNs,acceptedControlDeadline_)){
            out.ammoOwnsHand=bool(supply_.Held());return out;}
        if(active_)cancel(CancelReason::MissingSource);return out;
    }
    if(reserveObservation.result==ReloadObservationResult::CohortGap){
        // A typed coherent owner/config proof may overlap a callback before
        // the first genuine hold. Keep only the exact already-owned shell.
        if(active_&&!retiring_&&!lease_&&supply_.Held()&&!supply_.Pending()&&
           !deferredSeat_&&!completion_&&safe&&!s.cancel&&s.asset==SpasReloadAsset&&
           native_.owner==s.nativeOwner&&physical_==s.input.owner){
            if(s.input.nowNs-started_>3000000000ll){cancel(CancelReason::HoldNotEstablished);return out;}
            if(!startupGapDeadline_&&acceptedControlDeadline_>s.input.nowNs)
                startupGapDeadline_=s.input.nowNs+std::min<std::int64_t>({50000000,acceptedControlDeadline_-s.input.nowNs,3000000000ll-(s.input.nowNs-started_)});
            if(!startupGapDeadline_||s.input.nowNs>=startupGapDeadline_){++startupGapExpired_;cancel(CancelReason::MissingSource);return out;}
            Bc2ReloadInteractionSample profile;profile.insertion.identity={s.input.owner,s.weapon,{},s.trackingEpoch};
            profile.insertion.nowNs=s.input.nowNs;profile.assetName=s.asset;profile.meshPath=SpasReloadMesh;
            profile.rigFingerprint=s.raw.rigFingerprint;
            profile.selectedMeshIdentityVerified=s.meshes&&FindSelectedMesh(*s.meshes,s.nativeOwner,SelectedMeshKind::Spas12,s.input.nowNs)&&
                s.raw.valid&&s.raw.owner==s.nativeOwner&&s.raw.inputEvidence.owner==s.input.owner&&
                s.raw.inputEvidence.deadlineNs>s.input.nowNs;
            const auto source=reserve_&&reserve_->identity==native_?SpasAmmoSupplySource(profile,*reserve_):std::nullopt;
            if(!source||!s.gripPressed||s.input.released[0]){cancel(CancelReason::MissingSource);return out;}
            const auto keep=Keep({native_,cycle_,s.input.sequence,s.input.observedNs,s.input.deadlineNs,true},s.input.nowNs);
            if(keep==ReloadKeepAliveResult::Rejected){cancel(CancelReason::KeepAliveRejected);return out;}
            s.input.nowNs=Now(s.input.nowNs);
            if(s.input.nowNs-started_>3000000000ll){++startupGapExpired_;cancel(CancelReason::HoldNotEstablished);return out;}
            if(s.input.nowNs>=startupGapDeadline_||s.input.nowNs>=acceptedControlDeadline_||
               !Fresh(reserve_->observedNs,reserve_->deadlineNs,s.input.nowNs)){
                ++startupGapExpired_;cancel(CancelReason::MissingSource);return out;}
            AmmoSupplySample wait{s.input,*source,s.geometrySequence,s.trackingEpoch,s.bodyFromHand,s.gripPressed,intent};
            const auto retained=supply_.WaitHeld(wait,hands);
            if(!retained.held){cancel(CancelReason::MissingSource);return out;}
            ++startupGapWaits_;out.ammoOwnsHand=true;out.reloadHeld=s.input.nowNs<pulseUntil_;return out;
        }

        const bool pending=bridge_.Phase()==Bc2ReloadBridgePhase::Pending&&supply_.Pending().has_value();
        const bool held=lease_&&lease_->nativeBindingVerified&&lease_->allThreeHeld;
        const bool original=active_&&!retiring_&&safe&&!s.cancel&&s.asset==SpasReloadAsset&&
            native_.owner==s.nativeOwner&&physical_==s.input.owner&&s.meshes&&
            FindSelectedMesh(*s.meshes,s.nativeOwner,SelectedMeshKind::Spas12,s.input.nowNs)&&
            reserve_&&reserve_->verified&&reserve_->identity==native_&&
            Fresh(reserve_->observedNs,reserve_->deadlineNs,s.input.nowNs)&&
            lease_&&lease_->identity==native_&&lease_->cycle==cycle_&&
            Fresh(lease_->observedNs,lease_->deadlineNs,s.input.nowNs)&&
            ReloadKeepAliveDeferredWithinOriginalDeadline(ReloadKeepAliveResult::Deferred,s.input.nowNs,acceptedControlDeadline_);
        if(original&&(pending||held)){out.ammoOwnsHand=bool(supply_.Held());return out;}
        reserve_.reset();cancel(CancelReason::MissingSource);return out;
    }
    if(api_.reserveObserved&&reserveObservation.result==ReloadObservationResult::Rejected)reserve_.reset();
    if(observedReserve&&reserveObservation.result==ReloadObservationResult::Available){reserve_=observedReserve;startupGapDeadline_=0;}
    s.input.nowNs=Now(s.input.nowNs);
    if(!reserve_||reserve_->identity.owner!=s.nativeOwner||!Fresh(reserve_->observedNs,reserve_->deadlineNs,s.input.nowNs))reserve_.reset();
    const auto selected=s.meshes?FindSelectedMesh(*s.meshes,s.nativeOwner,SelectedMeshKind::Spas12,s.input.nowNs):nullptr;
    diagnosticSelected=selected!=nullptr;
    Bc2ReloadInteractionSample profile;profile.insertion.identity={s.input.owner,s.weapon,{},s.trackingEpoch};profile.insertion.nowNs=s.input.nowNs;
    profile.assetName=s.asset;profile.meshPath=SpasReloadMesh;profile.rigFingerprint=s.raw.rigFingerprint;
    profile.selectedMeshIdentityVerified=selected&&s.raw.valid&&s.raw.owner==s.nativeOwner&&
        s.raw.inputEvidence.deadlineNs>s.input.nowNs&&s.raw.inputEvidence.owner==s.input.owner;
    const auto source=reserve_?SpasAmmoSupplySource(profile,*reserve_):std::nullopt;
    diagnosticSource=source.has_value();
    Retire(s,hands,source);s.input.nowNs=Now(s.input.nowNs);
    RefreshRetirementBlock(s,gunSafe&&gun&&gun->deadlineNs>s.input.nowNs&&ReloadTrackingFresh(out.tracking,s.input.nowNs));
    if(cancelInput)return out; // Drain/reconciliation may progress, but never acquisition or submission.
    const auto publishBelt=[&]{
        if(!beltEnabled_||retiring_||supply_.Held()||supply_.Pending()||deferredSeat_||completion_||out.tracking.preview||!source||!reserve_||!gun)return;
        BeltAmmoTracking t;t.owner=s.nativeOwner;t.reserve=*reserve_;t.meshes=s.meshes;
        if(active_){
            if(!lease_||lease_->identity!=native_||lease_->cycle!=cycle_||!lease_->allThreeHeld||
               !ReloadShellControlFresh(out.tracking,s.input.nowNs))return;
            t.heldCycle=*lease_;
        }
        auto& v=t.visual;v.enabled=true;v.source=*source;v.input=s.input;v.gun=*gun;
        v.contact=pouch_.alternateContact.value_or(AmmoSupplyContact{pouch_.pouchCenterMeters,pouch_.pouchRadiusMeters});
        v.frame=beltFrame_;
        if(SpasBeltAmmoFresh(t,Now(s.input.nowNs)))try{out.tracking.belt=std::make_shared<const BeltAmmoTracking>(std::move(t));}catch(...){}
    };
    if(!source||((reserve_->loaded>=reserve_->capacity||reserve_->reserve<=0)&&!supply_.Pending())||retiring_){
        cancel(!source?CancelReason::MissingSource:retiring_?CancelReason::Retiring:reserve_->loaded>=reserve_->capacity?CancelReason::FullMagazine:CancelReason::EmptyReserve);publishBelt();return out;
    }
    if(intent==std::numeric_limits<std::uint64_t>::max()){cancel(CancelReason::IntentExhausted);return out;}
    AmmoSupplySample current{s.input,*source,s.geometrySequence,s.trackingEpoch,s.bodyFromHand,s.gripPressed,++intent};
    // A pending completion is reconciled before the newly reduced count can
    // invalidate its original item or spend credit. Evidence is never inferred.
    if(active_){
        const auto keep=Keep({native_,cycle_,s.input.sequence,s.input.observedNs,s.input.deadlineNs,true},s.input.nowNs);
        if(keep==ReloadKeepAliveResult::Deferred){out.ammoOwnsHand=bool(supply_.Held());return out;}
        if(keep==ReloadKeepAliveResult::Rejected){
            cancel(CancelReason::KeepAliveRejected);return out;
        }
        if(auto nativeLease=api_.lease(api_.context,native_,cycle_))lease_=nativeLease;
        if(auto ack=api_.ack(api_.context,native_,cycle_))acknowledgement_=ack;
        s.input.nowNs=Now(s.input.nowNs);current.input.nowNs=s.input.nowNs;
        if(lease_&&!Fresh(lease_->observedNs,lease_->deadlineNs,s.input.nowNs))lease_.reset();
    }
    const auto oldOriginal=Original(s);
    if(bridge_.Phase()==Bc2ReloadBridgePhase::Pending&&lease_){
        auto pending=Interaction(s,oldOriginal,*lease_,s.input.nowNs);
        // Pending safety uses current genuine ownership; geometry remains the
        // original renderer packet only when still available. Missing geometry
        // drops guided presentation, not the native transaction.
        auto& i=pending.insertion;i.sequence=s.input.sequence;i.observedNs=s.input.observedNs;i.deadlineNs=s.input.deadlineNs;
        i.weaponClaim=*gun;i.focused=s.input.focused;i.weaponTracked=s.input.tracked[1];
        if(!oldOriginal||oldOriginal->supply.input.sequence!=s.input.sequence){
            i.geometrySequence=0;pending.rawLeftWristWorldMeters={};pending.weaponWorldMeters={};
        }
        pending.selectedMeshIdentityVerified=selected!=nullptr;
        if(!supply_.Held()){i.held=false;i.itemTracked=false;}
        const auto result=bridge_.Update(pending,*lease_,acknowledgement_);
        if(result.consumed)completion_=result;
        if(result.phase==Bc2ReloadBridgePhase::Cancelled){cancel(CancelReason::BridgeCancelled);return out;}
    }
    ManualReloadAck physicalAck{};
    if(completion_&&acknowledgement_&&lease_&&reserve_&&supply_.Pending()){
        if(const auto receipt=SpasAmmoSupplyReceipt(*supply_.Pending(),bridge_.Owners(),*completion_,*acknowledgement_,*lease_,*reserve_,s.input.nowNs)){
            if(supply_.Resolve(s.input,hands,*receipt).accepted){physicalAck=*completion_->acknowledged;++completed_;Journal(4,s.input.nowNs);
                Feedback(out,interaction::FeedbackKind::ReloadApplied,s.input);
                // Receipt retains the ORIGINAL reservation after Resolve clears it.
                bool recorded=false;
                for(unsigned n=0;n<transactionCount_;++n){auto& t=transactions_[n];
                    if(t.reservation==receipt->reservation&&!t.resolved){t.after=*lease_;t.ack=*acknowledgement_;t.reserve=*reserve_;
                        t.resolvedNs=s.input.nowNs;t.resolved=true;recorded=true;break;}}
                if(!recorded)++transactionDropped_;
                completion_.reset();acknowledgement_.reset();pendingTargets_.reset();}
        }
    }
    if((reserve_->loaded>=reserve_->capacity||reserve_->reserve<=0)&&!supply_.Pending()){cancel(reserve_->loaded>=reserve_->capacity?CancelReason::FullMagazine:CancelReason::EmptyReserve);return out;}
    const auto held=supply_.Update(current,hands);diagnosticSupply=held.reason;
    if(held.acquired){++acquired_;Journal(1,s.input.nowNs);}
    if(supply_.Held()){
        bool remembered=false;for(const auto& e:history_)if(e&&SameInput(e->supply.input,current.input)){remembered=true;break;}
        if(!remembered){history_[historyNext_]=Evidence{current,supply_.Held()->claim,*gun};historyNext_=(historyNext_+1)%history_.size();}
    }
    // A real shell may be carried while the gun finishes pumping. Preserve its
    // current claim/presentation, but do not spend the one Reload edge until a
    // NEW full native read is ready. Cached reserve credit is insufficient.
    if(supply_.Held()&&!active_&&observedReserve&&reserve_->reloadInputReady){
        const auto identity=api_.identity(api_.context);
        if(!identity||*identity!=reserve_->identity||nextCycle_==std::numeric_limits<std::uint64_t>::max()){
            cancel(CancelReason::StartIdentityRejected);return out;
        }
        native_=*identity;physical_=s.input.owner;cycle_=++nextCycle_;
        startOrigin_={s.actionFlagsKnown,s.gripPressed,false,s.input.sequence,s.actionHeld,s.actionPressed};
        if(!api_.start(api_.context,{native_,cycle_,s.input.sequence,s.input.observedNs,s.input.deadlineNs,true})){
            cancel(CancelReason::StartRejected);return out;
        }
        acceptedControlDeadline_=s.input.deadlineNs;startupGapDeadline_=0;active_=true;retirementDrained_=false;blocksCurrent_=true;started_=s.input.nowNs;pulseUntil_=s.input.nowNs+100000000;lastRaw_=0;retirement_.reset();
        ++startedCount_;Journal(2,s.input.nowNs,0,0,reserve_->sequence);
    }
    if(active_&&!lease_&&s.input.nowNs-started_>3000000000ll){cancel(CancelReason::HoldNotEstablished);return out;}
    const auto original=Original(s);
    Bc2ReloadInteractionResult geometric;std::optional<Bc2ReloadInteractionSample> paired;
    bool geometrySampled=false;
    if(active_&&lease_&&lease_->allThreeHeld&&original&&bridge_.Phase()!=Bc2ReloadBridgePhase::Pending&&
        original->supply.input.sequence>=lastRaw_){
        paired=Interaction(s,original,*lease_,s.input.nowNs);
        geometric=insertion_.Update(*paired);lastRaw_=original->supply.input.sequence;geometrySampled=true;
        if(geometric.insertion.captured)Feedback(out,interaction::FeedbackKind::ReloadCapture,original->supply.input);
        ObserveGeometry(*paired,geometric,s.input.sequence);
        const auto failure=geometric.targets?0u:unsigned(geometric.reason)*100+unsigned(geometric.insertion.reason);
        if(failure&&failure!=lastGeometryFailure_)Journal(9,s.input.nowNs,failure);lastGeometryFailure_=failure;
        if(geometric.insertion.captured||geometric.insertion.seat)Journal(10,s.input.nowNs,geometric.insertion.seat?2:1,
            geometric.insertion.seat?geometric.insertion.seat->id:0,geometric.insertion.seat?geometric.insertion.seat->inputSequence:0);
    }
    if(geometric.insertion.seat&&geometric.targets&&paired&&original&&!deferredSeat_){
        deferredSeat_=DeferredSeat{*paired,*geometric.insertion.seat,*geometric.targets,*original};
        // The checked profile is immutable; do not retain a caller-owned view.
        deferredSeat_->contact.assetName=SpasReloadAsset;deferredSeat_->contact.meshPath=SpasReloadMesh;
    }
    if(deferredSeat_){const auto& d=*deferredSeat_;const auto& old=d.contact.insertion;
        if(!active_||!s.gripPressed||!supply_.Held()||supply_.Held()->claim.token!=d.seat.itemClaim||gun->token!=d.seat.weaponClaim||
           old.identity.owner!=s.input.owner||old.identity.weapon!=s.weapon||old.identity.trackingEpoch!=s.trackingEpoch||
           d.targets.nativeCycle!=cycle_||old.deadlineNs<=s.input.nowNs||d.targets.deadlineNs<=s.input.nowNs||
           old.itemClaim.deadlineNs<=s.input.nowNs||old.weaponClaim.deadlineNs<=s.input.nowNs||
           d.original.supply.input.deadlineNs<=s.input.nowNs){deferredSeat_.reset();}
    }
    const auto& owner=s.input.owner;
    ManualReloadSample manual;manual.owner={owner.actor,owner.actorGeneration,s.weapon.id,owner.equipGeneration,owner.space};
    manual.sequence=s.input.sequence;manual.nowNs=s.input.nowNs;manual.focused=s.input.focused;manual.tracked=s.input.tracked[0]&&s.input.tracked[1];
    manual.bindingsVerified=source.has_value()&&reserve_->verified&&selected&&profile.selectedMeshIdentityVerified&&
        profile.rigFingerprint==SpasReloadRig&&s.asset==SpasReloadAsset;
    manual.neutral=!s.gripPressed||(original&&Outside(s.raw));manual.acknowledgement=physicalAck;
    if(deferredSeat_){manual.neutral=false;
        if(lease_&&lease_->allThreeHeld)manual.gesture={deferredSeat_->seat.id,ReloadOperation::InsertRound};}
    const auto command=manual_.Update(manual);
    if(command.cancelled){Journal(7,s.input.nowNs,unsigned(command.reason));cancel(CancelReason::ManualPolicyCancelled);return out;}
    if(command.request&&deferredSeat_&&lease_){
        const auto retained=*deferredSeat_;auto contact=retained.contact;contact.insertion.nowNs=s.input.nowNs;
        // Current genuine native authorization is separate from the immutable
        // original hand/geometry packet, claim deadlines and authored targets.
        const auto owners=BindBc2ReloadOwners(s.input.owner,s.weapon,*lease_,s.input.nowNs);
        if(!owners){cancel(CancelReason::NativeOwnersRejected);return out;}
        const auto native=ToBc2ReloadInteractionLease(*owners,*lease_,s.input.nowNs);
        if(!native){cancel(CancelReason::NativeLeaseRejected);return out;}contact.native=*native;
        const auto prepared=bridge_.Begin(contact,*lease_,retained.seat,*command.request,retained.targets);
        const auto reservation=prepared.submit?supply_.ReserveFrom(current,hands,retained.original.supply,retained.seat,*command.request,cycle_):std::nullopt;
        if(!prepared.submit||!reservation||!SameAmmoReservation(*reservation,prepared.submit->reservation)||!api_.submit(api_.context,*prepared.submit)){
            Journal(8,s.input.nowNs,prepared.submit?100:unsigned(prepared.reason));
            cancel(CancelReason::SubmissionRejected);return out;
        }
        // Seating transfers presentation to the gun immediately. Native ammo
        // stays pending until its exact completion receipt; the free hand can
        // already return to the fore-end during the remaining stock animation.
        supply_.ReleaseSubmitted(s.input,hands,*reservation);
        ++submitted_;Journal(3,s.input.nowNs);
        if(transactionCount_<transactions_.size()){
            auto& t=transactions_[transactionCount_++];t.reservation=*reservation;t.before=prepared.submit->heldLease;
            t.original=retained.original.supply.input;t.current=s.input;t.submittedNs=s.input.nowNs;
        }else ++transactionDropped_;
        pendingTargets_=prepared.presentation;lease_.reset();deferredSeat_.reset();
    }
    out.reloadHeld=active_&&s.input.nowNs<pulseUntil_;
    out.ammoOwnsHand=bool(supply_.Held());out.tracking.inputEvidence=s.input;
    // A fresh Free/rejected/withdrawn result always wins. Only absence of a
    // new coherent contact permits reuse, and only under the old exact leases.
    if(geometrySampled||!supply_.Held()||bridge_.Phase()==Bc2ReloadBridgePhase::Pending)guidedPreview_.reset();
    if(guidedPreview_&&(!active_||retiring_||!selected||!lease_||!lease_->allThreeHeld||
       lease_->identity!=guidedPreview_->native.identity||lease_->cycle!=guidedPreview_->native.cycle||
       lease_->sequence<guidedPreview_->native.sequence||lease_->observedNs<guidedPreview_->native.observedNs||
       supply_.Held()->claim.token!=guidedPreview_->shellClaim.token||gun->token!=guidedPreview_->weaponClaim.token||
       !Fresh(lease_->observedNs,lease_->deadlineNs,s.input.nowNs)||
       !ReloadPreviewFresh(*guidedPreview_,out.tracking,s.input.nowNs)))guidedPreview_.reset();
    if(guidedPreview_){const auto& old=*guidedPreview_->selectedMeshes;const auto& currentMeshes=*s.meshes;
        if(old.owner!=currentMeshes.owner||old.sequence>currentMeshes.sequence||old.weaponData!=currentMeshes.weaponData||
           old.soleConfiguredArray!=currentMeshes.soleConfiguredArray||old.states!=currentMeshes.states)guidedPreview_.reset();}
    if(supply_.Held()&&selected){
        ReloadPreview preview;preview.selectedMeshes=s.meshes;preview.reserve=*reserve_;
        preview.shellClaim=supply_.Held()->claim;preview.weaponClaim=*gun;
        if(bridge_.Phase()==Bc2ReloadBridgePhase::Pending){
            // Original seated geometry may survive only its original deadline;
            // an advancing/missing native hold cannot authorize a pinned shell.
            if(pendingTargets_&&lease_&&lease_->allThreeHeld){preview.phase=ReloadPreviewPhase::Pending;preview.native=*lease_;preview.targets=*pendingTargets_;
                if(ReloadPreviewFresh(preview,out.tracking,s.input.nowNs))try{out.tracking.preview=std::make_shared<const ReloadPreview>(preview);}catch(...){}
            }
        }else{
            // A valid raw target is not a captured rail. Free carry must use
            // the renderer's current raw wrist, not the older N-1 contact that
            // the recognizer safely retains for physical insertion evidence.
            if(geometric.targets&&geometric.insertion.phase!=ReloadInsertionPhase::Free&&lease_&&lease_->allThreeHeld){
                preview.phase=ReloadPreviewPhase::Guided;preview.native=*lease_;preview.targets=*geometric.targets;
            }else if(guidedPreview_)preview=*guidedPreview_;
            if(ReloadPreviewFresh(preview,out.tracking,s.input.nowNs)){
                if(preview.phase==ReloadPreviewPhase::Guided)guidedPreview_=preview;
                try{out.tracking.preview=std::make_shared<const ReloadPreview>(preview);}catch(...){}
            }
        }
    }
    // Start/keep accepted this exact cycle earlier in this same Tick. Visual
    // control survives pending native advance and a spent item, but never
    // cancellation/retirement, stale reserve, owner loss or a missing mesh.
    if(active_&&!retiring_&&selected&&reserve_&&reserve_->identity==native_){
        ReloadShellControl control{cycle_,*reserve_,*gun,s.meshes};
        try{out.tracking.shellControl=std::make_shared<const ReloadShellControl>(control);}catch(...){}
        if(!ReloadShellControlFresh(out.tracking,s.input.nowNs))out.tracking.shellControl.reset();
    }
    publishBelt();
    return out;
}
void Bc2PhysicalReload::Report(std::ostream& out)const {
    out<<"{\"enabled\":"<<(enabled_?"true":"false")<<",\"headset_verified\":false,\"native_acceptance_verified\":false,\"consumer_code_integrated\":true"
        <<",\"active\":"<<(active_?"true":"false")<<",\"retiring\":"<<(retiring_?"true":"false")
        <<",\"startup_gap_waits\":"<<startupGapWaits_<<",\"startup_gap_expired\":"<<startupGapExpired_
        <<",\"acquired\":"<<acquired_<<",\"cycles\":"<<startedCount_<<",\"submitted\":"<<submitted_
        <<",\"completed\":"<<completed_<<",\"cancelled\":"<<cancelled_<<",\"reconciled\":"<<reconciled_
        <<",\"dropped\":"<<dropped_<<",\"events\":[";
    for(unsigned n=0;n<rowCount_;++n){if(n)out<<',';const auto& r=rows_[n];out<<"{\"event\":"<<r.event<<",\"reason\":"<<r.reason
        <<",\"now_ns\":"<<r.now<<",\"cycle\":"<<r.cycle<<",\"item_generation\":"<<r.item<<",\"request\":"<<r.request
        <<",\"item_id\":"<<r.itemId<<",\"claim\":"<<r.claim<<",\"seat\":"<<r.seat<<",\"source_sequence\":"<<r.sourceSequence<<",\"start_flags_known\":"<<(r.startOrigin.flagsKnown?"true":"false")
        <<",\"start_grip_pressed\":"<<(r.startOrigin.gripPressed?"true":"false")<<",\"start_eject_pressed\":false"
        <<",\"start_input_sequence\":"<<r.startOrigin.inputSequence<<",\"start_held\":"<<r.startOrigin.held<<",\"start_pressed\":"<<r.startOrigin.pressed<<'}';}out<<"],\"transaction_dropped\":"<<transactionDropped_<<",\"transactions\":";ReportTransactions(out);
    out<<",\"feedback\":{\"total\":"<<nextFeedback_<<",\"dropped_log_rows\":"<<feedbackEventDropped_<<",\"events\":[";
    for(unsigned n=0;n<feedbackEventCount_;++n){const auto& e=feedbackEvents_[n];if(n)out<<',';
        out<<"{\"event\":"<<e.id<<",\"kind\":\""<<(e.kind==interaction::FeedbackKind::ReloadCapture?"capture":"native_receipt")
           <<"\",\"input_sequence\":"<<e.inputSequence<<",\"space\":"<<e.space<<",\"observed_ns\":"<<e.observedNs<<",\"deadline_ns\":"<<e.deadlineNs<<'}';}out<<"]}";
    out<<",\"geometry\":";ReportGeometry(out);out<<",\"cancellation_evidence\":";ReportCancellations(out);
    out<<",\"supply_availability\":";ReportAvailability(out);out<<'}';
}

void Bc2PhysicalReload::ObserveAvailability(const PhysicalReloadSample& s,const interaction::HandInteraction& hands,
    bool safe,bool selected,bool source,std::optional<PhysicalReloadCancelReason> cancelled,
    std::optional<interaction::AmmoSupplyReason> supplyReason)noexcept {
    ++availabilitySamples_;AvailabilityRow row;auto& v=row.state;
    v.native=s.nativeOwner;v.physical=s.input.owner;v.weapon=s.weapon;
    std::copy_n(s.asset.begin(),std::min(s.asset.size(),v.asset.size()-1),v.asset.begin());
    v.sourceFlags=s.cancelFlags;v.cancelReason=cancelled?unsigned(*cancelled):~0u;v.supplyReason=supplyReason?unsigned(*supplyReason):~0u;
    const auto flag=[&](unsigned bit,bool value){if(value)v.flags|=std::uint32_t(1)<<bit;};
    const auto& in=s.input;const auto gun=hands.Current(interaction::InteractionHand::Right);
    const bool reserve=reserve_&&reserve_->identity.owner==s.nativeOwner&&Fresh(reserve_->observedNs,reserve_->deadlineNs,in.nowNs);
    const bool body=interaction::reload_insertion_detail::Rigid(s.bodyFromHand);
    if(body){for(unsigned n=0;n<3;++n)row.bodyHand[n]=s.bodyFromHand.values[3][n];
        const auto distance=[&](const std::array<float,3>& center){return std::hypot(row.bodyHand[0]-center[0],row.bodyHand[1]-center[1],row.bodyHand[2]-center[2]);};
        row.primaryDistance=distance(pouch_.pouchCenterMeters);
        if(pouch_.alternateContact)row.alternateDistance=distance(pouch_.alternateContact->centerMeters);
    }
    flag(0,in.focused);flag(1,in.tracked[0]);flag(2,in.tracked[1]);flag(3,s.gripPressed);flag(4,in.released[0]);flag(5,s.cancel);
    flag(6,safe);flag(7,reserve);flag(8,selected);flag(9,s.raw.valid);flag(10,s.raw.owner==s.nativeOwner);
    flag(11,s.raw.inputEvidence.owner==in.owner&&s.raw.inputEvidence.deadlineNs>in.nowNs);
    flag(12,s.raw.rigFingerprint==SpasReloadRig);flag(13,body);
    flag(14,body&&row.primaryDistance<=pouch_.pouchRadiusMeters);
    flag(15,body&&pouch_.alternateContact&&row.alternateDistance<=pouch_.alternateContact->radiusMeters);
    flag(16,bool(gun));flag(17,gun&&gun->token.kind==interaction::HandClaimKind::GunHold&&gun->token.owner==in.owner&&gun->token.item==s.weapon&&gun->inputSequence==in.sequence&&gun->deadlineNs>in.nowNs);
    flag(18,Fresh(in.observedNs,in.deadlineNs,in.nowNs,150000000));
    flag(19,lease_&&lease_->allThreeHeld&&Fresh(lease_->observedNs,lease_->deadlineNs,in.nowNs));
    flag(20,retiring_);flag(21,retirementDrained_);flag(22,bool(supply_.Held()));flag(23,bool(supply_.Pending()));flag(24,BlocksEquipment());
    flag(25,reserve&&reserve_->allThreeIdle);flag(26,reserve&&reserve_->reloadInputReady);flag(27,source);flag(28,beltEnabled_);
    flag(29,pouch_.alternateContact.has_value());flag(30,s.cancel&&!s.cancelFlags);
    if(reserve){v.loaded=reserve_->loaded;v.reserve=reserve_->reserve;v.capacity=reserve_->capacity;row.reserveObserved=reserve_->observedNs;row.reserveDeadline=reserve_->deadlineNs;}
    if(availabilityLast_&&*availabilityLast_==v)return;
    availabilityLast_=v;++availabilityTransitions_;row.sequence=in.sequence;row.geometrySequence=s.geometrySequence;
    row.rawSequence=s.raw.inputEvidence.sequence;row.rawObserved=s.raw.inputEvidence.observedNs;row.rawDeadline=s.raw.inputEvidence.deadlineNs;
    row.meshSequence=s.meshes?s.meshes->sequence:0;row.now=in.nowNs;row.observed=in.observedNs;row.deadline=in.deadlineNs;
    availability_[availabilityNext_]=row;availabilityNext_=(availabilityNext_+1)%availability_.size();
    availabilityCount_=std::min(availabilityCount_+1,static_cast<unsigned>(availability_.size()));
}
void Bc2PhysicalReload::ReportAvailability(std::ostream& out)const {
    static constexpr const char* names[]={"focused","left_tracked","right_tracked","grip_pressed","left_released","requested_cancel",
        "safe_current_gun","reserve_current","selected_spas_mesh","raw_valid","raw_native_owner","raw_physical_lease","spas_rig","body_pose_valid",
        "inside_primary","inside_alternate","gun_claim_present","gun_claim_current","input_fresh","all_three_held","retiring","native_drained",
        "shell_held","shell_pending","blocks_current","all_three_idle","reload_input_ready","supply_source","belt_enabled","alternate_contact","unattributed_cancel"};
    out<<"{\"schema\":1,\"capacity\":"<<availability_.size()<<",\"samples\":"<<availabilitySamples_<<",\"transitions\":"<<availabilityTransitions_
       <<",\"overwritten\":"<<(availabilityTransitions_-availabilityCount_)<<",\"cancel_reason_table\":\"cancellation_evidence\",\"supply_reason_table\":\"AmmoSupplyReason\",\"rows\":[";
    const auto start=(availabilityNext_+availability_.size()-availabilityCount_)%availability_.size();
    for(unsigned n=0;n<availabilityCount_;++n){if(n)out<<',';const auto& r=availability_[(start+n)%availability_.size()];const auto& v=r.state;
        out<<"{\"asset\":\"";for(char c:v.asset){if(!c)break;if(c=='\"'||c=='\\')out<<'\\';if(static_cast<unsigned char>(c)<32)out<<'?';else out<<c;}
        out<<"\",\"native_owner\":["<<v.native.player<<','<<v.native.soldier<<','<<v.native.weak<<','<<v.native.weapon<<','<<v.native.actorGeneration<<','<<v.native.equipGeneration<<','<<v.native.space
           <<"],\"physical_owner\":["<<v.physical.actor<<','<<v.physical.actorGeneration<<','<<v.physical.equipGeneration<<','<<v.physical.space<<"],\"weapon\":["<<v.weapon.id<<','<<v.weapon.generation
           <<"],\"input_sequence\":"<<r.sequence<<",\"geometry_sequence\":"<<r.geometrySequence<<",\"raw_sequence\":"<<r.rawSequence<<",\"mesh_sequence\":"<<r.meshSequence
           <<",\"now_ns\":"<<r.now<<",\"input_observed_ns\":"<<r.observed<<",\"input_deadline_ns\":"<<r.deadline
           <<",\"reserve_observed_ns\":"<<r.reserveObserved<<",\"reserve_deadline_ns\":"<<r.reserveDeadline<<",\"raw_observed_ns\":"<<r.rawObserved<<",\"raw_deadline_ns\":"<<r.rawDeadline
           <<",\"source_flags\":"<<v.sourceFlags<<",\"cancel_reason\":";if(v.cancelReason==~0u)out<<"null";else out<<v.cancelReason;
        out<<",\"supply_reason\":";if(v.supplyReason==~0u)out<<"null";else out<<v.supplyReason;
        out<<",\"loaded\":"<<v.loaded<<",\"reserve\":"<<v.reserve<<",\"capacity\":"<<v.capacity<<",\"body_hand_m\":["<<r.bodyHand[0]<<','<<r.bodyHand[1]<<','<<r.bodyHand[2]
           <<"],\"primary_distance_m\":"<<r.primaryDistance<<",\"alternate_distance_m\":"<<r.alternateDistance;
        for(unsigned b=0;b<std::size(names);++b)out<<",\""<<names[b]<<"\":"<<((v.flags&(std::uint32_t(1)<<b))?"true":"false");out<<'}';
    }out<<"]}";
}

void Bc2PhysicalReload::ObserveCancellation(const HandInteractionSample& input,PhysicalReloadCancelReason reason,unsigned flags)noexcept {
    if(cancellationCount_==cancellations_.size()){++cancellationDropped_;return;}
    auto& r=cancellations_[cancellationCount_++];r.reason=reason;r.sourceFlags=flags;r.input=input;r.cycle=cycle_;
    r.held=bool(supply_.Held());r.pending=bool(supply_.Pending());
    if(supply_.Held())r.itemGeneration=supply_.Held()->item.generation;
    if(supply_.Pending())r.pendingRequest=supply_.Pending()->request;
    r.reservePresent=bool(reserve_);if(reserve_){r.loaded=reserve_->loaded;r.reserve=reserve_->reserve;r.capacity=reserve_->capacity;
        r.reserveObserved=reserve_->observedNs;r.reserveDeadline=reserve_->deadlineNs;}
    r.leasePresent=bool(lease_);if(lease_){r.allThreeHeld=lease_->allThreeHeld;r.leaseObserved=lease_->observedNs;r.leaseDeadline=lease_->deadlineNs;}
}
void Bc2PhysicalReload::ReportCancellations(std::ostream& out)const {
    static constexpr const char* reasons[]={"external","api_or_clock","unsafe_input","requested_input","wrong_asset","owner_changed",
        "missing_source","full_magazine","empty_reserve","retiring","intent_exhausted","keepalive_rejected","bridge_cancelled",
        "start_identity_rejected","start_rejected","hold_not_established","manual_policy_cancelled","native_owners_rejected",
        "native_lease_rejected","submission_rejected"};
    out<<"{\"schema\":1,\"capacity\":"<<cancellations_.size()<<",\"total\":"<<cancelled_<<",\"dropped\":"<<cancellationDropped_
       <<",\"source_flag_names\":{\"1\":\"body_draw\",\"2\":\"fixture\",\"4\":\"aim_invalid\",\"8\":\"inactive\",\"16\":\"fire\",\"32\":\"use\",\"64\":\"next_weapon\",\"128\":\"previous_weapon\",\"256\":\"missing_pouch\",\"512\":\"magazine_busy\"},\"rows\":[";
    for(unsigned n=0;n<cancellationCount_;++n){if(n)out<<',';const auto& r=cancellations_[n];const auto& i=r.input;
        out<<"{\"reason\":"<<unsigned(r.reason)<<",\"reason_name\":\""<<reasons[unsigned(r.reason)]<<"\",\"source_flags\":"<<r.sourceFlags
           <<",\"cycle\":"<<r.cycle<<",\"now_ns\":"<<i.nowNs<<",\"input_sequence\":"<<i.sequence
           <<",\"input_observed_ns\":"<<i.observedNs<<",\"input_deadline_ns\":"<<i.deadlineNs
           <<",\"focused\":"<<(i.focused?"true":"false")<<",\"tracked\":["<<(i.tracked[0]?"true":"false")<<','<<(i.tracked[1]?"true":"false")
           <<"],\"released\":["<<(i.released[0]?"true":"false")<<','<<(i.released[1]?"true":"false")
           <<"],\"held\":"<<(r.held?"true":"false")<<",\"item_generation\":"<<r.itemGeneration
           <<",\"pending\":"<<(r.pending?"true":"false")<<",\"pending_request\":"<<r.pendingRequest
           <<",\"reserve_present\":"<<(r.reservePresent?"true":"false")<<",\"loaded\":"<<r.loaded<<",\"reserve\":"<<r.reserve<<",\"capacity\":"<<r.capacity
           <<",\"reserve_observed_ns\":"<<r.reserveObserved<<",\"reserve_deadline_ns\":"<<r.reserveDeadline
           <<",\"lease_present\":"<<(r.leasePresent?"true":"false")<<",\"all_three_held\":"<<(r.allThreeHeld?"true":"false")
           <<",\"lease_observed_ns\":"<<r.leaseObserved<<",\"lease_deadline_ns\":"<<r.leaseDeadline<<'}';
    }out<<"]}";
}

void Bc2PhysicalReload::ObserveGeometry(const Bc2ReloadInteractionSample& s,const Bc2ReloadInteractionResult& result,std::uint64_t current)noexcept {
    const auto& i=s.insertion;const auto item=i.identity.item.generation;
    if(!i.sequence||(i.sequence==geometrySource_&&s.native.cycle==geometryCycle_&&item==geometryItem_))return;
    if(!Rigid(s.rawLeftWristWorldMeters)||!Rigid(s.weaponWorldMeters))return;
    const auto p=SpasReloadInsertionProfile();
    const auto hand=Multiply(s.rawLeftWristWorldMeters,*InverseRigid(s.weaponWorldMeters));
    const auto raw=Multiply(Multiply(p.itemFromInsertion,Multiply(*InverseRigid(p.itemFromHand),hand)),*InverseRigid(p.weaponFromEntry));
    if(!Rigid(raw))return;
    GeometryRow row;row.cycle=s.native.cycle;row.item=item;row.source=i.sequence;row.current=current;
    row.observed=i.observedNs;row.deadline=i.deadlineNs;row.now=i.nowNs;
    for(unsigned n=0;n<3;++n)row.rail[n]=raw.values[3][n];
    row.radial=float(TravelRadial(raw,p));row.distance=std::hypot(row.rail[0],row.rail[1],row.rail[2]);
    row.keyedAngle=AlignmentAngle(raw,ReloadInsertionOrientation::Keyed);row.axisAngle=AlignmentAngle(raw,ReloadInsertionOrientation::AxialSymmetry);
    if(geometrySource_&&s.native.cycle==geometryCycle_&&item==geometryItem_&&i.sequence>geometrySource_){
        row.priorSource=geometrySource_;row.deltaMeters=Distance(raw,geometryPrior_);row.deltaAngle=Angle(raw,geometryPrior_);
    }
    row.phase=unsigned(result.insertion.phase);row.adapterReason=unsigned(result.reason);row.insertionReason=unsigned(result.insertion.reason);
    row.progress=result.insertion.progress;row.alignment=result.insertion.alignment;row.captured=result.insertion.captured;row.seated=result.insertion.seat.has_value();
    row.withinCapture=CaptureGeometry(raw,p);
    ++geometryTotal_;if(result.insertion.phase==ReloadInsertionPhase::Free)++geometryFree_;else ++geometryGuided_;
    if(row.captured)++geometryCaptured_;if(row.seated)++geometrySeated_;if(result.insertion.reason==ReloadInsertionReason::PoseJump)++geometryPoseJumps_;
    GeometryNearest* nearest=nullptr;
    for(unsigned n=0;n<geometryNearestCount_;++n)if(geometryNearest_[n].cycle==row.cycle&&geometryNearest_[n].item==row.item){nearest=&geometryNearest_[n];break;}
    if(!nearest&&geometryNearestCount_<geometryNearest_.size()){nearest=&geometryNearest_[geometryNearestCount_++];nearest->cycle=row.cycle;nearest->item=row.item;}
    if(nearest){
        if(!nearest->samples||row.distance<nearest->closest.distance)nearest->closest=row;
        if(TravelCoordinate(raw,p)<=0&&(!nearest->frontValid||row.distance<nearest->front.distance)){nearest->front=row;nearest->frontValid=true;}
        ++nearest->samples;if(row.captured)++nearest->captures;if(row.seated)++nearest->seats;
        if(result.insertion.reason==ReloadInsertionReason::PoseJump)++nearest->jumps;
    }else ++geometryUnretainedSamples_;
    geometrySource_=i.sequence;geometryItem_=item;geometryCycle_=s.native.cycle;geometryPrior_=raw;
    // Ring retains the final motion even during a long run; totals remain exact.
    geometryRows_[geometryNext_]=row;geometryNext_=(geometryNext_+1)%unsigned(geometryRows_.size());
    geometryCount_=std::min(geometryCount_+1,unsigned(geometryRows_.size()));
}
void Bc2PhysicalReload::ReportGeometry(std::ostream& out)const {
    const auto p=SpasReloadInsertionProfile();
    out<<"{\"source\":\"original_raw_contact\",\"phase_is_actual_insertion\":true,\"authoritative\":false,\"profile_revision\":"<<p.revision
       <<",\"travel_direction_in_entry\":["<<p.travelDirection[0]<<','<<p.travelDirection[1]<<','<<p.travelDirection[2]<<"],\"total\":"<<geometryTotal_
       <<",\"free\":"<<geometryFree_<<",\"guided\":"<<geometryGuided_<<",\"captures\":"<<geometryCaptured_<<",\"seats\":"<<geometrySeated_
       <<",\"pose_jumps\":"<<geometryPoseJumps_<<",\"rows\":[";
    const auto first=(geometryNext_+unsigned(geometryRows_.size())-geometryCount_)%unsigned(geometryRows_.size());
    for(unsigned n=0;n<geometryCount_;++n){if(n)out<<',';const auto& r=geometryRows_[(first+n)%geometryRows_.size()];
        out<<"{\"cycle\":"<<r.cycle<<",\"item_generation\":"<<r.item<<",\"source_sequence\":"<<r.source<<",\"current_sequence\":"<<r.current
           <<",\"observed_ns\":"<<r.observed<<",\"deadline_ns\":"<<r.deadline<<",\"processing_ns\":"<<r.now<<",\"rail_tip_m\":["<<r.rail[0]<<','<<r.rail[1]<<','<<r.rail[2]<<']'
           <<",\"radial_m\":"<<r.radial<<",\"entry_distance_m\":"<<r.distance<<",\"keyed_angle_rad\":"<<r.keyedAngle<<",\"axis_angle_rad\":"<<r.axisAngle
           <<",\"previous_logged_sequence\":"<<r.priorSource<<",\"previous_logged_delta_m\":"<<r.deltaMeters<<",\"previous_logged_delta_rad\":"<<r.deltaAngle
           <<",\"phase\":"<<r.phase<<",\"adapter_reason\":"<<r.adapterReason<<",\"insertion_reason\":"<<r.insertionReason
           <<",\"progress\":"<<r.progress<<",\"alignment\":"<<r.alignment<<",\"captured\":"<<(r.captured?"true":"false")
           <<",\"seated\":"<<(r.seated?"true":"false")<<",\"inside_capture_geometry\":"<<(r.withinCapture?"true":"false")<<'}';
    }
    out<<"],\"closest_per_item\":[";
    const auto closest=[&](const GeometryRow& r){out<<"{\"source_sequence\":"<<r.source<<",\"observed_ns\":"<<r.observed<<",\"deadline_ns\":"<<r.deadline
        <<",\"tip_m\":["<<r.rail[0]<<','<<r.rail[1]<<','<<r.rail[2]<<"],\"entry_distance_m\":"<<r.distance<<",\"keyed_angle_rad\":"<<r.keyedAngle
        <<",\"axis_angle_rad\":"<<r.axisAngle<<",\"adapter_reason\":"<<r.adapterReason<<",\"insertion_reason\":"<<r.insertionReason
        <<",\"inside_capture_geometry\":"<<(r.withinCapture?"true":"false")<<",\"captured\":"<<(r.captured?"true":"false")<<'}';};
    for(unsigned n=0;n<geometryNearestCount_;++n){if(n)out<<',';const auto& v=geometryNearest_[n];
        out<<"{\"cycle\":"<<v.cycle<<",\"item_generation\":"<<v.item<<",\"samples\":"<<v.samples<<",\"captures\":"<<v.captures
           <<",\"seats\":"<<v.seats<<",\"pose_jumps\":"<<v.jumps<<",\"nearest\":";closest(v.closest);
        out<<",\"nearest_front\":";if(v.frontValid)closest(v.front);else out<<"null";out<<'}';}
    out<<"],\"closest_unretained_contact_samples\":"<<geometryUnretainedSamples_<<'}';
}

void Bc2PhysicalReload::ReportTransactions(std::ostream& out)const {
    const auto identity=[&](const ReloadHoldIdentity& id){const auto& o=id.owner;
        out<<"{\"player\":"<<o.player<<",\"soldier\":"<<o.soldier<<",\"weak\":"<<o.weak<<",\"weapon\":"<<o.weapon
            <<",\"actor_generation\":"<<o.actorGeneration<<",\"equip_generation\":"<<o.equipGeneration<<",\"space\":"<<o.space
            <<",\"firing\":["<<id.firing[0]<<','<<id.firing[1]<<','<<id.firing[2]<<"],\"server_player\":"<<id.serverPlayer
            <<",\"server_soldier\":"<<id.serverSoldier<<",\"server_item\":"<<id.serverItem<<'}';};
    const auto input=[&](const interaction::HandInteractionSample& i){out<<"{\"sequence\":"<<i.sequence<<",\"observed_ns\":"<<i.observedNs
        <<",\"deadline_ns\":"<<i.deadlineNs<<",\"actor\":"<<i.owner.actor<<",\"actor_generation\":"<<i.owner.actorGeneration
        <<",\"equip_generation\":"<<i.owner.equipGeneration<<",\"space\":"<<i.owner.space<<'}';};
    const auto lease=[&](const ReloadRoundLease& l){out<<"{\"identity\":";identity(l.identity);
        out<<",\"cycle\":"<<l.cycle<<",\"sequence\":"<<l.sequence<<",\"observed_ns\":"<<l.observedNs<<",\"deadline_ns\":"<<l.deadlineNs
            <<",\"loaded\":"<<l.loaded<<",\"reserve\":"<<l.reserve<<",\"capacity\":"<<l.capacity
            <<",\"verified\":"<<(l.nativeBindingVerified?"true":"false")<<",\"all_three_held\":"<<(l.allThreeHeld?"true":"false")<<'}';};
    out<<'[';for(unsigned n=0;n<transactionCount_;++n){if(n)out<<',';const auto& t=transactions_[n];const auto& r=t.reservation;
        out<<"{\"item_id\":"<<r.item.id<<",\"item_generation\":"<<r.item.generation<<",\"claim\":"<<r.claim.id
            <<",\"weapon\":"<<r.identity.weapon.id<<",\"physical_equip_generation\":"<<r.identity.weapon.generation
            <<",\"pool\":"<<r.identity.pool.id<<",\"pool_generation\":"<<r.identity.pool.generation
            <<",\"seat\":"<<r.seat<<",\"request\":"<<r.request<<",\"cycle\":"<<r.cycle
            <<",\"source_sequence\":"<<r.sourceSequence<<",\"started_ns\":"<<r.startedNs<<",\"units\":"<<r.units
            <<",\"reserve_before\":"<<r.reserveBefore<<",\"operation\":"<<unsigned(r.operation)
            <<",\"submitted_ns\":"<<t.submittedNs<<",\"resolved_ns\":"<<t.resolvedNs<<",\"resolved\":"<<(t.resolved?"true":"false")
            <<",\"original_input\":";input(t.original);out<<",\"current_input\":";input(t.current);out<<",\"before\":";lease(t.before);
        if(t.resolved){out<<",\"after\":";lease(t.after);out<<",\"ack\":{\"identity\":";identity(t.ack.acknowledgement.identity);
            const auto& a=t.ack.acknowledgement;out<<",\"cycle\":"<<a.cycle<<",\"request\":"<<a.semantic.request
                <<",\"sample_sequence\":"<<a.sampleSequence<<",\"server_invocation\":"<<a.serverInvocation
                <<",\"observed_ns\":"<<t.ack.observedNs<<",\"deadline_ns\":"<<t.ack.deadlineNs
                <<",\"operation\":"<<unsigned(a.semantic.operation)<<",\"status\":"<<unsigned(a.semantic.status)
                <<",\"verified\":"<<(t.ack.verified?"true":"false")<<"},\"reserve_after\":{\"identity\":";identity(t.reserve.identity);
            out<<",\"sequence\":"<<t.reserve.sequence<<",\"observed_ns\":"<<t.reserve.observedNs<<",\"deadline_ns\":"<<t.reserve.deadlineNs
                <<",\"loaded\":"<<t.reserve.loaded<<",\"reserve\":"<<t.reserve.reserve<<",\"capacity\":"<<t.reserve.capacity
                <<",\"verified\":"<<(t.reserve.verified?"true":"false")<<'}';}
        out<<'}';}out<<']';
}
} // namespace fvr::bc2
