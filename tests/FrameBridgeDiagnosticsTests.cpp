#include "Test.h"
#include "fvr/graphics/FrameBridgeDiagnostics.h"
#include "fvr/graphics/FrameBridgeDiagnosticsJson.h"
#include "fvr/graphics/DepthEyeDiagnostics.h"
#include <sstream>
#include <thread>
using namespace fvr::graphics;
namespace {
FrameBridgeTimingKey Key(std::uint64_t request=1) {
    return {9,request,17,request+100,3,request+200,7,1000000000+std::int64_t(request)};
}
PairTicket Ticket(const FrameBridgeTimingKey& k) {
    PairTicket t;t.session[0]=5;t.resourceEpoch=13;t.sequence=k.request;t.frameId=k.frame;
    t.trackingGeneration=k.tracking;t.spaceGeneration=k.space;t.predictedNs=k.predictedNs;return t;
}
int Lifecycle() {
    FrameBridgeDiagnostics d;FrameBridgeTimingSnapshot s;const auto k=Key();const auto t=Ticket(k);
    d.SetFrequency(10000000);d.Begin(k,2784,3024,100);
    d.Mark(k,FrameBridgeTimingStage::CaptureLeftBegin,110);d.Mark(k,FrameBridgeTimingStage::CaptureLeftQueued,120);
    d.Mark(k,FrameBridgeTimingStage::CaptureRightBegin,130);d.Mark(k,FrameBridgeTimingStage::CaptureRightQueued,140);
    for(unsigned eye=0;eye<2;++eye)for(unsigned stage=0;stage<2;++stage)d.Depth(k,eye,stage,8,0,44,111+eye*20+stage);
    d.Mark(k,FrameBridgeTimingStage::Restored,150);d.Export(k,3,160);d.ExportBusy(k,161);
    d.Export(k,3,170);d.Mark(k,FrameBridgeTimingStage::ProducerFenceReady,171);d.Ticket(k,t);
    d.Mark(k,FrameBridgeTimingStage::Staged,180);
    CHECK(d.Snapshot(s)&&s.hasActive&&!s.count&&s.active.qpc[unsigned(FrameBridgeTimingStage::Published)]==0);
    d.Mark(k,FrameBridgeTimingStage::Published,190);d.Mark(k,FrameBridgeTimingStage::Published,195);
    d.Reaped(k,t,200);d.Released(t,true,201);
    CHECK(d.Snapshot(s)&&s.available&&!s.hasActive&&s.count==1&&s.completed==1&&s.frequency==10000000);
    const auto& r=s.rows[0];CHECK(r.key==k&&r.end==FrameBridgeTimingEnd::Consumed);
    CHECK(r.width==2784&&r.height==3024&&r.depthSamples==8&&r.depthFormat==44&&r.depthMask==15);
    CHECK(r.exportAttempts==2&&r.exportBusy==1&&r.lastExportBusyQpc==161);
    CHECK(r.qpc[unsigned(FrameBridgeTimingStage::ExportBegin)]==160&&r.qpc[unsigned(FrameBridgeTimingStage::Published)]==190);
    CHECK(r.qpc[unsigned(FrameBridgeTimingStage::Reaped)]==200&&r.qpc[unsigned(FrameBridgeTimingStage::Released)]==201);
    CHECK(s.unmatched==0&&s.invalidClock==0&&s.dropped==0);
    std::ostringstream json;WriteFrameBridgeTiming(json,s);const auto text=json.str();
    CHECK(text.find("\"frequency\":10000000")!=std::string::npos);
    CHECK(text.find("\"published\":190")!=std::string::npos);
    CHECK(text.find("\"end\":\"consumed\"")!=std::string::npos);
    CHECK(text.find("\"active\":null")!=std::string::npos);
    CHECK(!s.hasFirstNonConsumed&&text.find("\"first_non_consumed\":null")!=std::string::npos);return 0;
}
int ExactOwnershipAndMissingBoundaries() {
    FrameBridgeDiagnostics d;FrameBridgeTimingSnapshot s;auto k=Key();auto t=Ticket(k);d.Begin(k,32,16,1);d.Ticket(k,t);
    auto stale=k;++stale.channel;d.Mark(stale,FrameBridgeTimingStage::Published,2);
    auto wrong=t;++wrong.session[1];d.Released(wrong,true,3);wrong=t;++wrong.resourceEpoch;d.Reaped(k,wrong,4);
    CHECK(d.Snapshot(s)&&s.hasActive&&s.unmatched==3&&!s.count);
    CHECK(!s.active.qpc[unsigned(FrameBridgeTimingStage::Published)]&&!s.active.qpc[unsigned(FrameBridgeTimingStage::Reaped)]);
    d.Mark(k,FrameBridgeTimingStage::Published,0);d.Released(t,false,5);
    CHECK(d.Snapshot(s)&&s.rows[0].end==FrameBridgeTimingEnd::Abandoned&&s.invalidClock==1);
    ++k.channel;d.Begin(k,32,16,10);t=Ticket(k);d.Ticket(k,t);d.Reaped(k,t,11);d.Released(t,false,12);
    CHECK(d.Snapshot(s)&&s.rows[1].end==FrameBridgeTimingEnd::Discarded);
    // Cancel before any capture is not a completed/published pair.
    k=Key(2);d.Begin(k,32,16,20);d.Cancel(k,21);
    CHECK(d.Snapshot(s)&&s.rows[2].end==FrameBridgeTimingEnd::Cancelled&&!s.rows[2].ticket.sequence);
    CHECK(!s.rows[2].qpc[unsigned(FrameBridgeTimingStage::CaptureLeftQueued)]);
    k=Key(3);d.Begin(k,32,16,30);d.Begin(Key(4),32,16,40);d.Close(41);d.Close(42);
    CHECK(d.Snapshot(s)&&s.count==5&&s.rows[3].end==FrameBridgeTimingEnd::Superseded&&s.rows[4].end==FrameBridgeTimingEnd::Abandoned);
    return 0;
}
int BoundedRetention() {
    FrameBridgeDiagnostics d;FrameBridgeTimingSnapshot s;
    for(unsigned i=1;i<=FrameBridgeTimingCapacity+7;++i){const auto k=Key(i);d.Begin(k,64,48,i*10);d.Cancel(k,i*10+1);}
    CHECK(d.Snapshot(s)&&s.count==FrameBridgeTimingCapacity&&s.overwritten==7&&s.completed==FrameBridgeTimingCapacity+7);
    for(unsigned i=0;i<s.count;++i)CHECK(s.rows[i].key.request==i+8&&s.rows[i].end==FrameBridgeTimingEnd::Cancelled);
    // A snapshot is an owned copy: later publication cannot mutate it.
    d.Begin(Key(1000),64,48,10000);CHECK(s.rows[0].key.request==8&&!s.hasActive);return 0;
}
int DeferredRetirementLifecycle() {
    FrameBridgeDiagnostics d;FrameBridgeTimingSnapshot s;const auto old=Key(),next=Key(2);
    const auto oldTicket=Ticket(old),nextTicket=Ticket(next);
    d.Begin(old,2784,3024,100);d.Ticket(old,oldTicket);d.Mark(old,FrameBridgeTimingStage::Published,190);
    CHECK(d.MarkRetiring(old,oldTicket,200));
    CHECK(d.Snapshot(s)&&!s.hasActive&&s.hasRetiring&&!s.count&&s.retiring.key==old);
    CHECK(s.retiring.ackObservedQpc==200&&!s.retiring.qpc[unsigned(FrameBridgeTimingStage::Reaped)]&&
        !s.retiring.qpc[unsigned(FrameBridgeTimingStage::Released)]);
    std::ostringstream pending;WriteFrameBridgeTiming(pending,s);
    CHECK(pending.str().find("\"retiring\":{\"channel\":9,\"request\":1")!=std::string::npos&&
        pending.str().find("\"ack_observed_qpc\":200,\"graphics_release_qpc\":0")!=std::string::npos);
    d.Begin(next,2784,3024,210);d.Ticket(next,nextTicket);
    d.Mark(next,FrameBridgeTimingStage::CaptureLeftBegin,220);
    d.Reaped(old,oldTicket,230);d.Released(oldTicket,true,231);
    CHECK(d.Snapshot(s)&&s.hasActive&&!s.hasRetiring&&s.active.key==next&&s.count==1);
    CHECK(s.rows[0].key==old&&s.rows[0].end==FrameBridgeTimingEnd::Consumed&&s.rows[0].ackObservedQpc==200);
    CHECK(s.rows[0].qpc[unsigned(FrameBridgeTimingStage::Reaped)]==230&&s.rows[0].qpc[unsigned(FrameBridgeTimingStage::Released)]==231);
    CHECK(s.active.qpc[unsigned(FrameBridgeTimingStage::Begin)]==210&&
        s.active.qpc[unsigned(FrameBridgeTimingStage::CaptureLeftBegin)]==220&&!s.active.ackObservedQpc);
    d.Reaped(next,nextTicket,240);d.Released(nextTicket,true,241);
    CHECK(d.Snapshot(s)&&!s.hasActive&&!s.hasRetiring&&s.count==2&&!s.unmatched&&!s.invalidClock);
    std::ostringstream final;WriteFrameBridgeTiming(final,s);
    CHECK(final.str().find("\"ack_observed_qpc\":200,\"graphics_release_qpc\":231")!=std::string::npos&&
        final.str().find("\"retiring\":null")!=std::string::npos);
    return 0;
}
int DeferredRetirementExactOwnership() {
    FrameBridgeDiagnostics d;FrameBridgeTimingSnapshot s;const auto old=Key(),next=Key(2);
    const auto oldTicket=Ticket(old),nextTicket=Ticket(next);
    d.Begin(old,64,48,100);d.Ticket(old,oldTicket);
    auto wrongKey=old;++wrongKey.owner;auto wrongTicket=oldTicket;++wrongTicket.resourceEpoch;
    CHECK(!d.MarkRetiring(wrongKey,oldTicket,110));CHECK(!d.MarkRetiring(old,wrongTicket,111));
    CHECK(d.MarkRetiring(old,oldTicket,120));d.Begin(next,64,48,130);d.Ticket(next,nextTicket);
    CHECK(!d.MarkRetiring(next,nextTicket,140)); // One slot; never overwrite old evidence.
    d.Reaped(old,nextTicket,150);d.Released(wrongTicket,true,151);
    CHECK(d.Snapshot(s)&&s.unmatched==5&&s.hasActive&&s.hasRetiring&&!s.count);
    CHECK(s.retiring.key==old&&s.retiring.ackObservedQpc==120&&!s.retiring.qpc[unsigned(FrameBridgeTimingStage::Reaped)]);
    // A cancelled successor is independent of the old pair's eventual outcome.
    d.Cancel(next,160);d.Reaped(old,oldTicket,170);d.Released(oldTicket,false,171);
    CHECK(d.Snapshot(s)&&!s.hasActive&&!s.hasRetiring&&s.count==2);
    CHECK(s.rows[0].key==next&&s.rows[0].end==FrameBridgeTimingEnd::Cancelled&&
        s.rows[1].key==old&&s.rows[1].end==FrameBridgeTimingEnd::Discarded);
    d.Released(oldTicket,true,180);CHECK(d.Snapshot(s)&&s.unmatched==6&&s.completed==2);
    // Invalid clocks are reported, never replaced with the successor timestamp.
    d.Begin(Key(3),64,48,200);d.Ticket(Key(3),Ticket(Key(3)));
    CHECK(d.MarkRetiring(Key(3),Ticket(Key(3)),0));d.Begin(Key(4),64,48,210);d.Close(220);d.Close(221);
    CHECK(d.Snapshot(s)&&s.invalidClock==1&&s.count==4&&!s.hasActive&&!s.hasRetiring);
    CHECK(s.rows[2].key==Key(3)&&s.rows[2].end==FrameBridgeTimingEnd::Abandoned&&!s.rows[2].ackObservedQpc&&
        !s.rows[2].qpc[unsigned(FrameBridgeTimingStage::Reaped)]&&!s.rows[2].qpc[unsigned(FrameBridgeTimingStage::Released)]);
    CHECK(s.rows[3].key==Key(4)&&s.rows[3].end==FrameBridgeTimingEnd::Abandoned);
    return 0;
}
int DeferredRetirementBoundedRetention() {
    FrameBridgeDiagnostics d;FrameBridgeTimingSnapshot s;auto current=Key();d.Begin(current,64,48,10);d.Ticket(current,Ticket(current));
    for(unsigned i=1;i<=FrameBridgeTimingCapacity+7;++i){
        const auto old=current;const auto ticket=Ticket(old);
        CHECK(d.MarkRetiring(old,ticket,i*10+1));current=Key(i+1);d.Begin(current,64,48,i*10+2);d.Ticket(current,Ticket(current));
        d.Reaped(old,ticket,i*10+3);d.Released(ticket,true,i*10+4);
    }
    CHECK(d.Snapshot(s)&&s.hasActive&&!s.hasRetiring&&s.active.key==current&&
        s.count==FrameBridgeTimingCapacity&&s.overwritten==7&&s.completed==FrameBridgeTimingCapacity+7&&!s.unmatched);
    for(unsigned i=0;i<s.count;++i)CHECK(s.rows[i].key.request==i+8&&s.rows[i].end==FrameBridgeTimingEnd::Consumed&&
        s.rows[i].ackObservedQpc<s.rows[i].qpc[unsigned(FrameBridgeTimingStage::Reaped)]);
    return 0;
}
int FirstNonConsumedSurvivesWrap() {
    FrameBridgeDiagnostics d;FrameBridgeTimingSnapshot s;
    const auto consumed=Key(1),fault=Key(2),later=Key(3);
    d.Begin(consumed,64,48,10);d.Ticket(consumed,Ticket(consumed));d.Reaped(consumed,Ticket(consumed),11);d.Released(Ticket(consumed),true,12);
    CHECK(d.Snapshot(s)&&!s.hasFirstNonConsumed);
    // The first loss has both captures, but no fabricated publish/release.
    d.Begin(fault,64,48,20);d.Mark(fault,FrameBridgeTimingStage::CaptureLeftQueued,21);
    d.Mark(fault,FrameBridgeTimingStage::CaptureRightQueued,22);d.Cancel(fault,23);
    CHECK(d.Snapshot(s)&&s.hasFirstNonConsumed&&s.firstNonConsumed.key==fault);
    CHECK(s.firstNonConsumed.end==FrameBridgeTimingEnd::Cancelled&&
        s.firstNonConsumed.qpc[unsigned(FrameBridgeTimingStage::Cancelled)]==23&&
        !s.firstNonConsumed.ticket.sequence&&!s.firstNonConsumed.qpc[unsigned(FrameBridgeTimingStage::Published)]&&
        !s.firstNonConsumed.qpc[unsigned(FrameBridgeTimingStage::Released)]);
    // A later graphics-owned negative outcome must not replace that row.
    d.Begin(later,64,48,30);d.Ticket(later,Ticket(later));CHECK(d.MarkRetiring(later,Ticket(later),31));
    d.Begin(Key(4),64,48,32);d.Reaped(later,Ticket(later),33);d.Released(Ticket(later),false,34);d.Cancel(Key(4),35);
    for(unsigned i=5;i<FrameBridgeTimingCapacity+15;++i){const auto k=Key(i);const auto t=Ticket(k);
        d.Begin(k,64,48,i*10);d.Ticket(k,t);d.Reaped(k,t,i*10+1);d.Released(t,true,i*10+2);}
    CHECK(d.Snapshot(s)&&s.count==FrameBridgeTimingCapacity&&s.overwritten>0&&!s.hasActive&&!s.hasRetiring);
    CHECK(s.firstNonConsumed.key==fault&&s.firstNonConsumed.end==FrameBridgeTimingEnd::Cancelled&&
        s.firstNonConsumed.qpc[unsigned(FrameBridgeTimingStage::CaptureRightQueued)]==22&&
        s.firstNonConsumed.qpc[unsigned(FrameBridgeTimingStage::Cancelled)]==23);
    for(unsigned i=0;i<s.count;++i)CHECK(s.rows[i].key.request>4&&s.rows[i].end==FrameBridgeTimingEnd::Consumed);
    std::ostringstream json;WriteFrameBridgeTiming(json,s);
    CHECK(json.str().find("\"first_non_consumed\":{\"channel\":9,\"request\":2")!=std::string::npos);
    d.Begin(Key(1000),64,48,10000);d.Close(10001);
    CHECK(s.firstNonConsumed.key==fault); // Snapshot remains an independent copy.
    CHECK(d.Snapshot(s)&&s.firstNonConsumed.key==fault&&!s.unmatched&&!s.invalidClock);
    return 0;
}
int FirstDepthEyeObservation() {
    FirstDepthEyeFailure first;DepthEyeFailureRow row,out;row.key=Key(17);row.slot=1;row.slotCount=2;
    row.eye=0;row.width=2784;row.height=3024;row.beginQpc=101;row.endQpc=111;
    CHECK(first.Pending()&&!first.Snapshot(out,true));
    std::ostringstream absent;first.Write(absent,true);
    CHECK(absent.str().find("\"observed\":false")!=std::string::npos);
    first.Begin(row);first.Hook(false);first.Hook(false);
    CHECK(row.Test(0,true)&&!row.Test(1,false));
    CHECK(std::string(row.FailureClass())=="no_scoped_clear_calls");
    first.Observe(row);CHECK(!first.Pending()&&!first.Snapshot(out,false)&&first.Snapshot(out,true));
    CHECK(out.key==row.key&&out.allHooksEnd==2&&out.unscopedEnd==2&&out.evaluated==3&&out.passed==1);
    for(unsigned i=0;i<1000;++i){auto later=row;later.key=Key(i+100);later.scopedHooks=3;first.Observe(later);first.Hook(true);}
    CHECK(first.Snapshot(out,true)&&out.key==row.key&&out.allHooksEnd==2&&!out.captureAttemptedMask);
    std::ostringstream present,undrained;first.Write(present,true);first.Write(undrained,false);
    CHECK(present.str().find("\"reason\":\"no_scoped_clear_calls\"")!=std::string::npos);
    CHECK(present.str().find("\"source_slot\":1,\"source_slot_count\":2")!=std::string::npos);
    CHECK(present.str().find("\"capture_attempted_mask\":0,\"capture_accepted_mask\":0")!=std::string::npos);
    CHECK(undrained.str().find("\"available\":false")!=std::string::npos&&undrained.str().find("\"lease\"")==std::string::npos);
    return 0;
}
int DepthEyeFiltersAndFailureProvenance() {
    DepthEyeFailureRow row;row.scopedHooks=3;
    DepthClearSample shadow;shadow.width=1024;shadow.height=1024;shadow.format=44;shadow.samples=1;shadow.hasDescriptor=true;
    row.Filter(DepthClearFilter::Dimensions,shadow);
    DepthClearSample other=shadow;other.width=2784;other.height=3024;other.format=40;
    row.Filter(DepthClearFilter::Format,other);row.Filter(DepthClearFilter::Dimensions,shadow);
    CHECK(row.firstFilter==DepthClearFilter::Dimensions&&row.firstIgnored.width==1024);
    CHECK(row.filters[unsigned(DepthClearFilter::Dimensions)]==2&&row.filters[unsigned(DepthClearFilter::Format)]==1);
    CHECK(std::string(row.FailureClass())=="no_matching_clear");
    row.matchingHooks=2;row.invalidCause=DepthClearFailure::DepthValue;row.invalidClear.depthBits=0x7fc00000;
    CHECK(std::string(row.FailureClass())=="clear_depth_value");
    row.invalidCause=DepthClearFailure::WorldCapture;row.captureAttemptedMask=1;row.captureAcceptedMask=0;
    CHECK(std::string(row.FailureClass())=="world_capture_rejected");
    row.invalidCause=DepthClearFailure::None;
    for(unsigned i=0;i<6;++i)CHECK(row.Test(i,true));CHECK(!row.Test(6,false));
    row.captureAttemptedMask=3;row.captureAcceptedMask=1;
    CHECK(std::string(row.FailureClass())=="final_capture_rejected"&&row.evaluated==127&&row.passed==63);
    FirstDepthEyeFailure first;first.Observe(row);std::ostringstream json;first.Write(json,true);
    CHECK(json.str().find("\"depth_float_bits\":2143289344")!=std::string::npos); // NaN is stored as bits, valid JSON.
    CHECK(json.str().find("\"capture_attempted_mask\":3,\"capture_accepted_mask\":1")!=std::string::npos);
    return 0;
}

int ConcurrentSnapshots() {
    FrameBridgeDiagnostics d;std::atomic<bool> finished=false,bad=false;
    std::thread writer([&]{for(unsigned i=1;i<=1000;++i){const auto k=Key(i);d.Begin(k,64,48,i*10);d.Ticket(k,Ticket(k));d.Cancel(k,i*10+1);}finished=true;});
    FrameBridgeTimingSnapshot s;
    while(!finished.load())if(d.Snapshot(s))for(unsigned i=0;i<s.count;++i){const auto& r=s.rows[i];
        if(r.key.frame!=r.key.request+100||(r.ticket.sequence&&r.ticket.frameId!=r.key.frame))bad=true;}
    writer.join();CHECK(!bad&&d.Snapshot(s)&&s.count<=FrameBridgeTimingCapacity);
    // Diagnostic losses are allowed and counted; writers never wait for readers.
    CHECK(s.completed<=1000);return 0;
}
}
int main(){CHECK(Lifecycle()==0);CHECK(ExactOwnershipAndMissingBoundaries()==0);CHECK(BoundedRetention()==0);
    CHECK(DeferredRetirementLifecycle()==0);CHECK(DeferredRetirementExactOwnership()==0);CHECK(DeferredRetirementBoundedRetention()==0);
    CHECK(FirstNonConsumedSurvivesWrap()==0);CHECK(ConcurrentSnapshots()==0);
    CHECK(FirstDepthEyeObservation()==0);CHECK(DepthEyeFiltersAndFailureProvenance()==0);return 0;}
