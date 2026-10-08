#include "Bc2NativeCycleService.h"
#include "Bc2NativeCycleEvidence.h"
#include "Bc2M95StockShotConfig.h"
#include <algorithm>
#include <limits>

namespace fvr::bc2 {
using namespace interaction;
namespace {
constexpr NativeCycleHeldBoundary SpasBoundary{7,6,8,.1f,.5f};
constexpr NativeCycleHeldBoundary SpasEmptyBoundary{6,5,1,.1f,1.f,true};
constexpr NativeCycleHeldBoundary M95Boundary{8,7,1,.1f,2.3f};
const NativeCycleHeldBoundary& Boundary(Bc2NativeCycleMode mode,bool empty)noexcept {
    return mode==Bc2NativeCycleMode::M95?M95Boundary:empty?SpasEmptyBoundary:SpasBoundary;
}
bool Safe(const ReloadUpdateContext& c)noexcept {
    return ValidManualReloadDelta(c.deltaSeconds)&&!c.inputFlags&&!c.fireRequested&&!c.orderRequested&&!c.reloadRequested&&
        !c.flags24Through28[2]&&!c.flags24Through28[3]&&!c.flags24Through28[4]&&
        std::isfinite(c.reloadTimeMultiplier)&&c.reloadTimeMultiplier>0&&c.reloadTimeMultiplier<=4;
}
bool Held(const ReloadFiringObservation& b,bool restored,unsigned restoredPrevious,const NativeCycleHeldBoundary& p)noexcept {
    return b.currentState==p.current&&(b.previousState==p.previous||(restored&&b.previousState==restoredPrevious))&&b.nextState==p.next&&std::isfinite(b.phaseTimer)&&b.phaseTimer>0&&b.phaseTimer<=p.maxTimer&&!(b.flagsA8&24);
}
bool Active(Bc2NativeCyclePhase phase)noexcept {
    return phase!=Bc2NativeCyclePhase::Watching&&phase!=Bc2NativeCyclePhase::Complete;
}
}
bool Bc2NativeCycleService::EnableRecovery()noexcept {
    if(control_.input.sequence||cycle_||phase_!=Bc2NativeCyclePhase::Watching)return false;
    recoveryEnabled_=true;return true;
}
bool Bc2NativeCycleService::BeginRecovery(std::int64_t now)noexcept {
    if(Recovering()){recovery_.Interrupt(now);return !recovery_.Uncertain();}
    if(!recoveryEnabled_||phase_!=Bc2NativeCyclePhase::Held||release_||ready_||firedMask_!=7||heldMask_!=7||
       now<began_||guardEpoch_==UINT64_MAX)return false;
    // Conversion must occur while the native callbacks still prove established
    // holds. A long unobserved gap cannot acquire a continuous guard retroactively.
    for(unsigned b=0;b<3;++b){
        if(!heldAt_[b]||now<heldAt_[b]||now-heldAt_[b]>100000000)return false;
        if(openUpdate_[b]&&!openHold_[b])return false;
    }
    Bc2NativeCycleRecoveryState candidate;
    if(!candidate.Begin({native_,config_},{control_.input.owner,control_.item,control_.mechanism,
        cycle_,shot_,guardEpoch_+1,loaded_,reserve_,capacity_},began_,mode_==Bc2NativeCycleMode::M95,emptyCycle_))return false;
    for(unsigned b=0;b<3;++b)if(openUpdate_[b]&&!candidate.BeginHold(b,openUpdate_[b],before_[b].nowNs,openDeadline_[b]))return false;
    recovery_=std::move(candidate);++guardEpoch_;phase_=Bc2NativeCyclePhase::Converging;
    held_.reset();issued_={};suspendedAt_=suspendedUntil_=0;return true;
}
NativeCycleRecoveryDecision Bc2NativeCycleService::EvaluateRecovery(const NativeCycleRecoveryScope& scope,
    const ReloadFlowEventInput& entry,int capacity,std::int64_t deadline)noexcept {
    lastEvent_=entry.nativeInvocation;
    if(!Recovering())return {};
    const auto result=recovery_.Evaluate(scope,entry,capacity,deadline);
    if(!result)Cancel(Bc2NativeCycleFailure::Owner);return result;
}
void Bc2NativeCycleService::FinishRecovery(const NativeCycleRecoveryDecision& d,const ReloadFlowRecord& r,
    const NativeCycleInputGuard& guard,unsigned steps,unsigned rejected)noexcept {
    lastEvent_=r.id;
    if(!Recovering())return;
    if(!recovery_.Finish(d,r,guard,steps,rejected))Cancel(Bc2NativeCycleFailure::Completion);
}
bool Bc2NativeCycleService::SubmitRecovery(const WeaponCycleIdleDebtRelease& release,const HandInteractionSample& input)noexcept {
    return Recovering()&&input.owner==control_.input.owner&&input.sequence==control_.input.sequence&&
        input.observedNs==control_.input.observedNs&&input.deadlineNs==control_.input.deadlineNs&&
        input.nowNs>=control_.input.nowNs&&recovery_.Submit(release,input);
}
bool Bc2NativeCycleService::AcknowledgeRecovery(const WeaponCycleIdleDebtReady& ready,std::int64_t now)noexcept {
    if(!Recovering()||!recovery_.Acknowledge(ready,now))return false;
    phase_=Bc2NativeCyclePhase::Complete;return true;
}
bool Bc2NativeCycleService::Configure(Bc2NativeCycleMode mode)noexcept {
    if((mode!=Bc2NativeCycleMode::Spas&&mode!=Bc2NativeCycleMode::M95)||control_.input.sequence||native_.owner.player||
       cycle_||shot_||ready_||held_||phase_!=Bc2NativeCyclePhase::Watching||
       std::any_of(openUpdate_.begin(),openUpdate_.end(),[](auto value){return value!=0;}))return false;
    mode_=mode;return true;
}
bool Bc2NativeCycleService::Select(const Bc2NativeCycleSelection& source)noexcept {
    const auto& r=source.reserve;const auto& n=r.identity;const auto& o=n.owner;
    if((source.mode!=Bc2NativeCycleMode::Spas&&source.mode!=Bc2NativeCycleMode::M95)||
       !(source.mode==Bc2NativeCycleMode::M95?M95StockShotConfig(source.config):IsDiagnosticSpasConfig(source.config))||
       !r.verified||!r.allThreeIdle||!r.sequence||r.sequence<=selectionSequence_||source.nowNs<selectionTime_||
       !weapon_cycle_detail::Window(r.observedNs,r.deadlineNs,source.nowNs)||r.deadlineNs-r.observedNs>200000000||
       r.loaded<0||r.reserve<0||r.reserve>1000000||r.capacity<=0||r.capacity>1000000||r.loaded>r.capacity||
       (source.mode==Bc2NativeCycleMode::M95&&r.capacity!=5)||
       o.player<0x10000||o.soldier<0x10000||o.weak<0x10000||o.weapon<0x10000||
       !o.actorGeneration||!o.equipGeneration||!o.space||n.serverPlayer<0x10000||n.serverSoldier<0x10000||n.serverItem<0x10000)return false;
    for(unsigned a=0;a<3;++a){if(n.firing[a]<0x10000)return false;
        for(unsigned b=0;b<a;++b)if(n.firing[a]==n.firing[b])return false;}
    // Yield's exact ack/drain policy is shared with resource ownership. A stock
    // idle read cannot make this succeed while any old action remains owed.
    if(!YieldForReload())return false;
    mode_=source.mode;selectionRequired_=true;selection_=source;
    selectionSequence_=r.sequence;selectionTime_=source.nowNs;
    native_=n;config_=source.config;loaded_=r.loaded;reserve_=r.reserve;capacity_=r.capacity;
    return true;
}
bool Bc2NativeCycleService::Mapping(const Bc2NativeCycleControl& c)const noexcept {
    const auto& n=c.nativeOwner;const auto& h=c.input.owner;
    return c.permitted&&n.player>=0x10000&&n.soldier>=0x10000&&n.weak>=0x10000&&n.weapon>=0x10000&&
        weapon_cycle_detail::Owner(h)&&n.actorGeneration==h.actorGeneration&&n.equipGeneration&&n.space==h.space&&
        h.actor==((std::uint64_t(n.weak)<<32)|n.soldier)&&c.item.id==n.weapon&&c.item.generation==h.equipGeneration&&
        weapon_cycle_detail::Key(c.mechanism)&&c.input.sequence&&c.input.focused&&c.input.tracked[0]&&c.input.tracked[1]&&
        weapon_cycle_detail::Window(c.input.observedNs,c.input.deadlineNs,c.input.nowNs);
}
bool Bc2NativeCycleService::Control(const Bc2NativeCycleControl& c)noexcept {
    if(phase_==Bc2NativeCyclePhase::Cancelled)return false;
    if(selectionRequired_&&(!selection_||c.nativeOwner!=selection_->reserve.identity.owner))return false;
    lastEvent_=0; // Controller admission is not a native invocation.
    if(suspendedAt_&&c.input.nowNs>=suspendedUntil_){Cancel(Bc2NativeCycleFailure::Expired);return false;}
    if(!Mapping(c)){if(Active(phase_))Cancel(Bc2NativeCycleFailure::Control);return false;}
    if(control_.input.sequence){
        if(c.input.nowNs<control_.input.nowNs||c.input.observedNs<control_.input.observedNs||
           (c.input.owner==control_.input.owner&&c.input.sequence<control_.input.sequence))return false;
        if(c.input.owner==control_.input.owner&&c.input.sequence==control_.input.sequence&&
           (c.input.observedNs!=control_.input.observedNs||c.input.deadlineNs!=control_.input.deadlineNs))return false;
        if(Active(phase_)&&(c.nativeOwner!=control_.nativeOwner||c.input.owner!=control_.input.owner||c.item!=control_.item||c.mechanism!=control_.mechanism)){
            Cancel(Bc2NativeCycleFailure::Owner);return false;
        }
    }
    if(!Active(phase_)&&c.nativeOwner!=control_.nativeOwner){native_={};config_={};firedMask_=heldMask_=readyMask_=0;phase_=Bc2NativeCyclePhase::Watching;}
    // Native-only suspension never creates a controller edge or a renewed hand
    // lease. A fresh tracked neutral packet from the SAME owner can end it.
    const unsigned mechanismHand=mode_==Bc2NativeCycleMode::M95?1u:0u;
    if(suspendedAt_&&c.input.observedNs>suspendedAt_&&c.input.sequence>control_.input.sequence&&
       c.input.released[mechanismHand]&&!c.input.released[1u-mechanismHand]&&!c.release){suspendedAt_=suspendedUntil_=0;++heldResumptions_;}
    control_=c;
    if(Recovering())return !c.release&&recovery_.Control(c.input);
    return !c.release||Release(*c.release,c.input.nowNs);
}
bool Bc2NativeCycleService::YieldForReload()noexcept {
    if(Active(phase_)||ready_||std::any_of(openUpdate_.begin(),openUpdate_.end(),[](auto id){return id!=0;}))return false;
    Cancel(Bc2NativeCycleFailure::None);
    phase_=Bc2NativeCyclePhase::Watching;firedMask_=heldMask_=readyMask_=0;
    release_.reset();releaseSubmittedNs_=0;forwardReadyAt_=0;selection_.reset();recovery_={};return true;
}
bool Bc2NativeCycleService::SuspendCompletion()noexcept {
    if(phase_!=Bc2NativeCyclePhase::Releasing||!release_)return false;
    return true;
}
bool Bc2NativeCycleService::SuspendHeld(std::int64_t now)noexcept {
    if(suspendedAt_)return HeldWatch(now).has_value();
    if(phase_!=Bc2NativeCyclePhase::Held||release_||ready_||firedMask_!=7||heldMask_!=7||
       !held_||!weapon_cycle_detail::Lease(*held_,now)||now>=deadline_||
       now>INT64_MAX-MaximumHoldSuspensionNs)return false;
    suspendedAt_=now;suspendedUntil_=std::min(deadline_,now+MaximumHoldSuspensionNs);
    // These were controller-use leases. Native hold authority below is scoped
    // separately and cannot submit a gesture or resurrect a hand claim.
    held_.reset();issued_={};++heldSuspensions_;return true;
}
std::optional<Bc2NativeCycleCompletionWatch> Bc2NativeCycleService::HeldWatch(std::int64_t now)const noexcept {
    if(!suspendedAt_||phase_!=Bc2NativeCyclePhase::Held||release_||ready_||firedMask_!=7||heldMask_!=7||
       now<suspendedAt_||now>=suspendedUntil_||now>=deadline_)return {};
    return Bc2NativeCycleCompletionWatch{native_,config_,cycle_,suspendedAt_,suspendedUntil_,true};
}
std::optional<Bc2NativeCycleCompletionWatch> Bc2NativeCycleService::ObservationWatch(std::int64_t now)const noexcept {
    if(Recovering()&&!recovery_.Uncertain()&&now>=recovery_.Began()&&now<=INT64_MAX-100000000)
        return Bc2NativeCycleCompletionWatch{native_,config_,cycle_,recovery_.Began(),now+100000000,false};
    if(const auto completion=CompletionWatch(now))return completion;
    return HeldWatch(now);
}
std::uint64_t Bc2NativeCycleService::ObservationAuthority(std::uint64_t previous,std::int64_t now)const noexcept {
    if(const auto watch=ObservationWatch(now))return watch->cycle;
    if(suspendedAt_)return 0;
    return previous==cycle_&&now<deadline_&&
        (phase_==Bc2NativeCyclePhase::Held||phase_==Bc2NativeCyclePhase::Releasing)?previous:0;
}
std::optional<Bc2NativeCycleCompletionWatch> Bc2NativeCycleService::CompletionWatch(std::int64_t now)const noexcept {
    if(phase_!=Bc2NativeCyclePhase::Releasing||!release_||now<release_->observedNs||
       now>=deadline_||now-release_->observedNs>=CompletionNs())return {};
    return Bc2NativeCycleCompletionWatch{native_,config_,cycle_,release_->observedNs,
        release_->observedNs+std::min(CompletionNs(),deadline_-release_->observedNs)};
}
bool Bc2NativeCycleService::Current(const ReloadHoldInput& in)const noexcept {
    if(selectionRequired_&&(!selection_||in.identity!=selection_->reserve.identity||in.config!=selection_->config))return false;
    const bool observing=ObservationWatch(in.nowNs).has_value();
    if(!in.verified||in.branch>=3||(!observing&&(!Mapping(control_)||
       !weapon_cycle_detail::Window(control_.input.observedNs,control_.input.deadlineNs,in.nowNs)))||
       in.identity.owner!=control_.nativeOwner||
       in.leaseDeadlineNs<=in.nowNs||in.leaseDeadlineNs-in.nowNs>200000000||!(mode_==Bc2NativeCycleMode::M95?M95StockShotConfig(in.config):IsDiagnosticSpasConfig(in.config)))return false;
    if(in.identity.serverPlayer<0x10000||in.identity.serverSoldier<0x10000||in.identity.serverItem<0x10000)return false;
    for(unsigned branch=0;branch<3;++branch){
        if(in.identity.firing[branch]<0x10000||in.branches[branch].address!=in.identity.firing[branch]||
           in.branches[branch].wrapperOffset!=(branch==0?0x3cu:branch==1?0x40u:0x10u)||
           in.capacities[branch]<=0||in.capacities[branch]>1000000)return false;
        for(unsigned other=0;other<branch;++other)if(in.identity.firing[other]==in.identity.firing[branch])return false;
    }
    return true;
}
Bc2NativeCycleDecision Bc2NativeCycleService::Evaluate(const ReloadHoldInput& in,std::uint64_t invocation)noexcept {
    Bc2NativeCycleDecision out;
    if(phase_==Bc2NativeCyclePhase::Cancelled||Recovering())return out;
    lastEvent_=invocation;
    if(suspendedAt_&&in.nowNs>=suspendedUntil_){Cancel(Bc2NativeCycleFailure::Expired);return out;}
    // Same-branch reentry must not overwrite the pending own-Update receipt.
    // Cancel also clears a pre-shot evaluation while Watching.
    if(in.branch<3&&openUpdate_[in.branch]){Cancel(Bc2NativeCycleFailure::Owner);return out;}
    if(!Current(in)||!invocation||invocation<=lastInvocation_[in.branch<3?in.branch:0]){
        if(Active(phase_))Cancel(Bc2NativeCycleFailure::Owner);return out;
    }
    if(native_.owner.player&&(native_!=in.identity||config_!=in.config)){
        if(Active(phase_)){Cancel(Bc2NativeCycleFailure::Owner);return out;}
        firedMask_=heldMask_=readyMask_=0;
    }
    native_=in.identity;config_=in.config;lastInvocation_[in.branch]=invocation;openUpdate_[in.branch]=invocation;openHold_[in.branch]=false;before_[in.branch]=in;
    out={invocation,cycle_,in.branch,true,false,in.leaseDeadlineNs};
    if(phase_==Bc2NativeCyclePhase::Watching||phase_==Bc2NativeCyclePhase::Complete)return out;
    if(in.nowNs>=deadline_){Cancel(Bc2NativeCycleFailure::Expired);return out;}
    if(phase_==Bc2NativeCyclePhase::Releasing)return out;
    if(!Safe(in.context)||in.contextObservedNs<=0||in.contextObservedNs>in.nowNs||in.nowNs-in.contextObservedNs>ContextFreshNs){
        contexts_[in.branch]=0;
        if(phase_==Bc2NativeCyclePhase::Held)Cancel(Bc2NativeCycleFailure::Input);
        return out;
    }
    for(unsigned n=0;n<3;++n)if(!Held(in.branches[n],n<2&&restoredAt_[n]>=began_&&restoredAt_[n]<=in.nowNs,n<2?restoredPrevious_[n]:0u,Boundary(mode_,emptyCycle_))||in.branches[n].loaded!=loaded_||in.branches[n].reserve!=reserve_||
        in.capacities[n]!=capacity_){
        if(phase_==Bc2NativeCyclePhase::Held)Cancel(Bc2NativeCycleFailure::State);
        return out;
    }
    contexts_[in.branch]=in.contextObservedNs;
    if(firedMask_!=7)return out;
    for(auto observed:contexts_)if(observed<=0||observed>in.nowNs||in.nowNs-observed>ContextFreshNs)return out;
    if(!heldMask_)for(const auto& branch:in.branches)if(branch.phaseTimer<Boundary(mode_,emptyCycle_).minArmTimer)return out;
    phase_=heldMask_==7?Bc2NativeCyclePhase::Held:Bc2NativeCyclePhase::Arming;
    out.hold=true;out.cycle=cycle_;
    out.deadlineNs=std::min({deadline_,in.leaseDeadlineNs,suspendedAt_?suspendedUntil_:control_.input.deadlineNs});
    openHold_[in.branch]=true;openDeadline_[in.branch]=out.deadlineNs;return out;
}
void Bc2NativeCycleService::Finish(const Bc2NativeCycleDecision& d,const ReloadFlowRecord& record)noexcept {
    if(!d.tracked||d.branch>=3||record.id!=d.invocation||openUpdate_[d.branch]!=d.invocation)return;
    openUpdate_[d.branch]=0;openHold_[d.branch]=false;
    if(phase_==Bc2NativeCyclePhase::Cancelled)return;
    lastEvent_=record.id;
    const auto& in=before_[d.branch];
    if(Recovering()){
        auto boundary=Boundary(mode_,emptyCycle_);
        if(d.branch<2&&restoredAt_[d.branch]>=began_&&restoredAt_[d.branch]<=record.entry.nowNs&&
           record.entry.boundary.previous==restoredPrevious_[d.branch])boundary.previous=restoredPrevious_[d.branch];
        const bool exact=d.hold&&d.cycle==cycle_&&CycleHold(record,native_,d.branch,boundary,false)&&record.exit.nowNs<d.deadlineNs;
        if(!recovery_.RestoreHold(d.branch,d.invocation,record.exit.nowNs,exact))Cancel(Bc2NativeCycleFailure::Hold);
        return;
    }
    if(!ValidateCycleUpdate(record,native_,d.branch)){if(Active(phase_))Cancel(Bc2NativeCycleFailure::Completion);return;}
    const auto& shotAfter=*record.exit.boundary;
    const bool emptyShot=mode_==Bc2NativeCycleMode::Spas&&CycleEmptyShot(record,native_,d.branch);
    const bool positiveShot=CycleShot(record,native_,d.branch)&&(mode_!=Bc2NativeCycleMode::M95||
        (shotAfter.current==6&&shotAfter.previous==5&&shotAfter.next==7)||
        (shotAfter.current==8&&shotAfter.previous==7&&shotAfter.next==1));
    if(record.entry.boundary.loaded!=shotAfter.loaded&&!emptyShot&&!positiveShot){
        phase_=Bc2NativeCyclePhase::Cancelled;failure_=Bc2NativeCycleFailure::Shot;failureInvocation_=record.id;
        openUpdate_={};held_.reset();ready_.reset();issued_={};return;
    }
    if(record.entry.boundary.loaded==1&&record.exit.boundary->loaded==0&&!emptyShot){
        // A selected own Update consumed the last round but did not establish
        // the reviewed empty path. Withhold firing instead of silently ignoring it.
        phase_=Bc2NativeCyclePhase::Cancelled;failure_=Bc2NativeCycleFailure::Shot;failureInvocation_=record.id;
        openUpdate_={};held_.reset();ready_.reset();issued_={};return;
    }
    if(emptyShot||positiveShot){
        if(ready_){Cancel(Bc2NativeCycleFailure::Completion);return;}
        if(phase_!=Bc2NativeCyclePhase::Watching&&phase_!=Bc2NativeCyclePhase::Complete&&phase_!=Bc2NativeCyclePhase::ShotObserved){
            Cancel(Bc2NativeCycleFailure::Shot);return;
        }
        const auto& after=*record.exit.boundary;
        if(phase_!=Bc2NativeCyclePhase::ShotObserved){
            if(cycle_==UINT64_MAX||shot_==UINT64_MAX||record.exit.nowNs>INT64_MAX-MaximumCycleNs){Cancel(Bc2NativeCycleFailure::Shot);return;}
            recovery_={};++cycle_;++shot_;began_=record.exit.nowNs;deadline_=began_+MaximumCycleNs;
            emptyCycle_=emptyShot;loaded_=after.loaded;reserve_=after.reserve;capacity_=in.capacities[d.branch];
            firedMask_=heldMask_=readyMask_=0;contexts_={};heldAt_={};finishedAt_={};restoredAt_={};restoredPrevious_={};preRestoredAt_={};preRewindAt_={};cycleState_={};tail_={};issued_={};held_.reset();ready_.reset();release_.reset();releaseSubmittedNs_=0;forwardReadyAt_=0;
            phase_=Bc2NativeCyclePhase::ShotObserved;
        }
        if(after.loaded!=loaded_||after.reserve!=reserve_||in.capacities[d.branch]!=capacity_||
           (firedMask_&(1u<<d.branch))||emptyShot!=emptyCycle_||after.loaded<0||after.loaded>=capacity_){Cancel(Bc2NativeCycleFailure::Shot);return;}
        firedMask_|=1u<<d.branch;cycleState_[d.branch]=after.current;return;
    }
    cycleState_[d.branch]=record.exit.boundary->current;
    if(d.hold){
        auto boundary=Boundary(mode_,emptyCycle_);
        if(d.branch<2&&restoredAt_[d.branch]>=began_&&restoredAt_[d.branch]<=record.entry.nowNs&&record.entry.boundary.previous==restoredPrevious_[d.branch])boundary.previous=restoredPrevious_[d.branch];
        if(d.cycle!=cycle_||!CycleHold(record,native_,d.branch,boundary,false)||record.exit.nowNs>=d.deadlineNs){
            Cancel(record.exit.hold.applied&&!record.exit.hold.restored?Bc2NativeCycleFailure::Restore:Bc2NativeCycleFailure::Hold);return;
        }
        if(phase_==Bc2NativeCyclePhase::Releasing){
            // Submission may overlap an already executing scoped hold. Its
            // exact restored receipt completes, but cannot republish Held.
            if(!release_||record.entry.nowNs>releaseSubmittedNs_){Cancel(Bc2NativeCycleFailure::Completion);return;}
            return;
        }
        heldMask_|=1u<<d.branch;heldAt_[d.branch]=record.exit.nowNs;
        if(heldMask_==7){
            phase_=Bc2NativeCyclePhase::Held;
            if(suspendedAt_)return; // Native safety hold only; no hand lease publication.
            auto deadline=std::min({deadline_,control_.input.deadlineNs,in.leaseDeadlineNs});
            for(auto at:heldAt_)deadline=std::min(deadline,at+ContextFreshNs);
            PublishHeld(record.exit.nowNs,deadline);
        }
        return;
    }
    if(phase_==Bc2NativeCyclePhase::Releasing&&release_){
        const auto& after=*record.exit.boundary;
        if(record.entry.nowNs<release_->observedNs||after.loaded!=loaded_||after.reserve!=reserve_||record.exit.hold.applied||record.exit.holdRequested){
            Cancel(Bc2NativeCycleFailure::Completion);return;
        }
        const bool forward=mode_==Bc2NativeCycleMode::M95&&d.branch==0&&forwardReadyAt_&&
            record.entry.nowNs>=forwardReadyAt_&&CycleM95ForwardIdleUpdate(record,native_);
        if((tail_[d.branch]==TailLength()||forward)&&after.current==2&&after.next==2){
            readyMask_|=1u<<d.branch;finishedAt_[d.branch]=record.exit.nowNs;
            if(readyMask_==7){
                const auto observed=*std::max_element(finishedAt_.begin(),finishedAt_.end());
                const auto deadline=std::min(in.leaseDeadlineNs,observed+ContextFreshNs);
                if(sequence_==UINT64_MAX||observed>=deadline){Cancel(Bc2NativeCycleFailure::Completion);return;}
                ready_=WeaponCycleReady{*release_,++sequence_,observed,deadline,true,true};
                phase_=Bc2NativeCyclePhase::Complete;held_.reset();
            }
        }
    }
}
void Bc2NativeCycleService::Commit(const ReloadFlowRecord& record)noexcept {
    if(Recovering()){lastEvent_=record.id;if(!recovery_.Commit(record))Cancel(Bc2NativeCycleFailure::Completion);return;}
    if(phase_!=Bc2NativeCyclePhase::Releasing||!release_||record.entry.boundary.branch>=3)return;
    lastEvent_=record.id;
    const auto branch=record.entry.boundary.branch;
    const auto event=CycleCommit(record,native_,branch,openUpdate_[branch]);
    if(!event||event->beginNs<release_->observedNs||event->loaded!=loaded_||event->reserve!=reserve_){Cancel(Bc2NativeCycleFailure::Completion);return;}
    constexpr std::array<unsigned,3> expected{8,1,2};
    if(tail_[branch]>=TailLength()||event->current!=expected[tail_[branch]+(TailLength()==2?1:0)]){Cancel(Bc2NativeCycleFailure::Completion);return;}
    ++tail_[branch];
}
void Bc2NativeCycleService::ObserveRestore(const ReloadFlowRecord& record)noexcept {
    if(phase_==Bc2NativeCyclePhase::Watching||phase_==Bc2NativeCyclePhase::Cancelled||!native_.owner.player)return;
    lastEvent_=record.id;
    const auto branch=record.entry.boundary.branch;
    if(Recovering()||(phase_==Bc2NativeCyclePhase::Complete&&recovery_.Active())){
        if(!recovery_.Restore(record)){ContradictoryRestore();}return;}
    // No selected-server Restore contract is established. An unrelated actor
    // never reaches this service; runtime filters it before taking the gate.
    if(branch>=2){ContradictoryRestore();return;}
    if(!(firedMask_&(1u<<branch)))return;
    if(mode_==Bc2NativeCycleMode::M95&&CyclePreHoldSnapshotProgression(record,native_,branch,Boundary(mode_,false),.1f)){
        // Native prediction can cross the post-shot wait/hold boundary in
        // either direction before interception has begun. Preserve the actual
        // Restore output's previous state; never invent an omitted Commit.
        if(heldMask_||release_||ready_||cycleState_[branch]!=record.entry.boundary.current||
           (phase_!=Bc2NativeCyclePhase::ShotObserved&&phase_!=Bc2NativeCyclePhase::Arming)||
           record.id<=lastInvocation_[branch]||openUpdate_[branch]||record.entry.nowNs<began_||record.exit.nowNs>=deadline_||
           record.exit.boundary->loaded!=loaded_||record.exit.boundary->reserve!=reserve_){ContradictoryRestore();return;}
        const auto& after=*record.exit.boundary;
        lastInvocation_[branch]=record.id;cycleState_[branch]=after.current;contexts_={};
        if(after.current==Boundary(mode_,false).current){restoredAt_[branch]=record.exit.nowNs;restoredPrevious_[branch]=after.previous;}
        else {preRewindAt_[branch]=record.exit.nowNs;restoredAt_[branch]=preRestoredAt_[branch]=0;restoredPrevious_[branch]=0;}
        return;
    }
    if(mode_==Bc2NativeCycleMode::M95&&CycleM95ForwardReadyRestore(record,native_)){
        // Actual239 clientB completed its own8->1->2 before clientA's native
        // snapshot correction. ClientA must still finish its own neutral idle
        // Update; server must independently finish its own ordered tail.
        if(phase_!=Bc2NativeCyclePhase::Releasing||!release_||branch!=0||forwardReadyAt_||tail_[0]||
           tail_[1]!=TailLength()||!(readyMask_&2u)||finishedAt_[1]>record.entry.nowNs||
           record.id<=lastInvocation_[0]||openUpdate_[0]||record.entry.nowNs<releaseSubmittedNs_||
           record.exit.nowNs>=deadline_||record.exit.nowNs-release_->observedNs>=CompletionNs()||
           record.exit.boundary->loaded!=loaded_||record.exit.boundary->reserve!=reserve_){ContradictoryRestore();return;}
        forwardReadyAt_=record.exit.nowNs;lastInvocation_[0]=record.id;cycleState_[0]=2;return;
    }
    const auto proof=CyclePredictionRestore(record,native_,branch);
    if(!proof||proof->invocation<=lastInvocation_[branch]||openUpdate_[branch]||proof->beginNs<began_||
       (phase_!=Bc2NativeCyclePhase::Complete&&proof->endNs>=deadline_)||
       proof->loaded!=loaded_||proof->reserve!=reserve_){ContradictoryRestore();return;}
    const bool ready=proof->current==2&&proof->next==2&&
        (proof->beforePrevious==1||proof->beforePrevious==2||
         (mode_==Bc2NativeCycleMode::M95&&branch==0&&forwardReadyAt_&&(readyMask_&1u)&&proof->beforePrevious==8))&&
        proof->beforeTimer==0&&proof->timer==0;
    const bool completedTail=tail_[branch]==TailLength()||
        (mode_==Bc2NativeCycleMode::M95&&branch==0&&forwardReadyAt_&&(readyMask_&1u));
    const auto& boundary=Boundary(mode_,emptyCycle_);
    const bool held=proof->current==boundary.current&&proof->next==boundary.next&&
        (proof->beforePrevious==boundary.previous||(proof->beforePrevious==restoredPrevious_[branch]&&restoredAt_[branch]))&&
        proof->beforeTimer>0&&proof->beforeTimer<=boundary.maxTimer&&proof->timer>0&&proof->timer<=boundary.maxTimer;
    if(phase_==Bc2NativeCyclePhase::Complete){
        if(!completedTail||!(readyMask_&(1u<<branch))||!ready){ContradictoryRestore();return;}
    }else if(phase_==Bc2NativeCyclePhase::Releasing){
        // Other branches can still be held while an earlier branch is ready.
        // A completed branch must never rewind to the pre-release snapshot.
        if(!((tail_[branch]==0&&held)||(completedTail&&(readyMask_&(1u<<branch))&&ready))){ContradictoryRestore();return;}
    }else {
        const bool pre=!emptyCycle_&&proof->current==6&&proof->next==7&&cycleState_[branch]==6&&
            !restoredAt_[branch]&&!(heldMask_&(1u<<branch))&&
            (proof->beforePrevious==5||(proof->beforePrevious==6&&preRestoredAt_[branch])||
             (mode_==Bc2NativeCycleMode::M95&&proof->beforePrevious==8&&preRewindAt_[branch]))&&
            proof->beforeTimer>0&&proof->beforeTimer<=30&&proof->timer>0&&proof->timer<=30;
        // State 6 is the original post-shot interval, not a held pump lease.
        // Its timer is not interpreted as an unverified native rate of fire.
        if(pre)preRestoredAt_[branch]=proof->endNs;
        else if(!held||cycleState_[branch]!=boundary.current){ContradictoryRestore();return;}
    }
    lastInvocation_[branch]=proof->invocation;
    if(held){restoredAt_[branch]=proof->endNs;restoredPrevious_[branch]=record.exit.boundary->previous;}
}
void Bc2NativeCycleService::ContradictoryRestore()noexcept {
    // Owner retirement retains a completed outcome; contradictory evidence
    // from that same owner must revoke it, including after acknowledgement.
    ready_.reset();held_.reset();readyMask_=heldMask_=0;openUpdate_={};issued_={};suspendedAt_=suspendedUntil_=0;
    phase_=Bc2NativeCyclePhase::Cancelled;failure_=Bc2NativeCycleFailure::Restore;failureInvocation_=lastEvent_;
}
void Bc2NativeCycleService::PublishHeld(std::int64_t observed,std::int64_t deadline)noexcept {
    if(observed>=deadline||sequence_==UINT64_MAX)return;
    held_=WeaponCycleLease{control_.input.owner,control_.item,control_.mechanism,cycle_,shot_,++sequence_,observed,deadline,true};
    issued_[issueAt_]=*held_;issueAt_=(issueAt_+1)%issued_.size();
}
bool Bc2NativeCycleService::Release(const WeaponCycleRelease& release,std::int64_t now)noexcept {
    if(release_){return *release_==release;}
    if(phase_!=Bc2NativeCyclePhase::Held||!held_||!release.request||!release.inputSequence||
       release.inputSequence>control_.input.sequence||!weapon_cycle_detail::Window(release.observedNs,release.deadlineNs,now)||
       release.deadlineNs>release.cycle.deadlineNs||!weapon_cycle_detail::Same(release.cycle,*held_)||
       std::find(issued_.begin(),issued_.end(),release.cycle)==issued_.end())return false;
    release_=release;releaseSubmittedNs_=now;phase_=Bc2NativeCyclePhase::Releasing;held_.reset();tail_={};readyMask_=0;return true;
}
void Bc2NativeCycleService::Missing(std::uint32_t firing,std::uint64_t invocation)noexcept {
    if(Active(phase_)&&std::find(native_.firing.begin(),native_.firing.end(),firing)!=native_.firing.end()){
        lastEvent_=invocation;Cancel(Bc2NativeCycleFailure::Owner);
    }
}
void Bc2NativeCycleService::Cancel(Bc2NativeCycleFailure failure)noexcept {
    // Cleanup repeats cancellation after the first rejected native event. Keep
    // that first cause and invocation instead of reporting the later teardown.
    if(phase_==Bc2NativeCyclePhase::Cancelled)return;
    if(recovery_.Active())recovery_.LoseGuard();
    suspendedAt_=suspendedUntil_=0;
    // Cancellation is also a barrier for an Update evaluated before an owner
    // disappeared. Its later completion cannot create a cycle in a new owner.
    openUpdate_={};contexts_={};heldAt_={};restoredAt_={};restoredPrevious_={};held_.reset();issued_={};
    if(phase_==Bc2NativeCyclePhase::Watching||phase_==Bc2NativeCyclePhase::Complete){
        // Startup/vehicle publication loss has no known manual debt. Exact
        // completed outcomes stay retained until acknowledgement, even here.
        control_={};native_={};config_={};
        return;
    }
    phase_=Bc2NativeCyclePhase::Cancelled;failure_=failure;failureInvocation_=lastEvent_;held_.reset();
}
bool Bc2NativeCycleService::AcknowledgeReady(const WeaponCycleReady& ready)noexcept {
    if(!ready_||ready_->release!=ready.release||ready_->sequence!=ready.sequence||ready_->observedNs!=ready.observedNs||
       ready_->deadlineNs!=ready.deadlineNs||ready_->nativeReady!=ready.nativeReady||ready_->unchangedAmmunition!=ready.unchangedAmmunition)return false;
    ready_.reset();return true;
}
Bc2NativeCycleView Bc2NativeCycleService::View(std::int64_t now)const noexcept {
    Bc2NativeCycleView out{phase_,failure_,native_,{},{},Active(phase_)||ready_.has_value(),loaded_,reserve_,capacity_,cycle_,shot_};
    if(held_&&weapon_cycle_detail::Lease(*held_,now))out.held=held_;
    else if(held_&&held_->observedNs>now&&phase_==Bc2NativeCyclePhase::Held&&!suspendedAt_){
        // A native callback may publish between Gather's processing timestamp
        // and this view read. Keep an already-issued SAME-cycle live lease;
        // a newer future observation is not loss of the existing authority.
        // Every returned timestamp/deadline remains its original value.
        for(const auto& prior:issued_)if(weapon_cycle_detail::Same(prior,*held_)&&weapon_cycle_detail::Lease(prior,now)&&
            (!out.held||prior.sequence>out.held->sequence))out.held=prior;
    }
    if(selection_){out.selectedMode=mode_;out.selectedOwner=selection_->reserve.identity.owner;}
    out.ready=ready_; // Retained exact outcome; never restamp or use as a live lease.
    out.failureInvocation=failureInvocation_;out.heldSuspended=suspendedAt_!=0;
    out.heldSuspensions=heldSuspensions_;out.heldResumptions=heldResumptions_;
    return out;
}
}
