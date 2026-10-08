#include "Bc2MagazineResourceReload.h"
namespace fvr::bc2 {
namespace {
using namespace interaction;using namespace reload_insertion_detail;
DetachableMagazineConfig Config(const MagazineEquipmentProfile& p){auto c=p.geometry->interaction;c.backend=MagazineControlBackend::AmmunitionResource;return c;}
bool SameInput(const HandInteractionSample& a,const HandInteractionSample& b){return a.owner==b.owner&&a.sequence==b.sequence&&
    a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;}
bool CanSupply(DetachableMagazinePhase p){return p==DetachableMagazinePhase::WellEmpty||p==DetachableMagazinePhase::ReplacementHeld||p==DetachableMagazinePhase::Guided;}
ManualReloadOwner PhysicalOwner(const HandInteractionSample& in,HandInteractionKey weapon){return {in.owner.actor,in.owner.actorGeneration,weapon.id,in.owner.equipGeneration,in.owner.space};}
}
Bc2MagazineResourceReload::Bc2MagazineResourceReload(MagazineResourceApi api,AmmoSupplyConfig pouch)noexcept:
 api_(api),pouch_(pouch),supply_(pouch),interaction_(Config(Xm8MagazineEquipment())){}
void Bc2MagazineResourceReload::Cancel(const HandInteractionSample& in,HandInteraction& hands)noexcept {
    last_=interaction_.Cancel(in,hands);supply_.Cancel(in,hands);carry_.reset();submittedSeat_.reset();needRestore_=true;
    // The selected item may now be a shell weapon, vehicle, or unsupported
    // pickup. Settling an old exact request must not require a fresh magazine
    // view for that new selection. Uncertain or mismatched outcomes stay blocked.
    if(pending_&&pendingRequest_&&api_.outcome)
        if(const auto terminal=api_.outcome(api_.context,pendingRequest_->intent.id,pendingRequest_->intent.context))
            RecoverTerminal(*terminal,in,hands);
    busy_=pending_||bool(reservation_);
    latestResult_={};latestResult_.interaction=last_;latestResult_.blocksWeaponActions=busy_;
}
MagazineResourceProbeState Bc2MagazineResourceReload::ProbeState(std::int64_t now)const noexcept {
    return {latestResult_,latestView_&&AmmoResourceViewFresh(*latestView_,now)?latestView_:std::nullopt};
}
bool Bc2MagazineResourceReload::Send(const AmmoResourceView& v,const MagazinePhysicalSample& s,AmmunitionOperation op,bool discard,std::uint64_t& intent)noexcept {
    if(pending_||!api_.submit||intent==UINT64_MAX)return false;
    const auto request=MagazineResourceRequestFor(v,s.family,s.input,s.weapon,++intent,op,discard);
    if(!request||!api_.submit(api_.context,*request))return false;
    if(!discard&&(op==AmmunitionOperation::ReturnMagazine||op==AmmunitionOperation::RefillMagazine))submittedSeat_=request;
    pendingRequest_=request;event_=request->intent.id;operation_=op;discard_=discard;pending_=true;++submitted_;return true;
}
void Bc2MagazineResourceReload::Carry(MagazineTracking& t,const MagazinePhysicalSample& s)noexcept {
    if(!t.target||!t.target->handTarget){carry_.reset();return;}
    const auto& target=*t.target;
    if(s.raw.valid&&s.raw.owner==s.nativeOwner&&s.raw.inputEvidence.owner==target.owner&&
       s.raw.inputEvidence.sequence==target.inputSequence&&s.raw.inputEvidence.observedNs==target.observedNs&&
       s.raw.inputEvidence.deadlineNs>=target.deadlineNs&&Rigid(s.raw.weaponWorldMeters))
        carry_=MagazineCarryFrame{target.owner,target.item,target.handClaim,target.gunClaim,target.inputSequence,target.nativeCycle,
            target.observedNs,target.deadlineNs,s.raw.weaponWorldMeters};
    if(!carry_||carry_->inputSequence!=target.inputSequence||carry_->handClaim!=target.handClaim||carry_->deadlineNs!=target.deadlineNs){t.target.reset();return;}
    if(target.role==MagazinePropRole::Removed)t.removalFrame=carry_;else if(target.role==MagazinePropRole::Replacement)t.replacementFrame=carry_;
}
MagazinePhysicalResult Bc2MagazineResourceReload::Tick(const MagazinePhysicalSample& s,HandInteraction& hands,std::uint64_t& intent)noexcept {
    latestResult_=TickImpl(s,hands,intent);return latestResult_;
}
bool Bc2MagazineResourceReload::RecoverTerminal(const AmmoResourceOutcome& outcome,const HandInteractionSample& in,HandInteraction& hands)noexcept {
    if(!pending_||!pendingRequest_||outcome.request!=*pendingRequest_||outcome.resolvedNs<outcome.request.intent.observedNs||outcome.resolvedNs>in.nowNs)return false;
    const auto& request=outcome.request;
    const bool rejected=outcome.state==AmmoResourceRequestState::Rejected&&!outcome.nativeDispatched&&!outcome.receipt;
    const bool discarded=outcome.state==AmmoResourceRequestState::Completed&&request.discard&&!outcome.nativeDispatched&&!outcome.receipt;
    bool applied=false;
    if(outcome.state==AmmoResourceRequestState::Completed&&outcome.nativeDispatched&&outcome.receipt){
        const auto& r=*outcome.receipt;const auto& c=r.command;
        applied=!request.discard&&c.context==request.intent.context&&c.operation==request.intent.operation&&
            c.original==request.intent.original&&c.requestedNs>=request.intent.observedNs&&c.admissionDeadlineNs<=request.intent.deadlineNs&&
            r.before==c.before&&r.after==c.after&&r.authorityVerified&&r.copiesVerified&&r.authorityInvocation&&
            r.beganNs>=c.requestedNs&&r.completedNs>=r.beganNs&&r.completedNs<c.deadlineNs&&r.completedNs==outcome.resolvedNs;
    }
    if(!rejected&&!discarded&&!applied)return false;
    if(reservation_){
        if(!physical_||request.intent.operation!=AmmunitionOperation::RefillMagazine||discarded)return false;
        const auto before=applied?outcome.receipt->before.reserve:int(reservation_->reserveBefore);
        const auto after=applied?outcome.receipt->after.reserve:before;
        if(before<0||after<0)return false;
        const ManualReloadAck ack{physical_->id,physical_->owner,physical_->operation,
            applied?ReloadAcknowledgement::Applied:ReloadAcknowledgement::Rejected};
        if(!supply_.SettleTerminal(in,hands,{*reservation_,ack,request.intent.id,outcome.resolvedNs,
            unsigned(before),unsigned(after),true}).accepted)return false;
        reservation_.reset();
    }
    if(rejected)++rejected_;else ++completed_;
    pending_=false;pendingRequest_.reset();physical_.reset();submittedSeat_.reset();return true;
}
MagazinePhysicalResult Bc2MagazineResourceReload::TickImpl(const MagazinePhysicalSample& s,HandInteraction& hands,std::uint64_t& intent)noexcept {
    MagazinePhysicalResult out;const auto now=s.input.nowNs;
    const auto cancel=[&](CancelCause cause=CancelCause::Other){NoteCancel(cause,s.input);Cancel(s.input,hands);out.interaction=last_;out.blocksWeaponActions=busy_;return out;};
    if(s.cancel||!s.input.focused||!s.input.tracked[0]||!s.input.tracked[1]||s.input.released[1])return cancel(CancelCause::Input);
    if(!api_.read||!api_.submit)return cancel(CancelCause::Api);
    const auto view=api_.read(api_.context,s.nativeOwner,now);
    if(!view)return cancel(CancelCause::View);
    const auto observation=MagazineResourceObservationFor(*view,s.family,s.input.owner,s.weapon,now);
    const auto reserve=MagazineResourceReserve(*view,now);
    if(!observation||!reserve)return cancel(CancelCause::Mapping);
    latestView_=view;
    const auto& mapping=*observation->ownerBinding;
    const bool same=owner_&&owner_->physical==mapping.physical&&owner_->weapon==mapping.weapon&&owner_->native==mapping.native;
    const auto gun=hands.Current(InteractionHand::Right);
    if(!gun||gun->token.owner!=s.input.owner||gun->token.item!=s.weapon||gun->token.kind!=HandClaimKind::GunHold||
        gun->inputSequence!=s.input.sequence||gun->deadlineNs<=now)return cancel(CancelCause::Gun);
    if(!same&&owner_)Cancel(s.input,hands);
    if((needRestore_||!same)&&pending_){
        if(!view->terminal||!RecoverTerminal(*view->terminal,s.input,hands))return cancel(CancelCause::Terminal);
    }
    // Do not apply a prior-context completion to a newly selected gun. The
    // inventory service retains and resolves that operation independently.
    if(!same){physical_.reset();event_=seatedEvent_=cycle_=0;sequence_=0;history_={};historyNext_=0;}
    owner_=mapping;
    if(view->phase==AmmunitionLedgerPhase::NeedsReconciliation){busy_=true;return cancel(CancelCause::Reconciliation);}
    if(pending_&&view->request==event_&&view->requestState==AmmoResourceRequestState::Rejected){
        Cancel(s.input,hands);
        if(!view->terminal||!RecoverTerminal(*view->terminal,s.input,hands))return cancel(CancelCause::Terminal);
    }
    const bool fresh=s.input.sequence>sequence_;
    DetachableMagazineSample physical;physical.input=s.input;physical.weapon=s.weapon;physical.trackingEpoch=s.trackingEpoch;
    physical.resource=observation;physical.gripPressed=s.gripPressed;physical.ejectPressed=s.ejectPressed;
    physical.native={s.input.owner,s.weapon,cycle_,reserve->observedNs,reserve->deadlineNs,true,false,false,{}};
    if(needRestore_&&pending_&&!reservation_&&view->request==event_&&view->requestState==AmmoResourceRequestState::Completed){
        pending_=false;physical_.reset();++completed_;
    }
    // A cancelled hand no longer owns the original. Discard its existing token
    // explicitly; never put those rounds back into the pooled reserve.
    if((needRestore_||!same)&&view->phase==AmmunitionLedgerPhase::Ready&&view->wellEmpty&&view->originalState==MagazineResourceState::Held){
        if(!pending_)Send(*view,s,AmmunitionOperation::RemoveMagazine,true,intent);
        busy_=true;out.blocksWeaponActions=true;return out;
    }
    if(pending_&&discard_&&view->request==event_&&view->requestState==AmmoResourceRequestState::Completed){pending_=false;++completed_;}
    if((needRestore_||!profile_||profile_!=s.family.binding.profile||!same)&&view->phase==AmmunitionLedgerPhase::Ready){
        if(reservation_)return cancel(CancelCause::Supply); // a submitted supply still requires its exact receipt
        if(!interaction_.RestoreResource(physical,hands,Config(*s.family.binding.profile)))return cancel(CancelCause::Restore);
        profile_=s.family.binding.profile;needRestore_=false;last_={};cycle_=view->wellEmpty?view->original->id:0;
        last_.phase=view->wellEmpty?DetachableMagazinePhase::WellEmpty:DetachableMagazinePhase::Attached;
        physical.native.cycle=cycle_;original_.reset();physical_.reset();carry_.reset();
    }
    if(needRestore_||!profile_){out.blocksWeaponActions=true;return out;}
    if(last_.phase==DetachableMagazinePhase::Complete){
        if(!interaction_.FinishResourceCycle())return cancel(CancelCause::Restore);last_={};cycle_=seatedEvent_=0;original_.reset();physical_.reset();submittedSeat_.reset();
        physical.native.cycle=0;
    }
    const auto owners=BindMagazineOwners(s.input.owner,s.weapon,*reserve,cycle_,now,s.family);
    if(!owners)return cancel(CancelCause::Owner);
    // Resolve native results BEFORE publishing the reduced reserve to supply.
    // Gather can repeat a tracked input packet after the native callbacks have
    // completed. DetachableMagazine intentionally does not advance on that
    // repeat, so keep the exact request pending until a fresh packet can consume
    // its acknowledgement. The published receipt, owner and original deadlines
    // remain mandatory; this neither renews authority nor repeats the operation.
    if(fresh&&pending_&&!discard_&&view->request==event_&&view->requestState==AmmoResourceRequestState::Completed&&view->receipt){
        const auto& r=*view->receipt;
        if(r.command.operation!=operation_||r.command.context!=mapping.native||r.after!=view->snapshot.counts||
           view->snapshot.observedNs<r.completedNs)return cancel(CancelCause::Receipt);
        if(operation_==AmmunitionOperation::RemoveMagazine){
            if(!physical_||!view->original||r.command.id!=view->original->id)return cancel(CancelCause::Receipt);cycle_=r.command.id;physical.native.cycle=cycle_;
        }else if(operation_==AmmunitionOperation::ReturnMagazine){
            if(!interaction_.CompleteOriginalResourceReturn(s.input,hands,r))return cancel(CancelCause::Receipt);
            last_={};cycle_=seatedEvent_=0;original_.reset();physical_.reset();physical.native.cycle=0;
        }else if(operation_==AmmunitionOperation::RefillMagazine){
            if(!reservation_||!physical_||r.before.reserve-r.after.reserve!=int(reservation_->units)||r.before.reserve!=int(reservation_->reserveBefore))return cancel(CancelCause::Receipt);
            const auto after=MagazineSupply(*owners,*reserve,s.trackingEpoch,now,reservation_->units);
            const ManualReloadAck ack{physical_->id,physical_->owner,physical_->operation,ReloadAcknowledgement::Applied};
            if(!after||!supply_.Resolve(s.input,hands,{*reservation_,ack,*after,r.authorityInvocation,r.completedNs,reserve->deadlineNs,true}).consumed)return cancel(CancelCause::Supply);
            reservation_.reset();
        }
        if(physical_){physical.native.acknowledgement={physical_->id,physical_->owner,physical_->operation,ReloadAcknowledgement::Applied};physical.native.acknowledgementVerified=true;}
        pending_=false;++completed_;
    }
    // Original resources do not require reserve ammunition. The chest provider
    // exists only for a nonzero replacement cost, including full-mag removal.
    const auto source=MagazineSupply(*owners,*reserve,s.trackingEpoch,now,reservation_?reservation_->units:0);
    std::optional<AmmoSupplySample> current;
    if(source){current=AmmoSupplySample{s.input,*source,s.geometrySequence,s.trackingEpoch,s.bodyFromHand,
        CanSupply(last_.phase)&&s.gripPressed,++intent};
        const auto supplied=supply_.Update(*current,hands);if(supplied.acquired)++acquired_;
        if(fresh){history_[historyNext_]=InputEvidence{*current};historyNext_=(historyNext_+1)%history_.size();}
    }
    const InputEvidence* geometry=nullptr;
    if(s.raw.valid&&s.raw.owner==s.nativeOwner&&s.raw.rigFingerprint==profile_->geometry->rigFingerprint&&s.originalHandEvidence&&
        SameInput(s.raw.inputEvidence,*s.originalHandEvidence)&&s.raw.inputEvidence.deadlineNs>now){
        for(const auto& e:history_)if(e&&SameInput(e->supply.input,s.raw.inputEvidence)){geometry=&*e;break;}
        // Extraction with zero reserve still uses actual retained hand packets.
        {physical.geometryInput=s.raw.inputEvidence;physical.geometrySequence=s.raw.inputEvidence.sequence;
            if(Rigid(s.raw.rawLeftWristWorldMeters)&&Rigid(s.raw.weaponWorldMeters))physical.weaponFromHandMeters=Multiply(s.raw.rawLeftWristWorldMeters,*InverseRigid(s.raw.weaponWorldMeters));}
    }
    if(intent>UINT64_MAX-2)return cancel(CancelCause::Counter);physical.intent=++intent;physical.replacement=supply_.Held();
    if(!cycle_&&!pending_)original_=OriginalMagazine{s.input.owner,s.weapon,{0x424332524d4147ull,intent},
        {profile_->geometry->interaction.insertion.id,profile_->geometry->interaction.insertion.revision},
        {reserve->identity.serverItem,reserve->identity.owner.equipGeneration},s.trackingEpoch,reserve->sequence,
        reserve->observedNs,reserve->deadlineNs,unsigned(reserve->loaded),unsigned(reserve->capacity)};
    physical.original=original_;physical.removalPermitted=view->phase==AmmunitionLedgerPhase::Ready&&!view->wellEmpty&&!pending_;
    // Native copies may be between their own Updates. No fake held lease is
    // fed to the interaction; exact completed receipts alone advance it.
    last_=interaction_.Update(physical,hands);sequence_=std::max(sequence_,s.input.sequence);
    if(last_.phase==DetachableMagazinePhase::Cancelled)return cancel(CancelCause::Interaction);
    if(last_.phase==DetachableMagazinePhase::WellEmpty&&view->originalState==MagazineResourceState::Held&&!pending_){
        if(!Send(*view,s,AmmunitionOperation::RemoveMagazine,true,intent))return cancel(CancelCause::Submit);
    }
    if(last_.originalSeat){
        if(!Send(*view,s,AmmunitionOperation::ReturnMagazine,false,intent))return cancel(CancelCause::Submit);seatedEvent_=event_;
    }
    if(last_.transaction.request){const auto request=*last_.transaction.request;physical_=request;
        if(request.owner!=PhysicalOwner(s.input,s.weapon))return cancel(CancelCause::Owner);
        if(request.operation==ReloadOperation::UnseatMagazine){
            if(!Send(*view,s,AmmunitionOperation::RemoveMagazine,false,intent))return cancel(CancelCause::Submit);
        }else if(request.operation==ReloadOperation::SeatMagazine){
            if(!last_.seat||!current||!geometry)return cancel(CancelCause::Supply);
            reservation_=supply_.ReserveFrom(*current,hands,geometry->supply,*last_.seat,request,cycle_);
            if(!reservation_||!Send(*view,s,AmmunitionOperation::RefillMagazine,false,intent))return cancel(CancelCause::Submit);
            seatedEvent_=event_;supply_.ReleaseSubmitted(s.input,hands,*reservation_);
        }
    }
    busy_=pending_||view->wellEmpty||last_.phase!=DetachableMagazinePhase::Attached;
    if(last_.phase==DetachableMagazinePhase::Complete)busy_=false;
    out.interaction=last_;out.blocksWeaponActions=busy_;out.ownsLeftHand=bool(last_.removalClaim)||bool(supply_.Held());
    out.acquired=acquired_;out.submitted=submitted_;out.completed=completed_;
    out.tracking={s.family,true,s.nativeOwner,s.input,s.meshes,*reserve,cycle_,last_.prop};
    out.tracking.resource=std::make_shared<const MagazineResourcePresentation>(MagazineResourcePresentation{*view,mapping,seatedEvent_,submittedSeat_});
    Carry(out.tracking,s);if(!MagazineTargetFresh(out.tracking,now))out.tracking.target.reset();
    if(bodyAmmo_&&!supply_.Held()&&!supply_.Pending())out.bodyAmmo=BuildMagazineBodyAmmo(out.tracking,gun,
        {pouch_.pouchCenterMeters,pouch_.pouchRadiusMeters},frame_,now);
    return out;
}
void Bc2MagazineResourceReload::NoteCancel(CancelCause cause,const HandInteractionSample& in)noexcept {
    auto& count=cancelCounts_[unsigned(cause)];if(count!=UINT64_MAX)++count;
    const CancelEvent event{cause,in.nowNs,in.sequence,event_,cycle_,unsigned(last_.phase),unsigned(last_.reason),unsigned(last_.nativeFailureCheck),pending_};
    if(!firstCancel_)firstCancel_=event;
    if(!firstActiveCancel_&&(pending_||cycle_||reservation_))firstActiveCancel_=event;
}
void Bc2MagazineResourceReload::Report(std::ostream& out)const {
    out<<"\"magazine_resource_hands\":{\"submitted\":"<<submitted_<<",\"completed\":"<<completed_<<",\"rejected\":"<<rejected_
       <<",\"phase\":"<<unsigned(last_.phase)<<",\"pending\":"<<pending_<<",\"cycle\":"<<cycle_;
    out<<",\"cancel_cause_names\":[\"other\",\"input\",\"api\",\"view\",\"mapping\",\"gun\",\"terminal\",\"reconciliation\",\"restore\",\"owner\",\"receipt\",\"supply\",\"counter\",\"interaction\",\"submit\"],\"cancel_counts\":[";
    for(std::size_t n=0;n<cancelCounts_.size();++n){if(n)out<<',';out<<cancelCounts_[n];}out<<']';
    const auto event=[&](const std::optional<CancelEvent>& e){if(!e){out<<"null";return;}
        out<<"{\"cause\":"<<unsigned(e->cause)<<",\"now_ns\":"<<e->now<<",\"input\":"<<e->input<<",\"request\":"<<e->request
           <<",\"cycle\":"<<e->cycle<<",\"phase\":"<<e->phase<<",\"reason\":"<<e->reason<<",\"native_check\":"<<e->nativeCheck<<",\"pending\":"<<e->pending<<'}';};
    out<<",\"first_cancel\":";event(firstCancel_);out<<",\"first_active_cancel\":";event(firstActiveCancel_);out<<'}';
}
}
