#include "Test.h"
#include "Bc2NativeCycleRuntime.h"
#include <cstring>
using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
template<class T>void Put(std::array<std::byte,0x30>& data,unsigned offset,T value){std::memcpy(data.data()+offset,&value,sizeof(value));}
struct Fixture {
    AmmoResourceService ammunition;Bc2CycleReloadRouting routing;
    ReloadHoldIdentity identity{{0x10000,0x20000,0x30000,0x40000,1,2,3},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
    AmmoResourceBinding binding{identity.owner,{{0x20000,1,0x40000,19},2,3},0xb0000,0xc0000,0xd0000,0xe0000,1000*Ms,1100*Ms};
    AmmunitionSnapshot sample{binding.context,100,1000*Ms,1100*Ms,{4,20,5},true};
    NativeCycleRecoveryScope scope{identity,{}};WeaponCycleReloadCommand dispatched{};AmmoResourceNativeCall call{};
    bool Start(){
        if(!ammunition.Select(binding,identity,sample,sample.observedNs))return false;
        AmmoResourceRequest request{binding,{binding.context,1,sample.observedNs,sample.deadlineNs,AmmunitionOperation::RemoveMagazine}};
        const auto command=ammunition.Accept(request,sample.observedNs);if(!command)return false;
        dispatched.command=*command;dispatched.permit.resource=binding.context;dispatched.permit.debt.guardEpoch=7;
        dispatched.permit.debt.loaded=4;dispatched.permit.debt.reserve=20;dispatched.permit.debt.capacity=5;
        dispatched.permit.second={91,92,93};dispatched.drain={4,sample.observedNs};
        return routing.Begin(scope,dispatched);
    }
    bool Call(){
        return ammunition.InvokeNative(sample,[]()noexcept{return 1001*Ms;},[&](std::int64_t began)noexcept{
            call={identity,dispatched.command.id,100,began,began+1,dispatched.command.before,dispatched.command.after,true,true,true};return call;
        });
    }
    ReloadFlowRecord Record(unsigned branch){
        ReloadFlowRecord r;r.id=branch==2?100:101+branch;r.finished=r.identityRetained=true;
        auto& e=r.entry;e.kind=ReloadFlowEvent::Update;e.nativeInvocation=e.nativeUpdate=e.update=r.id;e.depth=1;
        e.thread=7;e.caller=0x6e90b0;e.context=0x24000;e.nowNs=branch==2?1001*Ms-1:(1002+branch)*Ms;e.contextCopied=true;
        Put(e.copiedContext,0x18,1.f/60);Put(e.copiedContext,0x20,1.f);e.copiedContext[0x24]=std::byte{1};Put(e.copiedContext,0x2c,7u);
        auto& b=e.boundary;b.owner=identity.owner;b.snapshotSequence=100;b.branch=std::uint8_t(branch);b.firing=identity.firing[branch];
        b.wrapperOffset=branch==0?0x3c:branch==1?0x40:0x10;b.current=b.next=2;b.previous=1;
        b.loaded=branch==2?4:0;b.reserve=20;
        if(branch==2){b.serverPlayer=identity.serverPlayer;b.serverSoldier=identity.serverSoldier;b.serverItem=identity.serverItem;}
        r.exit.thread=e.thread;r.exit.nowNs=e.nowNs+100;r.exit.boundary=b;r.exit.boundary->loaded=0;
        r.exit.contextCopied=true;r.exit.copiedContext=e.copiedContext;
        auto& guard=r.exit.cycleGuard;guard.original={0,7};guard.effective=guard.beforeRestore={1,0};
        guard.requested=guard.applied=guard.restored=true;r.exit.cycleGuardEpoch=7;r.exit.cycleGuardSteps=1;return r;
    }
    AmmoResourceOwnUpdate Row(const ReloadFlowRecord& r){
        const auto& before=r.entry.boundary;const auto& after=*r.exit.boundary;
        return {before.owner,after.owner,after.firing,after.serverPlayer,after.serverSoldier,after.serverItem,
            r.id,r.entry.nowNs,r.exit.nowNs,after.branch,1,before.loaded,before.reserve,after.loaded,after.reserve,
            2,2,0,true,true,true,false};
    }
    bool Finish(ReloadFlowRecord record){
        const auto decision=routing.Evaluate(scope,record.entry,5,record.entry.nowNs+80*Ms);
        if(!decision||!routing.Finish(decision,record))return false;
        ammunition.Observe(Row(record),record.exit.nowNs);return true;
    }
};
int GuardedOwnUpdatesJoinActualResourceCompletion(){
    Fixture f;CHECK(f.Start());auto server=f.Record(2);
    const auto admission=f.routing.Evaluate(f.scope,server.entry,5,server.entry.nowNs+80*Ms);CHECK(admission);
    CHECK(f.Call());CHECK(!f.routing.Rebase({5,1001*Ms}));
    CHECK(f.routing.Finish(admission,server));CHECK(!f.ammunition.Observe(f.Row(server),server.exit.nowNs));
    CHECK(f.Finish(f.Record(0)));CHECK(!f.ammunition.Outcome());CHECK(!f.routing.Rebase({5,1002*Ms}));
    CHECK(f.Finish(f.Record(1)));CHECK(f.ammunition.Outcome()&&f.ammunition.Outcome()->receipt);
    const auto receipt=*f.ammunition.Outcome()->receipt;
    auto substituted=f.ammunition.CompletionOwnUpdates();substituted[1]->invocation++;
    CHECK(!f.routing.Receipt(receipt,substituted));CHECK(f.routing.Receipt(receipt,f.ammunition.CompletionOwnUpdates()));
    CHECK(!f.routing.Rebase({4,receipt.completedNs}));CHECK(!f.routing.Rebase({5,receipt.completedNs-1}));
    const auto rebase=f.routing.Rebase({5,receipt.completedNs});CHECK(rebase);
    CHECK(rebase->dispatched==f.dispatched&&rebase->receipt==receipt&&rebase->guardEpoch==8);
    CHECK(rebase->ownUpdates[2].invocation==receipt.authorityInvocation);
    CHECK(rebase->ownUpdates[2].beginNs<receipt.beganNs); // actual helper-owning Update
    CHECK(rebase->ownUpdates[0].invocation!=rebase->ownUpdates[1].invocation);
    // Later guarded callbacks do not restamp the already completed receipt.
    auto later=f.Record(0);later.id=later.entry.nativeInvocation=later.entry.nativeUpdate=later.entry.update=200;
    later.entry.nowNs=1004*Ms;later.exit.nowNs=later.entry.nowNs+100;CHECK(f.Finish(later));
    CHECK(f.routing.Rebase({6,1004*Ms+100})->ownUpdates[0].invocation==101);
    f.routing.Applied();CHECK(!f.routing.Active()&&!f.routing.Rebase({7,1005*Ms}));return 0;
}
int CountsWithoutGuardCannotRebase(){
    for(unsigned failure=0;failure<4;++failure){
        Fixture f;CHECK(f.Start()&&f.Call());auto row=f.Record(0);
        const auto decision=f.routing.Evaluate(f.scope,row.entry,5,row.entry.nowNs+80*Ms);CHECK(decision);
        if(failure==0)row.exit.cycleGuard.restored=false;
        if(failure==1)row.exit.cycleGuardSteps=0;
        if(failure==2)row.exit.cycleGuardEpoch=8;
        if(failure==3)row.exit.boundary->loaded=1;
        CHECK(!f.routing.Finish(decision,row)&&f.routing.Failed());
        CHECK(!f.routing.Rebase({5,row.exit.nowNs}));CHECK(f.routing.Active());
    }return 0;
}
int ForeignAndOverlappingCallbacksCannotBorrowPendingCommand(){
    Fixture f;CHECK(f.Start());auto row=f.Record(0);auto wrong=f.scope;++wrong.identity.owner.equipGeneration;
    CHECK(!f.routing.Evaluate(wrong,row.entry,5,row.entry.nowNs+80*Ms));CHECK(f.routing.Failed()&&f.routing.Active());
    Fixture overlap;CHECK(overlap.Start());row=overlap.Record(0);
    CHECK(overlap.routing.Evaluate(overlap.scope,row.entry,5,row.entry.nowNs+80*Ms));
    ++row.entry.nativeInvocation;row.entry.nativeUpdate=row.entry.nativeInvocation;
    CHECK(!overlap.routing.Evaluate(overlap.scope,row.entry,5,row.entry.nowNs+80*Ms));
    CHECK(overlap.routing.Active()&&!overlap.routing.Drained());return 0;
}
}
int main(){CHECK(GuardedOwnUpdatesJoinActualResourceCompletion()==0);CHECK(CountsWithoutGuardCannotRebase()==0);CHECK(ForeignAndOverlappingCallbacksCannotBorrowPendingCommand()==0);return 0;}
