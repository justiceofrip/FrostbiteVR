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
int ReadReasonsPreserveAdmission(){
    Fixture f;CHECK(f.Select());const auto good=*f.service.View();
    CHECK(!f.channel.Read(f.identity.owner,f.now));CHECK(f.channel.Publish(good));
    CHECK(f.channel.Read(f.identity.owner,f.now));
    auto future=good;future.snapshot.observedNs=f.now+1;CHECK(f.channel.Publish(future));
    // Reproduce a newer publication seen against saved Gather time. Merely
    // recording actual processing time must NOT silently admit that view.
    CHECK(!f.channel.Read(f.identity.owner,f.now,f.now+2));
    CHECK(f.channel.Read(f.identity.owner,f.now+2));
    CHECK(!f.channel.Read(f.identity.owner,f.now+100*Ms));
    auto wrong=f.identity.owner;++wrong.equipGeneration;CHECK(!f.channel.Read(wrong,f.now+2));
    auto invalid=good;invalid.snapshot.counts.loaded=31;CHECK(f.channel.Publish(invalid));
    CHECK(!f.channel.Read(f.identity.owner,f.now));
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
}
int main(){if(Transactions()||Rejections()||CancelAndNoAutomaticRetry()||ChannelConcurrency()||TerminalOutcomesSurviveSelection()||ReadReasonsPreserveAdmission()||ReadJournalBoundAndDrain()||ConcurrentViewReadStillCoherent()||FailedOperationEvidenceSurvivesShutdownTail())return 1;
    std::puts("BC2 ammunition service: native-observer handoff, no-op/original/refill, identity, cancellation and 10000 command handoffs passed (mock Updates).");return 0;}
