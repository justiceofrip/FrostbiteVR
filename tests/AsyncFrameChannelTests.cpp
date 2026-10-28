#include "Test.h"
#include "fvr/ipc/RemoteFrameProvider.h"
#include "fvr/ipc/WaitForConnection.h"
#include <Windows.h>
#include <sstream>
#include <string>
#include <cstring>
using namespace fvr;
void Ticket(const ipc::FrameRequest& request,graphics::TextureDescriptor& d,graphics::PairTicket& t){
    d={};d.width=request.width;d.height=request.height;d.format=request.format;d.adapterLow=request.adapterLow;d.adapterHigh=request.adapterHigh;d.resourceEpoch=1;d.session[0]=1;
    t={};t.session=d.session;t.resourceEpoch=d.resourceEpoch;t.sequence=request.requestId;t.frameId=request.requestId;
    t.spaceGeneration=request.spaceGeneration;t.trackingGeneration=request.trackingGeneration;t.predictedNs=request.predictedNs;
}
int LookaheadProvider(bool overlap=false){
    const runtime::PresentationRequirements req{100,120,29,19,4};runtime::TrackingFrame a{};
    a.generation=11;a.spaceGeneration=7;a.predictedNs=1000000000;a.focused=a.headValid=true;
    a.eyes[0].position.x=-.032f;a.eyes[1].position.x=.032f;
    a.fov={math::FovTangents{-1.2f,.8f,1,-1},math::FovTangents{-.8f,1.2f,1,-1}};
    auto b=a;++b.generation;b.predictedNs+=13888889;b.head.position.x=.2f;
    auto c=b;++c.generation;c.predictedNs+=8333333;c.head.position.z=.4f;
    const auto make=[](const ipc::FrameRequest& r,const ipc::PipelineSlotKey& key,graphics::TextureDescriptor& d,graphics::PairTicket& t){
        Ticket(r,d,t);d.sharing=graphics::TextureSharing::LegacyFenced;
        const std::array<std::uint64_t,2> resources{100+key.slot*10ull,101+key.slot*10ull};
        std::memcpy(d.session.data(),resources.data(),sizeof(resources));t.session=d.session;
    };
    const auto retire=[](ipc::FrameChannel& producer,const ipc::PipelineSlotKey& key,ipc::Outcome expected){
        ipc::PipelineRetirement retirement{};
        return producer.ClaimPipelineRetirement(key,retirement)==ipc::ChannelResult::Ok&&retirement.outcome==expected&&
            producer.FinishPipelineRetirement(retirement)==ipc::ChannelResult::Ok;
    };
    ipc::RemoteFrameProvider host;CHECK(!host.Create(false,true));CHECK(!host.Create(true,false,true));
    CHECK(host.Create(true,true,overlap)&&host.LookaheadEnabled()&&host.ExportOverlapEnabled()==overlap);
    ipc::FrameChannel producer;CHECK(producer.ConnectProducer(host.Token(),true));
    ipc::FrameRequest ra{},rb{},rc{};ipc::PipelineSlotKey ka{},kb{},kc{};
    graphics::TextureDescriptor da{},db{},dc{},out{};graphics::PairTicket ta{},tb{},tc{},got{};runtime::TrackingFrame rendered{};
    host.ServiceRequests(req,a);CHECK(host.Statistics().requested==1);
    CHECK(producer.TryTakePipeline(ra,ka)==ipc::ChannelResult::Ok);
    host.ServiceRequests(req,b);host.ServiceRequests(req,b);CHECK(host.Statistics().requested==2);
    CHECK(producer.TryTakePipeline(rb,kb)==ipc::ChannelResult::Busy); // One native camera render.
    if(overlap){
        CHECK(producer.MarkCaptureReleased(ka)==ipc::ChannelResult::Ok);
        CHECK(producer.TryTakePipeline(rb,kb)==ipc::ChannelResult::Ok&&kb.slot!=ka.slot);
        CHECK(!host.TryGetCompletedPair(req,b,rendered,out,got)&&host.Statistics().completed==0);
    }
    make(ra,ka,da,ta);CHECK(producer.PublishPipeline(ka,da,ta,nullptr,nullptr)==ipc::ChannelResult::Ok);
    if(!overlap)CHECK(producer.TryTakePipeline(rb,kb)==ipc::ChannelResult::Ok&&kb.slot!=ka.slot); // A still undelivered.
    CHECK(host.TryGetCompletedPair(req,b,rendered,out,got)&&rendered.generation==a.generation&&runtime::PairMatchesFrame(req,rendered,out,got));
    CHECK(rendered.head.position.x==a.head.position.x&&host.Statistics().requested==2);
    make(rb,kb,db,tb);CHECK(producer.PublishPipeline(kb,db,tb,nullptr,nullptr)==ipc::ChannelResult::Ok);
    CHECK(!host.TryGetCompletedPair(req,b,rendered,out,got)); // A's held images are not implicitly released.
    host.ServiceRequests(req,c);CHECK(host.Statistics().requested==3&&host.Statistics().maxPendingRequests==2);
    CHECK(producer.TryTakePipeline(rc,kc)==ipc::ChannelResult::Busy); // Metadata alone cannot allocate a third source.
    host.PairConsumed(ta,true);CHECK(retire(producer,ka,ipc::Outcome::Consumed));
    CHECK(producer.TryTakePipeline(rc,kc)==ipc::ChannelResult::Ok&&kc.slot==ka.slot&&rc.trackingGeneration==c.generation);
    CHECK(host.TryGetCompletedPair(req,c,rendered,out,got)&&rendered.generation==b.generation&&rendered.predictedNs==b.predictedNs);
    CHECK(rendered.head.position.x==b.head.position.x&&runtime::PairMatchesFrame(req,rendered,out,got));
    host.Suspend();make(rc,kc,dc,tc);CHECK(producer.PublishPipeline(kc,dc,tc,nullptr,nullptr)==ipc::ChannelResult::Timeout);
    CHECK(retire(producer,kc,ipc::Outcome::Discarded));host.PairConsumed(tb,true);CHECK(retire(producer,kb,ipc::Outcome::Consumed));
    host.Close();producer.Close();

    // The oldest timeout is observed once; a later ready pose remains associated
    // with its own pixels. Request cadence is not assumed to be 90 Hz.
    CHECK(host.Create(true,true,overlap));CHECK(producer.ConnectProducer(host.Token(),true));CHECK(host.SetRequestLifetime(1));
    host.ServiceRequests(req,a);CHECK(host.SetRequestLifetime(200));host.ServiceRequests(req,b);Sleep(10);
    CHECK(producer.TryTakePipeline(rb,kb)==ipc::ChannelResult::Busy); // Retire expired A before a later admission.
    CHECK(producer.TryTakePipeline(rb,kb)==ipc::ChannelResult::Ok&&rb.trackingGeneration==b.generation);
    make(rb,kb,db,tb);CHECK(producer.PublishPipeline(kb,db,tb,nullptr,nullptr)==ipc::ChannelResult::Ok);
    CHECK(!host.TryGetCompletedPair(req,b,rendered,out,got));
    CHECK(host.Statistics().timeouts==1&&host.Statistics().firstTimeout.requestOrdinal==1&&
        host.Statistics().firstTimeout.requestedTracking.generation==a.generation&&host.Statistics().firstTimeout.lifetimeMs==1);
    CHECK(host.Statistics().timeoutHistoryCount==1&&host.Statistics().timeoutHistoryOverflow==0);
    const auto& timedOut=host.Statistics().timeoutHistory[0];
    CHECK(timedOut.request.requestOrdinal==1&&timedOut.request.clockValid&&timedOut.channel.available&&timedOut.channel.wireVersion==9);
    CHECK(timedOut.channel.requestedKey.request==1&&timedOut.channel.clockValid&&timedOut.channel.nowQpc>0);
    CHECK(host.TryGetCompletedPair(req,b,rendered,out,got)&&rendered.generation==b.generation&&runtime::PairMatchesFrame(req,rendered,out,got));
    CHECK(host.Statistics().requested==2&&host.Statistics().completed==1&&host.Statistics().timeouts==1);
    host.PairConsumed(tb,true);CHECK(retire(producer,kb,ipc::Outcome::Consumed));host.Close();producer.Close();

    // A recenter invalidates both queued poses before any new-space request.
    CHECK(host.Create(true,true,overlap));CHECK(producer.ConnectProducer(host.Token(),true));
    host.ServiceRequests(req,a);host.ServiceRequests(req,b);auto newer=c;++newer.spaceGeneration;
    host.ServiceRequests(req,newer);CHECK(producer.TryTakePipeline(rc,kc)==ipc::ChannelResult::Ok&&rc.spaceGeneration==newer.spaceGeneration);
    CHECK(host.Statistics().requested==3&&host.Statistics().timeouts==0);host.Suspend();
    CHECK(producer.SkipPipeline(kc)==ipc::ChannelResult::Ok&&retire(producer,kc,ipc::Outcome::Discarded));
    host.Close();producer.Close();return 0;
}

int ThreeSourcePostDeliveryRefill(){
    const runtime::PresentationRequirements req{100,120,29,19,4};runtime::TrackingFrame a{};
    a.generation=11;a.spaceGeneration=7;a.predictedNs=1000000000;a.focused=a.headValid=true;
    a.eyes[0].position.x=-.032f;a.eyes[1].position.x=.032f;
    a.fov={math::FovTangents{-1,1,1,-1},math::FovTangents{-1,1,1,-1}};
    auto b=a;++b.generation;b.predictedNs+=13888889;b.head.position.x=.2f;
    auto c=b;++c.generation;c.predictedNs+=8333333;c.head.position.z=.4f;
    const auto make=[](const ipc::FrameRequest& r,const ipc::PipelineSlotKey& k,graphics::TextureDescriptor& d,graphics::PairTicket& t){
        Ticket(r,d,t);d.sharing=graphics::TextureSharing::LegacyFenced;
        const std::array<std::uint64_t,2> ids{100+10ull*k.slot,101+10ull*k.slot};std::memcpy(d.session.data(),ids.data(),16);t.session=d.session;
    };
    for(bool third:{false,true}){
        ipc::RemoteFrameProvider host;CHECK(host.Create(true,true,true,third));ipc::FrameChannel producer;
        CHECK(producer.ConnectProducer(host.Token(),true,third));host.ServiceRequests(req,a);host.ServiceRequests(req,b);
        ipc::FrameRequest ra{},rb{},rc{};ipc::PipelineSlotKey ka{},kb{},kc{};
        CHECK(producer.TryTakePipeline(ra,ka)==ipc::ChannelResult::Ok&&producer.MarkCaptureReleased(ka)==ipc::ChannelResult::Ok);
        CHECK(producer.TryTakePipeline(rb,kb)==ipc::ChannelResult::Ok&&producer.MarkCaptureReleased(kb)==ipc::ChannelResult::Ok);
        graphics::TextureDescriptor da{},out{};graphics::PairTicket ta{},got{};runtime::TrackingFrame rendered{};
        make(ra,ka,da,ta);CHECK(producer.PublishPipeline(ka,da,ta,nullptr,nullptr)==ipc::ChannelResult::Ok);
        host.ServiceRequests(req,c);CHECK(host.Statistics().requested==2); // Full pre-delivery FIFO.
        LARGE_INTEGER before{},after{},hz{};CHECK(QueryPerformanceCounter(&before)&&QueryPerformanceFrequency(&hz));
        CHECK(host.TryGetCompletedPair(req,c,rendered,out,got)&&rendered.generation==a.generation&&rendered.predictedNs==a.predictedNs);
        CHECK(QueryPerformanceCounter(&after)&&runtime::PairMatchesFrame(req,rendered,out,got));
        CHECK(host.Statistics().requested==(third?3u:2u)&&host.Statistics().maxPendingRequests==2);
        runtime::SourceSlotIdentity source{};CHECK(host.ReadSourceSlot(ta,source)&&source.slotCount==(third?3u:2u));
        if(third){
            CHECK(producer.TryTakePipeline(rc,kc)==ipc::ChannelResult::Ok&&kc.slot==2&&rc.trackingGeneration==c.generation);
            runtime::PresentationRequirements decoded{};runtime::TrackingFrame pose{};CHECK(ipc::Decode(rc,decoded,pose));
            CHECK(pose.predictedNs==c.predictedNs&&pose.head.position.z==c.head.position.z);
            const auto duration=hz.QuadPart/1000*150+(hz.QuadPart%1000)*150/1000;
            CHECK(before.QuadPart+duration<=rc.deadlineQpc&&rc.deadlineQpc<=after.QuadPart+duration);
            CHECK(host.SetRequestLifetime(200));auto duplicate=c;duplicate.head.position.z=99;
            host.ServiceRequests(req,duplicate);CHECK(host.Statistics().requested==3);
            CHECK(!host.TryGetCompletedPair(req,c,rendered,out,got)); // Still exactly one Delivered lease.
            host.Suspend();CHECK(producer.SkipPipeline(kc)==ipc::ChannelResult::Ok);
        }
        host.Close();producer.Close();
    }
    // Timeout never takes the refill branch or reassociates the original B.
    ipc::RemoteFrameProvider host;CHECK(host.Create(true,true,true,true));ipc::FrameChannel producer;
    CHECK(producer.ConnectProducer(host.Token(),true,true)&&host.SetRequestLifetime(1));host.ServiceRequests(req,a);
    CHECK(host.SetRequestLifetime(200));host.ServiceRequests(req,b);Sleep(10);
    ipc::FrameRequest rb{};ipc::PipelineSlotKey kb{};
    CHECK(producer.TryTakePipeline(rb,kb)==ipc::ChannelResult::Busy);
    CHECK(producer.TryTakePipeline(rb,kb)==ipc::ChannelResult::Ok&&rb.trackingGeneration==b.generation);
    graphics::TextureDescriptor bd{},out{};graphics::PairTicket bt{},got{};runtime::TrackingFrame rendered{};
    make(rb,kb,bd,bt);CHECK(producer.PublishPipeline(kb,bd,bt,nullptr,nullptr)==ipc::ChannelResult::Ok);
    CHECK(!host.TryGetCompletedPair(req,c,rendered,out,got)&&host.Statistics().requested==2);
    CHECK(host.Statistics().timeouts==1&&host.Statistics().firstTimeout.requestOrdinal==1&&host.Statistics().firstTimeout.currentTracking.generation==c.generation);
    CHECK(host.Statistics().timeoutHistoryCount==1&&host.Statistics().timeoutHistory[0].channel.available&&
        host.Statistics().timeoutHistory[0].channel.wireVersion==10&&host.Statistics().timeoutHistory[0].channel.requestedKey.request==1);
    CHECK(host.TryGetCompletedPair(req,b,rendered,out,got)&&rendered.generation==b.generation&&rendered.predictedNs==b.predictedNs);
    CHECK(host.Statistics().requested==2&&host.Statistics().completed==1);host.Close();producer.Close();return 0;
}

int TimeoutHistoryRetention(){
    ipc::AsyncDeliveryStats stats{};
    for(unsigned i=0;i<19;++i){
        ipc::AsyncRequestTimeout row{};row.observed=true;row.requestOrdinal=i+1;row.currentTracking.generation=100+i;
        ipc::FrameTimeoutSnapshot channel{};channel.available=true;channel.wireVersion=10;
        channel.requestedKey={900+i,2,0};channel.reason=ipc::FrameTimeoutReason::SlotDeadlineOrTerminal;
        channel.phaseValid=true;channel.phase=ipc::Phase::Rendering;
        stats.RetainTimeout(row,channel);
        // Caller values can change without rewriting an earlier retained identity.
        row.requestOrdinal=0;channel.requestedKey={};
    }
    CHECK(stats.timeoutHistoryCount==16&&stats.timeoutHistoryOverflow==3&&stats.firstTimeout.requestOrdinal==1);
    for(unsigned i=0;i<16;++i)CHECK(stats.timeoutHistory[i].request.requestOrdinal==i+1&&
        stats.timeoutHistory[i].channel.requestedKey.request==900+i&&stats.timeoutHistory[i].request.currentTracking.generation==100+i);
    std::ostringstream json;stats.WriteTimeoutHistory(json);const auto encoded=json.str();
    CHECK(encoded.find("\"capacity\":16,\"retained\":16,\"overflow\":3")!=std::string::npos&&
        encoded.find("\"requested_id\":900,\"requested_slot\":2")!=std::string::npos&&
        encoded.find("\"requested_id\":915,")!=std::string::npos&&encoded.find("\"requested_id\":916,")==std::string::npos);
    CHECK(encoded.find("\"clock_valid\":false")!=std::string::npos&&encoded.find("\"request_ordinal_is_ipc_id\":false")!=std::string::npos);
    stats={};CHECK(stats.timeoutHistoryCount==0&&stats.timeoutHistoryOverflow==0&&!stats.firstTimeout.observed);
    return 0;
}
int main(){
    CHECK(!TimeoutHistoryRetention());
    CHECK(!ThreeSourcePostDeliveryRefill());
    CHECK(!LookaheadProvider());
    CHECK(!LookaheadProvider(true));
    // A successful peer observation must not be polled a second time and
    // relabeled as timeout when that immediately following poll would be Busy.
    unsigned calls=0,pauses=0;std::uint64_t tick=100;
    CHECK(ipc::WaitForConnection([&]{return ++calls==2;},[&]{return tick;},[&]{++pauses;++tick;},100,10));
    CHECK(calls==2&&pauses==1);
    calls=pauses=0;tick=100;
    CHECK(!ipc::WaitForConnection([&]{++calls;return false;},[&]{return tick;},[&]{++pauses;++tick;},100,3));
    CHECK(calls==3&&pauses==3);
    CHECK(!ipc::WaitForConnection([&]{++calls;return true;},[]{return 99ull;},[]{},100,3));
    CHECK(calls==3);
    ipc::RemoteFrameProvider host;CHECK(host.Create());CHECK(host.SetBudget(50));ipc::FrameChannel producer;CHECK(producer.ConnectProducer(host.Token()));
    runtime::PresentationRequirements req{100,120,29,19,4};runtime::TrackingFrame now{};
    now.generation=0x100000005;now.spaceGeneration=0x200000007;now.predictedNs=1000000000;now.focused=now.headValid=true;
    now.eyes[0].position.x=-.032f;now.eyes[1].position.x=.032f;now.fov={math::FovTangents{-1.2f,.8f,1,-1},math::FovTangents{-.8f,1.2f,1,-1}};
    runtime::TrackingFrame rendered;graphics::TextureDescriptor d;graphics::PairTicket ticket;ipc::FrameRequest request;
    CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    // Controls update while the immutable render request stays in flight.
    interaction::InputFrame input{};input.generation=1;input.spaceGeneration=now.spaceGeneration;input.predictedNs=now.predictedNs;input.focused=input.headValid=true;
    input.hands[1].active=interaction::Trigger;input.hands[1].trigger=.9f;
    const auto accepted=host.UpdateInputObserved(input);
    CHECK(accepted.known&&accepted.succeeded&&accepted.code==std::uint32_t(ipc::ChannelResult::Ok));
    std::int64_t inputDeadline=0;interaction::InputFrame controls{};CHECK(producer.ReadInput(controls,&inputDeadline)==ipc::ChannelResult::Ok&&controls.hands[1].trigger==.9f);
    CHECK(inputDeadline>0);const auto originalDeadline=inputDeadline;CHECK(producer.ReadInput(controls,&inputDeadline)==ipc::ChannelResult::Ok&&inputDeadline==originalDeadline);
    // The real transport result must remain known; diagnostic observation
    // cannot republish a duplicate generation or extend the accepted lease.
    auto duplicate=input;duplicate.hands[1].trigger=.1f;
    const auto rejected=host.UpdateInputObserved(duplicate);
    CHECK(rejected.known&&!rejected.succeeded&&rejected.code==std::uint32_t(ipc::ChannelResult::Invalid));
    CHECK(producer.ReadInput(controls,&inputDeadline)==ipc::ChannelResult::Ok&&controls.generation==input.generation&&
        controls.hands[1].trigger==.9f&&inputDeadline==originalDeadline);
    input.focused=false;input.hands={};++input.generation;
    const auto released=host.UpdateInputObserved(input);
    CHECK(released.known&&released.succeeded&&released.code==std::uint32_t(ipc::ChannelResult::Ok));
    CHECK(producer.ReadInput(controls)==ipc::ChannelResult::Ok&&!controls.focused&&!controls.hands[1].trigger);
    const auto original=now;++now.generation;now.predictedNs+=11000000;now.eyes[0].position.x+=.1f;
    CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket)); // No CPU wait or request replacement.
    graphics::TextureDescriptor source;graphics::PairTicket ready;Ticket(request,source,ready);CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);
    CHECK(host.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(rendered.generation==original.generation);
    CHECK(rendered.eyes[0].position.x==original.eyes[0].position.x);CHECK(runtime::PairMatchesFrame(req,rendered,d,ticket));
    CHECK(!runtime::PairMatchesFrame(req,now,d,ticket)); // The new head pose cannot label old pixels.
    Sleep(60);CHECK(producer.PollOutcome()==ipc::Outcome::Pending);host.PairConsumed(ticket,true);CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);
    CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    // Recenter while the producer is rendering cancels without publishing stale views.
    ++now.spaceGeneration;CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));Ticket(request,source,ready);
    CHECK(producer.Publish(source,ready)!=ipc::ChannelResult::Ok);
    CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    CHECK(request.spaceGeneration==now.spaceGeneration);Ticket(request,source,ready);CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);
    host.Suspend();CHECK(producer.PollOutcome()==ipc::Outcome::Discarded);
    CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    CHECK(!host.Statistics().firstTimeout.observed);
    const auto skippedOrdinal=host.Statistics().requested;
    CHECK(producer.Skip()==ipc::ChannelResult::Ok);CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));
    // A producer skip also returns Timeout. Preserve the observation without
    // relabeling it a proven elapsed-deadline or GPU failure.
    CHECK(host.Statistics().firstTimeout.observed&&host.Statistics().firstTimeout.requestOrdinal==skippedOrdinal);
    CHECK(host.Statistics().firstTimeout.requestedTracking.generation==now.generation);
    host.Suspend();producer.Close();CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));
    // Split API never cancels a delivered GPU pair on deadline or CancelRequest.
    ipc::FrameChannel channel;CHECK(channel.CreateHost());CHECK(producer.ConnectProducer(channel.Token()));
    CHECK(channel.BeginRequest(req,now,50)==ipc::ChannelResult::Ok);CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);Ticket(request,source,ready);
    CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);CHECK(channel.PollRequest(d,ticket)==ipc::ChannelResult::Ok);
    CHECK(channel.CancelRequest()==ipc::ChannelResult::Busy);Sleep(60);CHECK(channel.PollRequest(d,ticket)==ipc::ChannelResult::Busy);
    CHECK(producer.PollOutcome()==ipc::Outcome::Pending);CHECK(channel.Feedback(ticket,false)==ipc::ChannelResult::Ok);CHECK(producer.PollOutcome()==ipc::Outcome::Discarded);
    CHECK(channel.BeginRequest(req,now,1)==ipc::ChannelResult::Ok);Sleep(10);CHECK(channel.PollRequest(d,ticket)==ipc::ChannelResult::Timeout);
    CHECK(channel.BeginRequest(req,now,50)==ipc::ChannelResult::Ok);CHECK(channel.CancelRequest()==ipc::ChannelResult::Ok);
    CHECK(producer.TryTake(request)==ipc::ChannelResult::Busy);
    CHECK(channel.BeginRequest(req,now,0)==ipc::ChannelResult::Invalid);
    CHECK(channel.BeginRequest(req,now,201)==ipc::ChannelResult::Invalid);
    CHECK(channel.RequestPair(req,now,51,d,ticket)==ipc::ChannelResult::Invalid);
    CHECK(channel.BeginRequest(req,now,200)==ipc::ChannelResult::Ok);CHECK(channel.CancelRequest()==ipc::ChannelResult::Ok);
    producer.Close();channel.Close();
    // A native multi-frame handoff can exceed 50 ms at 60 Hz. A short blocking
    // budget must not kill this nonblocking request, or relabel its older pose.
    ipc::RemoteFrameProvider delayed;CHECK(delayed.Create());CHECK(delayed.SetBudget(1));
    CHECK(delayed.RequestLifetimeMs()==150);CHECK(!delayed.SetRequestLifetime(0));CHECK(!delayed.SetRequestLifetime(201));
    CHECK(producer.ConnectProducer(delayed.Token()));
    CHECK(!delayed.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    const auto requestedPose=now;Sleep(80);++now.generation;now.predictedNs+=88000000;now.eyes[0].position.x+=.2f;
    CHECK(!delayed.TryGetCompletedPair(req,now,rendered,d,ticket));
    Ticket(request,source,ready);CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);
    CHECK(delayed.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(runtime::PairMatchesFrame(req,rendered,d,ticket));
    CHECK(rendered.generation==requestedPose.generation&&rendered.eyes[0].position.x==requestedPose.eyes[0].position.x);
    CHECK(!runtime::PairMatchesFrame(req,now,d,ticket));
    CHECK(delayed.Statistics().completedAfter50Ms==1&&delayed.Statistics().completed==1&&delayed.Statistics().timeouts==0);
    CHECK(!delayed.Statistics().firstTimeout.observed);
    std::ostringstream noTimeout;delayed.Statistics().firstTimeout.Write(noTimeout);CHECK(noTimeout.str()=="null");
    delayed.PairConsumed(ticket,true);CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);
    // Successful delivery already queued the next immutable request with the
    // old lifetime. Cancel it explicitly before testing a different lifetime.
    delayed.Suspend();
    // A genuinely expired request still rejects its completed image and releases
    // producer ownership; extending the async lifetime did not remove cancellation.
    CHECK(delayed.SetRequestLifetime(10));CHECK(!delayed.TryGetCompletedPair(req,now,rendered,d,ticket));
    const auto timedOutPose=now;const auto timedOutOrdinal=delayed.Statistics().requested;
    CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    // Configuration can change for the NEXT request; this observation must
    // retain the ten-millisecond lifetime and original immutable pose.
    CHECK(delayed.SetRequestLifetime(200));Sleep(20);
    ++now.generation;now.predictedNs+=22000000;now.head.position.y+=.25f;
    CHECK(!delayed.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(delayed.Statistics().timeouts==1);
    const auto firstTimeout=delayed.Statistics().firstTimeout;
    CHECK(delayed.Statistics().timeoutHistoryCount==1&&!delayed.Statistics().timeoutHistory[0].channel.available);
    CHECK(firstTimeout.observed&&firstTimeout.clockValid&&!firstTimeout.queuedAtSubmission);
    CHECK(firstTimeout.requestOrdinal==timedOutOrdinal&&firstTimeout.lifetimeMs==10);
    CHECK(firstTimeout.ageNs>=10000000&&firstTimeout.ageNs==std::uint64_t(firstTimeout.observedSteadyNs-firstTimeout.startedSteadyNs));
    CHECK(firstTimeout.requirements.width==req.width&&firstTimeout.requirements.height==req.height&&
        firstTimeout.requirements.format==req.format&&firstTimeout.requirements.adapterLow==req.adapterLow&&firstTimeout.requirements.adapterHigh==req.adapterHigh);
    CHECK(firstTimeout.requestedTracking.generation==timedOutPose.generation&&firstTimeout.requestedTracking.spaceGeneration==timedOutPose.spaceGeneration&&
        firstTimeout.requestedTracking.predictedNs==timedOutPose.predictedNs&&firstTimeout.requestedTracking.head.position.y==timedOutPose.head.position.y&&
        firstTimeout.requestedTracking.eyes[0].position.x==timedOutPose.eyes[0].position.x);
    CHECK(firstTimeout.currentTracking.generation==now.generation&&firstTimeout.currentTracking.head.position.y==now.head.position.y);
    std::ostringstream timeoutJson;firstTimeout.Write(timeoutJson);const auto recordedTimeout=timeoutJson.str();
    CHECK(recordedTimeout.find("\"schema\":\"fvr.async-request-timeout.v1\"")!=std::string::npos&&
        recordedTimeout.find("\"cause\":\"unclassified\"")!=std::string::npos&&
        recordedTimeout.find("\"clock\":\"steady_clock_ns\"")!=std::string::npos);
    CHECK(recordedTimeout.find("\"request_ordinal_is_ipc_id\":false")!=std::string::npos&&
        recordedTimeout.find("\"queued_at_submission\":false,\"lifetime_ms\":10")!=std::string::npos&&
        recordedTimeout.find("\"clock_valid\":true")!=std::string::npos&&
        recordedTimeout.find("\"requirements\":{\"width\":100,\"height\":120,\"format\":29,\"adapter_luid_low\":19,\"adapter_luid_high\":4}")!=std::string::npos);
    CHECK(recordedTimeout.find("\"original_tracking\":{\"generation\":"+std::to_string(timedOutPose.generation)+",")!=std::string::npos&&
        recordedTimeout.find("\"current_tracking\":{\"generation\":"+std::to_string(now.generation)+",")!=std::string::npos&&
        recordedTimeout.find("\"focused\":true,\"head_valid\":true")!=std::string::npos);
    // Timeout cancelled a Rendering slot, so the host could not replace it
    // until the stale producer publication released that slot. Admit the next
    // request through the normal host API before asking the producer to take it.
    CHECK(delayed.Statistics().requested==timedOutOrdinal);
    Ticket(request,source,ready);CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Timeout);
    CHECK(!delayed.TryGetCompletedPair(req,now,rendered,d,ticket));
    CHECK(delayed.Statistics().requested==timedOutOrdinal+1&&delayed.Statistics().timeouts==1);
    CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);CHECK(producer.Skip()==ipc::ChannelResult::Ok);
    ++now.generation;now.predictedNs+=11000000;
    CHECK(!delayed.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(delayed.Statistics().timeouts==2);
    CHECK(delayed.Statistics().timeoutHistoryCount==2&&!delayed.Statistics().timeoutHistory[1].channel.available);
    CHECK(delayed.Statistics().timeoutHistory[1].request.requestOrdinal==timedOutOrdinal+1&&
        !delayed.Statistics().timeoutHistory[1].request.clockValid&&delayed.Statistics().timeoutHistory[1].request.observedSteadyNs==0);
    std::ostringstream historyJson;delayed.Statistics().WriteTimeoutHistory(historyJson);
    CHECK(historyJson.str().find("\"available\":false,\"reason\":\"snapshot_unavailable\"")!=std::string::npos);
    const auto& retainedTimeout=delayed.Statistics().firstTimeout;
    CHECK(retainedTimeout.requestOrdinal==firstTimeout.requestOrdinal&&retainedTimeout.observedSteadyNs==firstTimeout.observedSteadyNs&&
        retainedTimeout.ageNs==firstTimeout.ageNs&&retainedTimeout.lifetimeMs==10&&
        retainedTimeout.requestedTracking.generation==timedOutPose.generation&&retainedTimeout.currentTracking.generation==firstTimeout.currentTracking.generation);
    std::ostringstream retainedJson;retainedTimeout.Write(retainedJson);CHECK(retainedJson.str()==recordedTimeout);
    // Stale controls expire independently; no frame request is needed to release them.
    input.generation=99;delayed.UpdateInput(input);CHECK(producer.ReadInput(controls)==ipc::ChannelResult::Ok);
    Sleep(120);CHECK(producer.ReadInput(controls,&inputDeadline)==ipc::ChannelResult::Timeout&&controls.generation==0&&inputDeadline==0);
    delayed.Close();CHECK(producer.ReadInput(controls)==ipc::ChannelResult::Closed);producer.Close();
    CHECK(delayed.Create());CHECK(delayed.Statistics().timeouts==0&&!delayed.Statistics().firstTimeout.observed);
    CHECK(delayed.Statistics().timeoutHistoryCount==0&&delayed.Statistics().timeoutHistoryOverflow==0);
    // Cover the separate QueueRequest admission path without a timing wait:
    // skip its producer work and retain its original configured lifetime.
    CHECK(producer.ConnectProducer(delayed.Token()));CHECK(delayed.SetRequestLifetime(200));
    CHECK(!delayed.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    Ticket(request,source,ready);CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);
    CHECK(delayed.TryGetCompletedPair(req,now,rendered,d,ticket));
    const auto queuedOrdinal=delayed.Statistics().requested;const auto queuedPose=now;
    CHECK(delayed.SetRequestLifetime(10));delayed.PairConsumed(ticket,true);
    CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    CHECK(producer.Skip()==ipc::ChannelResult::Ok);++now.generation;now.predictedNs+=11000000;
    CHECK(!delayed.TryGetCompletedPair(req,now,rendered,d,ticket));
    CHECK(delayed.Statistics().firstTimeout.observed&&delayed.Statistics().firstTimeout.queuedAtSubmission&&
        delayed.Statistics().firstTimeout.requestOrdinal==queuedOrdinal&&delayed.Statistics().firstTimeout.lifetimeMs==200&&
        delayed.Statistics().firstTimeout.requestedTracking.generation==queuedPose.generation&&
        delayed.Statistics().firstTimeout.currentTracking.generation==now.generation);
    delayed.Close();producer.Close();return 0;
}
