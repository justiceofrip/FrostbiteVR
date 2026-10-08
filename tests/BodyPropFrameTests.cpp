#include "Bc2BodyAmmoHost.h"
#include "Bc2MagazineConsumerFixture.h"
#include "fvr/engine/FrostbiteCamera.h"
#include "fvr/ipc/RemoteFrameProvider.h"
#include "Test.h"
#include <Windows.h>
#include <iostream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::graphics;
namespace {
auto Identity(){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
BodyPropFrame Frame(const PairTicket& t){BodyPropFrame f;f.frameId=t.frameId;f.spaceGeneration=t.spaceGeneration;f.trackingGeneration=t.trackingGeneration;
    for(auto& e:f.eyes){e.view=Identity();e.projection=*math::MakeLhProjectionFromFovTangents({-1,1,1,-1},.05f,100);e.count=2;
        for(unsigned n=0;n<e.count;++n)e.instances[n]={{1,std::uint64_t(n)+1,3},Identity(),1000000000,1100000000,1,2,t.spaceGeneration};}
    return f;}
int ShapeAndLease(){PairTicket ticket;ticket.frameId=11;ticket.spaceGeneration=12;ticket.trackingGeneration=13;
    auto f=Frame(ticket);CHECK(BodyPropFrameMatches(f,ticket));CHECK(BodyPropFresh(f.eyes[0].instances[0],1000000000));
    CHECK(!BodyPropFresh(f.eyes[0].instances[0],1100000000));CHECK(!BodyPropFresh(f.eyes[0].instances[0],999999999));
    for(unsigned fault=0;fault<13;++fault){auto bad=f;if(fault==0)++bad.frameId;if(fault==1)++bad.spaceGeneration;
        if(fault==2)++bad.trackingGeneration;if(fault==3)++bad.bytes;if(fault==4)++bad.version;
        if(fault==5)bad.eyes[0].count=MaxBodyProps+1;if(fault==6)bad.eyes[1].reserved=1;
        if(fault==7)bad.eyes[0].projection.values[2][3]=0;if(fault==8)bad.eyes[1].instances[1].geometry.asset=0;
        if(fault==9)++bad.eyes[0].instances[0].deadlineNs;
        if(fault==10)bad.eyes[0].instances[0].actorGeneration=0;
        if(fault==11)bad.eyes[0].instances[0].equipmentGeneration=0;
        if(fault==12)++bad.eyes[0].instances[0].spaceGeneration;
        CHECK(!BodyPropFrameMatches(bad,ticket));}
    return 0;}
int CanonicalNativeRegistration(){auto world=Identity();world.values[3]={12.f,3.f,-8.f,1.f};
    const auto turn=math::MakeLhViewFromOpenXRPose({{.3f,.2f,-.4f},{0,std::sin(.3f),0,std::cos(.3f)}});CHECK(turn);
    world=interaction::Multiply(*interaction::InverseRigid(*turn),world);const auto view=interaction::InverseRigid(world);CHECK(view);
    for(const auto fov:{math::FovTangents{-1.2f,.8f,1,-.9f},math::FovTangents{-.8f,1.2f,1,-.9f}}){
        const auto proj=math::MakeLhProjectionFromFovTangents(fov,.05f,1000);CHECK(proj);
        const auto native=engine::NativeEye({world,*proj});CHECK(native);
        const auto eye=CanonicalBodyPropEye(native->view,native->projection);CHECK(eye);
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){CHECK(Near(eye->view.values[r][c],view->values[r][c]));CHECK(Near(eye->projection.values[r][c],proj->values[r][c]));}
    }return 0;}
BodyAmmoRenderSource Source(){magazine_consumer_fixture::Fixture f;f.Send();auto a=std::make_shared<BodyAmmoTracking>();a->magazine=f.result.tracking;
    auto& v=a->visual;v.enabled=true;const auto map=BindMagazineOwners(f.s.input.owner,f.s.weapon,f.reserve,0,f.now,f.s.family);
    v.source=*MagazineSupply(*map,f.reserve,f.s.trackingEpoch,f.now);v.input=f.s.input;v.gun=*f.gun;
    v.contact=*interaction::ChestAmmoSupply().alternateContact;v.frame=interaction::SupplyAnchorFrame::RecenteredBody;
    const auto& g=*Xm8MagazineEquipment().geometry;BodyAmmoRenderSource s;s.asset=g.asset;s.mesh=g.mesh;s.part=g.bones.magazine;s.rigFingerprint=g.rigFingerprint;
    s.geometry=MakeBodyAmmoGeometryKey(s.asset,s.mesh,s.part,s.rigFingerprint);s.prop={v,Identity()};s.authority=a;return s;}
int OriginalTypedDeadline(){auto a=Source();const auto now=a.prop.source.input.observedNs;
    for(unsigned kind=0;kind<3;++kind){auto original=a;auto authority=std::make_shared<BodyAmmoTracking>(*a.authority);
        auto selected=std::make_shared<SelectedMeshesSnapshot>(*authority->magazine->selected);authority->magazine->selected=selected;
        if(kind==0)selected->deadlineNs=now+5000000;
        if(kind==1)authority->magazine->family.deadlineNs=now+6000000;
        if(kind==2)authority->magazine->reserve.deadlineNs=now+7000000;
        // Supply is a copy of the reserve; preserve that dependency's identity.
        if(kind==2){authority->visual.source.deadlineNs=authority->magazine->reserve.deadlineNs;original.prop.source=authority->visual;}
        original.authority=authority;const auto p=BodyAmmoHostInstance(original,original,now);CHECK(p);
        CHECK(p->deadlineNs==now+(std::int64_t(kind)+5)*1000000);
        CHECK(BodyPropFresh(*p,p->deadlineNs-1));CHECK(!BodyPropFresh(*p,p->deadlineNs));
    }
    return 0;}
int PairMetadataIsAtomic(){ipc::RemoteFrameProvider host;CHECK(host.Create());ipc::FrameChannel producer;CHECK(producer.ConnectProducer(host.Token()));
    runtime::PresentationRequirements req{100,120,29,19,4};runtime::TrackingFrame input{};input.generation=11;input.spaceGeneration=12;input.predictedNs=1000000000;
    input.focused=input.headValid=true;input.fov={math::FovTangents{-1,1,1,-1},math::FovTangents{-1,1,1,-1}};
    runtime::TrackingFrame rendered;TextureDescriptor d;PairTicket ticket;ipc::FrameRequest request;
    CHECK(!host.TryGetCompletedPair(req,input,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    d.width=req.width;d.height=req.height;d.format=req.format;d.adapterLow=req.adapterLow;d.adapterHigh=req.adapterHigh;d.resourceEpoch=1;d.session[0]=1;
    PairTicket t;t.session=d.session;t.resourceEpoch=d.resourceEpoch;t.sequence=1;t.frameId=21;t.spaceGeneration=request.spaceGeneration;t.trackingGeneration=request.trackingGeneration;t.predictedNs=request.predictedNs;
    auto props=Frame(t);for(auto& e:props.eyes)e.ammo={41,1000000000,1100000000,51,61,t.spaceGeneration,0,180,30};
    CHECK(producer.Publish(d,t,&props)==ipc::ChannelResult::Ok);
    ++input.generation;input.predictedNs+=10000000;CHECK(host.TryGetCompletedPair(req,input,rendered,d,ticket));
    BodyPropFrame got;CHECK(host.ReadBodyProps(ticket,got));CHECK(got.trackingGeneration==rendered.generation&&got.trackingGeneration!=input.generation);
    CHECK(got.eyes[1].instances[1].deadlineNs==1100000000);
    CHECK(got.version==2&&got.bytes==2480);CHECK(got.eyes[0].ammo==props.eyes[0].ammo&&got.eyes[1].ammo==props.eyes[1].ammo);
    CHECK(AmmoCounterPair(got.eyes[0].ammo,got.eyes[1].ammo,t.spaceGeneration,1000000000));
    auto wrong=ticket;++wrong.sequence;CHECK(!host.ReadBodyProps(wrong,got));
    host.PairConsumed(ticket,true);CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);
    CHECK(!host.TryGetCompletedPair(req,input,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    ++t.frameId;++t.sequence;t.trackingGeneration=request.trackingGeneration;t.predictedNs=request.predictedNs;
    CHECK(producer.Publish(d,t,&props)==ipc::ChannelResult::Ok);CHECK(host.TryGetCompletedPair(req,input,rendered,d,ticket));
    CHECK(!host.ReadBodyProps(ticket,got)); // stale props cannot follow a new image ticket
    host.PairConsumed(ticket,false);CHECK(producer.PollOutcome()==ipc::Outcome::Discarded);
    return 0;}
}
int main(){if(ShapeAndLease()||CanonicalNativeRegistration()||OriginalTypedDeadline()||PairMetadataIsAtomic())return 1;
    std::cout<<"BodyPropFrame: 4 CPU groups passed; no GPU/native acceptance\n";}
