#include "Bc2MagazinePhysicalReload.h"
#include "Bc2WeaponFrameAccess.h"
#include "Bc2MagazineDetached.h"
#ifndef BASELINE_DIAGNOSTICS
#include "Bc2MagazineFallbackObservation.h"
#endif
#include "Bc2Xm8MagazineCalibration.h"
#include "Test.h"
#include "fvr/interaction/SupportGrip.h"
#include <cstring>
#include <iostream>
#include <sstream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;using namespace fvr::interaction::reload_insertion_detail;
namespace {
constexpr std::int64_t Ms=1000000;
auto Pose(float x=0,float y=0,float z=0){auto m=Identity();m.values[3]={x,y,z,1};return m;}
bool Same(const math::Matrix4& a,const math::Matrix4& b,float e=.00001f){for(unsigned n=0;n<4;++n)for(unsigned k=0;k<4;++k)
 if(std::abs(a.values[n][k]-b.values[n][k])>e)return false;return true;}
struct Fixture {
 MagazineCycleStartResult startResult=MagazineCycleStartResult::Started,inspectResult=MagazineCycleStartResult::Unknown;unsigned inspections=0,familyFault=0;
 std::int64_t now=1000*Ms,nativeLifetime=100*Ms;std::uint64_t seq=100,intent=0,cycle=0,event=0;
 bool observationDeferred=false,observationRejected=false,missingLease=false;
 bool keepDeferred=false,reserveDeferred=false,reserveRejected=false,reserveCohortGap=false,leaseHeld=true;
 unsigned starts=0,submits=0,cancels=0;bool held=false,gateSent=false,allowRetire=false,keep=true,source=true;
 Bc2AmmoReserveLease reserve{};MagazinePhysicalSample s{};MagazinePhysicalApi api{};HandInteraction hands;
 std::optional<HandClaim> gun;std::optional<ManualReloadRequest> unseat;
 std::optional<ReloadMagazineNativeRequest> submitted;std::optional<ReloadMagazineAckEvidence> ack;
 std::optional<ReloadHoldIdentity> retiredIdentity;std::uint64_t retiredCycle=0;
 std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
 std::optional<Bc2MagazinePhysicalReload> policy;MagazinePhysicalResult result;
 Fixture(bool enabled=true){
  reserve.identity.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};reserve.identity.firing={0x50000,0x60000,0x70000};
  reserve.identity.serverPlayer=0x80000;reserve.identity.serverSoldier=0x90000;reserve.identity.serverItem=0xa0000;
  reserve.loaded=27;reserve.reserve=83;reserve.capacity=30;reserve.verified=reserve.reloadInputReady=true;
  s.nativeOwner=reserve.identity.owner;s.weapon={0xb0000,17};s.trackingEpoch=7;
  s.input={{(std::uint64_t(0x30000)<<32)|0x20000,5,17,7},1,now,now+100*Ms,now,true,{true,true},{true,false}};
  s.asset=Xm8MagazineAsset;s.meshes=meshes;s.bodyFromHand=Pose(2);s.geometrySequence=1;
  meshes->owner=s.nativeOwner;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
  auto& mesh=meshes->states[0].meshes[0];mesh.kind=SelectedMeshKind::Xm8;mesh.address=0x120000;
  std::memcpy(mesh.assetPath.data(),Xm8MagazineMesh.data(),Xm8MagazineMesh.size());
  s.raw.valid=true;s.raw.owner=s.nativeOwner;s.raw.rigFingerprint=Xm8MagazineRig;s.raw.weaponWorldMeters=Pose();
  s.raw.nativeMagazineAttached=true;
  api.context=this;api.clock=[](void* p)noexcept{return static_cast<Fixture*>(p)->now;};
  api.reserve=[](void* p)noexcept->std::optional<Bc2AmmoReserveLease>{auto& f=*static_cast<Fixture*>(p);return f.source?std::optional{f.reserve}:std::nullopt;};
  api.reserveObserved=[](void* p)noexcept{auto& f=*static_cast<Fixture*>(p);
   if(f.reserveDeferred)return ReloadReserveObservation{ReloadObservationResult::Deferred,{}};
   if(f.reserveRejected)return ReloadReserveObservation{ReloadObservationResult::Rejected,{}};
   if(f.reserveCohortGap)return ReloadReserveObservation{ReloadObservationResult::CohortGap,{}};
   return ReloadReserveObservation{ReloadObservationResult::Available,f.api.reserve(p)};};
  api.identity=[](void* p)noexcept->std::optional<ReloadHoldIdentity>{return static_cast<Fixture*>(p)->reserve.identity;};
  api.start=[](void* p,const ReloadCycleControl& c,const ManualReloadRequest& r,const ReloadMagazineStartupPulse& pulse)noexcept{
   auto& f=*static_cast<Fixture*>(p);++f.starts;f.cycle=c.cycle;f.unseat=r;
   if(pulse.control!=c||pulse.endNs!=c.observedNs+100000000||pulse.endNs>c.deadlineNs)return MagazineCycleStartResult::NotStarted;
   return r.owner.actor==f.s.nativeOwner.soldier&&r.owner.equipGeneration==f.s.nativeOwner.equipGeneration&&c.deadlineNs==f.s.input.deadlineNs?f.startResult:MagazineCycleStartResult::NotStarted;};
  api.inspectStart=[](void* p,const ReloadHoldIdentity&,std::uint64_t,const std::optional<ReloadMagazineStartupPulse>&)noexcept{auto& f=*static_cast<Fixture*>(p);++f.inspections;return f.inspectResult;};
  api.keep=[](void* p,const ReloadCycleControl&)noexcept{return static_cast<Fixture*>(p)->keep;};
  api.keepObserved=[](void* p,const ReloadCycleControl&)noexcept{const auto& f=*static_cast<Fixture*>(p);return f.keepDeferred?ReloadKeepAliveResult::Deferred:f.keep?ReloadKeepAliveResult::Accepted:ReloadKeepAliveResult::Rejected;};
  api.lease=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadMagazineLease>{
   auto& f=*static_cast<Fixture*>(p);if(f.missingLease||(!f.held&&!f.submitted))return {};
   return ReloadMagazineLease{id,cycle,f.seq,f.now,f.now+f.nativeLifetime,f.reserve.loaded,f.reserve.reserve,f.reserve.capacity,true,f.held&&f.leaseHeld};};
  api.gate=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadMagazineGateAcknowledgement>{
   auto& f=*static_cast<Fixture*>(p);if(!f.held||f.gateSent||!f.unseat)return {};f.gateSent=true;
   const auto& r=*f.unseat;return ReloadMagazineGateAcknowledgement{{r.id,r.owner,r.operation,ReloadAcknowledgement::Applied},
    {id,cycle,f.seq,f.now,f.now+f.nativeLifetime,f.reserve.loaded,f.reserve.reserve,f.reserve.capacity,true,true}};};
  api.submit=[](void* p,const ReloadMagazineNativeRequest& r)noexcept{auto& f=*static_cast<Fixture*>(p);++f.submits;f.submitted=r;f.held=false;return true;};
  api.ack=[](void* p,const ReloadHoldIdentity&,std::uint64_t)noexcept->std::optional<ReloadMagazineAckEvidence>{return static_cast<Fixture*>(p)->ack;};
  api.cancel=[](void* p)noexcept{++static_cast<Fixture*>(p)->cancels;};
  api.retire=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadCycleRetirement>{auto& f=*static_cast<Fixture*>(p);
   if(!f.allowRetire)return {};f.retiredIdentity=id;f.retiredCycle=cycle;return ReloadCycleRetirement{id,cycle,++f.event,f.now,f.now+200*Ms,true};};
#ifndef BASELINE_OBSERVATION
  api.leaseObserved=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{
   auto& f=*static_cast<Fixture*>(p);
   if(f.observationRejected)return ReloadMagazineLeaseObservation{};
   if(f.observationDeferred)return ReloadMagazineLeaseObservation{ReloadMagazineObservationResult::Deferred,{}};
   return ReloadMagazineLeaseObservation{ReloadMagazineObservationResult::Ready,f.api.lease(p,id,cycle)};
  };
#endif
  policy.emplace(enabled,api,AmmoSupplyConfig{InteractionHand::Left,1000,{1001,1},{0,0,0},.15f,200*Ms});
 }
 void Geometry(float z){const auto p=Xm8MagazineConfig().insertion;
  s.raw.rawLeftWristWorldMeters=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,z),p.weaponFromEntry)));}
 void Sync(){s.input.nowNs=now;s.family={{s.nativeOwner,s.weapon,0xd0000,0xc0000,2},now,now+100*Ms,true};if(familyFault==1)s.family.verified=false;
  if(familyFault==2)s.family.deadlineNs=now;
  if(familyFault==3)++s.family.binding.weapon.id;
  if(familyFault==4)++s.family.binding.owner.weapon;
  if(familyFault==5)++s.family.binding.owner.equipGeneration;
  if(familyFault==6)s.family.binding.inventory=0;
  if(familyFault==7)s.family.binding.launcher=s.nativeOwner.weapon;
  if(familyFault==8)s.family.binding.launcherSlot=9;
  if(familyFault==9)++s.family.binding.weapon.generation;
  if(familyFault==10)++s.family.binding.owner.soldier;
  if(familyFault==11)++s.family.binding.inventory;
  reserve.sequence=100000+(++seq);reserve.observedNs=now;reserve.deadlineNs=now+100*Ms;
  meshes->sequence=seq;meshes->observedNs=now;meshes->deadlineNs=now+200*Ms;
  hands.Update(s.input);HandContactProof proof{{1002,1},s.input.sequence,s.input.deadlineNs,true};
  if(gun)gun=hands.Renew(s.input,gun->token,proof).claim;
  if(!gun)gun=hands.Acquire(s.input,{s.input.owner,InteractionHand::Right,HandClaimKind::GunHold,s.weapon,proof,++intent,0}).claim;
  result=policy->Tick(s,hands,intent);
 }
 std::int64_t inputLifetime=100*Ms;
 void Send(bool grip=false,bool eject=false,float z=.1f,bool previous=false){const auto old=s.input;
  now+=20*Ms;++s.input.sequence;s.input.observedNs=s.input.nowNs=now;s.input.deadlineNs=now+inputLifetime;
  s.input.released[0]=!grip;s.gripPressed=grip;s.ejectPressed=eject;s.geometrySequence=s.input.sequence;
  s.raw.inputEvidence=previous?old:s.input;s.originalHandEvidence=s.raw.inputEvidence;Geometry(z);Sync();}
 bool Eject(){Send();Send(false,true);if(starts!=1)return false;held=true;Send(false,true);return result.interaction.phase==DetachableMagazinePhase::WellEmpty;}
 bool Insert(bool previous=false){if(!Eject())return false;Send();s.bodyFromHand=Pose();Send(true,false,-.1f,previous);
  for(float z:{-.075f,-.04f,0.f,.035f,.07f,.1f,.1f,.1f,.1f,.1f,.1f}){Send(true,false,z,previous);if(submits)return true;}return false;}
 void Complete(){const auto& r=*submitted;const auto units=int(r.reservedUnits);const int beforeLoaded=reserve.loaded,beforeReserve=reserve.reserve;
  reserve.loaded+=units;reserve.reserve-=units;reserve.reloadInputReady=false;
  ack=ReloadMagazineAckEvidence{{{r.request.id,r.request.owner,r.request.operation,ReloadAcknowledgement::Applied},reserve.identity,cycle,
   seq+1,700,beforeLoaded,beforeReserve,reserve.loaded,reserve.reserve},now,now+100*Ms,true};}
 std::string Report(){std::ostringstream o;policy->Report(o);return o.str();}
};
int MeasuredProfileMatchesAttached(){const auto c=Xm8MagazineConfig();CHECK(ValidateReloadInsertionProfile(c.insertion));CHECK(DetachableMagazine(c).ValidConfig());
 const auto& p=c.insertion;const auto terminal=Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,p.travelMeters),p.weaponFromEntry));
 CHECK(Same(terminal,xm8_magazine_calibration::AttachedItem));CHECK(p.family==ReloadInsertionFamily::Magazine&&p.approach==ReloadInsertionApproach::RailContact);
 const auto entry=Multiply(*InverseRigid(p.itemFromInsertion),p.weaponFromEntry);
 CHECK(Near(terminal.values[3][1]-entry.values[3][1],.1f));CHECK(Near(terminal.values[3][2],entry.values[3][2]));return 0;}
int ConsumerEjectHoldInsertReceipt(){Fixture f;CHECK(f.Insert());CHECK(f.submits==1&&f.submitted->reservedUnits==3&&f.reserve.loaded==27&&f.reserve.reserve==83);
 CHECK(f.result.blocksWeaponActions&&f.result.interaction.phase==DetachableMagazinePhase::AwaitingSeat);
 CHECK(f.submitted->request.owner.actor==f.s.nativeOwner.soldier&&f.submitted->reservation.claim.owner==f.s.input.owner);
 f.Send(true);CHECK(f.result.completed==0&&f.submits==1);f.Complete();f.Send(true);
 if(f.result.completed!=1)std::cerr<<f.Report()<<'\n';CHECK(f.result.completed==1&&f.result.interaction.transaction.completed);
 CHECK(f.reserve.loaded==30&&f.reserve.reserve==80&&f.policy->BlocksEquipment());
 f.allowRetire=true;f.Send(false);f.Send(false);CHECK(!f.policy->BlocksEquipment());return 0;}
RigSnapshot SeatedMagazineRig(const Fixture& f){
 RigSnapshot rig;rig.names={"root","jntWpn_1","jntWpn_6","LeftHand"};rig.parents={-1,0,1,0};rig.weaponBone=1;
 for(const char* digit:{"Thumb","Index","Middle","Ring","Pinky"})for(unsigned j=1;j<=3;++j){
  rig.parents.push_back(j==1?3:int(rig.names.size()-1));rig.names.push_back(std::string("LeftHand")+digit+std::to_string(j));}
 rig.names.push_back("unrelated");rig.parents.push_back(0);rig.identity.soldier=f.s.nativeOwner.soldier;
 rig.identity.weak=f.s.nativeOwner.weak;rig.identity.count=unsigned(rig.names.size());rig.identity.evaluatedMatrices=0x10000;
 rig.inverseBind.assign(rig.names.size(),Pose());rig.evaluatedWorld.assign(rig.names.size(),Pose());
 rig.nativeEvaluated.resize(rig.names.size());for(auto& b:rig.nativeEvaluated)b.fill(std::byte{0x91});return rig;
}
int SeatedMagazineStaysAttachedThroughNativeContinuation(){
 Fixture f;CHECK(f.Insert());const auto submitted=*f.submitted;
 const auto originalSeat=*f.result.interaction.seat;const auto begin=f.now;
 auto rig=SeatedMagazineRig(f);const auto originalPalette=rig.nativeEvaluated;
 const auto binding=magazine_presentation_detail::Derive(rig);CHECK(binding);
 const auto placed=Pose(2,3,4);std::optional<MagazineTracking> prior;
 // Actual native continuation takes about2.8s. The loading hand releases and
 // moves away; each attachment must use a new current source, not a long lease.
 for(unsigned n=0;n<140;++n){f.Send(false,false,.7f);
  CHECK(f.result.interaction.phase==DetachableMagazinePhase::AwaitingSeat&&f.result.tracking.target);
  const auto& tracking=f.result.tracking;const auto& target=*tracking.target;
  CHECK(target.role==MagazinePropRole::Attached&&!target.handTarget&&!target.handClaim.id);
  CHECK(MagazineTargetFresh(tracking,f.now)&&!f.hands.Current(InteractionHand::Left));
  CHECK(Same(target.weaponFromItemMeters,xm8_magazine_calibration::AttachedItem));
  CHECK(target.observedNs==f.s.input.observedNs&&target.deadlineNs<=f.s.input.deadlineNs);
  CHECK(f.result.completed==0&&f.submits==1&&f.cancels==0&&f.result.blocksWeaponActions&&!f.result.reloadHeld);
  CHECK(!f.result.interaction.transaction.request&&!f.result.interaction.transaction.completed&&!f.result.interaction.seat);
  CHECK(f.submitted->reservation.seat==originalSeat.id&&f.submitted->request.id==submitted.request.id);
  CHECK(f.submitted->heldLease.observedNs==submitted.heldLease.observedNs&&f.submitted->heldLease.deadlineNs==submitted.heldLease.deadlineNs);
  CHECK(f.reserve.loaded==27&&f.reserve.reserve==83&&f.Report().find("\"pending\":1")!=std::string::npos);
  const auto plan=BuildMagazinePresentation(rig,*binding,tracking,placed,1,f.now);
  CHECK(plan.binding&&!plan.hideMagazine&&!plan.wristTarget&&plan.writes.size()==1);
  CHECK(plan.writes[0].index==binding->magazine&&Same(plan.writes[0].transform,Multiply(xm8_magazine_calibration::AttachedItem,placed)));
  const auto packed=BuildRigPosePlan(rig,plan.writes);CHECK(packed&&packed->edits.size()==1&&rig.nativeEvaluated==originalPalette);
  auto posed=originalPalette;posed[packed->edits[0].index]=packed->edits[0].after;
  for(unsigned i=0;i<posed.size();++i)if(i!=binding->magazine)CHECK(posed[i]==originalPalette[i]);
  for(unsigned eye=0;eye<2;++eye){const auto choice=SelectMagazinePackedPalette(tracking,&tracking,f.now,posed,originalPalette,true);
   CHECK(!choice.fallback&&choice.bytes[binding->magazine]==posed[binding->magazine]);}
  if(prior){CHECK(target.observedNs>prior->target->observedNs);CHECK(!MagazineTargetFresh(*prior,prior->target->deadlineNs));}
  prior=tracking;
 }
 CHECK(f.now-begin==2800*Ms);f.Complete();f.Send(false);
 CHECK(f.result.completed==1&&f.result.interaction.transaction.completed&&f.reserve.loaded==30&&f.reserve.reserve==80);
 f.allowRetire=true;f.Send(false);f.Send(false);CHECK(!f.policy->BlocksEquipment()&&f.submits==1);return 0;
}
int SubmittedMagazineReleasesSupportWithoutCompletingNativeReload(){
 Fixture f;CHECK(f.Insert());const auto submitted=*f.submitted;
 CHECK(f.result.blocksWeaponActions&&!f.result.ownsLeftHand&&!f.hands.Current(InteractionHand::Left));
 CHECK(!MagazineBlocksSupport(f.result,f.now)&&f.result.tracking.target&&
  f.result.tracking.target->role==MagazinePropRole::Attached&&!f.result.tracking.target->handTarget);
 // Successful physical submission transfers the prop to the gun. Continuing
 // squeeze cannot retain a second invisible AmmoObject until native completion.
 f.Send(true,false,.7f);CHECK(!f.result.ownsLeftHand&&!f.hands.Current(InteractionHand::Left));
 CHECK(!MagazineBlocksSupport(f.result,f.now)&&f.result.blocksWeaponActions);
 SupportGrip support;InputFrame input{};input.spaceGeneration=f.s.input.owner.space;
 input.focused=input.headValid=true;input.hands[0].grip.position.z=-.4f;
 for(auto& hand:input.hands){hand.gripTracked=hand.aimTracked=true;hand.active=Components;}
 const SupportGripOwner owner{f.s.input.owner.actor,f.s.input.owner.actorGeneration,f.s.weapon.id};
 const SupportGripContact contact{true,0,{}};std::optional<HandClaim> claim;
 const auto supportTick=[&](float squeeze){input.generation=f.s.input.sequence;input.predictedNs=f.now;input.hands[0].squeeze=squeeze;
  struct PoseIdentity {unsigned soldier,weak,weapon;std::uint64_t owner,space,equipmentGeneration,generation;
   bool valid,leftTracked,weaponActionsBlocked;};
  const PoseIdentity current{f.s.nativeOwner.soldier,f.s.nativeOwner.weak,f.s.nativeOwner.weapon,
   f.s.nativeOwner.actorGeneration,f.s.nativeOwner.space,f.s.nativeOwner.equipGeneration,input.generation,true,true,f.result.blocksWeaponActions};
  if(!WeaponFrameAdmitted(current,current,current.soldier,current.weak,current.weapon,WeaponFrameUse::Support)||
     WeaponFrameAdmitted(current,current,current.soldier,current.weak,current.weapon,WeaponFrameUse::Firing)!=!f.result.blocksWeaponActions)
    return SupportGripResult{};
  const auto visible=WeaponFrameAdmitted(current,current,current.soldier,current.weak,current.weapon,WeaponFrameUse::Support)?contact:SupportGripContact{};
  return support.Update(owner,input,visible,MagazineBlocksSupport(f.result,f.now));};
 f.Send(false,false,.7f);CHECK(!supportTick(0).holding); // Real release rearms support.
 for(unsigned n=0;n<140;++n){f.Send(true,false,.7f);const auto supported=supportTick(1);
  CHECK(supported.holding&&(n||supported.engaged));
  const HandContactProof proof{{77,1},f.s.input.sequence,f.s.input.deadlineNs,true};
  if(!claim)claim=f.hands.Acquire(f.s.input,{f.s.input.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,
    f.s.weapon,proof,++f.intent,f.gun->token.id}).claim;
  else claim=f.hands.Renew(f.s.input,claim->token,proof).claim;
  CHECK(claim&&f.hands.Current(InteractionHand::Left)->token.kind==HandClaimKind::WeaponSupport);
  CHECK(f.result.blocksWeaponActions&&!f.result.ownsLeftHand&&f.result.completed==0&&f.submits==1);
  CHECK(f.reserve.loaded==27&&f.reserve.reserve==83&&f.Report().find("\"pending\":1")!=std::string::npos);
  ActionOutput actions;actions.active=true;actions.held=actions.pressed=Fire|Use|NextWeapon|Reload;
  ApplyMagazinePhysicalActions(actions,&Xm8MagazineEquipment(),f.result);CHECK(actions.held==0&&actions.pressed==0);
  CHECK(f.submitted->request.id==submitted.request.id&&f.submitted->heldLease.deadlineNs==submitted.heldLease.deadlineNs);
 }
 f.Complete();f.Send(true,false,.7f);CHECK(f.result.completed==1&&!MagazineBlocksSupport(f.result,f.now)&&supportTick(1).holding);
 // A completed transaction can still wait for native retirement. Keep its fresh
 // attached presentation/support, without making pending Fire/equip usable.
 f.Send(true,false,.7f);CHECK(f.result.blocksWeaponActions&&!MagazineBlocksSupport(f.result,f.now)&&supportTick(1).holding);
 f.allowRetire=true;f.Send(true,false,.7f);CHECK(!f.policy->BlocksEquipment()&&supportTick(1).holding);
 CHECK(f.reserve.loaded==30&&f.reserve.reserve==80&&f.starts==1&&f.submits==1);return 0;
}
int SupportRequiresFreshSeatedResultAndSafety(){
 Fixture f;CHECK(f.Insert());f.Send(false);CHECK(!MagazineBlocksSupport(f.result,f.now));
 for(unsigned fault=0;fault<9;++fault){auto r=f.result;
  if(fault==0)r.interaction.phase=DetachableMagazinePhase::Guided;
  if(fault==1)r.ownsLeftHand=true;
  if(fault==2)r.tracking.target.reset();
  if(fault==3)r.tracking.target->role=MagazinePropRole::Replacement;
  if(fault==4)r.tracking.target->handTarget=true;
  if(fault==5)r.tracking.target->handClaim.id=5;
  if(fault==6)r.tracking.target->deadlineNs=f.now;
  if(fault==7)++r.tracking.target->owner.space;
  if(fault==8)r.tracking.cycle=0;
  CHECK(MagazineBlocksSupport(r,f.now));
 }
 CHECK(MagazineBlocksSupport(f.result,f.result.tracking.target->deadlineNs));
 for(unsigned fault=0;fault<5;++fault){Fixture q;CHECK(q.Insert());q.Send(false);
  if(fault==0)q.s.cancel=true;
  if(fault==1)q.s.input.focused=false;
  if(fault==2)q.s.input.tracked[0]=false;
  if(fault==3){++q.s.nativeOwner.equipGeneration;q.reserve.identity.owner=q.s.nativeOwner;q.s.raw.owner=q.s.nativeOwner;}
  if(fault==4)q.keep=false;
  q.Send(true);CHECK(MagazineBlocksSupport(q.result,q.now)&&q.cancels==1&&q.result.completed==0);
 }
 return 0;
}
int SeatedAttachmentStopsOnActualConsumerBoundaries(){
 for(unsigned fault=0;fault<9;++fault){Fixture f;CHECK(f.Insert());f.Send(false);
  CHECK(f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Attached);
  const auto before=f.result.tracking;
  if(fault==0)f.s.input.focused=false; // Pause/focus loss.
  if(fault==1)f.s.input.tracked[0]=false;
  if(fault==2)f.s.cancel=true;
  if(fault==3){++f.s.nativeOwner.soldier;f.reserve.identity.owner=f.s.nativeOwner;f.s.raw.owner=f.s.nativeOwner;} // Replacement/respawn.
  if(fault==4){++f.s.nativeOwner.equipGeneration;f.reserve.identity.owner=f.s.nativeOwner;f.s.raw.owner=f.s.nativeOwner;}
  if(fault==5){++f.s.input.owner.space;++f.s.nativeOwner.space;f.reserve.identity.owner=f.s.nativeOwner;f.s.raw.owner=f.s.nativeOwner;}
  if(fault==6)f.s.asset="40mmgl_sp";
  if(fault==7)f.s.meshes.reset();
  if(fault==8)f.keep=false;
  f.Send(false);CHECK(!f.result.tracking.target&&f.cancels==1&&f.result.completed==0&&f.submits==1);
  CHECK(f.reserve.loaded==27&&f.reserve.reserve==83);
  const std::array<std::array<std::byte,64>,1> posed{},ordinary{};
  CHECK(SelectMagazinePackedPalette(before,&f.result.tracking,f.now,posed,ordinary,true).fallback);
 }
 return 0;
}
int SeatedAttachmentReadGapNeverInflatesDeadline(){
 Fixture f;CHECK(f.Insert());f.Send(false);const auto original=f.result.tracking;CHECK(original.target);
 f.source=false;f.Send(false);CHECK(f.cancels==0&&f.result.tracking.target&&f.result.completed==0);
 CHECK(f.result.tracking.target->observedNs==original.target->observedNs&&f.result.tracking.target->deadlineNs==original.target->deadlineNs);
 f.Send(false);CHECK(f.cancels==0);f.source=true;f.Send(false);
 CHECK(f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Attached&&f.result.tracking.target->observedNs>original.target->observedNs);
 f.source=false;for(unsigned n=0;n<6;++n)f.Send(false);
 CHECK(f.cancels==1&&!f.result.tracking.target&&f.result.completed==0&&f.submits==1);
 CHECK(f.reserve.loaded==27&&f.reserve.reserve==83);return 0;
}
int SeatedAttachmentPackingChecksBothEyesAndExactProfile(){
 Fixture f;CHECK(f.Insert());f.Send(false);const auto original=f.result.tracking;
 auto rig=SeatedMagazineRig(f);const auto binding=magazine_presentation_detail::Derive(rig);CHECK(binding);
 const auto before=rig.nativeEvaluated;const auto plan=BuildMagazinePresentation(rig,*binding,original,Pose(),1,f.now);
 const auto pack=BuildRigPosePlan(rig,plan.writes);CHECK(pack&&pack->edits.size()==1);
 auto posed=before;posed[pack->edits[0].index]=pack->edits[0].after;
 CHECK(!SelectMagazinePackedPalette(original,&original,f.now,posed,before,true).fallback);
 f.s.cancel=true;f.Send(false);const auto second=SelectMagazinePackedPalette(original,&f.result.tracking,f.now,posed,before,true);
 CHECK(second.fallback&&second.bytes[binding->magazine]==before[binding->magazine]);
 CHECK(SelectMagazinePackedPalette(original,&original,original.target->deadlineNs,posed,before,true).fallback);
 CHECK(SelectMagazinePackedPalette(original,&original,f.now,posed,before,false).fallback);
 for(unsigned fault=0;fault<5;++fault){auto wrong=original;
  if(fault==0)++wrong.target->profile.generation;if(fault==1)++wrong.target->nativeCycle;
  if(fault==2)++wrong.family.binding.weapon.id;if(fault==3)++wrong.owner.soldier;
  if(fault==4)wrong.selected.reset();
  CHECK(!BuildMagazinePresentation(rig,*binding,wrong,Pose(),1,f.now).binding);
  CHECK(SelectMagazinePackedPalette(original,&wrong,f.now,posed,before,true).fallback);
 }
 rig.nativeHiddenLeaves.push_back(binding->magazine);CHECK(!BuildMagazinePresentation(rig,*binding,original,Pose(),1,f.now).binding);
 CHECK(rig.nativeEvaluated==before);return 0;
}
int PreviousRendererContactReservesOriginal(){Fixture f;CHECK(f.Insert(true));CHECK(f.submitted&&f.submitted->reservation.seat);
 CHECK(f.submits==1&&f.result.interaction.seat->inputSequence==f.s.raw.inputEvidence.sequence);
 CHECK(f.result.interaction.seat->inputSequence<f.s.input.sequence);return 0;}
int UnseatRequiresRealNativeGate(){Fixture f;f.Send();f.Send(false,true);CHECK(f.starts==1&&f.result.reloadHeld&&f.result.blocksWeaponActions);
 for(unsigned n=0;n<8;++n)f.Send(false,true);CHECK(f.result.interaction.phase==DetachableMagazinePhase::PreparingRemoval&&f.submits==0);
 f.held=true;f.Send(false,true);CHECK(f.result.interaction.phase==DetachableMagazinePhase::WellEmpty&&f.result.tracking.target);
 CHECK(f.result.tracking.target->role==MagazinePropRole::Hidden&&f.reserve.loaded==27&&f.reserve.reserve==83);return 0;}
int PullContactAndNoAmmoCredit(){Fixture f;f.Send();f.Send(true,false,.1f);CHECK(f.result.interaction.removalGrabbed&&f.starts==1);
 f.held=true;f.Send(true,false,.065f);f.Send(true,false,.03f);f.Send(true,false,-.005f);
 CHECK(f.result.interaction.physicallyRemoved&&f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld);
 CHECK(f.result.tracking.target&&f.result.tracking.target->handTarget&&f.result.tracking.target->role==MagazinePropRole::Removed);
 CHECK(f.reserve.reserve==83&&f.submits==0&&f.result.acquired==0);f.Send(false);CHECK(f.result.interaction.phase==DetachableMagazinePhase::WellEmpty);return 0;}
int WrongOrUnverifiedReceiptNeverCredits(){for(unsigned bad=0;bad<6;++bad){Fixture f;CHECK(f.Insert());f.Complete();
 if(bad==0)f.ack->verified=false;if(bad==1)++f.ack->acknowledgement.semantic.request;
 if(bad==2)++f.ack->acknowledgement.loadedAfter;if(bad==3)++f.ack->acknowledgement.identity.serverItem;
 if(bad==4)f.ack->acknowledgement.serverInvocation=0;if(bad==5)++f.ack->acknowledgement.reserveBefore;
 f.Send(true);CHECK(f.result.completed==0&&f.Report().find("\"pending\":1")!=std::string::npos);}
 return 0;}
int CancelPendingRequiresDrainAndAttachedProof(){Fixture f;CHECK(f.Insert());f.s.cancel=true;f.Send(true);CHECK(f.policy->BlocksEquipment());
 f.s.cancel=false;f.allowRetire=true;f.s.raw.nativeMagazineAttached=false;f.Send(false);f.Send(false);
 CHECK(f.policy->BlocksEquipment());f.s.raw.nativeMagazineAttached=true;f.Send(false);
 CHECK(!f.policy->BlocksEquipment()&&f.result.completed==0&&f.reserve.loaded==27&&f.reserve.reserve==83);
 f.gateSent=false;f.submitted.reset();f.Send(false);f.Send(false,true);CHECK(f.starts==2&&f.cycle==2);return 0;}
int IdleFullNoSyntheticHeldEject(){Fixture f;f.reserve.loaded=30;f.reserve.reloadInputReady=false;
 for(unsigned n=0;n<12;++n)f.Send(false,true);CHECK(f.starts==0);
 f.reserve.loaded=29;f.reserve.reloadInputReady=true;f.Send(false,true);CHECK(f.starts==0);
 f.Send();f.Send(false,true);CHECK(f.starts==1);return 0;}
int EmptyMagazineNeverStartsPhysicalTakeover(){
 for(bool eject:{false,true}){Fixture f;f.reserve.loaded=0;f.Send();
  for(unsigned n=0;n<4;++n)f.Send(!eject,eject);
  CHECK(f.starts==0&&f.submits==0&&f.cancels==0&&!f.policy->BlocksEquipment()&&!f.result.reloadHeld);
  CHECK(f.reserve.loaded==0&&f.reserve.reserve==83&&!f.hands.Current(InteractionHand::Left));
  // A later native refill must not turn a held failed gesture into a fresh pull/eject.
  f.reserve.loaded=27;f.Send(!eject,eject);CHECK(f.starts==0);
  f.Send();f.Send(!eject,eject);CHECK(f.starts==1);
 }
 return 0;
}
int EmptyFallbackPreservesNativeActionsOnlyOnFreshIdleEvidence(){
 constexpr auto ordinary=Reload|Fire|AlternateFire|Use|NextWeapon|PreviousWeapon|Jump;
 const auto dispatched=[&](Fixture& f,std::string_view asset=Xm8MagazineAsset){ActionOutput out;
  out.held=out.pressed=ordinary;ApplyMagazinePhysicalActions(out,asset,f.result);return out;};
 for(bool ready:{false,true}){Fixture f;f.reserve.loaded=0;f.reserve.reloadInputReady=ready;f.Send();f.Send(false,true);
  CHECK(f.result.ordinaryReloadAllowed&&!f.policy->BlocksEquipment()&&f.starts==0&&f.submits==0);
  auto out=dispatched(f);CHECK(out.held==ordinary&&out.pressed==ordinary);
  // A repeated controller packet is still a current native input boundary.
  f.Sync();out=dispatched(f);CHECK(out.held==ordinary&&out.pressed==ordinary);
  f.source=false;f.Send(false,true);CHECK(!f.result.ordinaryReloadAllowed);
  out=dispatched(f);CHECK(!(out.held&Reload)&&!(out.pressed&Reload));
 }
 for(unsigned fault=0;fault<4;++fault){Fixture f;f.reserve.loaded=0;f.Send();
  if(fault==0)f.s.input.focused=false;if(fault==1)f.familyFault=3;
  if(fault==2)f.s.cancel=true;if(fault==3)f.s.asset="40mmgl_sp";f.Send(false,true);
  CHECK(!f.result.ordinaryReloadAllowed&&f.starts==0);
 }
 // Existing start pulse and suppression are composed with the actual consumer.
 Fixture partial;partial.Send();partial.Send(false,true);auto out=dispatched(partial);
 CHECK(partial.starts==1&&!partial.result.ordinaryReloadAllowed&&partial.result.reloadHeld);
 CHECK(out.held==(Reload|Jump)&&out.pressed==Jump);
 partial.held=true;partial.Send(false,true);for(unsigned n=0;n<6;++n)partial.Send();out=dispatched(partial);
 CHECK(partial.cancels==0&&partial.policy->BlocksEquipment()&&partial.result.interaction.phase==DetachableMagazinePhase::WellEmpty);
 CHECK(!partial.result.ordinaryReloadAllowed&&out.held==Jump&&out.pressed==Jump);
 Fixture retiring;CHECK(retiring.Eject());retiring.s.cancel=true;retiring.reserve.loaded=0;retiring.Send();
 CHECK(retiring.policy->BlocksEquipment()&&!retiring.result.ordinaryReloadAllowed);
 out=dispatched(retiring);CHECK(out.held==Jump&&out.pressed==Jump);
 // An unrelated family keeps its existing ordinary reload mapping.
 out=dispatched(retiring,"SPAS12_sp");CHECK((out.held&Reload)&&(out.pressed&Reload));
 return 0;
}
int IdleBoundariesRequireNewNeutral(){Fixture f;f.Send();f.now+=500*Ms;f.Send(false,true);CHECK(f.starts==0);
 f.Send();f.Send(false,true);CHECK(f.starts==1);
 Fixture g;g.Send();g.s.asset="40mmgl";g.Send(false,true);g.s.asset=Xm8MagazineAsset;g.Send(false,true);CHECK(g.starts==0);
 g.Send();g.Send(false,true);CHECK(g.starts==1);return 0;}
int UnsupportedEquipDrainsWithoutDiscardingPending(){Fixture f;CHECK(f.Insert());
 f.s.asset="SPAS12_sp";f.source=false;f.Send(false);CHECK(f.policy->BlocksEquipment());
 f.allowRetire=true;f.Send(false);CHECK(!f.policy->BlocksEquipment()&&!f.result.blocksWeaponActions);
 CHECK(f.Report().find("\"pending\":1")!=std::string::npos&&f.reserve.loaded==27&&f.reserve.reserve==83);
 f.s.asset=Xm8MagazineAsset;f.source=true;f.Send(false);f.Send(false);CHECK(!f.policy->BlocksEquipment());
 CHECK(f.Report().find("\"pending\":0")!=std::string::npos&&f.result.completed==0);return 0;}
int NativeAttachmentMayArriveAfterRefill(){Fixture f;CHECK(f.Insert());f.Complete();f.Send(true);CHECK(f.result.completed==1);
 f.allowRetire=true;f.s.raw.nativeMagazineAttached=false;
 for(unsigned n=0;n<12;++n){f.Send(false);CHECK(f.policy->BlocksEquipment());}
 f.s.raw.nativeMagazineAttached=true;f.Send(false);CHECK(!f.policy->BlocksEquipment());
 f.reserve.loaded=29;f.reserve.reloadInputReady=true;f.gateSent=false;f.submitted.reset();f.ack.reset();
 f.Send(false);f.Send(false,true);CHECK(f.starts==2);return 0;}
int DisabledUnsupportedAndFocus(){Fixture off(false);off.Send();off.Send(false,true);CHECK(off.starts==0&&!off.result.tracking.enabled);
 Fixture wrong;wrong.s.asset="40mmgl_sp";wrong.Send();wrong.Send(false,true);CHECK(wrong.starts==0);
 Fixture lost;CHECK(lost.Eject());lost.s.input.focused=false;lost.Send();CHECK(lost.cancels==1&&!lost.result.tracking.target);return 0;}
int DistinctFamilyMappingAndExpiry(){
 Fixture f;CHECK(f.s.weapon.id!=f.s.nativeOwner.weapon);CHECK(f.Insert());
 CHECK(f.result.tracking.family.binding.weapon==f.s.weapon&&f.result.tracking.target->weapon==f.s.weapon);
 CHECK(f.submitted->request.owner.weapon==f.s.nativeOwner.weapon&&f.submitted->reservation.claim.owner==f.s.input.owner);
 CHECK(MagazineTargetFresh(f.result.tracking,f.now));
 auto forged=f.result.tracking;forged.target->weapon.id=f.s.nativeOwner.weapon;CHECK(!MagazineTargetFresh(forged,f.now));
 forged=f.result.tracking;forged.family.binding.owner.weapon++;CHECK(!MagazineTargetFresh(forged,f.now));
 forged=f.result.tracking;forged.family.deadlineNs=f.now;CHECK(!MagazineTargetFresh(forged,f.now));
 for(unsigned bad=1;bad<=10;++bad){Fixture g;g.familyFault=bad;g.Send();g.Send(false,true);CHECK(g.starts==0&&!g.result.tracking.enabled);}
 // Changing a still structurally valid inventory binding mid-cycle must not
 // silently adopt a new resource owner; advancing clocks alone were admitted.
 Fixture g;CHECK(g.Eject());g.familyFault=11;g.Send();CHECK(g.cancels==1&&g.policy->BlocksEquipment());
 return 0;
}
int NativeStartRejectionAndUnknownRecovery(){
 for(bool deferred:{false,true}){Fixture f;f.startResult=deferred?MagazineCycleStartResult::Unknown:MagazineCycleStartResult::NotStarted;
  f.Send();f.Send(true);CHECK(f.starts==1&&!f.result.reloadHeld&&f.submits==0&&f.cancels==0);
  CHECK(f.Report().find(deferred?"\"stage\":8":"\"stage\":6")!=std::string::npos);
  CHECK(f.Report().find("\"identity_present\":1,\"identity_matches\":1")!=std::string::npos);
  if(deferred){CHECK(f.policy->BlocksEquipment());for(unsigned n=0;n<4;++n)f.Send(true);
   CHECK(f.starts==1&&f.inspections==4&&f.policy->BlocksEquipment()&&f.cancels==0);
   f.inspectResult=MagazineCycleStartResult::NotStarted;f.Send(true);}
  CHECK(!f.policy->BlocksEquipment()&&!f.hands.Current(InteractionHand::Left));
  CHECK(f.reserve.loaded==27&&f.reserve.reserve==83&&f.result.completed==0);
  f.startResult=MagazineCycleStartResult::Started;f.Send(true);CHECK(f.starts==1);
  f.Send();f.Send(true);CHECK(f.starts==2&&f.cycle==2&&f.result.reloadHeld);
 }
 Fixture registered;registered.startResult=MagazineCycleStartResult::Unknown;registered.Send();registered.Send(false,true);
 registered.inspectResult=MagazineCycleStartResult::RegisteredCancelled;registered.Send(false,true);
 CHECK(registered.cancels==1&&registered.policy->BlocksEquipment()&&!registered.result.reloadHeld);
 registered.allowRetire=true;registered.Send();registered.Send();CHECK(!registered.policy->BlocksEquipment());
 Fixture direct;direct.startResult=MagazineCycleStartResult::RegisteredCancelled;direct.Send();direct.Send(false,true);
 CHECK(direct.cancels==1&&direct.policy->BlocksEquipment()&&!direct.result.reloadHeld);return 0;
}
int NativeTransferGapKeepsOnlySubmittedCycle(){Fixture f;CHECK(f.Insert());const auto original=f.result.tracking.target;
 CHECK(!f.result.ownsLeftHand&&!f.hands.Current(InteractionHand::Left));
 CHECK(original&&f.submits==1);f.source=false;f.Send(true);CHECK(f.cancels==0&&f.policy->BlocksEquipment()&&!f.result.reloadHeld);
 CHECK(f.result.completed==0&&f.submits==1&&f.Report().find("\"transfer_read_deferrals\":1")!=std::string::npos);
 CHECK(f.result.tracking.target&&f.result.tracking.target->observedNs==original->observedNs&&f.result.tracking.target->deadlineNs==original->deadlineNs);
 f.Send(true);CHECK(f.cancels==0&&f.submits==1);f.source=true;
 // Counts become coherent before the ordinary0.7s tail supplies the native ack.
 const auto held=f.submitted->heldLease;f.reserve.loaded=30;f.reserve.reserve=80;
 for(unsigned n=0;n<35;++n){f.Send(true);CHECK(f.cancels==0&&f.result.completed==0&&f.submits==1);}
 const auto& r=*f.submitted;f.ack=ReloadMagazineAckEvidence{{{r.request.id,r.request.owner,r.request.operation,ReloadAcknowledgement::Applied},
  f.reserve.identity,f.cycle,f.seq+1,700,held.loaded,held.reserve,30,80},f.now,f.now+100*Ms,true};
 f.Send(true);CHECK(f.result.completed==1&&f.cancels==1&&f.reserve.loaded==30&&f.reserve.reserve==80);
 f.allowRetire=true;f.Send();f.Send();CHECK(!f.policy->BlocksEquipment());return 0;}
int NativeTransferGapDoesNotExtendLeaseOrBypassSafety(){
 Fixture expired;CHECK(expired.Insert());expired.source=false;
 for(unsigned n=0;n<6;++n)expired.Send(true);CHECK(expired.cancels==1&&expired.result.completed==0&&expired.policy->BlocksEquipment());
 CHECK(expired.Report().find("\"transfer_read_expirations\":1")!=std::string::npos);
 for(unsigned bad=0;bad<5;++bad){Fixture f;CHECK(f.Insert());f.source=false;
  if(bad==0)f.s.input.focused=false;if(bad==1)f.s.input.tracked[0]=false;if(bad==2)f.s.cancel=true;
  if(bad==3)f.keep=false;if(bad==4)f.familyFault=11;f.Send(true);
  CHECK(f.cancels==1&&f.result.completed==0&&f.policy->BlocksEquipment()&&!f.result.tracking.target);}
 Fixture arming;arming.Send();arming.Send(false,true);arming.source=false;arming.Send();CHECK(arming.cancels==1); // No acknowledged hold: no special waiting.
 return 0;
}
int PersistentCancelAllowsOnlyVerifiedRetirement(){Fixture f;CHECK(f.Insert());f.s.cancel=true;f.Send(true);
 CHECK(f.cancels==1&&f.policy->BlocksEquipment()&&f.result.completed==0);f.allowRetire=true;
 f.s.raw.nativeMagazineAttached=false;f.Send();f.Send();CHECK(f.policy->BlocksEquipment());
 f.s.raw.nativeMagazineAttached=true;f.Send();CHECK(!f.policy->BlocksEquipment());
 CHECK(f.result.completed==0&&f.Report().find("\"pending\":0")!=std::string::npos);
 f.Send(false,true);CHECK(f.starts==1&&!f.result.reloadHeld);return 0;}
int AcknowledgedHoldSurvivesOnlyBoundedReadGap(){
 Fixture f;f.Send();f.Send(true);f.held=true;f.Send(true,false,.065f);CHECK(f.result.interaction.phase==DetachableMagazinePhase::Pulling);
 f.source=false;f.Send(true,false,.05f);CHECK(f.cancels==0&&f.result.blocksWeaponActions&&f.result.ownsLeftHand&&!f.result.reloadHeld);
 CHECK(f.Report().find("\"held_read_deferrals\":1")!=std::string::npos);f.source=true;f.Send(true,false,.03f);f.Send(true,false,-.005f);
 CHECK(f.result.interaction.physicallyRemoved&&f.submits==0&&f.reserve.loaded==27&&f.reserve.reserve==83);
 Fixture expired;CHECK(expired.Eject());expired.source=false;for(unsigned n=0;n<6;++n)expired.Send();CHECK(expired.cancels==1);
 CHECK(expired.Report().find("\"cancel_cause\":3")!=std::string::npos);
 for(unsigned bad=0;bad<4;++bad){Fixture g;CHECK(g.Eject());g.source=false;
  if(bad==0)g.s.input.focused=false;if(bad==1)g.s.input.tracked[0]=false;if(bad==2)g.familyFault=11;if(bad==3)g.keep=false;
  g.Send();CHECK(g.cancels==1&&g.submits==0&&g.result.completed==0);}
 return 0;}
int RemovedMagazineHandUsesAuthoredGraspWithoutMovingRawContact(){
 Fixture f;f.Send();
 const auto send=[&](float travel){
  f.now+=20*Ms;++f.s.input.sequence;f.s.input.observedNs=f.s.input.nowNs=f.now;f.s.input.deadlineNs=f.now+100*Ms;
  f.s.input.released[0]=false;f.s.gripPressed=true;f.s.ejectPressed=false;f.s.geometrySequence=f.s.input.sequence;
  f.Geometry(travel);auto turn=Pose();const float a=.4f;
  turn.values[0][0]=turn.values[1][1]=std::cos(a);turn.values[0][1]=std::sin(a);turn.values[1][0]=-std::sin(a);
  f.s.raw.rawLeftWristWorldMeters=Multiply(turn,f.s.raw.rawLeftWristWorldMeters);
  f.s.raw.rawLeftWristWorldMeters.values[3][0]+=.04f;f.s.raw.rawLeftWristWorldMeters.values[3][1]+=.02f;
  f.s.raw.inputEvidence=f.s.input;f.s.originalHandEvidence=f.s.input;f.Sync();
 };
 send(.1f);CHECK(f.result.interaction.removalGrabbed&&f.starts==1);
 const auto capturedRaw=f.s.raw.rawLeftWristWorldMeters;
 const auto itemFromRaw=Multiply(capturedRaw,*InverseRigid(xm8_magazine_calibration::AttachedItem));
 f.held=true;send(.1f);send(.1f);
 RigSnapshot rig;rig.names={"root","jntWpn_1","jntWpn_6","LeftHand"};rig.parents={-1,0,1,0};rig.weaponBone=1;
 for(const char* digit:{"Thumb","Index","Middle","Ring","Pinky"})for(unsigned j=1;j<=3;++j){
  rig.parents.push_back(j==1?3:int(rig.names.size()-1));rig.names.push_back(std::string("LeftHand")+digit+std::to_string(j));}
 rig.identity.soldier=f.s.nativeOwner.soldier;rig.identity.weak=f.s.nativeOwner.weak;
 rig.inverseBind.assign(rig.names.size(),Pose());rig.evaluatedWorld.assign(rig.names.size(),Pose());
 const auto binding=magazine_presentation_detail::Derive(rig);CHECK(binding);
 auto placed=Pose(12,3,8);const float yaw=.7f;placed.values[0][0]=placed.values[2][2]=std::cos(yaw);
 placed.values[0][2]=std::sin(yaw);placed.values[2][0]=-std::sin(yaw);
 for(const float travel:{.1f,.065f,.03f,-.005f}){
  if(travel!=.1f)send(travel);
  const auto original=f.result.tracking;CHECK(original.target&&original.target->role==MagazinePropRole::Removed);
  const auto rawBefore=f.s.raw.rawLeftWristWorldMeters;
  const auto itemBefore=original.target->weaponFromItemMeters;
  CHECK(Same(itemBefore,Multiply(*InverseRigid(itemFromRaw),rawBefore)));
  CHECK(Distance(Multiply(xm8_magazine_calibration::ItemFromHand,itemBefore),original.target->weaponFromHandMeters)>.03f);
  const auto plan=BuildMagazinePresentation(rig,*binding,original,placed,1,f.now);
  CHECK(plan.binding&&plan.wristTarget&&plan.writes.size()==17);
  CHECK(original.removalFrame);
  const auto& capturedFrame=original.removalFrame->weaponWorldMeters;
  const auto expected=Multiply(Multiply(xm8_magazine_calibration::ItemFromHand,itemBefore),capturedFrame);
  CHECK(Same(*plan.wristTarget,expected));
  CHECK(Same(plan.writes[0].transform,Multiply(itemBefore,capturedFrame)));
  CHECK(Same(plan.writes[1].transform,expected));
  for(unsigned i=0;i<15;++i)CHECK(Same(plan.writes[i+2].transform,Multiply(xm8_magazine_calibration::WristFromFinger[i],expected)));
  CHECK(Same(f.s.raw.rawLeftWristWorldMeters,rawBefore)&&Same(f.result.tracking.target->weaponFromItemMeters,itemBefore));
  CHECK(Same(f.result.tracking.target->weaponFromHandMeters,original.target->weaponFromHandMeters));
 }
 CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld&&f.result.interaction.physicallyRemoved);
 CHECK(f.submits==0&&f.reserve.loaded==27&&f.reserve.reserve==83&&f.result.acquired==0);
 return 0;
}
int PaletteIsolationAndCurrentGuard(){Fixture f;CHECK(f.Eject());f.Send();f.s.bodyFromHand=Pose();f.Send(true,false,-.055f);
 const auto original=f.result.tracking;CHECK(MagazineTargetFresh(original,f.now));
 CHECK(original.target->role==MagazinePropRole::Replacement&&original.target->handTarget&&f.submits==0);
 RigSnapshot rig;rig.names={"root","jntWpn_1","jntWpn_6","LeftHand"};rig.parents={-1,0,1,0};rig.weaponBone=1;
 for(const char* digit:{"Thumb","Index","Middle","Ring","Pinky"})for(unsigned j=1;j<=3;++j){rig.parents.push_back(j==1?3:int(rig.names.size()-1));rig.names.push_back(std::string("LeftHand")+digit+std::to_string(j));}
 rig.names.push_back("unrelated");rig.parents.push_back(0);const auto count=unsigned(rig.names.size());
 rig.identity.soldier=f.s.nativeOwner.soldier;rig.identity.weak=f.s.nativeOwner.weak;rig.identity.count=count;rig.identity.evaluatedMatrices=0x10000;
 rig.inverseBind.assign(count,Pose());rig.evaluatedWorld.assign(count,Pose());rig.nativeEvaluated.resize(count);
 for(auto& b:rig.nativeEvaluated)b.fill(std::byte{0x91});const auto before=rig.nativeEvaluated;
 const auto binding=magazine_presentation_detail::Derive(rig);CHECK(binding&&!BindMagazinePresentation(rig,Xm8MagazineAsset));
 const auto plan=BuildMagazinePresentation(rig,*binding,original,Pose(2,3,4),1,f.now);CHECK(plan.binding&&plan.writes.size()==17&&plan.wristTarget);
 CHECK(Same(plan.writes[0].transform,Multiply(original.target->weaponFromItemMeters,Pose(2,3,4))));
 const auto packed=BuildRigPosePlan(rig,plan.writes);CHECK(packed&&packed->edits.size()==17&&rig.nativeEvaluated==before);
 for(const auto& e:packed->edits){CHECK(e.index!=0&&e.index!=1&&e.index!=count-1);
  for(unsigned r=0;r<4;++r)for(unsigned n=12;n<16;++n)CHECK(e.after[r*16+n]==std::byte{0x91});}
 const auto hidden=HideMagazinePackedPalette(rig,*binding,before);CHECK(hidden&&(*hidden)[0]==before[0]&&(*hidden)[3]==before[3]&&(*hidden)[2]!=before[2]);
 auto current=original;CHECK(!SelectMagazinePackedPalette(original,&current,f.now,*hidden,before,true).fallback);
 ++current.owner.equipGeneration;CHECK(SelectMagazinePackedPalette(original,&current,f.now,*hidden,before,true).fallback);
 CHECK(SelectMagazinePackedPalette(original,&original,original.target->deadlineNs,*hidden,before,true).fallback);return 0;}
bool ReturnOriginal(Fixture& f,bool previous=false){
 f.Send();f.Send(true,false,.1f,previous);if(f.starts!=1||!f.result.interaction.original)return false;
 f.held=true;f.reserve.reloadInputReady=false;
 for(float z:{.065f,.03f,-.005f,-.035f,-.035f,0.f,.035f,.07f,.1f,.1f,.1f,.1f}){
  f.Send(true,false,z,previous);if(f.result.interaction.originalSeat)return true;
 }
 // Alignment begins on inward capture, never on the outward removal leg.
 for(unsigned n=0;n<12;++n){f.Send(true,false,.1f,previous);if(f.result.interaction.originalSeat)return true;}
 return false;
}
int RetainedOriginalReturnsWithoutRefillOrReserveCredit(){
 for(bool previous:{false,true}){Fixture f;CHECK(ReturnOriginal(f,previous));
  CHECK(f.result.interaction.phase==DetachableMagazinePhase::AwaitingOriginalReturn&&f.result.interaction.original);
  const auto instance=*f.result.interaction.original;const auto seat=*f.result.interaction.originalSeat;
  CHECK(instance.rounds==27&&instance.capacity==30&&instance.item!=f.s.weapon&&instance.pool.id==f.reserve.identity.serverItem);
  CHECK(seat.identity.item==instance.item&&seat.itemClaim.item==f.s.weapon&&seat.itemClaim.prerequisiteClaim==seat.weaponClaim.id);
  CHECK(f.cancels==1&&f.submits==0&&f.result.acquired==0&&f.result.completed==0&&f.policy->BlocksEquipment());
  CHECK(!f.hands.Current(InteractionHand::Left));
  CHECK(f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Attached&&MagazineTargetFresh(f.result.tracking,f.now));
  // Neither a drain receipt nor a visual attachment proves native reload ended.
  f.allowRetire=true;f.Send();CHECK(f.policy->BlocksEquipment());
  f.reserve.reloadInputReady=true;f.s.raw.nativeMagazineAttached=false;f.Send();CHECK(f.policy->BlocksEquipment());
  f.s.raw.nativeMagazineAttached=true;f.Send();CHECK(!f.policy->BlocksEquipment());
  CHECK(f.Report().find("\"original_returns\":1")!=std::string::npos);
  const auto proof=f.policy->ProbeState(f.now);CHECK(proof.originalReceipt&&proof.originalReceipt->original==instance&&
   proof.originalReceipt->seat==seat.id&&proof.originalReceipt->cycle==f.cycle&&proof.originalReceipt->verified);
  CHECK(f.starts==1&&f.submits==0&&f.result.completed==0&&f.reserve.loaded==27&&f.reserve.reserve==83);
  ActionOutput a;a.held=a.pressed=Fire|Use|Reload;ApplyMagazinePhysicalActions(a,Xm8MagazineAsset,f.result);
  CHECK(a.held==(Fire|Use)&&a.pressed==(Fire|Use));
  // A second interaction has a new instance; a held old gesture cannot repeat.
  f.gateSent=false;f.Send(true);CHECK(f.starts==1);f.Send();f.Send(true);
  CHECK(f.starts==2&&f.result.interaction.original&&f.result.interaction.original->item!=instance.item);
 }
 return 0;
}
int OriginalReturnRejectsChangedNativeCountsAndOwners(){
 for(unsigned fault=0;fault<4;++fault){Fixture f;CHECK(ReturnOriginal(f));
  const auto original=*f.result.interaction.original;f.allowRetire=true;
  if(fault==0){f.reserve.loaded=30;f.reserve.reserve=80;}
  if(fault==1)++f.reserve.reserve;
  if(fault==2){++f.s.nativeOwner.equipGeneration;f.reserve.identity.owner=f.s.nativeOwner;f.s.raw.owner=f.s.nativeOwner;}
  if(fault==3)f.s.cancel=true;
  f.Send();f.Send();CHECK(f.Report().find("\"original_returns\":0")!=std::string::npos);
  CHECK(f.submits==0&&f.result.completed==0&&f.result.acquired==0&&!f.policy->BlocksEquipment());
  CHECK(original.rounds==27); // Immutable removed-mag fact, not rewritten to changed reserve.
 }
 return 0;
}
int OriginalReturnWaitsThroughReadGapsWithoutFreshnessInflation(){
 Fixture f;CHECK(ReturnOriginal(f));f.source=false;f.Send();f.Send();CHECK(f.cancels==1&&f.policy->BlocksEquipment());
 CHECK(f.Report().find("\"original_returns\":0")!=std::string::npos&&!f.result.tracking.target);
 f.source=true;f.allowRetire=true;f.reserve.reloadInputReady=true;f.Send();
 CHECK(!f.policy->BlocksEquipment()&&f.Report().find("\"original_returns\":1")!=std::string::npos);
 CHECK(f.submits==0&&f.reserve.loaded==27&&f.reserve.reserve==83);return 0;
}
int OriginalTargetRejectsForgedMagazineFacts(){
 Fixture f;f.Send();f.Send(true);f.held=true;f.Send(true,false,.065f);f.Send(true,false,.03f);
 CHECK(f.result.tracking.target&&f.result.tracking.target->originalMagazine&&MagazineTargetFresh(f.result.tracking,f.now));
 for(unsigned bad=0;bad<5;++bad){auto forged=f.result.tracking;auto& v=*forged.target->originalMagazine;
  if(bad==0)++v.owner.actorGeneration;if(bad==1)++v.weapon.id;if(bad==2)++v.profile.id;
  if(bad==3)v.item=v.weapon;if(bad==4)v.rounds=v.capacity+1;CHECK(!MagazineTargetFresh(forged,f.now));}
 CHECK(f.submits==0&&f.result.acquired==0);return 0;
}
int LongEmptyWellTimeoutRequiresNativeRetirement(){
 Fixture f;CHECK(f.Eject());const auto begin=f.now;
 // Actual headset cycle1 was abandoned for30s with no supply item or seat.
 // Keep genuinely fresh packets/leases while exercising the transaction bound.
 for(unsigned n=0;n<1600&&!f.cancels;++n){f.Send();
  CHECK(f.result.blocksWeaponActions&&f.submits==0&&f.result.completed==0);
 }
 CHECK(f.cancels==1&&f.now-begin>=29000*Ms&&f.now-begin<=30100*Ms);
 CHECK(f.reserve.loaded==27&&f.reserve.reserve==83&&f.Report().find("\"pending\":0")!=std::string::npos);
 // Preserve the recognizer rejection, rather than overwriting it with Explicit.
 CHECK(f.Report().find("\"reason\":16")!=std::string::npos);
 CHECK(f.Report().find("\"cancel_cause\":11")!=std::string::npos);
 ActionOutput actions;actions.held=actions.pressed=Fire|Use|Reload;
 ApplyMagazinePhysicalActions(actions,Xm8MagazineAsset,f.result);CHECK(!actions.held&&!actions.pressed);
 for(unsigned n=0;n<4;++n)f.Send();CHECK(f.cancels==1&&f.policy->BlocksEquipment());
 f.allowRetire=true;f.s.raw.nativeMagazineAttached=false;f.Send();CHECK(f.policy->BlocksEquipment());
 f.s.raw.nativeMagazineAttached=true;f.Send();CHECK(!f.policy->BlocksEquipment()&&!f.result.blocksWeaponActions);
 actions.held=actions.pressed=Fire|Use|Reload;ApplyMagazinePhysicalActions(actions,Xm8MagazineAsset,f.result);
 CHECK(actions.held==(Fire|Use)&&actions.pressed==(Fire|Use));
 CHECK(f.starts==1&&f.submits==0&&f.result.completed==0&&f.reserve.loaded==27&&f.reserve.reserve==83);
 return 0;
}
int OwnerReplacementRetiresOldMagazineWithoutAReceipt(){
 Fixture f;CHECK(f.Eject());const auto old=f.s.nativeOwner;
 ++f.s.nativeOwner.equipGeneration;f.reserve.identity.owner=f.s.nativeOwner;f.s.raw.owner=f.s.nativeOwner;
 f.Send();CHECK(f.cancels==1&&f.policy->BlocksEquipment()&&f.result.completed==0);
 CHECK(f.Report().find("\"cancel_cause\":4")!=std::string::npos);
 f.allowRetire=true;f.Send();f.Send();CHECK(!f.policy->BlocksEquipment());
 ActionOutput actions;actions.held=actions.pressed=Fire;ApplyMagazinePhysicalActions(actions,Xm8MagazineAsset,f.result);
 CHECK(actions.held==Fire&&actions.pressed==Fire&&f.submits==0&&f.result.completed==0);
 CHECK(old!=f.s.nativeOwner&&f.reserve.loaded==27&&f.reserve.reserve==83);
 return 0;
}
int FullIdleCountsCannotDivertUnfinishedPhysicalRetirement(){for(bool zeroReserve:{false,true}){
 Fixture f;Bc2MagazineDetached detached(true);if(zeroReserve)f.reserve.reserve=3;
 CHECK(f.Insert());f.Complete();f.reserve.allThreeIdle=true;
 CHECK(detached.Handles(f.reserve)&&f.policy->BlocksEquipment());
 // Actual refill has arrived, but this consumer has not received/drained its
 // receipt. The shared dispatch must keep ticking it through both boundaries.
 CHECK(!detached.Routes(f.reserve,f.policy->BlocksEquipment()));f.Send(true);
 CHECK(f.result.completed==1&&f.policy->BlocksEquipment());
 f.allowRetire=true;f.s.raw.nativeMagazineAttached=false;
 for(unsigned n=0;n<4;++n){CHECK(!detached.Routes(f.reserve,f.policy->BlocksEquipment()));f.Send();}
 CHECK(f.policy->BlocksEquipment());f.s.raw.nativeMagazineAttached=true;
 CHECK(!detached.Routes(f.reserve,f.policy->BlocksEquipment()));f.Send();
 CHECK(!f.policy->BlocksEquipment()&&detached.Routes(f.reserve,f.policy->BlocksEquipment()));
 CHECK(f.starts==1&&f.submits==1&&f.reserve.loaded==30&&f.reserve.reserve==(zeroReserve?0:80));
 }return 0;}
}
int DeferredKeepAliveNeverRenews(){
 Fixture f;f.Send();f.Send(false,true);CHECK(f.starts==1);
 const auto oldDeadline=f.s.input.deadlineNs;f.keepDeferred=true;
 for(unsigned i=0;i<4;++i){f.Send();CHECK(f.now<oldDeadline);CHECK(f.cancels==0&&f.submits==0&&f.result.blocksWeaponActions);CHECK(!f.result.reloadHeld&&!f.result.tracking.target);}
 f.Send();CHECK(f.now==oldDeadline&&f.cancels==1&&f.submits==0);
 Fixture g;g.Send();g.Send(false,true);g.keepDeferred=true;g.Send();CHECK(g.cancels==0);
 g.keepDeferred=false;g.Send();CHECK(g.cancels==0);g.keep=false;g.Send();CHECK(g.cancels==1);
 return 0;
}
int ObserverDeferralAndStartOrigin(){
 for(bool grasp:{false,true}){auto ptr=std::make_unique<Fixture>();auto& f=*ptr;
  f.s.actionFlagsKnown=true;f.s.actionHeld=123;f.s.actionPressed=456;f.Send();f.Send(grasp,!grasp,.1f);CHECK(f.starts==1);
  const auto journal=f.Report();CHECK(journal.find("\"start_flags_known\":true")!=std::string::npos);
  CHECK(journal.find("\"start_held\":123")!=std::string::npos&&journal.find("\"start_pressed\":456")!=std::string::npos);
  CHECK(journal.find(grasp?"\"start_grip_pressed\":true":"\"start_eject_pressed\":true")!=std::string::npos);
  const auto deadline=f.s.input.deadlineNs;f.reserveDeferred=true;
  for(unsigned i=0;i<4;++i){f.Send();CHECK(f.now<deadline&&f.cancels==0&&f.submits==0&&f.result.blocksWeaponActions);}
  f.Send();CHECK(f.now==deadline&&f.cancels==1&&f.submits==0);
 }
 Fixture rejected;rejected.Send();rejected.Send(false,true);rejected.reserveRejected=true;rejected.Send();CHECK(rejected.cancels==1);return 0;
}
int ReserveDeferralCannotDelaySafetyLoss(){
 for(unsigned fault=0;fault<9;++fault){auto ptr=std::make_unique<Fixture>();auto& f=*ptr;
  f.Send();f.Send(false,true);CHECK(f.starts==1);f.reserveDeferred=true;
  if(fault==0)f.s.cancel=true;
  if(fault==1)f.s.input.focused=false;
  if(fault==2)f.s.input.tracked[0]=false;
  if(fault==3)++f.s.trackingEpoch;
  if(fault==4)++f.s.weapon.generation;
  if(fault==5)f.familyFault=11;
  if(fault==6)f.s.meshes.reset();
  if(fault==7)f.s.input.tracked[1]=false;
  f.Send();
  if(fault==8){CHECK(f.cancels==0&&f.gun);f.hands.Release(f.s.input,f.gun->token);f.result=f.policy->Tick(f.s,f.hands,f.intent);}
  CHECK(f.cancels==1&&f.submits==0);
 }
 return 0;
}

int TypedCohortGapUsesOnlyOriginalRemovalOrSubmittedEvidence(){
 for(bool submitted:{false,true}){Fixture f;
  if(submitted){CHECK(f.Insert());}else{f.Send();f.Send(true);f.held=true;f.Send(true,false,.065f);CHECK(f.result.interaction.phase==DetachableMagazinePhase::Pulling);}
  const auto before=f.result.tracking;const auto starts=f.starts,submits=f.submits;const auto oldLoaded=f.reserve.loaded,oldReserve=f.reserve.reserve;
  f.reserveCohortGap=true;f.Send(true,false,.04f);CHECK(f.cancels==0&&f.starts==starts&&f.submits==submits&&f.result.completed==0&&!f.result.reloadHeld);
  CHECK(f.reserve.loaded==oldLoaded&&f.reserve.reserve==oldReserve);
  if(before.target&&f.result.tracking.target){CHECK(f.result.tracking.target->deadlineNs==before.target->deadlineNs);CHECK(f.result.tracking.target->observedNs==before.target->observedNs);}
  f.reserveCohortGap=false;
  if(submitted){f.Complete();f.Send(true);CHECK(f.result.completed==1&&f.submits==1);}else{f.Send(true,false,.03f);f.Send(true,false,-.005f);CHECK(f.result.interaction.physicallyRemoved&&f.submits==0);}
 }
 for(unsigned fault=0;fault<5;++fault){Fixture f;CHECK(f.Insert());f.reserveCohortGap=true;
  if(fault==0)++f.s.nativeOwner.equipGeneration;if(fault==1)f.s.input.focused=false;if(fault==2)f.reserveRejected=true;
  if(fault==3)f.now+=100*Ms;if(fault==4)f.familyFault=11;
  f.Send(true);CHECK(f.cancels==1&&f.submits==1&&f.result.completed==0);}
 Fixture fresh;fresh.reserveCohortGap=true;fresh.Send();fresh.Send(false,true);CHECK(fresh.starts==0&&fresh.submits==0&&!fresh.hands.Current(InteractionHand::Left));return 0;
}

int TypedNativeObservationGap(){
 for(unsigned removal=0;removal<2;++removal){Fixture f;
  if(removal){f.Send();f.Send(true);f.held=true;f.nativeLifetime=5*Ms;f.Send(true,false,.075f);}
  else {CHECK(f.Eject());f.Send();f.s.bodyFromHand=Pose();f.nativeLifetime=5*Ms;f.Send(true,false,-.1f);f.Send(true,false,-.075f);}
  const auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim);const auto phase=f.result.interaction.phase;
  const auto progress=f.result.interaction.insertion.progress;const auto acquired=f.policy->ProbeState(f.now).acquired;
  f.observationDeferred=f.missingLease=true;
  f.Send(true,false,removal?.075f:.1f);CHECK(f.cancels==0&&f.submits==0&&f.result.ownsLeftHand);
  CHECK(f.result.interaction.phase==phase&&f.result.interaction.insertion.progress==progress);
  CHECK(f.hands.Current(InteractionHand::Left)->token==claim->token&&f.hands.Current(InteractionHand::Left)->inputSequence==f.s.input.sequence);
  CHECK(f.policy->ProbeState(f.now).acquired==acquired);
  f.Send(true,false,removal?.075f:.1f);CHECK(f.cancels==0&&f.submits==0);
  f.observationDeferred=f.missingLease=false;f.Send(true,false,removal?.075f:-.075f);
  CHECK(f.cancels==0&&f.submits==0&&f.hands.Current(InteractionHand::Left)->token==claim->token);
  CHECK(f.Report().find("\"observation_deferred\":2")!=std::string::npos);
 }
 {Fixture f;CHECK(f.Eject());f.Send();f.s.bodyFromHand=Pose();f.Send(true,false,-.1f);f.observationDeferred=f.missingLease=true;
  for(unsigned i=0;i<4;++i)f.Send(true,false,-.1f);
  CHECK(f.cancels==1&&f.submits==0&&f.Report().find("\"observation_expired\":1")!=std::string::npos);}
 for(unsigned mode=0;mode<7;++mode){Fixture f;CHECK(f.Eject());f.Send();f.s.bodyFromHand=Pose();f.Send(true,false,-.1f);
  f.observationDeferred=f.missingLease=true;f.Send(true,false,-.1f);CHECK(f.cancels==0);
  if(mode==0)f.observationRejected=true;if(mode==1)f.keep=false;if(mode==2)f.s.cancel=true;
  if(mode==3)f.s.input.focused=false;if(mode==4)++f.s.nativeOwner.actorGeneration;
  if(mode==5){const auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim);CHECK(f.hands.Release(f.s.input,claim->token).accepted);}
  if(mode==6)f.now+=110*Ms;
  f.Send(true,false,-.1f);CHECK(f.cancels==1&&f.submits==0);
 }
 {Fixture f;CHECK(f.Eject());f.Send();f.s.bodyFromHand=Pose();f.Send(true,false,-.1f);f.observationDeferred=f.missingLease=true;
  f.Send(true,false,-.1f);const auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim);
  f.now+=10*Ms;f.Sync();CHECK(f.cancels==0&&f.hands.Current(InteractionHand::Left)->deadlineNs==claim->deadlineNs);
  f.now+=50*Ms;f.Sync();CHECK(f.cancels==1&&f.submits==0);}
 {Fixture f;CHECK(f.Eject());f.Send();f.s.bodyFromHand=Pose();f.Send(true,false,-.1f);f.observationDeferred=f.missingLease=true;
  f.Send(true,false,-.1f);f.Send(false,false,-.1f);CHECK(f.cancels==0&&!f.hands.Current(InteractionHand::Left)&&!f.result.ownsLeftHand);}
 {Fixture f;CHECK(f.Insert());f.observationDeferred=f.missingLease=true;f.Complete();f.Send(false);
  CHECK(f.result.completed==1&&f.result.interaction.transaction.completed&&f.submits==1&&f.cancels==1);}
 return 0;
}
int StartupPulseSurvivesOnlyOriginalDeadlineDuringObservationWait(){
 Fixture f;f.Send();f.Send(true);CHECK(f.starts==1&&f.result.reloadHeld);
 CHECK(f.result.interaction.phase==DetachableMagazinePhase::PreparingRemoval);
 const auto started=f.now;const auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim);
 f.observationDeferred=f.missingLease=true;
 f.Send(true);CHECK(f.now-started==20*Ms&&f.result.reloadHeld&&f.cancels==0);
 CHECK(f.result.interaction.phase==DetachableMagazinePhase::PreparingRemoval&&f.submits==0&&f.result.acquired==0);
 CHECK(f.hands.Current(InteractionHand::Left)->token==claim->token);
 ActionOutput actions;ApplyMagazinePhysicalActions(actions,f.s.asset,f.result);CHECK(actions.held&Reload);
 f.Send(true);CHECK(f.now-started==40*Ms&&f.result.reloadHeld&&f.cancels==0&&f.submits==0);
 // A genuine Ready observation resets only the gap watchdog, not pulseUntil.
 f.observationDeferred=f.missingLease=false;f.Send(true);CHECK(f.now-started==60*Ms&&f.result.reloadHeld&&f.cancels==0);
 f.observationDeferred=f.missingLease=true;f.Send(true);CHECK(f.now-started==80*Ms&&f.result.reloadHeld&&f.cancels==0);
 f.Send(true);CHECK(f.now-started==100*Ms&&!f.result.reloadHeld&&f.cancels==0&&f.submits==0&&f.result.acquired==0);
 actions={};ApplyMagazinePhysicalActions(actions,f.s.asset,f.result);CHECK(!(actions.held&Reload));
 CHECK(f.result.interaction.phase==DetachableMagazinePhase::PreparingRemoval);
 f.s.cancel=true;f.Send(true);CHECK(f.cancels==1&&!f.result.reloadHeld&&f.submits==0);
 // Cancellation within the original pulse also removes it immediately.
 Fixture cancelled;cancelled.Send();cancelled.Send(true);cancelled.observationDeferred=cancelled.missingLease=true;
 cancelled.s.cancel=true;cancelled.Send(true);CHECK(cancelled.cancels==1&&!cancelled.result.reloadHeld);
 CHECK(cancelled.result.acquired==0&&cancelled.submits==0);
 return 0;
}
int StartupWaitUsesOriginalUnseatTimeoutNotHeldGap(){
 auto fixture=std::make_unique<Fixture>();auto& f=*fixture;f.Send();f.Send(true);const auto started=f.now;const auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim);
 f.observationDeferred=f.missingLease=true;
 for(unsigned n=0;n<8;++n){f.Send(true);CHECK(f.cancels==0&&f.result.interaction.phase==DetachableMagazinePhase::PreparingRemoval);
  CHECK(f.result.reloadHeld==(f.now-started<100*Ms));CHECK(!f.result.interaction.transaction.acknowledged&&!f.result.interaction.seat);
  CHECK(f.result.acquired==0&&f.submits==0&&f.hands.Current(InteractionHand::Left)->token==claim->token);}
 CHECK(f.now-started==160*Ms&&f.Report().find("\"observation_expired\":0")!=std::string::npos);
 f.observationDeferred=f.missingLease=false;f.held=true;f.Send(true);CHECK(f.cancels==0&&f.result.interaction.phase==DetachableMagazinePhase::Pulling);
 CHECK(f.result.interaction.transaction.acknowledged&&!f.result.reloadHeld&&f.submits==0);
 // Fresh control packets cannot renew the original4s unseat request deadline.
 for(unsigned duplicate=0;duplicate<2;++duplicate){auto fixture=std::make_unique<Fixture>();auto& timeout=*fixture;timeout.Send();timeout.Send(true);const auto origin=timeout.now;
  timeout.observationDeferred=timeout.missingLease=true;
  for(unsigned n=0;n<200;++n){timeout.Send(true);CHECK(timeout.cancels==0&&timeout.submits==0&&timeout.result.acquired==0);}
  CHECK(timeout.now-origin==4000*Ms&&!timeout.result.reloadHeld);
  if(duplicate){const auto previous=timeout.hands.Current(InteractionHand::Left);CHECK(previous);timeout.now+=Ms;timeout.Sync();}
  else timeout.Send(true);
  CHECK(timeout.cancels==1&&timeout.submits==0&&timeout.result.interaction.phase==DetachableMagazinePhase::Cancelled);
  CHECK(timeout.result.interaction.transaction.reason==ManualReloadCancel::AcknowledgementTimeout);
  CHECK(timeout.Report().find("\"observation_expired\":0")!=std::string::npos);
 }
 // Post-gate waiting cannot restart the original30s transaction lifetime.
 auto transactionFixture=std::make_unique<Fixture>();auto& transaction=*transactionFixture;CHECK(transaction.Eject());
 for(unsigned n=0;n<1499;++n){transaction.Send();CHECK(transaction.cancels==0&&transaction.submits==0);}
 transaction.observationDeferred=transaction.missingLease=true;transaction.Send();
 CHECK(transaction.cancels==1&&transaction.result.interaction.transaction.reason==ManualReloadCancel::TransactionTimeout);
 CHECK(transaction.submits==0&&transaction.result.acquired==0);
 for(unsigned loss=0;loss<4;++loss){auto fixture=std::make_unique<Fixture>();auto& unsafe=*fixture;unsafe.Send();unsafe.Send(true);unsafe.observationDeferred=unsafe.missingLease=true;
  for(unsigned n=0;n<4;++n)unsafe.Send(true);CHECK(unsafe.cancels==0);
  if(loss==0)++unsafe.s.nativeOwner.actorGeneration;if(loss==1)unsafe.keep=false;
  if(loss==2)unsafe.s.input.focused=false;if(loss==3)unsafe.now+=110*Ms;
  unsafe.Send(true);CHECK(unsafe.cancels==1&&!unsafe.result.reloadHeld&&unsafe.submits==0&&unsafe.result.acquired==0);
 }
 return 0;
}
int MagazineFallbackObservationDoesNotChangeSelection(){
#ifndef BASELINE_DIAGNOSTICS
 auto fixture=std::make_unique<Fixture>();auto& f=*fixture;CHECK(f.Insert());
 const auto old=f.result.tracking;CHECK(MagazineTargetFresh(old,f.now));
 std::vector<std::array<std::byte,64>> posed(2),ordinary(2);posed[0][0]=std::byte{7};ordinary[0][0]=std::byte{3};
 const auto success=SelectMagazinePackedPalette(old,&old,f.now,posed,ordinary,true);
 CHECK(!success.fallback&&success.bytes.data()==posed.data());CHECK(ClassifyMagazinePaletteFallback(old,&old,f.now,posed,true)==0);
 const auto check=[&](const MagazineTracking& original,const MagazineTracking* current,bool coherent,MagazineFallbackReason reason){
  const auto choice=SelectMagazinePackedPalette(original,current,f.now,posed,ordinary,coherent);
  return choice.fallback&&choice.bytes.data()==ordinary.data()&&(ClassifyMagazinePaletteFallback(original,current,f.now,posed,coherent)&MagazineFallbackBit(reason));
 };
 CHECK(check(old,nullptr,true,MagazineFallbackReason::CurrentMissing));CHECK(check(old,&old,false,MagazineFallbackReason::ShotIncoherent));
 auto expired=old;expired.target->deadlineNs=f.now;CHECK(check(expired,&old,true,MagazineFallbackReason::OldExpired));
 CHECK(check(old,&expired,true,MagazineFallbackReason::CurrentExpired));
 auto changed=old;++changed.target->gunClaim.id;CHECK(check(old,&changed,true,MagazineFallbackReason::RetentionMismatch));
 changed=old;++changed.cycle;CHECK(check(old,&changed,true,MagazineFallbackReason::RetentionMismatch));
 changed=old;++changed.owner.equipGeneration;CHECK(check(old,&changed,true,MagazineFallbackReason::RetentionMismatch));
 changed=old;changed.target->role=MagazinePropRole::Hidden;CHECK(check(old,&changed,true,MagazineFallbackReason::RetentionMismatch));
 changed=old;changed.replacementFrame=MagazineReplacementFrame{};CHECK(check(old,&changed,true,MagazineFallbackReason::RetentionMismatch));
 CHECK(ClassifyMagazinePaletteFallback(old,&old,f.now,{},true)&MagazineFallbackBit(MagazineFallbackReason::InvalidPalette));
 MagazineFallbackJournal<2> journal;MagazineFallbackEvent event;event.nowNs=f.now;event.pack=1;event.drawSerial=4;
 event.original=MagazineFallbackSnapshotOf(&old);event.current=MagazineFallbackSnapshotOf(&expired);
 event.flags=MagazineFallbackBit(MagazineFallbackReason::SourceReadFailed);CHECK(journal.Record(event));
 event.nowNs+=1;event.pack=2;event.flags=MagazineFallbackBit(MagazineFallbackReason::SourceChanged);CHECK(journal.Record(event));
 event.nowNs+=1;event.flags=MagazineFallbackBit(MagazineFallbackReason::OldExpired);CHECK(!journal.Record(event));
 CHECK(journal.Total()==3&&journal.Dropped()==1&&journal.ReasonCount(MagazineFallbackReason::SourceReadFailed)==1&&journal.ReasonCount(MagazineFallbackReason::SourceChanged)==1&&journal.ReasonCount(MagazineFallbackReason::OldExpired)==1);
 unsigned rows=0;journal.ForEach([&](const MagazineFallbackEvent& recorded){
  CHECK(recorded.nowNs==f.now+rows&&recorded.pack==rows+1&&recorded.drawSerial==4);
  CHECK(recorded.original.gun.id==old.target->gunClaim.id&&recorded.original.deadlines[4]==old.target->deadlineNs);
  CHECK(recorded.current.deadlines[4]==f.now);++rows;return 0;
 });CHECK(rows==2);
 // Source and native palette contents are unchanged by diagnostics.
 CHECK(posed[0][0]==std::byte{7}&&ordinary[0][0]==std::byte{3});
#endif
 return 0;
}
int RenderLifetimeKeepsGeometryButNeverNativeAuthority(){
 {Fixture f;CHECK(f.Insert());f.nativeLifetime=5*Ms;f.Send(false);
  CHECK(f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Attached);
  const auto tracking=f.result.tracking;CHECK(MagazineTargetFresh(tracking,f.now+13*Ms));
  CHECK(tracking.target->deadlineNs==f.s.input.deadlineNs);
  CHECK(!MagazineTargetFresh(tracking,tracking.target->deadlineNs));
  CHECK(f.submits==1&&f.result.completed==0&&f.reserve.loaded==27&&f.reserve.reserve==83);
 }
 for(bool previous:{false,true}){
  Fixture f;CHECK(f.Eject());f.Send();f.s.bodyFromHand=Pose();f.nativeLifetime=5*Ms;
  f.Send(true,false,-.1f,previous);if(previous)f.Send(true,false,-.1f,true);CHECK(f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Replacement);
  const auto tracking=f.result.tracking;const auto nativeDeadline=f.now+f.nativeLifetime;
  const auto packedAt=nativeDeadline+8*Ms;
  CHECK(MagazineTargetFresh(tracking,packedAt)); // Both observed 192 pack gaps were about 8 ms.
  CHECK(tracking.target->observedNs==f.s.raw.inputEvidence.observedNs);
  CHECK(tracking.target->deadlineNs==f.s.raw.inputEvidence.deadlineNs);
  CHECK(!MagazineTargetFresh(tracking,tracking.target->deadlineNs));
  for(unsigned fault=0;fault<4;++fault){auto changed=tracking;
   if(fault==0)++changed.target->owner.space;
   if(fault==1)++changed.target->nativeCycle;
   if(fault==2)changed.target->role=MagazinePropRole::Hidden;
   if(fault==3)++changed.target->handClaim.id;
   CHECK(!MagazineTargetRetained(tracking,changed,packedAt));
  }
  auto expired=tracking;expired.target->deadlineNs=packedAt;
  CHECK(!MagazineTargetFresh(expired,packedAt));
  // Expired native proof cannot be reused to acquire, insert, or acknowledge.
  // A fresh packet cannot reuse expired native proof, even while old rendered geometry is fresh.
  f.now=packedAt;f.missingLease=true;++f.s.input.sequence;f.s.input.observedNs=packedAt;f.s.input.deadlineNs=packedAt+100*Ms;f.Sync();
  CHECK(f.cancels==1&&f.submits==0&&!f.result.tracking.target);
  CHECK(f.reserve.loaded==27&&f.reserve.reserve==83);
 }
 return 0;
}
int PairedLauncherTransitionRetiresOnlyRifleMagazine(){
 for(unsigned stage=0;stage<3;++stage){Fixture f;
  if(stage==0){f.Send();f.Send(true);f.held=true;f.Send(true,false,.075f);f.Send(true,false,.04f);f.Send(true,false,.01f);
   CHECK(f.result.reloadHeld&&f.result.tracking.target&&f.hands.Current(InteractionHand::Left));
  }else if(stage==1){CHECK(f.Eject());f.Send();f.s.bodyFromHand=Pose();f.Send(true,false,-.1f);
   CHECK(f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Replacement);
   CHECK(f.hands.Current(InteractionHand::Left)&&f.result.ownsLeftHand);
  }else{CHECK(f.Insert());CHECK(f.result.interaction.phase==DetachableMagazinePhase::AwaitingSeat);}
  const auto rifle=f.s.nativeOwner;const auto rifleIdentity=f.reserve.identity;
  const auto cycle=f.cycle;const auto physical=f.s.weapon;const auto handOwner=f.s.input.owner;
  const auto right=f.hands.Current(InteractionHand::Right);CHECK(right);
  const auto submits=f.submits;
  // Exact supported family shares this physical gun across native rifle/launcher items.
  // Native mode selection itself is covered by WeaponModeTests; this drives its
  // resulting native-item change through the actual shared magazine consumer.
  f.s.nativeOwner.weapon=0xc0000;f.s.asset="40mmgl";f.source=false;
  f.s.raw.owner=f.s.nativeOwner;f.Send(true);
  CHECK(f.s.weapon==physical&&f.s.input.owner==handOwner);
  CHECK(f.hands.Current(InteractionHand::Right)&&f.hands.Current(InteractionHand::Right)->token==right->token);
  CHECK(f.cancels==1&&!f.hands.Current(InteractionHand::Left));
  CHECK(!f.result.reloadHeld&&!f.result.tracking.target&&!f.result.ownsLeftHand);
  CHECK(f.policy->BlocksEquipment()&&f.result.blocksWeaponActions);
  CHECK(f.submits==submits&&f.result.completed==0&&f.starts==1);
  f.Send(true);CHECK(f.cancels==1&&f.policy->BlocksEquipment()&&!f.result.tracking.target);
  f.allowRetire=true;f.Send(true);
  CHECK(f.retiredIdentity&&*f.retiredIdentity==rifleIdentity&&f.retiredCycle==cycle);
  CHECK(f.retiredIdentity->owner==rifle&&f.retiredIdentity->owner!=f.s.nativeOwner);
  CHECK(!f.policy->BlocksEquipment()&&!f.result.blocksWeaponActions);
  CHECK(!f.result.reloadHeld&&!f.result.tracking.target&&!f.hands.Current(InteractionHand::Left));
  // Real portable action routing preserves launcher Fire/Reload once old work drains.
  // This does not claim native launcher ammo execution, tube integration or rendering.
  ActionOutput actions;actions.held=actions.pressed=Fire|Reload;
  ApplyMagazinePhysicalActions(actions,"40mmgl",f.result);
  CHECK(actions.held==(Fire|Reload)&&actions.pressed==(Fire|Reload));
  for(unsigned tick=0;tick<3;++tick){f.Send(true);CHECK(!f.policy->BlocksEquipment()&&!f.result.reloadHeld&&!f.result.tracking.target);}
  CHECK(f.cancels==1&&f.starts==1&&f.submits==submits&&f.result.completed==0);
  CHECK(f.reserve.loaded==27&&f.reserve.reserve==83);
 }
 return 0;
}
int ShortPulseRollsBackWithoutNativeCalls(){
 Fixture f;f.Send();f.inputLifetime=90*Ms;f.Send(false,true);
 CHECK(f.starts==0&&f.cancels==0&&!f.policy->ProbeState(f.now).active);
 CHECK(!f.hands.Current(InteractionHand::Left)&&!f.result.reloadHeld&&!f.policy->BlocksEquipment());
 f.inputLifetime=100*Ms;f.Send();f.Send(false,true);CHECK(f.starts==1&&f.cancels==0);return 0;
}
bool RemovedForDeferral(Fixture& f){f.Send();f.Send(true);f.held=true;
 f.Send(true,false,.075f);f.Send(true,false,.04f);f.Send(true,false,.01f);
 return f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld&&f.result.tracking.target&&f.result.tracking.removalFrame;
}
void SetPresentationDeferral(Fixture& f,unsigned mode){f.reserveDeferred=mode==0;f.keepDeferred=mode!=0;f.reserveCohortGap=mode==2;}
int DeferredPresentationPreservesOnlyIssuedEvidence(){
 for(unsigned mode=0;mode<3;++mode){Fixture f;CHECK(RemovedForDeferral(f));const auto old=f.result.tracking;
  const auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim);SetPresentationDeferral(f,mode);
  for(unsigned n=0;n<2;++n){f.Send(true,false,-.005f);
   CHECK(f.cancels==0&&f.starts==1&&f.submits==0&&f.result.completed==0&&f.result.blocksWeaponActions&&f.result.ownsLeftHand);
   CHECK(f.result.tracking.target&&MagazineTargetFresh(f.result.tracking,f.now));const auto& v=*f.result.tracking.target;
   CHECK(v.inputSequence==old.target->inputSequence&&v.observedNs==old.target->observedNs&&v.deadlineNs==old.target->deadlineNs);
   CHECK(v.weaponFromItemMeters.values==old.target->weaponFromItemMeters.values&&v.handClaim==old.target->handClaim);
   CHECK(f.result.tracking.inputEvidence.sequence==f.s.input.sequence&&f.result.tracking.reserve.sequence==old.reserve.sequence);
   CHECK(f.result.tracking.reserve.deadlineNs==old.reserve.deadlineNs&&f.result.tracking.family.deadlineNs==old.family.deadlineNs);
   CHECK(f.result.tracking.removalFrame&&f.result.tracking.removalFrame->weaponWorldMeters.values==old.removalFrame->weaponWorldMeters.values);
   CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld&&!f.result.interaction.physicallyRemoved&&!f.result.interaction.removalGrabbed);
   CHECK(!f.result.interaction.transaction.request&&!f.result.interaction.transaction.acknowledged&&!f.result.interaction.transaction.completed);
   CHECK(!f.result.interaction.seat&&!f.result.interaction.originalSeat&&!f.result.interaction.insertion.seat&&!f.result.interaction.insertion.captured);
   CHECK(f.result.interaction.insertion.haptic==ReloadInsertionHaptic::None&&!f.result.reloadHeld);
   CHECK(f.hands.Current(InteractionHand::Left)->deadlineNs==claim->deadlineNs&&f.reserve.loaded==27&&f.reserve.reserve==83);
  }
  f.reserveDeferred=f.keepDeferred=f.reserveCohortGap=false;f.Send(true,false,-.005f);
  CHECK(f.result.tracking.target&&f.result.tracking.target->inputSequence>old.target->inputSequence&&f.cancels==0);
 }
 return 0;
}
int DeferredPresentationExpiryAndSafety(){
 for(unsigned mode=0;mode<3;++mode){Fixture f;CHECK(RemovedForDeferral(f));const auto deadline=f.result.tracking.target->deadlineNs;SetPresentationDeferral(f,mode);
  while(f.now+20*Ms<deadline){f.Send(true,false,.01f);CHECK(f.result.tracking.target&&f.result.tracking.target->deadlineNs==deadline);}
  f.Send(true,false,.01f);CHECK(f.now>=deadline&&!f.result.tracking.target);
 }
 for(unsigned fault=0;fault<11;++fault){Fixture f;CHECK(RemovedForDeferral(f));f.reserveDeferred=true;
  if(fault==0)f.s.input.focused=false;
  if(fault==1)f.s.input.tracked[0]=false;
  if(fault==2)++f.s.nativeOwner.equipGeneration;
  if(fault==3){++f.s.input.owner.space;++f.s.trackingEpoch;}
  if(fault==4)f.s.meshes.reset();
  if(fault==5)f.familyFault=11;
  if(fault==6){const auto hand=f.hands.Current(InteractionHand::Left);CHECK(hand&&f.hands.Release(f.s.input,hand->token).accepted);}
  if(fault==7){const auto hand=f.hands.Current(InteractionHand::Left);CHECK(hand&&f.hands.Release(f.s.input,hand->token).accepted);
   const auto replacement=f.hands.Acquire(f.s.input,{f.s.input.owner,InteractionHand::Left,HandClaimKind::Mechanism,f.s.weapon,
    {{2002,1},f.s.input.sequence,f.s.input.deadlineNs,true},++f.intent,f.gun->token.id});CHECK(replacement.accepted&&replacement.claim);}
  if(fault==8){CHECK(f.gun&&f.hands.Release(f.s.input,f.gun->token).accepted);f.gun.reset();}
  if(fault==9)f.policy->Cancel(f.s.input,f.hands);
  f.Send(fault!=10,false,.01f);CHECK(!f.result.tracking.target&&f.submits==0&&f.result.completed==0);
 }
 // Native startup has not emitted a magazine pose and cannot invent one.
 for(unsigned mode=0;mode<3;++mode){Fixture f;f.Send();f.Send(false,true);CHECK(!f.result.tracking.target);
  SetPresentationDeferral(f,mode);f.Send();CHECK(!f.result.tracking.target&&f.submits==0);}
 return 0;
}
int DeferredSeatedPresentationPreservesSupportPhase(){
 for(unsigned mode=0;mode<3;++mode){Fixture f;CHECK(f.Insert());SetPresentationDeferral(f,mode);f.Send(true,false,.7f);
  CHECK(!f.hands.Current(InteractionHand::Left));
  CHECK(!f.result.ownsLeftHand&&!MagazineBlocksSupport(f.result,f.now)&&f.submits==1&&f.result.completed==0);
  CHECK(f.policy->ProbeState(f.now).pending&&f.reserve.loaded==27&&f.reserve.reserve==83);
 }
 for(unsigned mode=0;mode<3;++mode)for(bool grip:{false,true}){Fixture f;CHECK(f.Insert());f.Send(true,false,.7f);
  CHECK(f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Attached);
  const auto old=f.result.tracking;SetPresentationDeferral(f,mode);f.Send(grip);
  CHECK(f.result.tracking.target&&f.result.tracking.target->deadlineNs==old.target->deadlineNs);
  const auto hand=f.hands.Current(InteractionHand::Left);const bool owned=hand&&hand->token.kind==HandClaimKind::AmmoObject;
  CHECK(f.result.interaction.phase==DetachableMagazinePhase::AwaitingSeat&&MagazineBlocksSupport(f.result,f.now)==owned);
  CHECK(f.result.blocksWeaponActions&&f.result.ownsLeftHand==owned&&!f.result.interaction.seat&&!f.result.interaction.transaction.request);
  CHECK(!f.result.interaction.transaction.completed&&f.result.completed==0&&f.submits==1&&f.reserve.loaded==27&&f.reserve.reserve==83);
 }
 return 0;
}
int main(){if(DeferredPresentationPreservesOnlyIssuedEvidence()||DeferredPresentationExpiryAndSafety()||DeferredSeatedPresentationPreservesSupportPhase())return 1;
 if(ShortPulseRollsBackWithoutNativeCalls())return 1;CHECK(PairedLauncherTransitionRetiresOnlyRifleMagazine()==0);CHECK(RenderLifetimeKeepsGeometryButNeverNativeAuthority()==0);CHECK(MagazineFallbackObservationDoesNotChangeSelection()==0);CHECK(StartupWaitUsesOriginalUnseatTimeoutNotHeldGap()==0);CHECK(StartupPulseSurvivesOnlyOriginalDeadlineDuringObservationWait()==0);CHECK(TypedNativeObservationGap()==0);
 for(unsigned mode=0;mode<2;++mode){Fixture f;f.Send();f.Send(true);f.held=true;f.nativeLifetime=5*Ms;f.Send(true);
  CHECK(f.result.interaction.phase==DetachableMagazinePhase::Pulling);
  if(mode==0)f.held=false;else f.leaseHeld=false;f.Send(true);
  CHECK(f.cancels==1&&f.result.interaction.nativeFailureCheck==MagazineNativeFailureCheck::HeldCycle&&f.submits==0);
  const auto report=f.Report();CHECK(report.find("\"native_boundary\":{\"failure_check\":3")!=std::string::npos);
  CHECK(report.find("\"source_available\":1")!=std::string::npos&&report.find("\"gate_applied\":1")!=std::string::npos);
  CHECK(report.find(mode==0?"\"returned_lease\":{\"present\":0":"\"returned_lease\":{\"present\":1")!=std::string::npos);
  CHECK(f.reserve.loaded==27&&f.reserve.reserve==83);
 }

 {Fixture f;f.Send();f.Send(true);CHECK(f.starts==1);f.held=true;f.nativeLifetime=5*Ms;
  f.Send(true,false,.075f);CHECK(f.result.interaction.phase==DetachableMagazinePhase::Pulling);
  const auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim);const auto oldNativeDeadline=f.now+f.nativeLifetime;
  f.Send(true,false,.04f);CHECK(f.now>oldNativeDeadline&&f.cancels==0&&f.hands.Current(InteractionHand::Left)->token==claim->token);
  f.Send(true,false,.01f);CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld&&f.cancels==0);
  CHECK(f.result.tracking.target&&f.result.tracking.target->deadlineNs==f.s.input.deadlineNs);
  f.Send(false,false,.01f);CHECK(f.result.interaction.phase==DetachableMagazinePhase::WellEmpty&&!f.hands.Current(InteractionHand::Left));
  CHECK(f.starts==1&&f.cancels==0&&f.submits==0&&f.reserve.loaded==27&&f.reserve.reserve==83);
 }

 {Fixture f;f.Send();f.Send(true);f.held=true;f.Send(true,false,.075f);f.Send(true,false,.04f);f.Send(true,false,.01f);
  CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld);
  const auto expected=f.hands.Current(InteractionHand::Left);CHECK(expected);
  CHECK(f.hands.Release(f.s.input,expected->token).accepted);f.Send(true,false,.01f);
  CHECK(f.cancels==1&&f.submits==0&&f.result.interaction.claimFailure);
  const auto report=f.Report();CHECK(report.find("\"claim_failure\":{\"renew_reason\":19")!=std::string::npos);
  CHECK(report.find("\"expected_id\":"+std::to_string(expected->token.id))!=std::string::npos);
  CHECK(report.find("\"current_present\":false")!=std::string::npos);
  CHECK(f.reserve.loaded==27&&f.reserve.reserve==83);
 }
 CHECK(TypedCohortGapUsesOnlyOriginalRemovalOrSubmittedEvidence()==0);CHECK(ReserveDeferralCannotDelaySafetyLoss()==0);CHECK(ObserverDeferralAndStartOrigin()==0);CHECK(DeferredKeepAliveNeverRenews()==0);for(const auto fn:{MeasuredProfileMatchesAttached,ConsumerEjectHoldInsertReceipt,PreviousRendererContactReservesOriginal,
 UnseatRequiresRealNativeGate,PullContactAndNoAmmoCredit,WrongOrUnverifiedReceiptNeverCredits,CancelPendingRequiresDrainAndAttachedProof,
 IdleFullNoSyntheticHeldEject,EmptyMagazineNeverStartsPhysicalTakeover,EmptyFallbackPreservesNativeActionsOnlyOnFreshIdleEvidence,IdleBoundariesRequireNewNeutral,UnsupportedEquipDrainsWithoutDiscardingPending,
 NativeAttachmentMayArriveAfterRefill,DisabledUnsupportedAndFocus,PaletteIsolationAndCurrentGuard,RemovedMagazineHandUsesAuthoredGraspWithoutMovingRawContact,DistinctFamilyMappingAndExpiry,NativeStartRejectionAndUnknownRecovery,NativeTransferGapKeepsOnlySubmittedCycle,NativeTransferGapDoesNotExtendLeaseOrBypassSafety,PersistentCancelAllowsOnlyVerifiedRetirement,AcknowledgedHoldSurvivesOnlyBoundedReadGap,LongEmptyWellTimeoutRequiresNativeRetirement,OwnerReplacementRetiresOldMagazineWithoutAReceipt,RetainedOriginalReturnsWithoutRefillOrReserveCredit,OriginalReturnRejectsChangedNativeCountsAndOwners,OriginalReturnWaitsThroughReadGapsWithoutFreshnessInflation,OriginalTargetRejectsForgedMagazineFacts})if(const int n=fn())return n;
 for(const auto fn:{SubmittedMagazineReleasesSupportWithoutCompletingNativeReload,SupportRequiresFreshSeatedResultAndSafety,SeatedMagazineStaysAttachedThroughNativeContinuation,SeatedAttachmentStopsOnActualConsumerBoundaries,
  SeatedAttachmentReadGapNeverInflatesDeadline,SeatedAttachmentPackingChecksBothEyesAndExactProfile,
  FullIdleCountsCannotDivertUnfinishedPhysicalRetirement})if(const auto result=fn())return result;
 std::cout<<"BC2 magazine physical: 45 groups passed\n";return 0;}
