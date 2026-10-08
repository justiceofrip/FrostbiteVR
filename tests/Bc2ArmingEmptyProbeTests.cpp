#include "Bc2ArmingEmptyProbe.h"
#include "Bc2ReloadNativePolicy.h"
#include "Bc2ReloadConfigDescriptor.h"
#include "fvr/interaction/TrackedRig.h"
#include "Test.h"
#include <cmath>
#include <cstring>
#include <vector>
#include <sstream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
auto Matrix(const math::Pose& p){return *InverseRigid(*math::MakeLhViewFromOpenXRPose(p));}
auto Pose(float x=0,float y=0,float z=0){auto m=reload_insertion_detail::Identity();m.values[3]={x,y,z,1};return m;}
InputFrame Input(std::uint64_t generation=1){InputFrame s;s.generation=generation;s.spaceGeneration=7;s.predictedNs=1000000000+std::int64_t(generation)*10000000;s.focused=s.headValid=true;
    for(auto& h:s.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}
    s.hands[0].grip.position={-.2f,-.4f,-.3f};s.hands[1].grip.position={.2f,-.4f,-.3f};return s;}
ReloadObservedConfig Config(){
 const auto& d=SpasReloadDescriptor;ReloadObservedConfig c;
 c.weaponData=0xb0000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
 std::copy(d.assetName.begin(),d.assetName.end(),c.assetName.begin());std::copy(d.assetPath.begin(),d.assetPath.end(),c.assetPath.begin());
 const auto&v=d.values;c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
 c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;
 c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
 c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}struct Loop {
    std::int64_t now=1000000000,started=0,submittedAt=0;std::uint64_t sequence=0,cycle=0,intent=0;
    bool anatomical=false,applied=false,ackTaken=false,firing=false;unsigned deliveredReload=0;unsigned submits=0,cancels=0,starts=0;int loaded=2,reserve=8;
    std::vector<Bc2ReloadNativeRequest> requests;
    std::vector<Bc2ReloadAckEvidence> receipts;
    ReloadHoldIdentity native;PhysicalReloadApi api;std::optional<Bc2ReloadNativeRequest> request;
    std::optional<Bc2PhysicalReload> consumer;Bc2ArmingEmptyProbe probe; Bc2ReloadNativePolicy nativePolicy; bool supplyEntry=true;HandInteraction hands;std::optional<HandClaim> gun;
    TrackedRig rig;ReloadRawContact raw;std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
    explicit Loop(){native.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};native.firing={0x50000,0x60000,0x70000};native.serverPlayer=0x80000;native.serverSoldier=0x90000;native.serverItem=0xa0000;
        meshes->owner=native.owner;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
        auto& m=meshes->states[0].meshes[0];m.kind=SelectedMeshKind::Spas12;m.address=0x120000;std::memcpy(m.assetPath.data(),SpasReloadMesh.data(),SpasReloadMesh.size());
        api.context=this;api.clock=[](void* p)noexcept{return static_cast<Loop*>(p)->now;};
        api.reserve=[](void* p)noexcept->std::optional<Bc2AmmoReserveLease>{auto& f=*static_cast<Loop*>(p);
            if(f.submittedAt&&!f.applied&&f.now-f.submittedAt>=180000000){++f.loaded;--f.reserve;f.applied=true;}
            return Bc2AmmoReserveLease{f.native,100000+f.sequence,f.now,f.now+100000000,f.loaded,f.reserve,8,true,true,true};};
        api.identity=[](void* p)noexcept->std::optional<ReloadHoldIdentity>{return static_cast<Loop*>(p)->native;};
        api.start=[](void* p,const ReloadCycleControl& c)noexcept{auto& f=*static_cast<Loop*>(p);++f.starts;f.cycle=c.cycle;f.started=f.now;f.submittedAt=0;f.request.reset();f.applied=f.ackTaken=false;return f.nativePolicy.Start(c,f.now);};
        api.keep=[](void* p,const ReloadCycleControl& c)noexcept{auto& f=*static_cast<Loop*>(p);return f.nativePolicy.KeepAlive(c,f.now);};
        api.lease=[](void*,const ReloadHoldIdentity&,std::uint64_t)noexcept->std::optional<ReloadRoundLease>{return {};};
        api.submit=[](void* p,const Bc2ReloadNativeRequest& r)noexcept{auto& f=*static_cast<Loop*>(p);++f.submits;f.request=r;f.requests.push_back(r);
            f.submittedAt=f.now;f.applied=f.ackTaken=false;return true;};
        api.ack=[](void* p,const ReloadHoldIdentity& id,std::uint64_t c)noexcept->std::optional<Bc2ReloadAckEvidence>{auto& f=*static_cast<Loop*>(p);if(!f.applied||f.ackTaken)return {};
            const auto& r=f.request->request;const Bc2ReloadAckEvidence ack{{{r.id,r.owner,r.operation,ReloadAcknowledgement::Applied},id,c,f.sequence,200+f.submits},f.now,f.now+100000000,true};
            f.ackTaken=true;f.receipts.push_back(ack);return ack;};
        api.cancel=[](void* p)noexcept{auto& f=*static_cast<Loop*>(p);++f.cancels;f.nativePolicy.Cancel();};
        api.retire=[](void* p,const ReloadHoldIdentity& id,std::uint64_t c)noexcept->std::optional<ReloadCycleRetirement>{auto& f=*static_cast<Loop*>(p);if(!f.nativePolicy.DrainCancelledInvocations(true))return {};return ReloadCycleRetirement{id,c,1000+f.sequence,f.now,f.now+100000000,true};};
        consumer.emplace(true,api);
    }
    void Tick(){now+=10000000;++sequence;auto input=Input(sequence);input.hands[1].grip.position={0,-.4f,0};
        const auto trigger=probe.Fire(native.owner,true,true,api.reserve(this),now);
        if(trigger>0&&!firing&&loaded>0)--loaded;firing=trigger>0;
        probe.Prepare(input,native.owner,SpasReloadAsset,raw,consumer->ProbeState(now),now,now+100000000,now);
        HandInteractionSample hand{{(std::uint64_t(native.owner.weak)<<32)|native.owner.soldier,5,17,7},sequence,now,now+100000000,now,true,{true,true},{input.hands[0].squeeze<=.35f,false}};
        hands.Update(hand);const HandContactProof grip{{1002,1},sequence,hand.deadlineNs,true};
        if(!gun)gun=hands.Acquire(hand,{hand.owner,InteractionHand::Right,HandClaimKind::GunHold,{0x40000,17},grip,++intent,0}).claim;
        else gun=hands.Renew(hand,gun->token,grip).claim;
        meshes->sequence=sequence;meshes->observedNs=now;meshes->deadlineNs=now+200000000;
        PhysicalReloadSample s;s.nativeOwner=native.owner;s.input=hand;s.weapon={0x40000,17};s.bodyFromHand=*PhysicalReloadPouchPose(input);
        s.geometrySequence=sequence;s.trackingEpoch=7;s.gripPressed=input.hands[0].squeeze>=.75f;s.cancel=probe.CancelConsumer();
        s.asset=SpasReloadAsset;s.meshes=meshes;s.raw=raw;if(raw.valid)s.originalHandEvidence=raw.inputEvidence;
        const auto result=consumer->Tick(s,hands,intent);
        if(probe.After(consumer->ProbeState(now),result.reloadHeld,unsigned(nativePolicy.Phase()),nativePolicy.Cycle(),sequence,now))++deliveredReload;
        if(probe.CancelConsumer())consumer->Cancel(hand,hands,PhysicalReloadCancelReason::RequestedInput,ReloadCancelFixture);
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

int Orchestration(){Loop f;for(unsigned n=0;n<1500;++n)f.Tick();
 CHECK(f.probe.Current()==Bc2ArmingEmptyProbe::Phase::Done);
 CHECK(f.starts==1&&f.submits==0&&f.deliveredReload==0&&f.loaded==0&&f.reserve==8);
 CHECK(f.nativePolicy.CancellationOrigin()&&f.nativePolicy.CancellationOrigin()->prior==ReloadRequestCyclePhase::Arming);
 std::ostringstream out;f.probe.Report(out);CHECK(out.str().find("\"native_verified\":false")!=std::string::npos);
 return 0;
}
}
int LostSourceCancelsBeforeCredit(){Loop f;for(unsigned n=0;n<1100&&f.probe.Current()!=Bc2ArmingEmptyProbe::Phase::NeutralArming;++n)f.Tick();
 CHECK(f.probe.Current()==Bc2ArmingEmptyProbe::Phase::NeutralArming);
 auto state=f.consumer->ProbeState(f.now);state.reserve.reset();
 CHECK(!f.probe.After(state,true,unsigned(f.nativePolicy.Phase()),f.cycle,f.sequence,f.now));
 CHECK(f.probe.CancelConsumer()&&f.probe.Current()==Bc2ArmingEmptyProbe::Phase::Failed);
 std::ostringstream before;f.probe.Report(before);CHECK(before.str().find("\"failure\":4")!=std::string::npos);
 f.probe.Cancel();f.probe.Cancel();std::ostringstream after;f.probe.Report(after);CHECK(before.str()==after.str());
 CHECK(after.str().find("\"empty_fire\":{")!=std::string::npos);
 CHECK(f.submits==0&&f.deliveredReload==0&&f.loaded==0&&f.reserve==8);return 0;
}
int RejectedEvidenceNeverReplaysEdge(){
 for(unsigned mode=0;mode<3;++mode){Loop f;f.probe.Cancel();CHECK(f.probe.Current()==Bc2ArmingEmptyProbe::Phase::Emptying);
  for(unsigned n=0;n<1100&&f.probe.Current()!=Bc2ArmingEmptyProbe::Phase::NeutralArming;++n)f.Tick();
  CHECK(f.probe.Current()==Bc2ArmingEmptyProbe::Phase::NeutralArming);auto state=f.consumer->ProbeState(f.now);
  auto phase=unsigned(f.nativePolicy.Phase());auto cycle=f.cycle;
  if(mode==0)++state.identity.owner.equipGeneration;
  if(mode==1)++cycle;
  if(mode==2)state.reserve->deadlineNs=f.now;
  CHECK(!f.probe.After(state,true,phase,cycle,f.sequence,f.now));CHECK(f.probe.CancelConsumer());
  CHECK(f.submits==0&&f.deliveredReload==0&&f.loaded==0&&f.reserve==8);
 }
 return 0;
}
int EmptyingGap(){Loop f;
 CHECK(f.probe.Fire(f.native.owner,true,true,f.api.reserve(&f),f.now)==0);
 f.now+=1000000;++f.sequence;CHECK(f.probe.Fire(f.native.owner,true,true,f.api.reserve(&f),f.now)>0);
 f.now+=1000000;CHECK(f.probe.Fire(f.native.owner,true,true,std::nullopt,f.now)==0);
 CHECK(f.probe.Current()==Bc2ArmingEmptyProbe::Phase::Emptying);
 f.now+=1000000;++f.sequence;f.probe.Fire(f.native.owner,true,true,f.api.reserve(&f),f.now);
 CHECK(f.probe.Current()==Bc2ArmingEmptyProbe::Phase::Emptying);
 f.probe.Fire(f.native.owner,false,true,f.api.reserve(&f),f.now);CHECK(f.probe.Current()==Bc2ArmingEmptyProbe::Phase::Failed);
 std::ostringstream first;f.probe.Report(first);f.probe.Cancel();std::ostringstream final;f.probe.Report(final);CHECK(first.str()==final.str());
 CHECK(final.str().find("\"empty_fire\":{")!=std::string::npos);return 0;}
int main(){if(EmptyingGap()||Orchestration()||LostSourceCancelsBeforeCredit()||RejectedEvidenceNeverReplaysEdge())return 1;std::puts("Actual empty-Arming consumer orchestration passed; native Step evidence not fabricated");}
