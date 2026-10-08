#include "Bc2ReloadRoundGate.h"
#include "Bc2ReloadConfigDescriptor.h"
#include <cstring>
namespace fvr::bc2 {
namespace {
bool Wait(const ReloadFiringObservation& b){return b.currentState==11&&b.nextState==12&&std::isfinite(b.phaseTimer)&&b.phaseTimer>0&&b.phaseTimer<=1;}
bool Safe(const ReloadUpdateContext& c){return std::isfinite(c.deltaSeconds)&&c.deltaSeconds>0&&c.deltaSeconds<=.05f&&c.reloadTimeMultiplier==1&&
    !c.inputFlags&&!c.fireRequested&&!c.orderRequested&&!c.reloadRequested&&!c.flags24Through28[2]&&!c.flags24Through28[3]&&!c.flags24Through28[4];}
bool SameHeld(const ReloadFiringObservation& a,const ReloadFiringObservation& b){return a.address==b.address&&a.wrapperOffset==b.wrapperOffset&&
    a.currentState==b.currentState&&a.nextState==b.nextState&&a.phaseTimer==b.phaseTimer&&a.loaded==b.loaded&&a.reserve==b.reserve;}
}
bool ReadReloadRoundTiming(const ReloadStateMemory& m,const ReloadObservedConfig& c)noexcept {
    return ReadReloadDescriptorTiming(m,c,SpasReloadDescriptor);
}
bool ReloadRoundGate::Enable()noexcept {if(phase_!=ReloadRoundGatePhase::Disabled||!initial_.Enable())return false;phase_=ReloadRoundGatePhase::Waiting;return true;}
void ReloadRoundGate::Cancel(ReloadRoundGateFailure why)noexcept {if(phase_==ReloadRoundGatePhase::Disabled||phase_==ReloadRoundGatePhase::Released||phase_==ReloadRoundGatePhase::Aborted)return;
    phase_=ReloadRoundGatePhase::Aborted;failure_=why;initial_.Stop();completion_.Cancel();}
bool ReloadRoundGate::Common(const ReloadHoldInput& in,bool timing)noexcept {
    if(!in.verified||in.branch>=3||in.nowNs<=0||in.leaseDeadlineNs<=in.nowNs||in.leaseDeadlineNs-in.nowNs>200000000||!IsDiagnosticSpasConfig(in.config)){
        Cancel(ReloadRoundGateFailure::OwnerOrRead);return false;}
    if(!Safe(in.context)){Cancel(ReloadRoundGateFailure::UnsafeInput);return false;}
    if(!timing){Cancel(ReloadRoundGateFailure::Timing);return false;}
    if(initial_.BeginNs()>0&&(in.identity!=initial_.Identity()||in.config!=armedConfig_)){Cancel(ReloadRoundGateFailure::OwnerOrRead);return false;}
    for(unsigned n=0;n<3;++n){const auto& b=in.branches[n];
        if(b.address!=in.identity.firing[n]||b.wrapperOffset!=(n==0?0x3cu:n==1?0x40u:0x10u)||
           b.currentState>15||b.nextState>15||!std::isfinite(b.phaseTimer)||b.loaded<0||b.reserve<0||b.loaded>1000000||b.reserve>1000000||
           (b.flagsA8&(8|16))||in.capacities[n]!=in.capacities[0]||in.capacities[n]<=0||in.capacities[n]>1000000||(capacity_&&in.capacities[n]!=capacity_)){Cancel(ReloadRoundGateFailure::UnexpectedState);return false;}
    }
    contexts_[in.branch]=in.nowNs;return true;
}
bool ReloadRoundGate::RecentHolds(const ReloadHoldInput& in,const std::array<std::int64_t,3>& held)const noexcept {
    for(unsigned n=0;n<3;++n)if(!held[n]||held[n]>in.nowNs||in.nowNs-held[n]>ReloadHoldProbe::ContextFreshNs||
        !contexts_[n]||contexts_[n]>in.nowNs||in.nowNs-contexts_[n]>ReloadHoldProbe::ContextFreshNs)return false;
    return true;
}
ReloadRoundGateDecision ReloadRoundGate::Track(const ReloadHoldInput& in,std::uint64_t update,bool hold,std::int64_t deadline)noexcept {
    if(!update||open_[in.branch]){Cancel(ReloadRoundGateFailure::Overlap);return {};}
    open_[in.branch]=update;return {update,in.branch,true,hold,deadline,in.branches[in.branch],phase_};
}
bool ReloadRoundGate::Sample(const ReloadHoldInput& in,bool stable)noexcept {
    // Mixed pre/post native samples may update the current branch, but cannot
    // prove global completion or a revert. Wait for one coherent cohort.
    if(!stable||std::any_of(open_.begin(),open_.end(),[](auto x){return x!=0;}))return true;
    ReloadRoundSample s;s.identity=in.identity;s.cycle=1;s.sequence=++sequence_;s.observedNs=in.nowNs;
    s.deadlineNs=std::min(in.leaseDeadlineNs,advanceDeadline_);s.branches=in.branches;s.nativeBindingVerified=true;
    s.allThreeHeld=RecentHolds(in,reholds_);
    const bool done=completion_.Observe(s,in.nowNs);
    if(completion_.Phase()==ReloadRoundPhase::Failed){Cancel(ReloadRoundGateFailure::Completion);return false;}
    if(done){ack_=completion_.TakeAcknowledgement(in.identity,1,in.nowNs);if(!ack_){Cancel(ReloadRoundGateFailure::Completion);return false;}
        if(s.allThreeHeld){secondBegin_=in.nowNs;secondDeadline_=in.nowNs+ReholdNs;phase_=ReloadRoundGatePhase::SecondHold;}
        else phase_=ReloadRoundGatePhase::Released;
    }return true;
}
ReloadRoundGateDecision ReloadRoundGate::Evaluate(const ReloadHoldInput& in,bool timing,bool stable,std::uint64_t update)noexcept {
    if(phase_==ReloadRoundGatePhase::Disabled||phase_==ReloadRoundGatePhase::Released||phase_==ReloadRoundGatePhase::Aborted)return {};
    // At idle, ordinary fire/reload inputs precede the diagnostic's arm point.
    if(phase_==ReloadRoundGatePhase::Waiting){
        if(!in.verified||in.branch>=3||!timing||!Safe(in.context)){return {};}
        contexts_[in.branch]=in.nowNs;
        if(!initial_.Evaluate(in))return {};
        phase_=ReloadRoundGatePhase::FirstHold;armedConfig_=in.config;loaded_=initial_.Loaded();reserve_=initial_.Reserve();capacity_=in.capacities[0];
        return Track(in,update,true,initial_.DeadlineNs());
    }
    if(!Common(in,timing))return {};
    if(phase_==ReloadRoundGatePhase::FirstHold){
        if(initial_.Evaluate(in))return Track(in,update,true,initial_.DeadlineNs());
        if(initial_.Phase()!=ReloadHoldPhase::Released||initial_.Reason()!=ReloadHoldReason::Expired){Cancel(ReloadRoundGateFailure::InitialHold);return {};}
        if(!RecentHolds(in,initialHolds_)){Cancel(ReloadRoundGateFailure::MissingHolds);return {};}
        for(const auto& b:in.branches)if(!Wait(b)||b.loaded!=loaded_||b.reserve!=reserve_){Cancel(ReloadRoundGateFailure::UnexpectedState);return {};}
        requestNs_=in.nowNs;advanceDeadline_=in.nowNs+AdvanceNs;
        const auto& o=in.identity.owner;
        const interaction::ManualReloadRequest request{1,{o.soldier,o.actorGeneration,o.weapon,o.equipGeneration,o.space},interaction::ReloadOperation::InsertRound,0,0};
        ReloadRoundLease lease{in.identity,1,++sequence_,in.nowNs,in.leaseDeadlineNs,loaded_,reserve_,capacity_,true,true};
        if(!completion_.Begin(request,lease,in.nowNs,advanceDeadline_)){Cancel(ReloadRoundGateFailure::Completion);return {};}
        phase_=ReloadRoundGatePhase::Advancing;
    }
    if(phase_==ReloadRoundGatePhase::Advancing){
        if(in.nowNs>=advanceDeadline_){Cancel(ReloadRoundGateFailure::Expired);return {};}
        if(!Sample(in,stable))return {};
        if(phase_==ReloadRoundGatePhase::Released)return {};
    }
    const auto& b=in.branches[in.branch];
    if(phase_==ReloadRoundGatePhase::SecondHold){
        if(in.nowNs>=secondDeadline_){phase_=ReloadRoundGatePhase::Released;return {};}
        if(!Wait(b)||b.loaded!=loaded_+1||b.reserve!=reserve_-1){Cancel(ReloadRoundGateFailure::UnexpectedState);return {};}
        return Track(in,update,true,secondDeadline_);
    }
    if(b.loaded==loaded_&&b.reserve==reserve_){
        if(transfers_[in.branch]||!Wait(b)){Cancel(ReloadRoundGateFailure::UnexpectedState);return {};}
        // Every released branch's last context is exact multiplier1 and recent.
        for(auto time:contexts_)if(!time||time>in.nowNs||in.nowNs-time>ReloadHoldProbe::ContextFreshNs){Cancel(ReloadRoundGateFailure::UnsafeInput);return {};}
        return Track(in,update,false,advanceDeadline_);
    }
    if(b.loaded==loaded_+1&&b.reserve==reserve_-1){
        if(Wait(b))return Track(in,update,true,advanceDeadline_);
        const bool ending=(b.loaded==capacity_||!b.reserve)&&(b.currentState==1||b.currentState==2)&&(b.nextState==1||b.nextState==2);
        if(ending)return Track(in,update,false,advanceDeadline_);
    }
    Cancel(ReloadRoundGateFailure::UnexpectedState);return {};
}
bool ReloadRoundGate::Transfer(const ReloadRoundTransfer& e,std::uint64_t parent)noexcept {
    if(phase_!=ReloadRoundGatePhase::Advancing||e.branch>=3||!parent||open_[e.branch]!=parent){Cancel(ReloadRoundGateFailure::Completion);return false;}
    if(!completion_.Observe(e)){Cancel(ReloadRoundGateFailure::Completion);return false;}
    transfers_[e.branch]=e.invocation;return true;
}
bool ReloadRoundGate::Finish(const ReloadRoundGateDecision& d,const ReloadFiringObservation& after,std::int64_t now,bool retained,const ReloadDeltaOverride& delta)noexcept {
    if(!d.tracked)return true;
    if(d.branch>=3||open_[d.branch]!=d.update){Cancel(ReloadRoundGateFailure::Overlap);return false;}open_[d.branch]=0;
    if(phase_==ReloadRoundGatePhase::Aborted||phase_==ReloadRoundGatePhase::Released)return false;
    if(!retained||now<=0||after.address!=Identity().firing[d.branch]||after.wrapperOffset!=d.before.wrapperOffset){Cancel(ReloadRoundGateFailure::OwnerOrRead);return false;}
    if(d.hold){
        if(!delta.applied||!delta.restored||delta.unexpectedNativeWrite||!SameHeld(d.before,after)){Cancel(ReloadRoundGateFailure::Patch);return false;}
        if(d.phase==ReloadRoundGatePhase::FirstHold)initialHolds_[d.branch]=now;
        else {reholds_[d.branch]=now;if(!firstReholds_[d.branch])firstReholds_[d.branch]=d.update;}
    }else{
        if(delta.applied){Cancel(ReloadRoundGateFailure::Patch);return false;}
        const bool baseline=after.loaded==loaded_&&after.reserve==reserve_;
        const bool advanced=after.loaded==loaded_+1&&after.reserve==reserve_-1;
        if((!baseline&&!advanced)||(baseline&&transfers_[d.branch])){Cancel(ReloadRoundGateFailure::Completion);return false;}
        if(!Wait(after)&&!((after.loaded==capacity_||!after.reserve)&&(after.currentState==1||after.currentState==2)&&(after.nextState==1||after.nextState==2))){Cancel(ReloadRoundGateFailure::UnexpectedState);return false;}
    }return true;
}
}
