#include "Bc2MagazinePhysicalProbe.h"
#include "Bc2PhysicalReload.h"
#include "fvr/interaction/TrackedRig.h"
#include "Test.h"
#include "fvr/interaction/ReloadGrip.h"
#include "fvr/interaction/BodyAnchors.h"
#include <cstring>
#include <iostream>
#include <sstream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;using namespace fvr::interaction::reload_insertion_detail;
namespace {
constexpr std::int64_t Ms=1000000;
auto Pose(float x=0,float y=0,float z=0){auto m=Identity();m.values[3]={x,y,z,1};return m;}
auto Matrix(const math::Pose& p){return *InverseRigid(*math::MakeLhViewFromOpenXRPose(p));}
InputFrame Input(std::uint64_t sequence){InputFrame in;in.generation=sequence;in.spaceGeneration=7;in.predictedNs=1000*Ms+std::int64_t(sequence)*10*Ms;
 in.focused=in.headValid=true;for(auto& h:in.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}
 in.hands[0].grip.position={-.2f,-.4f,-.3f};in.hands[1].grip.position={0,-.25f,-.45f};return in;}
std::string Report(const Bc2MagazinePhysicalProbe& p){std::ostringstream o;p.Report(o);return o.str();}
template<class Probe> bool Completed(const Probe& p){
 if constexpr(requires {p.Completed();})return p.Completed();
 else return Report(p).find("\"actual_consumer_completed\":true")!=std::string::npos;
}
template<class Probe> Probe ProbeFor(bool original,bool carry,bool sequence=false){
 if constexpr(requires {Probe(true,original,carry,sequence);})return Probe(true,original,carry,sequence);
 else if constexpr(requires {Probe(true,original,carry);})return Probe(true,original,carry);
 else return Probe(true,original);
}
float Metric(const std::string& report,const std::string& field){const auto key="\""+field+"\":";const auto at=report.find(key);
 return at==std::string::npos?0:std::stof(report.substr(at+key.size()));}
struct Loop {
 std::int64_t now=1000*Ms,started=0,submittedAt=0,retiredAt=0;std::uint64_t sequence=0,cycle=0,intent=0;
 bool freezeCarry=false,anatomical=true,applied=false,gateTaken=false,ackTaken=false,publishPairs=true,allowReceipt=true,allowAttached=true,allowRetirement=true,allowIdle=true,changeOnCancel=false;
 bool observedHalfPull=false,observedHalfCarry=false,shortCarryLease=false,shortIssued=false,chest=false;
 std::int64_t gapAt=0;
 unsigned starts=0,submits=0,cancels=0;int loaded=27,reserve=83;
 ReloadHoldIdentity native{};MagazinePhysicalApi api{};std::optional<ManualReloadRequest> unseat;
 std::optional<ReloadMagazineNativeRequest> request;
 std::optional<Bc2MagazinePhysicalReload> consumer;Bc2MagazinePhysicalProbe probe{true};MagazinePackCounters packs{};
 HandInteraction hands;std::optional<HandClaim> gun;TrackedRig rig;MagazineRawContact raw{};
 std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
 explicit Loop(bool original=false,bool carry=false,bool sequence=false,bool body=false):chest(body),probe(true,original,carry,sequence,body){native.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};native.firing={0x50000,0x60000,0x70000};
 native.serverPlayer=0x80000;native.serverSoldier=0x90000;native.serverItem=0xa0000;
 meshes->owner=native.owner;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
 auto& m=meshes->states[0].meshes[0];m.kind=SelectedMeshKind::Xm8;m.address=0x120000;std::memcpy(m.assetPath.data(),Xm8MagazineMesh.data(),Xm8MagazineMesh.size());
 api.context=this;api.clock=[](void* p)noexcept{return static_cast<Loop*>(p)->now;};
 api.reserve=[](void* p)noexcept->std::optional<Bc2AmmoReserveLease>{auto& f=*static_cast<Loop*>(p);
  if(f.submittedAt&&!f.applied&&f.now-f.submittedAt>=300*Ms){f.loaded+=int(f.request->reservedUnits);f.reserve-=int(f.request->reservedUnits);f.applied=true;}
  return Bc2AmmoReserveLease{f.native,100000+f.sequence,f.now,f.now+100*Ms,f.loaded,f.reserve,30,true,!f.started||(f.allowIdle&&f.retiredAt&&f.now-f.retiredAt>=100*Ms)};};
 api.identity=[](void* p)noexcept->std::optional<ReloadHoldIdentity>{return static_cast<Loop*>(p)->native;};
 api.start=[](void* p,const ReloadCycleControl& c,const ManualReloadRequest& r,const ReloadMagazineStartupPulse& pulse)noexcept{auto& f=*static_cast<Loop*>(p);++f.starts;f.started=f.now;f.cycle=c.cycle;f.unseat=r;
  if(pulse.control!=c||pulse.endNs!=c.observedNs+100000000||pulse.endNs>c.deadlineNs)return MagazineCycleStartResult::NotStarted;
  f.retiredAt=0;f.gateTaken=false;f.ackTaken=false;f.applied=false;f.submittedAt=0;f.request.reset();return MagazineCycleStartResult::Started;};
 api.inspectStart=[](void*,const ReloadHoldIdentity&,std::uint64_t,const std::optional<ReloadMagazineStartupPulse>&)noexcept{return MagazineCycleStartResult::Unknown;};
 api.keep=[](void*,const ReloadCycleControl&)noexcept{return true;};
 api.lease=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadMagazineLease>{auto& f=*static_cast<Loop*>(p);
  if(!f.started||f.now-f.started<150*Ms)return {};
  const bool shorten=f.shortCarryLease&&!f.shortIssued&&Report(f.probe).find("\"phase\":14")!=std::string::npos;
  if(shorten){f.shortIssued=true;f.gapAt=f.now;}
  return ReloadMagazineLease{id,cycle,f.sequence,f.now,f.now+(shorten?10:100)*Ms,f.loaded,f.reserve,30,true,!f.submittedAt};};
 api.gate=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadMagazineGateAcknowledgement>{auto& f=*static_cast<Loop*>(p);
  if(!f.started||f.now-f.started<150*Ms||f.gateTaken)return {};f.gateTaken=true;
  return ReloadMagazineGateAcknowledgement{{f.unseat->id,f.unseat->owner,ReloadOperation::UnseatMagazine,ReloadAcknowledgement::Applied},
   {id,cycle,f.sequence,f.now,f.now+100*Ms,f.loaded,f.reserve,30,true,true}};};
 api.submit=[](void* p,const ReloadMagazineNativeRequest& r)noexcept{auto& f=*static_cast<Loop*>(p);++f.submits;f.submittedAt=f.now;f.request=r;return true;};
 api.ack=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadMagazineAckEvidence>{auto& f=*static_cast<Loop*>(p);
  if(!f.applied||f.ackTaken||!f.allowReceipt)return {};f.ackTaken=true;const auto& r=*f.request;
  return ReloadMagazineAckEvidence{{{r.request.id,r.request.owner,r.request.operation,ReloadAcknowledgement::Applied},id,cycle,f.sequence,900,
   r.heldLease.loaded,r.heldLease.reserve,f.loaded,f.reserve},f.now,f.now+100*Ms,true};};
 api.cancel=[](void* p)noexcept{auto& f=*static_cast<Loop*>(p);++f.cancels;f.retiredAt=f.now;if(f.changeOnCancel){++f.loaded;--f.reserve;}};
 api.retire=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadCycleRetirement>{auto& f=*static_cast<Loop*>(p);
  if(!f.retiredAt||!f.allowRetirement)return {};return ReloadCycleRetirement{id,cycle,1000+f.sequence,f.now,f.now+200*Ms,true};};
 auto pouch=chest?ChestAmmoSupply():Bc2MagazinePhysicalReload::DefaultPouch();
 const auto ids=Bc2MagazinePhysicalReload::DefaultPouch();pouch.itemNamespace=ids.itemNamespace;pouch.pouch=ids.pouch;
 consumer.emplace(true,api,pouch);
 }
 void Tick(){const auto priorRaw=raw.rawLeftWristWorldMeters;bool frozen=false;now+=10*Ms;++sequence;auto in=Input(sequence);
  probe.Prepare(in,native.owner,Xm8MagazineAsset,raw,consumer->ProbeState(now),now,now+100*Ms,now);
  frozen=freezeCarry&&Report(probe).find("\"phase\":14")!=std::string::npos;
  if(in.hands[0].squeeze>.35f&&in.hands[0].squeeze<.75f){
   observedHalfCarry|=Report(probe).find("\"phase\":14")!=std::string::npos;
   observedHalfPull|=Report(probe).find("\"phase\":3")!=std::string::npos;
  }
  HandInteractionSample h{{(std::uint64_t(native.owner.weak)<<32)|native.owner.soldier,5,17,7},sequence,now,now+100*Ms,now,true,{true,true},{in.hands[0].squeeze<=.35f,false}};
  hands.Update(h);HandContactProof proof{{1002,1},sequence,h.deadlineNs,true};
  if(gun)gun=hands.Renew(h,gun->token,proof).claim;
  if(!gun)gun=hands.Acquire(h,{h.owner,InteractionHand::Right,HandClaimKind::GunHold,{0xb0000,17},proof,++intent,0}).claim;
  meshes->sequence=sequence;meshes->observedNs=now;meshes->deadlineNs=now+200*Ms;
  MagazinePhysicalSample s;s.nativeOwner=native.owner;s.input=h;s.weapon={0xb0000,17};s.trackingEpoch=7;s.geometrySequence=sequence;
  s.family={{native.owner,s.weapon,0xd0000,0xc0000,2},now,now+100*Ms,true};
  s.bodyFromHand=*(chest?BodyAnchorHandPose(in,InteractionHand::Left):PhysicalReloadPouchPose(in));s.gripPressed=ReloadGripActive(in.hands[0].squeeze,h,hands.Current(InteractionHand::Left));s.cancel=probe.CancelConsumer();s.asset=Xm8MagazineAsset;s.meshes=meshes;s.raw=raw;
  if(raw.valid)s.originalHandEvidence=raw.inputEvidence;
  const auto result=consumer->Tick(s,hands,intent);
  // Test-owned mock renderer receipt. Production reads actual verified Pack
  // counters; the fixture itself has no writer for these values.
  if(publishPairs&&result.tracking.target&&MagazineTargetFresh(result.tracking,now)){
   ++packs.pairs;packs.copies+=2;const auto role=unsigned(result.tracking.target->role);++packs.rolePairs[role];packs.roleCopies[role]+=2;}
  probe.Observe(consumer->ProbeState(now),packs,now);
  const auto body=Pose(5,2,3),left=Multiply(Pose(-.2f,-.4f,.3f),body),right=Multiply(Pose(0,-.25f,.45f),body);
  std::array<ArmAnchor,2> arms{{{{4.8f,1.8f,3},{0,1,0}},{{5.2f,1.8f,3},{0,1,0}}}};
  const auto posed=rig.Update({1,2,3,4},in,body,left,right,right,arms);
  if(posed){raw.valid=true;raw.owner=native.owner;raw.rigFingerprint=Xm8MagazineRig;raw.inputEvidence=h;
   raw.rawLeftWristWorldMeters=posed->left;raw.weaponWorldMeters=posed->weapon;raw.trackingBodyWorldMeters=body;
   raw.nativeMagazineAttached=!started||(allowAttached&&((applied&&now-submittedAt>600*Ms)||(retiredAt&&now-retiredAt>=100*Ms)));
   if(anatomical){auto attachment=Pose();attachment.values[1][1]=attachment.values[2][2]=-1;
    auto wrist=Multiply(attachment,Multiply(Matrix(in.hands[0].grip),body));wrist.values[3]=posed->left.values[3];raw.rawLeftWristWorldMeters=wrist;}
   if(frozen)raw.rawLeftWristWorldMeters=priorRaw;
  }
 }
 bool Run(){for(unsigned n=0;n<3000&&!probe.CancelConsumer();++n)Tick();return Completed(probe);}
};
int CarryPreviousLeaseMayExpire(){
 Loop f(false,true);f.shortCarryLease=true;CHECK(f.Run());CHECK(f.shortIssued);
 CHECK(Metric(Report(f.probe),"carry_wait_packets")>=1);CHECK(f.starts==1&&f.submits==1);
 CHECK(Metric(Report(f.probe),"carry_failure_flags")==0);return 0;
}
int CarryMissingHoldIsBounded(){
 Loop f(false,true);f.shortCarryLease=true;
 while(!f.gapAt&&!f.probe.CancelConsumer())f.Tick();CHECK(f.gapAt);
 auto state=f.consumer->ProbeState(f.now);state.nativeHolding=false;
 MagazineRawContact noRaw;
 for(unsigned n=0;n<7&&!f.probe.CancelConsumer();++n){f.now+=10*Ms;auto in=Input(++f.sequence);
  f.probe.Prepare(in,f.native.owner,Xm8MagazineAsset,noRaw,state,f.now,f.now+100*Ms,f.now);}
 CHECK(Metric(Report(f.probe),"failure")==20);
 CHECK(Metric(Report(f.probe),"carry_failure_flags")==48);CHECK(f.submits==0);
 CHECK(f.now-f.gapAt==60*Ms);return 0;
}
int CarryWaitDoesNotInventMotion(){
 Loop f(false,true);f.shortCarryLease=true;
 while(!f.gapAt&&!f.probe.CancelConsumer())f.Tick();CHECK(f.gapAt);
 const auto before=Report(f.probe);const auto samples=Metric(before,"carry_raw_samples");
 f.Tick();CHECK(Metric(Report(f.probe),"carry_raw_samples")==samples);
 CHECK(f.submits==0&&f.consumer->ProbeState(f.now).phase==DetachableMagazinePhase::RemovedHeld);
 // The real consumer still wins an explicit cancellation while waiting.
 f.consumer->Cancel(f.raw.inputEvidence,f.hands);f.Tick();CHECK(f.probe.CancelConsumer());return 0;
}
int CarryWaitRejectsLostControl(){
 for(unsigned flag:{1u,2u,4u}){Loop f(false,true);f.shortCarryLease=true;
  while(!f.gapAt&&!f.probe.CancelConsumer())f.Tick();CHECK(f.gapAt);
  auto state=f.consumer->ProbeState(f.now);state.nativeHolding=false;
  if(flag==1)state.active=false;if(flag==2)state.ownsHand=false;
  if(flag==4)state.phase=DetachableMagazinePhase::WellEmpty;
  f.now+=10*Ms;auto in=Input(++f.sequence);
  f.probe.Prepare(in,f.native.owner,Xm8MagazineAsset,f.raw,state,f.now,f.now+100*Ms,f.now);
  CHECK(Metric(Report(f.probe),"failure")==17);
  CHECK(Metric(Report(f.probe),"carry_failure_flags")==float(flag|16u));CHECK(f.submits==0);
 }return 0;
}
int RepeatedOriginalThenReplacement(){
 for(bool anatomical:{false,true}){Loop f(false,true,true);f.anatomical=anatomical;const bool pass=f.Run();
  if(!pass){std::cerr<<Report(f.probe)<<'\n';f.consumer->Report(std::cerr);}
  CHECK(pass&&f.starts==2&&f.submits==1&&f.loaded==30&&f.reserve==80);
  const auto state=f.consumer->ProbeState(f.now);CHECK(state.originalReturns==1&&state.completed==1&&state.acquired==1);
  const auto report=Report(f.probe);CHECK(report.find("\"second_cycle\":true")!=std::string::npos);
  CHECK(report.find("\"first_cycle\":{")!=std::string::npos&&Metric(report,"original_return_retirement")>0);
  CHECK(f.observedHalfCarry&&f.observedHalfPull);
 }return 0;
}
int RepeatRequiresFirstReturnRetirement(){Loop f(false,true,true);f.allowRetirement=false;CHECK(!f.Run());
 CHECK(f.starts==1&&f.submits==0&&f.loaded==27&&f.reserve==83);
 CHECK(Report(f.probe).find("\"second_cycle\":true")==std::string::npos);return 0;}
int RepeatStillRequiresObservedCarry(){Loop f(false,true,true);f.freezeCarry=true;CHECK(!f.Run());
 CHECK(f.starts==2&&f.submits==0&&f.consumer->ProbeState(f.now).originalReturns==1&&f.loaded==27&&f.reserve==83);return 0;}
int FreshFreeCarryThroughRealConsumer(){
 for(bool anatomical:{false,true}){Loop f(false,true);f.anatomical=anatomical;CHECK(f.Run());
  const auto report=Report(f.probe);
  CHECK(report.find("\"carry_exercised\":true")!=std::string::npos);
  CHECK(Metric(report,"carry_displacement_m")>=.10f&&Metric(report,"carry_angle_rad")>=.5f);
  CHECK(Metric(report,"carry_max_raw_step_m")>.07f&&Metric(report,"carry_max_raw_step_rad")>.5f);
  CHECK(f.starts==1&&f.submits==1&&f.consumer->ProbeState(f.now).completed==1&&f.loaded==30&&f.reserve==80);
  CHECK(f.observedHalfPull&&f.observedHalfCarry);
 }return 0;
}
int CarryCoverageRequiresObservedMotion(){Loop f(false,true);f.freezeCarry=true;CHECK(!f.Run());
 CHECK(f.submits==0&&f.loaded==27&&f.reserve==83);
 const auto report=Report(f.probe);CHECK(report.find("\"carry_exercised\":false")!=std::string::npos);
 CHECK(Metric(report,"carry_displacement_m")<.001f&&Metric(report,"carry_max_raw_step_m")<.001f);
 CHECK(f.probe.CancelConsumer());return 0;
}
int ClosedLoopRealPolicies(){for(bool anatomical:{false,true}){Loop f;f.anatomical=anatomical;const bool pass=f.Run();
 if(!pass){std::cerr<<Report(f.probe)<<'\n';f.consumer->Report(std::cerr);}
 CHECK(pass&&f.starts==1&&f.submits==1&&f.loaded==30&&f.reserve==80);CHECK(f.consumer->ProbeState(f.now).completed==1);
 CHECK(f.packs.rolePairs[1]&&f.packs.rolePairs[2]&&f.packs.rolePairs[3]);}return 0;}
int PackEvidenceCannotBeInvented(){Loop f;f.publishPairs=false;CHECK(!f.Run()&&f.submits==0&&f.loaded==27&&f.reserve==83);
 CHECK(Report(f.probe).find("\"removed_pair\":0")!=std::string::npos);return 0;}
int NativeReceiptCannotBeInferredFromCounts(){Loop f;f.allowReceipt=false;CHECK(!f.Run()&&f.submits==1&&f.loaded==30);
 CHECK(Report(f.probe).find("\"native_receipt\":false")!=std::string::npos&&f.consumer->ProbeState(f.now).completed==0);return 0;}
int AttachedBaselineRequiredAfterReceipt(){Loop f;f.allowAttached=false;CHECK(!f.Run()&&f.submits==1&&f.consumer->ProbeState(f.now).completed==1);
 CHECK(Report(f.probe).find("\"native_receipt\":true")!=std::string::npos);return 0;}
int FullOrEmptyPreflightNeverBegins(){for(int loaded:{0,30}){Loop f;f.loaded=loaded;CHECK(!f.Run()&&f.starts==0&&f.submits==0&&f.loaded==loaded);}
 return 0;}
int OriginalReturnThroughRealConsumer(){
 for(bool anatomical:{false,true}){Loop f(true);f.anatomical=anatomical;const bool pass=f.Run();
  if(!pass){std::cerr<<Report(f.probe)<<'\n';f.consumer->Report(std::cerr);}
  CHECK(pass&&f.starts==1&&f.cancels==1&&f.submits==0&&f.loaded==27&&f.reserve==83);
  const auto state=f.consumer->ProbeState(f.now);
  CHECK(state.originalReturns==1&&state.acquired==0&&state.submitted==0&&state.completed==0);
  CHECK(!state.active&&!state.retiring&&!state.pending&&!state.blocksEquipment&&!state.ownsHand);
  CHECK(state.originalReceipt&&state.originalReceipt->verified&&state.originalReceipt->original.rounds==27);
  CHECK(f.packs.rolePairs[1]>0&&f.packs.rolePairs[2]==0&&f.packs.rolePairs[3]==0);
  const auto report=Report(f.probe);CHECK(report.find("\"original_return_fixture\":true")!=std::string::npos);
  CHECK(report.find("\"expected_units\":0")!=std::string::npos);
 }return 0;
}
int OriginalReturnRequiresActualEvidence(){
 for(unsigned failure=0;failure<5;++failure){Loop f(true);
  if(failure==0)f.publishPairs=false;
  if(failure==1)f.allowRetirement=false;
  if(failure==2)f.allowIdle=false;
  if(failure==3)f.allowAttached=false;
  if(failure==4)f.changeOnCancel=true;
  CHECK(!f.Run());CHECK(f.submits==0);CHECK(f.consumer->ProbeState(f.now).originalReturns==0);
  if(failure!=4)CHECK(f.loaded==27&&f.reserve==83);
 }return 0;
}
int OriginalReturnRejectsUnexpectedSupplyOrReceipt(){
 for(unsigned mutation=0;mutation<5;++mutation){Loop f(true);
  for(unsigned i=0;i<1600&&!f.consumer->ProbeState(f.now).originalReturning;++i)f.Tick();
  auto state=f.consumer->ProbeState(f.now);CHECK(state.originalReturning);
  if(mutation==0)state.acquired=1;
  if(mutation==1)state.submitted=1;
  if(mutation==2){state.originalReturns=1;state.originalReceipt.reset();}
  if(mutation==3){CHECK(state.original);state.original->item.id++;}
  if(mutation==4){CHECK(state.reserve);state.reserve->loaded++;}
  f.probe.Observe(state,f.packs,f.now);CHECK(f.probe.CancelConsumer());
  CHECK(Report(f.probe).find("\"actual_consumer_completed\":false")!=std::string::npos);
 }return 0;
}
int ChestSupplySequence(){Loop f(false,true,true,true);CHECK(f.Run());CHECK(f.starts==2&&f.submits==1&&f.loaded==30&&f.reserve==80);
 CHECK(Report(f.probe).find("\"chest_supply\":true")!=std::string::npos);return 0;}
int DisabledAndBounded(){Bc2MagazinePhysicalProbe off;auto in=Input(1);const auto original=in;off.Prepare(in,{},"",{},{},1000*Ms,1100*Ms,1000*Ms);
 CHECK(in.hands[0].grip.position.x==original.hands[0].grip.position.x&&!off.CancelConsumer());
 Bc2MagazinePhysicalProbe probe(true);ReloadStateOwner owner{0x10000,0x20000,0x30000,0x40000,5,3,7};
 probe.Prepare(in,owner,Xm8MagazineAsset,{},{},1000*Ms,1100*Ms,1000*Ms);in.generation++;
 probe.Prepare(in,owner,Xm8MagazineAsset,{},{},31000*Ms,31100*Ms,31000*Ms);
 CHECK(probe.CancelConsumer()&&in.hands[0].squeeze==0);return 0;}
}
int main(){if(ChestSupplySequence()||CarryWaitRejectsLostControl()||CarryPreviousLeaseMayExpire()||CarryMissingHoldIsBounded()||CarryWaitDoesNotInventMotion()||RepeatedOriginalThenReplacement()||RepeatRequiresFirstReturnRetirement()||RepeatStillRequiresObservedCarry()||FreshFreeCarryThroughRealConsumer()||CarryCoverageRequiresObservedMotion()||OriginalReturnThroughRealConsumer()||OriginalReturnRequiresActualEvidence()||OriginalReturnRejectsUnexpectedSupplyOrReceipt()||ClosedLoopRealPolicies()||PackEvidenceCannotBeInvented()||NativeReceiptCannotBeInferredFromCounts()||AttachedBaselineRequiredAfterReceipt()||FullOrEmptyPreflightNeverBegins()||DisabledAndBounded())return 1;
 std::cout<<"BC2 magazine physical probe: 19 actual-policy/TrackedRig/evidence groups passed; native/GPU/headset unverified\n";}
