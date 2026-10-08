#include "Test.h"
#include "Bc2AmmoResourceEvidence.h"
#include "Bc2AmmoResourceHandoff.h"
#include "Bc2AmmoResourceBinding.h"
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Now=1000000000;
ReloadHoldIdentity Identity(){return {{0x10000,0x20000,0x30000,0x40000,1,2,3},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};}
AmmunitionSnapshot Snapshot(const ReloadHoldIdentity& i,int loaded,int reserve,int cap,std::int64_t now,std::uint64_t sequence=1){
    return {{{i.owner.soldier,i.owner.actorGeneration,i.owner.weapon,1},i.owner.equipGeneration,i.owner.space},
        sequence,now,now+100000000,{loaded,reserve,cap},true};
}
AmmoResourceOwnUpdate Row(const ReloadHoldIdentity& i,unsigned branch,const AmmoResourceNativeCall& c){
    AmmoResourceOwnUpdate r;r.beforeOwner=r.afterOwner=i.owner;r.firing=i.firing[branch];
    r.serverPlayer=i.serverPlayer;r.serverSoldier=i.serverSoldier;r.serverItem=i.serverItem;
    r.invocation=branch==2?c.invocation:c.invocation+branch+1;r.beginNs=c.beginNs-1;r.endNs=c.endNs+1000+branch;
    r.branch=branch;r.depth=1;r.loadedBefore=c.before.loaded;r.reserveBefore=c.before.reserve;
    r.loadedAfter=c.after.loaded;r.reserveAfter=c.after.reserve;r.current=r.next=2;
    r.finished=r.identityRetained=r.neutralContext=true;return r;
}
int Regression(){
    for(unsigned failure=0;failure<15;++failure){
        const auto identity=Identity();AmmunitionLedger ledger;auto s=Snapshot(identity,22,191,30,Now);CHECK(ledger.Bind(s,Now));
        const auto command=ledger.Queue(AmmunitionOperation::RemoveMagazine,s,Now);CHECK(command&&ledger.Dispatch(s,Now));
        AmmoResourceCompletion observer;CHECK(observer.Begin(*command,identity));
        AmmoResourceNativeCall call{identity,command->id,100,Now+1000,Now+2000,command->before,command->after,true,true,true};
        CHECK(observer.Call(call));CHECK(observer.Call(call));CHECK(!observer.Receipt(Now+2000));
        auto a=Row(identity,0,call),b=Row(identity,1,call),server=Row(identity,2,call);
        if(failure==1)++a.afterOwner.equipGeneration;
        if(failure==2)a.firing=identity.firing[2];
        if(failure==3)a.invocation=server.invocation;
        if(failure==4)server.invocation++;
        if(failure==5)server.beginNs=call.beginNs+1;
        if(failure==6)server.serverItem++;
        if(failure==7)server.loadedBefore++;
        if(failure==8)b.reserveAfter++;
        if(failure==9)b.timer=.01f;
        if(failure==10)b.next=12;
        if(failure==11)b.animationHold=true;
        if(failure==12)b.neutralContext=false;
        if(failure==13)b.identityRetained=false;
        if(failure==14)b.finished=false;
        observer.Observe(a);observer.Observe(b);observer.Observe(server);
        const auto receipt=observer.Receipt(Now+4000);
        if(failure==0){
            CHECK(receipt&&ledger.Complete(*receipt,Now+4000));
            CHECK(ledger.Original()->rounds==22&&ledger.Snapshot().counts.loaded==0);
            CHECK(!observer.Receipt(Now+200000000)); // old counts are not current authority
            a.endNs=Now+5000;a.loadedAfter=22;a.invocation+=10;
            CHECK(!observer.Observe(a)&&observer.Failed()); // prediction rollback invalidates completion
        }else CHECK(!receipt&&!ledger.Original());
    }
    const auto identity=Identity();AmmunitionLedger ledger;auto s=Snapshot(identity,22,191,30,Now);CHECK(ledger.Bind(s,Now));
    const auto c=ledger.Queue(AmmunitionOperation::RemoveMagazine,s,Now);CHECK(c);
    AmmoResourceCompletion observer;CHECK(observer.Begin(*c,identity));
    AmmoResourceNativeCall call{identity,c->id,100,Now+1000,Now+2000,c->before,c->after,true,false,true};
    CHECK(!observer.Call(call)&&observer.Failed());call.exactPostcondition=true;CHECK(!observer.Call(call));
    return 0;
}
int DelayedServerCompletion(){
    for(unsigned invalidate=0;invalidate<3;++invalidate){
        const auto i=Identity();AmmunitionLedger ledger;auto s=Snapshot(i,22,191,30,Now);CHECK(ledger.Bind(s,Now));
        const auto c=ledger.Queue(AmmunitionOperation::RemoveMagazine,s,Now);CHECK(c&&ledger.Dispatch(s,Now));
        AmmoResourceCompletion observer;CHECK(observer.Begin(*c,i));
        AmmoResourceNativeCall call{i,c->id,100,Now+1000,Now+2000,c->before,c->after,true,true,true};
        CHECK(observer.Call(call));AmmoResourceHandoff handoff;CHECK(handoff.Arm(call.invocation));
        const auto exact=Row(i,2,call);CHECK(handoff.Publish(exact));
        CHECK(!handoff.Publish(exact)&&!handoff.Arm(call.invocation+1));
        ledger.Cancel(); // tracking/equip loss cannot undo or reissue the native effect
        CHECK(ledger.Phase()==AmmunitionLedgerPhase::NeedsReconciliation);
        for(unsigned branch=0;branch<3;++branch){auto later=Row(i,branch,call);
            later.invocation+=10;later.beginNs=later.endNs=Now+150000000;
            observer.Observe(later);
        }
        CHECK(!observer.Receipt(Now+150000001)); // later server counts alone prove no helper call
        auto delayed=handoff.Take();CHECK(delayed&&*delayed==exact&&!handoff.Take());
        if(invalidate==1)++delayed->serverItem;
        if(invalidate==2)delayed->current=12;
        observer.Observe(*delayed);
        const auto receipt=observer.Receipt(Now+150000001);
        if(invalidate){CHECK(observer.Failed()&&!receipt&&!ledger.Original());}
        else {
            CHECK(receipt&&ledger.Complete(*receipt,Now+150000001));
            CHECK(ledger.Original()->rounds==22);
            CHECK(!observer.Receipt(Now+250000001)); // drainage never refreshes the row's timestamp
        }
        CHECK(!handoff.Arm(call.invocation)&&handoff.Arm(call.invocation+100));
        CHECK(!handoff.Publish(exact)); // previous command cannot publish into a newer slot
    }
    AmmoResourceHandoff h;CHECK(!h.Arm(0)&&!h.Arm(UINT64_MAX)&&!h.Take());
    CHECK(h.Arm(1));AmmoResourceOwnUpdate wrong;wrong.invocation=1;wrong.branch=0;
    CHECK(!h.Publish(wrong));wrong.branch=2;wrong.invocation=2;CHECK(!h.Publish(wrong));
    return 0;
}
int ConcurrentHandoff(){
    constexpr std::uint64_t Count=10000;AmmoResourceHandoff h;
    std::atomic<bool> failed=false,done=false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    const auto make=[](std::uint64_t id){auto row=AmmoResourceOwnUpdate{};
        row.branch=2;row.invocation=id;row.beginNs=id*3;row.endNs=id*3+2;
        row.loadedBefore=int(id%201);row.loadedAfter=row.loadedBefore-1;
        row.beforeOwner=Identity().owner;row.afterOwner=row.beforeOwner;
        row.reserveAfter=int(id);row.finished=row.identityRetained=true;return row;};
    std::thread producer([&]{for(std::uint64_t id=1;id<=Count&&!failed.load();++id){
        const auto row=make(id);
        while(!h.Publish(row)){
            if(failed.load()||std::chrono::steady_clock::now()>deadline){failed=true;return;}
            std::this_thread::yield();
        }
        if(h.Publish(row)){failed=true;return;}
    }done=true;});
    for(std::uint64_t id=1;id<=Count&&!failed.load();++id){
        if(!h.Arm(id)){failed=true;break;}
        std::optional<AmmoResourceOwnUpdate> row;
        while(!(row=h.Take())){
            if(failed.load()||std::chrono::steady_clock::now()>deadline){failed=true;break;}
            std::this_thread::yield();
        }
        if(!row||*row!=make(id)){failed=true;break;}
    }
    producer.join();CHECK(!failed.load()&&done.load()&&!h.Take());return 0;
}
int BindingPublication(){
    const auto i=Identity();const auto s=Snapshot(i,22,191,30,Now);
    AmmoResourceBinding b{i.owner,s.context,0x80000,0x90000,0xa0000,0,Now,Now+100000000};
    AmmoResourceBindingChannel channel;CHECK(!channel.Read(i.owner,Now));
    CHECK(channel.Publish(b)&&channel.Read(i.owner,Now));
    auto wrong=i.owner;++wrong.equipGeneration;CHECK(!channel.Read(wrong,Now));
    CHECK(!channel.Read(i.owner,Now-1)&&!channel.Read(i.owner,b.deadlineNs));
    CHECK(channel.Publish({})&&!channel.Read(i.owner,Now));
    b.context.resource.weaponGeneration=0;CHECK(channel.Publish(b)&&!channel.Read(i.owner,Now));
    return 0;
}
void ReadOwner(ReloadStateOwner& o){std::cin>>o.player>>o.soldier>>o.weak>>o.weapon>>o.actorGeneration>>o.equipGeneration>>o.space;}
int Replay(){
    ReloadHoldIdentity identity;ReadOwner(identity.owner);
    for(auto& f:identity.firing)std::cin>>f;
    std::cin>>identity.serverPlayer>>identity.serverSoldier>>identity.serverItem;
    int loaded=0,reserve=0,capacity=0;std::cin>>loaded>>reserve>>capacity;
    AmmunitionLedger ledger;
    for(unsigned index=0;index<2;++index){
        unsigned operation=0,rows=0;AmmoResourceNativeCall call;call.identity=identity;
        std::cin>>operation>>call.invocation>>call.beginNs>>call.endNs>>rows;
        std::cin>>call.before.loaded>>call.before.reserve>>call.after.loaded>>call.after.reserve;
        call.before.capacity=call.after.capacity=capacity;
        std::cin>>call.bindingVerified>>call.exactPostcondition>>call.contextUnchanged;
        auto s=Snapshot(identity,index?ledger.Snapshot().counts.loaded:loaded,
            index?ledger.Snapshot().counts.reserve:reserve,capacity,call.beginNs-1000,index+1);
        CHECK(std::cin&&rows>0&&rows<10000&&operation<=2);
        if(index==0)CHECK(ledger.Bind(s,s.observedNs));
        else if(operation==unsigned(AmmunitionOperation::RefillMagazine))CHECK(ledger.Original()&&ledger.Discard(*ledger.Original()));
        const auto original=operation==unsigned(AmmunitionOperation::ReturnMagazine)?ledger.Original():std::nullopt;
        const auto c=ledger.Queue(AmmunitionOperation(operation),s,s.observedNs,original);CHECK(c&&ledger.Dispatch(s,s.observedNs));
        call.command=c->id;AmmoResourceCompletion observer;CHECK(observer.Begin(*c,identity)&&observer.Call(call));
        bool completed=false;
        for(unsigned n=0;n<rows;++n){
            AmmoResourceOwnUpdate r;ReadOwner(r.beforeOwner);ReadOwner(r.afterOwner);
            std::cin>>r.firing>>r.serverPlayer>>r.serverSoldier>>r.serverItem>>r.invocation>>r.beginNs>>r.endNs>>r.branch>>r.depth;
            std::cin>>r.loadedBefore>>r.reserveBefore>>r.loadedAfter>>r.reserveAfter>>r.current>>r.next>>r.timer;
            std::cin>>r.finished>>r.identityRetained>>r.neutralContext>>r.animationHold;CHECK(std::cin);
            if(completed)continue;
            observer.Observe(r);const auto receipt=observer.Receipt(r.endNs);
            if(receipt){CHECK(ledger.Complete(*receipt,r.endNs));completed=true;}
        }
        CHECK(completed&&!observer.Failed());
    }
    CHECK(!ledger.WellEmpty());
    std::cout<<"native_trace_replayed loaded="<<ledger.Snapshot().counts.loaded<<" reserve="<<ledger.Snapshot().counts.reserve<<'\n';return 0;
}
}
int main(int argc,char** argv){
    if(argc==2&&std::string(argv[1])=="--replay")return Replay();
    CHECK(argc==1&&Regression()==0&&DelayedServerCompletion()==0&&ConcurrentHandoff()==0&&BindingPublication()==0);
    std::puts("Bc2AmmoResourceEvidence: actual server invocation and independent client completion required");return 0;
}
