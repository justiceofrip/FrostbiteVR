#pragma once
#include "Bc2NativeCycleRecovery.h"
#include "Bc2NativeCycleEvidence.h"
#include "Bc2ReloadFlowRuntime.h"
#include "fvr/interaction/WeaponCycleConvergence.h"

namespace fvr::bc2 {
struct NativeCycleRecoveryScope {
    ReloadHoldIdentity identity{};ReloadObservedConfig config{};
    bool operator==(const NativeCycleRecoveryScope&)const noexcept=default;
};
struct NativeCycleRecoveryDecision {
    std::uint64_t invocation=0,epoch=0;unsigned branch=3;
    int capacity=0;std::int64_t deadlineNs=0;
    explicit operator bool()const noexcept{return invocation&&epoch&&branch<3;}
};
// Serialized captured-evidence consumer. Runtime alone supplies current own
// boundaries and an actual scoped Update guard; this object has no memory API.
class Bc2NativeCycleRecoveryState {
public:
    using Policy=interaction::WeaponCycleConvergence<NativeCycleRecoveryScope>;
    bool Begin(const NativeCycleRecoveryScope& scope,const interaction::WeaponCycleDebt& debt,
        std::int64_t began,bool m95,bool empty)noexcept {
        if(active_||!policy_.Begin(scope,debt,began))return false;
        scope_=scope;debt_=debt;began_=began;m95_=m95;empty_=empty;active_=true;return true;
    }
    bool BeginHold(unsigned branch,std::uint64_t invocation,std::int64_t begin,std::int64_t deadline)noexcept {
        if(!active_||branch>=3||open_[branch]||!policy_.BeginHold(scope_,branch,invocation,begin,deadline))return false;
        open_[branch]=last_[branch]=invocation;return true;
    }
    bool RestoreHold(unsigned branch,std::uint64_t invocation,std::int64_t end,bool exact)noexcept {
        if(branch>=3||open_[branch]!=invocation)return false;
        open_[branch]=0;return policy_.RestoreHold(scope_,branch,invocation,end,exact);
    }
    NativeCycleRecoveryDecision Evaluate(const NativeCycleRecoveryScope& scope,const ReloadFlowEventInput& entry,
        int capacity,std::int64_t deadline)noexcept {
        NativeCycleRecoveryDecision out;
        const auto branch=entry.boundary.branch;const auto decoded=DecodeReloadUpdateContext(entry.copiedContext);
        if(!active_||Uncertain()||scope!=scope_||branch>=3||open_[branch]||!entry.nativeInvocation||
           entry.nativeInvocation<=last_[branch]||entry.kind!=ReloadFlowEvent::Update||entry.depth!=1||
           entry.nativeParent||entry.nativeUpdate!=entry.nativeInvocation||entry.nowNs<began_||
           (policy_.Acknowledged()&&entry.nowNs<std::max(acknowledgedAt_,paidNativeFloor_))||
           !entry.contextCopied||!decoded||!ValidManualReloadDelta(decoded->deltaSeconds)||
           decoded->reloadTimeMultiplier<=0||decoded->reloadTimeMultiplier>4||decoded->flags24Through28[2]||
           entry.boundary.firing!=scope_.identity.firing[branch]||entry.boundary.owner!=scope_.identity.owner||
           entry.boundary.loaded!=debt_.loaded||entry.boundary.reserve!=debt_.reserve||capacity!=debt_.capacity||
           !CycleConvergencePhase(entry.boundary.current,entry.boundary.next,entry.boundary.timer,m95_,empty_)||
           deadline<=entry.nowNs||deadline-entry.nowNs>Policy::MaximumFreshNs){LoseGuard();return out;}
        open_[branch]=last_[branch]=entry.nativeInvocation;
        return {entry.nativeInvocation,debt_.guardEpoch,branch,capacity,deadline};
    }
    bool Finish(const NativeCycleRecoveryDecision& d,const ReloadFlowRecord& record,const NativeCycleInputGuard& guard,
        unsigned steps,unsigned rejected)noexcept {
        if(!d||d.branch>=3||open_[d.branch]!=d.invocation){LoseGuard();return false;}
        open_[d.branch]=0;
        if(Uncertain()||record.id!=d.invocation||d.epoch!=debt_.guardEpoch||record.exit.nowNs>=d.deadlineNs||
           record.exit.cycleGuardEpoch!=d.epoch||record.exit.cycleGuard!=guard||
           record.exit.cycleGuardSteps!=steps||record.exit.cycleGuardRejected!=rejected||
           !CycleGuardedUpdate(record,scope_.identity,d.branch,guard,steps,rejected,m95_,empty_)||
           record.exit.boundary->loaded!=debt_.loaded||record.exit.boundary->reserve!=debt_.reserve){LoseGuard();return false;}
        ++guardedUpdates_;guardedSteps_+=steps;
        progress_[d.branch]={d.invocation,record.exit.nowNs,d.deadlineNs};
        const auto idle=[](const ReloadFlowBoundary& b){return b.current==2&&b.next==2&&b.timer==0;};
        if(idle(record.entry.boundary)&&idle(*record.exit.boundary)){
            currentIdle_[d.branch]={record.exit.nowNs,d.deadlineNs};
            // A valid callback can overlap the preceding voting-round boundary.
            // The policy rejects it as a vote without treating it as guard loss.
            policy_.ObserveIdle(scope_,{d.branch,d.invocation,debt_.guardEpoch,record.entry.nowNs,
                record.exit.nowNs,d.deadlineNs,debt_.loaded,debt_.reserve,debt_.capacity},record.exit.nowNs);
            if(!policy_.Acknowledged()&&!ready_&&policy_.Outcome()){
                const auto& result=*policy_.Outcome();
                if(const auto* release=std::get_if<interaction::WeaponCycleIdleDebtRelease>(&result.release))
                    ready_=interaction::WeaponCycleIdleDebtReady{*release,*std::max_element(result.native.second.begin(),result.native.second.end()),
                        result.native.observedNs,result.native.deadlineNs,true,true};
                else if(const auto* normalRelease=std::get_if<interaction::WeaponCycleRelease>(&result.release))
                    nativeReady_=interaction::WeaponCycleReady{*normalRelease,*std::max_element(result.native.second.begin(),result.native.second.end()),
                        result.native.observedNs,result.native.deadlineNs,true,true};
            }
            if(PaidConverged(record.exit.nowNs))paidNativeFloor_=record.exit.nowNs;
        }else {currentIdle_={};policy_.NativeBusy(scope_,record.exit.nowNs);grant_.reset();}
        return true;
    }
    bool Commit(const ReloadFlowRecord& record)noexcept {
        const auto branch=record.entry.boundary.branch;
        if(!active_||Uncertain()||branch>=3||!open_[branch]||
           !CycleGuardedCommit(record,scope_.identity,branch,open_[branch],m95_,empty_)||
           record.exit.boundary->loaded!=debt_.loaded||record.exit.boundary->reserve!=debt_.reserve){LoseGuard();return false;}
        return true;
    }
    bool Restore(const ReloadFlowRecord& record)noexcept {
        const auto b=record.entry.boundary.branch;
        if(!active_||Uncertain()||b>=2||open_[b]||record.id<=last_[b]||record.entry.nowNs<began_||
           (policy_.Acknowledged()&&record.entry.nowNs<std::max(acknowledgedAt_,paidNativeFloor_))||
           !CycleConvergenceRestore(record,scope_.identity,b,m95_,empty_)||
           record.exit.boundary->loaded!=debt_.loaded||record.exit.boundary->reserve!=debt_.reserve){LoseGuard();return false;}
        const auto& before=record.entry.boundary;const auto& after=*record.exit.boundary;
        const auto idle=[](const ReloadFlowBoundary& v){return v.current==2&&v.next==2&&v.timer==0;};
        const bool busy=!idle(before)||!idle(after);
        if(policy_.Acknowledged()&&policy_.Phase()==interaction::WeaponCycleConvergencePhase::Ready&&busy){
            // The already observed client ready->tail snapshot is the only
            // admission to paid reconciliation. Unseen shots, reload phases,
            // changed ammunition and arbitrary busy states are not payment.
            const unsigned tail=empty_?6u:m95_?8u:7u,next=empty_||m95_?1u:8u;
            const float maximum=empty_?1.f:m95_?2.3f:.5f;
            if(!idle(before)||after.current!=tail||after.previous!=2||after.next!=next||
               after.timer<=0||after.timer>maximum){LoseGuard();return false;}
        }
        last_[b]=record.id;
        progress_[b]={}; // A snapshot cannot stand in for a guarded own Update.
        if(busy){
            currentIdle_={};policy_.NativeBusy(scope_,record.exit.nowNs);grant_.reset();}
        return true; // A Restore itself never supplies an idle vote.
    }
    // A plain own Update admitted while Complete may finish after an independent
    // client replay starts. The service validates its exact unchanged idle
    // record. Draining it supplies NO guarded vote and discards all earlier
    // votes: two new guarded rounds must follow the final overlapping drain.
    bool DrainPaidOrdinary(unsigned branch,std::uint64_t invocation,std::int64_t now)noexcept {
        if(!Acknowledged()||Uncertain()||branch>=3||!invocation||open_[branch]||now<acknowledgedAt_)return false;
        last_[branch]=std::max(last_[branch],invocation);
        paidNativeFloor_=std::max(paidNativeFloor_,now);
        currentIdle_={};progress_={};grant_.reset();policy_.NativeBusy(scope_,now);return true;
    }
    bool Control(const interaction::HandInteractionSample& input)noexcept {
        if(!active_||Uncertain()||input.owner!=debt_.owner)return false;
        if(!grant_)grant_=policy_.ArmRecovery(scope_,input,m95_?interaction::WeaponCycleHandAssignment{
            interaction::InteractionHand::Right,interaction::InteractionHand::Left}:interaction::WeaponCycleHandAssignment{});
        return true;
    }
    void Interrupt(std::int64_t now)noexcept {
        if(!active_)return;policy_.Interrupt(scope_,now);grant_.reset();
    }
    bool RebaseTracking(const ReloadStateOwner& native,const interaction::HandInteractionSample& input)noexcept {
        auto previous=native;previous.space=scope_.identity.owner.space;
        auto next=scope_;next.identity.owner=native;
        if(!active_||Uncertain()||!Drained()||ready_||previous!=scope_.identity.owner||
           native.space<=previous.space||native.space!=input.owner.space||
           !policy_.RebaseTracking(scope_,next,input.owner,input.nowNs))return false;
        scope_=next;debt_.owner=input.owner;currentIdle_={};grant_.reset();return true;
    }
    std::optional<Bc2NativeCycleRecoveryView> View(std::int64_t now)noexcept {
        if(!active_)return {};
        Bc2NativeCycleRecoveryView view{scope_.identity,{},{},policy_.BlocksFire()};
        if(!Uncertain()&&grant_)view.authority=policy_.RecoveryAuthority(scope_,*grant_,now);
        // Retained exact outcome has its original completion time. Tracking
        // loss and polling never extend the authority or restamp readiness.
        if(!Uncertain())view.ready=ready_;return view;
    }
    bool Submit(const interaction::WeaponCycleIdleDebtRelease& release,const interaction::HandInteractionSample& input)noexcept {
        if(!active_||Uncertain()||input.owner!=debt_.owner||!release.inputSequence||release.inputSequence>input.sequence||
           release.observedNs>input.observedNs||release.deadlineNs>input.deadlineNs||
           !input.focused||!input.tracked[0]||!input.tracked[1]||
           !interaction::weapon_cycle_detail::Window(input.observedNs,input.deadlineNs,input.nowNs))return false;
        return policy_.SubmitRecovered(scope_,release,input.nowNs);
    }
    std::optional<interaction::WeaponCycleLease> AdvancingAuthority(
        const interaction::HandInteractionSample& input,std::uint64_t sequence,std::int64_t now)const noexcept {
        if(!active_||Uncertain()||policy_.Phase()!=interaction::WeaponCycleConvergencePhase::Debt||!sequence||
           input.owner!=debt_.owner||!input.focused||!input.tracked[0]||!input.tracked[1]||
           !interaction::weapon_cycle_detail::Window(input.observedNs,input.deadlineNs,now)||!Drained())return {};
        auto observed=input.observedNs,deadline=input.deadlineNs,first=now;
        interaction::WeaponCycleGuardedAdvance proof{debt_.guardEpoch,{},debt_.loaded,debt_.reserve,debt_.capacity};
        for(unsigned b=0;b<3;++b){const auto& p=progress_[b];
            if(!p.invocation||p.observed>now||now>=p.deadline)return {};
            observed=std::max(observed,p.observed);first=std::min(first,p.observed);deadline=std::min(deadline,p.deadline);
            proof.updates[b]=p.invocation;
        }
        if(observed>=deadline||observed-first>Policy::MaximumRoundSpanNs)return {};
        interaction::WeaponCycleLease result{debt_.owner,debt_.item,debt_.mechanism,debt_.cycle,debt_.shot,
            sequence,observed,deadline,false,proof};
        if(!interaction::weapon_cycle_detail::Lease(result,now))return {};return result;
    }
    bool SubmitAdvancing(const interaction::WeaponCycleRelease& release,std::int64_t now)noexcept {
        return active_&&!Uncertain()&&release.cycle.advancing&&
            release.cycle.advancing->guardEpoch==debt_.guardEpoch&&policy_.Submit(scope_,release,now);
    }
    const auto& NativeReady()const noexcept{return nativeReady_;}
    bool AcknowledgeAdvancing(const interaction::WeaponCycleReady& ready,std::int64_t now)noexcept {
        if(!active_||Uncertain()||!nativeReady_||nativeReady_->release!=ready.release||
           nativeReady_->sequence!=ready.sequence||nativeReady_->observedNs!=ready.observedNs||
           nativeReady_->deadlineNs!=ready.deadlineNs||!ready.nativeReady||!ready.unchangedAmmunition||
           !policy_.Outcome()||!Drained())return false;
        for(const auto& idle:currentIdle_)if(!idle.first||now<idle.first||now>=idle.second)return false;
        if(!policy_.Acknowledge(*policy_.Outcome()))return false;
        acknowledgedAt_=now;paidNativeFloor_=0;
        for(const auto& idle:currentIdle_)paidNativeFloor_=std::max(paidNativeFloor_,idle.first);
        nativeReady_.reset();return true;
    }
    bool Acknowledge(const interaction::WeaponCycleIdleDebtReady& ready,std::int64_t now)noexcept {
        if(!active_||Uncertain()||!ready_||ready_->release!=ready.release||ready_->sequence!=ready.sequence||
           ready_->observedNs!=ready.observedNs||ready_->deadlineNs!=ready.deadlineNs||
           ready_->nativeReady!=ready.nativeReady||ready_->unchangedAmmunition!=ready.unchangedAmmunition||!policy_.Outcome()||
           std::any_of(open_.begin(),open_.end(),[](auto id){return id!=0;}))return false;
        for(const auto& idle:currentIdle_)if(!idle.first||now<idle.first||now>=idle.second)return false;
        if(!policy_.Acknowledge(*policy_.Outcome()))return false;
        acknowledgedAt_=now;paidNativeFloor_=0;
        for(const auto& idle:currentIdle_)paidNativeFloor_=std::max(paidNativeFloor_,idle.first);
        ready_.reset();return true;
    }
    void LoseGuard()noexcept {if(active_)policy_.LoseGuard(scope_);currentIdle_={};progress_={};grant_.reset();ready_.reset();nativeReady_.reset();}
    bool Active()const noexcept{return active_;}
    bool Acknowledged()const noexcept{return active_&&policy_.Acknowledged();}
    std::int64_t AcknowledgedAt()const noexcept{return acknowledgedAt_;}
    bool PaidConverged(std::int64_t now)const noexcept {
        if(!Acknowledged()||Uncertain()||policy_.Phase()!=interaction::WeaponCycleConvergencePhase::Ready||!Drained())return false;
        for(const auto& idle:currentIdle_)if(!idle.first||now<idle.first||now>=idle.second)return false;
        return true;
    }
    bool Uncertain()const noexcept{return policy_.Phase()==interaction::WeaponCycleConvergencePhase::Uncertain;}
    bool BlocksFire()const noexcept{return active_&&policy_.BlocksFire();}
    bool Drained()const noexcept{return std::none_of(open_.begin(),open_.end(),[](auto id){return id!=0;});}
    std::uint64_t Epoch()const noexcept{return debt_.guardEpoch;}
    std::int64_t Began()const noexcept{return began_;}
    const NativeCycleRecoveryScope& Scope()const noexcept{return scope_;}
    std::uint64_t GuardedUpdates()const noexcept{return guardedUpdates_;}
    std::uint64_t GuardedSteps()const noexcept{return guardedSteps_;}
private:
    NativeCycleRecoveryScope scope_{};interaction::WeaponCycleDebt debt_{};Policy policy_{};
    bool active_=false,m95_=false,empty_=false;std::int64_t began_=0;
    std::int64_t acknowledgedAt_=0,paidNativeFloor_=0;
    std::array<std::uint64_t,3> open_{},last_{};
    std::array<std::pair<std::int64_t,std::int64_t>,3> currentIdle_{};
    struct Progress {std::uint64_t invocation=0;std::int64_t observed=0,deadline=0;};
    std::array<Progress,3> progress_{};
    std::optional<interaction::WeaponCycleRecoveryGrant> grant_;
    std::optional<interaction::WeaponCycleIdleDebtReady> ready_;
    std::optional<interaction::WeaponCycleReady> nativeReady_;
    std::uint64_t guardedUpdates_=0,guardedSteps_=0;
};
}
