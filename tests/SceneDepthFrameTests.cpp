#include "fvr/graphics/SceneDepthImportPolicy.h"
#include "fvr/graphics/SceneDepthFrame.h"
#include "fvr/graphics/DeferredDepthAdmission.h"
#include "fvr/ipc/RemoteFrameProvider.h"
#include "Test.h"
#include <iostream>
using namespace fvr;using namespace fvr::graphics;
SceneDepthFrame MakeDepth(const TextureDescriptor& descriptor,const PairTicket& ticket){
    SceneDepthFrame d;d.color=ticket;d.flags=SceneDepthCanonicalClip;d.width=descriptor.width;d.height=descriptor.height;
    d.adapterLow=descriptor.adapterLow;d.adapterHigh=descriptor.adapterHigh;d.format=44;d.samples=8;d.resourceGeneration=17;
    d.resources={{{101,102},{103,104}}};return d;
}
TextureDescriptor Descriptor(){TextureDescriptor d;d.width=100;d.height=120;d.format=29;d.adapterLow=19;d.adapterHigh=4;
    d.resourceEpoch=2;d.sharing=TextureSharing::LegacyFenced;const std::array<std::uint64_t,2> ids{1,2};std::memcpy(d.session.data(),ids.data(),16);return d;}
PairTicket Ticket(const TextureDescriptor& d){PairTicket t;t.resourceEpoch=d.resourceEpoch;t.session=d.session;t.sequence=1;t.frameId=21;t.spaceGeneration=12;t.trackingGeneration=11;t.predictedNs=1000000000;return t;}
int Shape(){const auto descriptor=Descriptor();const auto ticket=Ticket(descriptor);const auto depth=MakeDepth(descriptor,ticket);CHECK(SceneDepthMatches(depth,descriptor,ticket));
    auto reverse=depth;reverse.flags|=SceneDepthReversed;CHECK(SceneDepthMatches(reverse,descriptor,ticket));
    for(unsigned fault=0;fault<24;++fault){auto bad=depth;auto desc=descriptor;auto color=ticket;
        if(fault==0)++bad.version;if(fault==1)++bad.bytes;if(fault==2)bad.reserved=1;if(fault==3)bad.reserved1=1;
        if(fault==4)bad.flags=0;if(fault==5)bad.flags=5;if(fault==6)++bad.width;if(fault==7)++bad.height;
        if(fault==8)bad.format=28;if(fault==9)bad.samples=0;if(fault==10)bad.samples=3;if(fault==11)bad.samples=64;
        if(fault==12)++bad.adapterLow;if(fault==13)++bad.adapterHigh;if(fault==14)bad.resourceGeneration=0;
        if(fault==15)bad.resources[1][1]=0;if(fault==16)bad.resources[1][0]=bad.resources[0][1];if(fault==17)bad.resources[0][0]=1;
        if(fault==18)++bad.color.frameId;if(fault==19)++bad.color.trackingGeneration;if(fault==20)++bad.color.spaceGeneration;
        if(fault==21)++bad.color.sequence;if(fault==22)desc.sharing=TextureSharing::NamedKeyed;if(fault==23)color.predictedNs++;
        CHECK(!SceneDepthMatches(bad,desc,color));
    }return 0;
}
int Delivery(){ipc::RemoteFrameProvider host;CHECK(host.Create());ipc::FrameChannel producer;CHECK(producer.ConnectProducer(host.Token()));
    runtime::PresentationRequirements req{100,120,29,19,4};runtime::TrackingFrame input{};input.generation=11;input.spaceGeneration=12;input.predictedNs=1000000000;input.focused=input.headValid=true;
    input.fov={math::FovTangents{-1,1,1,-1},math::FovTangents{-1,1,1,-1}};
    runtime::TrackingFrame rendered;TextureDescriptor gotDescriptor;PairTicket got;ipc::FrameRequest request;SceneDepthFrame observed;
    CHECK(!host.TryGetCompletedPair(req,input,rendered,gotDescriptor,got));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    auto descriptor=Descriptor();auto ticket=Ticket(descriptor);auto depth=MakeDepth(descriptor,ticket);
    CHECK(!host.ReadSceneDepth(descriptor,ticket,observed));CHECK(producer.Publish(descriptor,ticket,nullptr,&depth)==ipc::ChannelResult::Ok);
    ++input.generation;input.predictedNs+=10000000;CHECK(host.TryGetCompletedPair(req,input,rendered,gotDescriptor,got));
    CHECK(host.ReadSceneDepth(gotDescriptor,got,observed));CHECK(observed.color.trackingGeneration==rendered.generation&&observed.color.trackingGeneration!=input.generation);
    CHECK(observed.resources==depth.resources);auto wrong=got;++wrong.sequence;CHECK(!host.ReadSceneDepth(gotDescriptor,wrong,observed));CHECK(observed.resourceGeneration==0);
    CHECK(host.ReadSceneDepth(gotDescriptor,got,observed));host.PairConsumed(got,true);CHECK(!host.ReadSceneDepth(gotDescriptor,got,observed));CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);
    CHECK(!host.TryGetCompletedPair(req,input,rendered,gotDescriptor,got));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    ++ticket.frameId;++ticket.sequence;ticket.trackingGeneration=request.trackingGeneration;ticket.predictedNs=request.predictedNs;
    CHECK(producer.Publish(descriptor,ticket,nullptr,&depth)==ipc::ChannelResult::Ok);CHECK(host.TryGetCompletedPair(req,input,rendered,gotDescriptor,got));
    CHECK(!host.ReadSceneDepth(gotDescriptor,got,observed)); // Prior frame's handles never accompany new color.
    host.PairConsumed(got,false);CHECK(producer.PollOutcome()==ipc::Outcome::Discarded);
    ++input.generation;input.predictedNs+=10000000;CHECK(!host.TryGetCompletedPair(req,input,rendered,gotDescriptor,got));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    ++ticket.frameId;++ticket.sequence;ticket.trackingGeneration=request.trackingGeneration;ticket.predictedNs=request.predictedNs;depth=MakeDepth(descriptor,ticket);
    CHECK(producer.Publish(descriptor,ticket,nullptr,&depth)==ipc::ChannelResult::Ok);CHECK(host.TryGetCompletedPair(req,input,rendered,gotDescriptor,got));CHECK(host.ReadSceneDepth(gotDescriptor,got,observed));
    producer.Close();CHECK(!host.ReadSceneDepth(gotDescriptor,got,observed));return 0;
}
int DepthImportCacheCases(){
    SceneDepthImportPolicy cache;SceneDepthImportPolicy::Selection selected;
    auto a=Descriptor();auto ta=Ticket(a);auto da=MakeDepth(a,ta);da.samples=1;
    auto b=a;b.resourceEpoch=3;const std::array<std::uint64_t,2> ids{21,22};std::memcpy(b.session.data(),ids.data(),16);
    auto tb=Ticket(b);auto db=MakeDepth(b,tb);db.samples=1;db.resources={{{201,202},{203,204}}};
    CHECK(cache.Select(da,a,ta,0,2,selected)&&selected.openSources&&selected.createLocal&&selected.resetImports);
    CHECK(cache.Commit(selected));
    CHECK(!cache.Select(da,a,ta,0,2,selected)); // Duplicate does not queue a second depth copy.
    CHECK(cache.Select(db,b,tb,1,2,selected)&&selected.openSources&&!selected.createLocal&&!selected.resetImports);
    CHECK(cache.Commit(selected));
    ++ta.sequence;++ta.frameId;da.color=ta;
    CHECK(cache.Select(da,a,ta,0,2,selected)&&!selected.openSources&&!selected.createLocal);
    const auto stale=selected;CHECK(cache.Commit(selected));CHECK(!cache.Commit(stale));
    // The two identities retain their own anti-replay watermark after A/B/A.
    CHECK(!cache.Select(db,b,tb,1,2,selected));
    ++tb.sequence;++tb.frameId;db.color=tb;
    auto wrong=db;wrong.color=ta;CHECK(!cache.Select(wrong,b,tb,1,2,selected));
    wrong=db;wrong.samples=8;CHECK(!cache.Select(wrong,b,tb,1,2,selected));
    wrong=db;wrong.quality=1;CHECK(!cache.Select(wrong,b,tb,1,2,selected));
    wrong=db;wrong.resources[0][0]=da.resources[0][0];CHECK(!cache.Select(wrong,b,tb,1,2,selected));
    wrong=db;wrong.resources[0][0]=LegacyResourceIds(a)[0];CHECK(!cache.Select(wrong,b,tb,1,2,selected));
    wrong=db;--wrong.resourceGeneration;CHECK(!cache.Select(wrong,b,tb,1,2,selected));
    wrong=db;wrong.resources[0][0]=999;CHECK(!cache.Select(wrong,b,tb,1,2,selected));
    // Resource recreation refreshes only the imported sources, not local images.
    ++db.resourceGeneration;db.resources={{{301,302},{303,304}}};
    CHECK(cache.Select(db,b,tb,1,2,selected)&&selected.openSources&&!selected.createLocal&&cache.Commit(selected));
    cache.Reset();CHECK(cache.Select(da,a,ta,0,2,selected)&&selected.createLocal&&selected.resetImports);
    // v8 reopens changed sources while retaining equal-shape MSAA targets.
    SceneDepthImportPolicy legacy;da.samples=8;
    CHECK(legacy.Select(da,a,ta,0,1,selected)&&selected.openSources&&selected.createLocal&&legacy.Commit(selected));
    ++ta.sequence;da.color=ta;++da.resourceGeneration;
    CHECK(legacy.Select(da,a,ta,0,1,selected)&&selected.openSources&&!selected.createLocal);
    CHECK(!legacy.Select(da,a,ta,1,1,selected));
    return 0;
}

int LegacyDepthAllocationCases(){
    SceneDepthImportPolicy cache;SceneDepthImportPolicy::Selection selected;
    auto descriptor=Descriptor();auto ticket=Ticket(descriptor);auto depth=MakeDepth(descriptor,ticket);
    unsigned sourceOpens=0,localCreates=0;
    // Reproduce forty single-slot source epochs. Resource IDs may be reused by
    // the driver after retirement; explicit invalidation must still reopen them.
    for(unsigned epoch=0;epoch<40;++epoch){
        cache.ResetSources();descriptor.resourceEpoch=epoch+1;ticket=Ticket(descriptor);depth=MakeDepth(descriptor,ticket);
        CHECK(cache.Select(depth,descriptor,ticket,0,1,selected)&&selected.openSources);
        CHECK(selected.createLocal==(epoch==0));
        sourceOpens+=4;localCreates+=selected.createLocal?4:0;CHECK(cache.Commit(selected));
        CHECK(!cache.Select(depth,descriptor,ticket,0,1,selected));
    }
    CHECK(sourceOpens==160&&localCreates==4);
    ++ticket.sequence;depth.color=ticket;
    CHECK(cache.Select(depth,descriptor,ticket,0,1,selected));const auto stale=selected;
    cache.ResetSources();CHECK(!cache.Commit(stale));
    // A failed replacement cannot authorize its selection or discard the
    // usable local allocation. No successful import/copy is committed here.
    auto invalid=depth;invalid.resources[0][0]=0;
    CHECK(!cache.Select(invalid,descriptor,ticket,0,1,selected));
    CHECK(cache.Select(depth,descriptor,ticket,0,1,selected)&&selected.openSources&&!selected.createLocal);
    CHECK(cache.Commit(selected));
    // Every local-allocation shape field, including adapter identity, must
    // invalidate reuse. Returning to the old shape before Commit still reuses
    // the original allocation; a failed attempt never commits a new shape.
    for(unsigned field=0;field<6;++field){auto changedDescriptor=descriptor;auto changed=depth;auto changedTicket=ticket;
        ++changedTicket.sequence;changed.color=changedTicket;
        if(field==0)++changedDescriptor.width;if(field==1)++changedDescriptor.height;
        if(field==2)changed.samples=4;if(field==3)++changed.quality;
        if(field==4)++changedDescriptor.adapterLow;if(field==5)++changedDescriptor.adapterHigh;
        changed.width=changedDescriptor.width;changed.height=changedDescriptor.height;
        changed.adapterLow=changedDescriptor.adapterLow;changed.adapterHigh=changedDescriptor.adapterHigh;
        CHECK(cache.Select(changed,changedDescriptor,changedTicket,0,1,selected)&&selected.createLocal);
    }
    auto badFormat=depth;badFormat.format=28;CHECK(!cache.Select(badFormat,descriptor,ticket,0,1,selected));
    ++ticket.sequence;depth.color=ticket;
    CHECK(cache.Select(depth,descriptor,ticket,0,1,selected)&&!selected.createLocal);
    ++descriptor.width;depth.width=descriptor.width;
    CHECK(cache.Select(depth,descriptor,ticket,0,1,selected)&&selected.createLocal&&cache.Commit(selected));
    ++ticket.sequence;depth.color=ticket;
    CHECK(cache.Select(depth,descriptor,ticket,0,1,selected)&&!selected.createLocal);
    --descriptor.width;depth.width=descriptor.width;
    CHECK(cache.Select(depth,descriptor,ticket,0,1,selected)&&selected.createLocal&&cache.Commit(selected));
    ++ticket.sequence;depth.color=ticket;
    CHECK(cache.Select(depth,descriptor,ticket,0,1,selected)&&!selected.createLocal);
    cache.Reset();CHECK(!cache.Commit(selected));
    CHECK(cache.Select(depth,descriptor,ticket,0,1,selected)&&selected.createLocal&&cache.Commit(selected));
    std::cout<<"Legacy depth cache: 40 equal-shape epochs, 160 source opens, 4 local creates (previously 160)\n";
    return 0;
}

int DeferredDepthAdmissionCases(){
    using Result=DepthAdmissionResult;
    for(unsigned slots:{2u,3u})for(unsigned slot=0;slot<slots;++slot){
        const DepthAdmissionKey key{7,11,19,23,29,31,37,1000000000,slot,slots};
        // Model the real destructive-clear boundary: first clear selects the
        // source, native world draw writes it, second clear will erase it.
        // A transient entry Busy must retain observation until that boundary.
        DeferredDepthAdmission admission;unsigned attempts=0,phase=0,copies=0,draws=0;
        float source=0,world=0,viewmodel=0;
        const auto attempt=[&]()noexcept{
            ++attempts;
            if(attempts==1)return phase==0?Result::Busy:Result::Rejected;
            return phase==2?Result::Ready:Result::Rejected;
        };
        CHECK(admission.Start(key,attempt)&&attempts==1);
        phase=1;source=1; // first native clear still executes
        ++draws;source=.75f;phase=2; // world draw, immediately before second clear
        CHECK(admission.BeforeWorldCopy(key,attempt)&&attempts==2);
        world=source;++copies;source=1; // preserve world BEFORE the destructive clear
        ++draws;source=.25f;viewmodel=source;++copies;
        CHECK(world==.75f&&viewmodel==.25f&&copies==2&&draws==2);
        CHECK(!admission.BeforeWorldCopy(key,attempt)&&!admission.Start(key,attempt)&&attempts==2);

        // Still busy at the required snapshot: absent optional depth, no later
        // stage retry using the viewmodel source as if it were world depth.
        DeferredDepthAdmission blocked;attempts=0;copies=0;draws=0;
        const auto busy=[&]()noexcept{++attempts;return Result::Busy;};
        CHECK(blocked.Start(key,busy));
        ++draws;source=.75f;
        if(blocked.BeforeWorldCopy(key,busy)){world=source;++copies;}
        source=1;++draws;source=.25f;
        CHECK(!blocked.BeforeWorldCopy(key,attempt)&&attempts==2&&copies==0&&draws==2);
        SceneDepthFrame absent{};CHECK(SceneDepthAbsent(absent));

        // Ready initially never repeats Begin (which would reset/reject an
        // already-active depth producer); rejected/foreign owner never retries.
        DeferredDepthAdmission immediate;attempts=0;
        const auto ready=[&]()noexcept{++attempts;return Result::Ready;};
        CHECK(immediate.Start(key,ready)&&immediate.BeforeWorldCopy(key,ready)&&attempts==1);
        {
            DeferredDepthAdmission rejected;attempts=0;
            const auto reject=[&]()noexcept{++attempts;return Result::Rejected;};
            CHECK(!rejected.Start(key,reject)&&!rejected.BeforeWorldCopy(key,ready)&&attempts==1);
        }
        // Every retained lifetime field matters, including slot/count and
        // original prediction. A successor cannot inherit the pending retry.
        for(unsigned field=0;field<10;++field){
            auto changed=key;
            if(field==0)++changed.channel;if(field==1)++changed.request;
            if(field==2)++changed.owner;if(field==3)++changed.frame;
            if(field==4)++changed.device;if(field==5)++changed.tracking;
            if(field==6)++changed.space;if(field==7)++changed.predicted;
            if(field==8)changed.slot=(slot+1)%slots;if(field==9)changed.slots=slots==2?3:2;
            DeferredDepthAdmission stale;attempts=0;
            CHECK(stale.Start(key,busy)&&!stale.BeforeWorldCopy(changed,ready)&&attempts==1);
            CHECK(!stale.BeforeWorldCopy(key,ready)&&attempts==1);
        }
        DeferredDepthAdmission lost;attempts=0;
        CHECK(lost.Start(key,busy)&&!lost.BeforeWorldCopy({},ready)&&attempts==1);
        DeferredDepthAdmission invalid;attempts=0;
        CHECK(!invalid.Start({},ready)&&!invalid.BeforeWorldCopy(key,ready)&&attempts==0);
    }
    return 0;
}

int main(){if(DeferredDepthAdmissionCases()||DepthImportCacheCases()||LegacyDepthAllocationCases())return 1;if(Shape()||Delivery())return 1;std::cout<<"SceneDepthFrame: exact attachment and bounded pre-world-copy admission passed; no GPU/native proof\n";}
