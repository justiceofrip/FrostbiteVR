#include "Test.h"
#include "fvr/ipc/RemoteFrameProvider.h"
#include <Windows.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <thread>

using namespace fvr;
namespace {
using Result=ipc::ChannelResult;
runtime::PresentationRequirements Requirements(){return {100,120,29,19,4};}
runtime::TrackingFrame Tracking(unsigned tick=0){
    runtime::TrackingFrame t{};t.generation=0x100000005ull+tick;t.spaceGeneration=0x200000007ull;
    t.predictedNs=1000000000ll+11000000ll*tick;t.focused=t.headValid=true;
    t.head.position.x=.01f*tick;t.referenceHead.position.y=1.6f;
    t.eyes[0].position.x=-.032f+.01f*tick;t.eyes[1].position.x=.032f+.01f*tick;
    t.fov={math::FovTangents{-1.2f,.8f,1,-1},math::FovTangents{-.8f,1.2f,1,-1}};
    return t;
}
void Pair(const ipc::FrameRequest& request,graphics::TextureDescriptor& d,graphics::PairTicket& t){
    d={};d.width=request.width;d.height=request.height;d.format=request.format;
    d.adapterLow=request.adapterLow;d.adapterHigh=request.adapterHigh;d.resourceEpoch=7;
    d.sharing=graphics::TextureSharing::LegacyFenced;
    const std::array<std::uint64_t,2> resources{1,2};std::memcpy(d.session.data(),resources.data(),16);
    t={};t.session=d.session;t.resourceEpoch=d.resourceEpoch;t.sequence=request.requestId;t.frameId=request.requestId;
    t.spaceGeneration=request.spaceGeneration;t.trackingGeneration=request.trackingGeneration;t.predictedNs=request.predictedNs;
}
graphics::BodyPropFrame Props(const graphics::PairTicket& t){
    graphics::BodyPropFrame p{};p.frameId=t.frameId;p.spaceGeneration=t.spaceGeneration;p.trackingGeneration=t.trackingGeneration;
    return p;
}
graphics::SceneDepthFrame Depth(const graphics::TextureDescriptor& d,const graphics::PairTicket& t){
    graphics::SceneDepthFrame depth{};depth.color=t;depth.flags=graphics::SceneDepthCanonicalClip;
    depth.width=d.width;depth.height=d.height;depth.adapterLow=d.adapterLow;depth.adapterHigh=d.adapterHigh;
    depth.format=44;depth.samples=1;depth.resourceGeneration=t.frameId;
    depth.resources={{{101,102},{103,104}}};return depth;
}
struct Fixture {
    ipc::FrameChannel host,producer;
    runtime::PresentationRequirements requirements=Requirements();
    ipc::FrameRequest request{};
    graphics::TextureDescriptor descriptor{};
    graphics::PairTicket ticket{};
    int Open(){CHECK(host.CreateHost());CHECK(producer.ConnectProducer(host.Token()));return 0;}
    int Deliver(unsigned tick=0){
        CHECK(host.BeginRequest(requirements,Tracking(tick),200)==Result::Ok);
        CHECK(producer.TryTake(request)==Result::Ok);return PublishAndDeliver();
    }
    int PublishAndDeliver(){
        Pair(request,descriptor,ticket);const auto props=Props(ticket);const auto depth=Depth(descriptor,ticket);
        CHECK(producer.Publish(descriptor,ticket,&props,&depth)==Result::Ok);
        CHECK(host.PollRequest(descriptor,ticket)==Result::Ok);return 0;
    }
};
int ConsecutiveProviderTicks(){
    ipc::RemoteFrameProvider host;CHECK(host.Create());CHECK(host.SetRequestLifetime(200));
    ipc::FrameChannel producer;CHECK(producer.ConnectProducer(host.Token()));
    const auto requirements=Requirements();runtime::TrackingFrame rendered{};
    graphics::TextureDescriptor d{};graphics::PairTicket ticket{};unsigned delivered=0;
    for(unsigned tick=0;tick<120;++tick){
        const auto current=Tracking(tick);
        const bool got=host.TryGetCompletedPair(requirements,current,rendered,d,ticket);
        CHECK(got==(tick!=0));
        if(got){
            ++delivered;const auto expected=Tracking(tick-1);
            CHECK(rendered.generation==expected.generation&&rendered.spaceGeneration==expected.spaceGeneration);
            CHECK(rendered.predictedNs==expected.predictedNs&&rendered.head.position.x==expected.head.position.x);
            CHECK(rendered.eyes[0].position.x==expected.eyes[0].position.x);
            CHECK(runtime::PairMatchesFrame(requirements,rendered,d,ticket));
            CHECK(!runtime::PairMatchesFrame(requirements,current,d,ticket));
            graphics::BodyPropFrame props;graphics::SceneDepthFrame depth;
            CHECK(host.ReadBodyProps(ticket,props)&&props.trackingGeneration==expected.generation);
            CHECK(host.ReadSceneDepth(d,ticket,depth)&&depth.color.trackingGeneration==expected.generation);
            // Merely queuing the successor does not free the delivered pixels.
            ipc::FrameRequest blocked;CHECK(producer.TryTake(blocked)==Result::Busy);
            CHECK(producer.PollOutcome()==ipc::Outcome::Pending);
            host.PairConsumed(ticket,true);CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);
            CHECK(!host.ReadSceneDepth(d,ticket,depth));
        }
        // No extra host call: feedback/reap alone makes this tick's queued pose available.
        ipc::FrameRequest request{};CHECK(producer.TryTake(request)==Result::Ok);
        CHECK(request.trackingGeneration==current.generation&&request.predictedNs==current.predictedNs);
        runtime::TrackingFrame decoded{};runtime::PresentationRequirements decodedRequirements{};
        CHECK(ipc::Decode(request,decodedRequirements,decoded));
        CHECK(decoded.eyes[1].position.x==current.eyes[1].position.x);
        Pair(request,d,ticket);const auto props=Props(ticket);const auto depth=Depth(d,ticket);
        CHECK(producer.Publish(d,ticket,&props,&depth)==Result::Ok);
    }
    CHECK(delivered==119&&host.Statistics().completed==119&&host.Statistics().requested==120);
    CHECK(host.Statistics().timeouts==0);
    host.Suspend();CHECK(producer.PollOutcome()==ipc::Outcome::Discarded);
    ipc::FrameRequest request;CHECK(producer.TryTake(request)==Result::Busy);
    return 0;
}
int QueuedOwnershipAndIdentity(){
    Fixture f;CHECK(!f.Open());CHECK(f.host.QueueRequest(f.requirements,Tracking(),200)==Result::Busy);
    CHECK(!f.Deliver());const auto first=f.ticket;
    CHECK(f.host.QueueRequest(f.requirements,Tracking(1),0)==Result::Invalid);
    CHECK(f.host.QueueRequest(f.requirements,Tracking(1),201)==Result::Invalid);
    auto invalid=Tracking(1);invalid.headValid=false;
    CHECK(f.host.QueueRequest(f.requirements,invalid,200)==Result::Invalid);
    CHECK(f.host.QueueRequest(f.requirements,Tracking(1),200)==Result::Ok);
    CHECK(f.host.QueueRequest(f.requirements,Tracking(2),200)==Result::Busy);
    CHECK(f.host.BeginRequest(f.requirements,Tracking(2),200)==Result::Busy);
    CHECK(f.host.RequestPair(f.requirements,Tracking(2),1,f.descriptor,f.ticket)==Result::Busy);
    CHECK(f.host.PollRequest(f.descriptor,f.ticket)==Result::Busy);
    CHECK(f.producer.TryTake(f.request)==Result::Busy&&f.producer.PollOutcome()==ipc::Outcome::Pending);
    auto wrong=first;++wrong.frameId;CHECK(f.host.Feedback(wrong,true)==Result::Invalid);
    CHECK(f.producer.PollOutcome()==ipc::Outcome::Pending);
    CHECK(f.host.Feedback(first,true)==Result::Ok);
    CHECK(f.host.Feedback(first,false)==Result::Invalid);
    // Complete is still producer-owned; taking before Reap must not overwrite it.
    CHECK(f.host.PollRequest(f.descriptor,f.ticket)==Result::Busy);
    CHECK(f.producer.TryTake(f.request)==Result::Busy);
    CHECK(f.producer.PollOutcome()==ipc::Outcome::Consumed);
    CHECK(f.host.PollRequest(f.descriptor,f.ticket)==Result::Busy);
    CHECK(f.producer.TryTake(f.request)==Result::Ok);
    CHECK(f.request.requestId==2&&f.request.trackingGeneration==Tracking(1).generation);
    CHECK(!f.PublishAndDeliver());const auto second=f.ticket;
    CHECK(f.host.Feedback(first,true)==Result::Invalid);
    CHECK(f.producer.PollOutcome()==ipc::Outcome::Pending);
    graphics::BodyPropFrame props;graphics::SceneDepthFrame depth;
    CHECK(!f.host.ReadBodyProps(first,props)&&!f.host.ReadSceneDepth(f.descriptor,first,depth));
    CHECK(f.host.ReadBodyProps(second,props)&&props.frameId==second.frameId);
    CHECK(f.host.ReadSceneDepth(f.descriptor,second,depth)&&depth.color.frameId==second.frameId);
    CHECK(f.host.Feedback(second,false)==Result::Ok);CHECK(f.producer.PollOutcome()==ipc::Outcome::Discarded);
    return 0;
}
int ExpiryBeforeAndAfterAcknowledgement(){
    for(unsigned afterAck=0;afterAck<2;++afterAck){
        Fixture f;CHECK(!f.Open());CHECK(!f.Deliver());const auto first=f.ticket;
        CHECK(f.host.QueueRequest(f.requirements,Tracking(1),1)==Result::Ok);
        if(afterAck){CHECK(f.host.Feedback(first,true)==Result::Ok);CHECK(f.producer.PollOutcome()==ipc::Outcome::Consumed);}
        Sleep(10);
        if(afterAck){
            // Promotion must honor the original queue deadline, not grant a fresh lifetime.
            CHECK(f.producer.TryTake(f.request)==Result::Busy);
            CHECK(f.host.PollRequest(f.descriptor,f.ticket)==Result::Timeout);
        }else{
            CHECK(f.host.PollRequest(f.descriptor,f.ticket)==Result::Timeout);
            CHECK(f.host.CancelRequest()==Result::Busy&&f.producer.PollOutcome()==ipc::Outcome::Pending);
            CHECK(f.host.Feedback(first,true)==Result::Ok);CHECK(f.producer.PollOutcome()==ipc::Outcome::Consumed);
        }
        CHECK(f.producer.TryTake(f.request)==Result::Busy);
        CHECK(!f.Deliver(2));CHECK(f.ticket.sequence==3);
        CHECK(f.host.Feedback(f.ticket,false)==Result::Ok);CHECK(f.producer.PollOutcome()==ipc::Outcome::Discarded);
    }
    return 0;
}
int CancelWhileGpuOwnsOldPair(){
    Fixture f;CHECK(!f.Open());CHECK(!f.Deliver());const auto first=f.ticket;
    CHECK(f.host.QueueRequest(f.requirements,Tracking(1),200)==Result::Ok);
    CHECK(f.host.CancelRequest()==Result::Busy);
    CHECK(f.host.CancelRequest()==Result::Busy&&f.host.PollRequest(f.descriptor,f.ticket)==Result::Busy);
    CHECK(f.producer.PollOutcome()==ipc::Outcome::Pending&&f.producer.TryTake(f.request)==Result::Busy);
    CHECK(f.host.Feedback(first,false)==Result::Ok);CHECK(f.producer.PollOutcome()==ipc::Outcome::Discarded);
    CHECK(f.host.CancelRequest()==Result::Ok&&f.producer.TryTake(f.request)==Result::Busy);
    CHECK(!f.Deliver(2));CHECK(f.ticket.trackingGeneration==Tracking(2).generation);
    CHECK(f.host.Feedback(f.ticket,true)==Result::Ok);CHECK(f.producer.PollOutcome()==ipc::Outcome::Consumed);
    return 0;
}
// Own the *real* named mutex on another thread. Reentrant same-thread mutex
// acquisition would not exercise deferred feedback. Destruction always releases
// and joins, including CHECK failures; no detached worker survives this fixture.
class HeldChannelMutex {
public:
    explicit HeldChannelMutex(const std::wstring& token,unsigned version=8){
        mutex_=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(L"Local\\FrostbiteVR.Control.v"+std::to_wstring(version)+L"."+token+L".mutex").c_str());
        ready_=CreateEventW(nullptr,TRUE,FALSE,nullptr);release_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(mutex_&&ready_&&release_)worker_=std::thread([this]{
            const auto result=WaitForSingleObject(mutex_,500);
            const bool owned=result==WAIT_OBJECT_0||result==WAIT_ABANDONED;
            acquired_.store(result==WAIT_OBJECT_0);SetEvent(ready_);
            if(owned){WaitForSingleObject(release_,1000);ReleaseMutex(mutex_);}
        });
    }
    bool Ready(){return worker_.joinable()&&WaitForSingleObject(ready_,500)==WAIT_OBJECT_0&&acquired_.load();}
    void Release(){if(release_)SetEvent(release_);if(worker_.joinable())worker_.join();}
    ~HeldChannelMutex(){Release();if(mutex_)CloseHandle(mutex_);if(ready_)CloseHandle(ready_);if(release_)CloseHandle(release_);}
private:
    HANDLE mutex_=nullptr,ready_=nullptr,release_=nullptr;std::atomic<bool> acquired_{false};std::thread worker_;
};
int DeferredAcknowledgement(){
    Fixture f;CHECK(!f.Open());CHECK(!f.Deliver());const auto first=f.ticket;
    CHECK(f.host.QueueRequest(f.requirements,Tracking(1),200)==Result::Ok);
    HeldChannelMutex blocker(f.host.Token());CHECK(blocker.Ready());
    CHECK(f.host.Feedback(first,true)==Result::Busy);
    auto wrong=first;++wrong.sequence;CHECK(f.host.Feedback(wrong,false)==Result::Invalid);
    // A duplicate may retry the same disposition, but cannot reverse it.
    CHECK(f.host.Feedback(first,false)==Result::Invalid);
    CHECK(f.host.Feedback(first,true)==Result::Busy);
    CHECK(f.host.FlushFeedback()==Result::Busy&&f.host.PollRequest(f.descriptor,f.ticket)==Result::Busy);
    CHECK(f.producer.PollOutcome()==ipc::Outcome::Pending);
    graphics::SceneDepthFrame depth;CHECK(!f.host.ReadSceneDepth(f.descriptor,first,depth));
    blocker.Release();CHECK(f.host.FlushFeedback()==Result::Ok);
    CHECK(f.host.Feedback(first,true)==Result::Invalid);
    CHECK(f.producer.PollOutcome()==ipc::Outcome::Consumed);
    CHECK(f.producer.TryTake(f.request)==Result::Ok&&f.request.trackingGeneration==Tracking(1).generation);
    CHECK(!f.PublishAndDeliver());CHECK(f.host.Feedback(f.ticket,false)==Result::Ok);
    CHECK(f.producer.PollOutcome()==ipc::Outcome::Discarded);return 0;
}
int ServiceDeferredFeedbackWithoutProviderQuery(){
    for(bool busyAtEnd:{false,true})for(unsigned cancellation=0;cancellation<3;++cancellation){
        ipc::RemoteFrameProvider host;CHECK(host.Create());CHECK(host.SetRequestLifetime(200));
        ipc::FrameChannel producer;CHECK(producer.ConnectProducer(host.Token()));
        const auto requirements=Requirements();runtime::TrackingFrame rendered{};
        graphics::TextureDescriptor d;graphics::PairTicket ticket;ipc::FrameRequest request;
        CHECK(!host.TryGetCompletedPair(requirements,Tracking(),rendered,d,ticket));
        CHECK(producer.TryTake(request)==Result::Ok);Pair(request,d,ticket);
        CHECK(producer.Publish(d,ticket)==Result::Ok);
        CHECK(host.TryGetCompletedPair(requirements,Tracking(1),rendered,d,ticket));
        HeldChannelMutex blocker(host.Token());CHECK(blocker.Ready());
        runtime::IFrameProvider& service=host;
        if(cancellation==1)service.Suspend(); // Cancellation before GPU completion.
        host.PairConsumed(ticket,true); // First ACK Busy; GPU done, local image held.
        CHECK(producer.PollOutcome()==ipc::Outcome::Pending);
        if(cancellation==2)service.Suspend(); // Cancellation before final ACK retry.
        if(!busyAtEnd)blocker.Release();
        service.ServiceFeedback(); // Host's final zero-wait service before EndFrame.
        if(busyAtEnd){
            CHECK(producer.PollOutcome()==ipc::Outcome::Pending);
            CHECK(host.Statistics().completed==1&&host.Statistics().requested==2);
            blocker.Release();service.ServiceFeedback(); // Next service recovers.
        }
        // This virtual service neither re-queries nor admits another request.
        CHECK(host.Statistics().completed==1&&host.Statistics().requested==2);
        CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);
        service.ServiceFeedback(); // Idempotent after the exact receipt was sent.
        if(cancellation)CHECK(producer.TryTake(request)==Result::Busy);
        else {CHECK(producer.TryTake(request)==Result::Ok);
            CHECK(request.trackingGeneration==Tracking(1).generation);
            CHECK(producer.Skip()==Result::Ok);}
        service.Suspend();service.ServiceFeedback();
    }
    return 0;
}
int DeferredAcknowledgementThenSpaceChange(){
    for(unsigned priorSuspend=0;priorSuspend<2;++priorSuspend){
        ipc::RemoteFrameProvider host;CHECK(host.Create());CHECK(host.SetRequestLifetime(200));
        ipc::FrameChannel producer;CHECK(producer.ConnectProducer(host.Token()));
        const auto requirements=Requirements();auto current=Tracking();runtime::TrackingFrame rendered{};
        graphics::TextureDescriptor d;graphics::PairTicket ticket;ipc::FrameRequest request;
        CHECK(!host.TryGetCompletedPair(requirements,current,rendered,d,ticket));CHECK(producer.TryTake(request)==Result::Ok);
        Pair(request,d,ticket);CHECK(producer.Publish(d,ticket)==Result::Ok);
        current=Tracking(1);CHECK(host.TryGetCompletedPair(requirements,current,rendered,d,ticket));const auto old=ticket;
        HeldChannelMutex blocker(host.Token());CHECK(blocker.Ready());
        host.PairConsumed(old,true);CHECK(host.FlushFeedback()==Result::Busy);
        if(priorSuspend)host.Suspend(); // Cancel also defers while the mutex is held.
        ++current.spaceGeneration;
        CHECK(!host.TryGetCompletedPair(requirements,current,rendered,d,ticket));
        CHECK(producer.PollOutcome()==ipc::Outcome::Pending);
        blocker.Release();
        // Deferred old feedback and cancellation share one critical section:
        // the old-space successor must be gone when its predecessor is reaped.
        CHECK(!host.TryGetCompletedPair(requirements,current,rendered,d,ticket));
        CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);
        CHECK(producer.TryTake(request)==Result::Busy);
        CHECK(!host.TryGetCompletedPair(requirements,current,rendered,d,ticket));
        CHECK(producer.TryTake(request)==Result::Ok);
        CHECK(request.spaceGeneration==current.spaceGeneration&&request.trackingGeneration==current.generation);
        CHECK(producer.Skip()==Result::Ok);host.Suspend();
    }
    return 0;
}
int ContendedSuspendBeforeGpuCompletion(){
    for(unsigned route=0;route<3;++route){
        ipc::RemoteFrameProvider host;CHECK(host.Create());CHECK(host.SetRequestLifetime(200));
        ipc::FrameChannel producer;CHECK(producer.ConnectProducer(host.Token()));
        const auto requirements=Requirements();auto current=Tracking();runtime::TrackingFrame rendered{};
        graphics::TextureDescriptor d;graphics::PairTicket ticket;ipc::FrameRequest request;
        CHECK(!host.TryGetCompletedPair(requirements,current,rendered,d,ticket));CHECK(producer.TryTake(request)==Result::Ok);
        Pair(request,d,ticket);CHECK(producer.Publish(d,ticket)==Result::Ok);
        current=Tracking(1);CHECK(host.TryGetCompletedPair(requirements,current,rendered,d,ticket));const auto old=ticket;
        HeldChannelMutex blocker(host.Token());CHECK(blocker.Ready());
        host.Suspend(); // Cannot acquire the gate; cancellation intent must persist.
        if(route==1){host.PairConsumed(old,true);CHECK(host.FlushFeedback()==Result::Busy);}
        CHECK(producer.PollOutcome()==ipc::Outcome::Pending);
        blocker.Release();
        // No second Suspend/TryGet call repairs the ordering before feedback.
        // Exercise immediate success, deferred success, and fatal-copy discard.
        if(route==1)CHECK(host.FlushFeedback()==Result::Ok);
        else host.PairConsumed(old,route==0);
        CHECK(producer.PollOutcome()==(route==2?ipc::Outcome::Discarded:ipc::Outcome::Consumed));
        CHECK(producer.TryTake(request)==Result::Busy); // Old-space successor is gone.
        current=Tracking(2);++current.spaceGeneration;
        CHECK(!host.TryGetCompletedPair(requirements,current,rendered,d,ticket));
        CHECK(producer.TryTake(request)==Result::Ok);
        CHECK(request.spaceGeneration==current.spaceGeneration&&request.trackingGeneration==current.generation);
        CHECK(producer.Skip()==Result::Ok);host.Suspend();
    }
    return 0;
}
int CancellationOnlyFlushBeforeAcknowledgement(){
    Fixture f;CHECK(!f.Open());CHECK(!f.Deliver());const auto old=f.ticket;
    CHECK(f.host.QueueRequest(f.requirements,Tracking(1),200)==Result::Ok);
    HeldChannelMutex blocker(f.host.Token());CHECK(blocker.Ready());
    CHECK(f.host.CancelRequest()==Result::Busy);blocker.Release();
    // A flush must drain cancellation even when there is not yet a GPU ack.
    CHECK(f.host.FlushFeedback()==Result::Ok);
    CHECK(f.producer.PollOutcome()==ipc::Outcome::Pending&&f.producer.TryTake(f.request)==Result::Busy);
    CHECK(f.host.Feedback(old,true)==Result::Ok);CHECK(f.producer.PollOutcome()==ipc::Outcome::Consumed);
    CHECK(f.producer.TryTake(f.request)==Result::Busy);
    CHECK(!f.Deliver(2));CHECK(f.host.Feedback(f.ticket,true)==Result::Ok);
    CHECK(f.producer.PollOutcome()==ipc::Outcome::Consumed);return 0;
}
int SuspendAndRecenterQueuedPose(){
    ipc::RemoteFrameProvider host;CHECK(host.Create());CHECK(host.SetRequestLifetime(200));
    ipc::FrameChannel producer;CHECK(producer.ConnectProducer(host.Token()));
    auto requirements=Requirements();auto current=Tracking();runtime::TrackingFrame rendered{};
    graphics::TextureDescriptor d;graphics::PairTicket ticket;ipc::FrameRequest request;
    CHECK(!host.TryGetCompletedPair(requirements,current,rendered,d,ticket));CHECK(producer.TryTake(request)==Result::Ok);
    Pair(request,d,ticket);CHECK(producer.Publish(d,ticket)==Result::Ok);
    current=Tracking(1);CHECK(host.TryGetCompletedPair(requirements,current,rendered,d,ticket));const auto held=ticket;
    ++current.spaceGeneration;requirements.width+=20;
    // Cancel the queued old-space pose immediately, retaining the old GPU pair.
    CHECK(!host.TryGetCompletedPair(requirements,current,rendered,d,ticket));
    CHECK(producer.PollOutcome()==ipc::Outcome::Pending&&producer.TryTake(request)==Result::Busy);
    host.PairConsumed(held,true);CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);
    CHECK(producer.TryTake(request)==Result::Busy);
    CHECK(!host.TryGetCompletedPair(requirements,current,rendered,d,ticket));CHECK(producer.TryTake(request)==Result::Ok);
    CHECK(request.spaceGeneration==current.spaceGeneration&&request.width==requirements.width);
    Pair(request,d,ticket);CHECK(producer.Publish(d,ticket)==Result::Ok);
    CHECK(host.TryGetCompletedPair(requirements,current,rendered,d,ticket));
    CHECK(runtime::PairMatchesFrame(requirements,rendered,d,ticket));
    const auto resumed=ticket;current.focused=false;
    CHECK(!host.TryGetCompletedPair(requirements,current,rendered,d,ticket));
    host.PairConsumed(resumed,false);CHECK(producer.PollOutcome()==ipc::Outcome::Discarded);
    CHECK(!host.TryGetCompletedPair(requirements,current,rendered,d,ticket));
    CHECK(producer.TryTake(request)==Result::Busy);
    current.focused=true;CHECK(!host.TryGetCompletedPair(requirements,current,rendered,d,ticket));
    CHECK(producer.TryTake(request)==Result::Ok);CHECK(producer.Skip()==Result::Ok);
    host.Suspend();return 0;
}
// Deliberately inspect/mutate the fixed wire ABI under its real mutex. This
// exercises receiver validation, not a second implementation of its policy.
struct PipelineWireView {
    HANDLE mapping=nullptr,mutex=nullptr;unsigned char* bytes=nullptr;
    unsigned size=6888,releaseOffset=6848,slotCount=2;
    explicit PipelineWireView(const std::wstring& token,bool third=false){
        if(third){size=10000;releaseOffset=9952;slotCount=3;}
        const auto prefix=std::wstring(third?L"Local\\FrostbiteVR.Control.v10.":L"Local\\FrostbiteVR.Control.v9.")+token;
        mapping=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,(prefix+L".map").c_str());
        mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(prefix+L".mutex").c_str());
        if(mapping)bytes=static_cast<unsigned char*>(MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,size));
    }
    ~PipelineWireView(){if(bytes)UnmapViewOfFile(bytes);if(mapping)CloseHandle(mapping);if(mutex)CloseHandle(mutex);}
    template<class T> bool Write(unsigned offset,const T& value){
        if(!bytes||!mutex||offset+sizeof(T)>size||WaitForSingleObject(mutex,100)!=WAIT_OBJECT_0)return false;
        std::memcpy(bytes+offset,&value,sizeof(value));ReleaseMutex(mutex);return true;
    }
    std::uint64_t Released(unsigned slot){
        std::uint64_t value=UINT64_MAX;if(bytes&&slot<slotCount&&WaitForSingleObject(mutex,100)==WAIT_OBJECT_0){
            std::memcpy(&value,bytes+releaseOffset+slot*8,sizeof(value));ReleaseMutex(mutex);}return value;
    }
};
void PipelinePair(const ipc::FrameRequest& request,const ipc::PipelineSlotKey& key,
    graphics::TextureDescriptor& d,graphics::PairTicket& t){
    Pair(request,d,t);const std::array<std::uint64_t,2> resources{100+10ull*key.slot,101+10ull*key.slot};
    std::memcpy(d.session.data(),resources.data(),sizeof(resources));t.session=d.session;
}
int TimeoutSnapshotsBeforeMutation(){
    // Three distinct ownership phases, no timing sleeps. Expire only the test
    // wire deadline under its real mutex, then inspect the unchanged outcome.
    for(unsigned mode=0;mode<3;++mode)for(unsigned stage=0;stage<3;++stage){
        const bool lookahead=mode!=0,third=mode==2;
        ipc::FrameChannel host,producer;CHECK(host.CreateHost(true,lookahead,lookahead,third));
        CHECK(producer.ConnectProducer(host.Token(),true,third));PipelineWireView wire(host.Token(),third);
        const auto admitted=lookahead?host.SubmitLookaheadRequest(Requirements(),Tracking(),200):host.BeginRequest(Requirements(),Tracking(),200);
        CHECK(admitted==Result::Ok);ipc::FrameRequest request{};ipc::PipelineSlotKey key{1,0,0};
        graphics::TextureDescriptor image{},out{};graphics::PairTicket ticket{},got{};
        ipc::FrameTimeoutSnapshot timeout{};timeout.available=true;
        CHECK(host.PollRequest(out,got,&timeout)==Result::Busy&&!timeout.available);
        if(stage){
            CHECK(producer.TryTakePipeline(request,key)==Result::Ok);
            if(lookahead)CHECK(producer.MarkCaptureReleased(key)==Result::Ok);
            if(stage==2){PipelinePair(request,key,image,ticket);CHECK(producer.PublishPipeline(key,image,ticket,nullptr,nullptr)==Result::Ok);}
        }
        const auto deadlineOffset=56+unsigned(key.slot*sizeof(ipc::PipelineSlot)+offsetof(ipc::PipelineSlot,frame)+
            offsetof(ipc::FrameSlot,request)+offsetof(ipc::FrameRequest,deadlineQpc));
        CHECK(wire.Write(deadlineOffset,std::int64_t(1)));
        CHECK(host.PollRequest(out,got,&timeout)==Result::Timeout);
        CHECK(timeout.available&&timeout.clockValid&&timeout.wireVersion==(third?10u:9u)&&timeout.requestedKey==key);
        CHECK(timeout.reason==ipc::FrameTimeoutReason::SlotDeadlineOrTerminal&&timeout.requestObserved&&
            timeout.observedRequestId==key.request&&timeout.observedDeadlineQpc==1&&timeout.deadlineExpired);
        CHECK(timeout.phaseValid&&timeout.phase==(stage==0?ipc::Phase::Requested:stage==1?ipc::Phase::Rendering:ipc::Phase::Ready));
        CHECK(!timeout.terminalPhase&&timeout.nativeHeldRequest==(stage?key.request:0)&&timeout.hasSource==(stage==2)&&!timeout.retiring);
        CHECK(timeout.captureReleasedRequest==((lookahead&&stage)?key.request:0));
        if(stage){
            if(stage==1)CHECK(producer.SkipPipeline(key)==Result::Ok);
            ipc::PipelineRetirement retired{};CHECK(producer.ClaimPipelineRetirement(key,retired)==Result::Ok&&retired.outcome==ipc::Outcome::Discarded);
            CHECK(producer.FinishPipelineRetirement(retired)==Result::Ok);
        }
        CHECK(host.PollRequest(out,got,&timeout)!=Result::Timeout&&!timeout.available);
        host.Close();timeout.available=true;CHECK(host.PollRequest(out,got,&timeout)==Result::Closed&&!timeout.available);
    }
    return 0;
}
int CaptureReleaseNegotiationAndCorruption(){
    ipc::FrameChannel host,producer;CHECK(!host.CreateHost(false,false,true));CHECK(!host.CreateHost(true,false,true));
    for(bool lookahead:{false,true}){
        CHECK(host.CreateHost(true,lookahead));CHECK(producer.ConnectProducer(host.Token(),true));
        CHECK(!host.ExportOverlapEnabled()&&!producer.ExportOverlapEnabled());
        CHECK(producer.MarkCaptureReleased({1,0,0})==Result::Invalid);producer.Close();host.Close();
    }
    // Unknown bit, overlap without lookahead, changed negotiated known bits,
    // nonzero default marker, and an unrelated released request all fail closed.
    for(unsigned failure=0;failure<7;++failure){
        CHECK(host.CreateHost(true,true,failure!=3));
        PipelineWireView wire(host.Token());CHECK(wire.bytes&&wire.mutex);
        if(failure<2){const std::uint32_t flags=failure?2u:4u;CHECK(wire.Write(12,flags));
            CHECK(!producer.ConnectProducer(host.Token(),true));
        }else{
            CHECK(producer.ConnectProducer(host.Token(),true));
            if(failure==2){const std::uint32_t flags=1;CHECK(wire.Write(12,flags));}
            else if(failure==5){
                CHECK(host.SubmitLookaheadRequest(Requirements(),Tracking(0),200)==Result::Ok);
                CHECK(host.SubmitLookaheadRequest(Requirements(),Tracking(1),200)==Result::Ok);
                ipc::FrameRequest a{},b{};ipc::PipelineSlotKey ak{},bk{};
                CHECK(producer.TryTakePipeline(a,ak)==Result::Ok&&producer.MarkCaptureReleased(ak)==Result::Ok);
                CHECK(producer.TryTakePipeline(b,bk)==Result::Ok);const std::uint64_t zero=0;
                CHECK(wire.Write(6848+ak.slot*8,zero)); // Two unreleased Rendering cameras must fail header validation.
            }else {const std::uint64_t wrong=99;CHECK(wire.Write(failure==6?6864:6848,wrong));}
            CHECK(!producer.Connected());
        }
        producer.Close();host.Close();
    }
    CHECK(host.CreateHost(true,true,true));CHECK(producer.ConnectProducer(host.Token(),true));
    CHECK(host.ExportOverlapEnabled()&&producer.ExportOverlapEnabled());
    CHECK(host.MarkCaptureReleased({1,0,0})==Result::Invalid);producer.Close();host.Close();
    return 0;
}
int CaptureReleaseMappedOwnership(){
    ipc::FrameChannel host,producer;CHECK(host.CreateHost(true,true,true));CHECK(producer.ConnectProducer(host.Token(),true));
    PipelineWireView wire(host.Token());CHECK(wire.bytes&&wire.mutex);
    const auto req=Requirements();ipc::FrameRequest a{},b{},c{};ipc::PipelineSlotKey ak{},bk{},ck{};
    graphics::TextureDescriptor ad{},bd{},out{};graphics::PairTicket at{},bt{},got{};
    CHECK(host.SubmitLookaheadRequest(req,Tracking(0),200)==Result::Ok&&host.SubmitLookaheadRequest(req,Tracking(1),200)==Result::Ok);
    CHECK(producer.TryTakePipeline(a,ak)==Result::Ok);auto wrong=ak;++wrong.request;
    CHECK(producer.MarkCaptureReleased(wrong)==Result::Invalid&&wire.Released(ak.slot)==0);
    {HeldChannelMutex held(host.Token(),9);CHECK(held.Ready());
        CHECK(producer.MarkCaptureReleased(ak)==Result::Busy);held.Release();}
    CHECK(wire.Released(ak.slot)==0&&producer.TryTakePipeline(b,bk)==Result::Busy);
    CHECK(producer.MarkCaptureReleased(ak)==Result::Ok&&producer.MarkCaptureReleased(ak)==Result::Ok);
    CHECK(wire.Released(ak.slot)==ak.request&&producer.TryTakePipeline(b,bk)==Result::Ok&&bk.slot!=ak.slot);
    CHECK(host.SubmitLookaheadRequest(req,Tracking(2),200)==Result::Busy); // Both undelivered receipts occupied.
    PipelinePair(a,ak,ad,at);PipelinePair(b,bk,bd,bt);
    CHECK(producer.PublishPipeline(bk,bd,bt,nullptr,nullptr)==Result::Ok);
    CHECK(host.PollRequest(out,got)==Result::Busy); // B ready cannot bypass A's unpublished head.
    CHECK(producer.MarkCaptureReleased(bk)==Result::Invalid);
    CHECK(producer.PublishPipeline(ak,ad,at,nullptr,nullptr)==Result::Ok);
    CHECK(host.PollRequest(out,got)==Result::Ok&&!std::memcmp(&got,&at,sizeof(got)));
    runtime::SourceSlotIdentity source{};CHECK(host.ReadSourceSlot(at,source)&&source.requestId==ak.request&&source.slot==ak.slot);
    CHECK(host.SubmitLookaheadRequest(req,Tracking(2),200)==Result::Ok); // One metadata-only C, no third source.
    CHECK(producer.TryTakePipeline(c,ck)==Result::Busy&&host.PollRequest(out,got)==Result::Busy);
    CHECK(host.Feedback(at,true)==Result::Ok);ipc::PipelineRetirement retired{};
    CHECK(producer.ClaimPipelineRetirement(ak,retired)==Result::Ok&&retired.outcome==ipc::Outcome::Consumed);
    CHECK(wire.Released(ak.slot)==ak.request&&producer.TryTakePipeline(c,ck)==Result::Busy);
    auto bad=retired;++bad.ticket.sequence;
    CHECK(producer.FinishPipelineRetirement(bad)==Result::Invalid&&wire.Released(ak.slot)==ak.request);
    CHECK(producer.FinishPipelineRetirement(retired)==Result::Ok&&wire.Released(ak.slot)==0);
    CHECK(producer.TryTakePipeline(c,ck)==Result::Ok&&ck.slot==ak.slot&&c.trackingGeneration==Tracking(2).generation);
    CHECK(producer.MarkCaptureReleased(ak)==Result::Invalid&&wire.Released(ck.slot)==0);
    CHECK(host.PollRequest(out,got)==Result::Ok&&!std::memcmp(&got,&bt,sizeof(got)));
    CHECK(host.CancelRequest()==Result::Busy);CHECK(producer.SkipPipeline(ck)==Result::Ok);
    CHECK(producer.ClaimPipelineRetirement(ck,retired)==Result::Ok&&retired.outcome==ipc::Outcome::Discarded);
    CHECK(producer.FinishPipelineRetirement(retired)==Result::Ok);
    CHECK(host.Feedback(bt,false)==Result::Ok&&producer.ClaimPipelineRetirement(bk,retired)==Result::Ok);
    CHECK(producer.FinishPipelineRetirement(retired)==Result::Ok);return 0;
}
int CaptureReleaseExpiryAndDeferredCancel(){
    for(bool expire:{false,true}){
        ipc::FrameChannel host,producer;CHECK(host.CreateHost(true,true,true));CHECK(producer.ConnectProducer(host.Token(),true));
        PipelineWireView wire(host.Token());const auto req=Requirements();ipc::FrameRequest a{},b{},next{};ipc::PipelineSlotKey ak{},bk{},key{};
        graphics::TextureDescriptor ad{},bd{},out{};graphics::PairTicket at{},bt{},got{};ipc::PipelineRetirement retired{};
        CHECK(host.SubmitLookaheadRequest(req,Tracking(0),expire?100:200)==Result::Ok&&host.SubmitLookaheadRequest(req,Tracking(1),200)==Result::Ok);
        CHECK(producer.TryTakePipeline(a,ak)==Result::Ok&&producer.MarkCaptureReleased(ak)==Result::Ok);
        CHECK(producer.TryTakePipeline(b,bk)==Result::Ok&&producer.MarkCaptureReleased(bk)==Result::Ok);
        PipelinePair(a,ak,ad,at);PipelinePair(b,bk,bd,bt);
        if(expire){
            Sleep(120);CHECK(producer.PublishPipeline(ak,ad,at,nullptr,nullptr)==Result::Timeout);
            CHECK(producer.PublishPipeline(bk,bd,bt,nullptr,nullptr)==Result::Ok);
            CHECK(host.PollRequest(out,got)==Result::Timeout); // Exactly oldest A, never reassociated with B.
            CHECK(host.PollRequest(out,got)==Result::Ok&&!std::memcmp(&got,&bt,sizeof(got)));
            CHECK(producer.ClaimPipelineRetirement(ak,retired)==Result::Ok&&retired.outcome==ipc::Outcome::Discarded);
            CHECK(wire.Released(ak.slot)==ak.request&&producer.FinishPipelineRetirement(retired)==Result::Ok);
            CHECK(host.Feedback(bt,true)==Result::Ok&&producer.ClaimPipelineRetirement(bk,retired)==Result::Ok);
            CHECK(producer.FinishPipelineRetirement(retired)==Result::Ok);
        }else{
            CHECK(producer.PublishPipeline(ak,ad,at,nullptr,nullptr)==Result::Ok&&host.PollRequest(out,got)==Result::Ok);
            CHECK(host.SubmitLookaheadRequest(req,Tracking(2),200)==Result::Ok);
            {HeldChannelMutex held(host.Token(),9);CHECK(held.Ready());
                CHECK(host.CancelRequest()==Result::Busy&&host.Feedback(at,true)==Result::Busy);held.Release();}
            CHECK(host.FlushFeedback()==Result::Ok); // Cancels B/C before applying A's exact ACK.
            CHECK(producer.MarkCaptureReleased(bk)==Result::Invalid&&producer.TryTakePipeline(next,key)==Result::Busy);
            CHECK(wire.Released(bk.slot)==bk.request&&producer.SkipPipeline(bk)==Result::Ok);
            CHECK(producer.ClaimPipelineRetirement(bk,retired)==Result::Ok&&retired.outcome==ipc::Outcome::Discarded);
            CHECK(producer.FinishPipelineRetirement(retired)==Result::Ok);
            CHECK(producer.ClaimPipelineRetirement(ak,retired)==Result::Ok&&retired.outcome==ipc::Outcome::Consumed);
            CHECK(producer.FinishPipelineRetirement(retired)==Result::Ok&&producer.TryTakePipeline(next,key)==Result::Busy);
        }
        CHECK(wire.Released(0)==0&&wire.Released(1)==0);
    }
    return 0;
}
int ThreeSourceNegotiation(){
    ipc::FrameChannel host,producer;
    CHECK(!host.CreateHost(false,true,true,true));CHECK(!host.CreateHost(true,false,true,true));
    CHECK(!host.CreateHost(true,true,false,true));CHECK(!producer.ConnectProducer(L"bad",false,true));
    for(bool third:{false,true}){
        CHECK(host.CreateHost(true,true,true,third));
        CHECK(!producer.ConnectProducer(host.Token(),true,!third)); // No cross-version probing/fallback.
        CHECK(producer.ConnectProducer(host.Token(),true,third));
        CHECK(host.SourceSlotCount()==(third?3u:2u)&&producer.SourceSlotCount()==host.SourceSlotCount());
        CHECK(host.ExportOverlapEnabled()&&producer.ExportOverlapEnabled());producer.Close();host.Close();
    }
    for(unsigned corrupt=0;corrupt<4;++corrupt){
        CHECK(host.CreateHost(true,true,true,true));PipelineWireView wire(host.Token(),true);CHECK(wire.bytes&&wire.mutex);
        if(corrupt==0){const std::uint32_t wrong=9;CHECK(wire.Write(4,wrong));}
        else if(corrupt==1){const std::uint32_t wrong=6888;CHECK(wire.Write(8,wrong));}
        else if(corrupt==2){const std::uint32_t wrong=1;CHECK(wire.Write(12,wrong));}
        else{const std::uint64_t wrong=99;CHECK(wire.Write(9952+2*8,wrong));}
        CHECK(!producer.ConnectProducer(host.Token(),true,true));host.Close();
    }
    // Portable v10 capacity remains two undelivered poses before any delivery,
    // despite the unused third source. The v9 policy keeps its original ABI.
    ipc::ThreeSlotFramePipeline policy{};CHECK(ipc::PipelineEnable(policy));
    ipc::FrameRequest a{},b{},c{};ipc::PipelineSlotKey key{};
    CHECK(ipc::Encode(Requirements(),Tracking(0),1,200,a)&&ipc::Encode(Requirements(),Tracking(1),2,200,b)&&
          ipc::Encode(Requirements(),Tracking(2),3,200,c));
    CHECK(ipc::PipelineLookaheadRequest(policy,a,1,key)&&key.slot==0);
    CHECK(ipc::PipelineLookaheadRequest(policy,b,1,key)&&key.slot==1);
    CHECK(!ipc::PipelineLookaheadRequest(policy,c,1,key)&&key.slot==UINT32_MAX);
    CHECK(sizeof(ipc::TwoSlotFramePipeline)==896&&sizeof(ipc::ThreeSlotFramePipeline)==1336);
    return 0;
}
int ThreeSourceCapacityAndRetirement(){
    for(bool third:{false,true}){
        ipc::FrameChannel host,producer;CHECK(host.CreateHost(true,true,true,third));
        CHECK(producer.ConnectProducer(host.Token(),true,third));
        ipc::FrameRequest a{},b{},c{},d{};ipc::PipelineSlotKey ak{},bk{},ck{},dk{};
        graphics::TextureDescriptor ad{},bd{},cd{},out{};graphics::PairTicket at{},bt{},ct{},got{};
        CHECK(host.SubmitLookaheadRequest(Requirements(),Tracking(0),200)==Result::Ok);
        CHECK(host.SubmitLookaheadRequest(Requirements(),Tracking(1),200)==Result::Ok);
        CHECK(producer.TryTakePipeline(a,ak)==Result::Ok&&producer.MarkCaptureReleased(ak)==Result::Ok);
        CHECK(producer.TryTakePipeline(b,bk)==Result::Ok&&producer.MarkCaptureReleased(bk)==Result::Ok);
        PipelinePair(a,ak,ad,at);CHECK(producer.PublishPipeline(ak,ad,at,nullptr,nullptr)==Result::Ok);
        CHECK(host.PollRequest(out,got)==Result::Ok&&!std::memcmp(&got,&at,sizeof(at)));
        CHECK(host.SubmitLookaheadRequest(Requirements(),Tracking(2),200)==Result::Ok);
        CHECK(host.SubmitLookaheadRequest(Requirements(),Tracking(3),200)==Result::Busy);
        const auto taken=producer.TryTakePipeline(c,ck);
        if(!third){CHECK(taken==Result::Busy);host.Close();producer.Close();continue;}
        // A Delivered + B restored/exporting + C Rendering: no fabricated A ACK.
        CHECK(taken==Result::Ok&&ck.slot==2&&c.trackingGeneration==Tracking(2).generation);
        CHECK(producer.MarkCaptureReleased({ck.request,UINT32_MAX,0})==Result::Invalid);
        runtime::SourceSlotIdentity source{};CHECK(host.ReadSourceSlot(at,source)&&source.slot==ak.slot&&source.slotCount==3);
        CHECK(host.PollRequest(out,got)==Result::Busy);
        PipelinePair(c,ck,cd,ct);CHECK(producer.PublishPipeline(ck,cd,ct,nullptr,nullptr)==Result::Ok);
        CHECK(host.PollRequest(out,got)==Result::Busy); // Out-of-order ready C cannot bypass B/A.
        PipelinePair(b,bk,bd,bt);
        auto alias=bd;alias.session=cd.session;auto aliasTicket=bt;aliasTicket.session=cd.session;
        CHECK(producer.PublishPipeline(bk,alias,aliasTicket,nullptr,nullptr)==Result::Invalid); // Check other slot2, not only A.
        auto depth=Depth(bd,bt);depth.resources={{{500,501},{502,503}}};
        const auto colors=graphics::LegacyResourceIds(cd);auto aliasDepth=depth;aliasDepth.resources[0][0]=colors[0];
        CHECK(producer.PublishPipeline(bk,bd,bt,nullptr,&aliasDepth)==Result::Invalid);
        CHECK(producer.PublishPipeline(bk,bd,bt,nullptr,&depth)==Result::Ok);
        auto wrong=at;++wrong.sequence;CHECK(host.Feedback(wrong,true)==Result::Invalid);
        CHECK(host.Feedback(at,true)==Result::Ok);ipc::PipelineRetirement retired{};
        CHECK(host.PollRequest(out,got)==Result::Ok&&!std::memcmp(&got,&bt,sizeof(bt)));
        CHECK(host.SubmitLookaheadRequest(Requirements(),Tracking(3),200)==Result::Ok); // One unassigned immutable D.
        CHECK(producer.TryTakePipeline(d,dk)==Result::Busy); // Logical A ACK is insufficient for physical reuse.
        CHECK(producer.ClaimPipelineRetirement(ak,retired)==Result::Ok&&retired.outcome==ipc::Outcome::Consumed);
        CHECK(producer.TryTakePipeline(d,dk)==Result::Busy);
        auto stale=retired;++stale.key.request;CHECK(producer.FinishPipelineRetirement(stale)==Result::Invalid);
        CHECK(producer.FinishPipelineRetirement(retired)==Result::Ok);
        CHECK(producer.TryTakePipeline(d,dk)==Result::Ok&&dk.slot==ak.slot&&d.requestId==ak.request+3);
        CHECK(producer.MarkCaptureReleased(ak)==Result::Invalid); // Old key cannot release reused source.
        CHECK(host.Feedback(bt,true)==Result::Ok&&producer.ClaimPipelineRetirement(bk,retired)==Result::Ok);
        CHECK(producer.FinishPipelineRetirement(retired)==Result::Ok);
        CHECK(host.PollRequest(out,got)==Result::Ok&&!std::memcmp(&got,&ct,sizeof(ct)));
        {HeldChannelMutex held(host.Token(),10);CHECK(held.Ready());
            CHECK(host.CancelRequest()==Result::Busy&&host.Feedback(ct,false)==Result::Busy);held.Release();}
        CHECK(host.FlushFeedback()==Result::Ok); // Cancel D before exact negative C ACK.
        CHECK(producer.MarkCaptureReleased(dk)==Result::Invalid&&producer.SkipPipeline(dk)==Result::Ok);
        CHECK(producer.ClaimPipelineRetirement(dk,retired)==Result::Ok&&retired.outcome==ipc::Outcome::Discarded);
        CHECK(producer.FinishPipelineRetirement(retired)==Result::Ok);
        CHECK(producer.ClaimPipelineRetirement(ck,retired)==Result::Ok&&retired.outcome==ipc::Outcome::Discarded);
        CHECK(producer.FinishPipelineRetirement(retired)==Result::Ok);
        CHECK(producer.TryTakePipeline(d,dk)==Result::Busy);host.Close();producer.Close();
    }
    return 0;
}
int ThreeSourceExpiryAndClose(){
    ipc::FrameChannel host,producer;CHECK(host.CreateHost(true,true,true,true));CHECK(producer.ConnectProducer(host.Token(),true,true));
    ipc::FrameRequest a{},b{};ipc::PipelineSlotKey ak{},bk{};graphics::TextureDescriptor ad{},bd{},out{};graphics::PairTicket at{},bt{},got{};
    CHECK(host.SubmitLookaheadRequest(Requirements(),Tracking(0),100)==Result::Ok);
    CHECK(host.SubmitLookaheadRequest(Requirements(),Tracking(1),200)==Result::Ok);
    CHECK(producer.TryTakePipeline(a,ak)==Result::Ok&&producer.MarkCaptureReleased(ak)==Result::Ok);
    CHECK(producer.TryTakePipeline(b,bk)==Result::Ok);PipelinePair(b,bk,bd,bt);
    CHECK(producer.PublishPipeline(bk,bd,bt,nullptr,nullptr)==Result::Ok);Sleep(120);
    PipelinePair(a,ak,ad,at);CHECK(producer.PublishPipeline(ak,ad,at,nullptr,nullptr)==Result::Timeout);
    ipc::FrameTimeoutSnapshot timeout{};
    CHECK(host.PollRequest(out,got,&timeout)==Result::Timeout); // Oldest original A once, despite ready B.
    CHECK(timeout.available&&timeout.requestedKey==ak&&timeout.nativeHeldRequest==ak.request&&timeout.hasSource&&
        timeout.captureReleasedRequest==ak.request&&timeout.terminalPhase&&timeout.deadlineExpired);
    // Sleep is a minimum delay, not a guarantee that Windows will schedule us
    // again before B's original 200 ms deadline. If B also expired, it must be
    // discarded with its own exact timeout evidence, never delivered late.
    ipc::FrameTimeoutSnapshot secondTimeout{};
    LARGE_INTEGER beforeSecond{};CHECK(QueryPerformanceCounter(&beforeSecond));
    const auto second=host.PollRequest(out,got,&secondTimeout);
    if(second==Result::Timeout){
        CHECK(secondTimeout.available&&secondTimeout.clockValid&&secondTimeout.requestObserved&&
            secondTimeout.wireVersion==10&&secondTimeout.reason==ipc::FrameTimeoutReason::SlotDeadlineOrTerminal&&
            secondTimeout.requestedKey==bk&&secondTimeout.observedRequestId==b.requestId&&
            secondTimeout.observedDeadlineQpc==b.deadlineQpc&&secondTimeout.hasSource&&
            secondTimeout.deadlineExpired&&secondTimeout.nowQpc>=b.deadlineQpc);
        std::cout<<"ThreeSourceExpiryAndClose: second original lease expired before delivery; lateness_qpc="
                 <<secondTimeout.nowQpc-b.deadlineQpc<<'\n';
    }else {
        if(second!=Result::Ok)std::cerr<<"Second poll result="<<unsigned(second)
            <<" available="<<secondTimeout.available<<" deadline_expired="<<secondTimeout.deadlineExpired<<'\n';
        CHECK(second==Result::Ok&&!secondTimeout.available&&beforeSecond.QuadPart<b.deadlineQpc&&!std::memcmp(&got,&bt,sizeof(bt)));
    }
    ipc::PipelineRetirement retired{};CHECK(producer.ClaimPipelineRetirement(ak,retired)==Result::Ok&&retired.outcome==ipc::Outcome::Discarded);
    CHECK(producer.FinishPipelineRetirement(retired)==Result::Ok);
    if(second==Result::Timeout){
        CHECK(producer.ClaimPipelineRetirement(bk,retired)==Result::Ok&&retired.outcome==ipc::Outcome::Discarded);
        CHECK(producer.FinishPipelineRetirement(retired)==Result::Ok);
        host.Close();
    }else {
        host.Close();CHECK(producer.ClaimPipelineRetirement(bk,retired)==Result::Busy); // Close is not a positive ACK.
    }
    producer.Close();return 0;
}
int ThreeSourceQueuedExpiry(){
    ipc::FrameChannel host,producer;CHECK(host.CreateHost(true,true,true,true));CHECK(producer.ConnectProducer(host.Token(),true,true));
    std::array<ipc::FrameRequest,3> requests{};std::array<ipc::PipelineSlotKey,3> keys{};
    std::array<graphics::TextureDescriptor,3> images{};std::array<graphics::PairTicket,3> tickets{};
    graphics::TextureDescriptor out{};graphics::PairTicket got{};
    for(unsigned i=0;i<2;++i)CHECK(host.SubmitLookaheadRequest(Requirements(),Tracking(i),200)==Result::Ok);
    for(unsigned i=0;i<2;++i){CHECK(producer.TryTakePipeline(requests[i],keys[i])==Result::Ok);
        CHECK(producer.MarkCaptureReleased(keys[i])==Result::Ok);PipelinePair(requests[i],keys[i],images[i],tickets[i]);
        CHECK(producer.PublishPipeline(keys[i],images[i],tickets[i],nullptr,nullptr)==Result::Ok);}
    CHECK(host.PollRequest(out,got)==Result::Ok);CHECK(host.SubmitLookaheadRequest(Requirements(),Tracking(2),200)==Result::Ok);
    CHECK(producer.TryTakePipeline(requests[2],keys[2])==Result::Ok&&keys[2].slot==2);
    PipelinePair(requests[2],keys[2],images[2],tickets[2]);CHECK(producer.PublishPipeline(keys[2],images[2],tickets[2],nullptr,nullptr)==Result::Ok);
    CHECK(host.Feedback(tickets[0],true)==Result::Ok&&host.PollRequest(out,got)==Result::Ok);
    CHECK(host.SubmitLookaheadRequest(Requirements(),Tracking(3),100)==Result::Ok); // A terminal but still physically held.
    ipc::FrameRequest blocked{};ipc::PipelineSlotKey key{};CHECK(producer.TryTakePipeline(blocked,key)==Result::Busy);
    Sleep(120);ipc::PipelineRetirement retired{};
    CHECK(producer.ClaimPipelineRetirement(keys[0],retired)==Result::Ok&&producer.FinishPipelineRetirement(retired)==Result::Ok);
    CHECK(producer.TryTakePipeline(blocked,key)==Result::Busy); // Promotion expires original D; never renew it.
    CHECK(host.Feedback(tickets[1],true)==Result::Ok&&host.PollRequest(out,got)==Result::Ok&&!std::memcmp(&got,&tickets[2],sizeof(got)));
    ipc::FrameTimeoutSnapshot timeout{};
    CHECK(host.PollRequest(out,got,&timeout)==Result::Timeout&&host.PollRequest(out,got)!=Result::Timeout);
    CHECK(timeout.available&&timeout.wireVersion==10&&timeout.requestedKey.request==4&&timeout.requestedKey.slot==UINT32_MAX&&
        timeout.reason==ipc::FrameTimeoutReason::RequestMissing&&!timeout.requestObserved&&!timeout.phaseValid);
    CHECK(host.Feedback(tickets[2],false)==Result::Ok);
    for(unsigned i=1;i<3;++i){CHECK(producer.ClaimPipelineRetirement(keys[i],retired)==Result::Ok);
        CHECK(producer.FinishPipelineRetirement(retired)==Result::Ok);}
    CHECK(producer.TryTakePipeline(blocked,key)==Result::Busy);host.Close();producer.Close();return 0;
}

}
int main(){
    if(TimeoutSnapshotsBeforeMutation())return 1;
    if(ThreeSourceNegotiation()||ThreeSourceCapacityAndRetirement()||ThreeSourceExpiryAndClose()||ThreeSourceQueuedExpiry())return 1;
    if(CaptureReleaseNegotiationAndCorruption()||CaptureReleaseMappedOwnership()||CaptureReleaseExpiryAndDeferredCancel())return 1;
    if(ConsecutiveProviderTicks()||QueuedOwnershipAndIdentity()||ExpiryBeforeAndAfterAcknowledgement()||
       CancelWhileGpuOwnsOldPair()||DeferredAcknowledgement()||ServiceDeferredFeedbackWithoutProviderQuery()||DeferredAcknowledgementThenSpaceChange()||
       ContendedSuspendBeforeGpuCompletion()||CancellationOnlyFlushBeforeAcknowledgement()||
       SuspendAndRecenterQueuedPose())return 1;
    std::puts("PipelinedFrameChannel: 120 synthetic ticks, 119 deliveries, 120 requests; ownership, expiry, cancellation, deferred acknowledgement and pose identity passed. No GPU or headset performance claim.");
    return 0;
}
