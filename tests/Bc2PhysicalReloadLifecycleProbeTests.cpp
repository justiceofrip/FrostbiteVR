#include "Bc2PhysicalReloadLifecycleProbe.h"
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
    bool anatomical=false,applied=false,ackTaken=false;unsigned submits=0,cancels=0,starts=0;int loaded=2,reserve=8;
    std::vector<Bc2ReloadNativeRequest> requests;
    std::vector<Bc2ReloadAckEvidence> receipts;
    ReloadHoldIdentity native;PhysicalReloadApi api;std::optional<Bc2ReloadNativeRequest> request;
    std::optional<Bc2PhysicalReload> consumer;Bc2PhysicalReloadLifecycleProbe probe; Bc2ReloadNativePolicy nativePolicy; bool supplyEntry=true;HandInteraction hands;std::optional<HandClaim> gun;
    TrackedRig rig;ReloadRawContact raw;std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
    explicit Loop(PhysicalReloadLifecycleScenario scenario):probe(true,1,scenario){native.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};native.firing={0x50000,0x60000,0x70000};native.serverPlayer=0x80000;native.serverSoldier=0x90000;native.serverItem=0xa0000;
        meshes->owner=native.owner;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
        auto& m=meshes->states[0].meshes[0];m.kind=SelectedMeshKind::Spas12;m.address=0x120000;std::memcpy(m.assetPath.data(),SpasReloadMesh.data(),SpasReloadMesh.size());
        api.context=this;api.clock=[](void* p)noexcept{return static_cast<Loop*>(p)->now;};
        api.reserve=[](void* p)noexcept->std::optional<Bc2AmmoReserveLease>{auto& f=*static_cast<Loop*>(p);
            if(f.submittedAt&&!f.applied&&f.now-f.submittedAt>=180000000){++f.loaded;--f.reserve;f.applied=true;}
            return Bc2AmmoReserveLease{f.native,100000+f.sequence,f.now,f.now+100000000,f.loaded,f.reserve,8,true,true};};
        api.identity=[](void* p)noexcept->std::optional<ReloadHoldIdentity>{return static_cast<Loop*>(p)->native;};
        api.start=[](void* p,const ReloadCycleControl& c)noexcept{auto& f=*static_cast<Loop*>(p);++f.starts;f.cycle=c.cycle;f.started=f.now;f.submittedAt=0;f.request.reset();f.applied=f.ackTaken=false;return f.nativePolicy.Start(c,f.now);};
        api.keep=[](void* p,const ReloadCycleControl& c)noexcept{auto& f=*static_cast<Loop*>(p);return f.nativePolicy.KeepAlive(c,f.now);};
        api.lease=[](void* p,const ReloadHoldIdentity& id,std::uint64_t c)noexcept->std::optional<ReloadRoundLease>{auto& f=*static_cast<Loop*>(p);
            if(!f.started||f.now-f.started<150000000)return {};return ReloadRoundLease{id,c,f.sequence,f.now,f.now+100000000,f.loaded,f.reserve,8,true,!f.submittedAt||f.applied};};
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
        if(nativePolicy.Phase()==ReloadRequestCyclePhase::Arming&&now-started>=150000000){
            ReloadHoldInput held;held.identity=native;held.verified=true;held.config=Config();held.nowNs=now;held.leaseDeadlineNs=now+100000000;
            held.context.deltaSeconds=.005f;held.context.reloadTimeMultiplier=1;held.context.flags24Through28[0]=true;
            for(unsigned n=0;n<3;++n){auto& x=held.branches[n];x.address=native.firing[n];x.wrapperOffset=n==0?0x3c:n==1?0x40:0x10;x.currentState=11;x.nextState=12;x.phaseTimer=.2f;x.loaded=loaded;x.reserve=reserve;held.capacities[n]=8;}
            for(unsigned n=0;n<3;++n){held.branch=n;const auto d=nativePolicy.Evaluate(held,true,true,10000+sequence*3+n,now);if(d.tracked){ReloadDeltaOverride patch;patch.applied=patch.restored=true;patch.original=1;nativePolicy.Finish(d,held.branches[n],now,true,patch);}}
        }
        if(supplyEntry&&nativePolicy.Cycle()&&now-started>=30000000)
            probe.Entry(ReloadPreholdEntryObservation{native,cycle,sequence,now,now+100000000,7,nativePolicy.Phase()});
        else probe.Entry({});
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

int StartupEpochChangesThenRealConsumer(){
 Loop f(PhysicalReloadLifecycleScenario::ArmingCancel);
 // Actual run starts equipgen2 ->3 ->4 before any operation. Keep raw/reserve
 // readiness windows shorter than 500ms while publication settles; zero claims.
 f.native.owner.equipGeneration=2;f.meshes->owner=f.native.owner;
 for(unsigned n=0;n<15;++n)f.Tick();CHECK(f.starts==0&&f.cancels==0);
 f.native.owner.equipGeneration=3;f.meshes->owner=f.native.owner;
 for(unsigned n=0;n<25;++n)f.Tick();CHECK(f.starts==0&&f.cancels==0);
 f.native.owner.equipGeneration=4;f.meshes->owner=f.native.owner;
 for(unsigned n=0;n<2200;++n)f.Tick();std::ostringstream out;f.probe.Report(out);
 CHECK(out.str().find("\"done\":true")!=std::string::npos);CHECK(out.str().find("\"failed\":false")!=std::string::npos);
 CHECK(f.starts==1&&f.submits==0&&f.loaded==2&&f.reserve==8);return 0;
}
int CommittedOwnerLossStillFails(){
 Loop f(PhysicalReloadLifecycleScenario::ArmingCancel);for(unsigned n=0;n<80;++n)f.Tick();
 f.native.owner.equipGeneration++;f.meshes->owner=f.native.owner;for(unsigned n=0;n<10;++n)f.Tick();
 std::ostringstream out;f.probe.Report(out);CHECK(out.str().find("\"failed\":true")!=std::string::npos);
 CHECK(f.starts==0&&f.submits==0);return 0;
}
int MissingStartupInputTimesOutWithoutOperation(){
 Loop f(PhysicalReloadLifecycleScenario::ArmingCancel);f.sequence=0;
 // No matching current tracking space: wait neutrally, then fail bounded.
 f.native.owner.space=8;f.meshes->owner=f.native.owner;
 for(unsigned n=0;n<350;++n)f.Tick();std::ostringstream out;f.probe.Report(out);
 CHECK(out.str().find("\"failed\":true")!=std::string::npos);
 CHECK(out.str().find("\"reason\":2")!=std::string::npos);
 CHECK(f.starts==0&&f.submits==0&&f.cancels==0&&f.loaded==2&&f.reserve==8);return 0;
}
int TerminalReceiptSurvivesLaterSourceExpiry(){
 Loop f(PhysicalReloadLifecycleScenario::RepeatArmingCancel);
 for(unsigned n=0;n<2100;++n)f.Tick();std::ostringstream accepted;f.probe.Report(accepted);
 CHECK(accepted.str().find("\"done\":true")!=std::string::npos);
 CHECK(accepted.str().find("\"failed\":false")!=std::string::npos);
 const auto starts=f.starts;const auto cancels=f.cancels;
 auto state=f.consumer->ProbeState(f.now);state.reserve.reset();
 for(unsigned n=0;n<800;++n){f.now+=10000000;auto input=Input(++f.sequence);
  f.probe.Prepare(input,f.native.owner,SpasReloadAsset,f.raw,state,f.now,f.now+100000000,f.now);
  CHECK(input.hands[0].squeeze==0);f.probe.Observe(state,f.now);}
 std::ostringstream after;f.probe.Report(after);CHECK(after.str()==accepted.str());
 CHECK(f.starts==starts&&f.cancels==cancels&&f.loaded==2&&f.reserve==8);return 0;
}
int ObservationBounds(){
 Loop f(PhysicalReloadLifecycleScenario::ArmingCancel);
 ReloadPreholdEntryObservation e{f.native,1,2,100,100000100,7,ReloadRequestCyclePhase::Arming};
 CHECK(FreshPreholdEntry(e,f.native,1,101));
 auto bad=e;bad.enteredMask=8;CHECK(!FreshPreholdEntry(bad,f.native,1,101));
 bad=e;++bad.cycle;CHECK(!FreshPreholdEntry(bad,f.native,1,101));
 bad=e;++bad.identity.owner.space;CHECK(!FreshPreholdEntry(bad,f.native,1,101));
 CHECK(!FreshPreholdEntry(e,f.native,1,99));CHECK(!FreshPreholdEntry(e,f.native,1,e.deadlineNs));return 0;
}
int Scenarios(){
 for(auto scenario:{PhysicalReloadLifecycleScenario::ArmingCancel,PhysicalReloadLifecycleScenario::HoldingCancel,PhysicalReloadLifecycleScenario::RepeatArmingCancel,PhysicalReloadLifecycleScenario::CompleteThenArmingCancel}){
  Loop f(scenario);for(unsigned n=0;n<2900;++n)f.Tick();std::ostringstream report;f.probe.Report(report);
  CHECK(report.str().find("\"done\":true")!=std::string::npos);CHECK(report.str().find("\"failed\":false")!=std::string::npos);
  const auto origin=f.nativePolicy.CancellationOrigin();CHECK(origin);
  CHECK(origin->prior==(scenario==PhysicalReloadLifecycleScenario::HoldingCancel?ReloadRequestCyclePhase::Holding:ReloadRequestCyclePhase::Arming));
  const bool completed=scenario==PhysicalReloadLifecycleScenario::CompleteThenArmingCancel;
  CHECK(f.loaded==(completed?3:2)&&f.reserve==(completed?7:8));CHECK(f.starts==(scenario==PhysicalReloadLifecycleScenario::RepeatArmingCancel||completed?2u:1u));
  CHECK(f.submits==(completed?1u:0u));
 }
 return 0;
}
int MissingEntryNeverArmingCancels(){Loop f(PhysicalReloadLifecycleScenario::ArmingCancel);f.supplyEntry=false;for(unsigned n=0;n<700;++n)f.Tick();CHECK(f.starts==1);CHECK(f.cancels==1&&f.submits==0);CHECK(f.nativePolicy.CancellationOrigin()->prior==ReloadRequestCyclePhase::Holding);std::ostringstream report;f.probe.Report(report);CHECK(report.str().find("\"failed\":true")!=std::string::npos);return 0;}
}
int main(){if(TerminalReceiptSurvivesLaterSourceExpiry()||StartupEpochChangesThenRealConsumer()||CommittedOwnerLossStillFails()||MissingStartupInputTimesOutWithoutOperation()||ObservationBounds()||Scenarios()||MissingEntryNeverArmingCancels())return 1;std::puts("Public lifecycle fixture real physical consumer/policy scenarios passed; no native evidence claim");}
