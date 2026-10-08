#include "Bc2ReloadRetirement.h"
#include "Bc2ReloadFlowRuntime.h"
#include "ReloadCycleTestFixture.h"
#include <atomic>
#include <thread>
namespace {
int ExactOldIdentityAndCycleOnly(){
    for(unsigned k=0;k<7;++k){Simulation s;CHECK(s.Arm());auto wrong=s.input.identity;auto cycle=s.control.cycle;
        if(k==0)wrong.owner.actorGeneration++;if(k==1)wrong.owner.equipGeneration++;if(k==2)wrong.owner.space++;
        if(k==3)wrong.serverItem++;if(k==4)wrong.firing[2]++;if(k==5)cycle++;if(k==6)cycle=0;
        CHECK(!CancelAndDrainReloadCycle(s.gate,wrong,cycle,true));CHECK(s.gate.Phase()==ReloadRequestCyclePhase::Holding);CHECK(s.Lease()->allThreeHeld);
    }
    Simulation s;CHECK(s.Arm());CHECK(CancelAndDrainReloadCycle(s.gate,s.input.identity,s.control.cycle,true));
    CHECK(s.gate.Phase()==ReloadRequestCyclePhase::Cancelled&&!s.Lease());
    auto next=s.control;next.cycle++;CHECK(s.gate.Start(next,s.input.nowNs));
    CHECK(!CancelAndDrainReloadCycle(s.gate,s.input.identity,s.control.cycle,true));CHECK(s.gate.Phase()==ReloadRequestCyclePhase::Arming);return 0;
}
int OrphanDrainPreservesUnknownAndAmmo(){Simulation s;CHECK(s.Arm()&&s.gate.Submit(s.Request(),s.input.nowNs)&&s.Tick());
    const auto before=s.input.branches;const auto orphan=s.Begin(0);CHECK(orphan.tracked&&!orphan.hold);s.gate.Cancel();
    auto next=s.control;next.cycle++;
    CHECK(!CancelAndDrainReloadCycle(s.gate,s.input.identity,s.control.cycle,false));CHECK(!s.gate.Start(next,s.input.nowNs));
    CHECK(CancelAndDrainReloadCycle(s.gate,s.input.identity,s.control.cycle,true));
    ReloadRetirementReceipts receipts;const auto receipt=receipts.Observe(s.gate,s.input.identity,s.control.cycle,s.input.nowNs,true);
    CHECK(receipt&&receipt->verified&&receipt->identity==s.input.identity&&receipt->cycle==s.control.cycle);
    CHECK(s.gate.UnresolvedRequest()&&s.gate.PendingRequest()==47&&!s.gate.TakeAcknowledgement(s.input.identity,s.control.cycle,s.input.nowNs));
    for(unsigned n=0;n<3;++n)CHECK(s.input.branches[n].loaded==before[n].loaded&&s.input.branches[n].reserve==before[n].reserve);
    CHECK(!s.gate.Start(s.control,s.input.nowNs));CHECK(s.gate.Start(next,s.input.nowNs));return 0;
}
int NativeCompletionNeverUndone(){Simulation s;CHECK(s.Arm()&&s.gate.Submit(s.Request(),s.input.nowNs)&&s.CompleteRound());
    CHECK(CancelAndDrainReloadCycle(s.gate,s.input.identity,s.control.cycle,true));
    CHECK(!s.gate.TakeAcknowledgement(s.input.identity,s.control.cycle,s.input.nowNs));
    for(const auto& b:s.input.branches)CHECK(b.loaded==7&&b.reserve==20);return 0;
}
int MonotonicReceiptsAndFiniteLifetimes(){Simulation s;CHECK(s.Arm());CHECK(CancelAndDrainReloadCycle(s.gate,s.input.identity,s.control.cycle,true));
    ReloadRetirementReceipts receipts;const auto now=s.input.nowNs;
    const auto a=receipts.Observe(s.gate,s.input.identity,s.control.cycle,now,true);CHECK(a&&a->event==1&&a->observedNs==now&&a->deadlineNs==now+200000000);
    CHECK(!receipts.Observe(s.gate,s.input.identity,s.control.cycle,now,true));CHECK(!receipts.Observe(s.gate,s.input.identity,s.control.cycle,now-1,true));
    CHECK(!receipts.Observe(s.gate,s.input.identity,s.control.cycle,now+1,false));
    const auto b=receipts.Observe(s.gate,s.input.identity,s.control.cycle,now+200000001,true);CHECK(b&&b->event==2&&b->observedNs>a->deadlineNs);
    CHECK(a->observedNs==now&&a->deadlineNs==now+200000000); // No old receipt is restamped.
    ReloadRetirementReceipts exhausted(UINT64_MAX);CHECK(!exhausted.Observe(s.gate,s.input.identity,s.control.cycle,now,true));
    CHECK(!receipts.Observe(s.gate,s.input.identity,s.control.cycle,INT64_MAX-10,true));return 0;
}
int NewArrivalCannotTakeOldHeldDecision(){Simulation s;CHECK(s.Arm()&&s.Tick());
    std::atomic_flag entry=ATOMIC_FLAG_INIT;std::atomic<unsigned> active=1;std::atomic<std::uint64_t> revision=7;
    std::atomic<bool> arrived=false,release=false,admitted=false;
    CHECK(!entry.test_and_set());const auto before=revision.load();
    // Same callback admission protocol as runtime Active. It never waits for
    // retirement and cannot reach Evaluate on a denied entry.
    std::thread callback([&]{const bool permit=!entry.test_and_set(std::memory_order_acquire);++active;++revision;
        admitted=permit;if(permit)entry.clear(std::memory_order_release);arrived=true;
        while(!release.load())std::this_thread::yield();++revision;--active;});
    while(!arrived.load())std::this_thread::yield();
    s.gate.Cancel();const bool quiet=active.load()==1&&revision.load()==before;
    const bool drained=CancelAndDrainReloadCycle(s.gate,s.input.identity,s.control.cycle,quiet);
    release=true;callback.join();entry.clear();CHECK(!admitted.load()&&!quiet&&!drained);
    // A later uncontended pass can retire the cancelled cycle. Its old held
    // decisions remain forbidden; no callback is replayed or ammo credited.
    CHECK(CancelAndDrainReloadCycle(s.gate,s.input.identity,s.control.cycle,true));
    const auto d=s.Begin(0);CHECK(!d.hold&&!s.gate.Allows(d,s.input.nowNs));return 0;
}
int CountObservationStartsAfterAck(){
    constexpr std::int64_t owner=1000000000,ack=owner+30000000,counts=owner+50000000,after=counts+1000000;
    const auto deadline=ReloadReserveReadDeadline(owner,owner+120000000,counts,after);
    CHECK(deadline&&*deadline==owner+120000000);CHECK(counts>ack&&owner<ack);
    CHECK(!ReloadReserveReadDeadline(owner,owner+120000000,counts,owner+120000000));
    CHECK(!ReloadReserveReadDeadline(owner,owner+120000000,counts,counts-1));
    CHECK(!ReloadReserveReadDeadline(owner,owner+120000000,owner-1,after));
    CHECK(!ReloadReserveReadDeadline(owner,owner+250000001,counts,after));
    CHECK(!ReloadReserveReadDeadline(INT64_MAX-100,INT64_MAX,INT64_MAX-50,INT64_MAX-40));return 0;
}
int DisabledRuntimeHasNoRetirement(){CHECK(!reloadFlowRuntime::RetireRequestCycle({},1));CHECK(!reloadFlowRuntime::ReadReserve());return 0;}
}
int main(){if(ExactOldIdentityAndCycleOnly()||OrphanDrainPreservesUnknownAndAmmo()||NativeCompletionNeverUndone()||
    MonotonicReceiptsAndFiniteLifetimes()||NewArrivalCannotTakeOldHeldDecision()||CountObservationStartsAfterAck()||DisabledRuntimeHasNoRetirement())return 1;
    std::printf("Seven retirement/read-observation groups passed; no game process used.\n");return 0;}
