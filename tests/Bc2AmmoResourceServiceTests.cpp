#include "Test.h"
#include "Bc2AmmoResourceService.h"
#include <thread>
#include <chrono>
#include <cstdio>
#include <sstream>
using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
struct Fixture {
    AmmoResourceService service;AmmoResourceChannel channel;
    ReloadHoldIdentity identity{{0x10000,0x20000,0x30000,0x40000,1,2,3},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
    AmmoResourceBinding binding;AmmunitionSnapshot sample;std::int64_t now=1000*Ms;std::uint64_t event=0,invocation=0;
    Fixture(){binding={identity.owner,{{identity.owner.soldier,1,identity.owner.weapon,19},2,3},0xb0000,0xc0000,0xd0000,0xe0000,now,now+100*Ms};
        sample={binding.context,1,now,now+100*Ms,{22,191,30},true};}
    bool Select(){binding.observedNs=sample.observedNs=now;binding.deadlineNs=sample.deadlineNs=now+100*Ms;
        ++sample.sequence;return service.Select(binding,identity,sample,now);}
    AmmoResourceRequest Request(AmmunitionOperation operation,bool discard=false){return {binding,
        {binding.context,++event,now,now+90*Ms,operation,discard||operation==AmmunitionOperation::ReturnMagazine?service.View()->original:std::nullopt},discard};}
    AmmoResourceOwnUpdate Row(unsigned branch,const AmmoResourceNativeCall& c){AmmoResourceOwnUpdate r;
        r.beforeOwner=r.afterOwner=c.identity.owner;r.firing=c.identity.firing[branch];r.serverPlayer=c.identity.serverPlayer;
        r.serverSoldier=c.identity.serverSoldier;r.serverItem=c.identity.serverItem;r.invocation=c.invocation+(branch==2?0:branch+1);
        r.beginNs=c.beginNs-1;r.endNs=c.endNs+10+branch;r.branch=branch;r.depth=1;
        r.loadedBefore=c.before.loaded;r.reserveBefore=c.before.reserve;r.loadedAfter=c.after.loaded;r.reserveAfter=c.after.reserve;
        r.current=r.next=2;r.finished=r.identityRetained=r.neutralContext=true;return r;}
    int Run(AmmunitionOperation op){
        CHECK(Select());const auto r=Request(op);CHECK(channel.Submit(r));CHECK(!channel.Submit(r));const auto read=channel.Take();CHECK(read&&!channel.Take());
        auto c=service.Accept(*read,now);CHECK(c);CHECK(!service.Accept(*read,now));CHECK(service.Dispatch(sample,now));CHECK(!service.Dispatch(sample,now));const auto inFlight=*service.View();
        AmmoResourceNativeCall call{identity,c->id,(invocation+=10),now+1,now+2,c->before,c->after,true,true,true};CHECK(service.Call(call));
        CHECK(!service.Observe(Row(0,call),now+100));CHECK(!service.Observe(Row(1,call),now+100));
        CHECK(service.Observe(Row(2,call),now+100));CHECK(!service.Active());
        CHECK(service.Outcome()&&service.Outcome()->receipt&&service.Outcome()->request==r);
        CHECK(channel.Publish(service.View(),service.Outcome()));
        const auto gap=channel.Read(identity.owner,now+100);CHECK(gap&&gap->terminal&&gap->command==inFlight.command);
        CHECK(gap->requestState==AmmoResourceRequestState::Dispatched&&gap->phase==inFlight.phase);
        CHECK(gap->snapshot.counts==c->before&&gap->snapshot.observedNs==inFlight.snapshot.observedNs&&!gap->receipt);
        CHECK(!service.Select(binding,identity,sample,now+100));
        CHECK(service.View()->command==inFlight.command&&service.View()->snapshot.counts==c->before);
        CHECK(!channel.Read(identity.owner,sample.deadlineNs)); // no invented freshness while waiting
        sample.counts=c->after;now+=Ms;CHECK(Select());CHECK(channel.Publish(service.View()));
        const auto view=channel.Read(identity.owner,now);CHECK(view&&view->receipt&&view->request==r.intent.id&&view->snapshot.counts==c->after);
        CHECK(view->phase==AmmunitionLedgerPhase::Ready&&view->requestState==AmmoResourceRequestState::Completed);return 0;
    }
};
int NativeEntryDeadlineKeepsUncalledOperationRejectable(){
    // Source-level reproduction of the retired two-clock boundary: Dispatch
    // happened before expiry, but the second clock refused entry afterwards.
    Fixture old;CHECK(old.Select());const auto oldRequest=old.Request(AmmunitionOperation::RemoveMagazine);
    const auto oldCommand=old.service.Accept(oldRequest,old.now);CHECK(oldCommand);
    CHECK(old.service.Dispatch(old.sample,oldCommand->admissionDeadlineNs-1));
    old.service.Cancel(oldCommand->admissionDeadlineNs);
    CHECK(old.service.Active()&&old.service.View()->requestState==AmmoResourceRequestState::Uncertain);
    CHECK(!old.service.Outcome());

    // Capacity is data, not family permission. Exercise the same native-entry
    // helper for all magazine mutations, including an empty-magazine no-op.
    for(int capacity:{17,30,100})for(bool empty:{false,true})
    for(auto operation:{AmmunitionOperation::RemoveMagazine,AmmunitionOperation::ReturnMagazine,AmmunitionOperation::RefillMagazine}){
        Fixture f;f.sample.counts={empty?0:capacity-1,191,capacity};
        if(operation!=AmmunitionOperation::RemoveMagazine){
            CHECK(f.Run(AmmunitionOperation::RemoveMagazine)==0);
            if(operation==AmmunitionOperation::RefillMagazine){
                const auto discard=f.Request(AmmunitionOperation::RemoveMagazine,true);
                CHECK(!f.service.Accept(discard,f.now));
                CHECK(f.service.Outcome()&&f.service.Outcome()->state==AmmoResourceRequestState::Completed);
            }
        }
        CHECK(f.Select());const auto request=f.Request(operation);
        const auto command=f.service.Accept(request,f.now);CHECK(command);
        const auto before=command->before;unsigned clocks=0,calls=0;
        f.now=command->admissionDeadlineNs;
        CHECK(!f.service.InvokeNative(f.sample,[&]()noexcept{++clocks;return f.now;},[&](std::int64_t began)noexcept{
            ++calls;return AmmoResourceNativeCall{f.identity,command->id,++f.invocation,began,began+1,command->before,command->after,true,true,true};
        }));
        CHECK(clocks==1&&calls==0&&!f.service.Active());
        CHECK(f.service.Outcome()&&f.service.Outcome()->request==request);
        CHECK(f.service.Outcome()->state==AmmoResourceRequestState::Rejected&&!f.service.Outcome()->nativeDispatched&&!f.service.Outcome()->receipt);
        const auto ledger=f.service.Inventory().Find(f.binding.context.resource);CHECK(ledger);
        CHECK(ledger->Phase()==AmmunitionLedgerPhase::Ready&&ledger->Snapshot().counts==before);
        CHECK(!f.service.Accept(request,f.now)); // never renew or replay this edge

        f.now+=Ms;CHECK(f.Select());const auto next=f.Request(operation);
        const auto nextCommand=f.service.Accept(next,f.now);CHECK(nextCommand);
        AmmoResourceNativeCall actual;
        CHECK(f.service.InvokeNative(f.sample,[&]()noexcept{return f.now;},[&](std::int64_t began)noexcept{
            ++calls;actual={f.identity,nextCommand->id,(f.invocation+=10),began,began+1,nextCommand->before,nextCommand->after,true,true,true};return actual;
        }));
        CHECK(calls==1&&f.service.Active());
        CHECK(!f.service.Observe(f.Row(0,actual),f.now+100));
        CHECK(!f.service.Observe(f.Row(1,actual),f.now+100));
        CHECK(f.service.Observe(f.Row(2,actual),f.now+100));
        CHECK(!f.service.Active()&&f.service.Outcome()->request==next&&f.service.Outcome()->nativeDispatched);
        CHECK(f.service.Outcome()->receipt&&f.service.Outcome()->receipt->after==nextCommand->after);
        CHECK(f.service.Inventory().Find(f.binding.context.resource)->Snapshot().counts==nextCommand->after);
    }
    return 0;
}
int ActualNativeEntryFailureRemainsUncertain(){
    Fixture f;CHECK(f.Select());const auto request=f.Request(AmmunitionOperation::RemoveMagazine);
    const auto command=f.service.Accept(request,f.now);CHECK(command);unsigned calls=0;
    CHECK(!f.service.InvokeNative(f.sample,[&]()noexcept{return f.now;},[&](std::int64_t began)noexcept{
        ++calls;return AmmoResourceNativeCall{f.identity,command->id,++f.invocation,began,began+1,command->before,command->after,true,false,true};
    }));
    CHECK(calls==1&&f.service.Active()&&!f.service.Outcome());
    CHECK(f.service.View()->requestState==AmmoResourceRequestState::Uncertain);
    CHECK(f.service.Inventory().Find(f.binding.context.resource)->Phase()==AmmunitionLedgerPhase::NeedsReconciliation);
    f.service.Cancel(f.now+Ms);CHECK(f.service.Active());
    CHECK(!f.service.Accept(f.Request(AmmunitionOperation::RemoveMagazine),f.now));
    CHECK(calls==1&&!f.service.Outcome());
    return 0;
}
Bc2AmmoReserveLease ReplacementReserve(const ReloadHoldIdentity& old,std::int64_t now,std::uint64_t sequence){
    auto next=old;++next.owner.actorGeneration;
    // Reuse two address sets over many real lifetime generations. Pointer
    // novelty is not our capacity or stale-completion protection.
    const auto shift=(next.owner.actorGeneration%2)?0u:0x100000u;
    next.owner.soldier=0x20000+shift;next.owner.weak=0x30000+shift;
    next.serverSoldier=0x90000+shift;next.serverItem=0xa0000+shift;
    next.firing={0x50000+shift,0x60000+shift,0x70000+shift};
    Bc2AmmoReserveLease r;r.identity=next;r.sequence=sequence;r.observedNs=now;r.deadlineNs=now+100*Ms;
    r.loaded=22;r.reserve=191;r.capacity=30;r.verified=r.allThreeIdle=true;return r;
}
AmmoResourceActorDrain ActorDrain(const Fixture& f,const Bc2AmmoReserveLease& replacement,std::uint64_t serial){
    return {*f.service.ActorIdentity(),f.service.Active(),serial,serial,f.now,replacement.deadlineNs,true};
}
void AdoptReplacement(Fixture& f,const Bc2AmmoReserveLease& r){
    f.identity=r.identity;f.binding.owner=f.identity.owner;
    f.binding.context.resource.actor=f.identity.owner.soldier;
    f.binding.context.resource.actorGeneration=f.identity.owner.actorGeneration;
    f.sample.context=f.binding.context;f.sample.counts={r.loaded,r.reserve,r.capacity};
}
int ActorRetirementKeepsPersistentServiceSafe(){
    // The actual persistent service reproduces the old problem: once a real
    // native call is entered, losing completion is not an ordinary rejection.
    Fixture f;CHECK(f.Select());const auto request=f.Request(AmmunitionOperation::RemoveMagazine);
    const auto command=f.service.Accept(request,f.now);CHECK(command);unsigned callbacks=0;
    AmmoResourceNativeCall oldCall;
    CHECK(f.service.InvokeNative(f.sample,[&]()noexcept{return f.now;},[&](std::int64_t began)noexcept{
        ++callbacks;oldCall={f.identity,command->id,++f.invocation,began,began+1,command->before,command->after,true,true,true};return oldCall;
    }));
    f.service.Cancel(f.now+Ms);CHECK(f.service.Active());
    CHECK(f.service.View()->requestState==AmmoResourceRequestState::Uncertain);
    f.now+=10*Ms;const auto replacement=ReplacementReserve(f.identity,f.now,100);
    const auto good=ActorDrain(f,replacement,10);
    for(unsigned bad=0;bad<14;++bad){auto r=replacement;auto d=good;
        switch(bad){
        case 0:r.identity=f.identity;++r.identity.owner.actorGeneration;break; // boat/recenter only
        case 1:r.identity.serverSoldier=f.identity.serverSoldier;break;
        case 2:r.identity.owner.actorGeneration=f.identity.owner.actorGeneration;break;
        case 3:d.callbacksDrained=false;break;
        case 4:d.callbackRevision=0;break;
        case 5:d.lifetimeEndEpoch=0;break;
        case 6:++d.expected.owner.weak;break;
        case 7:d.pending.reset();break;
        case 8:++d.pending->id;break;
        case 9:r.deadlineNs=f.now;break;
        case 10:r.allThreeIdle=false;break;
        case 11:r.identity.firing[2]=r.identity.firing[0];break;
        case 12:d.deadlineNs=f.now;break;
        case 13:r.verified=false;break;
        }
        CHECK(!f.service.RetireForReplacementActor(r,f.now,d));
        CHECK(f.service.Active()==command&&f.service.Inventory().Size()==1&&!f.service.LastActorRetirement());
    }
    // New selection alone cannot clear the old native obligation.
    auto binding=f.binding;binding.owner=replacement.identity.owner;
    binding.context.resource.actor=binding.owner.soldier;binding.context.resource.actorGeneration=binding.owner.actorGeneration;
    binding.observedNs=f.now;binding.deadlineNs=f.now+100*Ms;
    auto sample=f.sample;sample.context=binding.context;sample.observedNs=f.now;sample.deadlineNs=f.now+100*Ms;++sample.sequence;
    CHECK(!f.service.Select(binding,replacement.identity,sample,f.now)&&f.service.Active()==command);
    CHECK(f.service.RetireForReplacementActor(replacement,f.now,good));
    CHECK(!f.service.Active()&&f.service.Inventory().Size()==0&&callbacks==1);
    CHECK(f.service.Outcome()&&f.service.Outcome()->request==request&&!f.service.Outcome()->receipt);
    CHECK(f.service.Outcome()->state==AmmoResourceRequestState::ActorRetired&&f.service.Outcome()->nativeDispatched);
    CHECK(f.service.Outcome()->actorRetirement&&f.service.Outcome()->actorRetirement->pending==command);
    CHECK(!f.service.Observe(f.Row(2,oldCall),f.now));
    CHECK(!f.service.RetireForReplacementActor(replacement,f.now,good));
    AdoptReplacement(f,replacement);CHECK(f.Select());
    CHECK(!f.service.Accept(request,f.now)); // old request high-water survived
    CHECK(f.Run(AmmunitionOperation::RemoveMagazine)==0);
    CHECK(f.sample.counts.loaded==0&&f.sample.counts.reserve==191); // no old reserve credit

    // Keep the SAME service (including native request high-water marks) through
    // 130 further actor replacements. The historical 64-slot capacity is not
    // papered over by reconstructing the service for each lifetime.
    for(unsigned lifetime=0;lifetime<130;++lifetime){
        const auto completed=f.service.Outcome();
        if(lifetime%2){CHECK(f.Select());const auto next=f.Request(AmmunitionOperation::RefillMagazine);
            // An old removed original remains owned until explicit retirement;
            // a rejected refill is not permission to clear that actor.
            f.service.Accept(next,f.now);f.service.Cancel(f.now);
        }
        f.now+=20*Ms;const auto fresh=ReplacementReserve(*f.service.ActorIdentity(),f.now,200+lifetime);
        const auto drain=ActorDrain(f,fresh,20+lifetime);
        CHECK(f.service.RetireForReplacementActor(fresh,f.now,drain));
        CHECK(f.service.Inventory().Size()==0&&!f.service.Active());
        // A prior completed native transfer remains completed, not rolled back.
        if(!(lifetime%2)&&completed){CHECK(f.service.Outcome()->request==completed->request);
            CHECK(f.service.Outcome()->state==completed->state&&f.service.Outcome()->receipt==completed->receipt);}
        AdoptReplacement(f,fresh);CHECK(f.Select());CHECK(f.service.Inventory().Size()==1);
        CHECK(f.Run(AmmunitionOperation::RemoveMagazine)==0);
        CHECK(f.sample.counts.reserve==191);
    }
    // An intermediate replacement can itself disappear before its first item
    // publication. Empty-domain retirement must not create another permanent
    // barrier or fabricate requirements/counts.
    f.now+=20*Ms;auto r=ReplacementReserve(*f.service.ActorIdentity(),f.now,1000);
    CHECK(f.service.RetireForReplacementActor(r,f.now,ActorDrain(f,r,1000)));
    f.now+=20*Ms;r=ReplacementReserve(*f.service.ActorIdentity(),f.now,1001);
    CHECK(f.service.RetireForReplacementActor(r,f.now,ActorDrain(f,r,1001)));
    AdoptReplacement(f,r);CHECK(f.Select());CHECK(f.Run(AmmunitionOperation::RemoveMagazine)==0);
    std::puts("Persistent ammo actor retirement: exact drain/replacement, stale command rejection, completed transfer preservation, 132 lifetimes.");
    return 0;
}

int Transactions(){for(int loaded:{0,22,30}){
    Fixture f;f.sample.counts.loaded=loaded;CHECK(f.Run(AmmunitionOperation::RemoveMagazine)==0);
    CHECK(f.service.View()->wellEmpty&&f.service.View()->original->rounds==loaded);
    CHECK(f.Run(AmmunitionOperation::ReturnMagazine)==0);CHECK(f.sample.counts.loaded==loaded&&f.sample.counts.reserve==191);
    CHECK(f.Run(AmmunitionOperation::RemoveMagazine)==0);
    const auto drop=f.Request(AmmunitionOperation::RemoveMagazine,true);CHECK(!f.service.Accept(drop,f.now));
    CHECK(f.service.View()->requestState==AmmoResourceRequestState::Completed&&f.service.View()->originalState==MagazineResourceState::Discarded);
    CHECK(f.Run(AmmunitionOperation::RefillMagazine)==0);CHECK((f.sample.counts==AmmunitionCounts{30,161,30}));
}return 0;}
int Rejections(){for(unsigned bad=0;bad<12;++bad){Fixture f;CHECK(f.Select());auto r=f.Request(AmmunitionOperation::RemoveMagazine);
    if(bad==0)r.binding.inventory++;if(bad==1)r.binding.data++;if(bad==2)r.binding.persistence++;if(bad==3)r.binding.switching++;
    if(bad==4)r.binding.context.resource.weaponGeneration++;if(bad==5)r.binding.owner.weapon++;if(bad==6)r.intent.context.equipGeneration++;
    if(bad==7)r.intent.deadlineNs=f.now;if(bad==8)r.intent.observedNs=f.now+1;if(bad==9)r.intent.original=RemovedMagazineResource{};
    if(bad==10)r.discard=true;if(bad==11)r.intent.context.space++;
    CHECK(!f.service.Accept(r,f.now)&&!f.service.Active());CHECK(f.service.View()->requestState==AmmoResourceRequestState::Rejected);
    CHECK(!f.service.Accept(r,f.now));CHECK(f.service.Inventory().Find(f.binding.context.resource)->Snapshot().counts.loaded==22);
}return 0;}
int CancelAndNoAutomaticRetry(){for(bool dispatched:{false,true}){
    Fixture f;CHECK(f.Select());auto r=f.Request(AmmunitionOperation::RemoveMagazine);auto c=f.service.Accept(r,f.now);CHECK(c);
    if(dispatched)CHECK(f.service.Dispatch(f.sample,f.now));
    ++f.identity.owner.equipGeneration;f.binding.owner=f.identity.owner;++f.binding.context.equipGeneration;f.sample.context=f.binding.context;
    CHECK(!f.Select());CHECK(bool(f.service.Active())==dispatched);
    if(dispatched){CHECK(!f.Select());CHECK(!f.service.Accept(f.Request(AmmunitionOperation::RemoveMagazine),f.now));}
    else CHECK(f.Select());
}return 0;}
int ChannelConcurrency(){AmmoResourceChannel channel;std::atomic<bool> failed=false,done=false;constexpr unsigned Count=10000;
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    std::thread producer([&]{for(unsigned n=1;n<=Count&&!failed;++n){AmmoResourceRequest r;r.intent.id=n;r.binding.data=n*3;r.binding.persistence=n*5;
        while(!channel.Submit(r)){if(std::chrono::steady_clock::now()>until){failed=true;break;}std::this_thread::yield();}}done=true;});
    unsigned seen=0;while(seen<Count&&!failed){if(auto r=channel.Take()){
        if(r->intent.id!=seen+1||r->binding.data!=r->intent.id*3||r->binding.persistence!=r->intent.id*5)failed=true;++seen;
    }else if(std::chrono::steady_clock::now()>until)failed=true;else std::this_thread::yield();}
    producer.join();CHECK(!failed&&done&&seen==Count);return 0;
}
int TerminalOutcomesSurviveSelection(){
    Fixture f;CHECK(f.Run(AmmunitionOperation::RemoveMagazine)==0);
    const auto completed=*f.service.Outcome();CHECK(completed.receipt&&completed.nativeDispatched);
    CHECK(f.channel.Outcome(completed.request.intent.id,completed.request.intent.context));
    f.now+=500*Ms;++f.identity.owner.equipGeneration;f.binding.owner=f.identity.owner;
    ++f.binding.context.equipGeneration;f.sample.context=f.binding.context;CHECK(f.Select());
    CHECK(f.service.View()->terminal->request==completed.request);CHECK(!f.service.View()->receipt);
    CHECK(f.channel.Publish(f.service.View()));CHECK(f.channel.Publish({}));
    CHECK(f.channel.Outcome(completed.request.intent.id,completed.request.intent.context));
    CHECK(!f.channel.Outcome(completed.request.intent.id,f.sample.context));
    const auto drop=f.Request(AmmunitionOperation::RemoveMagazine,true);CHECK(!f.service.Accept(drop,f.now));
    CHECK(f.Select());const auto request=f.Request(AmmunitionOperation::RefillMagazine);CHECK(f.service.Accept(request,f.now));
    f.service.Cancel(f.now);const auto rejected=*f.service.Outcome();
    CHECK(rejected.request==request&&rejected.state==AmmoResourceRequestState::Rejected&&!rejected.nativeDispatched&&!rejected.receipt);
    CHECK(f.Select());const auto uncertain=f.Request(AmmunitionOperation::RefillMagazine);CHECK(f.service.Accept(uncertain,f.now));
    CHECK(f.service.Dispatch(f.sample,f.now));f.service.Cancel(f.now);
    CHECK(f.service.Outcome()->request==rejected.request); // uncertainty is never a rejected/complete terminal result
    return 0;
}
int SamePointerReplacementKeepsOriginalResourceIsolated(){
    for(int oldLoaded:{0,22,30})for(int newLoaded:{0,7,17}){
        Fixture f;f.sample.counts.loaded=oldLoaded;CHECK(f.Run(AmmunitionOperation::RemoveMagazine)==0);
        const auto oldBinding=f.binding;const auto oldCounts=f.sample.counts;
        const auto original=*f.service.View()->original;const auto completed=*f.service.Outcome();
        const auto staleReturn=f.Request(AmmunitionOperation::ReturnMagazine);
        // Native pickups can reuse a weapon address. The coherent inventory
        // binding supplies a new resource generation and equipment metadata;
        // neither a removed original nor its receipt belongs to that new item.
        f.now+=Ms;++f.identity.owner.equipGeneration;f.binding.owner=f.identity.owner;
        ++f.binding.context.resource.weaponGeneration;++f.binding.context.equipGeneration;
        f.binding.data+=0x1000;f.binding.persistence+=0x1000;
        f.sample.context=f.binding.context;f.sample.counts={newLoaded,24,17};
        const auto replacementCounts=f.sample.counts;CHECK(f.Select());
        CHECK(f.binding.owner.weapon==oldBinding.owner.weapon&&f.service.Inventory().Size()==2);
        CHECK(f.service.View()->snapshot.counts==replacementCounts&&!f.service.View()->wellEmpty);
        CHECK(!f.service.View()->original&&!f.service.View()->receipt&&!f.service.View()->command);
        CHECK(f.service.View()->originalState==MagazineResourceState::None);
        CHECK(f.service.View()->terminal&&f.service.View()->terminal->request==completed.request);
        CHECK(f.channel.Publish(f.service.View()));
        CHECK(!f.channel.Read(oldBinding.owner,f.now));
        const auto terminal=f.channel.Outcome(completed.request.intent.id,oldBinding.context);
        CHECK(terminal&&terminal->request==completed.request&&terminal->receipt==completed.receipt);
        CHECK(!f.channel.Outcome(completed.request.intent.id,f.binding.context));
        auto wrongResource=oldBinding.context;wrongResource.resource=f.binding.context.resource;
        CHECK(!f.channel.Outcome(completed.request.intent.id,wrongResource));
        // The old request is still within its original deadline, so refusal
        // proves ownership isolation rather than merely waiting for expiry.
        CHECK(f.now<staleReturn.intent.deadlineNs);
        CHECK(!f.service.Accept(staleReturn,f.now)&&!f.service.Active());
        auto borrowed=f.Request(AmmunitionOperation::ReturnMagazine);borrowed.intent.original=original;
        CHECK(!f.service.Accept(borrowed,f.now)&&!f.service.Active());
        CHECK(f.service.View()->snapshot.counts==replacementCounts);
        CHECK(f.service.Inventory().Find(f.binding.context.resource)->Snapshot().counts==replacementCounts);
        const auto old=f.service.Inventory().Find(oldBinding.context.resource);
        CHECK(old&&old->Original()==original&&old->ResourceState()==MagazineResourceState::Held);
        CHECK(old->WellEmpty()&&old->Snapshot().counts==oldCounts);
        // The replacement remains usable through its own remove/return cycle;
        // completing it cannot consume or restore the previous item's rounds.
        CHECK(f.Run(AmmunitionOperation::RemoveMagazine)==0);
        CHECK(f.service.View()->original->owner==f.binding.context.resource);
        CHECK(f.Run(AmmunitionOperation::ReturnMagazine)==0);
        CHECK(f.sample.counts==replacementCounts&&old->Original()==original);
        CHECK(old->ResourceState()==MagazineResourceState::Held&&old->Snapshot().counts==oldCounts);
    }
    std::puts("Same-pointer replacement: nine old/new loaded states retain separate resources, exact terminal contexts and conserved counts (mock Updates).");
    return 0;
}
int ReadReasonsPreserveAdmission(){
    Fixture f;CHECK(f.Select());const auto good=*f.service.View();
    const auto missing=f.channel.ReadObserved(f.identity.owner,f.now);
    CHECK(missing.reason==AmmoResourceReadReason::NoView&&missing.Deferred());CHECK(f.channel.Publish(good));
    const auto available=f.channel.ReadObserved(f.identity.owner,f.now);
    CHECK(available.reason==AmmoResourceReadReason::Available&&available.view&&!available.Deferred());
    auto future=good;future.snapshot.observedNs=f.now+1;CHECK(f.channel.Publish(future));
    // Reproduce a newer publication seen against saved Gather time. Merely
    // recording actual processing time must NOT silently admit that view.
    const auto ahead=f.channel.ReadObserved(f.identity.owner,f.now,f.now+2);
    CHECK(ahead.reason==AmmoResourceReadReason::Future&&ahead.Deferred());
    CHECK(f.channel.Read(f.identity.owner,f.now+2));
    const auto expired=f.channel.ReadObserved(f.identity.owner,f.now+100*Ms);
    CHECK(expired.reason==AmmoResourceReadReason::Expired&&expired.Deferred());
    auto wrong=f.identity.owner;++wrong.equipGeneration;const auto changed=f.channel.ReadObserved(wrong,f.now+2);
    CHECK(changed.reason==AmmoResourceReadReason::Owner&&!changed.view&&!changed.Deferred());
    auto invalid=good;invalid.snapshot.counts.loaded=31;CHECK(f.channel.Publish(invalid));
    const auto malformed=f.channel.ReadObserved(f.identity.owner,f.now);
    CHECK(malformed.reason==AmmoResourceReadReason::Invalid&&!malformed.view&&!malformed.Deferred());
    std::ostringstream out;f.channel.ReportReadEvidence(out,true);const auto text=out.str();
    CHECK(text.find("\"counts\":[2,0,1,0,1,1,1,1]")!=std::string::npos);
    CHECK(text.find("\"requested_now_ns\":1000000000,\"processing_now_ns\":1000000002")!=std::string::npos);
    CHECK(text.find("\"snapshot_observed_ns\":1000000001")!=std::string::npos);
    CHECK(text.find("\"total\":5,\"overwritten\":0")!=std::string::npos);
    return 0;
}
int ReadJournalBoundAndDrain(){
    AmmoResourceReadEvidence journal;AmmoResourceReadRow row;row.reason=AmmoResourceReadReason::Expired;
    for(unsigned n=1;n<=140;++n){row.request=n;journal.Note(row);}
    std::ostringstream live; journal.Report(live,false);CHECK(live.str().find("\"rows\"")==std::string::npos);
    std::ostringstream drained;journal.Report(drained,true);const auto text=drained.str();
    CHECK(text.find("\"total\":140,\"overwritten\":12,\"busy_dropped\":0")!=std::string::npos);
    CHECK(text.find("\"request\":12,")==std::string::npos&&text.find("\"request\":13,")!=std::string::npos);
    CHECK(text.find("\"request\":140,")!=std::string::npos);return 0;
}
int ConcurrentViewReadStillCoherent(){
    Fixture f;CHECK(f.Select());const auto good=*f.service.View();
    std::atomic<bool> done=false,failed=false;
    std::thread producer([&]{for(unsigned n=1;n<=10000;++n){auto v=good;v.request=n;v.snapshot.counts.loaded=int(n%31);f.channel.Publish(v);}done=true;});
    unsigned reads=0;
    do{if(const auto v=f.channel.Read(f.identity.owner,f.now);v&&v->snapshot.counts.loaded!=int(v->request%31))failed=true;++reads;}
    while(!done||reads<10000);
    producer.join();CHECK(!failed);return 0;
}
int FailedOperationEvidenceSurvivesShutdownTail(){
    for(const auto reason:{AmmoResourceReadReason::NoView,AmmoResourceReadReason::Future,AmmoResourceReadReason::Expired}){
        Fixture f;AmmoResourceChannel channel;CHECK(!channel.Read(f.identity.owner,f.now)); // startup cannot occupy the operation failure
        CHECK(f.Run(AmmunitionOperation::RemoveMagazine)==0);
        CHECK(channel.Publish(f.service.View(),f.service.Outcome()));
        const auto accepted=*channel.Read(f.identity.owner,f.now);
        auto failed=accepted;const auto failureTime=f.now+2*Ms;
        if(reason==AmmoResourceReadReason::NoView)CHECK(channel.Publish({}));
        if(reason==AmmoResourceReadReason::Future){failed.snapshot.observedNs=failureTime+1;CHECK(channel.Publish(failed));}
        if(reason==AmmoResourceReadReason::Expired){failed.snapshot.deadlineNs=failureTime;CHECK(channel.Publish(failed));}
        CHECK(!channel.Read(f.identity.owner,failureTime,failureTime+100));
        CHECK(channel.Publish(accepted)); // recovery does not erase the failed read
        CHECK(channel.Read(f.identity.owner,f.now));
        for(unsigned n=0;n<2600;++n)CHECK(!channel.Read(f.identity.owner,f.now+1000*Ms+n));
        std::ostringstream out;channel.ReportReadEvidence(out,true);const auto text=out.str();
        const auto failure=std::string("\"first_operation_failure\":{\"reason\":")+std::to_string(unsigned(reason))+
            ",\"requested_now_ns\":"+std::to_string(failureTime)+",\"processing_now_ns\":"+std::to_string(failureTime+100);
        CHECK(text.find(failure)!=std::string::npos);
        const auto prior=std::string("\"prior_operation_view\":{\"reason\":0,\"requested_now_ns\":")+std::to_string(f.now);
        CHECK(text.find(prior)!=std::string::npos);
        CHECK(text.find("\"first_by_reason\":[null,null,{\"reason\":2,\"requested_now_ns\":1000000000")!=std::string::npos);
        CHECK(f.service.View()->wellEmpty&&f.service.View()->original->rounds==22&&!f.service.Active());
        CHECK(f.service.Inventory().Size()==1); // diagnostics do not settle/return/discard a native item
        std::ostringstream live;channel.ReportReadEvidence(live,false);
        CHECK(live.str().find("first_operation_failure")==std::string::npos&&live.str().find("first_by_reason")==std::string::npos);
    }return 0;
}
int RejectedAdmissionCapturesExactCause(){
    using Reason=AmmoResourceRejectionReason;
    for(unsigned bad=0;bad<9;++bad){
        Fixture f;if(bad!=0)CHECK(f.Select());auto r=f.Request(AmmunitionOperation::RemoveMagazine);
        Reason expected=Reason::NoView;
        switch(bad){
            case 1:f.now=f.sample.deadlineNs;expected=Reason::ViewTime;break;
            case 2:r.binding.deadlineNs=f.now;expected=Reason::SourceBindingTime;break;
            case 3:r.binding.inventory=0;expected=Reason::SourceBindingInvalid;break;
            case 4:r.binding.inventory++;expected=Reason::ContextBindingMismatch;break;
            case 5:r.intent.deadlineNs=f.now;expected=Reason::IntentTime;break;
            case 6:r.intent.original=RemovedMagazineResource{};expected=Reason::LedgerSubmit;break;
            case 7:r.discard=true;expected=Reason::Discard;break;
            case 8:f.identity.firing[2]=1;CHECK(f.Select());r=f.Request(AmmunitionOperation::RemoveMagazine);expected=Reason::CompletionBegin;break;
        }
        CHECK(!f.service.Accept(r,f.now));const auto first=f.service.FirstRejection();CHECK(first);
        CHECK(first->reason==expected&&first->request==r&&first->processingNowNs==f.now);
        CHECK(!first->activeCommand&&!first->adjacentSnapshot&&!first->nativeDispatched);
        CHECK(bool(first->currentBinding)==(bad!=0)&&bool(first->currentSnapshot)==(bad!=0));
        CHECK(!f.service.Active()&&f.service.Outcome()->state==AmmoResourceRequestState::Rejected);
        if(bad!=0){CHECK(first->currentBinding==f.binding);CHECK(first->currentSnapshot->counts==f.sample.counts);}
        CHECK(std::string(AmmoResourceRejectionName(expected))!="unknown");
        CHECK(!f.service.Accept(r,f.now)); // Duplicate cannot replace the first row.
        auto later=r;++later.intent.id;later.intent.deadlineNs=f.now;
        CHECK(!f.service.Accept(later,f.now));
        CHECK(f.service.FirstRejection()->request==first->request&&f.service.FirstRejection()->reason==first->reason);
    }
    return 0;
}
int CapturedSeatDeadlineSeparatesNativeViewAndSubmittedSource(){
    using Reason=AmmoResourceRejectionReason;
    constexpr std::int64_t publication=90593482746000,seat=90593549920800,admission=90593617173700;
    for(bool currentRefreshed:{false,true}){
        Fixture f;f.now=publication-Ms;CHECK(f.Run(AmmunitionOperation::RemoveMagazine)==0);
        CHECK(!f.service.FirstRejection());f.now=seat;
        auto r=f.Request(AmmunitionOperation::ReturnMagazine);r.intent.deadlineNs=f.binding.deadlineNs;
        CHECK(r.binding.observedNs==publication&&r.intent.deadlineNs-seat==32825200);
        f.now=admission;if(currentRefreshed)CHECK(f.Select());
        CHECK(!f.service.Accept(r,admission));const auto first=f.service.FirstRejection();CHECK(first);
        CHECK(first->reason==(currentRefreshed?Reason::SourceBindingTime:Reason::ViewTime));
        CHECK(first->processingNowNs==admission&&first->request==r);
        CHECK(first->request.intent.observedNs==seat&&first->request.binding.deadlineNs==90593582746000);
        CHECK(first->currentSnapshot->observedNs==(currentRefreshed?admission:publication));
        CHECK((first->currentSnapshot->counts==AmmunitionCounts{0,191,30}));
        CHECK(f.service.View()->wellEmpty&&f.service.View()->original->rounds==22&&!f.service.Active());
        f.now+=500*Ms;CHECK(f.Select());auto later=f.Request(AmmunitionOperation::ReturnMagazine);
        later.intent.deadlineNs=f.now;CHECK(!f.service.Accept(later,f.now));
        CHECK(f.service.FirstRejection()->request==r&&f.service.FirstRejection()->processingNowNs==admission);
    }
    return 0;
}
int DispatchAndExpiryDiagnosticsDoNotChangeOutcomes(){
    using Reason=AmmoResourceRejectionReason;
    for(unsigned action=0;action<3;++action){
        Fixture f;CHECK(f.Select());const auto r=f.Request(AmmunitionOperation::RemoveMagazine);
        const auto command=f.service.Accept(r,f.now);CHECK(command&&!f.service.FirstRejection());
        if(action==0){auto adjacent=f.sample;adjacent.counts.loaded--;
            CHECK(!f.service.Dispatch(adjacent,f.now));
            CHECK(f.service.FirstRejection()->reason==Reason::Dispatch);
            CHECK(f.service.FirstRejection()->adjacentSnapshot->counts==adjacent.counts);
        }else{
            if(action==2)CHECK(f.service.Dispatch(f.sample,f.now));
            f.now=action==1?command->admissionDeadlineNs:command->deadlineNs;f.service.Expire(f.now);
            CHECK(f.service.FirstRejection()->reason==(action==1?Reason::QueuedExpired:Reason::DispatchedExpired));
        }
        const auto first=f.service.FirstRejection();CHECK(first->request==r&&first->activeCommand==command);
        CHECK(first->nativeDispatched==(action==2)&&first->processingNowNs==f.now);
        CHECK(first->currentSnapshot->counts==f.sample.counts);
        if(action<2){CHECK(!f.service.Active()&&f.service.Outcome()->state==AmmoResourceRequestState::Rejected);}
        else {CHECK(f.service.Active()&&!f.service.Outcome()&&f.service.View()->requestState==AmmoResourceRequestState::Uncertain);}
        f.service.Cancel(f.now+Ms);CHECK(f.service.FirstRejection()->processingNowNs==first->processingNowNs);
    }
    return 0;
}
int ConstructionProofCannotRenewInputOrBorrowCurrentAuthority(){
    for(unsigned bad=0;bad<21;++bad){
        Fixture f;CHECK(f.Select());const auto oldBinding=f.binding;const auto oldSample=f.sample;
        f.now+=67*Ms;auto r=f.Request(AmmunitionOperation::RemoveMagazine);
        r.sourceProof=AmmoResourceSourceProof{f.now,7,oldSample.sequence,oldSample.observedNs,oldSample.deadlineNs};
        f.now+=67*Ms;CHECK(f.Select()); // Source expired; independent current native sample is fresh.
        switch(bad){
            case 0:r.sourceProof.reset();break; // Legacy admission remains strict.
            case 1:r.sourceProof->constructedNs=r.intent.observedNs-1;break;
            case 2:r.sourceProof->constructedNs=f.now+1;break;
            case 3:r.sourceProof->constructedNs=oldBinding.deadlineNs;break;
            case 4:r.sourceProof->snapshotDeadlineNs=r.sourceProof->constructedNs;break;
            case 5:r.sourceProof->snapshotObservedNs=r.sourceProof->constructedNs+1;break;
            case 6:r.sourceProof->inputSequence=0;break;
            case 7:r.sourceProof->snapshotSequence=0;break;
            case 8:r.sourceProof->snapshotSequence=f.sample.sequence+1;break;
            case 9:r.binding.owner.weapon++;break;
            case 10:r.binding.inventory++;break;
            case 11:r.binding.switching++;break;
            case 12:r.binding.data++;break;
            case 13:r.binding.persistence++;break;
            case 14:r.intent.context.space++;break;
            case 15:r.intent.deadlineNs=f.now;break;
            case 16:r.intent.deadlineNs=r.intent.observedNs+100*Ms+1;break;
            case 17:f.now=f.sample.deadlineNs;break;
            case 18:f.now=f.sample.observedNs-1;break;
            case 19:r.sourceProof->snapshotDeadlineNs=r.sourceProof->snapshotObservedNs+100*Ms+1;break;
            case 20:r.binding.deadlineNs=r.sourceProof->constructedNs;break;
        }
        CHECK(!f.service.Accept(r,f.now)&&!f.service.Active());
        CHECK(f.service.FirstRejection()&&f.service.FirstRejection()->request==r);
        CHECK(f.service.Inventory().Find(f.binding.context.resource)->Snapshot().counts==f.sample.counts);
    }
    Fixture f;CHECK(f.Select());const auto source=f.sample;
    f.now+=67*Ms;auto r=f.Request(AmmunitionOperation::RemoveMagazine);
    r.sourceProof=AmmoResourceSourceProof{f.now,7,source.sequence,source.observedNs,source.deadlineNs};
    f.now+=67*Ms;CHECK(f.Select());const auto c=f.service.Accept(r,f.now);CHECK(c);
    CHECK(c->admissionDeadlineNs==r.intent.deadlineNs&&c->admissionDeadlineNs<f.sample.deadlineNs);
    CHECK(!f.service.Accept(r,f.now));f.service.Expire(r.intent.deadlineNs);
    CHECK(!f.service.Active()&&f.service.Outcome()->request==r&&!f.service.Outcome()->nativeDispatched);
    CHECK(f.service.FirstRejection()->reason==AmmoResourceRejectionReason::QueuedExpired);
    CHECK(f.service.FirstRejection()->request.sourceProof==r.sourceProof);
    return 0;
}
}
int main(){if(ActorRetirementKeepsPersistentServiceSafe())return 1;if(NativeEntryDeadlineKeepsUncalledOperationRejectable()||ActualNativeEntryFailureRemainsUncertain()||ConstructionProofCannotRenewInputOrBorrowCurrentAuthority()||RejectedAdmissionCapturesExactCause()||CapturedSeatDeadlineSeparatesNativeViewAndSubmittedSource()||DispatchAndExpiryDiagnosticsDoNotChangeOutcomes()||Transactions()||Rejections()||CancelAndNoAutomaticRetry()||ChannelConcurrency()||TerminalOutcomesSurviveSelection()||SamePointerReplacementKeepsOriginalResourceIsolated()||ReadReasonsPreserveAdmission()||ReadJournalBoundAndDrain()||ConcurrentViewReadStillCoherent()||FailedOperationEvidenceSurvivesShutdownTail())return 1;
    std::puts("BC2 ammunition service: native-observer handoff, no-op/original/refill, identity, cancellation and 10000 command handoffs passed (mock Updates).");return 0;}
