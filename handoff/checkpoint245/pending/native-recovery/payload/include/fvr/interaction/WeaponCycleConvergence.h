#pragma once
#include "fvr/interaction/WeaponCycle.h"
#include <algorithm>
#include <variant>

namespace fvr::interaction {
// Candidate policy, not native admission. Scope is an adapter's EXACT structural
// owner/configuration identity, compared by value (not a name or lossy hash).
// The adapter must retain a continuously enforced fire/ammunition-operation
// guard for guardEpoch. Losing that guarantee calls LoseGuard, never Begin.
// Only construct after validating the original own Update, exact native scope,
// neutral context and restoration, and idle BEFORE AND AFTER with zero timer.
// A copied prediction snapshot, Commit, counter, or phase label is not this type.
struct WeaponCycleIdleUpdate {
    unsigned branch=3;
    std::uint64_t invocation=0,guardEpoch=0;
    std::int64_t beginNs=0,endNs=0,deadlineNs=0;
    int loaded=0,reserve=0,capacity=0;
};
struct WeaponCycleConvergedIdle {
    WeaponCycleDebt debt{};
    std::array<std::uint64_t,3> first{},second{};
    std::int64_t observedNs=0,deadlineNs=0;
};
// This is deliberately NOT WeaponCycleLease{held=true}. Native idle never
// masquerades as a frozen native cycling boundary. The physical adapters bind
// this permit to the same ordered gesture/contact/hand policy.
using WeaponCycleRecoveredRelease=WeaponCycleIdleDebtRelease;
using WeaponCycleConvergenceRelease=std::variant<WeaponCycleRelease,WeaponCycleRecoveredRelease>;
struct WeaponCycleConvergenceOutcome {
    WeaponCycleConvergenceRelease release{};
    WeaponCycleConvergedIdle native{};
};
enum class WeaponCycleConvergencePhase {Empty,Debt,Released,Ready,Uncertain};

template<class Scope> class WeaponCycleConvergence {
public:
    static constexpr std::int64_t MaximumFreshNs=100000000,MaximumRoundSpanNs=50000000;
    bool Begin(const Scope& scope,const WeaponCycleDebt& debt,std::int64_t observed)noexcept {
        if(phase_!=WeaponCycleConvergencePhase::Empty||!weapon_cycle_detail::Owner(debt.owner)||
           !weapon_cycle_detail::Key(debt.item)||!weapon_cycle_detail::Key(debt.mechanism)||
           !debt.cycle||!debt.shot||!debt.guardEpoch||debt.loaded<0||debt.reserve<0||
           debt.capacity<=0||debt.loaded>=debt.capacity||observed<=0)return false;
        scope_=scope;debt_=debt;began_=floor_=observed;phase_=WeaponCycleConvergencePhase::Debt;return true;
    }
    bool BeginHold(const Scope& scope,unsigned branch,std::uint64_t invocation,
        std::int64_t begin,std::int64_t deadline)noexcept {
        if(!Bound(scope)||phase_!=WeaponCycleConvergencePhase::Debt||branch>=3||holds_[branch]||
           !invocation||invocation<=last_[branch]||begin<began_||deadline<=begin||deadline-begin>MaximumFreshNs)return false;
        holds_[branch]={invocation,begin,deadline};last_[branch]=invocation;
        ClearEvidence(begin);grant_.reset();return true;
    }
    bool RestoreHold(const Scope& scope,unsigned branch,std::uint64_t invocation,std::int64_t end,
        bool exactOriginalRestored)noexcept {
        if(!Bound(scope)||branch>=3||!holds_[branch]||holds_[branch]->invocation!=invocation)return false;
        const auto receipt=*holds_[branch];holds_[branch].reset();
        if(!exactOriginalRestored||end<receipt.begin||end>=receipt.deadline){LoseGuard(scope);return false;}
        // An original call already admitted before submission may finish later.
        // This drain point invalidates all earlier idle observations.
        ClearEvidence(end);return true;
    }
    bool Submit(const Scope& scope,const WeaponCycleRelease& release,std::int64_t accepted)noexcept {
        if(release_){return Bound(scope)&&phase_!=WeaponCycleConvergencePhase::Uncertain&&*release_==WeaponCycleConvergenceRelease{release};}
        if(!Bound(scope)||phase_!=WeaponCycleConvergencePhase::Debt||!Matches(release.cycle)||
           !weapon_cycle_detail::Lease(release.cycle,accepted)||!release.request||!release.inputSequence||
           !weapon_cycle_detail::Window(release.observedNs,release.deadlineNs,accepted)||
           release.deadlineNs>release.cycle.deadlineNs)return false;
        release_=release;phase_=WeaponCycleConvergencePhase::Released;grant_.reset();ClearEvidence(accepted);return true;
    }
    // Called for an interruption of physical custody. It cannot discard a
    // submitted release or terminal outcome and does not expire retained debt.
    void Interrupt(const Scope& scope,std::int64_t now)noexcept {
        if(!Bound(scope))return;grant_.reset();
        if(phase_==WeaponCycleConvergencePhase::Debt){issued_={};ClearEvidence(now);}
    }
    std::optional<WeaponCycleRecoveryGrant> ArmRecovery(const Scope& scope,
        const HandInteractionSample& neutral,WeaponCycleHandAssignment hands={})noexcept {
        if(!Bound(scope)||phase_!=WeaponCycleConvergencePhase::Debt||!Recovery(neutral.nowNs)||
           !weapon_cycle_detail::Hands(hands)||neutral.owner!=debt_.owner||!neutral.sequence||
           !neutral.focused||!neutral.tracked[0]||!neutral.tracked[1]||
           !neutral.released[weapon_cycle_detail::HandIndex(hands.mechanism)]||
           neutral.released[weapon_cycle_detail::HandIndex(hands.gun)]||
           neutral.observedNs<idle_->observedNs||
           !weapon_cycle_detail::Window(neutral.observedNs,neutral.deadlineNs,neutral.nowNs)||nonce_==UINT64_MAX)return {};
        if(grant_)return grant_;
        grant_=WeaponCycleRecoveryGrant{debt_,neutral,++nonce_};return grant_;
    }
    std::optional<WeaponCycleIdleDebtLease> RecoveryAuthority(const Scope& scope,
        const WeaponCycleRecoveryGrant& grant,std::int64_t now)noexcept {
        if(!Bound(scope)||!grant_||grant!=*grant_||!Recovery(now)||grant.neutral.observedNs>idle_->observedNs)return {};
        const auto sequence=*std::max_element(idle_->second.begin(),idle_->second.end());
        WeaponCycleIdleDebtLease authority{debt_.owner,debt_.item,debt_.mechanism,debt_.cycle,debt_.shot,
            sequence,idle_->observedNs,idle_->deadlineNs,grant,idle_->first,idle_->second};
        if(!weapon_cycle_detail::Lease(authority,now))return {};
        if(std::find(issued_.begin(),issued_.end(),authority)==issued_.end()){
            issued_[issueAt_]=authority;issueAt_=(issueAt_+1)%issued_.size();}
        return authority;
    }
    bool SubmitRecovered(const Scope& scope,const WeaponCycleRecoveredRelease& release,std::int64_t accepted)noexcept {
        if(release_){return Bound(scope)&&phase_!=WeaponCycleConvergencePhase::Uncertain&&*release_==WeaponCycleConvergenceRelease{release};}
        if(!Bound(scope)||phase_!=WeaponCycleConvergencePhase::Debt||!grant_||release.cycle.grant!=*grant_||
           !weapon_cycle_detail::Lease(release.cycle,accepted)||
           std::find(issued_.begin(),issued_.end(),release.cycle)==issued_.end()||
           !Recovery(accepted)||!release.request||release.inputSequence<=grant_->neutral.sequence||
           release.observedNs<=grant_->neutral.observedNs||
           !weapon_cycle_detail::Window(release.observedNs,release.deadlineNs,accepted)||
           release.deadlineNs>release.cycle.deadlineNs)return false;
        release_=release;phase_=WeaponCycleConvergencePhase::Released;grant_.reset();ClearEvidence(accepted);return true;
    }
    bool ObserveIdle(const Scope& scope,const WeaponCycleIdleUpdate& receipt,std::int64_t now)noexcept {
        if(!Bound(scope)||(phase_!=WeaponCycleConvergencePhase::Debt&&phase_!=WeaponCycleConvergencePhase::Released)||
           receipt.branch>=3||!receipt.invocation||receipt.invocation<=last_[receipt.branch]||
           receipt.guardEpoch!=debt_.guardEpoch||receipt.beginNs<floor_||receipt.endNs<=receipt.beginNs||
           receipt.endNs>now||receipt.deadlineNs<=now||receipt.deadlineNs-receipt.endNs>MaximumFreshNs||
           std::any_of(holds_.begin(),holds_.end(),[](const auto& value){return value.has_value();}))return false;
        if(receipt.loaded!=debt_.loaded||receipt.reserve!=debt_.reserve||receipt.capacity!=debt_.capacity){LoseGuard(scope);return false;}
        if(std::find(last_.begin(),last_.end(),receipt.invocation)!=last_.end())return false;
        last_[receipt.branch]=receipt.invocation;round_[receipt.branch]=receipt;
        if(std::any_of(round_.begin(),round_.end(),[](const auto& value){return !value;}))return true;
        auto first=round_[0]->beginNs,last=round_[0]->endNs,deadline=round_[0]->deadlineNs;
        for(const auto& value:round_){first=std::min(first,value->beginNs);last=std::max(last,value->endNs);deadline=std::min(deadline,value->deadlineNs);}
        if(now>=deadline||last-first>MaximumRoundSpanNs){
            for(auto& value:round_)if(value->deadlineNs<=now||last-value->beginNs>MaximumRoundSpanNs)value.reset();
            return true;
        }
        std::array<std::uint64_t,3> invocations{};
        for(unsigned b=0;b<3;++b)invocations[b]=round_[b]->invocation;
        if(!firstRound_||now>=firstDeadline_){firstRound_=invocations;firstDeadline_=deadline;floor_=last;round_={};return true;}
        idle_=WeaponCycleConvergedIdle{debt_,*firstRound_,invocations,last,deadline};
        firstRound_=invocations;firstDeadline_=deadline;floor_=last;round_={};
        if(release_){outcome_=WeaponCycleConvergenceOutcome{*release_,*idle_};phase_=WeaponCycleConvergencePhase::Ready;}
        return true;
    }
    // A native busy observation is never idle. Invalidate outstanding voting
    // rounds and unsubmitted authority, but retain an exact submitted outcome.
    void NativeBusy(const Scope& scope,std::int64_t now)noexcept {
        if(!Bound(scope))return;
        if(phase_==WeaponCycleConvergencePhase::Debt){grant_.reset();issued_={};}
        if(phase_==WeaponCycleConvergencePhase::Debt||phase_==WeaponCycleConvergencePhase::Released)ClearEvidence(now);
    }
    void LoseGuard(const Scope& scope)noexcept {
        if(!Bound(scope))return;
        phase_=WeaponCycleConvergencePhase::Uncertain;idle_.reset();outcome_.reset();grant_.reset();
        // Retain the exact debt/release and outstanding holds for diagnostics
        // and restoration. Rebinding another selection cannot clear them.
    }
    std::optional<WeaponCycleConvergedIdle> Recovery(std::int64_t now)const noexcept {
        if(phase_!=WeaponCycleConvergencePhase::Debt||!idle_||
           now<idle_->observedNs||now>=idle_->deadlineNs)return {};
        return idle_;
    }
    const std::optional<WeaponCycleConvergenceOutcome>& Outcome()const noexcept{return outcome_;}
    const std::optional<WeaponCycleConvergenceRelease>& Pending()const noexcept{return release_;}
    WeaponCycleConvergencePhase Phase()const noexcept{return phase_;}
    bool BlocksFire()const noexcept{return phase_!=WeaponCycleConvergencePhase::Empty&&(!outcome_||!acknowledged_);}
    bool Acknowledge(const WeaponCycleConvergenceOutcome& outcome)noexcept {
        if(!outcome_||outcome.release!=outcome_->release||outcome.native.debt!=outcome_->native.debt||
           outcome.native.first!=outcome_->native.first||outcome.native.second!=outcome_->native.second||
           outcome.native.observedNs!=outcome_->native.observedNs||outcome.native.deadlineNs!=outcome_->native.deadlineNs)return false;
        acknowledged_=true;return true;
    }
private:
    struct Hold {std::uint64_t invocation;std::int64_t begin,deadline;};
    bool Bound(const Scope& scope)const noexcept{return phase_!=WeaponCycleConvergencePhase::Empty&&scope_==scope;}
    bool Matches(const WeaponCycleLease& cycle)const noexcept {
        return cycle.owner==debt_.owner&&cycle.item==debt_.item&&cycle.mechanism==debt_.mechanism&&cycle.cycle==debt_.cycle&&cycle.shot==debt_.shot;
    }
    void ClearEvidence(std::int64_t now)noexcept{floor_=std::max(floor_,now);round_={};firstRound_.reset();firstDeadline_=0;idle_.reset();}
    Scope scope_{};WeaponCycleDebt debt_{};
    WeaponCycleConvergencePhase phase_=WeaponCycleConvergencePhase::Empty;
    std::array<std::optional<Hold>,3> holds_{};
    std::array<std::optional<WeaponCycleIdleUpdate>,3> round_{};
    std::array<std::uint64_t,3> last_{};
    std::optional<std::array<std::uint64_t,3>> firstRound_;
    std::optional<WeaponCycleConvergedIdle> idle_;
    std::optional<WeaponCycleRecoveryGrant> grant_;
    std::array<WeaponCycleIdleDebtLease,32> issued_{};
    unsigned issueAt_=0;
    std::optional<WeaponCycleConvergenceRelease> release_;
    std::optional<WeaponCycleConvergenceOutcome> outcome_;
    std::int64_t began_=0,floor_=0,firstDeadline_=0;
    std::uint64_t nonce_=0;
    bool acknowledged_=false;
};
}
