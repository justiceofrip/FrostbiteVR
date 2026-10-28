#include "Test.h"
#include "fvr/ipc/StagedFrameProducer.h"
#include "fvr/ipc/RemoteFrameProvider.h"
#include "fvr/graphics/FrameBridgePresentService.h"
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
using namespace fvr;
using namespace std::chrono_literals;
struct Feedback final:runtime::IFrameProvider {
    std::atomic<unsigned> consumed=0,discarded=0;std::thread::id graphicsThread{};
    std::atomic<bool> wrongThread=false;
    graphics::PairTicket lastTicket{};
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    void PairConsumed(const graphics::PairTicket& ticket,bool okay)noexcept override {
        if(std::this_thread::get_id()!=graphicsThread)wrongThread=true;
        lastTicket=ticket;
        if(okay)++consumed;else ++discarded;
    }
};
struct Observations {
    unsigned published=0,reaped=0;bool wrong=false;std::thread::id graphicsThread{};
    ipc::FrameLease lastPublished{};graphics::PairTicket lastTicket{};
    static void Observe(void* context,const ipc::FrameLease& lease,const graphics::PairTicket& ticket,ipc::StagedFrameObservation stage)noexcept {
        auto& o=*static_cast<Observations*>(context);
        if(std::this_thread::get_id()!=o.graphicsThread||ticket.frameId!=lease.native.frameId||
           ticket.trackingGeneration!=lease.tracking.generation||ticket.spaceGeneration!=lease.tracking.spaceGeneration)o.wrong=true;
        if(stage==ipc::StagedFrameObservation::Published){++o.published;o.lastPublished=lease;o.lastTicket=ticket;}
        else {++o.reaped;if(lease.requestId!=o.lastPublished.requestId||lease.channelGeneration!=o.lastPublished.channelGeneration||
            std::memcmp(&ticket,&o.lastTicket,sizeof(ticket)))o.wrong=true;}
    }
};
bool Acquire(ipc::StagedFrameProducer& producer,ipc::FrameLease& lease,ipc::NativeFrameKey key){
    const auto deadline=GetTickCount64()+1000;
    while(GetTickCount64()<deadline){if(producer.TryBegin(key,lease))return true;Sleep(1);}return false;
}
void MakeTicket(const ipc::FrameLease& lease,graphics::TextureDescriptor& d,graphics::PairTicket& t){
    d.width=lease.requirements.width;d.height=lease.requirements.height;d.format=lease.requirements.format;d.adapterLow=lease.requirements.adapterLow;d.adapterHigh=lease.requirements.adapterHigh;d.session[0]=17;d.resourceEpoch=4;
    t.session=d.session;t.resourceEpoch=d.resourceEpoch;t.sequence=1;t.frameId=lease.native.frameId;t.trackingGeneration=lease.tracking.generation;t.spaceGeneration=lease.tracking.spaceGeneration;t.predictedNs=lease.tracking.predictedNs;
}
ipc::ChannelResult PumpUntilHost(ipc::StagedFrameProducer& producer,std::future<ipc::ChannelResult>& pending){
    while(pending.wait_for(0ms)!=std::future_status::ready){producer.PumpGraphics();Sleep(1);}
    return pending.get();
}
bool WaitFeedback(ipc::StagedFrameProducer& producer,Feedback& feedback,unsigned consumed,unsigned discarded){
    const auto deadline=GetTickCount64()+1000;
    while(GetTickCount64()<deadline){producer.PumpGraphics();if(feedback.consumed==consumed&&feedback.discarded==discarded)return true;Sleep(1);}return false;
}
int LatePresentReapsWithoutAdmission(bool afterPresent){
    Feedback feedback;feedback.graphicsThread=std::this_thread::get_id();
    Observations observed;observed.graphicsThread=feedback.graphicsThread;
    ipc::FrameChannel host;CHECK(host.CreateHost());
    ipc::StagedFrameProducer producer(feedback,Observations::Observe,&observed);CHECK(producer.Connect(host.Token()));
    const runtime::PresentationRequirements requirements{100,120,29,19,4};
    runtime::TrackingFrame first{};first.generation=11;first.spaceGeneration=7;first.predictedNs=1000000000;
    first.headValid=first.focused=true;first.eyes[0].position.x=-.032f;first.eyes[1].position.x=.032f;
    first.fov={math::FovTangents{-1,1,1,-1},math::FovTangents{-1,1,1,-1}};
    CHECK(host.BeginRequest(requirements,first,200)==ipc::ChannelResult::Ok);
    const ipc::NativeFrameKey firstKey{101,201,301};ipc::FrameLease lease{};
    CHECK(producer.TryBegin(firstKey,lease));
    graphics::TextureDescriptor descriptor{};graphics::PairTicket ticket{};MakeTicket(lease,descriptor,ticket);
    CHECK(producer.Submit(lease,descriptor,ticket)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(host.PollRequest(descriptor,ticket)==ipc::ChannelResult::Ok);
    auto next=first;++next.generation;next.predictedNs+=11000000;next.head.position.x=.15f;
    CHECK(host.QueueRequest(requirements,next,200)==ipc::ChannelResult::Ok);
    auto nextKey=firstKey;++nextKey.frameId;ipc::FrameLease successor{};
    CHECK(!producer.TryBegin(nextKey,successor));
    auto wrong=ticket;++wrong.frameId;CHECK(host.Feedback(wrong,true)==ipc::ChannelResult::Invalid);
    producer.PumpGraphics();CHECK(feedback.consumed==0&&feedback.discarded==0);
    if(!afterPresent)CHECK(host.Feedback(ticket,true)==ipc::ChannelResult::Ok);
    CHECK(!producer.TryBegin(nextKey,successor)); // Ack alone has not reaped GPU ownership.

    graphics::FrameBridgePresentService service;
    const graphics::FrameBridgePresentIdentity owner{1,2,3,4,5,GetCurrentThreadId()};
    graphics::FrameBridgePresentBoundary boundary;boundary.current=owner;boundary.presentedSwapchain=owner.swapchain;
    boundary.enabled=boundary.lifetimeHeld=boundary.bridgeOwnerCurrent=boundary.connected=true;
    unsigned services=0;auto pump=[&]()noexcept {++services;producer.PumpGraphics();};
    {graphics::FrameBridgePresentService::Scope scope(service,true);
        CHECK(!scope.Run(owner,boundary,pump));} // Unrestored native boundary cannot reap.
    CHECK(services==0&&feedback.consumed==0&&!producer.TryBegin(nextKey,successor));
    boundary.nativeStateRestored=true;
    {graphics::FrameBridgePresentService::Scope scope(service,true);
        CHECK(scope.Run(owner,boundary,pump));CHECK(!scope.Run(owner,boundary,pump));
        if(afterPresent){
            CHECK(feedback.consumed==0&&!producer.TryBegin(nextKey,successor));
            // Original Present returns after the exact old pair is acknowledged.
            CHECK(host.Feedback(ticket,true)==ipc::ChannelResult::Ok);
            CHECK(!producer.TryBegin(nextKey,successor));
            CHECK(scope.RunAfterPresent(S_OK,owner,boundary,pump));
            CHECK(!scope.RunAfterPresent(S_OK,owner,boundary,pump));
        }
    }
    CHECK(services==(afterPresent?2u:1u)&&feedback.consumed==1&&feedback.discarded==0&&!feedback.wrongThread);
    CHECK(observed.published==1&&observed.reaped==1&&!observed.wrong);
    CHECK(host.PollRequest(descriptor,wrong)==ipc::ChannelResult::Busy);
    CHECK(producer.Submit(lease,descriptor,ticket)==ipc::ChannelResult::Invalid);
    // Present has released the exact old pair, but has not taken the queued
    // successor. Only the ordinary next visibility admission takes its ORIGINAL
    // request/poses; this extra graphics service does not refresh the lease.
    CHECK(producer.TryBegin(nextKey,successor));
    CHECK(successor.requestId==lease.requestId+1&&successor.channelGeneration==lease.channelGeneration);
    CHECK(successor.native==nextKey&&successor.tracking.generation==next.generation&&
          successor.tracking.predictedNs==next.predictedNs&&successor.tracking.head.position.x==next.head.position.x);
    CHECK(producer.Cancel(successor)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(feedback.consumed==1&&feedback.discarded==0);producer.CloseGraphics();host.Close();
    return 0;
}
struct DeferredObservations {
    unsigned published=0,acknowledged=0,reaped=0;bool wrong=false;
    std::thread::id graphicsThread{},acknowledgementThread{};
    HANDLE acknowledgedEntered=nullptr,acknowledgementRelease=nullptr;
    ipc::FrameLease publishedLease{},acknowledgedLease{};graphics::PairTicket publishedTicket{},acknowledgedTicket{};
    static void Observe(void* context,const ipc::FrameLease& lease,const graphics::PairTicket& ticket,ipc::StagedFrameObservation stage)noexcept {
        auto& o=*static_cast<DeferredObservations*>(context);
        if(stage==ipc::StagedFrameObservation::Acknowledged){
            ++o.acknowledged;o.acknowledgementThread=std::this_thread::get_id();
            o.acknowledgedLease=lease;o.acknowledgedTicket=ticket;
            // Test-only fault injection exposes a concurrent gate owner. Real
            // observers never wait; this hold is bounded even on test failure.
            if(o.acknowledgedEntered){SetEvent(o.acknowledgedEntered);
                if(WaitForSingleObject(o.acknowledgementRelease,1000)!=WAIT_OBJECT_0)o.wrong=true;}
        }else if(std::this_thread::get_id()!=o.graphicsThread)o.wrong=true;
        if(stage==ipc::StagedFrameObservation::Published){
            ++o.published;o.publishedLease=lease;o.publishedTicket=ticket;return;
        }
        if(stage==ipc::StagedFrameObservation::Reaped)++o.reaped;
        if(lease.requestId!=o.publishedLease.requestId||lease.channelGeneration!=o.publishedLease.channelGeneration||
           !(lease.native==o.publishedLease.native)||std::memcmp(&ticket,&o.publishedTicket,sizeof(ticket)))o.wrong=true;
    }
};
runtime::TrackingFrame DeferredTracking(){
    runtime::TrackingFrame t{};t.generation=11;t.spaceGeneration=7;t.predictedNs=1000000000;
    t.headValid=t.focused=true;t.eyes[0].position.x=-.032f;t.eyes[1].position.x=.032f;
    t.fov={math::FovTangents{-1,1,1,-1},math::FovTangents{-1,1,1,-1}};return t;
}
enum class RetirementRoute {Prepare,Pump,Cancel,Close};
int DeferredRetirement(bool consumed,RetirementRoute route){
    Feedback feedback;feedback.graphicsThread=std::this_thread::get_id();
    DeferredObservations observed;observed.graphicsThread=feedback.graphicsThread;
    ipc::FrameChannel host;CHECK(host.CreateHost());
    ipc::StagedFrameProducer producer(feedback,DeferredObservations::Observe,&observed);CHECK(producer.Connect(host.Token()));
    CHECK(producer.SetDeferredGraphicsRetirement(true));
    const runtime::PresentationRequirements requirements{100,120,29,19,4};auto tracking=DeferredTracking();
    CHECK(host.BeginRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);
    ipc::FrameLease first{},next{},unused{};const ipc::NativeFrameKey key{101,201,301},nextKey{101,202,301};
    CHECK(producer.TryBegin(key,first));CHECK(!producer.SetDeferredGraphicsRetirement(false));
    graphics::TextureDescriptor descriptor{};graphics::PairTicket ticket{};MakeTicket(first,descriptor,ticket);
    CHECK(producer.Submit(first,descriptor,ticket)==ipc::ChannelResult::Ok);
    CHECK(!producer.TryBegin(nextKey,next)); // A locally staged ticket is not an acknowledgement.
    producer.PumpGraphics();CHECK(host.PollRequest(descriptor,ticket)==ipc::ChannelResult::Ok);
    ++tracking.generation;tracking.predictedNs+=11000000;tracking.head.position.x=.25f;
    CHECK(host.QueueRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);
    auto wrongTicket=ticket;++wrongTicket.sequence;
    CHECK(host.Feedback(wrongTicket,consumed)==ipc::ChannelResult::Invalid);
    CHECK(!producer.TryBegin(nextKey,next)&&observed.acknowledged==0);
    CHECK(host.Feedback(ticket,consumed)==ipc::ChannelResult::Ok);
    CHECK(!producer.TryBegin(nextKey,next,false)&&observed.acknowledged==0); // Unsupported sharing mode keeps original ordering.
    // IPC contention cannot manufacture a terminal result or retire GPU work.
    const auto mutexName=L"Local\\FrostbiteVR.Control.v8."+host.Token()+L".mutex";
    HANDLE mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,mutexName.c_str());CHECK(mutex);
    CHECK(WaitForSingleObject(mutex,10)==WAIT_OBJECT_0);
    auto blocked=std::async(std::launch::async,[&]{return producer.TryBegin(nextKey,next);});
    CHECK(!blocked.get());CHECK(ReleaseMutex(mutex));CloseHandle(mutex);
    CHECK(observed.acknowledged==0&&feedback.consumed==0&&feedback.discarded==0);
    if(route==RetirementRoute::Prepare){
        observed.acknowledgedEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        observed.acknowledgementRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        CHECK(observed.acknowledgedEntered&&observed.acknowledgementRelease);
    }
    auto admission=std::async(std::launch::async,[&]{return producer.TryBegin(nextKey,next);});
    if(route==RetirementRoute::Prepare){
        CHECK(WaitForSingleObject(observed.acknowledgedEntered,1000)==WAIT_OBJECT_0);
        CHECK(producer.PrepareGraphicsResult(first)==ipc::ChannelResult::Busy);
        CHECK(feedback.consumed==0&&feedback.discarded==0);
        CHECK(SetEvent(observed.acknowledgementRelease));
    }
    CHECK(admission.get());
    if(observed.acknowledgedEntered){CloseHandle(observed.acknowledgedEntered);CloseHandle(observed.acknowledgementRelease);
        observed.acknowledgedEntered=observed.acknowledgementRelease=nullptr;}
    CHECK(observed.acknowledged==1&&observed.reaped==0&&observed.acknowledgementThread!=feedback.graphicsThread);
    CHECK(observed.acknowledgedLease.requestId==first.requestId&&
          std::memcmp(&observed.acknowledgedTicket,&ticket,sizeof(ticket))==0);
    CHECK(feedback.consumed==0&&feedback.discarded==0&&!feedback.wrongThread);
    CHECK(next.requestId==first.requestId+1&&next.channelGeneration==first.channelGeneration&&next.native==nextKey);
    CHECK(next.tracking.generation==tracking.generation&&next.tracking.predictedNs==tracking.predictedNs&&
          next.tracking.head.position.x==tracking.head.position.x);
    CHECK(!producer.TryBegin({101,203,301},unused)); // Only one successor; no second retirement slot.
    graphics::TextureDescriptor nextDescriptor{};graphics::PairTicket nextTicket{};MakeTicket(next,nextDescriptor,nextTicket);
    CHECK(producer.Submit(next,nextDescriptor,nextTicket)==ipc::ChannelResult::Busy);
    CHECK(!producer.SetDeferredGraphicsRetirement(false));
    if(route==RetirementRoute::Close){producer.CloseGraphics();}
    else if(route==RetirementRoute::Cancel){
        CHECK(producer.Cancel(next)==ipc::ChannelResult::Ok);
        CHECK(producer.PrepareGraphicsResult(next)==ipc::ChannelResult::Invalid); // Still drains the old actual outcome.
        producer.PumpGraphics();CHECK(host.PollRequest(nextDescriptor,nextTicket)==ipc::ChannelResult::Timeout);
    }else{
        if(route==RetirementRoute::Prepare){
            auto wrong=next;++wrong.native.owner;
            CHECK(producer.PrepareGraphicsResult(wrong)==ipc::ChannelResult::Invalid); // Drain first, authorize only exact successor.
        }else producer.PumpGraphics();
        CHECK(producer.PrepareGraphicsResult(next)==ipc::ChannelResult::Ok);
    }
    CHECK(feedback.consumed==(consumed?1u:0u)&&feedback.discarded==(consumed?0u:1u));
    CHECK(std::memcmp(&feedback.lastTicket,&ticket,sizeof(ticket))==0&&observed.reaped==1&&!observed.wrong);
    if(route==RetirementRoute::Prepare||route==RetirementRoute::Pump){
        CHECK(producer.Submit(first,descriptor,ticket)==ipc::ChannelResult::Invalid);
        CHECK(producer.Submit(next,nextDescriptor,nextTicket)==ipc::ChannelResult::Ok);producer.PumpGraphics();
        CHECK(host.PollRequest(nextDescriptor,nextTicket)==ipc::ChannelResult::Ok);
        CHECK(host.Feedback(nextTicket,true)==ipc::ChannelResult::Ok);producer.PumpGraphics();
        CHECK(feedback.consumed==(consumed?2u:1u)&&feedback.discarded==(consumed?0u:1u));
        CHECK(observed.published==2&&observed.acknowledged==1&&observed.reaped==2);
    }
    const auto total=feedback.consumed.load()+feedback.discarded.load();
    producer.PumpGraphics();producer.CloseGraphics();producer.CloseGraphics();
    CHECK(feedback.consumed+feedback.discarded==total&&!feedback.wrongThread&&!observed.wrong);
    CHECK(producer.PrepareGraphicsResult(next)==ipc::ChannelResult::Invalid);host.Close();return 0;
}
int DeferredSuccessorCancelledOrExpired(bool expired){
    Feedback feedback;feedback.graphicsThread=std::this_thread::get_id();
    DeferredObservations observed;observed.graphicsThread=feedback.graphicsThread;
    ipc::FrameChannel host;CHECK(host.CreateHost());
    ipc::StagedFrameProducer producer(feedback,DeferredObservations::Observe,&observed);CHECK(producer.Connect(host.Token()));
    CHECK(producer.SetDeferredGraphicsRetirement(true));
    const runtime::PresentationRequirements requirements{100,120,29,19,4};auto tracking=DeferredTracking();
    CHECK(host.BeginRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);
    ipc::FrameLease first{},next{};CHECK(producer.TryBegin({101,201,301},first));
    graphics::TextureDescriptor descriptor{};graphics::PairTicket ticket{};MakeTicket(first,descriptor,ticket);
    CHECK(producer.Submit(first,descriptor,ticket)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(host.PollRequest(descriptor,ticket)==ipc::ChannelResult::Ok);++tracking.generation;
    CHECK(host.QueueRequest(requirements,tracking,expired?1:200)==ipc::ChannelResult::Ok);
    if(expired)Sleep(3);else CHECK(host.CancelRequest()==ipc::ChannelResult::Busy);
    CHECK(host.Feedback(ticket,true)==ipc::ChannelResult::Ok);
    auto admission=std::async(std::launch::async,[&]{return producer.TryBegin({101,202,301},next);});
    CHECK(!admission.get());CHECK(observed.acknowledged==1&&observed.reaped==0&&feedback.consumed==0);
    if(expired)CHECK(host.PollRequest(descriptor,ticket)==ipc::ChannelResult::Timeout);
    // A cancelled/expired original queued sample is never renewed. A new host
    // request can still be taken while the old immutable retirement is owed.
    ++tracking.generation;tracking.predictedNs+=22000000;
    CHECK(host.BeginRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);
    CHECK(producer.TryBegin({101,202,301},next));CHECK(next.requestId==first.requestId+2);
    CHECK(next.tracking.generation==tracking.generation&&observed.acknowledged==1);
    producer.CloseGraphics();producer.CloseGraphics();
    CHECK(feedback.consumed==1&&feedback.discarded==0&&observed.reaped==1&&!feedback.wrongThread&&!observed.wrong);
    host.Close();return 0;
}
// Real mapped v9 channel + actual staged adapter. No D3D/device dependency:
// resource IDs below are opaque protocol fixtures, never opened as handles.
struct PipelineFeedback final:runtime::IFrameProvider {
    std::thread::id graphicsOwner=std::this_thread::get_id();
    std::array<graphics::PairTicket,4> tickets{};std::array<bool,4> outcomes{};
    unsigned calls=0;bool wrong=false;HANDLE entered=nullptr,mutexHeld=nullptr;
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    void PairConsumed(const graphics::PairTicket& ticket,bool consumed)noexcept override {
        if(std::this_thread::get_id()!=graphicsOwner||calls>=tickets.size()){wrong=true;return;}
        tickets[calls]=ticket;outcomes[calls++]=consumed;
        if(entered){SetEvent(entered);if(WaitForSingleObject(mutexHeld,1000)!=WAIT_OBJECT_0)wrong=true;entered=nullptr;}
    }
};
struct PipelineObservations {
    std::thread::id graphicsOwner=std::this_thread::get_id();
    std::array<ipc::FrameLease,3> leases{};std::array<graphics::PairTicket,3> tickets{};
    unsigned expectedCount=2,published=0,reaped=0;bool wrong=false;
    static void Observe(void* context,const ipc::FrameLease& lease,const graphics::PairTicket& ticket,ipc::StagedFrameObservation stage)noexcept {
        auto& o=*static_cast<PipelineObservations*>(context);
        if(std::this_thread::get_id()!=o.graphicsOwner||lease.sourceSlotCount!=o.expectedCount||lease.sourceSlot>=o.expectedCount){o.wrong=true;return;}
        if(stage==ipc::StagedFrameObservation::Published){++o.published;o.leases[lease.sourceSlot]=lease;o.tickets[lease.sourceSlot]=ticket;}
        else if(stage==ipc::StagedFrameObservation::Reaped){++o.reaped;
            const auto& p=o.leases[lease.sourceSlot];
            if(p.requestId!=lease.requestId||p.channelGeneration!=lease.channelGeneration||!(p.native==lease.native)||
                std::memcmp(&o.tickets[lease.sourceSlot],&ticket,sizeof(ticket)))o.wrong=true;
        }else o.wrong=true; // No invented early acknowledgement observer.
    }
};
void MakePipelinePair(const ipc::FrameLease& lease,graphics::TextureDescriptor& descriptor,graphics::PairTicket& ticket,graphics::SceneDepthFrame& depth){
    descriptor={};ticket={};depth={};MakeTicket(lease,descriptor,ticket);
    descriptor.sharing=graphics::TextureSharing::LegacyFenced;
    const auto base=std::uint64_t(1000+lease.sourceSlot*100);
    const std::array<std::uint64_t,2> colors{base+1,base+2};
    std::memcpy(descriptor.session.data(),colors.data(),sizeof(colors));ticket.session=descriptor.session;ticket.sequence=lease.native.frameId;
    depth.color=ticket;depth.flags=graphics::SceneDepthCanonicalClip;depth.width=descriptor.width;depth.height=descriptor.height;
    depth.samples=1;depth.format=44;depth.adapterLow=descriptor.adapterLow;depth.adapterHigh=descriptor.adapterHigh;depth.resourceGeneration=1;
    depth.resources={{{base+3,base+4},{base+5,base+6}}};
}
int PipelineMappedOwnership(){
    PipelineFeedback feedback;PipelineObservations observations;
    ipc::FrameChannel host,wrongMode;CHECK(host.CreateHost(true));CHECK(host.SourceSlotCount()==2);
    CHECK(!wrongMode.ConnectProducer(host.Token())); // No v8 fallback/name alias.
    ipc::StagedFrameProducer producer(feedback,PipelineObservations::Observe,&observations);
    CHECK(producer.Connect(host.Token(),true)&&producer.SourceSlotCount()==2);
    CHECK(!producer.SetDeferredGraphicsRetirement(true));
    const runtime::PresentationRequirements requirements{100,120,29,19,4};auto tracking=DeferredTracking();
    CHECK(host.BeginRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);
    ipc::FrameLease a{},b{},c{};CHECK(producer.TryBegin({101,201,301},a));CHECK(a.sourceSlot==0&&a.sourceSlotCount==2);
    graphics::TextureDescriptor da{},db{},out{};graphics::PairTicket ta{},tb{},got{};graphics::SceneDepthFrame za{},zb{},depth{};
    MakePipelinePair(a,da,ta,za);
    const graphics::SceneDepthFrame absent{};CHECK(producer.Submit(a,da,ta,&absent)==ipc::ChannelResult::Invalid);
    auto bad=za;bad.samples=8;CHECK(producer.Submit(a,da,ta,&bad)==ipc::ChannelResult::Invalid);
    bad=za;bad.quality=1;CHECK(producer.Submit(a,da,ta,&bad)==ipc::ChannelResult::Invalid);
    bad=za;++bad.color.trackingGeneration;CHECK(producer.Submit(a,da,ta,&bad)==ipc::ChannelResult::Invalid);
    std::array<graphics::BodyPropEye,2> props{};
    for(auto& eye:props)eye.ammo={1,1000000000,1100000000,1,2,tracking.spaceGeneration,24,111,30};
    CHECK(producer.SetBodyPropPair(a,props)==ipc::ChannelResult::Ok);
    CHECK(producer.Submit(a,da,ta,&za)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok&&std::memcmp(&got,&ta,sizeof(got))==0);
    runtime::SourceSlotIdentity first{};CHECK(host.ReadSourceSlot(ta,first)&&first.slot==a.sourceSlot&&first.requestId==a.requestId&&first.slotCount==2);
    ++tracking.generation;tracking.predictedNs+=11000000;tracking.head.position.x=.2f;
    CHECK(host.QueueRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);
    auto take=std::async(std::launch::async,[&]{return producer.TryBegin({101,202,301},b);});CHECK(take.get());
    CHECK(b.sourceSlot==1&&b.requestId==a.requestId+1&&b.tracking.head.position.x==.2f&&feedback.calls==0);
    CHECK(!producer.TryBegin({101,203,301},c));
    MakePipelinePair(b,db,tb,zb);CHECK(producer.Submit(b,db,tb,&zb)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Busy); // B cannot become a second delivered lease.
    // Metadata authority is the immutable A snapshot even while B is Ready and
    // another thread holds the channel mutex. Contention does not erase depth.
    const auto mutexName=L"Local\\FrostbiteVR.Control.v9."+host.Token()+L".mutex";
    HANDLE mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,mutexName.c_str());CHECK(mutex);
    HANDLE held=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);CHECK(held&&release);
    auto hold=std::async(std::launch::async,[&]{const auto acquired=WaitForSingleObject(mutex,1000)==WAIT_OBJECT_0;
        SetEvent(held);if(!acquired)return false;const auto released=WaitForSingleObject(release,1000)==WAIT_OBJECT_0;ReleaseMutex(mutex);return released;});
    CHECK(WaitForSingleObject(held,1000)==WAIT_OBJECT_0);
    graphics::BodyPropFrame body{};runtime::SourceSlotIdentity slot{};
    CHECK(host.ReadSourceSlot(ta,slot)&&slot.channelSession==first.channelSession&&slot.slot==0);
    CHECK(host.ReadSceneDepth(da,ta,depth)&&depth.resources==za.resources);
    CHECK(host.ReadBodyProps(ta,body)&&body.eyes[0].ammo.loaded==24);
    CHECK(!host.ReadSourceSlot(tb,slot));
    auto wrong=ta;++wrong.sequence;CHECK(host.Feedback(wrong,true)==ipc::ChannelResult::Invalid);
    CHECK(host.Feedback(ta,true)==ipc::ChannelResult::Busy);
    CHECK(!host.ReadSourceSlot(ta,slot)&&!host.ReadSceneDepth(da,ta,depth)); // Pending ACK revokes reads immediately.
    CHECK(SetEvent(release));CHECK(hold.get());CloseHandle(held);CloseHandle(release);
    CHECK(host.FlushFeedback()==ipc::ChannelResult::Ok);
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok&&std::memcmp(&got,&tb,sizeof(got))==0);
    CHECK(host.ReadSourceSlot(tb,slot)&&slot.slot==1&&slot.channelSession==first.channelSession);
    ++tracking.generation;tracking.predictedNs+=11000000;
    CHECK(host.QueueRequest(requirements,tracking,200)==ipc::ChannelResult::Ok); // Immutable C waits for A's actual retirement.
    CHECK(!producer.TryBegin({101,203,301},c)); // No third source or camera.
    // Force commit contention AFTER A's real callback. The next Pump may commit
    // but must never call that callback twice; A remains unavailable in between.
    HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);held=CreateEventW(nullptr,TRUE,FALSE,nullptr);release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    CHECK(entered&&held&&release);feedback.entered=entered;feedback.mutexHeld=held;
    auto commitHold=std::async(std::launch::async,[&]{if(WaitForSingleObject(entered,1000)!=WAIT_OBJECT_0)return false;
        if(WaitForSingleObject(mutex,1000)!=WAIT_OBJECT_0){SetEvent(held);return false;}SetEvent(held);
        const auto released=WaitForSingleObject(release,1000)==WAIT_OBJECT_0;ReleaseMutex(mutex);return released;});
    producer.PumpGraphics();CHECK(feedback.calls==1&&feedback.outcomes[0]);
    CHECK(SetEvent(release));CHECK(commitHold.get());CloseHandle(entered);CloseHandle(held);CloseHandle(release);
    CHECK(host.QueueRequest(requirements,tracking,200)==ipc::ChannelResult::Busy);
    CHECK(!producer.TryBegin({101,203,301},c)); // A callback ran, but its commit is still pending.
    producer.PumpGraphics();CHECK(feedback.calls==1);
    CHECK(producer.TryBegin({101,203,301},c)&&c.sourceSlot==a.sourceSlot&&c.requestId==b.requestId+1);
    CHECK(c.tracking.generation==tracking.generation&&c.tracking.predictedNs==tracking.predictedNs);
    CHECK(producer.Cancel(a)==ipc::ChannelResult::Invalid);
    CHECK(host.Feedback(ta,true)==ipc::ChannelResult::Invalid);
    CHECK(host.Feedback(tb,false)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(feedback.calls==2&&!feedback.outcomes[1]&&std::memcmp(&feedback.tickets[1],&tb,sizeof(tb))==0);
    CHECK(producer.Cancel(c)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Timeout);
    CHECK(observations.published==2&&observations.reaped==2&&!observations.wrong);
    producer.CloseGraphics();producer.CloseGraphics();host.Close();CloseHandle(mutex);
    CHECK(feedback.calls==2&&!feedback.wrong);return 0;
}
int PipelineCancellationAndClose(){
    // Close must not publish B's staged source. Other cases cancel B while A
    // remains Delivered; only exact ACK can retire A positively.
    for(unsigned stage=0;stage<4;++stage){
        PipelineFeedback feedback;ipc::FrameChannel host;CHECK(host.CreateHost(true));ipc::StagedFrameProducer producer(feedback);CHECK(producer.Connect(host.Token(),true));
        const runtime::PresentationRequirements requirements{100,120,29,19,4};auto tracking=DeferredTracking();
        ipc::FrameLease a{},b{};graphics::TextureDescriptor da{},db{},out{};graphics::PairTicket ta{},tb{},got{};graphics::SceneDepthFrame za{},zb{};
        CHECK(host.BeginRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);CHECK(producer.TryBegin({101,201,301},a));MakePipelinePair(a,da,ta,za);
        CHECK(producer.Submit(a,da,ta,&za)==ipc::ChannelResult::Ok);producer.PumpGraphics();CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok);
        ++tracking.generation;CHECK(host.QueueRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);
        if(stage){CHECK(producer.TryBegin({101,202,301},b));MakePipelinePair(b,db,tb,zb);}
        if(stage>=2)CHECK(producer.Submit(b,db,tb,&zb)==ipc::ChannelResult::Ok);
        if(stage==3)producer.PumpGraphics();
        CHECK(host.CancelRequest()==ipc::ChannelResult::Busy);
        if(stage==2){producer.CloseGraphics();CHECK(feedback.calls==2&&!feedback.outcomes[0]&&!feedback.outcomes[1]);}
        else{
            if(stage==1)CHECK(producer.Cancel(b)==ipc::ChannelResult::Ok);
            producer.PumpGraphics();CHECK(feedback.calls==(stage==3?1u:0u));
            CHECK(host.Feedback(ta,true)==ipc::ChannelResult::Ok);producer.PumpGraphics();
            CHECK(feedback.calls==(stage==3?2u:1u));CHECK(feedback.outcomes[feedback.calls-1]);producer.CloseGraphics();
        }
        const auto calls=feedback.calls;producer.CloseGraphics();host.Close();CHECK(feedback.calls==calls&&!feedback.wrong);
    }
    // Explicit endpoint mismatch also rejects v9â†’v8 instead of silently downgrading.
    ipc::FrameChannel old,wrong;CHECK(old.CreateHost());CHECK(!wrong.ConnectProducer(old.Token(),true));old.Close();return 0;
}
int PipelineLifetimeMismatch(){
    for(unsigned mismatch=0;mismatch<3;++mismatch){
        PipelineFeedback feedback;ipc::FrameChannel host;CHECK(host.CreateHost(true));ipc::StagedFrameProducer producer(feedback);CHECK(producer.Connect(host.Token(),true));
        runtime::PresentationRequirements requirements{100,120,29,19,4};auto tracking=DeferredTracking();
        ipc::FrameLease a{},b{};graphics::TextureDescriptor descriptor{},out{};graphics::PairTicket ticket{},got{};graphics::SceneDepthFrame depth{};
        CHECK(host.BeginRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);CHECK(producer.TryBegin({101,201,301},a));MakePipelinePair(a,descriptor,ticket,depth);
        CHECK(producer.Submit(a,descriptor,ticket,&depth)==ipc::ChannelResult::Ok);producer.PumpGraphics();CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok);
        CHECK(host.Feedback(ticket,true)==ipc::ChannelResult::Ok);producer.PumpGraphics();CHECK(feedback.calls==1);
        auto native=ipc::NativeFrameKey{101,202,301};if(mismatch==0)++native.owner;else if(mismatch==1)++native.deviceEpoch;else ++requirements.width;
        ++tracking.generation;CHECK(host.BeginRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);CHECK(!producer.TryBegin(native,b));
        producer.PumpGraphics();CHECK(!producer.TryBegin({101,203,301},b));producer.CloseGraphics();host.Close();CHECK(feedback.calls==1&&!feedback.wrong);
    }return 0;
}
int PipelineDeferredRequestCancellation(bool expire){
    PipelineFeedback feedback;ipc::FrameChannel host;CHECK(host.CreateHost(true));ipc::StagedFrameProducer producer(feedback);CHECK(producer.Connect(host.Token(),true));
    const runtime::PresentationRequirements requirements{100,120,29,19,4};auto tracking=DeferredTracking();
    ipc::FrameLease a{},b{},c{};graphics::TextureDescriptor da{},db{},out{};graphics::PairTicket ta{},tb{},got{};graphics::SceneDepthFrame za{},zb{};
    CHECK(host.BeginRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);CHECK(producer.TryBegin({101,201,301},a));MakePipelinePair(a,da,ta,za);
    CHECK(producer.Submit(a,da,ta,&za)==ipc::ChannelResult::Ok);producer.PumpGraphics();CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok);
    ++tracking.generation;CHECK(host.QueueRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);CHECK(producer.TryBegin({101,202,301},b));MakePipelinePair(b,db,tb,zb);
    CHECK(producer.Submit(b,db,tb,&zb)==ipc::ChannelResult::Ok);producer.PumpGraphics();CHECK(host.Feedback(ta,true)==ipc::ChannelResult::Ok);
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok);++tracking.generation;
    CHECK(host.QueueRequest(requirements,tracking,expire?1:200)==ipc::ChannelResult::Ok);
    CHECK(!producer.TryBegin({101,203,301},c)&&feedback.calls==0);
    if(expire)Sleep(3);
    else CHECK(host.CancelRequest()==ipc::ChannelResult::Busy);
    CHECK(host.Feedback(tb,false)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(feedback.calls==2&&feedback.outcomes[0]&&!feedback.outcomes[1]);
    CHECK(!producer.TryBegin({101,203,301},c)); // Old C was never renewed or promoted.
    if(expire)CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Timeout);
    ++tracking.generation;tracking.predictedNs+=33000000;
    CHECK(host.BeginRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);CHECK(producer.TryBegin({101,203,301},c));
    CHECK(c.requestId==b.requestId+2&&c.tracking.generation==tracking.generation);
    CHECK(producer.Cancel(c)==ipc::ChannelResult::Ok);producer.PumpGraphics();producer.CloseGraphics();host.Close();
    CHECK(feedback.calls==2&&!feedback.wrong);return 0;
}
int PipelineRemoteSuccessor(){
    PipelineFeedback feedback;ipc::RemoteFrameProvider host;CHECK(host.Create(true)&&host.SetRequestLifetime(200));
    ipc::StagedFrameProducer producer(feedback);CHECK(producer.Connect(host.Token(),true));
    const runtime::PresentationRequirements requirements{100,120,29,19,4};auto current=DeferredTracking();runtime::TrackingFrame rendered{};
    graphics::TextureDescriptor da{},db{},out{};graphics::PairTicket ta{},tb{},got{};graphics::SceneDepthFrame za{},zb{};
    ipc::FrameLease a{},b{},c{};
    CHECK(!host.TryGetCompletedPair(requirements,current,rendered,out,got));
    CHECK(producer.TryBegin({101,201,301},a));MakePipelinePair(a,da,ta,za);
    CHECK(producer.Submit(a,da,ta,&za)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    ++current.generation;current.predictedNs+=11000000;
    CHECK(host.TryGetCompletedPair(requirements,current,rendered,out,got)&&rendered.generation==a.tracking.generation);
    CHECK(host.Statistics().requested==2&&host.Statistics().completed==1&&feedback.calls==0);
    CHECK(producer.TryBegin({101,202,301},b)&&b.tracking.generation==current.generation);
    MakePipelinePair(b,db,tb,zb);CHECK(producer.Submit(b,db,tb,&zb)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(!host.TryGetCompletedPair(requirements,current,rendered,out,got)); // Serial delivery still holds A.
    host.PairConsumed(ta,true);++current.generation;current.predictedNs+=11000000;
    CHECK(host.TryGetCompletedPair(requirements,current,rendered,out,got)&&rendered.generation==b.tracking.generation);
    CHECK(host.Statistics().requested==3&&host.Statistics().completed==2&&feedback.calls==0);
    runtime::SourceSlotIdentity source{};CHECK(host.ReadSourceSlot(tb,source)&&source.slot==b.sourceSlot&&source.requestId==b.requestId);
    CHECK(!producer.TryBegin({101,203,301},c)); // The real provider queued C; A has not retired yet.
    producer.PumpGraphics();CHECK(feedback.calls==1&&feedback.outcomes[0]);
    CHECK(producer.TryBegin({101,203,301},c)&&c.tracking.generation==current.generation&&c.requestId==b.requestId+1);
    host.Suspend();CHECK(producer.Cancel(c)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    host.PairConsumed(tb,true);host.ServiceFeedback();producer.PumpGraphics();
    CHECK(feedback.calls==2&&feedback.outcomes[1]&&host.Statistics().timeouts==0);
    producer.CloseGraphics();host.Close();CHECK(feedback.calls==2&&!feedback.wrong);return 0;
}
int PipelineFaultBoundary(){
    PipelineFeedback feedback;unsigned published=0;ipc::FrameChannel host;CHECK(host.CreateHost(true));
    ipc::StagedFrameProducer producer(feedback,[](void* context,const ipc::FrameLease&,const graphics::PairTicket&,ipc::StagedFrameObservation stage)noexcept{
        if(stage==ipc::StagedFrameObservation::Published)++*static_cast<unsigned*>(context);
    },&published);CHECK(producer.Connect(host.Token(),true));
    const runtime::PresentationRequirements requirements{100,120,29,19,4};auto tracking=DeferredTracking();
    ipc::FrameLease a{},b{},unused{};graphics::TextureDescriptor da{},db{},out{};graphics::PairTicket ta{},tb{},got{};graphics::SceneDepthFrame za{},zb{};
    CHECK(host.BeginRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);CHECK(producer.TryBegin({101,201,301},a));MakePipelinePair(a,da,ta,za);
    CHECK(producer.Submit(a,da,ta,&za)==ipc::ChannelResult::Ok);producer.PumpGraphics();CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok);
    ++tracking.generation;CHECK(host.QueueRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);CHECK(producer.TryBegin({101,202,301},b));MakePipelinePair(b,db,tb,zb);
    CHECK(producer.Submit(b,db,tb,&zb)==ipc::ChannelResult::Ok); // Restored B is local Publishing, not Ready.
    const auto name=L"Local\\FrostbiteVR.Control.v9."+host.Token()+L".mutex";
    HANDLE mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,name.c_str());CHECK(mutex);
    HANDLE held=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);CHECK(held&&release);
    auto holding=std::async(std::launch::async,[&]{if(WaitForSingleObject(mutex,1000)!=WAIT_OBJECT_0){SetEvent(held);return false;}
        SetEvent(held);const auto okay=WaitForSingleObject(release,1000)==WAIT_OBJECT_0;ReleaseMutex(mutex);return okay;});
    CHECK(WaitForSingleObject(held,1000)==WAIT_OBJECT_0);
    CHECK(!producer.TryBegin({999,203,301},unused)); // Sticky close intent; no admission-thread graphics calls.
    CHECK(feedback.calls==0&&published==1);
    runtime::SourceSlotIdentity source{};CHECK(host.ReadSourceSlot(ta,source));
    CHECK(SetEvent(release));CHECK(holding.get());CloseHandle(held);CloseHandle(release);
    // Cross-process invalidation occurs at the next lock commit, not earlier.
    producer.PumpGraphics();CHECK(feedback.calls==1&&!feedback.outcomes[0]&&std::memcmp(&feedback.tickets[0],&tb,sizeof(tb))==0);
    CHECK(published==1&&host.PollRequest(out,got)==ipc::ChannelResult::Closed);
    CHECK(host.ReadSourceSlot(ta,source)); // Already Delivered A still owns its source.
    CHECK(!producer.TryBegin({101,204,301},unused));
    CHECK(host.Feedback(ta,true)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(feedback.calls==2&&feedback.outcomes[1]&&std::memcmp(&feedback.tickets[1],&ta,sizeof(ta))==0);
    producer.CloseGraphics();host.Close();CloseHandle(mutex);CHECK(feedback.calls==2&&!feedback.wrong);return 0;
}

int PipelineOptionalAttachments(){
    PipelineFeedback feedback;ipc::FrameChannel host;CHECK(host.CreateHost(true));
    ipc::StagedFrameProducer producer(feedback);CHECK(producer.Connect(host.Token(),true));
    const runtime::PresentationRequirements requirements{100,120,29,19,4};auto tracking=DeferredTracking();
    graphics::TextureDescriptor descriptor{},out{};graphics::PairTicket ticket{},got{},previous{};
    graphics::SceneDepthFrame depth{},read{};runtime::SourceSlotIdentity identity{};
    // Reuse the same source slot after exact retirement. Non-null malformed
    // metadata never means absence, including a canonical empty attachment.
    for(unsigned step=0;step<4;++step){
        CHECK(host.BeginRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);
        ipc::FrameLease lease{};CHECK(producer.TryBegin({101,201+step,301},lease));
        MakePipelinePair(lease,descriptor,ticket,depth);
        graphics::SceneDepthFrame bad{};
        CHECK(graphics::SceneDepthAbsent(bad));
        CHECK(producer.Submit(lease,descriptor,ticket,&bad)==ipc::ChannelResult::Invalid);
        bad.resources[0][0]=7;CHECK(!graphics::SceneDepthAbsent(bad));
        CHECK(producer.Submit(lease,descriptor,ticket,&bad)==ipc::ChannelResult::Invalid);
        bad=depth;bad.resources[1][1]=0;CHECK(producer.Submit(lease,descriptor,ticket,&bad)==ipc::ChannelResult::Invalid);
        bad=depth;bad.resources[1][1]=bad.resources[0][0];CHECK(producer.Submit(lease,descriptor,ticket,&bad)==ipc::ChannelResult::Invalid);
        bad=depth;bad.resources[0][0]=graphics::LegacyResourceIds(descriptor)[0];
        CHECK(producer.Submit(lease,descriptor,ticket,&bad)==ipc::ChannelResult::Invalid);
        const bool present=step==0||step==3;
        CHECK(producer.Submit(lease,descriptor,ticket,present?&depth:nullptr)==ipc::ChannelResult::Ok);
        producer.PumpGraphics();CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok);
        CHECK(host.ReadSourceSlot(got,identity)&&identity.depthPresent==present&&identity.requestId==lease.requestId);
        read=depth;CHECK(host.ReadSceneDepth(out,got,read)==present);
        CHECK(present?graphics::SceneDepthMatches(read,out,got):graphics::SceneDepthAbsent(read));
        if(step)CHECK(!host.ReadSourceSlot(previous,identity)&&!host.ReadSceneDepth(out,previous,read));
        CHECK(host.Feedback(got,true)==ipc::ChannelResult::Ok);
        CHECK(!host.ReadSourceSlot(got,identity));producer.PumpGraphics();
        CHECK(feedback.calls==step+1&&feedback.outcomes[step]&&!feedback.wrong);
        previous=got;++tracking.generation;tracking.predictedNs+=11000000;
    }
    producer.CloseGraphics();host.Close();CHECK(feedback.calls==4&&!feedback.wrong);return 0;
}
int PipelineOptionalAliases(bool lookahead=false){
    // Direct mapped-channel validation, independent of the staged prevalidator.
    // An incoming color-only pair still cannot alias ANY live source of A.
    for(bool firstHasDepth:{false,true}){
        ipc::FrameChannel host,native;CHECK(host.CreateHost(true,lookahead)&&native.ConnectProducer(host.Token(),true));
        const runtime::PresentationRequirements requirements{100,120,29,19,4};auto tracking=DeferredTracking();
        CHECK((lookahead?host.SubmitLookaheadRequest(requirements,tracking,200):
            host.BeginRequest(requirements,tracking,200))==ipc::ChannelResult::Ok);
        ipc::FrameRequest request{};ipc::PipelineSlotKey a{},b{};
        CHECK(native.TryTakePipeline(request,a)==ipc::ChannelResult::Ok);
        ipc::FrameLease lease{};CHECK(ipc::Decode(request,lease.requirements,lease.tracking));
        lease.native={101,201,301};lease.sourceSlot=a.slot;
        graphics::TextureDescriptor da{},db{},out{};graphics::PairTicket ta{},tb{},got{};graphics::SceneDepthFrame za{},zb{},read{};
        MakePipelinePair(lease,da,ta,za);
        const graphics::SceneDepthFrame empty{};
        CHECK(native.PublishPipeline(a,da,ta,nullptr,&empty)==ipc::ChannelResult::Invalid);
        auto bad=za;bad.samples=8;CHECK(native.PublishPipeline(a,da,ta,nullptr,&bad)==ipc::ChannelResult::Invalid);
        bad=za;bad.quality=1;CHECK(native.PublishPipeline(a,da,ta,nullptr,&bad)==ipc::ChannelResult::Invalid);
        bad=za;bad.resources[1][1]=0;CHECK(native.PublishPipeline(a,da,ta,nullptr,&bad)==ipc::ChannelResult::Invalid);
        CHECK(native.PublishPipeline(a,da,ta,nullptr,firstHasDepth?&za:nullptr)==ipc::ChannelResult::Ok);
        if(!lookahead)CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok);
        ++tracking.generation;tracking.predictedNs+=11000000;
        CHECK((lookahead?host.SubmitLookaheadRequest(requirements,tracking,200):
            host.QueueRequest(requirements,tracking,200))==ipc::ChannelResult::Ok);
        CHECK(native.TryTakePipeline(request,b)==ipc::ChannelResult::Ok&&a.slot!=b.slot);
        CHECK(ipc::Decode(request,lease.requirements,lease.tracking));lease.native.frameId=202;lease.sourceSlot=b.slot;
        MakePipelinePair(lease,db,tb,zb);
        std::array<std::uint64_t,6> held{graphics::LegacyResourceIds(da)[0],graphics::LegacyResourceIds(da)[1],
            za.resources[0][0],za.resources[0][1],za.resources[1][0],za.resources[1][1]};
        for(unsigned i=0;i<(firstHasDepth?6u:2u);++i){
            auto alias=db;auto aliasTicket=tb;auto ids=graphics::LegacyResourceIds(db);ids[0]=held[i];
            std::memcpy(alias.session.data(),ids.data(),sizeof(ids));aliasTicket.session=alias.session;
            CHECK(native.PublishPipeline(b,alias,aliasTicket,nullptr,nullptr)==ipc::ChannelResult::Invalid);
            bad=zb;bad.resources[0][0]=held[i];
            CHECK(native.PublishPipeline(b,db,tb,nullptr,&bad)==ipc::ChannelResult::Invalid);
        }
        CHECK(native.PublishPipeline(b,db,tb,nullptr,nullptr)==ipc::ChannelResult::Ok);
        if(lookahead)CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok&&std::memcmp(&got,&ta,sizeof(got))==0);
        runtime::SourceSlotIdentity identity{};
        CHECK(host.ReadSourceSlot(ta,identity)&&identity.depthPresent==firstHasDepth);
        CHECK(host.ReadSceneDepth(da,ta,read)==firstHasDepth);
        CHECK(host.Feedback(ta,true)==ipc::ChannelResult::Ok);
        ipc::PipelineRetirement retirement{};
        CHECK(native.ClaimPipelineRetirement(a,retirement)==ipc::ChannelResult::Ok&&retirement.hasSource&&retirement.outcome==ipc::Outcome::Consumed);
        CHECK(native.FinishPipelineRetirement(retirement)==ipc::ChannelResult::Ok);
        CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok);
        CHECK(host.ReadSourceSlot(tb,identity)&&!identity.depthPresent&&identity.slot==b.slot);
        CHECK(!host.ReadSceneDepth(db,tb,read)&&graphics::SceneDepthAbsent(read));
        CHECK(host.Feedback(tb,false)==ipc::ChannelResult::Ok);
        CHECK(native.ClaimPipelineRetirement(b,retirement)==ipc::ChannelResult::Ok&&retirement.hasSource&&retirement.outcome==ipc::Outcome::Discarded);
        CHECK(native.FinishPipelineRetirement(retirement)==ipc::ChannelResult::Ok);
        native.Close();host.Close();
    }
    return 0;
}

int PipelineLookaheadFifo(unsigned when){
    // The three admissions differ only in A's state: Requested, Rendering, or
    // Ready but not Delivered. No graphics resource is represented by B until
    // the ordinary next native admission and the existing Submit/Pump sequence.
    PipelineFeedback feedback;PipelineObservations observations;
    ipc::FrameChannel host;CHECK(host.CreateHost(true,true));
    ipc::StagedFrameProducer producer(feedback,PipelineObservations::Observe,&observations);
    CHECK(producer.Connect(host.Token(),true));
    const runtime::PresentationRequirements requirements{100,120,29,19,4};const auto first=DeferredTracking();
    auto next=first;++next.generation;next.predictedNs+=11000000;next.head.position.x=.25f;
    next.eyes[0].position.x=-.041f;next.eyes[1].position.x=.037f;next.fov[0].left=-1.2f;next.fov[1].right=1.3f;
    const auto originalNext=next;
    CHECK(host.BeginRequest(requirements,first,200)==ipc::ChannelResult::Invalid);
    CHECK(host.QueueRequest(requirements,first,200)==ipc::ChannelResult::Invalid);
    CHECK(host.SubmitLookaheadRequest(requirements,first,200)==ipc::ChannelResult::Ok);
    if(when==0)CHECK(host.SubmitLookaheadRequest(requirements,next,200)==ipc::ChannelResult::Ok);
    ipc::FrameLease a{},b{},unused{};CHECK(producer.TryBegin({101,201,301},a));
    CHECK(a.requestId==1&&a.tracking.generation==first.generation&&a.tracking.predictedNs==first.predictedNs);
    if(when==1)CHECK(host.SubmitLookaheadRequest(requirements,next,200)==ipc::ChannelResult::Ok);
    CHECK(!producer.TryBegin({101,202,301},unused)); // One active camera, even with B Requested.
    graphics::TextureDescriptor da{},db{},out{};graphics::PairTicket ta{},tb{},got{};graphics::SceneDepthFrame za{},zb{},depth{};
    MakePipelinePair(a,da,ta,za);CHECK(producer.Submit(a,da,ta,&za)==ipc::ChannelResult::Ok);
    CHECK(!producer.TryBegin({101,202,301},unused)); // Locally staged is not actual publication.
    producer.PumpGraphics();CHECK(observations.published==1&&observations.reaped==0&&feedback.calls==0);
    if(when==2)CHECK(host.SubmitLookaheadRequest(requirements,next,200)==ipc::ChannelResult::Ok);
    ++next.generation;next.predictedNs+=11000000;next.head.position.x=99;next.eyes[0].position.x=-9;
    CHECK(host.SubmitLookaheadRequest(requirements,next,200)==ipc::ChannelResult::Busy);
    // Admission can run off the graphics owner, but cannot invoke its feedback.
    auto admission=std::async(std::launch::async,[&]{return producer.TryBegin({101,202,301},b);});CHECK(admission.get());
    CHECK(b.requestId==a.requestId+1&&b.channelGeneration==a.channelGeneration&&b.sourceSlot!=a.sourceSlot);
    CHECK(b.tracking.generation==originalNext.generation&&b.tracking.spaceGeneration==originalNext.spaceGeneration&&
          b.tracking.predictedNs==originalNext.predictedNs&&b.tracking.head.position.x==originalNext.head.position.x&&
          b.tracking.eyes[0].position.x==originalNext.eyes[0].position.x&&b.tracking.eyes[1].position.x==originalNext.eyes[1].position.x&&
          b.tracking.fov[0].left==originalNext.fov[0].left&&b.tracking.fov[1].right==originalNext.fov[1].right);
    CHECK(feedback.calls==0&&observations.reaped==0&&!producer.TryBegin({101,203,301},unused));
    auto wrong=b;++wrong.native.owner;CHECK(producer.PrepareGraphicsResult(wrong)==ipc::ChannelResult::Invalid);
    CHECK(producer.PrepareGraphics(b));
    MakePipelinePair(b,db,tb,zb);CHECK(producer.Submit(b,db,tb,&zb)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(observations.published==2&&observations.reaped==0&&feedback.calls==0);
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok&&std::memcmp(&got,&ta,sizeof(got))==0);
    runtime::SourceSlotIdentity source{};
    CHECK(host.ReadSourceSlot(ta,source)&&source.requestId==a.requestId&&source.slot==a.sourceSlot);
    CHECK(host.ReadSceneDepth(da,ta,depth)&&depth.resources==za.resources);
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Busy); // FIFO and one Delivered lease.
    auto wrongTicket=ta;++wrongTicket.sequence;CHECK(host.Feedback(wrongTicket,true)==ipc::ChannelResult::Invalid);
    CHECK(host.Feedback(ta,true)==ipc::ChannelResult::Ok&&feedback.calls==0);
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok&&std::memcmp(&got,&tb,sizeof(got))==0);
    CHECK(host.ReadSourceSlot(tb,source)&&source.requestId==b.requestId&&source.slot==b.sourceSlot);
    producer.PumpGraphics();CHECK(feedback.calls==1&&feedback.outcomes[0]&&std::memcmp(&feedback.tickets[0],&ta,sizeof(ta))==0);
    CHECK(producer.Cancel(a)==ipc::ChannelResult::Invalid&&host.Feedback(ta,true)==ipc::ChannelResult::Invalid);
    CHECK(host.Feedback(tb,false)==ipc::ChannelResult::Ok);producer.PumpGraphics();producer.PumpGraphics();
    CHECK(feedback.calls==2&&!feedback.outcomes[1]&&std::memcmp(&feedback.tickets[1],&tb,sizeof(tb))==0);
    CHECK(observations.published==2&&observations.reaped==2&&!observations.wrong&&!feedback.wrong);
    producer.CloseGraphics();producer.CloseGraphics();host.Close();CHECK(feedback.calls==2);return 0;
}
int PipelineLookaheadDeferred(bool expire){
    PipelineFeedback feedback;ipc::FrameChannel host;CHECK(host.CreateHost(true,true));
    ipc::StagedFrameProducer producer(feedback);CHECK(producer.Connect(host.Token(),true));
    const runtime::PresentationRequirements requirements{100,120,29,19,4};auto tracking=DeferredTracking();
    ipc::FrameLease a{},b{},c{};graphics::TextureDescriptor da{},db{},out{};
    graphics::PairTicket ta{},tb{},got{};graphics::SceneDepthFrame za{},zb{};
    CHECK(host.SubmitLookaheadRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);
    ++tracking.generation;tracking.predictedNs+=11000000;
    CHECK(host.SubmitLookaheadRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);
    CHECK(producer.TryBegin({101,201,301},a));MakePipelinePair(a,da,ta,za);
    CHECK(producer.Submit(a,da,ta,&za)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(producer.TryBegin({101,202,301},b));MakePipelinePair(b,db,tb,zb);
    CHECK(producer.Submit(b,db,tb,&zb)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok&&std::memcmp(&got,&ta,sizeof(got))==0);
    CHECK(host.Feedback(ta,true)==ipc::ChannelResult::Ok);
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok&&std::memcmp(&got,&tb,sizeof(got))==0);
    ++tracking.generation;tracking.predictedNs+=11000000;
    CHECK(host.SubmitLookaheadRequest(requirements,tracking,expire?1:200)==ipc::ChannelResult::Ok);
    CHECK(!producer.TryBegin({101,203,301},c)&&feedback.calls==0); // C receipt has no free source yet.
    if(expire)Sleep(3);else CHECK(host.CancelRequest()==ipc::ChannelResult::Busy);
    // A's graphics retirement can promote C only with its ORIGINAL deadline.
    CHECK(host.Feedback(tb,false)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(feedback.calls==2&&feedback.outcomes[0]&&!feedback.outcomes[1]);
    CHECK(!producer.TryBegin({101,203,301},c));
    if(expire){CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Timeout);CHECK(host.PollRequest(out,got)!=ipc::ChannelResult::Timeout);}
    ++tracking.generation;tracking.predictedNs+=11000000;
    CHECK(host.SubmitLookaheadRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);
    CHECK(producer.TryBegin({101,203,301},c)&&c.requestId==b.requestId+2&&c.tracking.generation==tracking.generation);
    CHECK(producer.Cancel(c)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Timeout);
    producer.CloseGraphics();host.Close();CHECK(feedback.calls==2&&!feedback.wrong);return 0;
}
int PipelineLookaheadOptIn(){
    const runtime::PresentationRequirements requirements{100,120,29,19,4};const auto tracking=DeferredTracking();
    for(bool twoSlots:{false,true}){
        ipc::FrameChannel host;CHECK(host.CreateHost(twoSlots));
        CHECK(host.SubmitLookaheadRequest(requirements,tracking,200)==ipc::ChannelResult::Invalid);
        host.Close();
    }
    return 0;
}

int PipelineExportOverlap(){
    PipelineFeedback feedback;PipelineObservations observations;
    ipc::FrameChannel host;CHECK(host.CreateHost(true,true,true));
    ipc::StagedFrameProducer producer(feedback,PipelineObservations::Observe,&observations);
    CHECK(producer.Connect(host.Token(),true)&&producer.ExportOverlapEnabled());
    const runtime::PresentationRequirements requirements{100,120,29,19,4};const auto first=DeferredTracking();
    auto next=first;++next.generation;next.predictedNs+=11000000;next.head.position.x=.25f;
    CHECK(host.SubmitLookaheadRequest(requirements,first,200)==ipc::ChannelResult::Ok);
    CHECK(host.SubmitLookaheadRequest(requirements,next,200)==ipc::ChannelResult::Ok);
    ipc::FrameLease a{},b{},unused{};CHECK(producer.TryBegin({101,201,301},a));
    CHECK(producer.PrepareGraphics(a));
    CHECK(!producer.TryBegin({101,202,301},unused));
    auto wrong=a;++wrong.native.frameId;CHECK(producer.MarkCaptureReleased(wrong)==ipc::ChannelResult::Invalid);
    wrong=a;++wrong.channelGeneration;CHECK(producer.MarkCaptureReleased(wrong)==ipc::ChannelResult::Invalid);
    wrong=a;wrong.sourceSlot=1-a.sourceSlot;CHECK(producer.MarkCaptureReleased(wrong)==ipc::ChannelResult::Invalid);
    wrong=a;wrong.sourceSlotCount=1;CHECK(producer.MarkCaptureReleased(wrong)==ipc::ChannelResult::Invalid);

    // The graphics owner cannot invent the release when the wire commit is Busy.
    const auto name=L"Local\\FrostbiteVR.Control.v9."+host.Token()+L".mutex";
    HANDLE mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,name.c_str());CHECK(mutex);
    HANDLE held=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);CHECK(held&&release);
    auto holding=std::async(std::launch::async,[&]{
        const auto acquired=WaitForSingleObject(mutex,1000)==WAIT_OBJECT_0;SetEvent(held);
        if(!acquired)return false;
        const auto released=WaitForSingleObject(release,1000)==WAIT_OBJECT_0;ReleaseMutex(mutex);return released;
    });
    CHECK(WaitForSingleObject(held,1000)==WAIT_OBJECT_0);
    const auto busy=producer.MarkCaptureReleased(a);SetEvent(release);const auto heldOkay=holding.get();
    CloseHandle(release);CloseHandle(held);CloseHandle(mutex);
    CHECK(heldOkay&&busy==ipc::ChannelResult::Busy);
    CHECK(!producer.TryBegin({101,202,301},unused));
    graphics::BodyPropEye props{};std::array<graphics::BodyPropEye,2> eyes{};
    CHECK(producer.SetBodyProps(a,0,props)==ipc::ChannelResult::Ok);
    CHECK(producer.MarkCaptureReleased(a)==ipc::ChannelResult::Ok);
    CHECK(producer.MarkCaptureReleased(a)==ipc::ChannelResult::Ok);
    CHECK(producer.SetBodyProps(a,0,props)==ipc::ChannelResult::Invalid);
    CHECK(producer.SetBodyPropPair(a,eyes)==ipc::ChannelResult::Invalid);
    CHECK(producer.PrepareGraphics(a)); // Exact cancellation cleanup remains legal.

    graphics::TextureDescriptor da{},db{},out{};graphics::PairTicket ta{},tb{},got{};graphics::SceneDepthFrame za{},zb{};
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Busy);
    auto admission=std::async(std::launch::async,[&]{return producer.TryBegin({101,202,301},b);});CHECK(admission.get());
    CHECK(b.sourceSlot!=a.sourceSlot&&b.requestId==a.requestId+1&&b.tracking.generation==next.generation&&
          b.tracking.predictedNs==next.predictedNs&&b.tracking.head.position.x==next.head.position.x);
    CHECK(feedback.calls==0&&observations.published==0&&observations.reaped==0);
    CHECK(producer.PrepareGraphics(b)&&producer.SetBodyPropPair(b,eyes)==ipc::ChannelResult::Ok);
    CHECK(!producer.TryBegin({101,203,301},unused));
    auto third=next;++third.generation;third.predictedNs+=11000000;
    CHECK(host.SubmitLookaheadRequest(requirements,third,200)==ipc::ChannelResult::Busy);

    // A's real producer fence is represented by this late Submit. The release
    // marker alone neither publishes A nor authorizes host delivery/feedback.
    MakePipelinePair(a,da,ta,za);CHECK(producer.Submit(a,da,ta,&za)==ipc::ChannelResult::Ok);
    CHECK(producer.MarkCaptureReleased(a)==ipc::ChannelResult::Invalid); // No new marker after Submit.
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Busy);
    producer.PumpGraphics();CHECK(observations.published==1&&feedback.calls==0);
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok&&std::memcmp(&got,&ta,sizeof(got))==0);
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Busy);
    CHECK(host.Feedback(ta,true)==ipc::ChannelResult::Ok&&feedback.calls==0);
    CHECK(!producer.TryBegin({101,203,301},unused)); // B still owns the sole camera.
    MakePipelinePair(b,db,tb,zb);CHECK(producer.Submit(b,db,tb,&zb)==ipc::ChannelResult::Ok);
    producer.PumpGraphics();CHECK(feedback.calls==1&&feedback.outcomes[0]&&
        std::memcmp(&feedback.tickets[0],&ta,sizeof(ta))==0);
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok&&std::memcmp(&got,&tb,sizeof(got))==0);
    CHECK(host.Feedback(tb,false)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(feedback.calls==2&&!feedback.outcomes[1]&&std::memcmp(&feedback.tickets[1],&tb,sizeof(tb))==0);
    CHECK(observations.published==2&&observations.reaped==2&&!observations.wrong&&!feedback.wrong);
    CHECK(producer.MarkCaptureReleased(a)==ipc::ChannelResult::Invalid);
    producer.CloseGraphics();producer.CloseGraphics();host.Close();CHECK(feedback.calls==2);return 0;
}

int PipelineReleasedCancellation(bool expire){
    PipelineFeedback feedback;ipc::FrameChannel host;CHECK(host.CreateHost(true,true,true));
    ipc::StagedFrameProducer producer(feedback);CHECK(producer.Connect(host.Token(),true));
    const runtime::PresentationRequirements requirements{100,120,29,19,4};auto tracking=DeferredTracking();
    CHECK(host.SubmitLookaheadRequest(requirements,tracking,expire?100:200)==ipc::ChannelResult::Ok);
    ++tracking.generation;tracking.predictedNs+=11000000;
    CHECK(host.SubmitLookaheadRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);
    ipc::FrameLease a{},b{},unused{};CHECK(producer.TryBegin({101,201,301},a));
    CHECK(producer.MarkCaptureReleased(a)==ipc::ChannelResult::Ok);
    CHECK(producer.TryBegin({101,202,301},b));
    graphics::TextureDescriptor da{},out{};graphics::PairTicket ta{},got{};graphics::SceneDepthFrame za{};
    if(expire){Sleep(120);CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Timeout);}
    else CHECK(host.CancelRequest()==ipc::ChannelResult::Ok);
    CHECK(producer.Cancel(b)==ipc::ChannelResult::Ok);
    CHECK(producer.MarkCaptureReleased(b)==ipc::ChannelResult::Invalid);
    CHECK(!producer.TryBegin({101,203,301},unused)&&feedback.calls==0);
    // Cancelled/expired A is still held until the real source is submitted and
    // retired negatively; skipping B must not erase or acknowledge A's source.
    MakePipelinePair(a,da,ta,za);CHECK(producer.Submit(a,da,ta,&za)==ipc::ChannelResult::Ok);
    producer.PumpGraphics();producer.PumpGraphics();
    CHECK(feedback.calls==1&&!feedback.outcomes[0]&&std::memcmp(&feedback.tickets[0],&ta,sizeof(ta))==0);
    CHECK(host.Feedback(ta,true)==ipc::ChannelResult::Invalid&&!feedback.wrong);
    producer.CloseGraphics();producer.CloseGraphics();host.Close();CHECK(feedback.calls==1);return 0;
}

int PipelineExportOverlapOff(){
    for(bool lookahead:{false,true}){
        PipelineFeedback feedback;ipc::FrameChannel host;CHECK(host.CreateHost(true,lookahead));
        ipc::StagedFrameProducer producer(feedback);CHECK(producer.Connect(host.Token(),true)&&!producer.ExportOverlapEnabled());
        const runtime::PresentationRequirements requirements{100,120,29,19,4};const auto tracking=DeferredTracking();
        CHECK((lookahead?host.SubmitLookaheadRequest(requirements,tracking,200):host.BeginRequest(requirements,tracking,200))==ipc::ChannelResult::Ok);
        ipc::FrameLease lease{};CHECK(producer.TryBegin({101,201,301},lease));
        CHECK(producer.MarkCaptureReleased(lease)==ipc::ChannelResult::Invalid);
        CHECK(producer.Cancel(lease)==ipc::ChannelResult::Ok);producer.PumpGraphics();producer.CloseGraphics();host.Close();
        CHECK(feedback.calls==0);
    }
    return 0;
}


int ThreeSourceStagedOwnership(bool closePending){
    PipelineFeedback feedback;PipelineObservations observed;ipc::FrameChannel host;CHECK(host.CreateHost(true,true,true,true));
    observed.expectedCount=3;
    ipc::StagedFrameProducer producer(feedback,PipelineObservations::Observe,&observed);CHECK(producer.Connect(host.Token(),true,true));
    const runtime::PresentationRequirements req{100,120,29,19,4};auto pose=DeferredTracking();
    CHECK(host.SubmitLookaheadRequest(req,pose,200)==ipc::ChannelResult::Ok);++pose.generation;pose.predictedNs+=11000000;
    CHECK(host.SubmitLookaheadRequest(req,pose,200)==ipc::ChannelResult::Ok);
    ipc::FrameLease a{},b{},c{},unused{};CHECK(producer.TryBegin({101,201,301},a)&&a.sourceSlotCount==3);
    CHECK(producer.MarkCaptureReleased(a)==ipc::ChannelResult::Ok&&producer.TryBegin({101,202,301},b));
    CHECK(producer.MarkCaptureReleased(b)==ipc::ChannelResult::Ok);
    graphics::TextureDescriptor da{},db{},dc{},out{};graphics::PairTicket ta{},tb{},tc{},got{};graphics::SceneDepthFrame za{},zb{},zc{};
    MakePipelinePair(a,da,ta,za);MakePipelinePair(b,db,tb,zb);
    CHECK(producer.Submit(a,da,ta,&za)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok&&!std::memcmp(&got,&ta,sizeof(ta)));
    ++pose.generation;pose.predictedNs+=11000000;pose.head.position.x=.75f;
    CHECK(host.SubmitLookaheadRequest(req,pose,200)==ipc::ChannelResult::Ok);
    auto admission=std::async(std::launch::async,[&]{return producer.TryBegin({101,203,301},c);});CHECK(admission.get());
    CHECK(c.sourceSlot==2&&c.sourceSlotCount==3&&c.tracking.head.position.x==pose.head.position.x);
    CHECK(feedback.calls==0&&observed.published==1&&observed.reaped==0&&!producer.TryBegin({101,204,301},unused));
    auto wrong=c;wrong.sourceSlotCount=2;CHECK(producer.PrepareGraphicsResult(wrong)==ipc::ChannelResult::Invalid);
    CHECK(producer.PrepareGraphics(c));MakePipelinePair(c,dc,tc,zc);
    CHECK(producer.Submit(c,dc,tc,&zc)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Busy); // A held; C cannot bypass B.
    if(closePending){
        producer.CloseGraphics();producer.CloseGraphics();CHECK(feedback.calls==2&&!feedback.outcomes[0]&&!feedback.outcomes[1]);
        CHECK(!feedback.wrong);host.Close();return 0; // B has never adopted a source: no fake feedback.
    }
    CHECK(producer.Submit(b,db,tb,&zb)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(host.Feedback(ta,true)==ipc::ChannelResult::Ok&&feedback.calls==0);producer.PumpGraphics();
    CHECK(feedback.calls==1&&feedback.outcomes[0]&&!std::memcmp(&feedback.tickets[0],&ta,sizeof(ta)));
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok&&!std::memcmp(&got,&tb,sizeof(tb)));
    CHECK(host.Feedback(tb,true)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok&&!std::memcmp(&got,&tc,sizeof(tc)));
    CHECK(host.Feedback(tc,false)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(feedback.calls==3&&feedback.outcomes[1]&&!feedback.outcomes[2]&&!feedback.wrong&&!observed.wrong);
    CHECK(observed.published==3&&observed.reaped==3);
    CHECK(producer.MarkCaptureReleased(a)==ipc::ChannelResult::Invalid&&producer.MarkCaptureReleased(c)==ipc::ChannelResult::Invalid);
    producer.CloseGraphics();host.Close();CHECK(feedback.calls==3);return 0;
}

namespace fvr::ipc {
struct StagedFrameProducerTestAccess {
    static FrameChannel& Channel(StagedFrameProducer& p){return p.channel_;}
    static std::atomic_flag& Gate(StagedFrameProducer& p){return p.gate_;}
};
}
int PipelineOwnerTransition(bool third,bool expire){
    PipelineFeedback feedback;ipc::FrameChannel host;CHECK(host.CreateHost(true,true,true,third));
    ipc::StagedFrameProducer producer(feedback);CHECK(producer.Connect(host.Token(),true,third));
    const auto token=host.Token();const runtime::PresentationRequirements req{100,120,29,19,4};auto pose=DeferredTracking();
    const ipc::NativeFrameKey old{101,201,301},expectedOld{101,999,301},replacement{501,1,301};
    ipc::FrameLease a{},b{},unused{};graphics::TextureDescriptor da{},db{},out{};graphics::PairTicket ta{},tb{},got{};graphics::SceneDepthFrame za{},zb{};
    CHECK(host.SubmitLookaheadRequest(req,pose,200)==ipc::ChannelResult::Ok&&producer.TryBegin(old,a));
    CHECK(producer.TransitionWorldOwner(expectedOld,replacement)==ipc::ChannelResult::Busy); // Rendering.
    MakePipelinePair(a,da,ta,za);CHECK(producer.Submit(a,da,ta,&za)==ipc::ChannelResult::Ok);
    CHECK(producer.TransitionWorldOwner(expectedOld,replacement)==ipc::ChannelResult::Busy); // Unpublished source.
    producer.PumpGraphics();CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok);
    CHECK(producer.TransitionWorldOwner(expectedOld,replacement)==ipc::ChannelResult::Busy); // Host copy still owns it.
    CHECK(host.Feedback(ta,true)==ipc::ChannelResult::Ok);
    CHECK(producer.TransitionWorldOwner(expectedOld,replacement)==ipc::ChannelResult::Busy); // ACK is not graphics retirement.
    producer.PumpGraphics();CHECK(feedback.calls==1&&feedback.outcomes[0]&&!feedback.wrong);

    CHECK(producer.TransitionWorldOwner({102,201,301},replacement)==ipc::ChannelResult::Invalid);
    CHECK(producer.TransitionWorldOwner(expectedOld,{501,1,302})==ipc::ChannelResult::Invalid);
    CHECK(producer.TransitionWorldOwner(expectedOld,old)==ipc::ChannelResult::Invalid);
    CHECK(producer.TransitionWorldOwner(expectedOld,{0,1,301})==ipc::ChannelResult::Invalid);
    auto& gate=ipc::StagedFrameProducerTestAccess::Gate(producer);CHECK(!gate.test_and_set());
    const auto gateBusy=producer.TransitionWorldOwner(expectedOld,replacement);gate.clear();CHECK(gateBusy==ipc::ChannelResult::Busy);

    interaction::InputFrame input{};input.generation=1;input.spaceGeneration=7;input.predictedNs=1000000000;
    input.headValid=input.focused=true;input.head.position.x=.33f;
    CHECK(host.PublishInput(input)==ipc::ChannelResult::Ok);
    ++pose.generation;pose.predictedNs+=11000000;pose.head.position.x=.75f;
    CHECK(host.SubmitLookaheadRequest(req,pose,expire?1:200)==ipc::ChannelResult::Ok);
    if(expire)Sleep(20); // Real immutable wire deadline, never renewed by owner transition.
    CHECK(producer.TransitionWorldOwner(expectedOld,replacement)==ipc::ChannelResult::Ok);
    CHECK(host.Token()==token&&host.Connected());
    interaction::InputFrame read{};CHECK(producer.ReadInput(read)==ipc::ChannelResult::Ok&&read.generation==1&&read.head.position.x==.33f);
    ++input.generation;CHECK(host.PublishInput(input)==ipc::ChannelResult::Ok&&producer.ReadInput(read)==ipc::ChannelResult::Ok&&read.generation==2);
    CHECK(producer.PrepareGraphicsResult(a)==ipc::ChannelResult::Invalid&&producer.Submit(a,da,ta,&za)==ipc::ChannelResult::Invalid);
    if(expire){
        CHECK(!producer.TryBegin(replacement,unused));CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Timeout);
        ++pose.generation;pose.predictedNs+=11000000;CHECK(host.SubmitLookaheadRequest(req,pose,200)==ipc::ChannelResult::Ok);
    }
    CHECK(producer.TryBegin(replacement,b));
    CHECK(b.native==replacement&&b.channelGeneration==a.channelGeneration&&b.requestId>a.requestId&&
          b.tracking.generation==pose.generation&&b.tracking.predictedNs==pose.predictedNs&&b.tracking.head.position.x==.75f);
    CHECK(!producer.TryBegin(replacement,unused)); // Same new-owner frame cannot be rendered twice.
    MakePipelinePair(b,db,tb,zb);++db.resourceEpoch;tb.resourceEpoch=db.resourceEpoch;zb.color=tb;++zb.resourceGeneration;
    CHECK(producer.Submit(b,db,tb,&zb)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(host.PollRequest(out,got)==ipc::ChannelResult::Ok&&!std::memcmp(&got,&tb,sizeof(tb)));
    CHECK(host.Feedback(ta,true)==ipc::ChannelResult::Invalid); // Old resource/ticket never ACKs replacement.
    CHECK(host.Feedback(tb,true)==ipc::ChannelResult::Ok);producer.PumpGraphics();CHECK(feedback.calls==2&&!feedback.wrong);

    // Ordinary mismatch stays fatal; explicit transition cannot resurrect it.
    CHECK(!producer.TryBegin({701,2,301},unused));
    CHECK(producer.TransitionWorldOwner(replacement,{701,1,301})==ipc::ChannelResult::Closed);
    ++input.generation;CHECK(host.PublishInput(input)==ipc::ChannelResult::Closed);
    producer.CloseGraphics();host.Close();CHECK(feedback.calls==2);return 0;
}
int PipelineOwnerTransitionWireProof(bool third){
    PipelineFeedback feedback;ipc::FrameChannel host;CHECK(host.CreateHost(true,true,true,third));
    ipc::StagedFrameProducer producer(feedback);CHECK(producer.Connect(host.Token(),true,third));
    const ipc::NativeFrameKey old{101,201,301},replacement{501,1,301};
    runtime::PresentationRequirements req{100,120,29,19,4};auto pose=DeferredTracking();ipc::FrameLease lease{};
    CHECK(host.SubmitLookaheadRequest(req,pose,200)==ipc::ChannelResult::Ok&&producer.TryBegin(old,lease));
    CHECK(producer.Cancel(lease)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    graphics::TextureDescriptor descriptor{};graphics::PairTicket ticket{};CHECK(host.PollRequest(descriptor,ticket)==ipc::ChannelResult::Timeout);
    auto& wire=ipc::StagedFrameProducerTestAccess::Channel(producer);
    ++pose.generation;CHECK(host.SubmitLookaheadRequest(req,pose,200)==ipc::ChannelResult::Ok);
    ipc::FrameRequest request{};ipc::PipelineSlotKey key{};CHECK(wire.TryTakePipeline(request,key)==ipc::ChannelResult::Ok);
    // Independently hold a real mapped wire lease while all staged slots are Idle.
    CHECK(wire.CheckPipelineSourcesDrained()==ipc::ChannelResult::Busy);
    CHECK(producer.TransitionWorldOwner(old,replacement)==ipc::ChannelResult::Busy);
    CHECK(wire.SkipPipeline(key)==ipc::ChannelResult::Ok);
    ipc::PipelineRetirement retirement{};CHECK(wire.ClaimPipelineRetirement(key,retirement)==ipc::ChannelResult::Ok);
    CHECK(producer.TransitionWorldOwner(old,replacement)==ipc::ChannelResult::Busy);
    CHECK(wire.FinishPipelineRetirement(retirement)==ipc::ChannelResult::Ok);
    CHECK(host.PollRequest(descriptor,ticket)==ipc::ChannelResult::Timeout);

    // The read-only query cannot infer drain through a contended IPC mutex.
    const auto name=L"Local\\FrostbiteVR.Control.v"+std::wstring(third?L"10.":L"9.")+host.Token()+L".mutex";
    HANDLE mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,name.c_str());CHECK(mutex);
    CHECK(WaitForSingleObject(mutex,10)==WAIT_OBJECT_0);
    auto attempt=std::async(std::launch::async,[&]{return producer.TransitionWorldOwner(old,replacement);});
    const auto busy=attempt.get();CHECK(ReleaseMutex(mutex));CloseHandle(mutex);CHECK(busy==ipc::ChannelResult::Busy);
    CHECK(producer.TransitionWorldOwner(old,replacement)==ipc::ChannelResult::Ok);
    CHECK(feedback.calls==0);producer.CloseGraphics();host.Close();return 0;
}
int PipelineOwnerTransitionAddressReuse(bool third){
    PipelineFeedback feedback;ipc::FrameChannel host;CHECK(host.CreateHost(true,true,true,third));
    ipc::StagedFrameProducer producer(feedback);CHECK(producer.Connect(host.Token(),true,third));
    const ipc::NativeFrameKey original{101,100,301},intermediate{501,1,301},reused{101,1,301};
    const runtime::PresentationRequirements req{144,160,29,19,4};auto pose=DeferredTracking();ipc::FrameLease old{},fresh{},unused{};
    CHECK(host.SubmitLookaheadRequest(req,pose,200)==ipc::ChannelResult::Ok&&producer.TryBegin(original,old));
    CHECK(producer.Cancel(old)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    graphics::TextureDescriptor descriptor{};graphics::PairTicket ticket{};
    CHECK(host.PollRequest(descriptor,ticket)==ipc::ChannelResult::Timeout);
    CHECK(producer.TransitionWorldOwner(original,intermediate)==ipc::ChannelResult::Ok);
    // No request is admitted for B: its next incarnation can reuse A's address.
    CHECK(producer.TransitionWorldOwner(intermediate,reused)==ipc::ChannelResult::Ok);
    ++pose.generation;CHECK(host.SubmitLookaheadRequest(req,pose,200)==ipc::ChannelResult::Ok);
    CHECK(producer.TryBegin(reused,fresh)&&fresh.native==reused&&fresh.requestId>old.requestId&&fresh.channelGeneration==old.channelGeneration);
    CHECK(producer.PrepareGraphicsResult(old)==ipc::ChannelResult::Invalid);
    CHECK(!producer.TryBegin(reused,unused));
    CHECK(producer.Cancel(fresh)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    producer.CloseGraphics();host.Close();CHECK(feedback.calls==0);return 0;
}
int PipelineOwnerTransitionCold(bool third){
    PipelineFeedback feedback;ipc::FrameChannel host;CHECK(host.CreateHost(true,true,true,third));
    ipc::StagedFrameProducer producer(feedback);CHECK(producer.Connect(host.Token(),true,third));
    const ipc::NativeFrameKey old{101,999,301},replacement{501,1,301};
    const runtime::PresentationRequirements req{144,160,29,19,4};auto pose=DeferredTracking();ipc::FrameLease lease{};
    CHECK(host.SubmitLookaheadRequest(req,pose,200)==ipc::ChannelResult::Ok);
    CHECK(producer.TransitionWorldOwner(old,replacement)==ipc::ChannelResult::Ok);
    CHECK(producer.TryBegin(replacement,lease)&&lease.requirements.width==144&&lease.requirements.height==160);
    // An admitted-but-not-allocated source is no longer a cold transition.
    CHECK(producer.TransitionWorldOwner(old,{601,1,301})==ipc::ChannelResult::Invalid);
    CHECK(producer.TransitionWorldOwner(replacement,{601,1,301})==ipc::ChannelResult::Busy);
    CHECK(producer.Cancel(lease)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    CHECK(producer.TransitionWorldOwner(replacement,{601,1,301})==ipc::ChannelResult::Ok);
    CHECK(ipc::StagedFrameProducerTestAccess::Channel(producer).StopPipelineAdmission()==ipc::ChannelResult::Ok);
    CHECK(producer.TransitionWorldOwner({601,1,301},{701,1,301})==ipc::ChannelResult::Closed);
    producer.CloseGraphics();host.Close();CHECK(feedback.calls==0);
    if(!third){
        ipc::FrameChannel ordinary;CHECK(ordinary.CreateHost());ipc::StagedFrameProducer v8(feedback);CHECK(v8.Connect(ordinary.Token()));
        CHECK(v8.TransitionWorldOwner(old,replacement)==ipc::ChannelResult::Invalid);
        CHECK(ordinary.CheckPipelineSourcesDrained()==ipc::ChannelResult::Invalid);v8.CloseGraphics();ordinary.Close();
    }
    return 0;
}

int main(){
    for(bool third:{false,true}){
        CHECK(!PipelineOwnerTransition(third,false));CHECK(!PipelineOwnerTransition(third,true));
        CHECK(!PipelineOwnerTransitionWireProof(third));CHECK(!PipelineOwnerTransitionCold(third));
        CHECK(!PipelineOwnerTransitionAddressReuse(third));
    }
    CHECK(!ThreeSourceStagedOwnership(false));
    CHECK(!ThreeSourceStagedOwnership(true));
    CHECK(!PipelineExportOverlapOff());
    CHECK(!PipelineExportOverlap());
    CHECK(!PipelineReleasedCancellation(false));
    CHECK(!PipelineReleasedCancellation(true));
    CHECK(!PipelineLookaheadOptIn());
    for(unsigned when=0;when<3;++when)CHECK(!PipelineLookaheadFifo(when));
    CHECK(!PipelineLookaheadDeferred(false));
    CHECK(!PipelineLookaheadDeferred(true));
    CHECK(!PipelineOptionalAliases(true));
    CHECK(!PipelineOptionalAttachments());
    CHECK(!PipelineOptionalAliases());
    CHECK(!PipelineMappedOwnership());
    CHECK(!PipelineCancellationAndClose());
    CHECK(!PipelineLifetimeMismatch());
    CHECK(!PipelineDeferredRequestCancellation(false));
    CHECK(!PipelineDeferredRequestCancellation(true));
    CHECK(!PipelineRemoteSuccessor());
    CHECK(!PipelineFaultBoundary());
    CHECK(!LatePresentReapsWithoutAdmission(false));
    CHECK(!LatePresentReapsWithoutAdmission(true));
    CHECK(!DeferredRetirement(true,RetirementRoute::Prepare));
    CHECK(!DeferredRetirement(false,RetirementRoute::Pump));
    CHECK(!DeferredRetirement(true,RetirementRoute::Cancel));
    CHECK(!DeferredRetirement(true,RetirementRoute::Close));
    CHECK(!DeferredRetirement(false,RetirementRoute::Close));
    CHECK(!DeferredSuccessorCancelledOrExpired(false));
    CHECK(!DeferredSuccessorCancelledOrExpired(true));
    Feedback feedback;feedback.graphicsThread=std::this_thread::get_id();
    std::atomic<bool> receivedBodyPair=false;
    Observations observed;observed.graphicsThread=std::this_thread::get_id();
    ipc::FrameChannel host;CHECK(host.CreateHost());ipc::StagedFrameProducer producer(feedback,Observations::Observe,&observed);CHECK(producer.Connect(host.Token()));
    runtime::PresentationRequirements requirements{100,120,29,0x87654321,-2};runtime::TrackingFrame tracking{};
    tracking.generation=0x100000007ULL;tracking.spaceGeneration=0x200000003ULL;tracking.predictedNs=10000000000;
    tracking.headValid=tracking.focused=true;tracking.eyes[0].position.x=-.032f;tracking.eyes[1].position.x=.032f;
    tracking.fov={math::FovTangents{-1.1f,.9f,1,-1},math::FovTangents{-.9f,1.1f,1,-1}};
    interaction::InputFrame controls{};controls.generation=1;controls.spaceGeneration=1;controls.predictedNs=1000000000;controls.focused=controls.headValid=true;
    CHECK(host.PublishInput(controls)==ipc::ChannelResult::Ok);interaction::InputFrame gotControls{};
    CHECK(producer.ReadInput(gotControls)==ipc::ChannelResult::Ok&&gotControls.generation==1);
    auto request=[&](bool consume,std::atomic<bool>* delivered=nullptr,unsigned holdMs=0){return std::async(std::launch::async,[&,consume,delivered,holdMs]{graphics::TextureDescriptor d;graphics::PairTicket t;auto result=ipc::ChannelResult::Busy;const auto retryUntil=GetTickCount64()+1000;while(result==ipc::ChannelResult::Busy&&GetTickCount64()<retryUntil){result=host.RequestPair(requirements,tracking,50,d,t);if(result==ipc::ChannelResult::Busy)Sleep(1);}if(result==ipc::ChannelResult::Ok){graphics::BodyPropFrame props;if(host.ReadBodyProps(t,props)&&props.eyes[0].ammo.loaded==24&&props.eyes[1].ammo.loaded==24&&props.eyes[0].ammo.reserve==111&&props.eyes[1].ammo.reserve==111)receivedBodyPair=true;if(delivered)delivered->store(true);if(holdMs)Sleep(holdMs);host.Feedback(t,consume);const auto feedbackUntil=GetTickCount64()+1000;while(host.FlushFeedback()==ipc::ChannelResult::Busy&&GetTickCount64()<feedbackUntil)Sleep(1);}return result;});};
    ipc::NativeFrameKey key{0x100000001ULL,0x200000009ULL,0x300000004ULL};
    const auto firstStarted=GetTickCount64();ipc::FrameLease first;auto pending=request(true);
    // Visibility callback runs on a distinct thread. Only owned values cross.
    auto visibility=std::async(std::launch::async,[&]{return Acquire(producer,first,key);});CHECK(visibility.get());
    CHECK(first.tracking.generation==tracking.generation&&first.tracking.fov[0].left==tracking.fov[0].left);
    ipc::FrameLease other;CHECK(!producer.TryBegin(key,other));
    graphics::TextureDescriptor d;graphics::PairTicket ticket;MakeTicket(first,d,ticket);
    auto wrong=first;++wrong.native.owner;CHECK(producer.Submit(wrong,d,ticket)==ipc::ChannelResult::Invalid);
    wrong=first;++wrong.native.deviceEpoch;CHECK(producer.Cancel(wrong)==ipc::ChannelResult::Invalid);
    auto bad=ticket;++bad.frameId;CHECK(producer.Submit(first,d,bad)==ipc::ChannelResult::Invalid);
    bad=ticket;++bad.trackingGeneration;CHECK(producer.Submit(first,d,bad)==ipc::ChannelResult::Invalid);
    graphics::SceneDepthFrame invalidDepth;CHECK(producer.Submit(first,d,ticket,&invalidDepth)==ipc::ChannelResult::Invalid);
    std::array<graphics::BodyPropEye,2> props{};
    for(auto& eye:props)eye.ammo={1,1000000000,1100000000,1,2,tracking.spaceGeneration,24,111,30};
    CHECK(producer.SetBodyPropPair(wrong,props)==ipc::ChannelResult::Invalid);
    CHECK(producer.SetBodyPropPair(first,props)==ipc::ChannelResult::Ok);
    auto invalidProps=props;invalidProps[0].ammo.loaded=99;invalidProps[1].count=graphics::MaxBodyProps+1;
    CHECK(producer.SetBodyPropPair(first,invalidProps)==ipc::ChannelResult::Invalid);
    CHECK(producer.Submit(first,d,ticket)==ipc::ChannelResult::Ok);
    CHECK(observed.published==0&&observed.reaped==0); // Staged is not IPC-published.
    CHECK(producer.SetBodyPropPair(first,props)==ipc::ChannelResult::Invalid);
    CHECK(producer.Submit(first,d,ticket)==ipc::ChannelResult::Invalid);
    CHECK(producer.Cancel(first)==ipc::ChannelResult::Invalid);
    // Deterministic lost-wakeup regression: Publish signals while a recursive
    // outer hold keeps the named mutex unavailable to the awakened host.
    const auto mutexName=L"Local\\FrostbiteVR.Control.v8."+host.Token()+L".mutex";
    HANDLE heldMutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,mutexName.c_str());CHECK(heldMutex);
    CHECK(WaitForSingleObject(heldMutex,10)==WAIT_OBJECT_0);
    const auto firstPublished=GetTickCount64();producer.PumpGraphics();Sleep(8);CHECK(ReleaseMutex(heldMutex));CloseHandle(heldMutex);
    const auto releasedAt=GetTickCount64();const auto firstResult=PumpUntilHost(producer,pending);
    if(firstResult!=ipc::ChannelResult::Ok)std::fprintf(stderr,"first result=%u acquire/publish=%llu held=%llu total=%llu consumed=%u discarded=%u\n",unsigned(firstResult),firstPublished-firstStarted,releasedAt-firstPublished,GetTickCount64()-firstStarted,feedback.consumed.load(),feedback.discarded.load());
    CHECK(firstResult==ipc::ChannelResult::Ok&&receivedBodyPair);
    for(unsigned i=0;i<100&&!feedback.consumed;++i){producer.PumpGraphics();Sleep(1);}CHECK(feedback.consumed==1&&!feedback.discarded);
    producer.PumpGraphics();CHECK(feedback.consumed==1);
    CHECK(observed.published==1&&observed.reaped==1&&!observed.wrong);
    // Cancelled native frame leaves no GPU ticket, then stale leases cannot act.
    pending=request(false);CHECK(!producer.TryBegin(key,other));++key.frameId;ipc::FrameLease cancelled;CHECK(Acquire(producer,cancelled,key));CHECK(producer.Cancel(cancelled)==ipc::ChannelResult::Ok);
    while(pending.wait_for(0ms)!=std::future_status::ready){producer.PumpGraphics();Sleep(1);}
    CHECK(pending.get()==ipc::ChannelResult::Timeout);producer.PumpGraphics();CHECK(feedback.discarded==0);
    CHECK(observed.published==1&&observed.reaped==1); // Cancellation has no GPU publication.
    pending=request(false);CHECK(!producer.TryBegin(key,other));++key.frameId;ipc::FrameLease late;CHECK(Acquire(producer,late,key));
    CHECK(producer.Cancel(cancelled)==ipc::ChannelResult::Invalid);
    CHECK(pending.get()==ipc::ChannelResult::Timeout);MakeTicket(late,d,ticket);
    CHECK(producer.Submit(late,d,ticket)==ipc::ChannelResult::Ok);CHECK(WaitFeedback(producer,feedback,1,1));
    CHECK(observed.published==1&&observed.reaped==1); // Expired before publish is not reaped feedback.
    // Once delivered, exceeding the rendezvous deadline must not reclaim GPU use.
    const auto heldStarted=GetTickCount64();std::atomic<bool> delivered=false;pending=request(true,&delivered,90);++key.frameId;ipc::FrameLease held;CHECK(Acquire(producer,held,key));MakeTicket(held,d,ticket);
    const auto heldSubmitted=GetTickCount64();CHECK(producer.Submit(held,d,ticket)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    const auto until=GetTickCount64()+1000;while(!delivered&&GetTickCount64()<until){producer.PumpGraphics();Sleep(1);}
    if(!delivered){std::fprintf(stderr,"held acquire/submit=%llu total=%llu result=%u consumed=%u discarded=%u\n",heldSubmitted-heldStarted,GetTickCount64()-heldStarted,unsigned(pending.get()),feedback.consumed.load(),feedback.discarded.load());return 1;}
    // Suspending new rendering cannot cancel/reclaim a pair already owned by
    // the host, or let a later native frame reuse its GPU allocation.
    auto resumedKey=key;++resumedKey.frameId;
    CHECK(!producer.TryBegin(resumedKey,other));
    CHECK(producer.Cancel(held)==ipc::ChannelResult::Invalid);
    CHECK(producer.Cancel(cancelled)==ipc::ChannelResult::Invalid);
    producer.PumpGraphics();CHECK(!producer.TryBegin(resumedKey,other));
    CHECK(feedback.consumed==1&&feedback.discarded==1);
    Sleep(60);producer.PumpGraphics();CHECK(feedback.consumed==1&&feedback.discarded==1);
    CHECK(pending.get()==ipc::ChannelResult::Ok);CHECK(WaitFeedback(producer,feedback,2,1));
    CHECK(observed.published==2&&observed.reaped==2);
    // New channel repeats request IDs; local connection generation blocks ABA.
    producer.CloseGraphics();host.Close();CHECK(host.CreateHost());CHECK(producer.Connect(host.Token()));
    pending=request(false);CHECK(!producer.TryBegin(key,other));++key.frameId;ipc::FrameLease fresh;CHECK(Acquire(producer,fresh,key));CHECK(fresh.requestId==first.requestId&&fresh.channelGeneration!=first.channelGeneration);
    MakeTicket(fresh,d,ticket);auto previousConnection=first;previousConnection.native=fresh.native;
    CHECK(producer.Submit(previousConnection,d,ticket)==ipc::ChannelResult::Invalid);CHECK(producer.Cancel(previousConnection)==ipc::ChannelResult::Invalid);
    CHECK(producer.Submit(fresh,d,ticket)==ipc::ChannelResult::Ok);CHECK(PumpUntilHost(producer,pending)==ipc::ChannelResult::Ok);CHECK(WaitFeedback(producer,feedback,2,2));
    producer.CloseGraphics();producer.CloseGraphics();CHECK(feedback.consumed==2&&feedback.discarded==2&&!feedback.wrongThread);
    CHECK(observed.published==3&&observed.reaped==3&&!observed.wrong);
    return 0;
}
