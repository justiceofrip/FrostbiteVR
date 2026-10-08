#include "Bc2PreholdStartPreread.h"
#include "Bc2ReloadInvocationEntry.h"
#include "Test.h"
#include <atomic>
#include <thread>
#include <cstring>
using namespace fvr::bc2;
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

int Run(){auto input=Input();for(auto& b:input.branches){b.currentState=b.nextState=2;b.phaseTimer=0;}
 PreholdCleanupEvidence e;e.source=input;e.revision=3;e.operation=1;e.sequence=9;e.observedNs=input.nowNs;e.deadlineNs=input.leaseDeadlineNs;e.coherent=e.configVerified=true;
 ReloadStateSnapshot snapshot;snapshot.owner=input.identity.owner;snapshot.config=input.config;
 for(unsigned n=0;n<2;++n){snapshot.branches[n].address=input.identity.firing[n];snapshot.branches[n].loaded=input.branches[n].loaded;snapshot.branches[n].reserve=input.branches[n].reserve;}
 CHECK(PreholdStartPublishedMatches(e,snapshot));auto changedSnapshot=snapshot;changedSnapshot.config.reloadTime++;
 CHECK(!PreholdStartPublishedMatches(e,changedSnapshot));changedSnapshot=snapshot;changedSnapshot.branches[0].loaded++;
 CHECK(!PreholdStartPublishedMatches(e,changedSnapshot));
 ReloadCycleControl control{input.identity,1,9,input.nowNs,input.leaseDeadlineNs,true};
 PreholdObservationKey key{input.identity,0,3,0,10,0,0,1,0};auto current=key;++current.callbackRevision;
 CHECK(PreholdStartPrereadMayCommit(key,current,e,control,input.config,input.nowNs+1));
 for(unsigned fault=0;fault<8;++fault){auto k=current;auto evidence=e;auto c=control;
  if(fault==0)++k.cancelEpoch;if(fault==1)++k.ownerRevision;if(fault==2)++k.family;if(fault==3)++k.profile;if(fault==4)k.callbackRevision+=2;
  if(fault==5)c.deadlineNs=input.nowNs;if(fault==6)evidence.source.branches[1].loaded++;if(fault==7)evidence.source.config.reloadTime++;
  CHECK(!PreholdStartPrereadMayCommit(key,k,evidence,c,input.config,input.nowNs+1));}
 // A valid trial is not an actual cleanup receipt. If the deadline closes
 // before CPU ledger commit, cancel accepted policy rather than acknowledge it.
 {Bc2ReloadRequestCycle policy{true};CHECK(policy.Start(control,input.nowNs+1));PreholdCleanupLedger actual;auto confirmed=e;confirmed.callbacksExcluded=true;
  if(!actual.Prepare(confirmed,e.deadlineNs))policy.Cancel();CHECK(policy.Phase()==ReloadRequestCyclePhase::Cancelled);CHECK(actual.Phase()==PreholdCleanupPhase::Empty);}
 // Reproduce run05: expensive preparation under exclusion lets a real entrant
 // invalidate accepted Start. New preread permits OriginalUpdate before any
 // registration, then rejects the changed revision without policy mutation.
 for(bool old:{true,false}){
  std::atomic<unsigned> active{0};std::atomic<std::uint64_t> revision{10};std::atomic_flag exclusive=ATOMIC_FLAG_INIT;
  std::atomic<bool> go{false},finished{false};unsigned originals=0;bool rejectedEntry=false;Bc2ReloadRequestCycle policy{true};
  std::thread peer([&]{while(!go.load())std::this_thread::yield();rejectedEntry=!EnterReloadInvocation(active,revision,exclusive);++originals;ExitReloadInvocation(active,revision);finished=true;});
  if(old){CHECK(EnterReloadInvocation(active,revision,exclusive));{ReloadInvocationExclusion lock(exclusive,active,revision);CHECK(lock.Quiet());CHECK(policy.Start(control,input.nowNs));go=true;while(!finished.load())std::this_thread::yield();if(!lock.Quiet())policy.Cancel(ReloadRequestCycleFailure::Owner);}
   ExitReloadInvocation(active,revision);CHECK(policy.Phase()==ReloadRequestCyclePhase::Cancelled&&rejectedEntry);}
  else {go=true;while(!finished.load())std::this_thread::yield();CHECK(!rejectedEntry&&originals==1);
   CHECK(EnterReloadInvocation(active,revision,exclusive));{ReloadInvocationExclusion lock(exclusive,active,revision);auto changed=key;changed.callbackRevision=revision.load();CHECK(lock.Quiet());CHECK(!PreholdStartPrereadMayCommit(key,changed,e,control,input.config,input.nowNs+1));CHECK(policy.Cycle()==0);}
   ExitReloadInvocation(active,revision);
   // Fresh subsequent read commits under original control deadline only.
   auto fresh=key;fresh.callbackRevision=revision.load();CHECK(EnterReloadInvocation(active,revision,exclusive));{ReloadInvocationExclusion lock(exclusive,active,revision);auto nowKey=fresh;nowKey.callbackRevision=revision.load();CHECK(lock.Quiet()&&PreholdStartPrereadMayCommit(fresh,nowKey,e,control,input.config,input.nowNs+2));CHECK(policy.Start(control,input.nowNs+2));PreholdCleanupLedger ledger;auto confirmed=e;confirmed.callbacksExcluded=true;CHECK(ledger.Prepare(confirmed,input.nowNs+2));CHECK(policy.Phase()==ReloadRequestCyclePhase::Arming);}
   ExitReloadInvocation(active,revision);}
  peer.join();CHECK(originals==1);
 }
 return 0;}
int main(){if(Run())return 1;std::puts("Actual policy Start old-path cancellation / preread supersession regression passed");}
