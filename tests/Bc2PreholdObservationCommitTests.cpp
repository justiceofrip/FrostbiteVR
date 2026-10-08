#include "Bc2PreholdObservationCommit.h"
#include "Bc2ReloadRequestCycle.h"
#include "Test.h"
#include <cstring>
using namespace fvr::bc2;
namespace {
ReloadHoldInput Input()
{
    ReloadHoldInput i;
    i.verified = true;
    i.branch = 0;
    i.nowNs = 1000000000;
    i.leaseDeadlineNs = i.nowNs + 100000000;
    i.identity.owner = {0x10000, 0x20000, 0x30000, 0x40000, 1, 2, 3};
    i.identity.firing = {0x50000, 0x60000, 0x70000};
    i.identity.serverPlayer = 0x80000;
    i.identity.serverSoldier = 0x90000;
    i.identity.serverItem = 0xa0000;
    auto &c = i.config;
    std::memcpy(c.assetName.data(), "SPAS12_sp", sizeof("SPAS12_sp"));
    std::memcpy(c.assetPath.data(), "Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12",
                sizeof("Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12"));
    c.weaponData = 0xb0000;
    c.firingData = 0xc0000;
    c.primaryFire = 0xd0000;
    c.ammoAddress = c.primaryFire + 0x170;
    c.fireLogicType = 1;
    c.reloadType = 0;
    c.fireInputAction = 8;
    c.reloadInputAction = 29;
    c.baseCapacity = c.numberOfMagazines = 4;
    c.reloadDelay = .06f;
    c.reloadTime = .72f;
    c.reloadThreshold = c.postReloadTime = 1;
    c.boltDelay = .5f;
    i.context.deltaSeconds = .005f;
    i.context.reloadTimeMultiplier = 1;
    i.context.flags24Through28[0] = true;
    for (unsigned n = 0; n < 3; ++n)
    {
        auto &b = i.branches[n];
        b.address = i.identity.firing[n];
        b.wrapperOffset = n == 0 ? 0x3c : n == 1 ? 0x40 : 0x10;
        b.currentState = 11;
        b.nextState = 12;
        b.phaseTimer = .2f;
        b.loaded = 6;
        b.reserve = 21;
        i.capacities[n] = 8;
    }
    return i;
}
int Commit(){auto input=Input();PreholdCleanupEvidence e;e.source=input;e.revision=3;e.operation=1;e.sequence=9;
 e.observedNs=input.nowNs;e.deadlineNs=input.nowNs+1000000;e.coherent=e.configVerified=true;
 PreholdObservationKey key{input.identity,1,3,7,9,0,0,2,input.nowNs+2000000000};
 CHECK(PreholdObservationMayCommit(key,key,e,input.config,input.nowNs+1));
 auto bad=key;++bad.cycle;CHECK(!PreholdObservationMayCommit(key,bad,e,input.config,input.nowNs+1));
 bad=key;++bad.cancelEpoch;CHECK(!PreholdObservationMayCommit(key,bad,e,input.config,input.nowNs+1));
 bad=key;bad.phase=6;CHECK(!PreholdObservationMayCommit(key,bad,e,input.config,input.nowNs+1));
 bad=key;++bad.identity.owner.equipGeneration;CHECK(!PreholdObservationMayCommit(key,bad,e,input.config,input.nowNs+1));
 bad=key;++bad.ownerRevision;CHECK(!PreholdObservationMayCommit(key,bad,e,input.config,input.nowNs+1));
 bad=key;++bad.callbackRevision;CHECK(!PreholdObservationMayCommit(key,bad,e,input.config,input.nowNs+1));
 bad=key;++bad.operationDeadlineNs;CHECK(!PreholdObservationMayCommit(key,bad,e,input.config,input.nowNs+1));
 CHECK(!PreholdObservationMayCommit(key,key,e,input.config,e.deadlineNs));
 auto stale=e;stale.source.config.reloadTime+=1;CHECK(!PreholdObservationMayCommit(key,key,stale,input.config,input.nowNs+1));
 stale=e;stale.coherent=false;CHECK(!PreholdObservationMayCommit(key,key,stale,input.config,input.nowNs+1));
 return 0;
}
}
int LedgerInterleaving(){
 const auto input=Input();const auto at=input.nowNs;
 for(unsigned change=0;change<7;++change){
  auto original=PreholdCleanupEvidence{input,3,1,9,at,at+100000000,true,true,true};
  original.source.nowNs=at;original.source.leaseDeadlineNs=original.deadlineNs;
  for(auto& b:original.source.branches){b.currentState=b.nextState=2;b.phaseTimer=0;}
  PreholdCleanupLedger ledger;CHECK(ledger.Prepare(original,at));
  Bc2ReloadRequestCycle policy(true);
  ReloadCycleControl control{input.identity,1,9,at,at+100000000,true};CHECK(policy.Start(control,at));
  PreholdObservationKey captured{input.identity,policy.Cycle(),3,7,9,0,0,unsigned(policy.Phase()),ledger.OperationDeadlineNs()};
  auto prepared=original;prepared.callbacksExcluded=false;
  prepared.observedNs=prepared.source.nowNs=at+1000;prepared.deadlineNs=prepared.source.leaseDeadlineNs=at+100000000;
  for(auto& b:prepared.source.branches){b.currentState=11;b.nextState=12;b.phaseTimer=.2f;}
  auto current=captured;
  if(change==1){policy.Cancel();current.phase=unsigned(policy.Phase());++current.cancelEpoch;}
  if(change==2){++current.ownerRevision;++current.identity.owner.equipGeneration;}
  if(change==3)++current.callbackRevision;
  if(change==4)++current.cycle;
  if(change==5)++current.profile;
  if(change==6)++current.operationDeadlineNs;
  // Same split consumer ordering as Runtime: complete native snapshot outside
  // policy exclusion, then commit only while current identity remains exact.
  const auto now=at+2000;
  const bool commit=PreholdObservationMayCommit(captured,current,prepared,input.config,now);
  if(commit)CHECK(ledger.ObserveEntry(prepared,0,now));
  CHECK(commit==(change==0));CHECK(ledger.EnteredMask()==(change==0?1u:0u));
  CHECK(ledger.Phase()==(change==0?PreholdCleanupPhase::Entered:PreholdCleanupPhase::Prepared));
  CHECK(ledger.OperationDeadlineNs()==captured.operationDeadlineNs);
  CHECK(ledger.Original().source.branches[0].loaded==input.branches[0].loaded);
  CHECK(policy.Phase()==(change==1?ReloadRequestCyclePhase::Cancelled:ReloadRequestCyclePhase::Arming));
 }
 return 0;
}
int main(){if(Commit()||LedgerInterleaving())return 1;std::puts("Prehold outside-lock actual ledger/policy interleaving regressions passed");}
