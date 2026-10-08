#include "Test.h"
#include "fvr/interaction/WeaponActionGate.h"
#include "fvr/interaction/PhysicalWeaponCycle.h"
#include <cstdlib>

using namespace fvr::interaction;
namespace {
void Require(bool okay){if(!okay){std::fputs("fixture setup failed\n",stderr);std::abort();}}
fvr::math::Matrix4 Identity(){fvr::math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
WeaponMechanismDescriptor Descriptor(WeaponAction action=WeaponAction::Bolt){
    WeaponMechanismDescriptor d;d.id=1;d.revision=1;d.afterShot=d.afterEmptyFeed=action;d.tacticalRetainsChamber=true;
    d.feedPlan.stepCount=2;d.feedPlan.steps[0]={ReloadOperation::UnseatMagazine,1};d.feedPlan.steps[1]={ReloadOperation::SeatMagazine,1};
    d.feedPlan.maxSampleGapNs=100000000;d.feedPlan.ackTimeoutNs=100000000;d.feedPlan.transactionTimeoutNs=1000000000;return d;
}
struct Fixture {
    WeaponActionGate gate;
    PhysicalWeaponCycle physical;
    HandInteraction hands;
    WeaponCycleProfile profile;
    WeaponCycleLease lease,initialLease;
    WeaponActionEvidence evidence;
    PhysicalWeaponCycleSample input;
    PhysicalWeaponCycleResult result;
    std::optional<WeaponCycleRelease> submitted;
    std::uint64_t intent=0;
    int loaded=5,reserve=20,releases=0;
    Fixture(WeaponAction action=WeaponAction::Bolt,WeaponActionReadiness readiness=WeaponActionReadiness::SemanticChamber){
        // Synthetic geometry, deliberately not a BC2 weapon calibration.
        profile.id=9;profile.revision=1;profile.family=action==WeaponAction::Pump?WeaponCycleFamily::Pump:WeaponCycleFamily::Bolt;
        profile.closedContact=Identity();profile.axis={0,0,-1};profile.stroke=.10f;profile.rearTolerance=.003f;profile.frontTolerance=.003f;
        profile.contactRadius=.05f;profile.lateralTolerance=.02f;profile.maxStepMeters=.025f;
        profile.unlockRadians=action==WeaponAction::Pump?0:1.1f;profile.rotationTolerance=.10f;
        profile.endpointDwellNs=10000000;profile.maximumCycleNs=3000000000ll;
        input.source={{1,2,3,4},1,1000000000,1100000000,1000000000,true,{true,true},{true,false}};
        evidence={input.source.owner,{10,2},1,1,1,0,0,1000000000,1100000000,ChamberKnowledge::Occupied,
            ChamberKnowledge::Unknown,true,true};
        if(readiness==WeaponActionReadiness::NativeAfterShot)evidence.chamber=ChamberKnowledge::Unknown;
        input.rawContact=Identity();Require(gate.Bind(Descriptor(action),evidence,input.source.nowNs,readiness));
    }
    void Advance(){
        ++input.source.sequence;input.source.nowNs+=10000000;input.source.observedNs=input.source.nowNs;input.source.deadlineNs=input.source.nowNs+100000000;
        evidence.sequence=input.source.sequence;evidence.observedNs=input.source.observedNs;evidence.deadlineNs=input.source.deadlineNs;
        lease.sequence=evidence.sequence;lease.observedNs=evidence.observedNs;lease.deadlineNs=evidence.deadlineNs;
    }
    void Gun(){
        hands.Update(input.source);const auto old=hands.Current(InteractionHand::Right);
        if(old){Require(hands.Renew(input.source,old->token,{{7,1},input.source.sequence,input.source.deadlineNs,true}).accepted);input.gun=old->token;}
        else {const auto acquired=hands.Acquire(input.source,{input.source.owner,InteractionHand::Right,HandClaimKind::GunHold,evidence.item,
            {{7,1},input.source.sequence,input.source.deadlineNs,true},++intent,0});Require(acquired.accepted&&acquired.claim);input.gun=acquired.claim->token;}
    }
    void Shot(){
        Advance();++evidence.shot;--loaded;evidence.chamber=ChamberKnowledge::Empty;evidence.nativeReady=false;
        Require(gate.Observe(evidence,input.source.nowNs));
    }
    void Begin(){
        lease={input.source.owner,evidence.item,{20,1},30+evidence.shot+evidence.feed,
            gate.Requirement().purpose==WeaponCyclePurpose::AfterShot?evidence.shot:0,
            evidence.sequence,evidence.observedNs,evidence.deadlineNs,true};initialLease=lease;
        Require(gate.BeginCycle(profile,lease,input.source.nowNs));Require(physical.Begin(profile,lease,input.source.nowNs));
    }
    void Step(float travel=0,float angle=0,bool grip=true){
        Advance();Require(gate.Observe(evidence,input.source.nowNs));input.grip=grip;input.source.released[0]=!grip;
        if(input.source.focused&&input.source.tracked[0]&&input.source.tracked[1])Gun();
        input.lease=lease;input.rawContact=weapon_cycle_detail::Target(profile,travel,angle);
        input.contact={lease.mechanism,input.source.sequence,input.source.deadlineNs,true};input.acquireIntent=++intent;
        result=physical.Update(input,hands);
        if(result.release){Require(gate.Submit(*result.release,input.source.nowNs));submitted=result.release;++releases;}
    }
    void Grip(){Step(0,0,false);Step();}
    void Unlock(){if(profile.family==WeaponCycleFamily::Bolt)for(float a:{.3f,.6f,.9f,1.1f,1.1f})Step(0,a);}
    void Rear(){for(float t:{.02f,.04f,.06f,.08f,.10f,.10f})Step(t,profile.unlockRadians);}
    void Forward(){for(float t:{.08f,.06f,.04f,.02f,0.f,0.f})Step(t,profile.unlockRadians);}
    void Lock(){if(profile.family==WeaponCycleFamily::Bolt)for(float a:{.8f,.5f,.2f,0.f,0.f})Step(0,a);}
    void Cycle(){Grip();Unlock();Rear();Forward();Lock();}
    WeaponCycleReady Ready(ChamberKnowledge chamber=ChamberKnowledge::Occupied){
        Require(submitted.has_value());Advance();evidence.nativeReady=true;evidence.actionClosed=true;evidence.chamber=chamber;
        return {*submitted,evidence.sequence,evidence.observedNs,evidence.deadlineNs,true,true};
    }
    bool Complete(ChamberKnowledge chamber=ChamberKnowledge::Occupied){
        const auto ready=Ready(chamber);
        return gate.Complete(ready,evidence,input.source.nowNs)&&physical.ObserveReady(ready,input.source.nowNs);
    }
};
int OrderedBoltAndExactNativeCompletion(){
    Fixture f;CHECK(!f.gate.BlocksFire(f.input.source));f.Shot();CHECK(f.loaded==4&&f.reserve==20);
    CHECK(f.gate.BlocksFire(f.input.source)&&f.gate.Requirement().action==WeaponAction::Bolt);
    f.Begin();f.Grip();CHECK(f.physical.Phase()==WeaponCyclePhase::Unlock);f.Unlock();CHECK(f.physical.Phase()==WeaponCyclePhase::Rear);
    f.Rear();CHECK(f.physical.Phase()==WeaponCyclePhase::Forward);f.Forward();CHECK(f.physical.Phase()==WeaponCyclePhase::Lock&&!f.submitted);
    CHECK(f.gate.BlocksFire(f.input.source));f.Lock();CHECK(f.releases==1&&f.physical.Phase()==WeaponCyclePhase::AwaitingNative);
    CHECK(!f.hands.Current(InteractionHand::Left)&&!f.result.target&&f.gate.BlocksFire(f.input.source));
    CHECK(f.Complete());CHECK(!f.gate.BlocksFire(f.input.source)&&f.loaded==4&&f.reserve==20);
    CHECK(!f.gate.Submit(*f.submitted,f.input.source.nowNs));return 0;
}
int NativeReadyCannotSkipGesture(){
    Fixture f;f.Shot();f.Begin();f.Grip();f.evidence.nativeReady=true;f.evidence.actionClosed=true;f.evidence.chamber=ChamberKnowledge::Occupied;
    f.Step();CHECK(f.gate.BlocksFire(f.input.source));CHECK(f.gate.Phase()==WeaponActionGatePhase::Cycling);
    f.Unlock();f.Rear();f.Forward();CHECK(f.gate.BlocksFire(f.input.source)&&f.releases==0);return 0;
}
int PartialAndOutOfOrderGestures(){
    for(unsigned kind=0;kind<4;++kind){Fixture f;f.Shot();f.Begin();f.Grip();
        if(kind==0){f.Step(.02f);CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled);}
        if(kind==1){f.Unlock();for(float t:{.02f,.04f,.06f,.04f,.02f,0.f,0.f})f.Step(t,1.1f);CHECK(f.physical.Phase()==WeaponCyclePhase::Rear);}
        if(kind==2){f.Unlock();f.Rear();f.Step(.08f,.3f);CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled);}
        if(kind==3){f.Unlock();f.Rear();f.Forward();f.Step(.02f,.5f);CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled);}
        CHECK(f.gate.BlocksFire(f.input.source)&&f.releases==0&&f.loaded==4&&f.reserve==20);
    }return 0;
}
int InterruptedBoltRegripsFullSequence(){
    for(unsigned phase=0;phase<4;++phase)for(unsigned loss=0;loss<3;++loss){
        Fixture f;f.Shot();f.Begin();f.Grip();if(phase>=1)f.Unlock();if(phase>=2)f.Rear();if(phase>=3)f.Forward();
        if(loss==0)f.input.source.tracked[0]=false;
        if(loss==1)f.input.source.focused=false;
        f.Step(0,0,loss!=2);CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled&&f.gate.BlocksFire(f.input.source));
        CHECK(!f.hands.Current(InteractionHand::Left)&&f.releases==0);
        f.input.source.tracked[0]=true;f.input.source.focused=true;
        f.Step();CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled); // Held grip cannot rearm.
        f.Cycle();CHECK(f.releases==1&&f.Complete());CHECK(!f.gate.BlocksFire(f.input.source)&&f.loaded==4&&f.reserve==20);
    }return 0;
}
int SubmittedReceiptSurvivesLossAndCannotRepeat(){
    Fixture f;f.Shot();f.Begin();f.Cycle();const auto original=*f.submitted;
    f.input.source.tracked[0]=false;f.input.source.focused=false;
    for(unsigned n=0;n<5;++n){f.Step();CHECK(f.physical.Phase()==WeaponCyclePhase::AwaitingNative&&!f.result.release&&!f.result.target);}
    CHECK(f.releases==1&&*f.submitted==original&&f.gate.BlocksFire(f.input.source));
    CHECK(f.Complete());CHECK(f.gate.BlocksFire(f.input.source));
    f.input.source.tracked[0]=true;f.input.source.focused=true;f.Step(0,0,false);CHECK(!f.gate.BlocksFire(f.input.source));return 0;
}
int CompletionMustMatchAndHaveCoherentChamber(){
    for(unsigned kind=0;kind<14;++kind){Fixture f;f.Shot();f.Begin();f.Cycle();auto ready=f.Ready();auto e=f.evidence;
        switch(kind){case 0:++ready.release.request;break;case 1:++ready.release.cycle.shot;break;case 2:++ready.release.cycle.owner.equipGeneration;break;
        case 3:ready.unchangedAmmunition=false;break;case 4:ready.nativeReady=false;break;case 5:e.chamber=ChamberKnowledge::Unknown;break;
        case 6:e.actionClosed=false;break;case 7:e.nativeReady=false;break;case 8:++e.shot;break;case 9:++e.feed;break;case 10:++e.item.generation;break;
        case 11:++e.sequence;break;case 12:++e.observedNs;break;case 13:e.deadlineNs=f.input.source.nowNs;break;}
        CHECK(!f.gate.Complete(ready,e,f.input.source.nowNs));CHECK(f.gate.BlocksFire(f.input.source));
    }return 0;
}
int LastRoundCycleStaysEmptyAndFeedAddsSeparateCycle(){
    Fixture f;f.loaded=1;f.Shot();f.Begin();f.Cycle();CHECK(f.Complete(ChamberKnowledge::Empty));
    CHECK(f.gate.Phase()==WeaponActionGatePhase::Ready&&f.gate.BlocksFire(f.input.source)&&f.loaded==0&&f.reserve==20);
    f.Advance();++f.evidence.feed;f.evidence.chamberBeforeFeed=ChamberKnowledge::Empty;f.evidence.nativeReady=false;f.loaded=5;f.reserve-=5;
    CHECK(f.gate.Observe(f.evidence,f.input.source.nowNs));CHECK(f.gate.Requirement().purpose==WeaponCyclePurpose::AfterFeed);
    f.Begin();CHECK(f.lease.shot==0);f.Cycle();CHECK(f.Complete());CHECK(!f.gate.BlocksFire(f.input.source)&&f.loaded==5&&f.reserve==15);return 0;
}
int TacticalFeedRetainsIndependentChamber(){
    Fixture f;f.Advance();++f.evidence.feed;f.evidence.chamberBeforeFeed=ChamberKnowledge::Occupied;
    CHECK(f.gate.Observe(f.evidence,f.input.source.nowNs));CHECK(f.gate.Phase()==WeaponActionGatePhase::Ready&&!f.gate.BlocksFire(f.input.source));
    auto bad=f.evidence;++bad.sequence;++bad.feed;bad.chamberBeforeFeed=ChamberKnowledge::Unknown;
    CHECK(!f.gate.Observe(bad,f.input.source.nowNs));CHECK(f.gate.Phase()==WeaponActionGatePhase::Faulted&&f.gate.BlocksFire(f.input.source));return 0;
}
int UnknownAndStaleEvidenceNeverUnlock(){
    Fixture f;auto e=f.evidence;++e.sequence;e.chamber=ChamberKnowledge::Unknown;CHECK(f.gate.Observe(e,f.input.source.nowNs));
    CHECK(f.gate.BlocksFire(f.input.source));f.input.source.nowNs=e.deadlineNs;CHECK(f.gate.BlocksFire(f.input.source));
    Fixture g;auto duplicate=g.evidence;++duplicate.deadlineNs;CHECK(!g.gate.Observe(duplicate,g.input.source.nowNs));
    CHECK(g.gate.BlocksFire(g.input.source));CHECK(!g.gate.Observe(g.evidence,g.input.source.nowNs));
    g.Advance();CHECK(g.gate.Observe(g.evidence,g.input.source.nowNs));CHECK(!g.gate.BlocksFire(g.input.source));
    g.input.source.nowNs=g.evidence.deadlineNs;CHECK(g.gate.BlocksFire(g.input.source));return 0;
}
int EventGapsAndEventsDuringCycleFailClosed(){
    for(unsigned k=0;k<4;++k){Fixture f;
        if(k==2||k==3){f.Shot();f.Begin();}
        f.Advance();if(k==0)f.evidence.shot+=2;if(k==1){++f.evidence.shot;++f.evidence.feed;}if(k==2)++f.evidence.shot;if(k==3)++f.evidence.feed;
        CHECK(!f.gate.Observe(f.evidence,f.input.source.nowNs));CHECK(f.gate.Phase()==WeaponActionGatePhase::Faulted&&f.gate.BlocksFire(f.input.source));
    }return 0;
}
int OwnerChangesDoNotEraseRequiredCycle(){
    Fixture f;f.Shot();f.Begin();f.Cycle();const auto old=f.evidence.owner;
    f.Advance();++f.evidence.owner.equipGeneration;CHECK(f.gate.Observe(f.evidence,f.input.source.nowNs));
    f.input.source.owner=f.evidence.owner;CHECK(f.gate.BlocksFire(f.input.source));
    auto ready=f.Ready();CHECK(!f.gate.Complete(ready,f.evidence,f.input.source.nowNs));
    f.evidence.owner=old;CHECK(f.gate.Complete(ready,f.evidence,f.input.source.nowNs));
    CHECK(f.gate.BlocksFire(f.input.source)); // Old owner receipt does not authorize new owner input.
    f.Advance();f.evidence.owner=f.input.source.owner;CHECK(f.gate.Observe(f.evidence,f.input.source.nowNs));
    CHECK(!f.gate.BlocksFire(f.input.source));return 0;
}
int NativeRetirementPreservesDebtAndRejectsStaleRestart(){
    Fixture f;f.Shot();f.Begin();f.Grip();f.Step(0,.3f);const auto original=f.initialLease;
    WeaponActionRetirement retirement{original,f.evidence.sequence+1,f.input.source.nowNs,f.input.source.deadlineNs,true,true};
    auto wrong=retirement;++wrong.cycle.cycle;CHECK(!f.gate.Retire(wrong,f.input.source.nowNs));
    CHECK(f.gate.Retire(retirement,f.input.source.nowNs));CHECK(f.gate.Phase()==WeaponActionGatePhase::Required&&f.gate.BlocksFire(f.input.source));
    auto stale=f.lease;++stale.cycle;CHECK(!f.gate.BeginCycle(f.profile,stale,f.input.source.nowNs));
    f.Advance();f.Advance();CHECK(f.gate.Observe(f.evidence,f.input.source.nowNs));++f.lease.cycle;
    CHECK(f.gate.BeginCycle(f.profile,f.lease,f.input.source.nowNs));return 0;
}
int RepeatedBoltAndPumpUseSameGate(){
    for(auto action:{WeaponAction::Bolt,WeaponAction::Pump}){Fixture f(action);for(unsigned n=0;n<3;++n){
        f.Shot();f.Begin();f.Cycle();CHECK(f.gate.BlocksFire(f.input.source));CHECK(f.Complete());CHECK(!f.gate.BlocksFire(f.input.source));}
        CHECK(f.releases==3&&f.loaded==2&&f.reserve==20);
    }return 0;
}
int ProfilePurposeAndTimeoutCannotBeSubstituted(){
    Fixture f;f.Shot();auto lease=WeaponCycleLease{f.input.source.owner,f.evidence.item,{20,1},31,f.evidence.shot,
        f.evidence.sequence,f.evidence.observedNs,f.evidence.deadlineNs,true};
    auto pump=f.profile;pump.family=WeaponCycleFamily::Pump;pump.unlockRadians=0;CHECK(!f.gate.BeginCycle(pump,lease,f.input.source.nowNs));
    ++lease.shot;CHECK(!f.gate.BeginCycle(f.profile,lease,f.input.source.nowNs));
    f.Begin();f.Cycle();auto ready=f.Ready();f.input.source.nowNs+=4000000000ll;
    ready.observedNs=f.input.source.nowNs;ready.deadlineNs=ready.observedNs+100000000;f.evidence.observedNs=ready.observedNs;f.evidence.deadlineNs=ready.deadlineNs;
    CHECK(!f.gate.Complete(ready,f.evidence,f.input.source.nowNs)&&f.gate.BlocksFire(f.input.source));return 0;
}
int NativeAfterShotNeedsNoInventedChamber(){
    for(auto action:{WeaponAction::Bolt,WeaponAction::Pump}){
        Fixture f(action,WeaponActionReadiness::NativeAfterShot);CHECK(!f.gate.BlocksFire(f.input.source));
        for(unsigned repeat=0;repeat<3;++repeat){
            f.Advance();++f.evidence.shot;--f.loaded;f.evidence.chamber=ChamberKnowledge::Unknown;f.evidence.nativeReady=false;
            CHECK(f.gate.Observe(f.evidence,f.input.source.nowNs)&&f.gate.Requirement().purpose==WeaponCyclePurpose::AfterShot);
            CHECK(f.gate.BlocksFire(f.input.source));f.Begin();f.Grip();
            f.evidence.nativeReady=true;f.Step();CHECK(f.gate.BlocksFire(f.input.source));
            f.Unlock();f.Rear();f.Forward();f.Lock();CHECK(f.gate.BlocksFire(f.input.source));
            auto ready=f.Ready(ChamberKnowledge::Unknown);auto wrong=ready;++wrong.release.request;
            CHECK(!f.gate.Complete(wrong,f.evidence,f.input.source.nowNs));
            f.evidence.actionClosed=false;CHECK(!f.gate.Complete(ready,f.evidence,f.input.source.nowNs));
            f.evidence.actionClosed=true;CHECK(f.gate.Complete(ready,f.evidence,f.input.source.nowNs));
            CHECK(f.physical.ObserveReady(ready,f.input.source.nowNs)&&!f.gate.BlocksFire(f.input.source));
            CHECK(f.evidence.chamber==ChamberKnowledge::Unknown&&f.reserve==20);
        }
        CHECK(f.loaded==2&&f.releases==3);
        // Independent native feed remains native; this mode adds no charging
        // or chamber-count requirement after the owed shot action was cleared.
        f.Advance();++f.evidence.feed;f.evidence.chamberBeforeFeed=ChamberKnowledge::Unknown;
        CHECK(f.gate.Observe(f.evidence,f.input.source.nowNs)&&f.gate.Phase()==WeaponActionGatePhase::Ready);
        CHECK(!f.gate.BlocksFire(f.input.source));f.evidence.nativeReady=false;f.Advance();CHECK(f.gate.Observe(f.evidence,f.input.source.nowNs));
        CHECK(f.gate.BlocksFire(f.input.source));
    }
    Fixture semantic;auto e=semantic.evidence;e.chamber=ChamberKnowledge::Unknown;
    WeaponActionGate separate;CHECK(!separate.Bind(Descriptor(),e,e.observedNs));
    CHECK(!separate.Bind(Descriptor(WeaponAction::Automatic),e,e.observedNs,WeaponActionReadiness::NativeAfterShot));return 0;
}
}
int main(){
    if(OrderedBoltAndExactNativeCompletion()||NativeReadyCannotSkipGesture()||PartialAndOutOfOrderGestures()||InterruptedBoltRegripsFullSequence()||
       SubmittedReceiptSurvivesLossAndCannotRepeat()||CompletionMustMatchAndHaveCoherentChamber()||LastRoundCycleStaysEmptyAndFeedAddsSeparateCycle()||
       TacticalFeedRetainsIndependentChamber()||UnknownAndStaleEvidenceNeverUnlock()||EventGapsAndEventsDuringCycleFailClosed()||
       OwnerChangesDoNotEraseRequiredCycle()||NativeRetirementPreservesDebtAndRejectsStaleRestart()||RepeatedBoltAndPumpUseSameGate()||ProfilePurposeAndTimeoutCannotBeSubstituted()||NativeAfterShotNeedsNoInventedChamber())return 1;
    std::puts("15 action-gate groups passed: physical bolt/pump controller emulation including native readiness without invented chamber; synthetic geometry/native receipts, no game binding.");return 0;
}
