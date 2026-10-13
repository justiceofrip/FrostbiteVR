#include "fvr/graphics/SceneDepthFrame.h"
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
int main(){if(Shape()||Delivery())return 1;std::cout<<"SceneDepthFrame: exact legacy color attachment, validation and delivered ownership passed; no GPU/native proof\n";}
