#include "Bc2MagazinePhysicalProbe.h"
#include "Bc2ReloadInterruptionProbe.h"
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
 enum class InputFault {None,LeftTracking,RightTracking,HeadTracking,Focus,Expired};
 std::int64_t now=1000*Ms,started=0,submittedAt=0,retiredAt=0;std::uint64_t sequence=0,cycle=0,intent=0;
 bool freezeCarry=false,anatomical=true,applied=false,gateTaken=false,ackTaken=false,publishPairs=true,allowReceipt=true,allowAttached=true,allowRetirement=true,allowIdle=true,changeOnCancel=false;
 bool observedHalfPull=false,observedHalfCarry=false,shortCarryLease=false,shortIssued=false,chest=false,nativeCancelled=false;
 std::int64_t gapAt=0;
 unsigned starts=0,submits=0,cancels=0;int loaded=27,reserve=83;
 ReloadHoldIdentity native{};MagazinePhysicalApi api{};std::optional<ManualReloadRequest> unseat;
 std::optional<ReloadMagazineNativeRequest> request;
 std::optional<Bc2MagazinePhysicalReload> consumer;Bc2MagazinePhysicalProbe probe{true};MagazinePackCounters packs{};
 HandInteraction hands;std::optional<HandClaim> gun;TrackedRig rig;ControllerActions actions;MagazineRawContact raw{};
 Bc2ReloadInterruptionProbe* interruption=nullptr;
 std::array<MagazineRawContact,4> rawHistory{};unsigned rawDelay=1;
 std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
 explicit Loop(bool original=false,bool carry=false,bool sequence=false,bool body=false):chest(body),probe(true,original,carry,sequence,body){native.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};native.firing={0x50000,0x60000,0x70000};
 native.serverPlayer=0x80000;native.serverSoldier=0x90000;native.serverItem=0xa0000;
 meshes->owner=native.owner;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
 auto& m=meshes->states[0].meshes[0];m.kind=SelectedMeshKind::Xm8;m.address=0x120000;std::memcpy(m.assetPath.data(),Xm8MagazineMesh.data(),Xm8MagazineMesh.size());
 api.context=this;api.clock=[](void* p)noexcept{return static_cast<Loop*>(p)->now;};
 api.reserve=[](void* p)noexcept->std::optional<Bc2AmmoReserveLease>{auto& f=*static_cast<Loop*>(p);
  if(f.submittedAt&&!f.applied&&!f.nativeCancelled&&f.now-f.submittedAt>=300*Ms){f.loaded+=int(f.request->reservedUnits);f.reserve-=int(f.request->reservedUnits);f.applied=true;}
  return Bc2AmmoReserveLease{f.native,100000+f.sequence,f.now,f.now+100*Ms,f.loaded,f.reserve,30,true,
   !f.started||(f.allowIdle&&((f.retiredAt&&f.now-f.retiredAt>=100*Ms)||(f.applied&&f.now-f.submittedAt>600*Ms)))};};
 api.identity=[](void* p)noexcept->std::optional<ReloadHoldIdentity>{return static_cast<Loop*>(p)->native;};
 api.start=[](void* p,const ReloadCycleControl& c,const ManualReloadRequest& r,const ReloadMagazineStartupPulse& pulse)noexcept{auto& f=*static_cast<Loop*>(p);++f.starts;f.started=f.now;f.cycle=c.cycle;f.unseat=r;
  if(pulse.control!=c||pulse.endNs!=c.observedNs+100000000||pulse.endNs>c.deadlineNs)return MagazineCycleStartResult::NotStarted;
  f.retiredAt=0;f.gateTaken=false;f.ackTaken=false;f.applied=false;f.nativeCancelled=false;f.submittedAt=0;f.request.reset();return MagazineCycleStartResult::Started;};
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
 api.cancel=[](void* p)noexcept{auto& f=*static_cast<Loop*>(p);++f.cancels;f.retiredAt=f.now;f.nativeCancelled=true;if(f.changeOnCancel){++f.loaded;--f.reserve;}};
 api.retire=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadCycleRetirement>{auto& f=*static_cast<Loop*>(p);
  if(!f.retiredAt||!f.allowRetirement)return {};return ReloadCycleRetirement{id,cycle,1000+f.sequence,f.now,f.now+200*Ms,true};};
 auto pouch=chest?ChestAmmoSupply():Bc2MagazinePhysicalReload::DefaultPouch();
 const auto ids=Bc2MagazinePhysicalReload::DefaultPouch();pouch.itemNamespace=ids.itemNamespace;pouch.pouch=ids.pouch;
 consumer.emplace(true,api,pouch);
 }
 // Pausing the motion driver never resets the real consumer or hand arbiter.
 // Lost tracking is supplied as input, not a direct consumer Cancel call.
 void Tick(bool drive=true,InputFault fault=InputFault::None){const auto priorRaw=raw.rawLeftWristWorldMeters;bool frozen=false;now+=10*Ms;++sequence;auto in=Input(sequence);
  if(interruption&&interruption->LossStarted())drive=false;
  if(drive)probe.Prepare(in,native.owner,Xm8MagazineAsset,raw,consumer->ProbeState(now),now,now+100*Ms,now);
  if(fault==InputFault::LeftTracking)in.hands[0].gripTracked=in.hands[0].aimTracked=false;
  if(fault==InputFault::RightTracking)in.hands[1].gripTracked=in.hands[1].aimTracked=false;
  if(fault==InputFault::HeadTracking)in.headValid=false;
  if(fault==InputFault::Focus){in.focused=false;in.hands={};}
  if(fault==InputFault::Expired)in.predictedNs=now-110*Ms;
  auto actionInput=in;if(interruption)interruption->ActionInput(actionInput);
  const auto action=actions.Update(actionInput,{native.owner.soldier,native.owner.equipGeneration,true,true},now);
  if(interruption)interruption->Prepare(in,now);
  frozen=freezeCarry&&Report(probe).find("\"phase\":14")!=std::string::npos;
  if(in.hands[0].squeeze>.35f&&in.hands[0].squeeze<.75f){
   observedHalfCarry|=Report(probe).find("\"phase\":14")!=std::string::npos;
   observedHalfPull|=Report(probe).find("\"phase\":3")!=std::string::npos;
  }
  HandInteractionSample h{{(std::uint64_t(native.owner.weak)<<32)|native.owner.soldier,5,17,7},sequence,in.predictedNs,in.predictedNs+100*Ms,now,
   in.focused&&in.headValid,{in.hands[0].gripTracked,in.hands[1].gripTracked&&in.hands[1].aimTracked},{in.hands[0].squeeze<=.35f,false}};
  hands.Update(h);HandContactProof proof{{1002,1},sequence,h.deadlineNs,true};
  if(gun)gun=hands.Renew(h,gun->token,proof).claim;
  if(!gun)gun=hands.Acquire(h,{h.owner,InteractionHand::Right,HandClaimKind::GunHold,{0xb0000,17},proof,++intent,0}).claim;
  meshes->sequence=sequence;meshes->observedNs=now;meshes->deadlineNs=now+200*Ms;
  MagazinePhysicalSample s;s.nativeOwner=native.owner;s.input=h;s.weapon={0xb0000,17};s.trackingEpoch=7;s.geometrySequence=sequence;
  s.family={{native.owner,s.weapon,0xd0000,0xc0000,2},now,now+100*Ms,true};
  const auto pouch=chest?BodyAnchorHandPose(in,InteractionHand::Left):PhysicalReloadPouchPose(in);
  if(pouch)s.bodyFromHand=*pouch;
  s.gripPressed=ReloadGripActive(in.hands[0].squeeze,h,hands.Current(InteractionHand::Left));s.cancel=!action.active||!pouch||(drive&&probe.CancelConsumer());s.asset=Xm8MagazineAsset;s.meshes=meshes;s.raw=raw;
  s.actionFlagsKnown=true;s.actionHeld=action.held;s.actionPressed=action.pressed;
  if(raw.valid)s.originalHandEvidence=raw.inputEvidence;
  const auto result=consumer->Tick(s,hands,intent);
  // Test-owned mock renderer receipt. Production reads actual verified Pack
  // counters; the fixture itself has no writer for these values.
  if(publishPairs&&result.tracking.target&&MagazineTargetFresh(result.tracking,now)){
   ++packs.pairs;packs.copies+=2;const auto role=unsigned(result.tracking.target->role);++packs.rolePairs[role];packs.roleCopies[role]+=2;}
  if(drive)probe.Observe(consumer->ProbeState(now),packs,now);
  if(interruption)interruption->Observe(consumer->ProbeState(now),packs,raw,now);
  const auto body=Pose(5,2,3),left=Multiply(Pose(-.2f,-.4f,.3f),body),right=Multiply(Pose(0,-.25f,.45f),body);
  std::array<ArmAnchor,2> arms{{{{4.8f,1.8f,3},{0,1,0}},{{5.2f,1.8f,3},{0,1,0}}}};
  const auto posed=rig.Update({1,2,3,4},in,body,left,right,right,arms);
  if(posed){raw.valid=true;raw.owner=native.owner;raw.rigFingerprint=Xm8MagazineRig;raw.inputEvidence=h;
   raw.rawLeftWristWorldMeters=posed->left;raw.weaponWorldMeters=posed->weapon;raw.trackingBodyWorldMeters=body;
   raw.nativeMagazineAttached=!started||(allowAttached&&((applied&&now-submittedAt>600*Ms)||(retiredAt&&now-retiredAt>=100*Ms)));
   if(anatomical){auto attachment=Pose();attachment.values[1][1]=attachment.values[2][2]=-1;
    auto wrist=Multiply(attachment,Multiply(Matrix(in.hands[0].grip),body));wrist.values[3]=posed->left.values[3];raw.rawLeftWristWorldMeters=wrist;}
   if(frozen)raw.rawLeftWristWorldMeters=priorRaw;
   rawHistory[sequence%rawHistory.size()]=raw;
   raw=rawHistory[(sequence+rawHistory.size()-(rawDelay-1))%rawHistory.size()];
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
int LaggedGeometryThroughRealConsumer(){
 for(unsigned delay:{2u,3u}){Loop f(false,true,true,true);f.rawDelay=delay;
  const bool pass=f.Run();if(!pass){std::cerr<<"Renderer input lag "<<delay<<": "<<Report(f.probe)<<'\n';f.consumer->Report(std::cerr);}
  CHECK(pass&&f.starts==2&&f.submits==1&&f.loaded==30&&f.reserve==80);
  CHECK(f.consumer->ProbeState(f.now).originalReturns==1&&f.consumer->ProbeState(f.now).completed==1);
 }return 0;
}
int RepeatDriverWithoutResettingConsumer(){
 Loop f(true,false,false,true);const auto* original=&*f.consumer;
 for(unsigned episode=0;episode<3;++episode){
  if(episode)f.probe=Bc2MagazinePhysicalProbe(true,true,false,false,true);
  const bool pass=f.Run();if(!pass){std::cerr<<Report(f.probe)<<'\n';f.consumer->Report(std::cerr);}
  CHECK(pass && &*f.consumer==original);
  CHECK(f.starts==episode+1&&f.consumer->ProbeState(f.now).originalReturns==episode+1);
  CHECK(f.submits==0&&f.loaded==27&&f.reserve==83);
 }
 f.probe=Bc2MagazinePhysicalProbe(true,false,true,false,true);CHECK(f.Run());
 CHECK(f.starts==4&&f.submits==1&&f.consumer->ProbeState(f.now).originalReturns==3);
 CHECK(f.loaded==30&&f.reserve==80);return 0;
}
int TrackingInterruptionThenRealRecovery(){
 for(const auto phase:{DetachableMagazinePhase::Pulling,DetachableMagazinePhase::RemovedHeld,
     DetachableMagazinePhase::WellEmpty,DetachableMagazinePhase::ReplacementHeld,DetachableMagazinePhase::Guided,DetachableMagazinePhase::AwaitingSeat})
 for(const auto fault:{Loop::InputFault::LeftTracking,Loop::InputFault::RightTracking,Loop::InputFault::HeadTracking,Loop::InputFault::Focus,Loop::InputFault::Expired}){
  Loop f(false,true,false,true);const auto* original=&*f.consumer;
  for(unsigned n=0;n<2500&&!f.probe.CancelConsumer()&&f.consumer->ProbeState(f.now).phase!=phase;++n)f.Tick();
  CHECK(f.consumer->ProbeState(f.now).phase==phase&&f.starts==1);
  const unsigned oldSubmits=phase==DetachableMagazinePhase::AwaitingSeat?1:0;CHECK(f.submits==oldSubmits);
  const auto interrupted=f.consumer->ProbeState(f.now);const auto oldCycle=interrupted.cycle;
  f.Tick(false,fault);CHECK(f.cancels==1&&!f.hands.Current(InteractionHand::Left));
  CHECK(f.consumer->ProbeState(f.now).retiring);
  for(unsigned n=0;n<60&&f.consumer->ProbeState(f.now).retiring;++n)f.Tick(false);
  CHECK(!f.consumer->ProbeState(f.now).retiring&&!f.consumer->BlocksEquipment());
  CHECK(f.loaded==27&&f.reserve==83&&f.consumer->ProbeState(f.now).completed==0);
  // Only replace the controller script. Native counters, counts, ownership,
  // supplies, claims and their monotonically increasing IDs all persist.
  f.probe=Bc2MagazinePhysicalProbe(true,false,true,true,true);
  const bool pass=f.Run();if(!pass){std::cerr<<"Interrupted phase "<<unsigned(phase)<<", input fault "<<unsigned(fault)<<": "<<Report(f.probe)<<'\n';f.consumer->Report(std::cerr);}
  CHECK(pass && &*f.consumer==original);
  const auto done=f.consumer->ProbeState(f.now);
  CHECK(done.originalReturns==1&&done.completed==1&&done.acquired==interrupted.acquired+1);
  CHECK(done.cycle>oldCycle&&f.starts==3&&f.submits==oldSubmits+1&&f.loaded==30&&f.reserve==80);
 }
 return 0;
}
int RepeatedReplacementsKeepLifetimeEvidence(){
 Loop f(false,true,false,true);const auto* original=&*f.consumer;
 unsigned simulatedShots=0;std::uint64_t previousCycle=0,previousRequest=0;
 for(unsigned episode=0;episode<4;++episode){
  if(episode){
   // Test-owned native observation: three rounds fired between episodes.
   // No production consumer state, ledger, IDs or receipts are reset here.
   f.loaded-=3;simulatedShots+=3;f.probe=Bc2MagazinePhysicalProbe(true,false,true,false,true);
  }
  const bool pass=f.Run();if(!pass){std::cerr<<Report(f.probe)<<'\n';f.consumer->Report(std::cerr);}
  CHECK(pass && &*f.consumer==original);
  const auto s=f.consumer->ProbeState(f.now);
  CHECK(s.started==episode+1&&s.acquired==episode+1&&s.submitted==episode+1&&s.completed==episode+1);
  CHECK(f.request&&f.request->request.id>previousRequest&&s.cycle>previousCycle);
  CHECK(f.loaded==30&&f.loaded+f.reserve+int(simulatedShots)==110);
  previousCycle=s.cycle;previousRequest=f.request->request.id;
 }
 return 0;
}
int NewEpisodeRejectsBusyConsumer(){
 for(const auto phase:{DetachableMagazinePhase::PreparingRemoval,DetachableMagazinePhase::RemovedHeld,
     DetachableMagazinePhase::ReplacementHeld,DetachableMagazinePhase::AwaitingSeat}){
  Loop f(false,true,false,true);
  for(unsigned n=0;n<2500&&!f.probe.CancelConsumer()&&f.consumer->ProbeState(f.now).phase!=phase;++n)f.Tick();
  const auto state=f.consumer->ProbeState(f.now);CHECK(state.phase==phase);
  Bc2MagazinePhysicalProbe next(true,false,true,false,true);auto in=Input(++f.sequence);f.now=in.predictedNs;
  next.Prepare(in,f.native.owner,Xm8MagazineAsset,f.raw,state,f.now,f.now+100*Ms,f.now);
  CHECK(Metric(Report(next),"failure")==21&&in.hands[0].squeeze==0);
  CHECK(f.starts==1&&!f.cancels); // The driver did not call the native API.
 }
 return 0;
}
int LifetimeCountersCannotRollBack(){
 Loop f(false,true,true,true);CHECK(f.Run());
 for(unsigned field=0;field<5;++field){
  auto state=f.consumer->ProbeState(f.now);Bc2MagazinePhysicalProbe next(true);
  auto in=Input(++f.sequence);f.now=in.predictedNs;
  next.Prepare(in,f.native.owner,Xm8MagazineAsset,f.raw,state,f.now,f.now+100*Ms,f.now);
  CHECK(!next.CancelConsumer());
  if(field==0)--state.acquired;if(field==1)--state.started;if(field==2)--state.submitted;
  if(field==3)--state.completed;if(field==4)--state.originalReturns;
  next.Observe(state,f.packs,f.now);CHECK(Metric(Report(next),"failure")==4);
 }
 return 0;
}
int PriorEvidenceCannotCompleteNewEpisode(){
 Loop f(true,false,false,true);CHECK(f.Run());const auto old=f.consumer->ProbeState(f.now);
 f.probe=Bc2MagazinePhysicalProbe(true,true,false,false,true);f.Tick();
 CHECK(!f.probe.CancelConsumer()&&Report(f.probe).find("\"native_receipt\":false")!=std::string::npos);
 f.probe.Observe(old,f.packs,f.now);
 CHECK(!f.probe.CancelConsumer()&&Report(f.probe).find("\"native_receipt\":false")!=std::string::npos);
 f.publishPairs=false;CHECK(!f.Run());
 CHECK(f.consumer->ProbeState(f.now).originalReturns==1&&f.starts==2&&f.submits==0);
 // Old receipt plus a forged completion count is still not fresh evidence.
 Bc2MagazinePhysicalProbe next(true,true);auto in=Input(++f.sequence);f.now=in.predictedNs;
 next.Prepare(in,f.native.owner,Xm8MagazineAsset,f.raw,old,f.now,f.now+100*Ms,f.now);
 auto replay=old;++replay.started;++replay.originalReturns;
 next.Observe(replay,f.packs,f.now);CHECK(next.CancelConsumer()&&!next.Completed());
 return 0;
}
int BoundedInterruptionDriverThroughRealConsumer(){
 for(unsigned failure=0;failure<4;++failure){Loop f(true,false,false,true);Bc2ReloadInterruptionProbe probe;
  CHECK(probe.Begin(f.consumer->ProbeState(f.now),f.packs,f.now));f.interruption=&probe;
  if(failure==1)f.allowRetirement=false;
  if(failure==2)f.changeOnCancel=true;
  if(failure==3)f.allowAttached=false;
  for(unsigned n=0;n<2500&&!probe.Recovered()&&!probe.Failed();++n)f.Tick();
  if(!failure){
   if(!probe.Recovered()){probe.Report(std::cerr);f.consumer->Report(std::cerr);}
   CHECK(probe.Recovered()&&f.cancels==1&&f.starts==1&&f.submits==0);
   CHECK(f.consumer->ProbeState(f.now).cancelled==1&&f.consumer->ProbeState(f.now).reconciled==1);
   CHECK(f.loaded==27&&f.reserve==83);
   f.interruption=nullptr;f.probe=Bc2MagazinePhysicalProbe(true,false,true,true,true);
   CHECK(f.Run()&&f.starts==3&&f.submits==1&&f.loaded==30&&f.reserve==80);
  }else CHECK(probe.Failed()&&!probe.Recovered()&&f.starts==1&&f.submits==0);
 }
 return 0;
}
int RecoveryNeedsNewNativeRetirementAndRestoredInput(){
 Loop f(true,false,false,true);Bc2ReloadInterruptionProbe probe;
 CHECK(probe.Begin(f.consumer->ProbeState(f.now),f.packs,f.now));f.interruption=&probe;
 for(unsigned n=0;n<2500&&!probe.Recovered()&&!probe.Failed();++n)f.Tick();CHECK(probe.Recovered());
 // Replaying the observed happy-path counter shape is not enough: each guard
 // below is independently required before the same completed sample is usable.
 for(unsigned fault=0;fault<6;++fault){Loop g(true,false,false,true);Bc2ReloadInterruptionProbe q;
  CHECK(q.Begin(g.consumer->ProbeState(g.now),g.packs,g.now));g.interruption=&q;
  for(unsigned n=0;n<2500&&q.State()!=Bc2ReloadInterruptionProbe::Phase::Retiring&&!q.Failed();++n)g.Tick();
  CHECK(q.State()==Bc2ReloadInterruptionProbe::Phase::Retiring);
  // Temporarily stop observing in the diagnostic; production cleanup proceeds.
  g.interruption=nullptr;for(unsigned n=0;n<30;++n)g.Tick(false);
  auto s=g.consumer->ProbeState(g.now);auto raw=g.raw;CHECK(!s.retiring&&s.reconciled==1);
  if(fault==0)--s.reconciled;if(fault==1)--s.cancelled;if(fault==2)raw.nativeMagazineAttached=false;
  if(fault==3)raw.inputEvidence.tracked[0]=false;if(fault==4)raw.inputEvidence.deadlineNs=g.now;
  if(fault==5)++raw.owner.weapon;
  q.Observe(s,g.packs,raw,g.now);CHECK(!q.Recovered());
  q.Observe(g.consumer->ProbeState(g.now),g.packs,g.raw,g.now);CHECK(q.Recovered());
 }
 return 0;
}
int DisabledAndBounded(){Bc2MagazinePhysicalProbe off;auto in=Input(1);const auto original=in;off.Prepare(in,{},"",{},{},1000*Ms,1100*Ms,1000*Ms);
 CHECK(in.hands[0].grip.position.x==original.hands[0].grip.position.x&&!off.CancelConsumer());
 Bc2MagazinePhysicalProbe probe(true);ReloadStateOwner owner{0x10000,0x20000,0x30000,0x40000,5,3,7};
 probe.Prepare(in,owner,Xm8MagazineAsset,{},{},1000*Ms,1100*Ms,1000*Ms);in.generation++;
 probe.Prepare(in,owner,Xm8MagazineAsset,{},{},31000*Ms,31100*Ms,31000*Ms);
 CHECK(probe.CancelConsumer()&&in.hands[0].squeeze==0);return 0;}
}
int main(){if(LaggedGeometryThroughRealConsumer()||RecoveryNeedsNewNativeRetirementAndRestoredInput()||BoundedInterruptionDriverThroughRealConsumer()||RepeatedReplacementsKeepLifetimeEvidence()||NewEpisodeRejectsBusyConsumer()||LifetimeCountersCannotRollBack()||PriorEvidenceCannotCompleteNewEpisode()||RepeatDriverWithoutResettingConsumer()||TrackingInterruptionThenRealRecovery()||ChestSupplySequence()||CarryWaitRejectsLostControl()||CarryPreviousLeaseMayExpire()||CarryMissingHoldIsBounded()||CarryWaitDoesNotInventMotion()||RepeatedOriginalThenReplacement()||RepeatRequiresFirstReturnRetirement()||RepeatStillRequiresObservedCarry()||FreshFreeCarryThroughRealConsumer()||CarryCoverageRequiresObservedMotion()||OriginalReturnThroughRealConsumer()||OriginalReturnRequiresActualEvidence()||OriginalReturnRejectsUnexpectedSupplyOrReceipt()||ClosedLoopRealPolicies()||PackEvidenceCannotBeInvented()||NativeReceiptCannotBeInferredFromCounts()||AttachedBaselineRequiredAfterReceipt()||FullOrEmptyPreflightNeverBegins()||DisabledAndBounded())return 1;
 std::cout<<"BC2 magazine physical probe: persistent actual-policy/TrackedRig/evidence groups passed; native/GPU/headset unverified\n";}
