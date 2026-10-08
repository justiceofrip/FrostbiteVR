#include "Bc2ReloadPreentryDeferral.h"
#include "Bc2MagazineStart.h"
#include "Bc2ReloadLockBodyTelemetry.h"
#include "Bc2ReloadPolicyLock.h"
#include "Bc2ReloadRequestCycle.h"
#include "Test.h"
#include <thread>
#include <cstring>
using namespace fvr::bc2;using namespace fvr::interaction;
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
int Deferral(){
 auto in=Input();for(auto& b:in.branches){b.currentState=b.nextState=2;b.phaseTimer=0;}
 CHECK(ReloadPreentryEvaluationMayDefer(in));
 auto bad=in;bad.branches[1].currentState=11;bad.branches[1].nextState=12;CHECK(!ReloadPreentryEvaluationMayDefer(bad));
 bad=in;bad.branches[0].loaded=0;CHECK(!ReloadPreentryEvaluationMayDefer(bad));
 bad=in;bad.context.inputFlags=4;CHECK(!ReloadPreentryEvaluationMayDefer(bad));
 bad=in;bad.branches[2].reserve++;CHECK(!ReloadPreentryEvaluationMayDefer(bad));
 bad=in;bad.branches[1].phaseTimer=.2f;CHECK(!ReloadPreentryEvaluationMayDefer(bad));
 Bc2ReloadRequestCycle policy(true);ReloadCycleControl control{in.identity,1,1,in.nowNs,in.nowNs+100000000,true};CHECK(policy.Start(control,in.nowNs));
 std::atomic_flag gate=ATOMIC_FLAG_INIT;std::atomic<bool> held=false,release=false;unsigned original=0,cancels=0;
 std::thread peer([&]{gate.test_and_set();held.store(true);while(!release.load())std::this_thread::yield();gate.clear();});
 while(!held.load())std::this_thread::yield();
 ReloadRequestDecision decision;std::int64_t clock=1000000000;
 {ReloadPolicyLock lock(gate,[&]{return ++clock;},[]{std::this_thread::yield();});CHECK(!lock.Held());
  if(!ReloadPreentryEvaluationMayDefer(in)){policy.Cancel(ReloadRequestCycleFailure::Owner);++cancels;}
 }
 // Cached entry observation misses the lock and returns no receipt. It must
 // not convert unavailability into owner cancellation (actual repeat03).
 bool observed=false;
 {ReloadPolicyLock lock(gate,[&]{return ++clock;},[]{std::this_thread::yield();});
  CHECK(!lock.Held());if(lock.Held())observed=true;}
 CHECK(!observed&&policy.Phase()==ReloadRequestCyclePhase::Arming&&cancels==0);
 auto inspection=MagazineCycleStartResult::Unknown;
 {ReloadPolicyLock lock(gate,[&]{return ++clock;},[]{},false);
  if(lock.Held())inspection=InspectMagazineStartRegistration(true,false,control.identity,1,control.identity,2);}
 CHECK(inspection==MagazineCycleStartResult::Unknown&&policy.Phase()==ReloadRequestCyclePhase::Arming);
 CHECK(InspectMagazineStartRegistration(true,true,control.identity,1,control.identity,1)==MagazineCycleStartResult::RegisteredCancelled);
 CHECK(InspectMagazineStartRegistration(true,true,control.identity,1,control.identity,2)==MagazineCycleStartResult::NotStarted);

 ++original;CHECK(!decision.tracked&&!decision.hold);CHECK(policy.Phase()==ReloadRequestCyclePhase::Arming&&cancels==0&&original==1);
 release.store(true);peer.join();
 CHECK(policy.KeepAlive(control,in.nowNs+1000));
 for(auto& b:in.branches){b.currentState=11;b.nextState=12;b.phaseTimer=.2f;}
 CHECK(!ReloadPreentryEvaluationMayDefer(in));
 in.nowNs+=2000;ReloadRequestDecision entry;
 for(unsigned n=0;n<3;++n){in.branch=n;entry=policy.Evaluate(in,true,true,n+1,in.nowNs);}
 CHECK(entry.tracked&&entry.hold&&policy.Phase()==ReloadRequestCyclePhase::Holding);
 // A decision-critical held callback retains the conservative cancellation.
 gate.test_and_set();
 {ReloadPolicyLock lock(gate,[&]{return ++clock;},[]{},false);CHECK(!lock.Held());
  if(!ReloadPreentryEvaluationMayDefer(in)){policy.Cancel(ReloadRequestCycleFailure::Owner);++cancels;}}
 gate.clear();CHECK(cancels==1&&policy.Phase()==ReloadRequestCyclePhase::Cancelled);
 // Accepted observation never renews controller deadline. Actual expiry cancels.
 Bc2ReloadRequestCycle expiry(true);CHECK(expiry.Start(control,control.observedNs));
 CHECK(!expiry.KeepAlive(control,control.deadlineNs));CHECK(expiry.Phase()==ReloadRequestCyclePhase::Cancelled);
 return 0;
}
}
int Telemetry(){
 ReloadLockBodyTelemetry telemetry;
 telemetry.Observe(2,3,4,100,200);CHECK(telemetry.Total()==1&&telemetry.Slow()==0&&telemetry.Maximum()==100);
 telemetry.Observe(2,3,4,0,200);telemetry.Observe(2,3,4,200,100);CHECK(telemetry.Invalid()==2);
 for(unsigned n=0;n<40;++n)telemetry.Observe(n,3,4,100,200100+n);
 CHECK(telemetry.Slow()==40&&telemetry.CountAfterDrain()==32);
 CHECK(telemetry.RowsAfterDrain()[0].site==0&&telemetry.RowsAfterDrain()[31].site==31);
 CHECK(telemetry.Maximum()==200039);return 0;
}
int main(){if(Deferral()||Telemetry())return 1;std::puts("Pre-entry contention consumer/expiry regression passed");}
