#include "Bc2QueuePhaseObservation.h"
#include "Test.h"
#include <memory>
#include <thread>
namespace fvr::bc2::queuePhase {
// Pause the real opening transition at its CAS/publication boundary without
// adding a callback, wait, or test switch to the production observation path.
struct JournalTestAccess {
    static bool ClaimOpening(Journal& journal){
        auto armed=Window::Armed;
        return journal.phase_.compare_exchange_strong(armed,Window::Opening);
    }
    static void PublishOpening(Journal& journal,Stamp now){journal.PublishOpening(now);}
};
}
using namespace fvr::bc2::queuePhase;
namespace {
Stamp At(std::int64_t ms){return {ms,ms*1000000};}
void Return(Journal& j,Token& token,ThreadState& thread,std::int64_t at,unsigned result=0){
    j.OriginalEnter(token,thread,At(at));j.OriginalReturn(token,thread,At(at+1));
    j.End(token,thread,At(at+2),true,result);
}
int Nested(){
    auto j=std::make_unique<Journal>();ThreadState thread{};Row row{};
    CHECK(j->Configure(1000)&&j->Start());
    auto pre=j->Begin(Kind::Consumer,thread,At(1),{7,70},1,2);
    CHECK(pre.active&&pre.slot==Capacity);Return(*j,pre,thread,2);CHECK(j->Snapshot().reserved==0);
    auto input=j->Begin(Kind::Input,thread,At(10),{7,70},100,200);
    CHECK(input.slot==Capacity);j->AcceptedLocal(input,At(11),7,8,9);
    CHECK(input.slot==0&&input.row.begin==10&&input.row.acceptedAt==11);
    CHECK(!j->Read(0,row));j->OriginalEnter(input,thread,At(12));
    auto first=j->Begin(Kind::Consumer,thread,At(13),{7,70},300,400);
    CHECK(first.row.parent==input.row.invocation&&first.row.originalInput==input.row.invocation);
    CHECK(first.row.inputDepth==1&&first.row.consumerDepth==1);
    j->OriginalEnter(first,thread,At(14));
    auto nested=j->Begin(Kind::Consumer,thread,At(15),{7,70},300,400);
    CHECK(nested.row.parent==first.row.invocation&&nested.row.consumerDepth==2);
    nested.row.stream=0x11223344;nested.row.outElement=0x55667788;nested.row.opaqueThird=0xffffffff;
    Return(*j,nested,thread,16,0xfedcba98);
    CHECK(thread.current==first.row.invocation&&thread.consumerDepth==1);
    j->OriginalReturn(first,thread,At(19));j->End(first,thread,At(20),true,3);
    auto recursive=j->Begin(Kind::Input,thread,At(21),{7,70},100,200);
    CHECK(recursive.row.inputDepth==2&&recursive.row.originalInput==input.row.invocation);
    Return(*j,recursive,thread,22);CHECK(thread.originalInput==input.row.invocation);
    j->OriginalReturn(input,thread,At(25));CHECK(thread.originalInput==0);
    j->End(input,thread,At(26),true);CHECK(thread.current==0&&thread.inputDepth==0&&thread.consumerDepth==0);
    CHECK(j->Read(2,row)&&row.result==0xfedcba98&&row.opaqueThird==0xffffffff&&!row.abnormal&&!row.clockError);
    CHECK(row.endActiveInput==1&&row.endActiveConsumer==2);
    j->Close(At(27));j->Close(At(30000));
    CHECK(j->Snapshot().closedAt==27&&j->Snapshot().closedAt<j->Snapshot().deadline);
    CHECK(j->Drained(true,0)&&!j->Drained(false,0)&&!j->Drained(true,1));
    CHECK(!j->Snapshot().unrecordedOverlap);
    CHECK(!j->Start()&&!j->Configure(1000));return 0;
}
int CrossThreadAndPublication(){
    auto j=std::make_unique<Journal>();CHECK(j->Configure(1000)&&j->Start());
    ThreadState main{};auto input=j->Begin(Kind::Input,main,At(100),{7,70},1,2);
    j->AcceptedLocal(input,At(101),7,1,2);j->OriginalEnter(input,main,At(102));
    std::atomic<bool> begun{false},finish{false},done{false};unsigned workerSlot=Capacity;
    std::thread worker([&]{
        ThreadState local{};auto consumer=j->Begin(Kind::Consumer,local,At(103),{7,71},3,4);
        workerSlot=consumer.slot;begun.store(true,std::memory_order_release);
        while(!finish.load(std::memory_order_acquire))std::this_thread::yield();
        Return(*j,consumer,local,104,1);done.store(true,std::memory_order_release);
    });
    while(!begun.load(std::memory_order_acquire))std::this_thread::yield();
    Row row{};const bool unpublished=workerSlot==1&&!j->Read(workerSlot,row);
    j->Close();const bool refusedDrain=!j->Drained(true,0);
    finish.store(true,std::memory_order_release);
    // Publication itself must be sufficient to read a complete immutable row;
    // do not rely on join() to establish its acquire/release relationship.
    bool published=false;while(!published&&!done.load(std::memory_order_acquire))published=j->Read(workerSlot,row);
    if(!published)published=j->Read(workerSlot,row);worker.join();
    CHECK(unpublished&&refusedDrain&&published);
    CHECK(row.thread.thread==7&&row.thread.birth==71&&row.parent==0&&row.originalInput==0);
    CHECK(row.inputDepth==0&&row.consumerDepth==1&&row.beginActiveInput==1&&row.result==1);
    j->OriginalReturn(input,main,At(107));j->End(input,main,At(108),true);
    CHECK(j->Drained(true,0)&&j->Snapshot().reserved==2);return 0;
}
int CarryInAfterExpiry(Kind kind){
    auto j=std::make_unique<Journal>();CHECK(j->Configure(1000)&&j->Start());
    std::atomic<bool> begun{false},finish{false};unsigned workerSlot=0;bool restored=false;
    std::thread worker([&]{
        ThreadState local{};auto token=j->Begin(kind,local,At(10),{8,80},3,4);
        workerSlot=token.slot;j->OriginalEnter(token,local,At(11));
        begun.store(true,std::memory_order_release);
        while(!finish.load(std::memory_order_acquire))std::this_thread::yield();
        // A nonlocal input never calls AcceptedLocal. Either kind can remain
        // inside its original while another thread opens and expires the window.
        j->OriginalReturn(token,local,At(20110));j->End(token,local,At(20111),true);
        restored=!local.current&&!local.originalInput&&!local.inputDepth&&!local.consumerDepth;
    });
    while(!begun.load(std::memory_order_acquire))std::this_thread::yield();
    ThreadState main{};auto input=j->Begin(Kind::Input,main,At(100),{7,70},1,2);
    j->AcceptedLocal(input,At(101),7,1,2);Return(*j,input,main,102);
    auto late=j->Begin(Kind::Consumer,main,At(20101),{7,70},1,2);Return(*j,late,main,20102);
    j->Close(At(20105));const auto pending=j->Snapshot();const bool refusedDrain=!j->Drained(true,0);
    finish.store(true,std::memory_order_release);worker.join();
    const auto complete=j->Snapshot();Row row{};
    CHECK(workerSlot==Capacity&&refusedDrain&&restored);
    CHECK(pending.window==Window::Expired&&!pending.unrecordedOverlap);
    CHECK(pending.activeInput+pending.activeConsumer==1);
    CHECK(j->Drained(true,0)&&complete.reserved==1&&complete.unrecordedOverlap);
    CHECK(!complete.activeInput&&!complete.activeConsumer&&!complete.clockErrors&&!complete.capped);
    CHECK(j->Read(0,row)&&row.acceptedLocal&&row.thread.birth==70&&!j->Read(1,row));
    CHECK(!j->Start()&&j->Snapshot().unrecordedOverlap);return 0;
}
int OpeningRace(Kind kind,bool endBeforePublish){
    auto j=std::make_unique<Journal>();CHECK(j->Configure(1000)&&j->Start());
    ThreadState main{};auto input=j->Begin(Kind::Input,main,At(100),{7,70},1,2);
    CHECK(JournalTestAccess::ClaimOpening(*j));
    std::atomic<bool> begun{false},finish{false};unsigned workerSlot=0;
    std::thread worker([&]{
        ThreadState local{};auto token=j->Begin(kind,local,At(101),{8,80},3,4);
        workerSlot=token.slot;j->OriginalEnter(token,local,At(102));
        begun.store(true,std::memory_order_release);
        while(!finish.load(std::memory_order_acquire))std::this_thread::yield();
        j->OriginalReturn(token,local,At(endBeforePublish?104:20110));
        j->End(token,local,At(endBeforePublish?105:20111),true);
    });
    while(!begun.load(std::memory_order_acquire))std::this_thread::yield();
    if(endBeforePublish){finish.store(true,std::memory_order_release);worker.join();}
    const auto opening=j->Snapshot();
    JournalTestAccess::PublishOpening(*j,At(100));j->AcceptedLocal(input,At(100),7,1,2);
    Return(*j,input,main,106);
    auto late=j->Begin(Kind::Consumer,main,At(20100),{7,70},1,2);Return(*j,late,main,20101);
    j->Close(At(20105));const bool drainedBeforeReturn=j->Drained(true,0);
    if(!endBeforePublish){finish.store(true,std::memory_order_release);worker.join();}
    const auto complete=j->Snapshot();
    CHECK(workerSlot==Capacity&&opening.window==Window::Opening&&!opening.activation);
    CHECK(!opening.unrecordedOverlap&&drainedBeforeReturn==endBeforePublish);
    CHECK(j->Drained(true,0)&&complete.unrecordedOverlap&&complete.reserved==1);
    CHECK(complete.window==Window::Expired&&complete.activation==100&&complete.deadline==20100);
    CHECK(!complete.clockErrors&&!complete.capped&&!complete.activeInput&&!complete.activeConsumer);
    return 0;
}
int Bounds(){
    auto j=std::make_unique<Journal>();CHECK(j->Configure(1000)&&j->Start());ThreadState thread{};
    auto first=j->Begin(Kind::Input,thread,At(100),{1,2},3,4);j->AcceptedLocal(first,At(100),1,2,3);
    Return(*j,first,thread,100);const auto deadline=j->Snapshot().deadline;CHECK(deadline==20100);
    for(unsigned n=1;n<Capacity;++n){auto token=j->Begin(Kind::Consumer,thread,At(103),{1,2},3,4);Return(*j,token,thread,104);}
    auto overflow=j->Begin(Kind::Consumer,thread,At(107),{1,2},3,4);
    CHECK(overflow.active&&overflow.slot==Capacity);Return(*j,overflow,thread,108);
    CHECK(j->Snapshot().reserved==Capacity&&j->Snapshot().capped==1&&j->Snapshot().dropped==1);
    CHECK(j->Snapshot().window==Window::Capped&&!j->Begin(Kind::Input,thread,At(110),{1,2},3,4).active);
    CHECK(j->Snapshot().deadline==deadline);j->Close();CHECK(j->Drained(true,0));
    j=std::make_unique<Journal>();CHECK(j->Configure(1000)&&j->Start());
    first=j->Begin(Kind::Input,thread,At(100),{1,2},3,4);j->AcceptedLocal(first,At(100),1,2,3);Return(*j,first,thread,101);
    auto crossing=j->Begin(Kind::Consumer,thread,At(20099),{1,2},3,4);Return(*j,crossing,thread,20100);
    Row row{};CHECK(j->Read(1,row)&&row.crossedDeadline&&!row.clockError);
    auto late=j->Begin(Kind::Input,thread,At(20103),{1,2},3,4);j->AcceptedLocal(late,At(20103),1,2,3);Return(*j,late,thread,20104);
    CHECK(j->Snapshot().window==Window::Expired&&j->Snapshot().reserved==2&&j->Snapshot().deadline==20100);
    CHECK(!j->Snapshot().unrecordedOverlap); // Wholly post-window intervals do not taint it.
    CHECK(!j->Start());j->Close();CHECK(!j->Start()&&j->Drained(true,0));return 0;
}
int AbnormalAndClock(){
    auto j=std::make_unique<Journal>();CHECK(j->Configure(1000)&&j->Start());ThreadState thread{};Row row{};
    auto token=j->Begin(Kind::Input,thread,At(100),{1,2},3,4);j->AcceptedLocal(token,At(101),1,2,3);
    j->OriginalEnter(token,thread,At(102));j->Close();CHECK(!j->Drained(true,0)&&!j->Read(0,row));
    j->End(token,thread,At(103),false);CHECK(j->Read(0,row)&&row.abnormal&&!row.originalReturn&&!row.clockError);
    CHECK(j->Drained(true,0)&&thread.originalInput==0&&thread.current==0);
    j=std::make_unique<Journal>();CHECK(j->Configure(1000)&&j->Start());
    token=j->Begin(Kind::Input,thread,At(100),{1,2},3,4);j->AcceptedLocal(token,At(101),1,2,3);
    j->OriginalEnter(token,thread,At(102));j->OriginalReturn(token,thread,At(103));j->End(token,thread,At(99),true);
    CHECK(j->Read(0,row)&&row.clockError&&j->Snapshot().clockErrors==1&&j->Snapshot().dropped==0);
    CHECK(j->Snapshot().closed&&!j->Start());return 0;
}
}
int main(){
    if(Nested()||CrossThreadAndPublication()||Bounds()||AbnormalAndClock())return 1;
    for(const auto kind:{Kind::Input,Kind::Consumer})
        if(CarryInAfterExpiry(kind)||OpeningRace(kind,false)||OpeningRace(kind,true))return 1;
    return 0;
}
