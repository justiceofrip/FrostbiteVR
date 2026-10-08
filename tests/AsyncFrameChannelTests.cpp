#include "Test.h"
#include "fvr/ipc/RemoteFrameProvider.h"
#include <Windows.h>
using namespace fvr;
void Ticket(const ipc::FrameRequest& request,graphics::TextureDescriptor& d,graphics::PairTicket& t){
    d={};d.width=request.width;d.height=request.height;d.format=request.format;d.adapterLow=request.adapterLow;d.adapterHigh=request.adapterHigh;d.resourceEpoch=1;d.session[0]=1;
    t={};t.session=d.session;t.resourceEpoch=d.resourceEpoch;t.sequence=request.requestId;t.frameId=request.requestId;
    t.spaceGeneration=request.spaceGeneration;t.trackingGeneration=request.trackingGeneration;t.predictedNs=request.predictedNs;
}
int main(){
    ipc::RemoteFrameProvider host;CHECK(host.Create());CHECK(host.SetBudget(50));ipc::FrameChannel producer;CHECK(producer.ConnectProducer(host.Token()));
    runtime::PresentationRequirements req{100,120,29,19,4};runtime::TrackingFrame now{};
    now.generation=0x100000005;now.spaceGeneration=0x200000007;now.predictedNs=1000000000;now.focused=now.headValid=true;
    now.eyes[0].position.x=-.032f;now.eyes[1].position.x=.032f;now.fov={math::FovTangents{-1.2f,.8f,1,-1},math::FovTangents{-.8f,1.2f,1,-1}};
    runtime::TrackingFrame rendered;graphics::TextureDescriptor d;graphics::PairTicket ticket;ipc::FrameRequest request;
    CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    // Controls update while the immutable render request stays in flight.
    interaction::InputFrame input{};input.generation=1;input.spaceGeneration=now.spaceGeneration;input.predictedNs=now.predictedNs;input.focused=input.headValid=true;
    input.hands[1].active=interaction::Trigger;input.hands[1].trigger=.9f;host.UpdateInput(input);
    std::int64_t inputDeadline=0;interaction::InputFrame controls{};CHECK(producer.ReadInput(controls,&inputDeadline)==ipc::ChannelResult::Ok&&controls.hands[1].trigger==.9f);
    CHECK(inputDeadline>0);const auto originalDeadline=inputDeadline;CHECK(producer.ReadInput(controls,&inputDeadline)==ipc::ChannelResult::Ok&&inputDeadline==originalDeadline);
    input.focused=false;input.hands={};++input.generation;host.UpdateInput(input);
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
    CHECK(producer.Skip()==ipc::ChannelResult::Ok);CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));
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
    delayed.PairConsumed(ticket,true);CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);
    // A genuinely expired request still rejects its completed image and releases
    // producer ownership; extending the async lifetime did not remove cancellation.
    CHECK(delayed.SetRequestLifetime(10));CHECK(!delayed.TryGetCompletedPair(req,now,rendered,d,ticket));
    CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);Sleep(20);
    CHECK(!delayed.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(delayed.Statistics().timeouts==1);
    Ticket(request,source,ready);CHECK(producer.Publish(source,ready)!=ipc::ChannelResult::Ok);
    // Stale controls expire independently; no frame request is needed to release them.
    input.generation=99;delayed.UpdateInput(input);CHECK(producer.ReadInput(controls)==ipc::ChannelResult::Ok);
    Sleep(120);CHECK(producer.ReadInput(controls,&inputDeadline)==ipc::ChannelResult::Timeout&&controls.generation==0&&inputDeadline==0);
    delayed.Close();CHECK(producer.ReadInput(controls)==ipc::ChannelResult::Closed);producer.Close();return 0;
}
