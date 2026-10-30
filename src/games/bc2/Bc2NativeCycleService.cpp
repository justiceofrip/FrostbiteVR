#include "Bc2NativeCycleService.h"
#include "Bc2NativeCycleEvidence.h"
#include "Bc2M95StockShotConfig.h"
#include <algorithm>
#include <limits>

namespace fvr::bc2 {
using namespace interaction;
namespace {
const Bc2CycleNativeProfile* Profile(Bc2NativeCycleMode mode,const ReloadObservedConfig& config)noexcept {
    const auto* profile=ResolveNativeCycleProfile(config);
    return profile&&((mode==Bc2NativeCycleMode::Bolt&&profile->family==Bc2NativeCycleFamily::Bolt)||
        (mode==Bc2NativeCycleMode::Pump&&profile->family==Bc2NativeCycleFamily::Pump))?profile:nullptr;
}
NativeCycleHeldBoundary Boundary(Bc2NativeCycleMode mode,const ReloadObservedConfig& config,bool empty)noexcept {
    const auto* profile=Profile(mode,config);return profile?profile->Held(empty):NativeCycleHeldBoundary{};
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
bool Bc2NativeCycleService::EnableConcurrentBolt()noexcept {
    if(!recoveryEnabled_||control_.input.sequence||cycle_||phase_!=Bc2NativeCyclePhase::Watching)return false;
    concurrentBoltEnabled_=true;return true;
}
bool Bc2NativeCycleService::BeginRecovery(std::int64_t now)noexcept {
    if(Recovering()){
        // A submitted stroke keeps its original completion route. Before
        // submission, an interruption revokes advancing hand authority and
        // requires the existing neutral/regrip recovery after native idle.
        if(concurrentCycle_&&release_)return !recovery_.Uncertain();
        recovery_.Interrupt(now);
        if(concurrentCycle_)issued_={}; // Advancing leases are not original held-release denial evidence.
        concurrentCycle_=false;phase_=Bc2NativeCyclePhase::Converging;
        held_.reset();return !recovery_.Uncertain();
    }
    if(!recoveryEnabled_||phase_!=Bc2NativeCyclePhase::Held||release_||ready_||firedMask_!=7||heldMask_!=7||
       now<began_||guardEpoch_==UINT64_MAX||!Profile(mode_,config_))return false;
    // Conversion must occur while the native callbacks still prove established
    // holds. A long unobserved gap cannot acquire a continuous guard retroactively.
    for(unsigned b=0;b<3;++b){
        if(!heldAt_[b]||now<heldAt_[b]||now-heldAt_[b]>100000000)return false;
        if(openUpdate_[b]&&!openHold_[b])return false;
    }
    Bc2NativeCycleRecoveryState candidate;
    if(!candidate.Begin({native_,config_},{control_.input.owner,control_.item,control_.mechanism,
        cycle_,shot_,guardEpoch_+1,loaded_,reserve_,capacity_},began_,
        Profile(mode_,config_)->Convergence(emptyCycle_)))return false;
    for(unsigned b=0;b<3;++b)if(openUpdate_[b]&&!candidate.BeginHold(b,openUpdate_[b],before_[b].nowNs,openDeadline_[b]))return false;
    recovery_=std::move(candidate);++guardEpoch_;phase_=Bc2NativeCyclePhase::Converging;
    // Retain issued held-lease identity only to identify an overtaken release.
    // Converging cannot call Release or publish held_, so this grants no pose,
    // motion or completion authority. Cancel/new cycle/rebase still clear it.
    held_.reset();suspendedAt_=suspendedUntil_=0;return true;
}
bool Bc2NativeCycleService::BeginInputRecovery(const ReloadHoldInput& in,std::uint64_t invocation)noexcept {
    // Safe() also rejects bad timing and unsupported context flags. Only the
    // action bits covered by the existing invocation-local guard can convert;
    // neither an expired native observation nor changed ammo is recoverable here.
    const auto& c=in.context;
    if(phase_!=Bc2NativeCyclePhase::Held||!recoveryEnabled_||!invocation||
       in.branch>=3||openUpdate_[in.branch]||invocation<=lastInvocation_[in.branch]||
       !Current(in,true)||native_!=in.identity||config_!=in.config||
       in.contextObservedNs<=0||in.contextObservedNs>in.nowNs||in.nowNs-in.contextObservedNs>ContextFreshNs||
       !ValidManualReloadDelta(c.deltaSeconds)||!std::isfinite(c.reloadTimeMultiplier)||
       c.reloadTimeMultiplier<=0||c.reloadTimeMultiplier>4||c.flags24Through28[2]||
       (c.inputFlags&~7u)||c.fireRequested!=bool(c.inputFlags&1u)||
       c.orderRequested!=bool(c.inputFlags&2u)||c.reloadRequested!=bool(c.inputFlags&4u)||
       (!c.inputFlags&&!c.flags24Through28[3]&&!c.flags24Through28[4]))return false;
    for(unsigned b=0;b<3;++b)
        if(!Held(in.branches[b],b<2&&restoredAt_[b]>=began_&&restoredAt_[b]<=in.nowNs,
            b<2?restoredPrevious_[b]:0u,Boundary(mode_,config_,emptyCycle_))||
           in.branches[b].loaded!=loaded_||in.branches[b].reserve!=reserve_||in.capacities[b]!=capacity_)return false;
    return BeginRecovery(in.nowNs);
}
bool Bc2NativeCycleService::ReleaseSupersededByRecovery(const Bc2NativeCycleControl& c)const noexcept {
    if(phase_!=Bc2NativeCyclePhase::Converging||!recovery_.Active()||recovery_.Uncertain()||
       concurrentCycle_||release_||ready_||!c.release||!Mapping(c)||
       c.nativeOwner!=control_.nativeOwner||c.input.owner!=control_.input.owner||
       c.item!=control_.item||c.mechanism!=control_.mechanism||
       c.input.sequence<control_.input.sequence||c.input.observedNs<control_.input.observedNs||c.input.nowNs<control_.input.nowNs||
       (c.input.sequence==control_.input.sequence&&(c.input.observedNs!=control_.input.observedNs||c.input.deadlineNs!=control_.input.deadlineNs)))return false;
    const auto& r=*c.release;const auto& lease=r.cycle;
    return r.request&&r.inputSequence&&r.inputSequence<=c.input.sequence&&r.observedNs<=c.input.nowNs&&
        weapon_cycle_detail::Window(r.observedNs,r.deadlineNs,r.observedNs)&&r.deadlineNs<=lease.deadlineNs&&
        lease.held&&!lease.advancing&&lease.owner==control_.input.owner&&lease.item==control_.item&&
        lease.mechanism==control_.mechanism&&lease.cycle==cycle_&&lease.shot==shot_&&
        std::find(issued_.begin(),issued_.end(),lease)!=issued_.end();
}
NativeCycleRecoveryDecision Bc2NativeCycleService::EvaluateRecovery(const NativeCycleRecoveryScope& scope,
    const ReloadFlowEventInput& entry,int capacity,std::int64_t deadline)noexcept {
    lastEvent_=entry.nativeInvocation;
    if(entry.boundary.branch<3)paidOrdinaryIdle_[entry.boundary.branch]={};
    if(!Recovering()||recovery_.ReloadCommand())return {};
    if(entry.boundary.branch<3&&paidRecoveryDrain_[entry.boundary.branch]){Cancel(Bc2NativeCycleFailure::Owner);return {};}
    const auto result=recovery_.Evaluate(scope,entry,capacity,deadline);
    if(!result)Cancel(Bc2NativeCycleFailure::Owner);return result;
}
bool Bc2NativeCycleService::DrainingHeldInvocation(const Bc2NativeCycleDecision& d,std::uint32_t firing,std::int64_t now)const noexcept {
    return Recovering()&&d.tracked&&d.hold&&d.branch<3&&d.invocation&&d.cycle==cycle_&&
        firing==native_.firing[d.branch]&&openUpdate_[d.branch]==d.invocation&&openHold_[d.branch]&&
        d.deadlineNs==openDeadline_[d.branch]&&now>=before_[d.branch].nowNs&&now<d.deadlineNs;
}
bool Bc2NativeCycleService::DrainingPaidOrdinaryInvocation(const Bc2NativeCycleDecision& d,std::uint32_t firing,std::int64_t now)const noexcept {
    if(!Recovering()||!recovery_.Acknowledged()||recovery_.Uncertain()||!d.tracked||d.hold||d.branch>=3||
       !d.invocation||d.cycle!=cycle_||paidRecoveryDrain_[d.branch]!=d.invocation||
       openUpdate_[d.branch]!=d.invocation||openHold_[d.branch]||firing!=native_.firing[d.branch])return false;
    const auto& in=before_[d.branch];
    return in.identity==native_&&in.config==config_&&in.capacities[d.branch]==capacity_&&
        in.nowNs<=paidReplayBegan_&&d.deadlineNs==in.leaseDeadlineNs&&now>=in.nowNs&&now<d.deadlineNs;
}
void Bc2NativeCycleService::FinishRecovery(const NativeCycleRecoveryDecision& d,const ReloadFlowRecord& r,
    const NativeCycleInputGuard& guard,unsigned steps,unsigned rejected)noexcept {
    lastEvent_=r.id;
    if(!Recovering())return;
    if(!recovery_.Finish(d,r,guard,steps,rejected))Cancel(Bc2NativeCycleFailure::Completion);
    else {
        ++recoveryUpdates_;recoverySteps_+=steps;
        if(recovery_.Acknowledged()){
            if(recovery_.PaidConverged(r.exit.nowNs)&&
               std::none_of(paidRecoveryDrain_.begin(),paidRecoveryDrain_.end(),[](auto id){return id!=0;})&&
               std::none_of(openUpdate_.begin(),openUpdate_.end(),[](auto id){return id!=0;}))
                phase_=Bc2NativeCyclePhase::Complete;
        }else if(concurrentCycle_){
            if(recovery_.NativeReady()){ready_=recovery_.NativeReady();held_.reset();}
            else if(!release_&&sequence_!=UINT64_MAX){
                if(auto next=recovery_.AdvancingAuthority(control_.input,sequence_+1,r.exit.nowNs)){
                    ++sequence_;held_=next;issued_[issueAt_]=*next;issueAt_=(issueAt_+1)%issued_.size();
                }
            }
        }
    }
}
bool Bc2NativeCycleService::SubmitRecovery(const WeaponCycleIdleDebtRelease& release,const HandInteractionSample& input)noexcept {
    const bool accepted=Recovering()&&input.owner==control_.input.owner&&input.sequence==control_.input.sequence&&
        input.observedNs==control_.input.observedNs&&input.deadlineNs==control_.input.deadlineNs&&
        input.nowNs>=control_.input.nowNs&&recovery_.Submit(release,input);
    if(accepted&&(!lastRecoveryRelease_||*lastRecoveryRelease_!=release)){lastRecoveryRelease_=release;++recoveryReleases_;}
    return accepted;
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
bool Bc2NativeCycleService::ReserveValid(const Bc2AmmoReserveLease& r,std::int64_t now)const noexcept {
    const auto& n=r.identity;const auto& o=n.owner;
    if(!r.verified||!r.allThreeIdle||!r.sequence||r.sequence<=selectionSequence_||now<selectionTime_||
       !weapon_cycle_detail::Window(r.observedNs,r.deadlineNs,now)||r.deadlineNs-r.observedNs>200000000||
       r.loaded<0||r.reserve<0||r.reserve>1000000||r.capacity<=0||r.capacity>1000000||r.loaded>r.capacity||
       o.player<0x10000||o.soldier<0x10000||o.weak<0x10000||o.weapon<0x10000||
       !o.actorGeneration||!o.equipGeneration||!o.space||n.serverPlayer<0x10000||n.serverSoldier<0x10000||n.serverItem<0x10000)return false;
    for(unsigned a=0;a<3;++a){if(n.firing[a]<0x10000)return false;
        for(unsigned b=0;b<a;++b)if(n.firing[a]==n.firing[b])return false;}
    return true;
}
bool Bc2NativeCycleService::SelectionValid(const Bc2NativeCycleSelection& source)const noexcept {
    const auto* profile=Profile(source.mode,source.config);
    return profile&&(source.mode!=Bc2NativeCycleMode::Bolt||source.reserve.capacity==profile->config->values.baseCapacity)&&
        ReserveValid(source.reserve,source.nowNs);
}
void Bc2NativeCycleService::ApplySelection(const Bc2NativeCycleSelection& source)noexcept {
    const auto& r=source.reserve;
    mode_=source.mode;selectionRequired_=true;selection_=source;
    selectionSequence_=r.sequence;selectionTime_=source.nowNs;
    native_=r.identity;config_=source.config;loaded_=r.loaded;reserve_=r.reserve;capacity_=r.capacity;
}
bool Bc2NativeCycleService::Select(const Bc2NativeCycleSelection& source)noexcept {
    if(!SelectionValid(source))return false;
    // Yield's exact ack/drain policy is shared with resource ownership. A stock
    // idle read cannot make this succeed while any old action remains owed.
    if(!YieldForReload())return false;
    ApplySelection(source);
    return true;
}
std::optional<Bc2NativeCycleActorRetirement> Bc2NativeCycleService::SelectReplacementActor(
    const Bc2NativeCycleSelection& source,const Bc2NativeCycleActorDrain& drain)noexcept {
    if(!SelectionValid(source))return {};
    const auto retired=RetireForReplacementActor(source.reserve,source.nowNs,drain);
    if(retired)ApplySelection(source);
    return retired;
}
std::optional<Bc2NativeCycleActorRetirement> Bc2NativeCycleService::RetireForReplacementActor(
    const Bc2AmmoReserveLease& reserve,std::int64_t now,const Bc2NativeCycleActorDrain& drain)noexcept {
    const auto& replacement=reserve.identity;const auto& previous=native_.owner;
    if(!ReserveValid(reserve,now)||!drain.callbacksDrained||!drain.callbackRevision||
       drain.expected!=native_||drain.cycle!=cycle_||drain.shot!=shot_||
       !NativeCycleClientActorReplaced(previous,replacement.owner)||native_.serverSoldier<0x10000||
       replacement.serverSoldier==native_.serverSoldier||
       !weapon_cycle_detail::Window(drain.observedNs,drain.deadlineNs,now)||
       drain.deadlineNs-drain.observedNs>ContextFreshNs||
       drain.observedNs<reserve.observedNs||drain.deadlineNs>reserve.deadlineNs||
       (actorRetirement_&&(actorRetirement_->event==UINT64_MAX||
        drain.callbackRevision<=actorRetirement_->callbackRevision||drain.observedNs<actorRetirement_->observedNs)))return {};
    Bc2NativeCycleActorRetirement retired;
    retired.event=actorRetirement_?actorRetirement_->event+1:1;retired.callbackRevision=drain.callbackRevision;
    retired.observedNs=drain.observedNs;retired.mode=mode_;retired.outcome=View(now);
    retired.replacement=replacement;retired.release=release_;
    if(recovery_.Active()){
        auto captured=recovery_;retired.recovery=captured.View(now);
        retired.recoveryUncertain=recovery_.Uncertain();
        if(lastRecoveryRelease_&&lastRecoveryRelease_->cycle.cycle==cycle_&&lastRecoveryRelease_->cycle.shot==shot_)
            retired.recoveryRelease=lastRecoveryRelease_;
    }
    // Only lifetime-bound live state is retired. Invocation/lease/cycle/shot,
    // selection and recovery counter high-water marks survive, so no old packet
    // or callback can become a new owner's operation. No native memory is touched.
    control_={};phase_=Bc2NativeCyclePhase::Watching;failure_=Bc2NativeCycleFailure::None;
    openUpdate_={};contexts_={};heldAt_={};finishedAt_={};restoredAt_={};restoredPrevious_={};
    forwardReadyAt_={};preRestoredAt_={};preRewindAt_={};cycleState_={};tail_={};before_={};
    issued_={};issueAt_=0;firedMask_=heldMask_=readyMask_=0;failureInvocation_=0;
    began_=deadline_=suspendedAt_=suspendedUntil_=reloadPauseSpent_=0;emptyCycle_=false;
    recovery_={};concurrentCycle_=false;lastRecoveryRelease_.reset();openHold_={};openDeadline_={};
    held_.reset();releaseSubmittedNs_=0;release_.reset();ready_.reset();ClearAcknowledgedReady();
    native_={};config_={};loaded_=reserve_=capacity_=0;
    selectionRequired_=true;selection_.reset();selectionSequence_=reserve.sequence;selectionTime_=now;
    actorRetirement_=retired;return retired;
}
bool Bc2NativeCycleService::MappingScope(const Bc2NativeCycleControl& c)const noexcept {
    const auto& n=c.nativeOwner;const auto& h=c.input.owner;
    return c.permitted&&n.player>=0x10000&&n.soldier>=0x10000&&n.weak>=0x10000&&n.weapon>=0x10000&&
        weapon_cycle_detail::Owner(h)&&n.actorGeneration==h.actorGeneration&&n.equipGeneration&&n.space==h.space&&
        h.actor==((std::uint64_t(n.weak)<<32)|n.soldier)&&c.item.id==n.weapon&&c.item.generation==h.equipGeneration&&
        weapon_cycle_detail::Key(c.mechanism)&&c.input.sequence&&
        weapon_cycle_detail::Window(c.input.observedNs,c.input.deadlineNs,c.input.nowNs);
}
bool Bc2NativeCycleService::Mapping(const Bc2NativeCycleControl& c)const noexcept {
    return MappingScope(c)&&c.input.focused&&c.input.tracked[0]&&c.input.tracked[1];
}
bool Bc2NativeCycleService::Control(const Bc2NativeCycleControl& c)noexcept {
    if(phase_==Bc2NativeCyclePhase::Cancelled)return false;
    if(selectionRequired_&&(!selection_||c.nativeOwner!=selection_->reserve.identity.owner))return false;
    lastEvent_=0; // Controller admission is not a native invocation.
    if(suspendedAt_&&c.input.nowNs>=suspendedUntil_){Cancel(Bc2NativeCycleFailure::Expired);return false;}
    // Runtime owner checks can overtake a packet's original deadline. Reject
    // its hand/release authority without turning that observation gap into
    // permanent native cancellation. Fresh own Updates still have to prove
    // SuspendExpiredController; its fixed bound and recovery remain unchanged.
    // Do not copy or restamp this packet, including a submitted old release.
    if(c.input.observedNs>0&&c.input.deadlineNs>c.input.observedNs&&c.input.nowNs>=c.input.deadlineNs)return false;
    // A fresh same-owner tracking/focus-loss packet revokes physical control,
    // not the known native debt. It cannot renew control or submit a stroke.
    // Preserve an already submitted outcome; otherwise require the existing
    // own-Update conversion/drain proof before guarded recovery can proceed.
    if(!c.release&&MappingScope(c)&&(!c.input.focused||!c.input.tracked[0]||!c.input.tracked[1])&&
       c.nativeOwner==control_.nativeOwner&&c.input.owner==control_.input.owner&&
       c.item==control_.item&&c.mechanism==control_.mechanism&&
       c.input.sequence>control_.input.sequence&&c.input.observedNs>control_.input.observedNs&&
       c.input.nowNs>=control_.input.nowNs&&
       (SuspendCompletion()||BeginRecovery(c.input.nowNs)||SuspendHeld(c.input.nowNs)))return false;
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
    if(!Active(phase_)&&c.nativeOwner!=control_.nativeOwner){native_={};config_={};firedMask_=heldMask_=readyMask_=0;ClearAcknowledgedReady();phase_=Bc2NativeCyclePhase::Watching;}
    // Native-only suspension never creates a controller edge or a renewed hand
    // lease. A fresh tracked neutral packet from the SAME owner can end it.
    const unsigned mechanismHand=mode_==Bc2NativeCycleMode::M95?1u:0u;
    if(suspendedAt_&&c.input.observedNs>suspendedAt_&&c.input.sequence>control_.input.sequence&&
       c.input.released[mechanismHand]&&!c.input.released[1u-mechanismHand]&&!c.release){suspendedAt_=suspendedUntil_=0;++heldResumptions_;}
    const auto previousInput=control_.input.sequence;
    control_=c;
    // A short publication gap can suspend an established native hold even
    // though the player's next tracked packet still has squeeze held. Once
    // all native branches have refreshed their exact holds, move to guarded
    // recovery instead of waiting three seconds and permanently cancelling.
    // BeginRecovery verifies those own-Update receipts; no old motion, grab
    // lease or completed stroke is restored. Fresh neutral + a full gesture
    // remain required by the recovery consumer.
    if(suspendedAt_&&!c.release&&c.input.observedNs>suspendedAt_&&c.input.sequence>previousInput)
        BeginRecovery(c.input.nowNs);
    if(concurrentCycle_&&phase_==Bc2NativeCyclePhase::Advancing)return !c.release||Release(*c.release,c.input.nowNs);
    if(Recovering())return !c.release&&recovery_.Control(c.input);
    return !c.release||Release(*c.release,c.input.nowNs);
}
bool Bc2NativeCycleService::RebaseTracking(const Bc2NativeCycleControl& c,const Bc2NativeCycleActorDrain& drain)noexcept {
    auto previousNative=c.nativeOwner;previousNative.space=control_.nativeOwner.space;
    auto previousHand=c.input.owner;previousHand.space=control_.input.owner.space;
    if(mode_!=Bc2NativeCycleMode::Spas||!Mapping(c)||c.release||release_||ready_||
       previousNative!=control_.nativeOwner||previousHand!=control_.input.owner||
       c.nativeOwner.space<=control_.nativeOwner.space||c.item!=control_.item||c.mechanism!=control_.mechanism||
       c.input.sequence<=control_.input.sequence||c.input.observedNs<=control_.input.observedNs||
       !drain.callbacksDrained||!drain.callbackRevision||drain.expected!=native_||
       drain.cycle!=cycle_||drain.shot!=shot_||drain.observedNs<c.input.observedNs||
       !weapon_cycle_detail::Window(drain.observedNs,drain.deadlineNs,c.input.nowNs)||
       drain.deadlineNs-drain.observedNs>ContextFreshNs||
       std::any_of(openUpdate_.begin(),openUpdate_.end(),[](auto id){return id!=0;})||
       (trackingRebase_&&drain.callbackRevision<=trackingRebase_->callbackRevision))return false;
    // Work on a candidate so a busy callback, pending receipt or failed
    // conversion changes nothing. Recentring cannot forgive an owed stroke.
    auto next=*this;
    if(!next.Recovering()&&!next.BeginRecovery(c.input.nowNs))return false;
    if(!next.recovery_.RebaseTracking(c.nativeOwner,c.input))return false;
    next.trackingRebase_=Bc2NativeCycleTrackingRebase{control_.nativeOwner,c.nativeOwner,
        control_.input.owner,c.input.owner,c.item,c.mechanism,cycle_,shot_,drain.callbackRevision,c.input.nowNs};
    next.native_.owner=c.nativeOwner;next.control_=c;next.held_.reset();next.issued_={};
    if(next.selection_)next.selection_->reserve.identity.owner=c.nativeOwner;
    *this=std::move(next);return true;
}
bool Bc2NativeCycleService::YieldForReload()noexcept {
    if(Active(phase_)||ready_||std::any_of(openUpdate_.begin(),openUpdate_.end(),[](auto id){return id!=0;}))return false;
    Cancel(Bc2NativeCycleFailure::None);
    phase_=Bc2NativeCyclePhase::Watching;firedMask_=heldMask_=readyMask_=0;
    release_.reset();releaseSubmittedNs_=0;forwardReadyAt_={};selection_.reset();recovery_={};concurrentCycle_=false;return true;
}
bool Bc2NativeCycleService::PrepareReload(const NativeCycleRecoveryScope& scope,const WeaponCycleReloadPause& pause,
    const AmmoResourceContext& resource,const WeaponCycleReloadDrain& drain,std::int64_t now)noexcept {
    if(mode_!=Bc2NativeCycleMode::Bolt||scope!=NativeCycleRecoveryScope{native_,config_}||release_||ready_||
       recovery_.ReloadPause()||pause.owner!=control_.input.owner||pause.item!=control_.item||pause.mechanism!=control_.mechanism||
       pause.cycle!=cycle_||pause.shot!=shot_||pause.inputSequence>control_.input.sequence||
       pause.observedNs>control_.input.observedNs||pause.deadlineNs>control_.input.deadlineNs||
       now>=deadline_||reloadPauseSpent_>=WeaponCycle::MaximumReloadPauseNs||
       pause.pauseDeadlineNs-pause.pausedNs>WeaponCycle::MaximumReloadPauseNs-reloadPauseSpent_||
       std::any_of(openUpdate_.begin(),openUpdate_.end(),[](auto id){return id!=0;}))return false;
    auto next=*this;
    if(!next.Recovering()&&!next.BeginRecovery(now))return false;
    if(!next.recovery_.PrepareReload(scope,pause,resource,drain,now))return false;
    next.concurrentCycle_=false;next.phase_=Bc2NativeCyclePhase::Converging;
    next.held_.reset();next.issued_={};next.suspendedAt_=next.suspendedUntil_=0;
    *this=std::move(next);return true;
}
std::optional<WeaponCycleReloadPermit> Bc2NativeCycleService::ReloadPermit(const NativeCycleRecoveryScope& scope,std::int64_t now)noexcept {
    return Recovering()?recovery_.ReloadPermit(scope,now):std::nullopt;
}
bool Bc2NativeCycleService::BeginReloadCommand(const NativeCycleRecoveryScope& scope,const WeaponCycleReloadPermit& permit,
    const AmmunitionCommand& command,const WeaponCycleReloadDrain& drain,std::int64_t now)noexcept {
    return Recovering()&&!concurrentCycle_&&
        std::none_of(openUpdate_.begin(),openUpdate_.end(),[](auto id){return id!=0;})&&
        recovery_.BeginReloadCommand(scope,permit,command,drain,now);
}
bool Bc2NativeCycleService::ApplyReloadReceipt(const NativeCycleRecoveryScope& scope,const WeaponCycleReloadAmmoRebase& rebase,std::int64_t now)noexcept {
    if(!Recovering()||concurrentCycle_||rebase.guardEpoch<=guardEpoch_||
       std::any_of(openUpdate_.begin(),openUpdate_.end(),[](auto id){return id!=0;}))return false;
    for(unsigned b=0;b<3;++b)if(rebase.ownUpdates[b].invocation<=lastInvocation_[b])return false;
    if(!recovery_.ApplyReloadReceipt(scope,rebase,now))return false;
    const auto& debt=recovery_.Debt();loaded_=debt.loaded;reserve_=debt.reserve;capacity_=debt.capacity;guardEpoch_=debt.guardEpoch;
    for(unsigned b=0;b<3;++b)lastInvocation_[b]=rebase.ownUpdates[b].invocation;
    held_.reset();issued_={};return true;
}
bool Bc2NativeCycleService::ResumeReload(const NativeCycleRecoveryScope& scope,const WeaponCycleIdleDebtLease& authority,std::int64_t now)noexcept {
    if(!Recovering()||concurrentCycle_||!recovery_.ReloadPause()||
       std::any_of(openUpdate_.begin(),openUpdate_.end(),[](auto id){return id!=0;}))return false;
    const auto& pause=*recovery_.ReloadPause();
    if(now<pause.pausedNs||now>=pause.pauseDeadlineNs)return false;
    const auto elapsed=now-pause.pausedNs;
    if(elapsed>WeaponCycle::MaximumReloadPauseNs-reloadPauseSpent_||deadline_>INT64_MAX-elapsed||
       !recovery_.ResumeReload(scope,authority,now))return false;
    reloadPauseSpent_+=elapsed;deadline_+=elapsed;return true;
}
bool Bc2NativeCycleService::SuspendCompletion()noexcept {
    if((phase_!=Bc2NativeCyclePhase::Releasing&&phase_!=Bc2NativeCyclePhase::Advancing)||!release_)return false;
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
    const bool pending=phase_==Bc2NativeCyclePhase::ShotObserved||phase_==Bc2NativeCyclePhase::Arming||phase_==Bc2NativeCyclePhase::Held;
    if(!suspendedAt_||!pending||release_||ready_||!firedMask_||
       now<suspendedAt_||now>=suspendedUntil_||now>=deadline_)return {};
    return Bc2NativeCycleCompletionWatch{native_,config_,cycle_,suspendedAt_,suspendedUntil_,phase_==Bc2NativeCyclePhase::Held};
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
        (phase_==Bc2NativeCyclePhase::Held||phase_==Bc2NativeCyclePhase::Releasing||
         (phase_==Bc2NativeCyclePhase::Complete&&native_.owner.player&&control_.nativeOwner==native_.owner))?previous:0;
}
std::optional<Bc2NativeCycleCompletionWatch> Bc2NativeCycleService::CompletionWatch(std::int64_t now)const noexcept {
    if(phase_!=Bc2NativeCyclePhase::Releasing||!release_||now<release_->observedNs||
       now>=deadline_||now-release_->observedNs>=CompletionNs())return {};
    return Bc2NativeCycleCompletionWatch{native_,config_,cycle_,release_->observedNs,
        release_->observedNs+std::min(CompletionNs(),deadline_-release_->observedNs)};
}
bool Bc2NativeCycleService::SuspendExpiredController(const ReloadHoldInput& in)noexcept {
    // Gather may publish a fresh native owner before it forwards the matching
    // controller packet. A stalled frame must not erase an established owed
    // stroke. Fresh exact native cohort evidence can retain its hold, but never
    // renew the old hand lease or submit motion from an expired input packet.
    const bool pending=phase_==Bc2NativeCyclePhase::ShotObserved||phase_==Bc2NativeCyclePhase::Arming||phase_==Bc2NativeCyclePhase::Held;
    if(!recoveryEnabled_||suspendedAt_||!pending||release_||ready_||!cycle_||!shot_||!firedMask_||
       in.nowNs<control_.input.deadlineNs||
       control_.input.deadlineNs>INT64_MAX-MaximumHoldSuspensionNs||
       in.nowNs>=control_.input.deadlineNs+MaximumHoldSuspensionNs||in.nowNs>=deadline_||
       !Current(in,true)||native_!=in.identity||config_!=in.config||
       in.contextObservedNs<=0||in.contextObservedNs>in.nowNs||in.nowNs-in.contextObservedNs>ContextFreshNs)return false;
    // Before all own-shot receipts arrive, prediction copies may legitimately
    // still show the pre-shot count. Observe their original Updates; only the
    // existing shot/cohort checks can establish a hold. A held cycle already
    // owes the exact post-shot count and cannot use that admission exception.
    if(phase_==Bc2NativeCyclePhase::Held){
      if(firedMask_!=7||heldMask_!=7||!held_||!Safe(in.context))return false;
      for(unsigned b=0;b<3;++b)
        if(!Held(in.branches[b],b<2&&restoredAt_[b]>=began_&&restoredAt_[b]<=in.nowNs,
            b<2?restoredPrevious_[b]:0u,Boundary(mode_,config_,emptyCycle_))||
           in.branches[b].loaded!=loaded_||in.branches[b].reserve!=reserve_||in.capacities[b]!=capacity_)return false;
    }
    suspendedAt_=in.nowNs;
    // The interruption bound starts at the original input expiry, not at each
    // later native callback. Fresh neutral input is still required to resume.
    suspendedUntil_=std::min(deadline_,control_.input.deadlineNs+MaximumHoldSuspensionNs);
    held_.reset();issued_={};++heldSuspensions_;return true;
}
bool Bc2NativeCycleService::Current(const ReloadHoldInput& in,bool nativeOnly)const noexcept {
    const auto* profile=Profile(mode_,in.config);
    if(selectionRequired_&&(!selection_||in.identity!=selection_->reserve.identity||in.config!=selection_->config))return false;
    const bool observing=ObservationWatch(in.nowNs).has_value();
    if(!in.verified||in.branch>=3||(!observing&&(!Mapping(control_)||
       (!nativeOnly&&!weapon_cycle_detail::Window(control_.input.observedNs,control_.input.deadlineNs,in.nowNs))))||
       in.identity.owner!=control_.nativeOwner||
       in.leaseDeadlineNs<=in.nowNs||in.leaseDeadlineNs-in.nowNs>200000000||!profile)return false;
    if(in.identity.serverPlayer<0x10000||in.identity.serverSoldier<0x10000||in.identity.serverItem<0x10000)return false;
    for(unsigned branch=0;branch<3;++branch){
        if(in.identity.firing[branch]<0x10000||in.branches[branch].address!=in.identity.firing[branch]||
           in.branches[branch].wrapperOffset!=(branch==0?0x3cu:branch==1?0x40u:0x10u)||
           in.capacities[branch]<=0||in.capacities[branch]>1000000||
           (mode_==Bc2NativeCycleMode::Bolt&&in.capacities[branch]!=profile->config->values.baseCapacity))return false;
        for(unsigned other=0;other<branch;++other)if(in.identity.firing[other]==in.identity.firing[branch])return false;
    }
    return true;
}
Bc2NativeCycleDecision Bc2NativeCycleService::Evaluate(const ReloadHoldInput& in,std::uint64_t invocation)noexcept {
    Bc2NativeCycleDecision out;
    if(in.branch<3)paidOrdinaryIdle_[in.branch]={};
    if(phase_==Bc2NativeCyclePhase::Cancelled||Recovering())return out;
    lastEvent_=invocation;
    if(suspendedAt_&&in.nowNs>=suspendedUntil_){Cancel(Bc2NativeCycleFailure::Expired);return out;}
    // Same-branch reentry must not overwrite the pending own-Update receipt.
    // Cancel also clears a pre-shot evaluation while Watching.
    if(in.branch<3&&openUpdate_[in.branch]){Cancel(Bc2NativeCycleFailure::Owner);return out;}
    if(invocation&&in.branch<3&&invocation>lastInvocation_[in.branch])SuspendExpiredController(in);
    if(!Current(in)||!invocation||invocation<=lastInvocation_[in.branch<3?in.branch:0]){
        if(Active(phase_))Cancel(Bc2NativeCycleFailure::Owner);return out;
    }
    if(native_.owner.player&&(native_!=in.identity||config_!=in.config)){
        if(Active(phase_)){Cancel(Bc2NativeCycleFailure::Owner);return out;}
        firedMask_=heldMask_=readyMask_=0;ClearAcknowledgedReady();
    }
    native_=in.identity;config_=in.config;lastInvocation_[in.branch]=invocation;openUpdate_[in.branch]=invocation;openHold_[in.branch]=false;before_[in.branch]=in;
    out={invocation,cycle_,in.branch,true,false,in.leaseDeadlineNs};
    if(phase_==Bc2NativeCyclePhase::Watching||phase_==Bc2NativeCyclePhase::Complete)return out;
    if(in.nowNs>=deadline_){Cancel(Bc2NativeCycleFailure::Expired);return out;}
    if(phase_==Bc2NativeCyclePhase::Releasing&&acknowledgedReady_&&!CompletionWatch(in.nowNs)){
        Cancel(Bc2NativeCycleFailure::Expired);return out;
    }
    if(phase_==Bc2NativeCyclePhase::Releasing)return out;
    const bool contextSafe=Safe(in.context);
    const bool contextFresh=in.contextObservedNs>0&&in.contextObservedNs<=in.nowNs&&in.nowNs-in.contextObservedNs<=ContextFreshNs;
    if(!contextSafe||!contextFresh){
        contexts_[in.branch]=0;
        if(phase_==Bc2NativeCyclePhase::Held){
            if(!firstInputRejection_)firstInputRejection_=Bc2NativeCycleInputRejection{
                native_,in.context,invocation,cycle_,shot_,in.branch,in.contextObservedNs,in.nowNs,in.leaseDeadlineNs,
                contextSafe,contextFresh};
            Cancel(Bc2NativeCycleFailure::Input);
        }
        return out;
    }
    for(unsigned n=0;n<3;++n)if(!Held(in.branches[n],n<2&&restoredAt_[n]>=began_&&restoredAt_[n]<=in.nowNs,n<2?restoredPrevious_[n]:0u,Boundary(mode_,config_,emptyCycle_))||in.branches[n].loaded!=loaded_||in.branches[n].reserve!=reserve_||
        in.capacities[n]!=capacity_){
        if(phase_==Bc2NativeCyclePhase::Held)Cancel(Bc2NativeCycleFailure::State);
        return out;
    }
    contexts_[in.branch]=in.contextObservedNs;
    if(firedMask_!=7)return out;
    // Initial arming needs every own context. Once established, retain this
    // branch from its freshly read complete cohort and its own safe context.
    // Waiting for stale sibling contexts here lets the original timer advance
    // during a frame gap. Physical leases still require all fresh hold receipts.
    if(heldMask_!=7)for(auto observed:contexts_)if(observed<=0||observed>in.nowNs||in.nowNs-observed>ContextFreshNs)return out;
    if(!heldMask_)for(const auto& branch:in.branches)if(branch.phaseTimer<Boundary(mode_,config_,emptyCycle_).minArmTimer)return out;
    phase_=heldMask_==7?Bc2NativeCyclePhase::Held:Bc2NativeCyclePhase::Arming;
    out.hold=true;out.cycle=cycle_;
    out.deadlineNs=std::min({deadline_,in.leaseDeadlineNs,suspendedAt_?suspendedUntil_:control_.input.deadlineNs});
    openHold_[in.branch]=true;openDeadline_[in.branch]=out.deadlineNs;return out;
}
void Bc2NativeCycleService::Finish(const Bc2NativeCycleDecision& d,const ReloadFlowRecord& record)noexcept {
    if(!d.tracked||d.branch>=3||record.id!=d.invocation||openUpdate_[d.branch]!=d.invocation)return;
    openUpdate_[d.branch]=0;openHold_[d.branch]=false;paidOrdinaryIdle_[d.branch]={};
    if(phase_==Bc2NativeCyclePhase::Cancelled)return;
    lastEvent_=record.id;
    const auto& in=before_[d.branch];
    if(Recovering()){
        if(recovery_.Acknowledged()&&paidRecoveryDrain_[d.branch]==d.invocation){
            paidRecoveryDrain_[d.branch]=0;
            const auto idle=[&](const ReloadFlowBoundary& b){return b.current==2&&b.next==2&&b.timer==0&&
                b.loaded==loaded_&&b.reserve==reserve_;};
            const auto& h=record.exit.hold;
            if(d.hold||d.cycle!=cycle_||in.identity!=native_||in.config!=config_||
               in.capacities[d.branch]!=capacity_||in.nowNs>paidReplayBegan_||
               d.deadlineNs!=in.leaseDeadlineNs||record.exit.nowNs>=d.deadlineNs||
               !ValidateCycleUpdate(record,native_,d.branch)||!idle(record.entry.boundary)||!idle(*record.exit.boundary)||
               record.entry.boundary.flagsA8!=record.exit.boundary->flagsA8||record.exit.holdRequested||
               h.applied||h.restored||h.unexpectedNativeWrite||h.original||h.beforeRestore||
               record.exit.cycleGuard!=NativeCycleInputGuard{}||record.exit.cycleGuardEpoch||
               record.exit.cycleGuardSteps||record.exit.cycleGuardRejected||
               !recovery_.DrainPaidOrdinary(d.branch,d.invocation,record.exit.nowNs)){
                Cancel(Bc2NativeCycleFailure::Completion);return;
            }
            finishedAt_[d.branch]=record.exit.nowNs;return;
        }
        auto boundary=Boundary(mode_,config_,emptyCycle_);
        if(d.branch<2&&restoredAt_[d.branch]>=began_&&restoredAt_[d.branch]<=record.entry.nowNs&&
           record.entry.boundary.previous==restoredPrevious_[d.branch])boundary.previous=restoredPrevious_[d.branch];
        const bool exact=d.hold&&d.cycle==cycle_&&CycleHold(record,native_,d.branch,boundary,false)&&record.exit.nowNs<d.deadlineNs;
        if(!recovery_.RestoreHold(d.branch,d.invocation,record.exit.nowNs,exact))Cancel(Bc2NativeCycleFailure::Hold);
        return;
    }
    if(!ValidateCycleUpdate(record,native_,d.branch)){if(Active(phase_))Cancel(Bc2NativeCycleFailure::Completion);return;}
    const auto& shotAfter=*record.exit.boundary;
    const bool emptyShot=CycleEmptyShot(record,native_,d.branch);
    const bool positiveShot=CycleShot(record,native_,d.branch)&&(mode_!=Bc2NativeCycleMode::M95||
        (shotAfter.current==6&&shotAfter.previous==5&&shotAfter.next==7)||
        (shotAfter.current==8&&shotAfter.previous==7&&shotAfter.next==1));
    // M95 currently owns the physical bolt, not its magazine reload. Once
    // its exact bolt outcome is acknowledged, unrelated native reload work
    // is observation only. Do not retain the completed bolt's count guard
    // across that unsupported operation. A consumed round still has to prove
    // its exact new shot, and pending debt/Ready never uses this exception.
    if(mode_==Bc2NativeCycleMode::M95&&!ready_&&
       (phase_==Bc2NativeCyclePhase::Watching||phase_==Bc2NativeCyclePhase::Complete)&&
       !emptyShot&&!positiveShot&&CyclePassiveMagazineUpdate(record,native_,d.branch,in.capacities[d.branch])){
        ClearAcknowledgedReady();return;
    }
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
            recovery_={};concurrentCycle_=false;ClearAcknowledgedReady();++cycle_;++shot_;began_=record.exit.nowNs;deadline_=began_+MaximumCycleNs;
            emptyCycle_=emptyShot;reloadPauseSpent_=0;loaded_=after.loaded;reserve_=after.reserve;capacity_=in.capacities[d.branch];
            firedMask_=heldMask_=readyMask_=0;contexts_={};heldAt_={};finishedAt_={};restoredAt_={};restoredPrevious_={};preRestoredAt_={};preRewindAt_={};cycleState_={};tail_={};issued_={};held_.reset();ready_.reset();release_.reset();releaseSubmittedNs_=0;forwardReadyAt_={};
            phase_=Bc2NativeCyclePhase::ShotObserved;
        }
        if(after.loaded!=loaded_||after.reserve!=reserve_||in.capacities[d.branch]!=capacity_||
           (firedMask_&(1u<<d.branch))||emptyShot!=emptyCycle_||after.loaded<0||after.loaded>=capacity_){Cancel(Bc2NativeCycleFailure::Shot);return;}
        firedMask_|=1u<<d.branch;cycleState_[d.branch]=after.current;return;
    }
    cycleState_[d.branch]=record.exit.boundary->current;
    if(phase_==Bc2NativeCyclePhase::Complete&&recovery_.Acknowledged()){
        finishedAt_[d.branch]=record.exit.nowNs;
        const auto idle=[&](const ReloadFlowBoundary& b){return b.current==2&&b.next==2&&b.timer==0&&
            b.loaded==loaded_&&b.reserve==reserve_;};
        const auto& h=record.exit.hold;
        // Evaluate's native read occurs after the captured entry and before
        // OriginalUpdate. Keep those original bounds, never delivery time.
        if(!recovery_.Uncertain()&&!d.hold&&d.cycle==cycle_&&lastInvocation_[d.branch]==d.invocation&&
           in.identity==native_&&in.config==config_&&
           in.capacities[d.branch]==capacity_&&record.entry.nowNs>=recovery_.AcknowledgedAt()&&
           record.entry.nowNs<=in.nowNs&&in.nowNs<=record.exit.nowNs&&
           d.deadlineNs==in.leaseDeadlineNs&&record.exit.nowNs<d.deadlineNs&&
           idle(record.entry.boundary)&&idle(shotAfter)&&record.entry.boundary.previous==shotAfter.previous&&
           record.entry.boundary.flagsA8==shotAfter.flagsA8&&!record.exit.holdRequested&&
           !h.applied&&!h.restored&&!h.unexpectedNativeWrite&&!h.original&&!h.beforeRestore&&
           record.exit.cycleGuard==NativeCycleInputGuard{}&&!record.exit.cycleGuardEpoch&&
           !record.exit.cycleGuardSteps&&!record.exit.cycleGuardRejected)
            paidOrdinaryIdle_[d.branch]={d.invocation,cycle_,shot_,record.entry.nowNs,record.exit.nowNs};
    }
    if(d.hold){
        auto boundary=Boundary(mode_,config_,emptyCycle_);
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
            if(concurrentBoltEnabled_&&mode_==Bc2NativeCycleMode::M95&&BeginRecovery(record.exit.nowNs)){
                concurrentCycle_=true;phase_=Bc2NativeCyclePhase::Advancing;return;
            }
            auto deadline=std::min({deadline_,control_.input.deadlineNs,in.leaseDeadlineNs});
            for(auto at:heldAt_)deadline=std::min(deadline,at+ContextFreshNs);
            PublishHeld(record.exit.nowNs,deadline);
        }
        return;
    }
    if(phase_==Bc2NativeCyclePhase::Releasing&&release_){
        const auto& after=*record.exit.boundary;
        if(acknowledgedReady_&&!CompletionWatch(record.exit.nowNs)){
            Cancel(Bc2NativeCycleFailure::Completion);return;
        }
        if(record.entry.nowNs<release_->observedNs||after.loaded!=loaded_||after.reserve!=reserve_||record.exit.hold.applied||record.exit.holdRequested){
            Cancel(Bc2NativeCycleFailure::Completion);return;
        }
        const bool forward=d.branch<2&&forwardReadyAt_[d.branch]&&
            record.entry.nowNs>=forwardReadyAt_[d.branch]&&
            CycleForwardIdleUpdate(record,native_,d.branch,Boundary(mode_,config_,emptyCycle_).current);
        if((tail_[d.branch]==TailLength()||forward)&&after.current==2&&after.next==2){
            readyMask_|=1u<<d.branch;finishedAt_[d.branch]=record.exit.nowNs;
            if(readyMask_==7){
                if(acknowledgedReady_){
                    // The physical consumer already accepted this exact cycle.
                    // Only the native tail was replayed: do not invent another
                    // gesture, lease, Ready sequence or physical acknowledgement.
                    if(acknowledgedReady_->release!=*release_){Cancel(Bc2NativeCycleFailure::Completion);return;}
                    phase_=Bc2NativeCyclePhase::Complete;held_.reset();return;
                }
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
    if(recovery_.ReloadCommand())return; // Only the dispatched resource observer consumes these callbacks.
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
bool Bc2NativeCycleService::PaidRecoveryRestoreOrdered(const ReloadFlowRecord& record)const noexcept {
    const auto branch=record.entry.boundary.branch;
    if(branch>=2||openUpdate_[branch]||record.id<=lastInvocation_[branch])return false;
    for(unsigned b=0;b<3;++b){
        if(record.entry.nowNs>=finishedAt_[b])continue;
        const auto& prior=paidOrdinaryIdle_[b];
        // Only an independently validated, already-finished sibling Update
        // can explain inverted delivery. Own-branch stale work still fails.
        if(b==branch||!prior.invocation||openUpdate_[b]||prior.invocation!=lastInvocation_[b]||
           prior.cycle!=cycle_||prior.shot!=shot_||prior.endNs!=finishedAt_[b]||
           before_[b].identity!=native_||before_[b].config!=config_||before_[b].capacities[b]!=capacity_||
           before_[b].nowNs>record.entry.nowNs||
           prior.beginNs<recovery_.AcknowledgedAt()||prior.beginNs>record.entry.nowNs||
           prior.endNs>record.exit.nowNs)return false;
    }
    return true;
}
void Bc2NativeCycleService::ObserveRestore(const ReloadFlowRecord& record)noexcept {
    if(recovery_.ReloadCommand())return;
    if(record.entry.boundary.branch<3)paidOrdinaryIdle_[record.entry.boundary.branch]={};
    if(phase_==Bc2NativeCyclePhase::Watching||phase_==Bc2NativeCyclePhase::Cancelled||!native_.owner.player)return;
    if(mode_==Bc2NativeCycleMode::M95&&phase_==Bc2NativeCyclePhase::Complete&&!ready_&&
       record.entry.boundary.branch<2&&!openUpdate_[record.entry.boundary.branch]&&
       record.id>lastInvocation_[record.entry.boundary.branch]&&
       CyclePassiveMagazineRestore(record,native_,record.entry.boundary.branch,capacity_)){
        lastInvocation_[record.entry.boundary.branch]=record.id;
        if(record.entry.boundary.current!=2||record.exit.boundary->current!=2||
           record.exit.boundary->loaded!=loaded_||record.exit.boundary->reserve!=reserve_)ClearAcknowledgedReady();
        return;
    }
    lastEvent_=record.id;
    const auto branch=record.entry.boundary.branch;
    if(Recovering()||(phase_==Bc2NativeCyclePhase::Complete&&recovery_.Active())){
        if(recovery_.Acknowledged()&&!PaidRecoveryRestoreOrdered(record)){ContradictoryRestore();return;}
        if(phase_==Bc2NativeCyclePhase::Complete&&recovery_.Acknowledged()){
            for(unsigned b=0;b<3;++b)if(openUpdate_[b]&&(openHold_[b]||before_[b].identity!=native_||
               before_[b].config!=config_||before_[b].nowNs>record.entry.nowNs)){
                ContradictoryRestore();return;
            }
        }
        if(!recovery_.Restore(record)){ContradictoryRestore();}
        else if(phase_==Bc2NativeCyclePhase::Complete&&recovery_.BlocksFire()){
            // The physical stroke stays paid. Only fresh guarded native idle
            // votes can settle its prediction replay; no hand is reacquired.
            paidRecoveryDrain_=openUpdate_;paidReplayBegan_=record.entry.nowNs;
            phase_=Bc2NativeCyclePhase::Converging;held_.reset();
        }
        return;}
    // No selected-server Restore contract is established. An unrelated actor
    // never reaches this service; runtime filters it before taking the gate.
    if(branch>=2){ContradictoryRestore();return;}
    if(!(firedMask_&(1u<<branch)))return;
    const auto& boundary=Boundary(mode_,config_,emptyCycle_);
    if(CycleConvergenceRestore(record,native_,branch,Profile(mode_,config_)->Convergence(emptyCycle_))&&
       record.entry.boundary.current==2&&record.entry.boundary.next==2&&record.entry.boundary.timer==0&&
       record.exit.boundary->current==boundary.current&&record.exit.boundary->previous==2&&
       record.exit.boundary->next==boundary.next&&record.exit.boundary->timer>0&&record.exit.boundary->timer<=boundary.maxTimer){
        // Actual255: a provisionally ready client restores the server's still
        // running tail, then completes its own original tail again. Keep the
        // already submitted physical release; withdraw only this client's vote.
        // The unchanged snapshot is not itself an Update or a new manual debt.
        const bool forward=forwardReadyAt_[branch]!=0;
        const auto previous=record.entry.boundary.previous;
        const bool paid=phase_==Bc2NativeCyclePhase::Complete&&acknowledgedReady_&&release_&&
            acknowledgedReady_->release==*release_&&acknowledgedAt_>0&&
            release_->cycle.cycle==cycle_&&release_->cycle.shot==shot_&&
            release_->cycle.owner==control_.input.owner&&release_->cycle.item==control_.item&&
            release_->cycle.mechanism==control_.mechanism&&native_.owner==control_.nativeOwner&&
            firedMask_==7&&heldMask_==7&&readyMask_==7&&
            std::all_of(cycleState_.begin(),cycleState_.end(),[](auto state){return state==2;})&&
            record.entry.nowNs>=acknowledgedAt_&&
            record.entry.nowNs>=*std::max_element(finishedAt_.begin(),finishedAt_.end());
        if((phase_!=Bc2NativeCyclePhase::Releasing&&!paid)||!release_||ready_||cycleState_[branch]!=2||
           (acknowledgedReady_&&record.entry.nowNs<acknowledgedAt_)||
           !(readyMask_&(1u<<branch))||(tail_[branch]!=TailLength()&&!forward)||
           (previous!=1&&previous!=2&&!(forward&&previous==boundary.current))||
           record.id<=lastInvocation_[branch]||openUpdate_[branch]||
           record.entry.nowNs<releaseSubmittedNs_||record.entry.nowNs<finishedAt_[branch]||
           record.exit.nowNs>=deadline_||record.exit.nowNs-release_->observedNs>=CompletionNs()||
           record.exit.boundary->loaded!=loaded_||record.exit.boundary->reserve!=reserve_){ContradictoryRestore();return;}
        // Actual413 also proves this snapshot shape. Moving its arrival after
        // ACK is covered by an emulated ordering regression, not a claim that
        // the lost416 failure record had the same payload. Paid authority only
        // drains this exact native replay under the original release deadline.
        if(paid)phase_=Bc2NativeCyclePhase::Releasing;
        readyMask_&=~(1u<<branch);tail_[branch]=0;finishedAt_[branch]=0;
        forwardReadyAt_[branch]=0;
        lastInvocation_[branch]=record.id;cycleState_[branch]=boundary.current;
        restoredAt_[branch]=record.exit.nowNs;restoredPrevious_[branch]=record.exit.boundary->previous;
        return;
    }
    if(CyclePredictedShotRollback(record,native_,branch,emptyCycle_)){
        // Actual253: clientB restored 4 rounds/post-shot to its exact original
        // 5-round idle snapshot, replayed 5->4, then the server fired 5->4.
        // Actual292 repeats this on the final round: clientB restores0->1
        // from the original idle snapshot, then replays1->0 before any hold.
        // Withdraw only that tentative branch observation. The same physical
        // debt, deadline and server evidence survive; all three own Updates
        // must establish the shot again before any held authority is issued.
        if(heldMask_||release_||ready_||phase_!=Bc2NativeCyclePhase::ShotObserved||
           cycleState_[branch]!=6||record.id<=lastInvocation_[branch]||openUpdate_[branch]||
           record.entry.nowNs<began_||record.exit.nowNs>=deadline_||
           record.entry.boundary.loaded!=loaded_||record.entry.boundary.reserve!=reserve_||
           record.exit.boundary->loaded>capacity_){ContradictoryRestore();return;}
        firedMask_&=~(1u<<branch);lastInvocation_[branch]=record.id;cycleState_[branch]=2;
        contexts_={};restoredAt_[branch]=preRestoredAt_[branch]=preRewindAt_[branch]=0;restoredPrevious_[branch]=0;
        return;
    }
    if(!emptyCycle_&&CyclePreHoldSnapshotProgression(record,native_,branch,Boundary(mode_,config_,false),.1f)){
        // Native prediction can cross the post-shot wait/hold boundary in
        // either direction before interception has begun. Preserve the actual
        // Restore output's previous state; never invent an omitted Commit.
        // SPAS251 and M95241 both demonstrate this same native transition;
        // the selected operating class still supplies its exact held boundary.
        if(heldMask_||release_||ready_||cycleState_[branch]!=record.entry.boundary.current||
           (phase_!=Bc2NativeCyclePhase::ShotObserved&&phase_!=Bc2NativeCyclePhase::Arming)||
           record.id<=lastInvocation_[branch]||openUpdate_[branch]||record.entry.nowNs<began_||record.exit.nowNs>=deadline_||
           record.exit.boundary->loaded!=loaded_||record.exit.boundary->reserve!=reserve_){ContradictoryRestore();return;}
        const auto& after=*record.exit.boundary;
        lastInvocation_[branch]=record.id;cycleState_[branch]=after.current;contexts_={};
        if(after.current==Boundary(mode_,config_,false).current){restoredAt_[branch]=record.exit.nowNs;restoredPrevious_[branch]=after.previous;}
        else {preRewindAt_[branch]=record.exit.nowNs;restoredAt_[branch]=preRestoredAt_[branch]=0;restoredPrevious_[branch]=0;}
        return;
    }
    if(CycleForwardReadyRestore(record,native_,branch,boundary)){
        // Actual239 M95 and actual289 SPAS clientA follow completed clientB.
        // Actual287 SPAS clientB follows the server's completed tail. Neither snapshot is an
        // invented client Commit: this client must still finish its own idle
        // Update, and the server must independently complete its exact tail.
        const unsigned source=branch==0?1u:2u;
        const bool admitted=mode_==Bc2NativeCycleMode::Spas||branch==0;
        const bool sourceCompleted=tail_[source]==TailLength()||
            (source<2&&forwardReadyAt_[source]&&(readyMask_&(1u<<source)));
        if(!admitted||phase_!=Bc2NativeCyclePhase::Releasing||!release_||ready_||
           forwardReadyAt_[branch]||tail_[branch]||heldMask_!=7||
           cycleState_[branch]!=boundary.current||!sourceCompleted||cycleState_[source]!=2||
           !(readyMask_&(1u<<source))||!finishedAt_[source]||finishedAt_[source]>record.entry.nowNs||
           (mode_==Bc2NativeCycleMode::Spas&&record.entry.nowNs-finishedAt_[source]>ContextFreshNs)||
           record.id<=lastInvocation_[branch]||openUpdate_[branch]||record.entry.nowNs<releaseSubmittedNs_||
           record.exit.nowNs>=deadline_||record.exit.nowNs-release_->observedNs>=CompletionNs()||
           record.exit.boundary->loaded!=loaded_||record.exit.boundary->reserve!=reserve_){ContradictoryRestore();return;}
        forwardReadyAt_[branch]=record.exit.nowNs;lastInvocation_[branch]=record.id;cycleState_[branch]=2;return;
    }
    const auto proof=CyclePredictionRestore(record,native_,branch);
    if(!proof||proof->invocation<=lastInvocation_[branch]||openUpdate_[branch]||proof->beginNs<began_||
       (phase_!=Bc2NativeCyclePhase::Complete&&proof->endNs>=deadline_)||
       proof->loaded!=loaded_||proof->reserve!=reserve_){ContradictoryRestore();return;}
    const bool ready=proof->current==2&&proof->next==2&&
        (proof->beforePrevious==1||proof->beforePrevious==2||
         (forwardReadyAt_[branch]&&(readyMask_&(1u<<branch))&&proof->beforePrevious==boundary.current))&&
        proof->beforeTimer==0&&proof->timer==0;
    const bool completedTail=tail_[branch]==TailLength()||
        (forwardReadyAt_[branch]&&(readyMask_&(1u<<branch)));
    const bool held=proof->current==boundary.current&&proof->next==boundary.next&&
        (proof->beforePrevious==boundary.previous||(proof->beforePrevious==restoredPrevious_[branch]&&restoredAt_[branch]))&&
        proof->beforeTimer>0&&proof->beforeTimer<=boundary.maxTimer&&proof->timer>0&&proof->timer<=boundary.maxTimer;
    if(phase_==Bc2NativeCyclePhase::Complete){
        if(!completedTail||!(readyMask_&(1u<<branch))||!ready){ContradictoryRestore();return;}
    }else if(phase_==Bc2NativeCyclePhase::Releasing){
        // Other branches can still be held while an earlier branch is ready.
        // Ready-to-tail replay requires the explicit unchanged-snapshot proof
        // and provisional-vote withdrawal above; a repeated phase alone cannot.
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
    ready_.reset();held_.reset();readyMask_=heldMask_=0;openUpdate_={};issued_={};suspendedAt_=suspendedUntil_=0;ClearAcknowledgedReady();
    phase_=Bc2NativeCyclePhase::Cancelled;failure_=Bc2NativeCycleFailure::Restore;failureInvocation_=lastEvent_;
}
void Bc2NativeCycleService::PublishHeld(std::int64_t observed,std::int64_t deadline)noexcept {
    if(observed>=deadline||sequence_==UINT64_MAX)return;
    held_=WeaponCycleLease{control_.input.owner,control_.item,control_.mechanism,cycle_,shot_,++sequence_,observed,deadline,true};
    issued_[issueAt_]=*held_;issueAt_=(issueAt_+1)%issued_.size();
}
bool Bc2NativeCycleService::Release(const WeaponCycleRelease& release,std::int64_t now)noexcept {
    if(release_){return *release_==release;}
    if((phase_!=Bc2NativeCyclePhase::Held&&phase_!=Bc2NativeCyclePhase::Advancing)||!held_||!release.request||!release.inputSequence||
       release.inputSequence>control_.input.sequence||!weapon_cycle_detail::Window(release.observedNs,release.deadlineNs,now)||
       release.deadlineNs>release.cycle.deadlineNs||!weapon_cycle_detail::Same(release.cycle,*held_)||
       std::find(issued_.begin(),issued_.end(),release.cycle)==issued_.end())return false;
    if(concurrentCycle_){
        if(!recovery_.SubmitAdvancing(release,now))return false;
        release_=release;releaseSubmittedNs_=now;held_.reset();return true;
    }
    release_=release;releaseSubmittedNs_=now;phase_=Bc2NativeCyclePhase::Releasing;held_.reset();tail_={};readyMask_=0;return true;
}
void Bc2NativeCycleService::Missing(std::uint32_t firing,std::uint64_t invocation)noexcept {
    const auto branch=std::find(native_.firing.begin(),native_.firing.end(),firing);
    if(branch==native_.firing.end())return;
    if(Active(phase_)){
        lastEvent_=invocation;Cancel(Bc2NativeCycleFailure::Owner);
        return;
    }
    // A final tail on another branch can publish Complete while this idle
    // Update is still running. A missing observation closes that exact callback
    // without undoing the completed cycle (or its acknowledgement). Otherwise
    // the next valid Restore sees a callback that will never finish and cancels.
    // Keep all cycle/count/owner evidence: a contradictory Restore still fails.
    const auto index=static_cast<unsigned>(branch-native_.firing.begin());
    paidOrdinaryIdle_[index]={};
    if(invocation&&openUpdate_[index]==invocation){
        openUpdate_[index]=0;openHold_[index]=false;openDeadline_[index]=0;
    }
}
void Bc2NativeCycleService::Cancel(Bc2NativeCycleFailure failure)noexcept {
    // Cleanup repeats cancellation after the first rejected native event. Keep
    // that first cause and invocation instead of reporting the later teardown.
    if(phase_==Bc2NativeCyclePhase::Cancelled)return;
    ClearAcknowledgedReady();
    if(recovery_.Active())recovery_.LoseGuard();
    suspendedAt_=suspendedUntil_=0;
    // Cancellation is also a barrier for an Update evaluated before an owner
    // disappeared. Its later completion cannot create a cycle in a new owner.
    openUpdate_={};contexts_={};heldAt_={};restoredAt_={};restoredPrevious_={};held_.reset();issued_={};
    if(phase_==Bc2NativeCyclePhase::Watching||phase_==Bc2NativeCyclePhase::Complete){
        // Startup/vehicle publication loss has no known manual debt. Exact
        // completed outcomes stay retained until acknowledgement, even here.
        control_={};
        // An unacknowledged completed outcome still needs its exact retired
        // actor identity. Keeping that identity grants no control or hold.
        if(!ready_){native_={};config_={};}
        return;
    }
    phase_=Bc2NativeCyclePhase::Cancelled;failure_=failure;failureInvocation_=lastEvent_;held_.reset();
}
bool Bc2NativeCycleService::AcknowledgeReady(const WeaponCycleReady& ready,std::int64_t now)noexcept {
    if(!ready_||ready_->release!=ready.release||ready_->sequence!=ready.sequence||ready_->observedNs!=ready.observedNs||
       ready_->deadlineNs!=ready.deadlineNs||ready_->nativeReady!=ready.nativeReady||ready_->unchangedAmmunition!=ready.unchangedAmmunition)return false;
    if(concurrentCycle_){
        if(!recovery_.AcknowledgeAdvancing(ready,now))return false;
        phase_=Bc2NativeCyclePhase::Complete;
    }
    if(!recovery_.Active()&&release_&&*release_==ready.release&&native_.owner.player&&
       native_.owner==control_.nativeOwner&&ready.nativeReady&&ready.unchangedAmmunition&&
       (now==0||now>=ready.observedNs)){
        acknowledgedReady_=ready;acknowledgedAt_=std::max(now,ready.observedNs);
    }
    ready_.reset();return true;
}
Bc2NativeCycleView Bc2NativeCycleService::View(std::int64_t now)const noexcept {
    Bc2NativeCycleView out{phase_,failure_,native_,{},{},Active(phase_)||ready_.has_value(),loaded_,reserve_,capacity_,cycle_,shot_};
    if(held_&&weapon_cycle_detail::Lease(*held_,now))out.held=held_;
    else if(held_&&held_->observedNs>now&&(phase_==Bc2NativeCyclePhase::Held||phase_==Bc2NativeCyclePhase::Advancing)&&!suspendedAt_){
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
    out.trackingRebase=trackingRebase_;
    return out;
}
}
