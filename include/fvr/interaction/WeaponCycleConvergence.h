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
// Construct only while the adapter excludes new callbacks and has drained all
// callbacks admitted before this revision. This is not a caller's ready flag.
struct WeaponCycleReloadDrain {
    std::uint64_t revision=0;
    std::int64_t observedNs=0;
    bool operator==(const WeaponCycleReloadDrain&)const=default;
};
struct WeaponCycleReloadPermit {
    WeaponCycleReloadPause pause{};WeaponCycleDebt debt{};AmmoResourceContext resource{};
    WeaponCycleReloadDrain drain{};
    std::array<std::uint64_t,3> first{},second{};
    std::int64_t observedNs=0,deadlineNs=0;
    bool operator==(const WeaponCycleReloadPermit&)const=default;
};
struct WeaponCycleReloadCommand {
    WeaponCycleReloadPermit permit{};AmmunitionCommand command{};WeaponCycleReloadDrain drain{};
    bool operator==(const WeaponCycleReloadCommand&)const=default;
};
struct WeaponCycleReloadAmmoRebase {
    WeaponCycleReloadCommand dispatched{};AmmunitionReceipt receipt{};
    std::array<WeaponCycleIdleUpdate,3> ownUpdates{};
    WeaponCycleReloadDrain drain{};
    std::uint64_t guardEpoch=0;
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
           debt.capacity<=0||debt.loaded>=debt.capacity||debt.ammunitionRebase||observed<=0)return false;
        scope_=scope;debt_=debt;began_=floor_=observed;phase_=WeaponCycleConvergencePhase::Debt;return true;
    }
    bool BeginHold(const Scope& scope,unsigned branch,std::uint64_t invocation,
        std::int64_t begin,std::int64_t deadline)noexcept {
        if(!Bound(scope)||reloadPause_||phase_!=WeaponCycleConvergencePhase::Debt||branch>=3||holds_[branch]||
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
        if(!Bound(scope)||reloadPause_||phase_!=WeaponCycleConvergencePhase::Debt||!Matches(release.cycle)||
           !weapon_cycle_detail::Lease(release.cycle,accepted)||!release.request||!release.inputSequence||
           !weapon_cycle_detail::Window(release.observedNs,release.deadlineNs,accepted)||
           release.deadlineNs>release.cycle.deadlineNs)return false;
        release_=release;phase_=WeaponCycleConvergencePhase::Released;grant_.reset();ClearEvidence(accepted);return true;
    }
    // Called for an interruption of physical custody. It cannot discard a
    // submitted release or terminal outcome and does not expire retained debt.
    void Interrupt(const Scope& scope,std::int64_t now)noexcept {
        if(!Bound(scope))return;grant_.reset();
        if(phase_==WeaponCycleConvergencePhase::Debt){issued_={};reloadPermit_.reset();ClearEvidence(now);}
    }
    bool PrepareReload(const Scope& scope,const WeaponCycleReloadPause& pause,const AmmoResourceContext& resource,
        const WeaponCycleReloadDrain& drain,std::int64_t now)noexcept {
        if(!Bound(scope)||phase_!=WeaponCycleConvergencePhase::Debt||reloadPause_||release_||outcome_||
           pause.owner!=debt_.owner||pause.item!=debt_.item||pause.mechanism!=debt_.mechanism||
           pause.cycle!=debt_.cycle||pause.shot!=debt_.shot||!pause.pause||!pause.profile||!pause.revision||!pause.inputSequence||
           !std::isfinite(pause.travel)||pause.travel<=0||!std::isfinite(pause.rotation)||
           !weapon_cycle_detail::Window(pause.observedNs,pause.deadlineNs,now)||pause.pausedNs<pause.observedNs||
           pause.pausedNs>now||pause.cycleDeadlineNs<=pause.pausedNs||now>=pause.pauseDeadlineNs||
           pause.pauseDeadlineNs-pause.pausedNs>WeaponCycle::MaximumReloadPauseNs||
           !ResourceMatches(resource)||!Drain(drain,now)||drain.observedNs<pause.observedNs)return false;
        reloadPause_=pause;reloadResource_=resource;reloadDrain_=drain;
        grant_.reset();issued_={};ClearEvidence(drain.observedNs);return true;
    }
    std::optional<WeaponCycleReloadPermit> ReloadPermit(const Scope& scope,std::int64_t now)noexcept {
        if(!Bound(scope)||!reloadPause_||reloadCommand_||phase_!=WeaponCycleConvergencePhase::Debt||!Recovery(now)||
           now>=reloadPause_->pauseDeadlineNs)return {};
        WeaponCycleReloadPermit permit{*reloadPause_,debt_,reloadResource_,reloadDrain_,idle_->first,idle_->second,
            idle_->observedNs,std::min(idle_->deadlineNs,reloadPause_->pauseDeadlineNs)};
        reloadPermit_=permit;return permit;
    }
    bool BeginReloadCommand(const Scope& scope,const WeaponCycleReloadPermit& permit,const AmmunitionCommand& command,
        const WeaponCycleReloadDrain& drain,std::int64_t now)noexcept {
        if(!Bound(scope)||!reloadPause_||reloadCommand_||!reloadPermit_||permit!=*reloadPermit_||
           phase_!=WeaponCycleConvergencePhase::Debt||!Recovery(now)||!Drain(drain,now)||
           drain.revision<=reloadDrain_.revision||drain.observedNs<permit.observedNs||now>=permit.deadlineNs||
           !weapon_cycle_detail::MagazineCommand(command)||command.context!=reloadResource_||
           command.id<=appliedCommand_||command.before!=AmmunitionCounts{debt_.loaded,debt_.reserve,debt_.capacity}||
           command.requestedNs<permit.observedNs||command.requestedNs>now||now>=command.admissionDeadlineNs||
           command.sourceSequence<reloadPause_->inputSequence)return false;
        reloadCommand_=WeaponCycleReloadCommand{permit,command,drain};reloadDrain_=drain;reloadPermit_.reset();
        grant_.reset();issued_={};ClearEvidence(drain.observedNs);return true;
    }
    bool ApplyReloadReceipt(const Scope& scope,const WeaponCycleReloadAmmoRebase& rebase,std::int64_t now)noexcept {
        // A retained dispatched operation survives hand/selection interruption.
        // Its actual completion time, not the observer's later poll time, must
        // meet the original ledger deadline. Never roll back a completed write.
        if(!Bound(scope)||phase_!=WeaponCycleConvergencePhase::Debt||!reloadPause_||!reloadCommand_||
           rebase.dispatched!=*reloadCommand_||rebase.receipt.command!=reloadCommand_->command||
           rebase.receipt.command.id<=appliedCommand_||rebase.receipt.authorityInvocation<=appliedAuthorityInvocation_||!Drain(rebase.drain,now)||
           rebase.drain.revision<=reloadDrain_.revision||rebase.guardEpoch<=debt_.guardEpoch||
           rebase.receipt.completedNs>now||rebase.drain.observedNs<rebase.receipt.completedNs)return false;
        auto next=debt_;next.loaded=rebase.receipt.after.loaded;next.reserve=rebase.receipt.after.reserve;
        next.capacity=rebase.receipt.after.capacity;next.guardEpoch=rebase.guardEpoch;
        WeaponCycleAmmoRebaseProvenance proof{debt_.owner,debt_.item,debt_.mechanism,debt_.cycle,debt_.shot,
            reloadPause_->pause,debt_.guardEpoch,rebase.guardEpoch,rebase.drain.revision,rebase.receipt};
        for(unsigned b=0;b<3;++b){const auto& u=rebase.ownUpdates[b];
            if(u.branch!=b||!u.invocation||u.invocation<=last_[b]||u.guardEpoch!=debt_.guardEpoch||
               std::find(last_.begin(),last_.end(),u.invocation)!=last_.end()||
               u.beginNs<=0||u.endNs<=u.beginNs||u.endNs<rebase.receipt.beganNs||u.endNs>rebase.receipt.completedNs||
               u.deadlineNs<=u.endNs||u.deadlineNs-u.endNs>MaximumFreshNs||
               AmmunitionCounts{u.loaded,u.reserve,u.capacity}!=rebase.receipt.after)return false;
            proof.ownUpdates[b]=u.invocation;
        }
        next.ammunitionRebase=proof;if(!weapon_cycle_detail::AmmoRebase(next))return false;
        for(unsigned b=0;b<3;++b)last_[b]=proof.ownUpdates[b];
        debt_=next;appliedCommand_=rebase.receipt.command.id;appliedAuthorityInvocation_=rebase.receipt.authorityInvocation;reloadDrain_=rebase.drain;
        lastReloadReceipt_=rebase.receipt;reloadCommand_.reset();reloadPermit_.reset();grant_.reset();issued_={};
        ClearEvidence(std::max(rebase.drain.observedNs,now));return true;
    }
    bool ResumeReload(const Scope& scope,const WeaponCycleIdleDebtLease& authority,std::int64_t now)noexcept {
        if(!Bound(scope)||!reloadPause_||reloadCommand_||!lastReloadReceipt_||!grant_||authority.grant!=*grant_||
           !weapon_cycle_detail::Lease(authority,now)||!Recovery(now)||
           std::find(issued_.begin(),issued_.end(),authority)==issued_.end()||
           !debt_.ammunitionRebase||debt_.ammunitionRebase->pause!=reloadPause_->pause||now>=reloadPause_->pauseDeadlineNs)return false;
        reloadPause_.reset();reloadPermit_.reset();return true;
    }
    const auto& ReloadPause()const noexcept{return reloadPause_;}
    const auto& ReloadCommand()const noexcept{return reloadCommand_;}
    const auto& LastReloadReceipt()const noexcept{return lastReloadReceipt_;}
    const WeaponCycleDebt& Debt()const noexcept{return debt_;}
    // Adapter verifies that only the tracking coordinate epoch changed and
    // excludes/drains native callbacks. Preserve the physical debt and callback
    // high-water marks, but invalidate every old pose/grant/idle observation.
    bool RebaseTracking(const Scope& scope,const Scope& replacement,
        HandInteractionOwner owner,std::int64_t now)noexcept {
        auto previous=owner;previous.space=debt_.owner.space;
        if(!Bound(scope)||reloadPause_||phase_!=WeaponCycleConvergencePhase::Debt||release_||outcome_||
           previous!=debt_.owner||owner.space<=debt_.owner.space||now<floor_||
           std::any_of(holds_.begin(),holds_.end(),[](const auto& h){return h.has_value();}))return false;
        scope_=replacement;debt_.owner=owner;grant_.reset();issued_={};ClearEvidence(now);return true;
    }
    std::optional<WeaponCycleRecoveryGrant> ArmRecovery(const Scope& scope,
        const HandInteractionSample& neutral,WeaponCycleHandAssignment hands={})noexcept {
        if(!Bound(scope)||reloadCommand_||(reloadPause_&&!lastReloadReceipt_)||phase_!=WeaponCycleConvergencePhase::Debt||!Recovery(neutral.nowNs)||
           !weapon_cycle_detail::Hands(hands)||neutral.owner!=debt_.owner||!neutral.sequence||
           !neutral.focused||!neutral.tracked[0]||!neutral.tracked[1]||
           !neutral.released[weapon_cycle_detail::HandIndex(hands.mechanism)]||
           neutral.released[weapon_cycle_detail::HandIndex(hands.gun)]||
           !idleEstablishedNs_||neutral.observedNs<idleEstablishedNs_||
           !weapon_cycle_detail::Window(neutral.observedNs,neutral.deadlineNs,neutral.nowNs)||nonce_==UINT64_MAX)return {};
        if(grant_)return grant_;
        grant_=WeaponCycleRecoveryGrant{debt_,neutral,++nonce_};return grant_;
    }
    std::optional<WeaponCycleIdleDebtLease> RecoveryAuthority(const Scope& scope,
        const WeaponCycleRecoveryGrant& grant,std::int64_t now)noexcept {
        if(!Bound(scope)||!grant_||grant!=*grant_||!Recovery(now)||
           grant.neutral.observedNs>idle_->observedNs||idleFirstBeginNs_<grant.neutral.observedNs)return {};
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
        if(!Bound(scope)||reloadPause_||phase_!=WeaponCycleConvergencePhase::Debt||!grant_||release.cycle.grant!=*grant_||
           !weapon_cycle_detail::Lease(release.cycle,accepted)||
           std::find(issued_.begin(),issued_.end(),release.cycle)==issued_.end()||
           !Recovery(accepted)||!release.request||release.inputSequence<=grant_->neutral.sequence||
           release.observedNs<=grant_->neutral.observedNs||
           !weapon_cycle_detail::Window(release.observedNs,release.deadlineNs,accepted)||
           release.deadlineNs>release.cycle.deadlineNs)return false;
        release_=release;phase_=WeaponCycleConvergencePhase::Released;grant_.reset();ClearEvidence(accepted);return true;
    }
    bool ObserveIdle(const Scope& scope,const WeaponCycleIdleUpdate& receipt,std::int64_t now)noexcept {
        if(!Bound(scope)||reloadCommand_||(phase_!=WeaponCycleConvergencePhase::Debt&&phase_!=WeaponCycleConvergencePhase::Released)||
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
        if(!firstRound_||now>=firstDeadline_){
            // A new aggregate after a proof gap must establish idle again
            // before another neutral packet can arm. A previously admitted
            // neutral remains bound to its original guard and source bounds;
            // it still needs two fresh post-neutral rounds for authority.
            idleEstablishedNs_=idleFirstBeginNs_=0;idle_.reset();
            firstRound_=invocations;firstRoundBeginNs_=first;firstDeadline_=deadline;floor_=last;round_={};return true;
        }
        // Control may process an unchanged original packet AFTER newer native
        // callbacks. Its neutral edge follows the first continuously proven
        // idle, not necessarily this newest refresh. Never restamp the packet.
        if(!idleEstablishedNs_)idleEstablishedNs_=last;
        idleFirstBeginNs_=firstRoundBeginNs_;
        idle_=WeaponCycleConvergedIdle{debt_,*firstRound_,invocations,last,deadline};
        firstRound_=invocations;firstRoundBeginNs_=first;firstDeadline_=deadline;floor_=last;round_={};
        if(release_){
            // A paid stroke retains its immutable original outcome. New idle
            // votes settle native prediction only; they never create a second
            // physical completion or renew the original receipt.
            if(!acknowledged_)outcome_=WeaponCycleConvergenceOutcome{*release_,*idle_};
            phase_=WeaponCycleConvergencePhase::Ready;
        }
        return true;
    }
    // A native busy observation is never idle. Invalidate outstanding voting
    // rounds and unsubmitted authority, but retain an exact submitted outcome.
    void NativeBusy(const Scope& scope,std::int64_t now)noexcept {
        if(!Bound(scope))return;
        // The adapter must prove an exact same-operation native replay. Keep
        // payment, but withdraw native readiness until two NEW guarded rounds.
        // An unacknowledged outcome retains its existing delivery semantics.
        if(phase_==WeaponCycleConvergencePhase::Ready&&acknowledged_)
            phase_=WeaponCycleConvergencePhase::Released;
        if(phase_==WeaponCycleConvergencePhase::Debt){grant_.reset();issued_={};}
        if(phase_==WeaponCycleConvergencePhase::Debt||phase_==WeaponCycleConvergencePhase::Released)ClearEvidence(now);
    }
    void LoseGuard(const Scope& scope)noexcept {
        if(!Bound(scope))return;
        phase_=WeaponCycleConvergencePhase::Uncertain;idle_.reset();outcome_.reset();grant_.reset();
        idleEstablishedNs_=idleFirstBeginNs_=0;
        // Retain the exact debt/release and outstanding holds for diagnostics
        // and restoration. Rebinding another selection cannot clear them.
    }
    std::optional<WeaponCycleConvergedIdle> Recovery(std::int64_t now)const noexcept {
        if(reloadCommand_||phase_!=WeaponCycleConvergencePhase::Debt||!idle_||
           now<idle_->observedNs||now>=idle_->deadlineNs)return {};
        return idle_;
    }
    const std::optional<WeaponCycleConvergenceOutcome>& Outcome()const noexcept{return outcome_;}
    const std::optional<WeaponCycleConvergenceRelease>& Pending()const noexcept{return release_;}
    WeaponCycleConvergencePhase Phase()const noexcept{return phase_;}
    bool Acknowledged()const noexcept{return acknowledged_;}
    bool BlocksFire()const noexcept{return phase_!=WeaponCycleConvergencePhase::Empty&&
        (phase_!=WeaponCycleConvergencePhase::Ready||!outcome_||!acknowledged_);}
    bool Acknowledge(const WeaponCycleConvergenceOutcome& outcome)noexcept {
        if(acknowledged_||phase_!=WeaponCycleConvergencePhase::Ready||!outcome_||outcome.release!=outcome_->release||outcome.native.debt!=outcome_->native.debt||
           outcome.native.first!=outcome_->native.first||outcome.native.second!=outcome_->native.second||
           outcome.native.observedNs!=outcome_->native.observedNs||outcome.native.deadlineNs!=outcome_->native.deadlineNs)return false;
        acknowledged_=true;return true;
    }
private:
    bool ResourceMatches(const AmmoResourceContext& c)const noexcept {
        // Inventory actor/equip keys and hand actor/equip keys may occupy
        // different namespaces. The adapter proves their structural join at Prepare;
        // every later command must match this retained resource context by value.
        return c.resource.actor&&c.resource.actorGeneration==debt_.owner.actorGeneration&&
            c.resource.weapon==debt_.item.id&&c.resource.weaponGeneration&&
            c.equipGeneration&&c.space==debt_.owner.space;
    }
    bool Drain(const WeaponCycleReloadDrain& drain,std::int64_t now)const noexcept {
        return drain.revision&&drain.observedNs>=floor_&&drain.observedNs<=now&&
            std::none_of(holds_.begin(),holds_.end(),[](const auto& h){return h.has_value();});
    }
    struct Hold {std::uint64_t invocation;std::int64_t begin,deadline;};
    bool Bound(const Scope& scope)const noexcept{return phase_!=WeaponCycleConvergencePhase::Empty&&scope_==scope;}
    bool Matches(const WeaponCycleLease& cycle)const noexcept {
        return cycle.owner==debt_.owner&&cycle.item==debt_.item&&cycle.mechanism==debt_.mechanism&&cycle.cycle==debt_.cycle&&cycle.shot==debt_.shot;
    }
    void ClearEvidence(std::int64_t now)noexcept{floor_=std::max(floor_,now);round_={};firstRound_.reset();
        firstDeadline_=firstRoundBeginNs_=idleEstablishedNs_=idleFirstBeginNs_=0;idle_.reset();}
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
    std::optional<WeaponCycleReloadPause> reloadPause_;
    AmmoResourceContext reloadResource_{};WeaponCycleReloadDrain reloadDrain_{};
    std::optional<WeaponCycleReloadPermit> reloadPermit_;
    std::optional<WeaponCycleReloadCommand> reloadCommand_;
    std::optional<AmmunitionReceipt> lastReloadReceipt_;
    std::uint64_t appliedCommand_=0,appliedAuthorityInvocation_=0;
    std::int64_t began_=0,floor_=0,firstDeadline_=0,firstRoundBeginNs_=0,idleEstablishedNs_=0,idleFirstBeginNs_=0;
    std::uint64_t nonce_=0;
    bool acknowledged_=false;
};
}
