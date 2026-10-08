#include "fvr/interaction/WeaponCycleConvergence.h"
#include <iostream>
#define CHECK(value) do {if(!(value)){std::cerr<<__func__<<":"<<__LINE__<<" " #value "\n";return false;}}while(false)
using namespace fvr::interaction;
constexpr std::int64_t Ms=1000000;
struct Scope {
    std::array<std::uint64_t,6> owners{1,2,3,4,5,6};
    std::array<std::uint64_t,3> copies{7,8,9};
    std::array<std::uint8_t,16> config{1,2,3};
    bool operator==(const Scope&)const=default;
};
struct Fixture {
    Scope scope{};
    WeaponCycleDebt debt{{1,2,3,4},{5,3},{6,3},7,8,9,4,45,5};
    WeaponCycleConvergence<Scope> policy;
    std::uint64_t invocation=100;
    std::int64_t now=110*Ms;
    Fixture(int loaded=4){debt.loaded=loaded;policy.Begin(scope,debt,Ms);}
    WeaponCycleRelease Release()const {
        return {{debt.owner,debt.item,debt.mechanism,debt.cycle,debt.shot,10,100*Ms,180*Ms,true},11,12,100*Ms,180*Ms};
    }
    WeaponCycleIdleUpdate Idle(unsigned branch,std::int64_t begin){
        return {branch,++invocation,debt.guardEpoch,begin,begin+Ms,begin+81*Ms,debt.loaded,debt.reserve,debt.capacity};
    }
    bool Round(std::int64_t begin,std::array<unsigned,3> order={0,1,2}){
        for(auto branch:order){auto r=Idle(branch,begin);now=r.endNs;if(!policy.ObserveIdle(scope,r,now))return false;begin+=2*Ms;}
        return true;
    }
    HandInteractionSample Neutral()const {
        return {debt.owner,100,now+Ms,now+81*Ms,now+Ms,true,{true,true},{true,false}};
    }
};
bool TwoRoundsAreIndependentOfPredictionAndBranchOrder(){
    std::array<unsigned,3> order{0,1,2};unsigned cases=0;
    do {for(int loaded:{0,4}){
        Fixture f(loaded);CHECK(f.policy.Submit(f.scope,f.Release(),f.now));
        CHECK(f.Round(120*Ms,order));CHECK(!f.policy.Outcome()&&f.policy.BlocksFire());
        CHECK(f.Round(140*Ms,order));CHECK(f.policy.Outcome()&&f.policy.BlocksFire());
        const auto saved=*f.policy.Outcome();CHECK(saved.native.debt.loaded==loaded);
        CHECK(f.policy.Acknowledge(saved)&&!f.policy.BlocksFire());
        // Acknowledgement settles this immutable transaction, never a new item.
        CHECK(!f.policy.Begin(f.scope,f.debt,200*Ms));++cases;
    }}while(std::next_permutation(order.begin(),order.end()));CHECK(cases==12);return true;
}
bool CopiedReadyOrOneBranchCannotComplete(){
    Fixture f;CHECK(f.policy.Submit(f.scope,f.Release(),f.now));
    for(unsigned n=0;n<30;++n){auto r=f.Idle(0,(120+n*2)*Ms);CHECK(f.policy.ObserveIdle(f.scope,r,r.endNs));}
    CHECK(!f.policy.Outcome()&&f.policy.BlocksFire());
    // Copied snapshots and local Commit phases have no API capable of voting.
    CHECK(f.Round(190*Ms));CHECK(!f.policy.Outcome());CHECK(f.Round(210*Ms));CHECK(f.policy.Outcome());return true;
}
bool ExpiredFirstRoundCannotJoinFreshSecond(){
    Fixture f;CHECK(f.policy.Submit(f.scope,f.Release(),f.now));CHECK(f.Round(120*Ms));
    CHECK(f.Round(400*Ms));CHECK(!f.policy.Outcome());CHECK(f.Round(420*Ms));CHECK(f.policy.Outcome());return true;
}
bool HoldsDrainAfterQueuedPhysicalSubmission(){
    Fixture f;CHECK(f.policy.BeginHold(f.scope,0,1,105*Ms,150*Ms));
    // A different branch may enter earlier while its policy callback arrives later.
    CHECK(f.policy.BeginHold(f.scope,1,2,104*Ms,150*Ms));
    CHECK(f.policy.Submit(f.scope,f.Release(),110*Ms));
    CHECK(!f.policy.BeginHold(f.scope,2,3,111*Ms,151*Ms));
    auto premature=f.Idle(2,112*Ms);CHECK(!f.policy.ObserveIdle(f.scope,premature,premature.endNs));
    CHECK(f.policy.RestoreHold(f.scope,0,1,113*Ms,true));CHECK(f.policy.RestoreHold(f.scope,1,2,114*Ms,true));
    CHECK(!f.policy.ObserveIdle(f.scope,premature,115*Ms));
    CHECK(f.Round(120*Ms));CHECK(f.Round(140*Ms));CHECK(f.policy.Outcome());return true;
}
bool WrongAndUnrestoredHoldReceiptsCannotComplete(){
    for(unsigned variant=0;variant<3;++variant){Fixture f;CHECK(f.policy.BeginHold(f.scope,0,1,105*Ms,150*Ms));
        CHECK(f.policy.Submit(f.scope,f.Release(),110*Ms));
        CHECK(!f.policy.RestoreHold(f.scope,0,variant==0?2:1,variant==1?150*Ms:115*Ms,variant!=2));
        CHECK(!f.Round(160*Ms));CHECK(!f.policy.Outcome()&&f.policy.BlocksFire());
        CHECK(f.policy.Pending()&&std::get<WeaponCycleRelease>(*f.policy.Pending())==f.Release());
    }return true;
}
bool TrackingLossRetainsSubmittedOutcomeAndItsOriginalBounds(){
    Fixture f;const auto release=f.Release();CHECK(f.policy.Submit(f.scope,release,110*Ms));
    f.policy.Interrupt(f.scope,115*Ms);CHECK(f.Round(120*Ms));CHECK(f.Round(140*Ms));
    const auto saved=*f.policy.Outcome();f.policy.Interrupt(f.scope,60000*Ms);
    CHECK(f.policy.Outcome()->native.observedNs==saved.native.observedNs);
    CHECK(std::get<WeaponCycleRelease>(f.policy.Outcome()->release)==release);
    auto changed=saved;changed.native.deadlineNs++;CHECK(!f.policy.Acknowledge(changed));
    CHECK(f.policy.Acknowledge(saved));return true;
}
bool IdleAfterLongInterruptionDoesNotEraseUnfinishedGesture(){
    Fixture f;f.policy.Interrupt(f.scope,60000*Ms);CHECK(f.Round(60010*Ms));CHECK(f.Round(60030*Ms));
    CHECK(f.policy.Recovery(f.now)&&!f.policy.Outcome()&&f.policy.BlocksFire());
    auto neutral=f.Neutral();const auto grant=f.policy.ArmRecovery(f.scope,neutral);CHECK(grant);
    // Fresh convergence while the real gesture would run cannot become Ready.
    CHECK(f.Round(62000*Ms));CHECK(f.Round(62020*Ms));CHECK(!f.policy.Outcome());
    const auto authority=f.policy.RecoveryAuthority(f.scope,*grant,f.now);CHECK(authority);
    WeaponCycleRecoveredRelease stroke{*authority,55,101,f.now+Ms,authority->deadlineNs};
    CHECK(f.policy.SubmitRecovered(f.scope,stroke,f.now+2*Ms));CHECK(!f.policy.Outcome());
    CHECK(f.Round(62050*Ms));CHECK(f.Round(62070*Ms));CHECK(f.policy.Outcome());
    CHECK(std::get<WeaponCycleRecoveredRelease>(f.policy.Outcome()->release)==stroke);
    CHECK(f.policy.Outcome()->native.debt==f.debt);return true;
}
bool RecoveryRequiresNewNeutralAndCurrentOriginalEvidence(){
    for(unsigned variant=0;variant<5;++variant){Fixture f;CHECK(f.Round(120*Ms));CHECK(f.Round(140*Ms));auto n=f.Neutral();
        if(variant==0)n.released[0]=false;
        if(variant==1)n.released[1]=true;
        if(variant==2)n.tracked[1]=false;
        if(variant==3)n.observedNs-=20*Ms;
        if(variant==4)n.nowNs=n.deadlineNs;
        CHECK(!f.policy.ArmRecovery(f.scope,n));CHECK(!f.policy.Outcome());
    }
    Fixture f;CHECK(f.Round(120*Ms));CHECK(f.Round(140*Ms));auto grant=f.policy.ArmRecovery(f.scope,f.Neutral());CHECK(grant);
    CHECK(f.Round(160*Ms));CHECK(f.Round(180*Ms));
    const auto authority=f.policy.RecoveryAuthority(f.scope,*grant,f.now);CHECK(authority);
    WeaponCycleRecoveredRelease stroke{*authority,55,101,190*Ms,authority->deadlineNs};
    f.policy.Interrupt(f.scope,187*Ms);CHECK(f.Round(190*Ms));CHECK(f.Round(200*Ms));
    CHECK(!f.policy.SubmitRecovered(f.scope,stroke,200*Ms));CHECK(f.policy.BlocksFire());return true;
}
bool ExpiredRecoveryOfferCannotRenewOldNativeOrInputEvidence(){
    Fixture f;CHECK(f.Round(120*Ms));CHECK(f.Round(140*Ms));const auto offer=*f.policy.Recovery(f.now);
    CHECK(!f.policy.Recovery(offer.deadlineNs));
    auto neutral=f.Neutral();neutral.nowNs=offer.deadlineNs;CHECK(!f.policy.ArmRecovery(f.scope,neutral));
    CHECK(f.policy.BlocksFire()&&!f.policy.Outcome());return true;
}
bool AmmunitionAndGuardContradictionsRetainUncertainty(){
    for(unsigned variant=0;variant<4;++variant){Fixture f;const auto release=f.Release();CHECK(f.policy.Submit(f.scope,release,110*Ms));
        auto r=f.Idle(0,120*Ms);
        if(variant==0)++r.loaded;if(variant==1)--r.reserve;if(variant==2)++r.capacity;
        if(variant==3)f.policy.LoseGuard(f.scope);
        CHECK(!f.policy.ObserveIdle(f.scope,r,r.endNs));CHECK(f.policy.Phase()==WeaponCycleConvergencePhase::Uncertain);
        CHECK(f.policy.BlocksFire()&&!f.policy.Outcome()&&f.policy.Pending());
        CHECK(!f.policy.Submit(f.scope,release,120*Ms));CHECK(!f.policy.Begin(f.scope,f.debt,130*Ms));
    }return true;
}
bool NewOwnerOrConfigurationCannotBorrowOrRetireOldDebt(){
    for(unsigned variant=0;variant<3;++variant){Fixture f;Scope foreign=f.scope;
        if(variant==0)foreign.owners[0]++;if(variant==1)foreign.copies[1]++;if(variant==2)foreign.config[15]++;
        CHECK(!f.policy.Begin(foreign,f.debt,200*Ms));CHECK(!f.policy.Submit(foreign,f.Release(),110*Ms));
        auto r=f.Idle(0,120*Ms);CHECK(!f.policy.ObserveIdle(foreign,r,r.endNs));
        CHECK(f.policy.Phase()==WeaponCycleConvergencePhase::Debt&&f.policy.BlocksFire());
        CHECK(f.Round(140*Ms));CHECK(f.Round(160*Ms));CHECK(f.policy.Recovery(f.now));
    }return true;
}
bool ReplayedOrOverlappingRoundsCannotGrantReadiness(){
    Fixture f;CHECK(f.policy.Submit(f.scope,f.Release(),110*Ms));auto r=f.Idle(0,120*Ms);
    CHECK(f.policy.ObserveIdle(f.scope,r,r.endNs));CHECK(!f.policy.ObserveIdle(f.scope,r,r.endNs));
    r.branch=1;CHECK(!f.policy.ObserveIdle(f.scope,r,r.endNs));
    CHECK(f.Round(140*Ms));auto old=f.Idle(0,142*Ms);CHECK(!f.policy.ObserveIdle(f.scope,old,150*Ms));
    CHECK(!f.policy.Outcome());CHECK(f.Round(160*Ms));CHECK(f.policy.Outcome());return true;
}
bool GuardTokenFreshnessAndSkewCannotBeRestamped(){
    for(unsigned variant=0;variant<5;++variant){Fixture f;CHECK(f.policy.Submit(f.scope,f.Release(),110*Ms));auto r=f.Idle(0,120*Ms);
        if(variant==0)r.guardEpoch++;if(variant==1)r.beginNs=109*Ms;if(variant==2)r.endNs=r.beginNs;
        if(variant==3)r.deadlineNs=r.endNs;if(variant==4)r.deadlineNs=r.endNs+101*Ms;
        CHECK(!f.policy.ObserveIdle(f.scope,r,125*Ms));CHECK(!f.policy.Outcome());
    }
    Fixture f;CHECK(f.policy.Submit(f.scope,f.Release(),110*Ms));auto a=f.Idle(0,120*Ms),b=f.Idle(1,121*Ms),c=f.Idle(2,180*Ms);
    CHECK(f.policy.ObserveIdle(f.scope,a,a.endNs));CHECK(f.policy.ObserveIdle(f.scope,b,b.endNs));CHECK(f.policy.ObserveIdle(f.scope,c,c.endNs));
    CHECK(!f.policy.Outcome());CHECK(f.Round(190*Ms));CHECK(!f.policy.Outcome());
    // The retained fresh branch2 sample is the start of the next round; it
    // cannot reuse the prior round's branch2 invocation.
    a=f.Idle(0,210*Ms);b=f.Idle(1,212*Ms);
    CHECK(f.policy.ObserveIdle(f.scope,a,a.endNs));CHECK(!f.policy.Outcome());
    CHECK(f.policy.ObserveIdle(f.scope,b,b.endNs));CHECK(f.policy.Outcome());return true;
}
bool IssuedRecoveryAuthorityCannotBeInventedOrExtended(){
    for(unsigned mutation=0;mutation<8;++mutation){Fixture f;CHECK(f.Round(120*Ms));CHECK(f.Round(140*Ms));
        auto grant=f.policy.ArmRecovery(f.scope,f.Neutral());CHECK(grant);
        CHECK(f.Round(160*Ms));CHECK(f.Round(180*Ms));
        auto authority=f.policy.RecoveryAuthority(f.scope,*grant,f.now);CHECK(authority);
        WeaponCycleRecoveredRelease release{*authority,55,101,190*Ms,authority->deadlineNs};
        switch(mutation){case 0:++release.cycle.sequence;break;case 1:++release.cycle.grant.nonce;break;
        case 2:++release.cycle.first[0];break;case 3:++release.cycle.second[2];break;
        case 4:++release.deadlineNs;break;case 5:++release.cycle.grant.debt.shot;break;
        case 6:release.inputSequence=grant->neutral.sequence;break;case 7:release.observedNs=grant->neutral.observedNs;break;}
        CHECK(!f.policy.SubmitRecovered(f.scope,release,191*Ms));CHECK(f.policy.BlocksFire()&&!f.policy.Pending());
    }return true;
}
int main(){
    if(!IssuedRecoveryAuthorityCannotBeInventedOrExtended()||!TwoRoundsAreIndependentOfPredictionAndBranchOrder()||!CopiedReadyOrOneBranchCannotComplete()||
       !ExpiredFirstRoundCannotJoinFreshSecond()||!HoldsDrainAfterQueuedPhysicalSubmission()||
       !WrongAndUnrestoredHoldReceiptsCannotComplete()||!TrackingLossRetainsSubmittedOutcomeAndItsOriginalBounds()||
       !IdleAfterLongInterruptionDoesNotEraseUnfinishedGesture()||!RecoveryRequiresNewNeutralAndCurrentOriginalEvidence()||
       !ExpiredRecoveryOfferCannotRenewOldNativeOrInputEvidence()||!AmmunitionAndGuardContradictionsRetainUncertainty()||
       !NewOwnerOrConfigurationCannotBorrowOrRetireOldDebt()||!ReplayedOrOverlappingRoundsCannotGrantReadiness()||
       !GuardTokenFreshnessAndSkewCannotBeRestamped())return 1;
    std::cout<<"14 convergence/recovery groups passed; evidence adapters mocked; no native admission\n";
}
