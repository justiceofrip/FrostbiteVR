#include "Bc2PhysicalReloadProbe.h"
#include "Bc2PhysicalReloadPreparation.h"
#include "NativeProbeConfig.h"
#include "fvr/interaction/TrackedRig.h"
#include "Test.h"
#include <cmath>
#include <iostream>
#include <sstream>
#include <cstring>
#include <vector>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
auto Matrix(const math::Pose& p){return *InverseRigid(*math::MakeLhViewFromOpenXRPose(p));}
auto Pose(float x=0,float y=0,float z=0){auto m=reload_insertion_detail::Identity();m.values[3]={x,y,z,1};return m;}
InputFrame Input(std::uint64_t generation=1){InputFrame s;s.generation=generation;s.spaceGeneration=7;s.predictedNs=1000000000+std::int64_t(generation)*10000000;s.focused=s.headValid=true;
    for(auto& h:s.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}
    s.hands[0].grip.position={-.2f,-.4f,-.3f};s.hands[1].grip.position={.2f,-.4f,-.3f};return s;}
std::string Report(const Bc2PhysicalReloadProbe& p){std::ostringstream o;p.Report(o);return o.str();}
int InverseMatchesActualTrackedRig(){for(bool anatomical:{false,true})for(float yaw:{0.f,.7f,2.8f})for(float units:{1.f,100.f}){
    auto source=Input();source.worldUnitsPerMeter=units;source.hands[0].grip.orientation={.2f,0,0,std::sqrt(.96f)};
    math::Pose bodyPose;bodyPose.position={10*units,3*units,-7*units};bodyPose.orientation={0,std::sin(yaw/2),0,std::cos(yaw/2)};
    const auto body=Matrix(bodyPose);auto left=Multiply(Pose(-.2f*units,-.4f*units,.3f*units),body);
    auto right=Multiply(Pose(.2f*units,-.4f*units,.3f*units),body),weapon=right;
    const TrackedRigOwner owner{1,2,3,4};std::array<ArmAnchor,2> arms{{{{9,3,7},{0,1,0}},{{11,3,7},{0,1,0}}}};
    TrackedRig rig;const auto initial=rig.Update(owner,source,body,left,right,weapon,arms);CHECK(initial&&initial->tracked[0]);
    // Production free/ammo wrist uses a fixed anatomical orientation while
    // retaining the ordinary unconstrained controller position. The diagnostic
    // inverse must derive that actual mapping, not assume native calibration.
    const auto publishedWrist=[&](const TrackedRigPose& tracked,const InputFrame& frame){
        if(!anatomical)return tracked.left;
        auto attachment=Pose();attachment.values[1][1]=attachment.values[2][2]=-1;
        auto controller=Matrix(frame.hands[0].grip);
        for(unsigned axis=0;axis<3;++axis)controller.values[3][axis]*=units;
        auto wrist=Multiply(attachment,Multiply(controller,body));wrist.values[3]=tracked.left.values[3];return wrist;
    };
    ReloadRawContact contact;contact.valid=true;contact.owner.space=7;contact.rigFingerprint=SpasReloadRig;contact.inputEvidence.sequence=source.generation;
    contact.rawLeftWristWorldMeters=publishedWrist(*initial,source);contact.trackingBodyWorldMeters=body;
    for(unsigned axis=0;axis<3;++axis){contact.rawLeftWristWorldMeters.values[3][axis]/=units;contact.trackingBodyWorldMeters.values[3][axis]/=units;}
    auto actual=source;actual.generation++;actual.predictedNs+=10000000;actual.hands[0].grip.position={-.15f,-.32f,-.5f};
    actual.hands[0].grip.orientation={0,.3f,.1f,std::sqrt(.90f)};
    const auto nativeDesired=rig.Update(owner,actual,body,left,right,weapon,arms);CHECK(nativeDesired);
    auto desired=publishedWrist(*nativeDesired,actual);for(unsigned axis=0;axis<3;++axis)desired.values[3][axis]/=units;
    const auto command=PhysicalReloadProbeController(contact,source,desired);CHECK(command);
    auto driven=actual;driven.hands[0].grip=*command;driven.generation++;driven.predictedNs+=10000000;
    const auto output=rig.Update(owner,driven,body,left,right,weapon,arms);CHECK(output);auto measured=publishedWrist(*output,driven);
    for(unsigned axis=0;axis<3;++axis)measured.values[3][axis]/=units;
    CHECK(reload_insertion_detail::Distance(measured,desired)<.00002f&&reload_insertion_detail::Angle(measured,desired)<.002f);
    auto bad=contact;bad.trackingBodyWorldMeters={};CHECK(!PhysicalReloadProbeController(bad,source,desired));
    bad=contact;bad.rawLeftWristWorldMeters.values[3][0]+=.1f;CHECK(!PhysicalReloadProbeController(bad,source,desired));
    bad=contact;++bad.inputEvidence.sequence;CHECK(!PhysicalReloadProbeController(bad,source,desired));
}return 0;}
int DisabledAndOwnerLoss(){auto input=Input();const auto copy=input;Bc2PhysicalReloadProbe off;off.Prepare(input,{},"",{}, {},1,2,1);
    CHECK(input.hands[0].grip.position.x==copy.hands[0].grip.position.x&&!off.CancelConsumer());
    Bc2PhysicalReloadProbe on(true);ReloadStateOwner owner{0x10000,0x20000,0x30000,0x40000,1,2,7};
    on.Prepare(input,owner,SpasReloadAsset,{}, {},1000000000,1100000000,1000000000);CHECK(!on.CancelConsumer());
    ++owner.equipGeneration;input.generation++;on.Prepare(input,owner,SpasReloadAsset,{}, {},1010000000,1110000000,1010000000);
    CHECK(on.CancelConsumer()&&Report(on).find("\"actual_consumer_completed\":false")!=std::string::npos);return 0;}
int ReceiptRequiresActualCounts(){for(unsigned fail=0;fail<3;++fail){Bc2PhysicalReloadProbe p(true);auto s=Input();
    ReloadStateOwner owner{0x10000,0x20000,0x30000,0x40000,1,2,7};PhysicalReloadProbeState state;
    ReloadRawContact raw;raw.valid=true;raw.owner=owner;
    Bc2AmmoReserveLease reserve;reserve.verified=true;reserve.identity.owner=owner;reserve.loaded=2;reserve.reserve=8;reserve.capacity=8;
    for(unsigned ms=0;ms<=6000;ms+=20){const auto now=1000000000ll+std::int64_t(ms)*1000000;s=Input(ms+1);reserve.observedNs=now;reserve.deadlineNs=now+100000000;state.reserve=reserve;
        p.Prepare(s,owner,SpasReloadAsset,raw,state,now,now+100000000,now);}
    const auto now=7020000000ll;state.submitted=1;state.completed=1;state.reserve->loaded=3;state.reserve->reserve=7;state.reserve->deadlineNs=now+100000000;
    if(fail==1)state.reserve->loaded=4;if(fail==2)state.reserve->reserve=8;
    p.Observe(state,now);CHECK(p.CancelConsumer());
    CHECK((Report(p).find("\"actual_consumer_completed\":true")!=std::string::npos)==(fail==0));
    s=Input(1001);p.Prepare(s,owner,SpasReloadAsset,raw,state,40000000000ll,40100000000ll,40000000000ll);
    CHECK((Report(p).find("\"actual_consumer_completed\":true")!=std::string::npos)==(fail==0)); // completion cannot turn into timeout
}return 0;}
int FlagsAndBounds(){constexpr auto flags=0x4000000u|0x2000000u|0x197800u|9;
    CHECK(ValidPhysicalReloadConfig(flags)&&ValidPhysicalReloadProbeConfig(flags,30000));
    CHECK(!ValidPhysicalReloadProbeConfig(flags,15000)&&!ValidPhysicalReloadProbeConfig(flags|0x400u,30000)&&
        !ValidPhysicalReloadProbeConfig(flags|0x200000u,30000)&&!ValidPhysicalReloadProbeConfig(flags&~0x2000000u,30000));
    Bc2PhysicalReloadProbe p(true);auto s=Input();ReloadStateOwner o{0x10000,0x20000,0x30000,0x40000,1,2,7};
    p.Prepare(s,o,SpasReloadAsset,{}, {},1000000000,1100000000,1000000000);s.generation++;
    p.Prepare(s,o,SpasReloadAsset,{}, {},31000000000ll,31100000000ll,31000000000ll);CHECK(p.CancelConsumer()&&s.hands[0].squeeze==0);return 0;}
// Real consumer + real TrackedRig, with only native API/counts replaced by a
// deterministic test runtime. The probe never receives a synthetic seat/claim.
struct Loop {
    std::int64_t now=1000000000,started=0,submittedAt=0;std::uint64_t sequence=0,cycle=0,intent=0;
    bool anatomical=false,applied=false,ackTaken=false;unsigned submits=0,cancels=0,starts=0;int loaded=2,reserve=8;
    std::vector<Bc2ReloadNativeRequest> requests;
    std::vector<Bc2ReloadAckEvidence> receipts;
    ReloadHoldIdentity native;PhysicalReloadApi api;std::optional<Bc2ReloadNativeRequest> request;
    std::optional<Bc2PhysicalReload> consumer;Bc2PhysicalReloadProbe probe;HandInteraction hands;std::optional<HandClaim> gun;
    TrackedRig rig;ReloadRawContact raw;std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
    explicit Loop(unsigned rounds=1):probe(true,rounds){native.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};native.firing={0x50000,0x60000,0x70000};native.serverPlayer=0x80000;native.serverSoldier=0x90000;native.serverItem=0xa0000;
        meshes->owner=native.owner;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
        auto& m=meshes->states[0].meshes[0];m.kind=SelectedMeshKind::Spas12;m.address=0x120000;std::memcpy(m.assetPath.data(),SpasReloadMesh.data(),SpasReloadMesh.size());
        api.context=this;api.clock=[](void* p)noexcept{return static_cast<Loop*>(p)->now;};
        api.reserve=[](void* p)noexcept->std::optional<Bc2AmmoReserveLease>{auto& f=*static_cast<Loop*>(p);
            if(f.submittedAt&&!f.applied&&f.now-f.submittedAt>=180000000){++f.loaded;--f.reserve;f.applied=true;}
            return Bc2AmmoReserveLease{f.native,100000+f.sequence,f.now,f.now+100000000,f.loaded,f.reserve,8,true,true};};
        api.identity=[](void* p)noexcept->std::optional<ReloadHoldIdentity>{return static_cast<Loop*>(p)->native;};
        api.start=[](void* p,const ReloadCycleControl& c)noexcept{auto& f=*static_cast<Loop*>(p);++f.starts;f.cycle=c.cycle;f.started=f.now;return true;};
        api.keep=[](void*,const ReloadCycleControl&)noexcept{return true;};
        api.lease=[](void* p,const ReloadHoldIdentity& id,std::uint64_t c)noexcept->std::optional<ReloadRoundLease>{auto& f=*static_cast<Loop*>(p);
            if(!f.started||f.now-f.started<150000000)return {};return ReloadRoundLease{id,c,f.sequence,f.now,f.now+100000000,f.loaded,f.reserve,8,true,!f.submittedAt||f.applied};};
        api.submit=[](void* p,const Bc2ReloadNativeRequest& r)noexcept{auto& f=*static_cast<Loop*>(p);++f.submits;f.request=r;f.requests.push_back(r);
            f.submittedAt=f.now;f.applied=f.ackTaken=false;return true;};
        api.ack=[](void* p,const ReloadHoldIdentity& id,std::uint64_t c)noexcept->std::optional<Bc2ReloadAckEvidence>{auto& f=*static_cast<Loop*>(p);if(!f.applied||f.ackTaken)return {};
            const auto& r=f.request->request;const Bc2ReloadAckEvidence ack{{{r.id,r.owner,r.operation,ReloadAcknowledgement::Applied},id,c,f.sequence,200+f.submits},f.now,f.now+100000000,true};
            f.ackTaken=true;f.receipts.push_back(ack);return ack;};
        api.cancel=[](void* p)noexcept{++static_cast<Loop*>(p)->cancels;};
        api.retire=[](void*,const ReloadHoldIdentity&,std::uint64_t)noexcept->std::optional<ReloadCycleRetirement>{return {};};
        consumer.emplace(true,api);
    }
    void Tick(){now+=10000000;++sequence;auto input=Input(sequence);input.hands[1].grip.position={0,-.4f,0};
        probe.Prepare(input,native.owner,SpasReloadAsset,raw,consumer->ProbeState(now),now,now+100000000,now);
        HandInteractionSample hand{{(std::uint64_t(native.owner.weak)<<32)|native.owner.soldier,5,17,7},sequence,now,now+100000000,now,true,{true,true},{input.hands[0].squeeze<=.35f,false}};
        hands.Update(hand);const HandContactProof grip{{1002,1},sequence,hand.deadlineNs,true};
        if(!gun)gun=hands.Acquire(hand,{hand.owner,InteractionHand::Right,HandClaimKind::GunHold,{0x40000,17},grip,++intent,0}).claim;
        else gun=hands.Renew(hand,gun->token,grip).claim;
        meshes->sequence=sequence;meshes->observedNs=now;meshes->deadlineNs=now+200000000;
        PhysicalReloadSample s;s.nativeOwner=native.owner;s.input=hand;s.weapon={0x40000,17};s.bodyFromHand=*PhysicalReloadPouchPose(input);
        s.geometrySequence=sequence;s.trackingEpoch=7;s.gripPressed=input.hands[0].squeeze>=.75f;s.cancel=probe.CancelConsumer();
        s.asset=SpasReloadAsset;s.meshes=meshes;s.raw=raw;if(raw.valid)s.originalHandEvidence=raw.inputEvidence;
        consumer->Tick(s,hands,intent);probe.Observe(consumer->ProbeState(now),now);
        const auto body=Pose(5,2,3),left=Multiply(Pose(-.2f,-.4f,.3f),body),right=Multiply(Pose(0,-.4f,0),body);
        std::array<ArmAnchor,2> arms{{{{4.8f,1.8f,3},{0,1,0}},{{5.2f,1.8f,3},{0,1,0}}}};
        const auto pose=rig.Update({1,2,3,4},input,body,left,right,right,arms);
        if(pose){raw.valid=true;raw.owner=native.owner;raw.rigFingerprint=SpasReloadRig;raw.inputEvidence=hand;
            raw.rawLeftWristWorldMeters=pose->left;raw.weaponWorldMeters=pose->weapon;raw.trackingBodyWorldMeters=body;
            if(anatomical){auto attachment=Pose();attachment.values[1][1]=attachment.values[2][2]=-1;
                auto tracked=Multiply(attachment,Multiply(Matrix(input.hands[0].grip),body));
                tracked.values[3]=pose->left.values[3];raw.rawLeftWristWorldMeters=tracked;}
        }
    }
};
int ClosedLoopThroughConsumer(){Loop f;for(unsigned n=0;n<2200&&!f.probe.CancelConsumer();++n)f.Tick();
    if(f.submits!=1||Report(f.probe).find("\"actual_consumer_completed\":true")==std::string::npos){std::cerr<<Report(f.probe)<<'\n';f.consumer->Report(std::cerr);}
    CHECK(f.submits==1&&f.loaded==3&&f.reserve==7&&Report(f.probe).find("\"actual_consumer_completed\":true")!=std::string::npos);
    CHECK(f.consumer->ProbeState(f.now).completed==1);return 0;}
int TwoRealConsumerShells(){for(bool anatomical:{false,true})for(int initialLoaded:{2,6}){Loop f(2);f.anatomical=anatomical;f.loaded=initialLoaded;for(unsigned n=0;n<2900&&!f.probe.CancelConsumer();++n)f.Tick();
    if(f.submits!=2||Report(f.probe).find("\"actual_consumer_completed\":true")==std::string::npos){std::cerr<<Report(f.probe)<<'\n';f.consumer->Report(std::cerr);}
    CHECK(f.submits==2&&f.loaded==initialLoaded+2&&f.reserve==6&&f.starts==1&&f.cancels==unsigned(initialLoaded==6));
    CHECK(f.consumer->ProbeState(f.now).acquired==2&&f.consumer->ProbeState(f.now).completed==2);
    CHECK(f.requests.size()==2&&f.receipts.size()==2);
    const auto& a=f.requests[0];const auto& b=f.requests[1];
    CHECK(a.reservation.item!=b.reservation.item&&a.reservation.claim!=b.reservation.claim&&a.request.id!=b.request.id);
    CHECK(a.heldLease.cycle==b.heldLease.cycle&&a.heldLease.sequence<b.heldLease.sequence);
    CHECK(b.heldLease.loaded==a.heldLease.loaded+1&&b.heldLease.reserve==a.heldLease.reserve-1);
    CHECK(f.receipts[0].acknowledgement.semantic.request==a.request.id&&f.receipts[1].acknowledgement.semantic.request==b.request.id);
    CHECK(f.receipts[0].acknowledgement.serverInvocation!=f.receipts[1].acknowledgement.serverInvocation);
    CHECK(Report(f.probe).find("\"requested_rounds\":2")!=std::string::npos);
    }return 0;
}
int PreparationWaitsForNativeCapacity(){
    {Loop f(2);f.loaded=7;for(unsigned n=0;n<750;++n){f.Tick();CHECK(f.starts==0&&f.submits==0&&!f.probe.CancelConsumer());}
        // Only a later actual authoritative count, never elapsed prep time,
        // makes this two-round fixture eligible for its first acquisition.
        f.loaded=6;for(unsigned n=0;n<2100&&!f.probe.CancelConsumer();++n)f.Tick();
        CHECK(f.starts==1&&f.submits==2&&f.loaded==8&&f.reserve==6);
        CHECK(Report(f.probe).find("\"actual_consumer_completed\":true")!=std::string::npos);}
    {Loop f(2);f.loaded=7;for(unsigned n=0;n<1100&&!f.probe.CancelConsumer();++n)f.Tick();
        CHECK(f.starts==0&&f.submits==0&&Report(f.probe).find("\"failure\":15")!=std::string::npos);
        CHECK(Report(f.probe).find("\"loaded\":7,\"reserve\":8,\"capacity\":8")!=std::string::npos);}
    {Loop f(2);f.reserve=1;for(unsigned n=0;n<1100&&!f.probe.CancelConsumer();++n)f.Tick();
        CHECK(f.starts==0&&f.submits==0&&Report(f.probe).find("\"failure\":16")!=std::string::npos);}
    return 0;
}
int PreparationScheduleAndNativeCooldown(){
    unsigned high=0,edges=0;bool previous=false;
    for(unsigned ms=0;ms<30000;++ms){const bool current=PhysicalReloadPreparationTrigger(ms,true)>0;
        high+=current;edges+=current&&!previous;previous=current;CHECK(PhysicalReloadPreparationTrigger(ms,false)==0);}
    CHECK(high==200&&edges==2&&PhysicalReloadPreparationTrigger(4500,true)==0&&PhysicalReloadPreparationTrigger(5000,true)==1);
    // Replay the relevant saved224357 native readiness interval. First shot
    // entered state6 at3313ms; state7 did not finish until about4625ms.
    // The old second pulse4500..4600 never intersected ready state2. There is
    // no assumption that pulse delivery changes native firing state or counts.
    unsigned oldReady=0,newReady=0;
    for(unsigned ms=4400;ms<5500;++ms){const bool nativeReady=ms>=4625;
        oldReady+=nativeReady&&ms>=4500&&ms<4600;
        newReady+=nativeReady&&PhysicalReloadPreparationTrigger(ms,true)>0;}
    CHECK(oldReady==0&&newReady==100);
    // A later hitch could still cover the new pulse; no extra trigger retry is
    // synthesized. The capacity regression above must then keep acquisition off.
    for(unsigned ms=5100;ms<30000;++ms)CHECK(PhysicalReloadPreparationTrigger(ms,true)==0);
    return 0;
}
int RepeatedBoundsAndStaleRearm(){
    for(unsigned rounds:{0u,3u}){Loop f(rounds);f.Tick();CHECK(f.probe.CancelConsumer()&&f.starts==0&&f.submits==0);}
    {Loop f(2);f.loaded=7;for(unsigned n=0;n<1100&&!f.probe.CancelConsumer();++n)f.Tick();
        CHECK(f.probe.CancelConsumer()&&f.starts==0&&f.submits==0);}
    {Loop f(2);for(unsigned n=0;n<1500&&!f.consumer->ProbeState(f.now).completed&&!f.probe.CancelConsumer();++n)f.Tick();
        CHECK(f.consumer->ProbeState(f.now).completed==1&&!f.probe.CancelConsumer());
        // Repeating an old renderer packet cannot establish fresh pouch neutral.
        const auto frozen=f.raw;for(unsigned n=0;n<450&&!f.probe.CancelConsumer();++n){f.raw=frozen;f.Tick();}
        CHECK(f.probe.CancelConsumer()&&f.submits==1&&f.consumer->ProbeState(f.now).acquired==1);}
    return 0;
}
}
int main(){if(InverseMatchesActualTrackedRig()||DisabledAndOwnerLoss()||ReceiptRequiresActualCounts()||FlagsAndBounds()||ClosedLoopThroughConsumer()||TwoRealConsumerShells()||RepeatedBoundsAndStaleRearm()||PreparationWaitsForNativeCapacity()||PreparationScheduleAndNativeCooldown())return 1;
    std::cout<<"Bc2PhysicalReloadProbe: 9 groups passed, actual TrackedRig and physical consumer repeated loop; no native/headset claim\n";}
