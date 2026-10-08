#include "Test.h"
#include "Bc2ReloadDeferredCompletionTests.h"
#include "Bc2ReloadFlowRuntime.h"
#include "Bc2ReloadInvocationEntry.h"
#include "Bc2ReloadPhaseRetry.h"
#include "Bc2ReloadPolicyLock.h"
#include "Bc2ReloadCohortRetry.h"
#include <barrier>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>
#include <vector>
using namespace fvr::bc2;
namespace {
int NativeReadinessRequiresAllThreeCoherentInputStates(){
    ReloadHoldIdentity id;id.owner={0x10000,0x20000,0x30000,0x40000,1,2,7};
    id.firing={0x50000,0x60000,0x70000};id.serverPlayer=0x80000;id.serverSoldier=0x90000;id.serverItem=0xa0000;
    std::array<ReloadFlowBoundary,3> a{};const std::array<int,3> caps{8,8,8};
    for(unsigned n=0;n<3;++n){auto& b=a[n];b.owner=id.owner;b.snapshotSequence=12;b.branch=std::uint8_t(n);b.firing=id.firing[n];
        b.wrapperOffset=n==0?0x3c:n==1?0x40:0x10;b.loaded=6;b.reserve=12;b.current=b.next=2;
        if(n==2){b.serverPlayer=id.serverPlayer;b.serverSoldier=id.serverSoldier;b.serverItem=id.serverItem;}}
    CHECK(ReloadReserveInputReady(id,a,a,caps,caps));
    CHECK(ReloadReserveIdle(id,a,a,caps,caps));
    for(unsigned n=0;n<3;++n){auto busy=a;busy[n].current=busy[n].next=7;busy[n].timer=.18f;
        CHECK(ReloadReserveCopiesAgree(id,busy,busy,caps,caps)); // Reserve credit is still real while pumping.
        CHECK(!ReloadReserveInputReady(id,busy,busy,caps,caps));
        CHECK(!ReloadReserveIdle(id,busy,busy,caps,caps));
        busy=a;busy[n].next=9;CHECK(!ReloadReserveInputReady(id,busy,busy,caps,caps));
        busy=a;busy[n].timer=.01f;CHECK(!ReloadReserveInputReady(id,busy,busy,caps,caps));
        busy=a;busy[n].owner.equipGeneration++;CHECK(!ReloadReserveInputReady(id,a,busy,caps,caps));
    }
    auto full=a;for(auto& b:full)b.loaded=8;CHECK(!ReloadReserveInputReady(id,full,full,caps,caps));
    CHECK(ReloadReserveIdle(id,full,full,caps,caps));
    auto empty=a;for(auto& b:empty)b.reserve=0;CHECK(!ReloadReserveInputReady(id,empty,empty,caps,caps));
    CHECK(ReloadReserveIdle(id,empty,empty,caps,caps));
    for(auto& b:full)b.reserve=0;CHECK(ReloadReserveIdle(id,full,full,caps,caps));
    for(unsigned n=0;n<3;++n){auto busy=full;busy[n].next=9;CHECK(!ReloadReserveIdle(id,busy,busy,caps,caps));
        busy=full;busy[n].timer=.01f;CHECK(!ReloadReserveIdle(id,busy,busy,caps,caps));
        busy=full;busy[n].owner.equipGeneration++;CHECK(!ReloadReserveIdle(id,full,busy,caps,caps));}
    auto changed=a;changed[2].serverItem++;CHECK(!ReloadReserveInputReady(id,a,changed,caps,caps));
    CHECK(!Bc2AmmoReserveLease{}.reloadInputReady);
    CHECK(!Bc2AmmoReserveLease{}.allThreeIdle);
    return 0;
}
int PolicyLockRecoversRealOverlap(){
    std::atomic_flag gate{};std::atomic<unsigned> inside=0;std::barrier waiting(2),released(2);
    std::thread contender;bool exclusive=false;ReloadPolicyLockEvidence result;
    {
        ReloadPolicyLock first(gate,[]{return 100ll;},[]{});CHECK(first.Held());inside=1;
        contender=std::thread([&]{std::int64_t now=100;bool firstPause=true;
            ReloadPolicyLock second(gate,[&]{return ++now;},[&]{if(firstPause){firstPause=false;waiting.arrive_and_wait();released.arrive_and_wait();}});
            result=second.Evidence();if(second.Held()){exclusive=inside.fetch_add(1)==0;--inside;}
        });
        waiting.arrive_and_wait();inside=0;
    }
    released.arrive_and_wait();contender.join();
    CHECK(exclusive&&result.held&&result.contended&&result.attempts==2&&result.failure==ReloadPolicyLockFailure::None);
    CHECK(!gate.test()&&inside==0);return 0;
}
int PolicyLockBoundsAndOwnerRelease(){
    for(unsigned mode=0;mode<6;++mode){std::atomic_flag gate{};gate.test_and_set();std::int64_t now=100;unsigned calls=0,pauses=0;
        {
            ReloadPolicyLock lock(gate,[&]()->std::int64_t{++calls;
                if(mode==0)return now+=100000; // Deadline exhausted with a live owner.
                if(mode==1)return 100; // Clock stalls: fixed iteration bound still wins.
                if(mode==2)return calls==1?100:99; // Regressed clock.
                if(mode==3)return 0; // Unavailable clock.
                return calls<3?100:250100; // Acquired at the boundary, rejected before policy entry.
            },[&]{++pauses;if(mode==4)gate.clear(std::memory_order_release);},mode!=5);
            CHECK(!lock.Held());const auto& e=lock.Evidence();CHECK(e.contended&&e.attempts<=ReloadPolicyLock::MaxAttempts);
            if(mode==0||mode==4)CHECK(e.failure==ReloadPolicyLockFailure::Deadline);
            if(mode==1)CHECK(e.failure==ReloadPolicyLockFailure::AttemptLimit&&e.attempts==ReloadPolicyLock::MaxAttempts);
            if(mode==2||mode==3)CHECK(e.failure==ReloadPolicyLockFailure::Clock);
            if(mode==5)CHECK(e.attempts==1&&calls==0&&pauses==0); // Non-request legacy behavior.
        }
        CHECK(gate.test()==(mode!=4)); // A failed borrower never clears somebody else's lock.
    }
    std::atomic_flag gate{};
    {ReloadPolicyLock outer(gate,[]{return 100ll;},[]{});CHECK(outer.Held());
        std::int64_t now=100;{ReloadPolicyLock recursive(gate,[&]{return now+=100000;},[]{});CHECK(!recursive.Held());}
        CHECK(gate.test());}
    CHECK(!gate.test());return 0;
}
int ConcurrentOrdinaryAdmissions(){
    std::atomic<unsigned> active=0;std::atomic<std::uint64_t> revision=0;std::atomic_flag exclusive{};
    std::barrier rendezvous(3);std::array<bool,2> admitted{};
    const auto invoke=[&](unsigned index){
        admitted[index]=EnterReloadInvocation(active,revision,exclusive);
        rendezvous.arrive_and_wait();rendezvous.arrive_and_wait();ExitReloadInvocation(active,revision);
    };
    std::thread a(invoke,0),b(invoke,1);rendezvous.arrive_and_wait();
    const bool overlap=admitted[0]&&admitted[1]&&active.load()==2&&revision.load()==2&&!exclusive.test();
    rendezvous.arrive_and_wait();a.join();b.join();
    CHECK(overlap&&active.load()==0&&revision.load()==4&&!exclusive.test());return 0;
}
int ExclusiveArrivalRequiresFreshDrain(){
    std::atomic<unsigned> active=0;std::atomic<std::uint64_t> revision=0;std::atomic_flag exclusive{};
    CHECK(EnterReloadInvocation(active,revision,exclusive)); // The API invocation itself.
    {
        ReloadInvocationExclusion owner(exclusive,active,revision);CHECK(owner.held&&owner.Quiet());
        {ReloadInvocationExclusion contender(exclusive,active,revision);CHECK(!contender.held&&!contender.Quiet());}
        CHECK(exclusive.test()&&owner.Quiet()); // A failed guard cannot release another guard.
        std::barrier rendezvous(2);bool admitted=true;
        std::thread entrant([&]{admitted=EnterReloadInvocation(active,revision,exclusive);
            rendezvous.arrive_and_wait();rendezvous.arrive_and_wait();ExitReloadInvocation(active,revision);});
        rendezvous.arrive_and_wait();const bool refused=!admitted&&active.load()==2&&!owner.Quiet();
        rendezvous.arrive_and_wait();entrant.join();
        CHECK(refused&&active.load()==1&&!owner.Quiet()); // Count returning to one cannot hide the arrival.
    }
    CHECK(!exclusive.test());
    {ReloadInvocationExclusion retry(exclusive,active,revision);CHECK(retry.held&&retry.Quiet());}
    ExitReloadInvocation(active,revision);CHECK(active.load()==0&&revision.load()==4);return 0;
}
int AlreadyAdmittedInvocationPreventsDrain(){
    std::atomic<unsigned> active=0;std::atomic<std::uint64_t> revision=0;std::atomic_flag exclusive{};
    CHECK(EnterReloadInvocation(active,revision,exclusive));
    CHECK(EnterReloadInvocation(active,revision,exclusive));
    {ReloadInvocationExclusion exclusion(exclusive,active,revision);CHECK(exclusion.held&&!exclusion.Quiet());
        ExitReloadInvocation(active,revision);CHECK(active.load()==1&&!exclusion.Quiet());}
    {ReloadInvocationExclusion retry(exclusive,active,revision);CHECK(retry.held&&retry.Quiet());}
    ExitReloadInvocation(active,revision);CHECK(active.load()==0&&revision.load()==4);return 0;
}
struct Fixture {
    static constexpr unsigned base=0x400000,player=0x10000,soldier=0x12000,weak=0x13000,inventory=0x14000,
        items=0x15000,weapon=0x16000,data=0x17000,firingData=0x18000,primary=0x19000,branch0=0x1a000,branch1=0x1b000;
    std::vector<std::byte> bytes=std::vector<std::byte>(0x20000);
    ReloadStateSnapshot snapshot{};ReloadFlowBinding binding{};
    unsigned failAt=0,raceAt=0,raceTarget=0,raceValue=0,raceCalls=0;
    template<class T>void Put(unsigned at,T value){std::memcpy(bytes.data()+at-0x10000,&value,sizeof(value));}
    Fixture(){
        binding.state.preferredBase=base;binding.state.imageSize=0x100000;binding.state.firingVtableRva=0x1234;
        snapshot.owner={player,soldier,weak,weapon,1,2,3};snapshot.sequence=4;snapshot.observedNs=100;
        snapshot.inventory=inventory;snapshot.selectedSlot=0;snapshot.soldierFlags=1;
        snapshot.config.weaponData=data;snapshot.config.firingData=firingData;snapshot.config.primaryFire=primary;
        snapshot.config.ammoAddress=primary+0x170;snapshot.config.fireLogicType=1;snapshot.config.reloadType=0;
        snapshot.branches[0].address=branch0;snapshot.branches[0].wrapperOffset=0x3c;
        snapshot.branches[1].address=branch1;snapshot.branches[1].wrapperOffset=0x40;
        Put<std::uint8_t>(player+0xccd,8);Put(player+0xc54,weak);Put(weak,soldier+4);Put(soldier+0x220,player);Put(player+0xc68,soldier);
        Put<std::uint8_t>(soldier+0x114,1);Put(soldier+0x248,inventory);Put(soldier+0x24c,inventory);
        Put(soldier+0x260,items);Put(soldier+0x264,items+8);Put(items,weapon);Put(inventory+0x14c,0u);
        Put(weapon+0x3c,branch0);Put(weapon+0x40,branch1);Put(weapon+4,data);Put(data+0x98,firingData);Put(firingData+0x40,primary);
        Put(primary+0x24,1u);Put(primary+0x20,0u);
        for(auto branch:{branch0,branch1}){Put(branch,base+binding.state.firingVtableRva);Put(branch+8,firingData);Put(branch+12,primary+0x170);
            Put(branch+0x3c,11u);Put(branch+0x40,10u);Put(branch+0x44,12u);Put(branch+0x50,.72f);Put(branch+0x7c,4);Put(branch+0x80,8);}
    }
    ReloadStateMemory Memory(){return {this,[](void* context,unsigned at,void* output,std::size_t size){
        auto& f=*static_cast<Fixture*>(context);if(at==f.failAt||at<0x10000||std::uint64_t(at)+size>0x10000+f.bytes.size())return false;
        if(at==f.raceAt&&++f.raceCalls==2)f.Put(f.raceTarget,f.raceValue);
        std::memcpy(output,f.bytes.data()+at-0x10000,size);return true;
    },nullptr};}
    std::optional<ReloadFlowBoundary> Read(unsigned branch=branch0){return ReadReloadFlowBoundary(Memory(),binding,base,snapshot,100000100,branch,200);}
    bool Owner(unsigned branch=branch0){return ReadReloadFlowOwner(Memory(),binding,base,snapshot,100000100,branch,200);}
};
int BoundariesAndNoWrites(){
    Fixture f;const auto original=f.bytes;auto a=f.Read();auto b=f.Read(f.branch1);
    CHECK(a&&b&&a->owner==f.snapshot.owner&&a->snapshotSequence==4&&a->branch==0&&b->branch==1);
    CHECK(a->wrapperOffset==0x3c&&b->wrapperOffset==0x40&&a->current==11&&a->previous==10&&a->next==12);
    CHECK(a->loaded==4&&a->reserve==8&&Near(a->timer,.72f)&&f.bytes==original);
    f.Put(f.branch0+0x7c,5);f.Put(f.branch0+0x80,7);a=f.Read();CHECK(a&&a->loaded==5&&a->reserve==7);
    // Eligibility flag changes stay observable, never identify authority.
    f.Put<std::uint8_t>(f.soldier+0x114,17);a=f.Read();CHECK(a&&a->soldierFlags==17);
    return 0;
}
int CohortRetryReconstructsAfterRealOverlap(){
    Fixture f;std::barrier rendezvous(2);unsigned rawReads=0,fullReads=0;std::array<unsigned,3> branchReads{};
    struct Shared {Fixture* f;std::barrier<>* barrier;unsigned* reads;} shared{&f,&rendezvous,&rawReads};
    const ReloadStateMemory memory{&shared,[](void* ptr,unsigned at,void* out,std::size_t size){
        auto& s=*static_cast<Shared*>(ptr);auto base=s.f->Memory();const bool okay=base.read(base.context,at,out,size);
        if(okay&&at==Fixture::branch1&&size==0xb0&&++*s.reads==1){s.barrier->arrive_and_wait();s.barrier->arrive_and_wait();}
        return okay;
    },nullptr};
    std::thread engine([&]{rendezvous.arrive_and_wait();f.Put(Fixture::branch1+0x50,.234807f);
        f.Put(Fixture::branch0+0x50,.65f);rendezvous.arrive_and_wait();});
    const auto read=[&](std::int64_t now,ReloadCohortRejection& d)->std::optional<std::array<ReloadFlowBoundary,3>>{
        ++fullReads;d={};std::array<ReloadFlowBoundary,3> fresh{};
        for(unsigned n=0;n<3;++n){++branchReads[n];const auto address=n==1?f.branch1:f.branch0;
            auto b=ReadReloadFlowBoundary(memory,f.binding,f.base,f.snapshot,100000100,address,now,&d.client);
            if(!b){d.stage=3;d.callerBranch=2;d.readBranch=n;d.capacityOkay=true;return {};}
            fresh[n]=*b;
        }return fresh;
    };
    ReloadCohortRejection d;ReloadCohortRetryAttempt attempt;
    const auto result=ReadReloadCohortCoherent(read,[]{return 201ll;},200,d,attempt);engine.join();
    CHECK(result&&attempt.attempted&&attempt.recovered&&fullReads==2);
    CHECK(branchReads[0]==2&&branchReads[1]==2&&branchReads[2]==1);
    CHECK(attempt.first.client.failure==ReloadFlowBoundaryFailure::ChangedState&&attempt.first.client.changedOffset==0x50);
    CHECK(Near((*result)[0].timer,.65f)&&Near((*result)[1].timer,.234807f)); // No successful prefix from failed attempt survives.
    CHECK((*result)[0].owner==f.snapshot.owner&&(*result)[0].snapshotSequence==f.snapshot.sequence);
    CHECK(attempt.firstNs==200&&attempt.retryNs==201&&!attempt.clockRejected);return 0;
}
int CohortRetryRejectsOtherFailures(){
    for(unsigned mode=0;mode<8;++mode){unsigned reads=0,clocks=0;ReloadCohortRejection final;ReloadCohortRetryAttempt attempt;
        const auto result=ReadReloadCohortCoherent([&](std::int64_t,ReloadCohortRejection& d)->std::optional<unsigned>{
            ++reads;d={3,2,1,false,true};d.client.failure=ReloadFlowBoundaryFailure::ChangedState;
            if(mode==0)d.client.failure=ReloadFlowBoundaryFailure::StateIdentity;
            if(mode==1)d.client.failure=ReloadFlowBoundaryFailure::ScopeBefore;
            if(mode==2)d.client.failure=ReloadFlowBoundaryFailure::StateReadAfter;
            if(mode==3)d.capacityOkay=false;
            if(mode==4)d.readBranch=3;
            if(mode==5)d.stage=5;
            if(mode==6)d.boundaryOkay=true;
            if(mode==7){d.readBranch=2;d.server.failure=ReloadServerBoundaryFailure::State;d.server.stateFailure=4;}
            return {};
        },[&]{++clocks;return 201ll;},200,final,attempt);
        CHECK(!result&&reads==1&&clocks==0&&!attempt.attempted);
    }return 0;
}
int CohortRetryNeverRenewsOrLoops(){
    for(unsigned mode=0;mode<5;++mode){Fixture f;f.raceAt=f.branch1;f.raceTarget=f.branch1+0x50;f.raceValue=0x3e70715b;
        unsigned reads=0;ReloadCohortRejection final;ReloadCohortRetryAttempt attempt;
        const auto read=[&](std::int64_t now,ReloadCohortRejection& d)->std::optional<ReloadFlowBoundary>{
            ++reads;d={};
            if(reads==2&&mode==0){f.raceCalls=0;f.raceValue=0x3e75715b;} // Second race is terminal; no third observation.
            if(reads==2&&mode==2)f.Put(f.player+0xc54,f.weak+4); // Exact expected actor cannot be replaced.
            if(reads==2&&mode==3)f.Put(f.firingData+0x40,f.primary+4); // Nor can config be replaced.
            auto result=ReadReloadFlowBoundary(f.Memory(),f.binding,f.base,f.snapshot,100000100,f.branch1,now,&d.client);
            if(!result){d.stage=3;d.callerBranch=2;d.readBranch=1;d.capacityOkay=true;}return result;
        };
        const auto result=ReadReloadCohortCoherent(read,[&]{return mode==1?100000100ll:mode==4?199ll:201ll;},200,final,attempt);
        CHECK(!result&&attempt.attempted&&!attempt.recovered&&reads==(mode==4?1u:2u));
        if(mode==0)CHECK(final.client.failure==ReloadFlowBoundaryFailure::ChangedState);
        if(mode==1)CHECK(final.client.failure==ReloadFlowBoundaryFailure::Lease);
        if(mode==2||mode==3)CHECK(final.client.failure==ReloadFlowBoundaryFailure::ScopeBefore);
        if(mode==4)CHECK(attempt.clockRejected);
    }
    // Server raw-state race has the same two-observation bound; state/identity
    // errors are not interchangeable with a coherence failure.
    unsigned reads=0;ReloadCohortRejection final;ReloadCohortRetryAttempt attempt;
    auto server=ReadReloadCohortCoherent([&](std::int64_t,ReloadCohortRejection& d)->std::optional<unsigned>{
        d={};if(++reads==1){d={3,0,2,false,true};d.server.failure=ReloadServerBoundaryFailure::State;d.server.stateFailure=3;return {};}
        return 7;
    },[]{return 201ll;},200,final,attempt);
    CHECK(server==7u&&reads==2&&attempt.recovered);return 0;
}
int OwnerAndLeaseRejections(){
    const std::array<std::pair<unsigned,unsigned>,13> changes{{
        {Fixture::player+0xc54,Fixture::weak+4},{Fixture::weak,Fixture::soldier+8},{Fixture::soldier+0x220,Fixture::player+4},
        {Fixture::player+0xc68,Fixture::soldier+4},{Fixture::soldier+0x24c,Fixture::inventory+4},{Fixture::inventory+0x14c,2},
        {Fixture::items+4,Fixture::weapon},{Fixture::weapon+0x40,Fixture::branch0},{Fixture::weapon+4,Fixture::data+4},
        {Fixture::data+0x98,Fixture::firingData+4},{Fixture::firingData+0x40,Fixture::primary+4},
        {Fixture::primary+0x24,2},{Fixture::branch0,Fixture::base+0x1238}}};
    for(auto [at,value]:changes){Fixture f;f.Put(at,value);CHECK(!f.Read()&&!f.Owner());}
    Fixture f;CHECK(!f.Read(0x20000));
    for(auto [deadline,now]:{std::pair<std::int64_t,std::int64_t>{100,100},{200,200},{250000101,200},{200,99}})
        {CHECK(!ReadReloadFlowBoundary(f.Memory(),f.binding,f.base,f.snapshot,deadline,f.branch0,now));
         CHECK(!ReadReloadFlowOwner(f.Memory(),f.binding,f.base,f.snapshot,deadline,f.branch0,now));}
    f.snapshot.owner.actorGeneration=0;CHECK(!f.Read());f.snapshot.owner.actorGeneration=1;
    f.snapshot.branches[1].wrapperOffset=0x3c;CHECK(!f.Read());return 0;
}
int MalformedAndChangingState(){
    for(unsigned offset:{0x3cu,0x40u,0x44u}){Fixture f;f.Put(f.branch0+offset,16u);CHECK(!f.Read());}
    for(float timer:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),1000001.f}){
        Fixture f;f.Put(f.branch0+0x50,timer);CHECK(!f.Read());}
    for(unsigned offset:{0x7cu,0x80u}){Fixture f;f.Put(f.branch0+offset,-2);CHECK(!f.Read());}
    {Fixture f;f.failAt=f.weapon+4;CHECK(!f.Read());}
    {Fixture f;f.raceAt=f.branch0;f.raceTarget=f.branch0+0x7c;f.raceValue=5;CHECK(!f.Read());}
    {Fixture f;f.raceAt=f.player+0xc54;f.raceTarget=f.weak;f.raceValue=f.soldier+8;CHECK(!f.Read());}
    return 0;
}
int RejectedReadProvenance(){
    Fixture flags;flags.raceAt=flags.soldier+0x114;flags.raceTarget=flags.soldier+0x114;flags.raceValue=17;
    ReloadFlowBoundaryDiagnostic d;
    CHECK(!ReadReloadFlowBoundary(flags.Memory(),flags.binding,flags.base,flags.snapshot,100000100,flags.branch0,200,&d));
    CHECK(d.failure==ReloadFlowBoundaryFailure::ChangedScope&&d.beforeSoldierFlags==1&&d.afterSoldierFlags==17&&d.differsOnlySoldierFlags);
    Fixture ammo;ammo.raceAt=ammo.branch0;ammo.raceTarget=ammo.branch0+0x7c;ammo.raceValue=5;
    CHECK(!ReadReloadFlowBoundary(ammo.Memory(),ammo.binding,ammo.base,ammo.snapshot,100000100,ammo.branch0,200,&d));
    CHECK(d.failure==ReloadFlowBoundaryFailure::ChangedState&&d.changedOffset==0x7c&&d.changedBefore==4&&d.changedAfter==5);
    Fixture owner;owner.raceAt=owner.player+0xc54;owner.raceTarget=owner.weak;owner.raceValue=owner.soldier+8;
    CHECK(!ReadReloadFlowBoundary(owner.Memory(),owner.binding,owner.base,owner.snapshot,100000100,owner.branch0,200,&d));
    CHECK(d.failure==ReloadFlowBoundaryFailure::ScopeAfter&&!d.differsOnlySoldierFlags);
    Fixture clean;CHECK(ReadReloadFlowBoundary(clean.Memory(),clean.binding,clean.base,clean.snapshot,100000100,clean.branch0,200,&d));
    CHECK(d.failure==ReloadFlowBoundaryFailure::None&&d.changedOffset==UINT32_MAX&&!d.differsOnlySoldierFlags);
    return 0;
}
int ExactPhaseRetryPolicy(){
    ReloadFlowBoundaryDiagnostic d;d.differsOnlySoldierFlags=true;d.beforeSoldierFlags=3;d.afterSoldierFlags=19;
    for(unsigned n=0;n<=unsigned(ReloadFlowBoundaryFailure::StateRange);++n){d.failure=ReloadFlowBoundaryFailure(n);
        CHECK(ReloadPhaseOnlyRace(d)==(d.failure==ReloadFlowBoundaryFailure::ChangedScope));}
    d.failure=ReloadFlowBoundaryFailure::ChangedScope;
    for(unsigned bit:{0u,1u,2u,4u,8u,17u,32u,64u,128u}){d.afterSoldierFlags=std::uint8_t(3^bit);CHECK(!ReloadPhaseOnlyRace(d));}
    d.afterSoldierFlags=19;d.differsOnlySoldierFlags=false;CHECK(!ReloadPhaseOnlyRace(d));return 0;
}
int ClientPhaseRetryUsesOnlyFreshCompleteReads(){
    for(bool owner:{false,true})for(unsigned scenario=0;scenario<7;++scenario){
        Fixture f;f.raceAt=f.soldier+0x114;f.raceTarget=f.soldier+0x114;f.raceValue=scenario==4?3:17;
        if(scenario==5){f.raceAt=f.player+0xccd;f.raceTarget=f.weak;f.raceValue=f.soldier+8;}
        if(scenario==6)f.raceAt=0;
        unsigned reads=0,clocks=0;ReloadFlowBoundaryDiagnostic d;ReloadPhaseRetryAttempt retry;
        const auto result=ReadReloadPhaseCoherent([&](std::int64_t now,ReloadFlowBoundaryDiagnostic& diagnostic){++reads;
            return owner?ReadReloadFlowOwner(f.Memory(),f.binding,f.base,f.snapshot,100000100,f.branch0,now,&diagnostic):
                bool(ReadReloadFlowBoundary(f.Memory(),f.binding,f.base,f.snapshot,100000100,f.branch0,now,&diagnostic));
        },[&]{++clocks;
            if(scenario==1){f.raceCalls=0;f.raceValue=1;}
            if(scenario==2)f.Put(f.weak,f.soldier+8);
            return scenario==3?100000100ll:201ll;
        },200,d,retry);
        CHECK(result==(scenario==0||scenario==6));
        CHECK(reads==(scenario<4?2u:1u)&&clocks==(scenario<4?1u:0u));
        CHECK(retry.attempted==(scenario<4)&&retry.recovered==(scenario==0));
        if(scenario<4)CHECK(retry.first.beforeFlags==1&&retry.first.afterFlags==17&&retry.firstNs==200);
        if(scenario==3)CHECK(d.failure==ReloadFlowBoundaryFailure::Lease&&retry.retryNs==100000100);
    }
    // A recovered full boundary contains newly sampled counts/timer; none are
    // copied from the failed attempt and no allowance reaches the strict reader.
    Fixture f;f.raceAt=f.soldier+0x114;f.raceTarget=f.soldier+0x114;f.raceValue=17;
    ReloadFlowBoundaryDiagnostic d;ReloadPhaseRetryAttempt retry;
    auto boundary=ReadReloadPhaseCoherent([&](std::int64_t now,ReloadFlowBoundaryDiagnostic& diagnostic){
        return ReadReloadFlowBoundary(f.Memory(),f.binding,f.base,f.snapshot,100000100,f.branch0,now,&diagnostic);
    },[&]{f.Put(f.branch0+0x7c,5);f.Put(f.branch0+0x80,7);f.Put(f.branch0+0x50,.4f);return 201ll;},200,d,retry);
    CHECK(boundary&&retry.recovered&&boundary->loaded==5&&boundary->reserve==7&&Near(boundary->timer,.4f));
    return 0;
}
int StructuralOwnerDoesNotRequireFrozenSiblingState(){
    // State and raw animation/update words can advance on a sibling thread.
    // They cannot produce ammo/hold authority through this bool-only reader.
    for(unsigned offset:{0x3cu,0x40u,0x44u,0x48u,0x50u,0x60u,0x74u,0x7cu,0x80u,0x94u,0x9cu,0xa4u,0xa8u}){
        Fixture owner;owner.raceAt=owner.branch0;owner.raceTarget=owner.branch0+offset;owner.raceValue=0x55667788;
        CHECK(owner.Owner()&&owner.raceCalls==2);
        Fixture boundary;boundary.raceAt=boundary.branch0;boundary.raceTarget=boundary.branch0+offset;boundary.raceValue=0x55667788;
        ReloadFlowBoundaryDiagnostic d;
        CHECK(!ReadReloadFlowBoundary(boundary.Memory(),boundary.binding,boundary.base,boundary.snapshot,100000100,boundary.branch0,200,&d));
        CHECK(d.failure==ReloadFlowBoundaryFailure::ChangedState&&d.changedOffset==offset);
    }
    for(unsigned offset:{0u,8u,12u}){Fixture f;f.raceAt=f.branch0;f.raceTarget=f.branch0+offset;f.raceValue=0;
        CHECK(!f.Owner());}
    {Fixture f;f.raceAt=f.player+0xc54;f.raceTarget=f.weak;f.raceValue=f.soldier+8;CHECK(!f.Owner());}
    {Fixture f;f.raceAt=f.soldier+0x114;f.raceTarget=f.soldier+0x114;f.raceValue=17;
        ReloadFlowBoundaryDiagnostic d;CHECK(!ReadReloadFlowOwner(f.Memory(),f.binding,f.base,f.snapshot,100000100,f.branch0,200,&d));
        CHECK(d.failure==ReloadFlowBoundaryFailure::ChangedScope&&d.differsOnlySoldierFlags);}
    {Fixture f;CHECK(f.Owner()&&f.Owner(f.branch1)&&!f.Owner(0x20000));f.failAt=f.branch0+8;CHECK(!f.Owner());}
    return 0;
}
ReloadFlowEventInput Input(const ReloadFlowBoundary& boundary,ReloadFlowEvent kind=ReloadFlowEvent::Update){
    ReloadFlowEventInput in;in.kind=kind;in.thread=1;in.depth=1;in.nowNs=1000;in.tickMs=20;in.boundary=boundary;return in;
}
ReloadFlowEventEnd End(const ReloadFlowBoundary& boundary,unsigned thread=1){
    ReloadFlowEventEnd end;end.thread=thread;end.nowNs=2000;end.tickMs=21;end.boundary=boundary;return end;
}
int NestingAndTransferResults(){
    Fixture f;const auto state=*f.Read();auto records=std::make_unique<ReloadFlowRecords>();
    auto in=Input(state);const auto update=records->Begin(in);CHECK(update==1);
    in=Input(state,ReloadFlowEvent::Commit);in.parent=update;in.update=update;in.depth=2;const auto commit=records->Begin(in);CHECK(commit==2);
    auto committed=state;committed.previous=state.current;committed.current=state.next;CHECK(records->End(commit,End(committed)));
    in=Input(committed,ReloadFlowEvent::Transfer);in.parent=update;in.update=update;in.depth=2;in.transferPath=ReloadTransferPath::OrdinaryState12;
    const auto transfer=records->Begin(in);CHECK(transfer==3);auto transferred=committed;transferred.loaded++;transferred.reserve--;
    CHECK(records->End(transfer,End(transferred)));CHECK(records->End(update,End(transferred)));
    const auto rows=records->Records();CHECK(rows.size()==3&&rows[2].entry.parent==1&&rows[2].entry.update==1);
    CHECK(rows[2].identityRetained&&rows[2].exit.boundary->loaded==5&&rows[2].entry.boundary.loaded==4);
    // External zero-duration preparation has no manufactured Update parent.
    in=Input(transferred,ReloadFlowEvent::Transfer);in.transferPath=ReloadTransferPath::ZeroDurationPreparation;
    CHECK(records->Begin(in)==4&&records->Records()[3].entry.update==0);return 0;
}
int RestoreSourceAndDestination(){
    Fixture f;const auto state=*f.Read();auto records=std::make_unique<ReloadFlowRecords>();
    auto in=Input(state,ReloadFlowEvent::Restore);in.context=0x20000;in.snapshotCopied=true;
    const auto put=[&](unsigned at,auto value){std::memcpy(in.copiedSnapshot.data()+at,&value,sizeof(value));};
    put(0,1u);put(4,2u);put(8,0.f);put(0x18,8);put(0x1c,4);
    const auto id=records->Begin(in);CHECK(id==1);auto after=state;after.previous=state.current;after.current=1;after.next=2;after.timer=0;after.loaded=8;after.reserve=4;
    auto end=End(after);end.snapshotCopied=true;end.copiedSnapshot=in.copiedSnapshot;
    CHECK(records->End(id,end));const auto good=records->Records()[0];CHECK(ReloadRestoreMatched(good));
    CHECK(good.entry.update==0&&good.entry.parent==0);
    for(unsigned n=0;n<11;++n){auto bad=good;switch(n){
        case 0:bad.entry.kind=ReloadFlowEvent::Transfer;break;case 1:bad.finished=false;break;case 2:bad.identityRetained=false;break;
        case 3:bad.entry.snapshotCopied=false;break;case 4:bad.exit.snapshotCopied=false;break;case 5:bad.exit.copiedSnapshot[0x3f]^=std::byte{1};break;
        case 6:bad.exit.boundary->loaded--;break;case 7:bad.exit.boundary->reserve++;break;case 8:bad.exit.boundary->previous=0;break;
        case 9:bad.exit.boundary->timer=.1f;break;case 10:bad.exit.boundary.reset();break;}
        CHECK(!ReloadRestoreMatched(bad));}
    return 0;
}
int InvalidNestingAndLifecycle(){
    Fixture f;const auto state=*f.Read();auto records=std::make_unique<ReloadFlowRecords>();auto in=Input(state);
    const auto id=records->Begin(in);CHECK(id);
    CHECK(!records->End(id,End(state,2)));
    auto ending=End(state);ending.nowNs=999;CHECK(!records->End(id,ending));
    in.parent=id;in.depth=2;in.kind=ReloadFlowEvent::Commit;in.thread=2;CHECK(!records->Begin(in));
    in.thread=1;in.boundary.owner.equipGeneration++;CHECK(!records->Begin(in));in.boundary=state;in.parent=500;CHECK(!records->Begin(in));
    in.parent=id;in.update=500;CHECK(!records->Begin(in));in.update=id;in.depth=9;CHECK(!records->Begin(in));
    ending=End(state);ending.boundary.reset();CHECK(records->End(id,ending));
    CHECK(records->Records()[0].finished&&!records->Records()[0].identityRetained);
    in.depth=2;CHECK(!records->Begin(in));CHECK(!records->End(id,End(state)));
    in=Input(state);const auto other=records->Begin(in);CHECK(other==2);ending=End(state);ending.boundary->owner.space++;
    CHECK(records->End(other,ending)&&!records->Records()[1].identityRetained);return 0;
}
int FixedStorageOverflow(){
    Fixture f;const auto state=*f.Read();auto records=std::make_unique<ReloadFlowRecords>();const auto in=Input(state);
    CHECK(records->Limit()==ReloadFlowRecords::Capacity);
    for(unsigned n=0;n<ReloadFlowRecords::Capacity;++n){const auto id=records->Begin(in);CHECK(id==n+1&&records->End(id,End(state)));}
    CHECK(!records->Begin(in)&&records->Dropped()==1&&records->Records().size()==ReloadFlowRecords::Capacity);
    CHECK(records->Records().front().id==1&&records->Records().back().finished);
    CHECK(!records->EnableRecoveryCapacity());
    auto recovery=std::make_unique<ReloadFlowRecords>();CHECK(recovery->EnableRecoveryCapacity());
    CHECK(recovery->Limit()==ReloadFlowRecords::RecoveryCapacity);
    for(unsigned n=0;n<ReloadFlowRecords::RecoveryCapacity;++n){const auto id=recovery->Begin(in);CHECK(id==n+1&&recovery->End(id,End(state)));}
    CHECK(!recovery->Begin(in)&&recovery->Dropped()==1&&!recovery->EnableRecoveryCapacity());
    CHECK(recovery->Records().size()==ReloadFlowRecords::RecoveryCapacity&&recovery->Records().back().finished);return 0;
}
}
#if defined(_M_IX86)
#include <Windows.h>
#include <MinHook.h>
namespace {
using UpdateFn=void(__thiscall*)(void*,void*,unsigned);using CommitFn=void(__thiscall*)(void*,void*);using TransferFn=void(__thiscall*)(void*,unsigned);
UpdateFn originalUpdate=nullptr;CommitFn originalCommit=nullptr;TransferFn originalTransfer=nullptr;CommitFn originalRestore=nullptr;
struct Fake {unsigned updates=0,commits=0,transfers=0,restores=0,extra=0,argument=0;void* context=nullptr;};unsigned callbacks=0;
__declspec(noinline) void __fastcall FakeUpdate(void* self,void*,void* context,unsigned extra){auto& f=*static_cast<Fake*>(self);++f.updates;f.context=context;f.extra=extra;}
__declspec(noinline) void __fastcall FakeCommit(void* self,void*,void* context){auto& f=*static_cast<Fake*>(self);++f.commits;f.context=context;}
__declspec(noinline) void __fastcall FakeTransfer(void* self,void*,unsigned argument){auto& f=*static_cast<Fake*>(self);++f.transfers;f.argument=argument;}
__declspec(noinline) void __fastcall FakeRestore(void* self,void*,void* context){auto& f=*static_cast<Fake*>(self);++f.restores;f.context=context;}
void __fastcall HookRestore(void* self,void*,void* context){++callbacks;originalRestore(self,context);}
void __fastcall HookUpdate(void* self,void*,void* context,unsigned extra){++callbacks;originalUpdate(self,context,extra);}
void __fastcall HookCommit(void* self,void*,void* context){++callbacks;originalCommit(self,context);}
void __fastcall HookTransfer(void* self,void*,unsigned argument){++callbacks;originalTransfer(self,argument);}
int SyntheticAbi(){
    CHECK(MH_Initialize()==MH_OK);CHECK(MH_CreateHook(FakeUpdate,HookUpdate,reinterpret_cast<void**>(&originalUpdate))==MH_OK);
    CHECK(MH_CreateHook(FakeCommit,HookCommit,reinterpret_cast<void**>(&originalCommit))==MH_OK);
    CHECK(MH_CreateHook(FakeTransfer,HookTransfer,reinterpret_cast<void**>(&originalTransfer))==MH_OK);CHECK(MH_CreateHook(FakeRestore,HookRestore,reinterpret_cast<void**>(&originalRestore))==MH_OK);CHECK(MH_EnableHook(MH_ALL_HOOKS)==MH_OK);
    UpdateFn volatile update=reinterpret_cast<UpdateFn>(FakeUpdate);CommitFn volatile commit=reinterpret_cast<CommitFn>(FakeCommit);
    TransferFn volatile transfer=reinterpret_cast<TransferFn>(FakeTransfer);CommitFn volatile restore=reinterpret_cast<CommitFn>(FakeRestore);Fake object;std::array<unsigned,16> context{};
    for(unsigned n=0;n<1024;++n){const auto before=context;update(&object,context.data(),n^0xfedcba98u);CHECK(object.updates==n+1&&object.context==context.data()&&object.extra==(n^0xfedcba98u));
        commit(&object,context.data());CHECK(object.commits==n+1&&object.context==context.data());
        transfer(&object,n^0x87654321u);CHECK(object.transfers==n+1&&object.argument==(n^0x87654321u)&&context==before);
        restore(&object,context.data());CHECK(object.restores==n+1&&object.context==context.data()&&context==before);}
    CHECK(callbacks==4096);CHECK(MH_DisableHook(MH_ALL_HOOKS)==MH_OK);update(&object,nullptr,7);CHECK(object.updates==1025&&callbacks==4096);
    CHECK(MH_Uninitialize()==MH_OK);std::printf("Synthetic x86 original forwarding/stack cleanup passed (4096 calls).\n");return 0;
}
}
#endif
int main(){
    if(completion_journal_tests::Run())return 1;
    if(NativeReadinessRequiresAllThreeCoherentInputStates())return 1;
    if(CohortRetryReconstructsAfterRealOverlap()||CohortRetryRejectsOtherFailures()||CohortRetryNeverRenewsOrLoops())return 1;
    if(PolicyLockRecoversRealOverlap()||PolicyLockBoundsAndOwnerRelease())return 1;
    if(ConcurrentOrdinaryAdmissions()||ExclusiveArrivalRequiresFreshDrain()||AlreadyAdmittedInvocationPreventsDrain()||StructuralOwnerDoesNotRequireFrozenSiblingState()||ExactPhaseRetryPolicy()||ClientPhaseRetryUsesOnlyFreshCompleteReads())return 1;
    if(BoundariesAndNoWrites()||OwnerAndLeaseRejections()||MalformedAndChangingState()||RejectedReadProvenance()||NestingAndTransferResults()||RestoreSourceAndDestination()||InvalidNestingAndLifecycle()||FixedStorageOverflow())return 1;
#if defined(_M_IX86)
    if(SyntheticAbi())return 1;
#endif
    std::printf("Twenty reload-flow-runtime deterministic groups passed. No game process used.\n");return 0;
}
