#include "Test.h"
#include "Bc2ReloadFlowRuntime.h"
#include <memory>
#include <thread>
#include <vector>
#include <algorithm>
using namespace fvr::bc2;
using namespace fvr;
namespace {
ReloadFlowEventInput Entry(unsigned branch=0,ReloadFlowEvent kind=ReloadFlowEvent::Update){
    ReloadFlowEventInput e;e.kind=kind;e.thread=12;e.depth=1;e.nowNs=41000000000ll;e.contextCopied=true;
    auto& b=e.boundary;b.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};b.firing=0x50000+0x1000*branch;b.branch=std::uint8_t(branch);
    b.wrapperOffset=branch==0?0x3c:branch==1?0x40:0x10;b.current=11;b.next=12;b.timer=.2f;b.loaded=6;b.reserve=21;
    if(branch==2){b.serverPlayer=0x80000;b.serverSoldier=0x90000;b.serverItem=0xa0000;}return e;
}
ReloadFlowEventEnd End(const ReloadFlowEventInput& e){ReloadFlowEventEnd out;out.thread=e.thread;out.nowNs=e.nowNs+1000;out.boundary=e.boundary;out.contextCopied=true;return out;}
int CompletionAfterRecorderCapacityAndFortySeconds(){
    const auto e=Entry();auto recorder=std::make_unique<ReloadFlowRecords>();
    for(unsigned n=0;n<ReloadFlowRecords::Capacity;++n){const auto id=recorder->Begin(e);CHECK(id&&recorder->End(id,End(e)));}
    CHECK(!recorder->Begin(e)&&recorder->Dropped()==1);
    ReloadFlowInvocationIds ids(ReloadFlowRecords::Capacity);ReloadRoundCompletion completion(true);
    ReloadHoldIdentity identity{e.boundary.owner,{0x50000,0x51000,0x52000},0x80000,0x90000,0xa0000};
    ReloadRoundLease lease{identity,94,100,e.nowNs,e.nowNs+100000000,6,21,8,true,true};
    const interaction::ManualReloadRequest request{777,{0x20000,1,0x40000,2,3},interaction::ReloadOperation::InsertRound,0,0};
    CHECK(completion.Begin(request,lease,e.nowNs,e.nowNs+1500000000));
    std::uint64_t serverTransfer=0;
    for(unsigned branch=0;branch<3;++branch){
        auto update=Entry(branch);ReloadFlowInvocation original;
        CHECK(original.Begin(ids.Next(),update));
        auto transfer=Entry(branch,ReloadFlowEvent::Transfer);transfer.nowNs+=100;transfer.depth=2;transfer.transferPath=ReloadTransferPath::OrdinaryState12;
        ReloadFlowInvocation called;CHECK(called.Begin(ids.Next(),transfer,&original));auto out=End(transfer);out.boundary->loaded=7;out.boundary->reserve=20;
        const auto observed=called.Finish(out);CHECK(observed&&observed->id>ReloadFlowRecords::Capacity&&observed->entry.parent==original.Record().id&&observed->entry.update==original.Record().id);
        CHECK(completion.Observe({identity,94,observed->id,observed->entry.nowNs,observed->exit.nowNs,branch,6,21,7,20,true,observed->identityRetained}));
        if(branch==2)serverTransfer=observed->id;
        CHECK(original.Finish(End(update)));
    }
    ReloadRoundSample sample;sample.identity=identity;sample.cycle=94;sample.sequence=101;sample.observedNs=e.nowNs+2000;sample.deadlineNs=e.nowNs+50000000;sample.nativeBindingVerified=sample.allThreeHeld=true;
    for(unsigned n=0;n<3;++n){auto& b=sample.branches[n];b.address=identity.firing[n];b.wrapperOffset=n==0?0x3c:n==1?0x40:0x10;b.currentState=11;b.nextState=12;b.phaseTimer=.72f;b.loaded=7;b.reserve=20;}
    CHECK(completion.Observe(sample,sample.observedNs));const auto ack=completion.TakeAcknowledgement(identity,94,sample.observedNs);
    CHECK(ack&&ack->semantic.request==777&&ack->serverInvocation==serverTransfer&&recorder->Records().size()==ReloadFlowRecords::Capacity);return 0;
}
int IndependentNestedParentEvidence(){ReloadFlowInvocationIds ids;auto u=Entry();ReloadFlowInvocation update;CHECK(update.Begin(ids.Next(),u));
    auto c=Entry(0,ReloadFlowEvent::Commit);c.depth=2;ReloadFlowInvocation commit;CHECK(commit.Begin(ids.Next(),c,&update));
    auto t=Entry(0,ReloadFlowEvent::Transfer);t.depth=3;ReloadFlowInvocation transfer;CHECK(transfer.Begin(ids.Next(),t,&commit));
    CHECK(transfer.Record().entry.parent==commit.Record().id&&transfer.Record().entry.update==update.Record().id);
    CHECK(transfer.Finish(End(t))&&commit.Finish(End(c))&&update.Finish(End(u)));
    CHECK(!transfer.Finish(End(t)));ReloadFlowInvocation late;CHECK(!late.Begin(ids.Next(),t,&update));return 0;}
int ExitEvidenceFailuresCannotBeRetried(){for(unsigned test=0;test<6;++test){auto e=Entry();ReloadFlowInvocation invocation;CHECK(invocation.Begin(1,e));auto end=End(e);
    switch(test){case 0:end.thread++;break;case 1:end.nowNs=e.nowNs-1;break;case 2:end.boundary.reset();break;case 3:end.contextCopied=false;break;
        case 4:end.boundary->owner.equipGeneration++;break;case 5:end.boundary->firing++;break;}
    CHECK(!invocation.Finish(end)&&!invocation.Finish(End(e)));}
    auto restore=Entry(0,ReloadFlowEvent::Restore);ReloadFlowInvocation bad;CHECK(bad.Begin(1,restore));CHECK(!bad.Finish(End(restore)));return 0;}
int RejectedCallerNesting(){for(unsigned test=0;test<7;++test){auto e=Entry();ReloadFlowInvocation parent;CHECK(parent.Begin(9,e));auto child=e;child.kind=ReloadFlowEvent::Transfer;child.depth=2;
    switch(test){case 0:child.thread++;break;case 1:child.depth=1;break;case 2:child.depth=9;break;case 3:child.boundary.owner.space++;break;
        case 4:child.parent=9;break;case 5:child.update=9;break;case 6:child.boundary.firing++;break;}
    ReloadFlowInvocation candidate;CHECK(!candidate.Begin(10,child,&parent));}
    ReloadFlowInvocation same;auto e=Entry();CHECK(same.Begin(9,e));ReloadFlowInvocation older;auto c=e;c.depth=2;CHECK(!older.Begin(8,c,&same));return 0;}
int MonotonicThreadIdsAndOverflow(){ReloadFlowInvocationIds ids;std::array<std::vector<std::uint64_t>,4> values;std::array<std::thread,4> threads;
    for(unsigned n=0;n<4;++n)threads[n]=std::thread([&,n]{for(unsigned j=0;j<1000;++j)values[n].push_back(ids.Next());});
    for(auto& thread:threads)thread.join();std::vector<std::uint64_t> all;for(auto& list:values)all.insert(all.end(),list.begin(),list.end());std::sort(all.begin(),all.end());
    for(unsigned n=0;n<all.size();++n)CHECK(all[n]==n+1);ReloadFlowInvocationIds last(UINT64_MAX-1);CHECK(last.Next()==UINT64_MAX&&last.Next()==0&&last.Next()==0);return 0;}
int ExplicitModeApiRemainsOff(){using namespace reloadFlowRuntime;CHECK(!RequestIdentity());CHECK(!StartRequestCycle({})&&!KeepAliveRequestCycle({}));
    CHECK(!RequestLease({},1)&&!SubmitRequest({})&&!TakeRequestAcknowledgement({},1));CancelRequestCycle();
    CHECK(!Install({},engine::PeImage{},0,{},true,true,true));return 0;}
}
int main(){if(CompletionAfterRecorderCapacityAndFortySeconds()||IndependentNestedParentEvidence()||ExitEvidenceFailuresCannotBeRetried()||RejectedCallerNesting()||MonotonicThreadIdsAndOverflow()||ExplicitModeApiRemainsOff())return 1;
    std::printf("Six persistent native adapter evidence groups passed; no game process used.\n");return 0;}
