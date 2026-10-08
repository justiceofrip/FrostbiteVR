#include "Test.h"
#include "fvr/ipc/StagedFrameProducer.h"
#include "fvr/ipc/RemoteFrameProvider.h"
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
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    void PairConsumed(const graphics::PairTicket&,bool okay)noexcept override {
        if(std::this_thread::get_id()!=graphicsThread)wrongThread=true;
        if(okay)++consumed;else ++discarded;
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
int main(){
    Feedback feedback;feedback.graphicsThread=std::this_thread::get_id();
    ipc::FrameChannel host;CHECK(host.CreateHost());ipc::StagedFrameProducer producer(feedback);CHECK(producer.Connect(host.Token()));
    runtime::PresentationRequirements requirements{100,120,29,0x87654321,-2};runtime::TrackingFrame tracking{};
    tracking.generation=0x100000007ULL;tracking.spaceGeneration=0x200000003ULL;tracking.predictedNs=10000000000;
    tracking.headValid=tracking.focused=true;tracking.eyes[0].position.x=-.032f;tracking.eyes[1].position.x=.032f;
    tracking.fov={math::FovTangents{-1.1f,.9f,1,-1},math::FovTangents{-.9f,1.1f,1,-1}};
    interaction::InputFrame controls{};controls.generation=1;controls.spaceGeneration=1;controls.predictedNs=1000000000;controls.focused=controls.headValid=true;
    CHECK(host.PublishInput(controls)==ipc::ChannelResult::Ok);interaction::InputFrame gotControls{};
    CHECK(producer.ReadInput(gotControls)==ipc::ChannelResult::Ok&&gotControls.generation==1);
    auto request=[&](bool consume,std::atomic<bool>* delivered=nullptr,unsigned holdMs=0){return std::async(std::launch::async,[&,consume,delivered,holdMs]{graphics::TextureDescriptor d;graphics::PairTicket t;auto result=ipc::ChannelResult::Busy;const auto retryUntil=GetTickCount64()+1000;while(result==ipc::ChannelResult::Busy&&GetTickCount64()<retryUntil){result=host.RequestPair(requirements,tracking,50,d,t);if(result==ipc::ChannelResult::Busy)Sleep(1);}if(result==ipc::ChannelResult::Ok){if(delivered)delivered->store(true);if(holdMs)Sleep(holdMs);host.Feedback(t,consume);const auto feedbackUntil=GetTickCount64()+1000;while(host.FlushFeedback()==ipc::ChannelResult::Busy&&GetTickCount64()<feedbackUntil)Sleep(1);}return result;});};
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
    CHECK(producer.Submit(first,d,ticket)==ipc::ChannelResult::Ok);
    CHECK(producer.Submit(first,d,ticket)==ipc::ChannelResult::Invalid);
    CHECK(producer.Cancel(first)==ipc::ChannelResult::Invalid);
    // Deterministic lost-wakeup regression: Publish signals while a recursive
    // outer hold keeps the named mutex unavailable to the awakened host.
    const auto mutexName=L"Local\\FrostbiteVR.Control.v4."+host.Token()+L".mutex";
    HANDLE heldMutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,mutexName.c_str());CHECK(heldMutex);
    CHECK(WaitForSingleObject(heldMutex,10)==WAIT_OBJECT_0);
    const auto firstPublished=GetTickCount64();producer.PumpGraphics();Sleep(8);CHECK(ReleaseMutex(heldMutex));CloseHandle(heldMutex);
    const auto releasedAt=GetTickCount64();const auto firstResult=PumpUntilHost(producer,pending);
    if(firstResult!=ipc::ChannelResult::Ok)std::fprintf(stderr,"first result=%u acquire/publish=%llu held=%llu total=%llu consumed=%u discarded=%u\n",unsigned(firstResult),firstPublished-firstStarted,releasedAt-firstPublished,GetTickCount64()-firstStarted,feedback.consumed.load(),feedback.discarded.load());
    CHECK(firstResult==ipc::ChannelResult::Ok);
    for(unsigned i=0;i<100&&!feedback.consumed;++i){producer.PumpGraphics();Sleep(1);}CHECK(feedback.consumed==1&&!feedback.discarded);
    producer.PumpGraphics();CHECK(feedback.consumed==1);
    // Cancelled native frame leaves no GPU ticket, then stale leases cannot act.
    pending=request(false);CHECK(!producer.TryBegin(key,other));++key.frameId;ipc::FrameLease cancelled;CHECK(Acquire(producer,cancelled,key));CHECK(producer.Cancel(cancelled)==ipc::ChannelResult::Ok);
    while(pending.wait_for(0ms)!=std::future_status::ready){producer.PumpGraphics();Sleep(1);}
    CHECK(pending.get()==ipc::ChannelResult::Timeout);producer.PumpGraphics();CHECK(feedback.discarded==0);
    pending=request(false);CHECK(!producer.TryBegin(key,other));++key.frameId;ipc::FrameLease late;CHECK(Acquire(producer,late,key));
    CHECK(producer.Cancel(cancelled)==ipc::ChannelResult::Invalid);
    CHECK(pending.get()==ipc::ChannelResult::Timeout);MakeTicket(late,d,ticket);
    CHECK(producer.Submit(late,d,ticket)==ipc::ChannelResult::Ok);CHECK(WaitFeedback(producer,feedback,1,1));
    // Once delivered, exceeding the rendezvous deadline must not reclaim GPU use.
    const auto heldStarted=GetTickCount64();std::atomic<bool> delivered=false;pending=request(true,&delivered,90);++key.frameId;ipc::FrameLease held;CHECK(Acquire(producer,held,key));MakeTicket(held,d,ticket);
    const auto heldSubmitted=GetTickCount64();CHECK(producer.Submit(held,d,ticket)==ipc::ChannelResult::Ok);producer.PumpGraphics();
    const auto until=GetTickCount64()+1000;while(!delivered&&GetTickCount64()<until){producer.PumpGraphics();Sleep(1);}
    if(!delivered){std::fprintf(stderr,"held acquire/submit=%llu total=%llu result=%u consumed=%u discarded=%u\n",heldSubmitted-heldStarted,GetTickCount64()-heldStarted,unsigned(pending.get()),feedback.consumed.load(),feedback.discarded.load());return 1;}
    Sleep(60);producer.PumpGraphics();CHECK(feedback.consumed==1&&feedback.discarded==1);
    CHECK(pending.get()==ipc::ChannelResult::Ok);CHECK(WaitFeedback(producer,feedback,2,1));
    // New channel repeats request IDs; local connection generation blocks ABA.
    producer.CloseGraphics();host.Close();CHECK(host.CreateHost());CHECK(producer.Connect(host.Token()));
    pending=request(false);CHECK(!producer.TryBegin(key,other));++key.frameId;ipc::FrameLease fresh;CHECK(Acquire(producer,fresh,key));CHECK(fresh.requestId==first.requestId&&fresh.channelGeneration!=first.channelGeneration);
    MakeTicket(fresh,d,ticket);auto previousConnection=first;previousConnection.native=fresh.native;
    CHECK(producer.Submit(previousConnection,d,ticket)==ipc::ChannelResult::Invalid);CHECK(producer.Cancel(previousConnection)==ipc::ChannelResult::Invalid);
    CHECK(producer.Submit(fresh,d,ticket)==ipc::ChannelResult::Ok);CHECK(PumpUntilHost(producer,pending)==ipc::ChannelResult::Ok);CHECK(WaitFeedback(producer,feedback,2,2));
    producer.CloseGraphics();producer.CloseGraphics();CHECK(feedback.consumed==2&&feedback.discarded==2&&!feedback.wrongThread);
    return 0;
}
