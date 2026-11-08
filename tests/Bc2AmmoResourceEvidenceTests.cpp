#include "Test.h"
#include "Bc2AmmoResourceEvidence.h"
#include "Bc2AmmoResourceHandoff.h"
#include "Bc2AmmoResourceBinding.h"
#include "Bc2DiagnosticObservationWindow.h"
#include "Bc2AmmoResourceBindingObservation.h"
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
namespace fvr::bc2 {
// Test-only ownership of the actual channel gate: no runtime waits or hooks.
struct AmmoResourceBindingChannelTestAccess {
    static bool Hold(AmmoResourceBindingChannel& c){return !c.gate_.test_and_set(std::memory_order_acquire);}
    static void Release(AmmoResourceBindingChannel& c){c.gate_.clear(std::memory_order_release);}
};
}
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
    AmmoResourceBindingChannel channel;
    const auto parity=[&](const ReloadStateOwner& owner,std::int64_t now,AmmoResourceBindingReadReason reason,
        const std::optional<AmmoResourceBinding>& raw,std::uint64_t revision)->int {
        const auto plain=channel.Read(owner,now);
        // Dirty fields ensure an early refusal resets the observation rather
        // than retaining a previous successful read's facts.
        AmmoResourceBindingReadObservation diagnostic;
        diagnostic.reason=AmmoResourceBindingReadReason::Busy;diagnostic.gateHeld=diagnostic.revisionRead=true;
        diagnostic.storedRevision=diagnostic.currentRevision=99;diagnostic.value=b;
        const auto observed=channel.Read(owner,now,&diagnostic);
        CHECK(plain==observed&&channel.Read(owner,now)==plain);
        CHECK(bool(observed)==(reason==AmmoResourceBindingReadReason::Available));
        CHECK(diagnostic.reason==reason&&diagnostic.gateHeld&&diagnostic.value==raw);
        CHECK(diagnostic.storedRevision==revision&&diagnostic.currentRevision==revision);
        CHECK(diagnostic.revisionRead==(revision!=0));
        if(observed)CHECK(observed==raw);return 0;
    };
    CHECK(parity(i.owner,Now,AmmoResourceBindingReadReason::Unpublished,{},0)==0);
    CHECK(channel.Publish(b));
    CHECK(parity(i.owner,Now,AmmoResourceBindingReadReason::Available,b,1)==0);
    auto wrong=i.owner;++wrong.equipGeneration;
    CHECK(parity(wrong,Now,AmmoResourceBindingReadReason::Owner,b,1)==0);
    CHECK(parity(i.owner,Now-1,AmmoResourceBindingReadReason::Future,b,1)==0);
    CHECK(parity(i.owner,b.deadlineNs-1,AmmoResourceBindingReadReason::Available,b,1)==0);
    CHECK(parity(i.owner,b.deadlineNs,AmmoResourceBindingReadReason::Expired,b,1)==0);
    CHECK(channel.Publish({}));
    CHECK(parity(i.owner,Now,AmmoResourceBindingReadReason::NoBinding,{},2)==0);
    b.context.resource.weaponGeneration=0;CHECK(channel.Publish(b));
    CHECK(parity(i.owner,Now,AmmoResourceBindingReadReason::Invalid,b,3)==0);
    b.context.resource.weaponGeneration=1;b.deadlineNs=Now+100000001;CHECK(channel.Publish(b));
    CHECK(parity(i.owner,Now,AmmoResourceBindingReadReason::Invalid,b,4)==0);
    return 0;
}
int ObservationWindows(){
    using Window=DiagnosticObservationWindow<AmmoResourceBindingReadObservation>;
    std::atomic_flag flag=ATOMIC_FLAG_INIT;Window journal(flag);Window::Snapshot copy;
    const auto i=Identity();const auto s=Snapshot(i,22,191,30,Now);
    AmmoResourceBinding b{i.owner,s.context,0x80000,0x90000,0xa0000,0,Now,Now+100000000};
    AmmoResourceBindingChannel channel;CHECK(channel.Publish(b));
    AmmoResourceBindingReadObservation accepted,failureA,failureB;
    CHECK(channel.Read(i.owner,Now,&accepted));
    auto wrong=i.owner;++wrong.equipGeneration;CHECK(!channel.Read(wrong,Now,&failureA));
    CHECK(failureA.reason==AmmoResourceBindingReadReason::Owner);
    CHECK(!journal.Begin(Now).epoch&&!journal.Publish({},failureA,true));
    CHECK(!journal.Request(0,Now)&&!journal.Request(Now,Now));
    CHECK(journal.Request(Now,Now+100));
    const auto a=journal.Begin(Now+1),lateA=journal.Begin(Now+2);
    CHECK(a.epoch&&a.beginNs==Now&&a.endNs==Now+100);
    CHECK(!journal.Begin(Now-1).epoch&&!journal.Begin(Now+100).epoch);
    CHECK(journal.Publish(a,accepted,false)&&journal.Publish(a,failureA,true));
    CHECK(journal.Read(copy)&&copy.calls==2&&copy.rejected==1);
    CHECK(copy.firstFailure&&copy.firstFailure->value.reason==AmmoResourceBindingReadReason::Owner);
    CHECK(copy.precedingAccepted&&copy.precedingAccepted->window.epoch==a.epoch);
    CHECK(copy.precedingAccepted->value.reason==AmmoResourceBindingReadReason::Available);

    // A failed reset cannot activate the new window or clear the old evidence.
    CHECK(!flag.test_and_set(std::memory_order_acquire));
    CHECK(!journal.Request(Now+200,Now+300)&&!journal.Begin(Now+201).epoch);
    CHECK(!journal.Publish(lateA,failureA,true)&&!journal.Read(copy));
    flag.clear(std::memory_order_release);
    const auto drops=journal.Drops();CHECK(drops.request==1&&drops.publication==1&&drops.snapshot==1);
    CHECK(journal.Read(copy)&&copy.window.epoch==a.epoch&&copy.calls==2);

    CHECK(journal.Request(Now+200,Now+300));const auto next=journal.Begin(Now+201);
    CHECK(next.epoch==a.epoch+1&&next.beginNs==Now+200&&next.endNs==Now+300);
    CHECK(journal.Read(copy)&&copy.calls==0&&copy.rejected==0&&copy.lateEpochDropped==0);
    CHECK(!copy.firstFailure&&!copy.precedingAccepted&&!copy.lastAccepted&&!copy.latest);
    // The late A capture must not become B's first failure, last acceptance, or
    // preceding acceptance. Its drop is visible in B's per-window accounting.
    CHECK(!journal.Publish(lateA,failureA,true)&&!journal.Publish(lateA,accepted,false));
    CHECK(journal.Read(copy)&&copy.lateEpochDropped==2&&!copy.firstFailure&&!copy.lastAccepted);
    b.context.resource.weaponGeneration=0;b.observedNs=Now+200;CHECK(channel.Publish(b));
    CHECK(!channel.Read(i.owner,Now+201,&failureB));
    CHECK(failureB.reason==AmmoResourceBindingReadReason::Invalid&&failureB.value==b);
    CHECK(journal.Publish(next,accepted,false)&&journal.Publish(next,failureB,true));

    // Poll refreshes keep the first retained failure and the original ticket's
    // bounds even when a later successful publication becomes latest.
    CHECK(journal.Request(Now+250,Now+400)&&journal.Publish(journal.Begin(Now+301),accepted,false));
    CHECK(journal.Request(Now+260,Now+350)); // delayed poll cannot shorten the live window
    CHECK(journal.Read(copy)&&copy.window.epoch==next.epoch&&copy.window.endNs==Now+400);
    CHECK(copy.calls==3&&copy.rejected==1&&copy.lateEpochDropped==2);
    CHECK(copy.firstFailure&&copy.firstFailure->window.epoch==next.epoch&&copy.firstFailure->window.endNs==Now+300);
    CHECK(copy.firstFailure->value.reason==AmmoResourceBindingReadReason::Invalid&&copy.firstFailure->value.value==b);
    CHECK(copy.precedingAccepted&&copy.precedingAccepted->window.epoch==next.epoch);
    CHECK(copy.latest&&copy.latest->value.reason==AmmoResourceBindingReadReason::Available);
    CHECK(!journal.Publish(lateA,failureA,true));
    CHECK(journal.Read(copy)&&copy.calls==3&&copy.lateEpochDropped==3&&copy.firstFailure->value.value==b);
    CHECK(!journal.Begin(Now+400).epoch&&!journal.Request(Now+400,Now+400));
    CHECK(journal.Request(Now+400,Now+500));
    CHECK(journal.Read(copy)&&copy.window.epoch==next.epoch+1&&copy.calls==0&&copy.rejected==0&&copy.lateEpochDropped==0);
    CHECK(!copy.firstFailure&&!copy.precedingAccepted&&!copy.latest);
    CHECK(journal.Drops().request==1&&journal.Drops().publication==1&&journal.Drops().snapshot==1);
    return 0;
}
struct FixedBindingReadRow {
    AmmoResourceBindingReadObservation bindingRead{};
    std::int64_t bindingReadNs=0,bindingReadEndNs=0;
};
using BindingWindow=DiagnosticObservationWindow<FixedBindingReadRow,AmmoResourceBindingPublicationRetention>;
int BindingPublicationRaces(){
    // Each case invokes the real Publish while the channel gate is held, then
    // the real Read: even a same-identity failed writer MUST still invalidate.
    for(unsigned change=0;change<10;++change){
        const auto i=Identity();const auto s=Snapshot(i,22,191,30,Now);
        const AmmoResourceBinding original{i.owner,s.context,0x80000,0x90000,0xa0000,0,Now,Now+100000000};
        AmmoResourceBindingChannel channel;AmmoResourceBindingPublishObservation committed;
        CHECK(channel.Publish(original,&committed));
        CHECK(committed.reason==AmmoResourceBindingPublishReason::Published&&committed.attemptedRevision==1);
        CHECK(committed.previousCopied&&!committed.previous&&committed.previousRevision==0);
        std::optional<AmmoResourceBinding> attempted=original;
        ++attempted->observedNs;++attempted->deadlineNs;
        if(change==1)++attempted->owner.equipGeneration;
        if(change==2)++attempted->context.resource.weaponGeneration;
        if(change==3)++attempted->inventory;
        if(change==4)++attempted->switching;
        if(change==5)++attempted->data;
        if(change==6)++attempted->persistence;
        if(change==7)++attempted->context.space;
        if(change==8)attempted.reset();
        if(change==9)++attempted->owner.actorGeneration;
        AmmoResourceBindingPublicationObservation failure{Now+10,Now+20,{}};
        // Dirty values must not survive a failed publication.
        failure.publication=committed;
        CHECK(AmmoResourceBindingChannelTestAccess::Hold(channel));
        bool published=true;
        std::thread publisher([&]{published=channel.Publish(attempted,&failure.publication);});publisher.join();
        AmmoResourceBindingReadObservation busy;
        const auto busyRead=channel.Read(i.owner,Now+21,&busy);
        AmmoResourceBindingChannelTestAccess::Release(channel);
        CHECK(!published&&!busyRead&&busy.reason==AmmoResourceBindingReadReason::Busy&&!busy.value);
        CHECK(failure.publication.reason==AmmoResourceBindingPublishReason::Busy);
        CHECK(failure.publication.attemptedRevision==2&&failure.publication.attempted==attempted);
        CHECK(!failure.publication.previousCopied&&!failure.publication.previous&&!failure.publication.revisionRead);
        FixedBindingReadRow refused;refused.bindingReadNs=Now+30;refused.bindingReadEndNs=Now+31;
        CHECK(!channel.Read(i.owner,Now+30,&refused.bindingRead)&&!channel.Read(i.owner,Now+30));
        CHECK(refused.bindingRead.reason==AmmoResourceBindingReadReason::Revision&&refused.bindingRead.value==original);
        CHECK(refused.bindingRead.storedRevision==1&&refused.bindingRead.currentRevision==2);
        std::atomic_flag flag=ATOMIC_FLAG_INIT;BindingWindow journal(flag);BindingWindow::Snapshot copy;
        CHECK(journal.Request(Now,Now+1000));const auto ticket=journal.Begin(Now+1);
        if(change%2==0)CHECK(journal.ObserveRetention(ticket,failure));
        CHECK(journal.Publish(ticket,refused,true));
        if(change%2!=0)CHECK(journal.ObserveRetention(ticket,failure));
        CHECK(journal.Read(copy)&&copy.calls==1&&copy.rejected==1&&copy.retention.matched);
        CHECK(copy.firstFailure&&copy.firstFailure->value.bindingRead.value==original);
        const auto same=copy.retention.AttemptMatchesStoredIdentity();
        if(change==8)CHECK(!same);else CHECK(same&&*same==(change==0));
        // Recovery uses a new real commit; it cannot reinterpret the refusal.
        CHECK(channel.Publish(original,&committed)&&channel.Read(i.owner,Now+40));
        CHECK(committed.attemptedRevision==3&&committed.previousCopied&&committed.previous==original&&committed.previousRevision==1);
        CHECK(journal.ObserveRetention(ticket,AmmoResourceBindingPublicationObservation{Now+40,Now+41,committed}));
        CHECK(journal.Read(copy)&&copy.retention.matched->publication.attemptedRevision==2);
    }
    return 0;
}
int BindingPublicationRetention(){
    const auto i=Identity();const auto s=Snapshot(i,22,191,30,Now);
    const AmmoResourceBinding b{i.owner,s.context,0x80000,0x90000,0xa0000,0,Now,Now+100000000};
    const auto attempt=[&](std::uint64_t revision,std::int64_t begin,std::int64_t end){
        AmmoResourceBindingPublicationObservation p{begin,end,{}};
        p.publication.attemptedRevision=revision;p.publication.reason=AmmoResourceBindingPublishReason::Busy;
        p.publication.attempted=b;return p;};
    FixedBindingReadRow refused;refused.bindingReadNs=Now+30;refused.bindingReadEndNs=Now+31;
    refused.bindingRead.reason=AmmoResourceBindingReadReason::Revision;
    refused.bindingRead.gateHeld=refused.bindingRead.revisionRead=true;
    refused.bindingRead.storedRevision=1;refused.bindingRead.currentRevision=2;refused.bindingRead.value=b;
    std::atomic_flag flag=ATOMIC_FLAG_INIT;BindingWindow journal(flag);BindingWindow::Snapshot copy;
    CHECK(!journal.ObserveRetention({},attempt(2,Now+10,Now+20)));
    CHECK(journal.Request(Now,Now+1000));const auto ticket=journal.Begin(Now+1);
    CHECK(journal.Publish(ticket,refused,true));
    CHECK(journal.Read(copy)&&!copy.retention.matched&&!copy.retention.AttemptMatchesStoredIdentity());
    CHECK(journal.ObserveRetention(ticket,attempt(3,Now+32,Now+33))); // Later different revision is no evidence.
    CHECK(journal.ObserveRetention(ticket,attempt(2,Now+32,Now+33))); // Even matching claimed revision began too late.
    CHECK(journal.Read(copy)&&!copy.retention.matched);
    CHECK(!flag.test_and_set(std::memory_order_acquire));
    const bool dropped=journal.ObserveRetention(ticket,attempt(2,Now+10,Now+20));
    flag.clear(std::memory_order_release);
    CHECK(!dropped&&journal.RetentionDropped()==1&&journal.Read(copy)&&!copy.retention.matched);
    // An exact attempt that finishes later is retained, with its true interval.
    CHECK(journal.ObserveRetention(ticket,attempt(2,Now+10,Now+40)));
    CHECK(journal.Read(copy)&&copy.retention.matched&&copy.retention.matched->endNs>copy.retention.readEndNs);
    for(unsigned n=0;n<32;++n)CHECK(journal.ObserveRetention(ticket,attempt(10+n,Now+50+n,Now+51+n)));
    CHECK(journal.Read(copy)&&copy.retention.count==16&&copy.retention.overwritten==19);
    CHECK(copy.calls==1&&copy.rejected==1&&copy.retention.matched->publication.attemptedRevision==2);
    CHECK(journal.Request(Now+1000,Now+2000));const auto next=journal.Begin(Now+1001);
    CHECK(!journal.ObserveRetention(ticket,attempt(2,Now+10,Now+20)));
    CHECK(journal.Read(copy)&&copy.lateEpochDropped==1&&!copy.retention.target&&!copy.retention.matched);
    // A prior attempt evicted before the refusal stays unknown.
    CHECK(journal.ObserveRetention(next,attempt(2,Now+1010,Now+1020)));
    for(unsigned n=0;n<16;++n)CHECK(journal.ObserveRetention(next,attempt(10+n,Now+1010,Now+1020)));
    refused.bindingReadNs=Now+1030;refused.bindingReadEndNs=Now+1031;
    CHECK(journal.Publish(next,refused,true)&&journal.Read(copy)&&!copy.retention.matched);
    CHECK(copy.retention.overwritten==1);
    // An earlier non-Revision refusal remains the window's first failure.
    CHECK(journal.Request(Now+2000,Now+3000));const auto third=journal.Begin(Now+2001);
    auto other=refused;other.bindingRead.reason=AmmoResourceBindingReadReason::Busy;
    CHECK(journal.Publish(third,other,true)&&journal.Publish(third,refused,true));
    CHECK(journal.ObserveRetention(third,attempt(2,Now+1010,Now+1020)));
    CHECK(journal.Read(copy)&&!copy.retention.target&&!copy.retention.matched);
    // Missing read metadata is unknown even when an attempt claims its revision.
    for(unsigned missing=0;missing<7;++missing){
        std::atomic_flag missingFlag=ATOMIC_FLAG_INIT;BindingWindow missingJournal(missingFlag);
        CHECK(missingJournal.Request(Now,Now+1000));const auto missingTicket=missingJournal.Begin(Now+1);
        auto incomplete=refused;incomplete.bindingReadNs=Now+30;incomplete.bindingReadEndNs=Now+31;
        if(missing==0)incomplete.bindingRead.gateHeld=false;
        if(missing==1)incomplete.bindingRead.revisionRead=false;
        if(missing==2)incomplete.bindingRead.storedRevision=0;
        if(missing==3)incomplete.bindingRead.storedRevision=2;
        if(missing==4)incomplete.bindingRead.storedRevision=3;
        if(missing==5)incomplete.bindingReadNs=0;
        if(missing==6)incomplete.bindingReadEndNs=Now+29;
        CHECK(missingJournal.Publish(missingTicket,incomplete,true));
        CHECK(missingJournal.ObserveRetention(missingTicket,attempt(2,Now+10,Now+20)));
        CHECK(missingJournal.Read(copy)&&!copy.retention.matched&&!copy.retention.AttemptMatchesStoredIdentity());
    }
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
    CHECK(argc==1&&Regression()==0&&DelayedServerCompletion()==0&&ConcurrentHandoff()==0&&BindingPublication()==0&&ObservationWindows()==0&&BindingPublicationRaces()==0&&BindingPublicationRetention()==0);
    std::puts("Bc2AmmoResourceEvidence: actual server invocation and independent client completion required");
    std::puts("Binding publication diagnostics: 10 real channel contention cases and bounded exact-revision retention passed");return 0;
}
