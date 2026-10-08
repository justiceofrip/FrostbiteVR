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
        const auto idle=[](const ReloadFlowBoundary& b){return b.current==2&&b.next==2&&b.timer==0;};
        if(idle(record.entry.boundary)&&idle(*record.exit.boundary)){
            currentIdle_[d.branch]={record.exit.nowNs,d.deadlineNs};
            // A valid callback can overlap the preceding voting-round boundary.
            // The policy rejects it as a vote without treating it as guard loss.
            policy_.ObserveIdle(scope_,{d.branch,d.invocation,debt_.guardEpoch,record.entry.nowNs,
                record.exit.nowNs,d.deadlineNs,debt_.loaded,debt_.reserve,debt_.capacity},record.exit.nowNs);
            if(!ready_&&policy_.Outcome()){
                const auto& result=*policy_.Outcome();
                if(const auto* release=std::get_if<interaction::WeaponCycleIdleDebtRelease>(&result.release))
                    ready_=interaction::WeaponCycleIdleDebtReady{*release,*std::max_element(result.native.second.begin(),result.native.second.end()),
                        result.native.observedNs,result.native.deadlineNs,true,true};
            }
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
           !CycleConvergenceRestore(record,scope_.identity,b,m95_,empty_)||
           record.exit.boundary->loaded!=debt_.loaded||record.exit.boundary->reserve!=debt_.reserve){LoseGuard();return false;}
        last_[b]=record.id;
        const auto& before=record.entry.boundary;const auto& after=*record.exit.boundary;
        if(before.current!=2||before.next!=2||before.timer!=0||after.current!=2||after.next!=2||after.timer!=0){
            currentIdle_={};policy_.NativeBusy(scope_,record.exit.nowNs);grant_.reset();}
        return true; // A Restore itself never supplies an idle vote.
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
    bool Acknowledge(const interaction::WeaponCycleIdleDebtReady& ready,std::int64_t now)noexcept {
        if(!active_||Uncertain()||!ready_||ready_->release!=ready.release||ready_->sequence!=ready.sequence||
           ready_->observedNs!=ready.observedNs||ready_->deadlineNs!=ready.deadlineNs||
           ready_->nativeReady!=ready.nativeReady||ready_->unchangedAmmunition!=ready.unchangedAmmunition||!policy_.Outcome()||
           std::any_of(open_.begin(),open_.end(),[](auto id){return id!=0;}))return false;
        for(const auto& idle:currentIdle_)if(!idle.first||now<idle.first||now>=idle.second)return false;
        if(!policy_.Acknowledge(*policy_.Outcome()))return false;ready_.reset();return true;
    }
    void LoseGuard()noexcept {if(active_)policy_.LoseGuard(scope_);currentIdle_={};grant_.reset();ready_.reset();}
    bool Active()const noexcept{return active_;}
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
    std::array<std::uint64_t,3> open_{},last_{};
    std::array<std::pair<std::int64_t,std::int64_t>,3> currentIdle_{};
    std::optional<interaction::WeaponCycleRecoveryGrant> grant_;
    std::optional<interaction::WeaponCycleIdleDebtReady> ready_;
    std::uint64_t guardedUpdates_=0,guardedSteps_=0;
};
}
