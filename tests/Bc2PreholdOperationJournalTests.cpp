#include "Bc2PreholdOperationJournal.h"
#include "Test.h"
#include <cstring>
#include <cstdio>
using namespace fvr::bc2;
namespace {
constexpr std::int64_t Now=1000000000;
ReloadHoldIdentity Identity(){ReloadHoldIdentity i;i.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};i.firing={0x50000,0x60000,0x70000};i.serverPlayer=0x80000;i.serverSoldier=0x90000;i.serverItem=0xa0000;return i;}
ReloadCycleControl Control(std::uint64_t cycle){return {Identity(),cycle,cycle,Now,Now+100000000,true};}
ReloadObservedConfig CurrentSpasConfig(){
 const auto& d=SpasReloadDescriptor;ReloadObservedConfig c;
 c.weaponData=0xb0000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
 std::copy(d.assetName.begin(),d.assetName.end(),c.assetName.begin());std::copy(d.assetPath.begin(),d.assetPath.end(),c.assetPath.begin());
 const auto&v=d.values;c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
 c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;
 c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
 c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}
PreholdOperationRecord Snapshot(const Bc2ReloadNativePolicy& p,std::uint64_t epoch=1){PreholdOperationRecord r;r.epoch=epoch;r.operation=p.Cycle();r.family=p.Family();r.profile=p.MagazineProfileId();r.policyPhase=p.Phase();r.cancellation=p.CancellationOrigin();return r;}
int ActualOriginsSurviveAcceptedStart(){
 Bc2ReloadNativePolicy p;PreholdOperationJournal journal;CHECK(p.Start(Control(1),Now));p.Cancel();
 auto first=Snapshot(p);CHECK(first.cancellation&&first.cancellation->prior==ReloadRequestCyclePhase::Arming);
 first.entered=7;first.called=first.exact=first.idle=4;first.cleanupDeadlineNs=Now+2000000000;first.ledgerPhase=unsigned(PreholdCleanupPhase::Expired);
 // Runtime captures before Start clears policy, appends ONLY after acceptance.
 CHECK(p.DrainCancelledInvocations(true));CHECK(p.Start(Control(2),Now));CHECK(!p.CancellationOrigin());
 CHECK(journal.Archive(first,Now+2200000000,true));
 CHECK(journal.Row(0).cancellation->cycle==1&&journal.Row(0).cancellation->prior==ReloadRequestCyclePhase::Arming);
 CHECK(journal.Row(0).called==4&&journal.Row(0).exact==4&&journal.Row(0).entered==7);
 p.Cancel();CHECK(journal.Archive(Snapshot(p),Now+4300000000,true));
 CHECK(journal.Count()==2&&journal.Row(1).cancellation->cycle==2&&journal.Row(0).operation==1);
 CHECK(journal.Row(0).archivedNs>=journal.Row(0).cleanupDeadlineNs);return 0;
}
int HoldingIsNotCachedArming(){
 Bc2ReloadNativePolicy p;CHECK(p.Start(Control(1),Now));auto prior=Snapshot(p);CHECK(!prior.cancellation);
 ReloadHoldInput in;in.verified=true;in.identity=Identity();in.config=CurrentSpasConfig();in.nowNs=Now+1000;in.leaseDeadlineNs=Now+100000000;
 in.context.deltaSeconds=.005f;in.context.reloadTimeMultiplier=1;in.context.flags24Through28[0]=true;
 for(unsigned n=0;n<3;++n){auto& b=in.branches[n];b.address=in.identity.firing[n];b.wrapperOffset=n==0?0x3c:n==1?0x40:0x10;b.currentState=11;b.nextState=12;b.phaseTimer=.2f;b.loaded=7;b.reserve=24;in.capacities[n]=8;}
 for(unsigned n=0;n<3;++n){in.branch=n;const auto d=p.Evaluate(in,true,true,n+1,in.nowNs);if(n<2)CHECK(!d.tracked);else {CHECK(d.tracked&&d.hold);ReloadDeltaOverride patch;patch.applied=patch.restored=true;patch.original=1;CHECK(p.Finish(d,in.branches[n],in.nowNs,true,patch));}}
 CHECK(p.Phase()==ReloadRequestCyclePhase::Holding);p.Cancel();auto actual=Snapshot(p);
 CHECK(actual.cancellation&&actual.cancellation->prior==ReloadRequestCyclePhase::Holding);
 PreholdOperationJournal j;CHECK(j.Archive(actual,Now+1,true));CHECK(j.Row(0).cancellation->prior!=ReloadRequestCyclePhase::Arming);return 0;
}
int BoundedNoOverwriteAndEpoch(){
 PreholdOperationJournal j;PreholdOperationRecord r;r.operation=1;
 CHECK(!j.Archive(r,1,false)&&j.Count()==0);CHECK(j.Archive(r,1,true));CHECK(!j.Archive(r,2,true));
 r.epoch=1;CHECK(j.Archive(r,2,true));
 for(unsigned n=2;n<PreholdOperationJournal::Capacity;++n){r.operation=n;CHECK(j.Archive(r,n+1,true));}
 CHECK(j.Count()==PreholdOperationJournal::Capacity);r.operation=100;CHECK(!j.Archive(r,100,true)&&j.Dropped()==1);
 CHECK(j.Row(0).epoch==0&&j.Row(0).operation==1&&j.Row(0).archivedNs==1);return 0;
}
}
int main(){if(ActualOriginsSurviveAcceptedStart()||HoldingIsNotCachedArming()||BoundedNoOverwriteAndEpoch())return 1;std::puts("Operation journal real cancellation origin, new Start and bounded retention passed; no native claim");}
