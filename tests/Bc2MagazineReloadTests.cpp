#include "Bc2MagazineReloadCycle.h"
#include "Bc2ReloadNativePolicy.h"
#include "Bc2ReloadAbort.h"
#include "Bc2ReloadRetirement.h"
#include "Test.h"
#include <cstring>
#include <bit>
#include <limits>
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
ReloadHoldInput Input(int loaded=20,int reserve=90){
    ReloadHoldInput i;i.verified=true;i.branch=0;i.nowNs=1000000000;i.leaseDeadlineNs=i.nowNs+100000000;
    i.identity.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};i.identity.firing={0x50000,0x60000,0x70000};
    i.identity.serverPlayer=0x80000;i.identity.serverSoldier=0x90000;i.identity.serverItem=0xa0000;
    auto& c=i.config;std::memcpy(c.assetName.data(),"XM8_sp_s",sizeof("XM8_sp_s"));
    std::memcpy(c.assetPath.data(),"Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped",sizeof("Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped"));
    c.weaponData=0xb0000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
    c.fireLogicType=2;c.reloadType=1;c.fireInputAction=8;c.reloadInputAction=29;
    c.baseCapacity=30;c.numberOfMagazines=4;c.reloadTime=2.8f;c.reloadThreshold=.75f;
    i.context.deltaSeconds=.005f;i.context.reloadTimeMultiplier=1;i.context.flags24Through28[0]=true;
    for(unsigned n=0;n<3;++n){auto& b=i.branches[n];b.address=i.identity.firing[n];
        b.wrapperOffset=n==0?0x3c:n==1?0x40:0x10;b.currentState=11;b.nextState=12;
        b.phaseTimer=1.8f;b.loaded=loaded;b.reserve=reserve;i.capacities[n]=30;}
    return i;
}
ManualReloadOwner Owner(const ReloadHoldIdentity& i){const auto& o=i.owner;return {o.soldier,o.actorGeneration,o.weapon,o.equipGeneration,o.space};}
template<class Policy> bool PulseStart(Policy& p,const ReloadCycleControl& c,const ManualReloadRequest& r,std::int64_t now,int fault=0){
 if constexpr(requires {typename Policy::StartupPulseProof;}){
  typename Policy::StartupPulseProof pulse{c,c.observedNs+100000000};
  if(fault==1)++pulse.control.sequence;if(fault==2)++pulse.control.identity.owner.space;
  if(fault==3)++pulse.control.cycle;if(fault==4)++pulse.endNs;if(fault==5)pulse.endNs=c.observedNs;if(fault==6)++pulse.control.deadlineNs;
  return p.Start(c,r,now,pulse);
 }else return p.Start(c,r,now);
}
template<class Policy> bool PulseEvidence(const Policy& p,const ReloadCycleControl& original,std::int64_t end){
 if constexpr(requires {typename Policy::StartupPulseProof;}){
  if(!p.StartupPulse()||p.StartupPulse()->control!=original||p.StartupPulse()->endNs!=end||p.FirstHoldingNs()<end)return false;
  for(auto observed:p.ArmingContexts())if(observed<end||observed>p.FirstHoldingNs())return false;
 }return true;
}
template<class Input> void ContextTime(Input& i,std::int64_t observed){if constexpr(requires{i.contextObservedNs;})i.contextObservedNs=observed;}
struct Simulation {
    Bc2MagazineReloadCycle policy{true};ReloadHoldInput input;ReloadCycleControl control{};
    std::uint64_t update=0,invocation=1000;unsigned originals=0;
    Simulation(int loaded=20,int reserve=90):input(Input(loaded,reserve)){
        control={input.identity,93,1,input.nowNs,input.leaseDeadlineNs,true};
    }
    bool Tick(std::int64_t delta=1000){input.nowNs+=delta;input.leaseDeadlineNs=input.nowNs+100000000;
        ++control.sequence;control.observedNs=input.nowNs;control.deadlineNs=input.leaseDeadlineNs;
        return policy.KeepAlive(control,input.nowNs);}
    ReloadRequestDecision Begin(unsigned branch,bool stable=true){input.branch=branch;return policy.Evaluate(input,true,stable,++update);}
    bool End(const ReloadRequestDecision& d,bool restore=true){++originals;ReloadDeltaOverride patch;
        if(d.hold){patch.applied=true;patch.restored=restore;patch.original=std::bit_cast<unsigned>(input.context.deltaSeconds);}
        input.nowNs+=100;return policy.Finish(d,input.branches[d.branch],input.nowNs,true,patch);}
    bool Start(){return policy.Start(control,{1,Owner(input.identity),ReloadOperation::UnseatMagazine,0,0},input.nowNs);}
    bool Arm(){
        if(!Start())return false;
        for(unsigned n=0;n<3;++n){if(!Tick())return false;const auto d=Begin(n);
            if(n<2){if(d.tracked)return false;}else if(!d.hold||!End(d))return false;}
        return HoldAll();
    }
    bool HoldAll(){for(unsigned n=0;n<3;++n){if(!Tick())return false;auto d=Begin(n);
        if(!d.hold||!policy.Allows(d,input.nowNs)||!End(d))return false;}return true;}
    std::optional<ReloadMagazineLease> Lease(){return policy.Lease(input.identity,control.cycle,input.nowNs);}
    std::optional<ReloadMagazineGateAcknowledgement> Unseat(){return policy.TakeUnseatAcknowledgement(input.identity,control.cycle,input.nowNs);}
    ReloadMagazineNativeRequest Request(){
        const auto& o=input.identity.owner;HandInteractionOwner physical{(std::uint64_t(o.weak)<<32)|o.soldier,o.actorGeneration,17,o.space};
        HandInteractionKey item{0xe0000,1};HandClaimToken claim{24,physical,InteractionHand::Left,HandClaimKind::AmmoObject,item,{7,1},10};
        return {{2,Owner(input.identity),ReloadOperation::SeatMagazine,1,0},Lease().value_or(ReloadMagazineLease{}),
            {item,claim,7,2,control.cycle},unsigned(std::min(30-input.branches[0].loaded,input.branches[0].reserve))};
    }
    bool Transfer(unsigned branch,int amount,bool record=true,bool tail=true){
        if(!Tick())return false;const auto d=Begin(branch);if(!d.tracked||d.hold)return false;
        auto& b=input.branches[branch];ReloadMagazineTransfer event{input.identity,control.cycle,++invocation,input.nowNs,input.nowNs+1,
            branch,b.loaded,b.reserve,b.loaded+amount,b.reserve-amount,true,true};
        if(record&&!policy.Transfer(event,d.update))return false;
        b.loaded+=amount;b.reserve-=amount;b.currentState=tail?12:2;b.nextState=tail?1:2;b.phaseTimer=tail?.65f:0;
        return End(d);
    }
    bool Settle(){for(auto& b:input.branches){b.currentState=b.nextState=2;b.phaseTimer=0;}
        if(!Tick())return false;const auto d=Begin(0);return !d.tracked&&policy.Phase()==ReloadRequestCyclePhase::Finished;}
};
int HeldAndNativeCompletion(){
    static_assert(!Bc2MagazineReloadCycle::DefaultRuntimeDispatchEnabled);
    Simulation f;CHECK(f.Arm());const auto lease=f.Lease();CHECK(lease&&lease->allThreeHeld);
    CHECK(lease->loaded==20&&lease->reserve==90); // Unseat never subtracts native ammunition.
    auto request=f.Request();CHECK(!f.policy.Submit(request,f.input.nowNs)); // Unseat gate must be consumed first.
    const auto removed=f.Unseat();CHECK(removed&&removed->semantic.request==1&&removed->semantic.operation==ReloadOperation::UnseatMagazine);
    CHECK(!f.Unseat());CHECK(f.policy.Submit(request,f.input.nowNs));CHECK(!f.policy.Submit(request,f.input.nowNs));
    CHECK(f.Transfer(0,10));CHECK(f.Transfer(1,10,false)); // Actual propagation may replace a client transfer.
    CHECK(f.Transfer(2,10));CHECK(!f.policy.TakeAcknowledgement(f.input.identity,f.control.cycle,f.input.nowNs));
    CHECK(f.Settle());const auto ack=f.policy.TakeAcknowledgement(f.input.identity,f.control.cycle,f.input.nowNs);CHECK(ack&&ack->verified);
    CHECK(ack->acknowledgement.semantic.request==2&&ack->acknowledgement.semantic.operation==ReloadOperation::SeatMagazine);
    CHECK(ack->acknowledgement.loadedBefore==20&&ack->acknowledgement.loadedAfter==30);
    CHECK(ack->acknowledgement.reserveBefore==90&&ack->acknowledgement.reserveAfter==80&&ack->acknowledgement.serverInvocation);
    CHECK(!f.policy.TakeAcknowledgement(f.input.identity,f.control.cycle,f.input.nowNs));return 0;
}
int ResourceCounts(){
    for(auto counts:{std::pair{0,90},std::pair{29,90},std::pair{20,3}}){
        Simulation f{counts.first,counts.second};CHECK(f.Arm()&&f.Unseat());const auto request=f.Request();const int amount=int(request.reservedUnits);
        CHECK(amount==std::min(30-counts.first,counts.second));CHECK(f.policy.Submit(request,f.input.nowNs));
        for(unsigned n=0;n<3;++n)CHECK(f.Transfer(n,amount));CHECK(f.Settle());
        const auto ack=f.policy.TakeAcknowledgement(f.input.identity,f.control.cycle,f.input.nowNs);CHECK(ack);
        CHECK(ack->acknowledgement.loadedAfter+ack->acknowledgement.reserveAfter==counts.first+counts.second);
    }
    for(auto counts:{std::pair{30,90},std::pair{20,0}}){Simulation f{counts.first,counts.second};CHECK(!f.Arm());CHECK(!f.Unseat());}
    Simulation wrong;CHECK(wrong.Arm()&&wrong.Unseat());auto request=wrong.Request();++request.reservedUnits;
    CHECK(!wrong.policy.Submit(request,wrong.input.nowNs));return 0;
}
int EvidenceFailures(){
    Simulation noServer;CHECK(noServer.Arm()&&noServer.Unseat());CHECK(noServer.policy.Submit(noServer.Request(),noServer.input.nowNs));
    for(unsigned n=0;n<3;++n)CHECK(noServer.Transfer(n,10,n!=2));
    CHECK(!noServer.Settle());CHECK(!noServer.policy.TakeAcknowledgement(noServer.input.identity,noServer.control.cycle,noServer.input.nowNs));
    Simulation partial;CHECK(partial.Arm()&&partial.Unseat());CHECK(partial.policy.Submit(partial.Request(),partial.input.nowNs));
    CHECK(!partial.Transfer(2,1));CHECK(partial.policy.UnresolvedRequest());
    Simulation patch;CHECK(patch.Arm());CHECK(patch.Tick());const auto d=patch.Begin(0);CHECK(d.hold);CHECK(!patch.End(d,false));CHECK(!patch.Unseat());
    Simulation stale;CHECK(stale.Arm());stale.input.nowNs=stale.control.deadlineNs;CHECK(!stale.Lease());CHECK(!stale.Unseat());
    Simulation cancelled;CHECK(cancelled.Arm()&&cancelled.Unseat());CHECK(cancelled.policy.Submit(cancelled.Request(),cancelled.input.nowNs));
    cancelled.policy.Cancel();CHECK(cancelled.policy.UnresolvedRequest());CHECK(!cancelled.policy.TakeAcknowledgement(cancelled.input.identity,cancelled.control.cycle,cancelled.input.nowNs));
    return 0;
}
int IdentityAndGates(){
    Simulation overlap;CHECK(overlap.Arm());CHECK(overlap.Tick());const auto d=overlap.Begin(0);CHECK(d.hold);
    CHECK(!overlap.Begin(0).tracked);CHECK(!overlap.Unseat());CHECK(overlap.policy.DrainCancelledInvocations(true));
    Simulation changed;CHECK(changed.Arm());++changed.input.identity.owner.equipGeneration;CHECK(!changed.Begin(0).tracked);CHECK(!changed.Unseat());
    Simulation input;CHECK(input.Arm());input.input.context.fireRequested=true;input.input.context.inputFlags=1;
    CHECK(!input.Begin(0).tracked);CHECK(!input.Unseat());
    Simulation original;CHECK(original.Arm());const auto old=original.Lease();CHECK(old);
    auto forged=original.control;forged.deadlineNs++;CHECK(!original.policy.KeepAlive(forged,original.input.nowNs));
    auto c=Input().config;CHECK(IsXm8MagazineConfig(c));c.reloadThreshold=1;CHECK(!IsXm8MagazineConfig(c));
    c=Input().config;c.assetName[0]='S';CHECK(!IsXm8MagazineConfig(c));
    c=Input().config;c.reloadType=0;CHECK(!IsXm8MagazineConfig(c));
    c=Input().config;c.assetPath[0]='X';CHECK(!IsXm8MagazineConfig(c));return 0;
}
int TimingReads(){
    struct Memory {std::array<unsigned,12> words{};unsigned calls=0;bool mutate=false;};
    Memory mem;mem.words[0x10/4]=std::bit_cast<unsigned>(.75f);mem.words[0x18/4]=std::bit_cast<unsigned>(2.8f);mem.words[0x20/4]=1;mem.words[0x24/4]=2;
    const ReloadStateMemory reader{&mem,[](void* value,unsigned at,void* out,std::size_t n){auto& m=*static_cast<Memory*>(value);
        if(n!=4||at<0xd0000||at>=0xd0030)return false;unsigned word=m.words[(at-0xd0000)/4];
        if(m.mutate&&m.calls++>=6&&at==0xd0018)++word;std::memcpy(out,&word,4);return true;},nullptr};
    CHECK(ReadXm8MagazineTiming(reader,Input().config));mem.mutate=true;CHECK(!ReadXm8MagazineTiming(reader,Input().config));
    mem.mutate=false;mem.words[0x2c/4]=1;CHECK(!ReadXm8MagazineTiming(reader,Input().config));return 0;
}
int NativeDispatchAndAbortScope(){
    auto in=Input(28,174);ReloadCycleControl c{in.identity,1,1,in.nowNs,in.leaseDeadlineNs,true};
    const ManualReloadRequest unseat{1,Owner(in.identity),ReloadOperation::UnseatMagazine,0,0};
    Bc2ReloadNativePolicy normal;CHECK(!normal.IsMagazine()&&!normal.StartMagazine(c,unseat,in.nowNs));
    Bc2ReloadNativePolicy p;CHECK(p.ConfigureMagazine()&&p.IsMagazine()&&!p.Start(c,in.nowNs));
    CHECK(p.StartMagazine(c,unseat,in.nowNs));CHECK(!p.ConfigureMagazine());
    for(unsigned n=0;n<7;++n){++in.nowNs;in.branch=n%3;
        const auto d=p.Evaluate(in,true,true,n+1,in.nowNs);
        if(d.hold){ReloadDeltaOverride exact;exact.applied=exact.restored=true;CHECK(p.Allows(d,in.nowNs));CHECK(p.Finish(d,in.branches[in.branch],in.nowNs,true,exact));}}
    const auto l=p.MagazineLease(in.identity,1,in.nowNs);CHECK(l&&l->allThreeHeld&&!p.Lease(in.identity,1,in.nowNs));
    const auto abort=p.AbortLease(in.identity,1,in.nowNs);CHECK(abort&&abort->deadlineNs==l->deadlineNs&&abort->loaded==28&&abort->reserve==174);
    CHECK(p.TakeUnseatAcknowledgement(in.identity,1,in.nowNs));CHECK(!p.Submit({},in.nowNs));
    ReloadAbortCleanup oldLimit;CHECK(oldLimit.Arm(*abort,2,in.nowNs,true));
    CHECK(!oldLimit.Claim(in,2,1,true,101,true,in.nowNs).call); // SPAS limit unchanged.
    ReloadAbortCleanup xm8;CHECK(xm8.Arm(*abort,2,in.nowNs,true,ReloadAbortTiming::VerifiedXm8Magazine));
    const auto d=xm8.Claim(in,2,1,true,101,true,in.nowNs);CHECK(d.call);
    auto after=d.before;after.currentState=after.nextState=2;after.phaseTimer=0;
    CHECK(xm8.Finish(d,after,true,true,in.nowNs));CHECK(after.loaded==28&&after.reserve==174);
    auto empty=*abort;empty.loaded=0;ReloadAbortCleanup unsupported;CHECK(!unsupported.Arm(empty,2,in.nowNs,true,ReloadAbortTiming::VerifiedXm8Magazine));
    CHECK(!CancelAndDrainReloadCycle(p,in.identity,1,false));CHECK(CancelAndDrainReloadCycle(p,in.identity,1,true));
    ReloadRetirementReceipts retirement;CHECK(retirement.Observe(p,in.identity,1,in.nowNs,true));
    return 0;
}
}
int TypedMagazineCycleObservation(){
 Simulation f;CHECK(f.Start());
 CHECK(f.policy.ObserveLease(f.input.identity,f.control.cycle,f.input.nowNs).result==ReloadMagazineObservationResult::Deferred);
 Simulation held;CHECK(held.Arm());
 auto ready=held.policy.ObserveLease(held.input.identity,held.control.cycle,held.input.nowNs);
 CHECK(ready.result==ReloadMagazineObservationResult::Ready&&ready.lease&&ready.lease->allThreeHeld);
 CHECK(held.Tick());const auto open=held.Begin(0);CHECK(open.hold);
 CHECK(held.policy.ObserveLease(held.input.identity,held.control.cycle,held.input.nowNs).result==ReloadMagazineObservationResult::Deferred);
 CHECK(held.End(open));CHECK(held.policy.ObserveLease(held.input.identity,held.control.cycle,held.input.nowNs).result==ReloadMagazineObservationResult::Ready);
 const auto originalDeadline=held.Lease()->deadlineNs;
 while(held.input.nowNs<=originalDeadline)CHECK(held.Tick(30000000));
 CHECK(held.policy.ObserveLease(held.input.identity,held.control.cycle,held.input.nowNs).result==ReloadMagazineObservationResult::Deferred);
 CHECK(held.HoldAll());CHECK(held.policy.ObserveLease(held.input.identity,held.control.cycle,held.input.nowNs).result==ReloadMagazineObservationResult::Deferred);
 CHECK(held.Tick());const auto republished=held.Begin(0);CHECK(republished.hold&&held.End(republished));
 const auto refreshed=held.policy.ObserveLease(held.input.identity,held.control.cycle,held.input.nowNs);
 CHECK(refreshed.result==ReloadMagazineObservationResult::Ready&&refreshed.lease&&refreshed.lease->allThreeHeld);
 auto wrong=held.input.identity;++wrong.owner.actorGeneration;
 CHECK(held.policy.ObserveLease(wrong,held.control.cycle,held.input.nowNs).result==ReloadMagazineObservationResult::Rejected);
 held.input.nowNs=held.control.deadlineNs;
 CHECK(held.policy.ObserveLease(held.input.identity,held.control.cycle,held.input.nowNs).result==ReloadMagazineObservationResult::Rejected);
 CHECK(held.policy.Phase()==ReloadRequestCyclePhase::Cancelled);
 return 0;
}
int HoldingMissingPositiveReceiptIsDeferred(){
 // Publication/control remain valid at60ms, but original hold/context
 // receipts exceed their50ms budget. No callback has observed native loss.
 Simulation aged;CHECK(aged.Arm()&&aged.Unseat());const auto old=aged.Lease();CHECK(old&&old->allThreeHeld);
 CHECK(aged.Tick(60000000));CHECK(aged.input.nowNs<old->deadlineNs);
 const auto missing=aged.policy.ObserveLease(aged.input.identity,aged.control.cycle,aged.input.nowNs);
 CHECK(missing.result==ReloadMagazineObservationResult::Deferred&&!missing.lease);
 CHECK(aged.policy.Phase()==ReloadRequestCyclePhase::Holding&&aged.input.branches[0].loaded==20&&aged.input.branches[0].reserve==90);
 // Observation cannot renew old evidence; recovery requires real callbacks.
 CHECK(aged.Lease()&&!aged.Lease()->allThreeHeld&&aged.Lease()->deadlineNs==old->deadlineNs);
 CHECK(aged.HoldAll());CHECK(aged.Tick());const auto republish=aged.Begin(0);CHECK(republish.hold&&aged.End(republish));
 const auto recovered=aged.policy.ObserveLease(aged.input.identity,aged.control.cycle,aged.input.nowNs);
 CHECK(recovered.result==ReloadMagazineObservationResult::Ready&&recovered.lease&&recovered.lease->allThreeHeld);
 // Publish precedes Finish: the final branch can restore all receipt times
 // while leaving a false publication until the next native callback.
 Simulation lag;CHECK(lag.Arm()&&lag.Unseat());CHECK(lag.Tick(60000000));
 for(unsigned branch=0;branch<3;++branch){if(branch)CHECK(lag.Tick());const auto d=lag.Begin(branch);CHECK(d.hold&&lag.End(d));}
 const auto lagged=lag.Lease();CHECK(lagged&&!lagged->allThreeHeld);
 CHECK(lag.policy.ObserveLease(lag.input.identity,lag.control.cycle,lag.input.nowNs).result==ReloadMagazineObservationResult::Deferred);
 CHECK(lag.Tick());const auto refresh=lag.Begin(0);CHECK(refresh.hold&&lag.End(refresh));
 const auto positive=lag.policy.ObserveLease(lag.input.identity,lag.control.cycle,lag.input.nowNs);
 CHECK(positive.result==ReloadMagazineObservationResult::Ready&&positive.lease&&positive.lease->allThreeHeld);
 // Fresh negative native proofs still cancel, rather than enter any wait.
 for(unsigned fault=0;fault<4;++fault){Simulation bad;CHECK(bad.Arm()&&bad.Unseat());CHECK(bad.Tick());
  if(fault==0){bad.input.branches[0].currentState=bad.input.branches[0].nextState=2;CHECK(!bad.Begin(0).tracked);}
  if(fault==1){bad.input.context.inputFlags=4;CHECK(!bad.Begin(0).tracked);}
  if(fault==2){++bad.input.identity.owner.actorGeneration;CHECK(!bad.Begin(0).tracked);}
  if(fault==3){const auto d=bad.Begin(0);CHECK(d.hold&&!bad.End(d,false));}
  CHECK(bad.policy.Phase()==ReloadRequestCyclePhase::Cancelled);
  CHECK(bad.policy.ObserveLease(bad.control.identity,bad.control.cycle,bad.input.nowNs).result==ReloadMagazineObservationResult::Rejected);
 }
 // Missing-positive observation cannot extend the original control deadline.
 Simulation expired;CHECK(expired.Arm()&&expired.Unseat());CHECK(expired.Tick(60000000));
 CHECK(expired.policy.ObserveLease(expired.input.identity,expired.control.cycle,expired.input.nowNs).result==ReloadMagazineObservationResult::Deferred);
 expired.input.nowNs=expired.control.deadlineNs;
 CHECK(expired.policy.ObserveLease(expired.input.identity,expired.control.cycle,expired.input.nowNs).result==ReloadMagazineObservationResult::Rejected);
 CHECK(expired.policy.Phase()==ReloadRequestCyclePhase::Cancelled);
 // False holding authority after submission is expected native advancement,
 // not an observation gap; completion receipts retain their existing path.
 Simulation advancing;CHECK(advancing.Arm()&&advancing.Unseat());CHECK(advancing.policy.Submit(advancing.Request(),advancing.input.nowNs));
 auto continuing=advancing.policy.ObserveLease(advancing.input.identity,advancing.control.cycle,advancing.input.nowNs);
 CHECK(continuing.result==ReloadMagazineObservationResult::Ready&&continuing.lease&&!continuing.lease->allThreeHeld);
 for(unsigned branch=0;branch<3;++branch)CHECK(advancing.Transfer(branch,10));CHECK(advancing.Settle());
 continuing=advancing.policy.ObserveLease(advancing.input.identity,advancing.control.cycle,advancing.input.nowNs);
 CHECK(continuing.result==ReloadMagazineObservationResult::Ready&&continuing.lease&&!continuing.lease->allThreeHeld);
 CHECK(advancing.policy.TakeAcknowledgement(advancing.input.identity,advancing.control.cycle,advancing.input.nowNs));
 return 0;
}
int EstablishedPulseRequiresGenuineContext(){
 for(unsigned fault=0;fault<5;++fault){Simulation s;
  CHECK(PulseStart(s.policy,s.control,{1,Owner(s.input.identity),ReloadOperation::UnseatMagazine,0,0},s.input.nowNs));
  CHECK(s.Tick(90000000)&&s.Tick(20000000));
  for(unsigned n=0;n<3;++n){CHECK(s.Tick());ContextTime(s.input,s.input.nowNs);auto d=s.Begin(n);
   if(n<2)CHECK(!d.hold);else CHECK(d.hold&&s.End(d));}
  CHECK(s.HoldAll());
  auto observed=s.input.nowNs;if(fault==0)observed=0;if(fault==1)observed+=1;
  if(fault==2)observed-=ReloadHoldProbe::ContextFreshNs+1;
  if(fault==3){s.input.context.inputFlags=1;s.input.context.fireRequested=true;}
  if(fault==4){s.input.context.inputFlags=4;s.input.context.reloadRequested=true;}
  ContextTime(s.input,observed);CHECK(!s.Begin(0).hold);
  CHECK(s.policy.Failure()==(fault<3?ReloadRequestCycleFailure::Owner:ReloadRequestCycleFailure::Timing));
 }return 0;
}
int ImmutableStartupPulseGate(){
 for(bool aek:{false,true}){Simulation s;
  if(aek){s.policy=Bc2MagazineReloadCycle(true,AekMagazineNativeProfile);
   const auto& profile=AekMagazineNativeProfile.configuration;std::memset(s.input.config.assetName.data(),0,s.input.config.assetName.size());
   std::memset(s.input.config.assetPath.data(),0,s.input.config.assetPath.size());
   std::memcpy(s.input.config.assetName.data(),profile.assetName.data(),profile.assetName.size());
   std::memcpy(s.input.config.assetPath.data(),profile.assetPath.data(),profile.assetPath.size());s.input.config.reloadTime=3.2f;}
  const auto initial=s.control;const auto end=initial.observedNs+100000000;
  CHECK(PulseStart(s.policy,s.control,{1,Owner(s.input.identity),ReloadOperation::UnseatMagazine,0,0},s.input.nowNs));
  for(unsigned n=0;n<3;++n){CHECK(s.Tick());ContextTime(s.input,s.input.nowNs);CHECK(!s.Begin(n).hold);}
  // Old context read before end processed afterward cannot establish holding.
  s.input.nowNs=end+1000;s.input.leaseDeadlineNs=end+100000000;s.control.observedNs=s.input.nowNs;
  s.control.deadlineNs=s.input.leaseDeadlineNs;++s.control.sequence;CHECK(s.policy.KeepAlive(s.control,s.input.nowNs));
  for(unsigned n=0;n<3;++n){ContextTime(s.input,end-1000);CHECK(!s.Begin(n).hold);}
  for(unsigned n=0;n<3;++n){ContextTime(s.input,s.input.nowNs);auto d=s.Begin(n);if(n<2)CHECK(!d.hold);else CHECK(d.hold&&s.End(d));}
  CHECK(s.HoldAll());CHECK(PulseEvidence(s.policy,initial,end));
  CHECK(!PulseStart(s.policy,initial,{1,Owner(s.input.identity),ReloadOperation::UnseatMagazine,0,0},s.input.nowNs));
  // External reload/fire remains a hard Timing failure after genuine Holding.
  s.input.context.inputFlags=4;s.input.context.reloadRequested=true;ContextTime(s.input,s.input.nowNs);
  CHECK(!s.Begin(0).hold&&s.policy.Failure()==ReloadRequestCycleFailure::Timing);
 }
 Simulation expired;expired.control.deadlineNs+=50000000;
 CHECK(!PulseStart(expired.policy,expired.control,{1,Owner(expired.input.identity),ReloadOperation::UnseatMagazine,0,0},expired.input.nowNs+100000000));
 Simulation max;max.control.observedNs=std::numeric_limits<std::int64_t>::max()-100000000;
 max.control.deadlineNs=std::numeric_limits<std::int64_t>::max();
 CHECK(PulseStart(max.policy,max.control,{1,Owner(max.input.identity),ReloadOperation::UnseatMagazine,0,0},max.control.observedNs));
 for(int fault=1;fault<=6;++fault){Simulation s;CHECK(!PulseStart(s.policy,s.control,{1,Owner(s.input.identity),ReloadOperation::UnseatMagazine,0,0},s.input.nowNs,fault));}
 Simulation fire;CHECK(fire.Arm());fire.input.context.inputFlags=1;fire.input.context.fireRequested=true;
 CHECK(!fire.Begin(0).hold&&fire.policy.Failure()==ReloadRequestCycleFailure::Timing);
 return 0;
}
int main(){if(EstablishedPulseRequiresGenuineContext()||ImmutableStartupPulseGate())return 1;if(HoldingMissingPositiveReceiptIsDeferred())return 1;if(TypedMagazineCycleObservation())return 1;if(HeldAndNativeCompletion()||ResourceCounts()||EvidenceFailures()||IdentityAndGates()||TimingReads()||NativeDispatchAndAbortScope())return 1;
    std::puts("Magazine cycle: ten deterministic groups passed; existing launch paths remain disabled");return 0;}

