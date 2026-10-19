#include "Test.h"
#include "fvr/ipc/PipelinedFrameProvider.h"
#include <Windows.h>
using namespace fvr;
// Two host frame channels behind one provider (BF4 --frame-channels 2), each answered by its
// own producer as the adapter's two bridges do.
static void Ticket(const ipc::FrameRequest& request,std::uint8_t session,graphics::TextureDescriptor& d,graphics::PairTicket& t){
    d={};d.width=request.width;d.height=request.height;d.format=request.format;d.adapterLow=request.adapterLow;d.adapterHigh=request.adapterHigh;d.resourceEpoch=1;d.session[0]=session;
    t={};t.session=d.session;t.resourceEpoch=d.resourceEpoch;t.sequence=request.requestId;t.frameId=request.requestId;
    t.spaceGeneration=request.spaceGeneration;t.trackingGeneration=request.trackingGeneration;t.predictedNs=request.predictedNs;
}
static runtime::TrackingFrame Next(runtime::TrackingFrame t){++t.generation;t.predictedNs+=11000000;return t;}
int main(){
    ipc::RemoteFrameProvider a,b;CHECK(a.Create());CHECK(b.Create());
    ipc::FrameChannel pa,pb;CHECK(pa.ConnectProducer(a.Token()));CHECK(pb.ConnectProducer(b.Token()));
    ipc::PipelinedFrameProvider p(a,b);
    runtime::PresentationRequirements req{100,120,29,19,4};runtime::TrackingFrame now{};
    now.generation=0x100000005;now.spaceGeneration=0x200000007;now.predictedNs=1000000000;now.focused=now.headValid=true;
    now.eyes[0].position.x=-.032f;now.eyes[1].position.x=.032f;now.fov={math::FovTangents{-1.2f,.8f,1,-1},math::FovTangents{-.8f,1.2f,1,-1}};
    runtime::TrackingFrame rendered;graphics::TextureDescriptor d,source;graphics::PairTicket ticket,ready;ipc::FrameRequest ra,rb;

    // One channel begins per host frame: the second begins on the next one.
    const auto first=now;
    CHECK(!p.TryGetCompletedPair(req,now,rendered,d,ticket));
    CHECK(a.Pending()&&!b.Pending());CHECK(pa.TryTake(ra)==ipc::ChannelResult::Ok);CHECK(pb.TryTake(rb)!=ipc::ChannelResult::Ok);
    now=Next(now);const auto second=now;
    CHECK(!p.TryGetCompletedPair(req,now,rendered,d,ticket));
    CHECK(b.Pending());CHECK(pb.TryTake(rb)==ipc::ChannelResult::Ok);
    CHECK(ra.trackingGeneration==first.generation&&rb.trackingGeneration==second.generation);

    // Both complete before the host's next frame: the older pair now, the newer one held
    // for the following frame (never dropped), each labelled with its own pose.
    Ticket(ra,1,source,ready);CHECK(pa.Publish(source,ready)==ipc::ChannelResult::Ok);
    Ticket(rb,2,source,ready);CHECK(pb.Publish(source,ready)==ipc::ChannelResult::Ok);
    now=Next(now);
    CHECK(p.TryGetCompletedPair(req,now,rendered,d,ticket));
    CHECK(rendered.generation==first.generation&&ticket.session[0]==1&&runtime::PairMatchesFrame(req,rendered,d,ticket));
    CHECK(p.HeldPairs()==1&&p.Delivered(0)==1&&p.Delivered(1)==0);
    p.PairConsumed(ticket,true); // offered to both channels; only its own accepts it
    CHECK(pa.PollOutcome()==ipc::Outcome::Consumed);CHECK(pb.PollOutcome()==ipc::Outcome::Pending);
    now=Next(now);const auto third=now;
    CHECK(p.TryGetCompletedPair(req,now,rendered,d,ticket));
    CHECK(rendered.generation==second.generation&&ticket.session[0]==2&&runtime::PairMatchesFrame(req,rendered,d,ticket));
    CHECK(p.Delivered(1)==1);
    // The idle first channel began again in that same call (the held pair's did not).
    CHECK(a.Pending());CHECK(pa.TryTake(ra)==ipc::ChannelResult::Ok&&ra.trackingGeneration==third.generation);
    p.PairConsumed(ticket,false);CHECK(pb.PollOutcome()==ipc::Outcome::Discarded);

    // Staggered requests: after a completion that took ~60 ms, a channel waits until the
    // other one's request is kStagger of the typical completion time old.
    Sleep(60);
    Ticket(ra,1,source,ready);CHECK(pa.Publish(source,ready)==ipc::ChannelResult::Ok);
    now=Next(now);
    CHECK(p.TryGetCompletedPair(req,now,rendered,d,ticket)&&ticket.session[0]==1);
    CHECK(p.TypicalLatencyUs()>8000.0);
    CHECK(b.Pending());CHECK(pb.TryTake(rb)==ipc::ChannelResult::Ok); // the other channel was idle: it began
    p.PairConsumed(ticket,true);CHECK(pa.PollOutcome()==ipc::Outcome::Consumed);
    now=Next(now);
    CHECK(!p.TryGetCompletedPair(req,now,rendered,d,ticket));
    CHECK(!a.Pending()); // the other request is still young
    Sleep(static_cast<DWORD>(ipc::PipelinedFrameProvider::kStagger*p.TypicalLatencyUs()/1000.0)+10);
    now=Next(now);
    CHECK(!p.TryGetCompletedPair(req,now,rendered,d,ticket));
    CHECK(a.Pending());CHECK(pa.TryTake(ra)==ipc::ChannelResult::Ok);

    // Suspend (menu, recenter) hands a held pair back to its channel as not consumed.
    Ticket(rb,2,source,ready);CHECK(pb.Publish(source,ready)==ipc::ChannelResult::Ok);
    Ticket(ra,1,source,ready);CHECK(pa.Publish(source,ready)==ipc::ChannelResult::Ok);
    now=Next(now);
    CHECK(p.TryGetCompletedPair(req,now,rendered,d,ticket));
    CHECK(ticket.session[0]==2); // b's request was the older one
    p.PairConsumed(ticket,true);CHECK(pb.PollOutcome()==ipc::Outcome::Consumed);
    p.Suspend();CHECK(pa.PollOutcome()==ipc::Outcome::Discarded);
    now=Next(now);
    // Nothing held is delivered after the suspension; requests start afresh.
    CHECK(!p.TryGetCompletedPair(req,now,rendered,d,ticket));

    // Controller input reaches the producer on the first channel only.
    interaction::InputFrame input{};input.generation=1;input.spaceGeneration=now.spaceGeneration;input.predictedNs=now.predictedNs;input.focused=input.headValid=true;
    input.hands[1].active=interaction::Trigger;input.hands[1].trigger=.7f;
    CHECK(p.UpdateInputObserved(input).succeeded);
    interaction::InputFrame controls{};
    CHECK(pa.ReadInput(controls)==ipc::ChannelResult::Ok&&controls.hands[1].trigger==.7f);
    CHECK(pb.ReadInput(controls)!=ipc::ChannelResult::Ok);
    p.Suspend();pa.Close();pb.Close();a.Close();b.Close();
    return 0;
}
