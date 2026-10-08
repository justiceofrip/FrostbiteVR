#include "Bc2MagazineConsumerFixture.h"
#include "Bc2SightContact.h"
#include <bit>
#include <cstdio>
using namespace magazine_consumer_fixture;
namespace {
// Deliberately unregistered fixture data: this proves policy reuse, not any
// AEK/native feature admission. Different owner route, geometry and capacity.
const MagazineNativeProfile& SecondNative(){static constexpr std::array<ReloadTimingWord,2> timing{{
 {0x10,std::bit_cast<std::uint32_t>(.6f)},{0x18,std::bit_cast<std::uint32_t>(3.6f)}}};
 static const MagazineNativeProfile p{{"fixture_rifle","fixture/native/rifle",
  {2,1,8,29,40,5,0,3.6f,.6f,0,0,0,false,false},timing,ReloadDescriptorAdmission::ReviewedNative},
  MagazineIdentityRoute::SelectedCarriedItem,MagazineCycleAdmission::ReviewedReload11Transfer12,4500000000ll};return p;}
RigSnapshot Rig(const MagazineGeometryProfile& p){RigSnapshot r;
 r.names={"root",std::string(p.bones.weapon),std::string(p.bones.wrist),"untouched",std::string(p.bones.magazine)};r.parents={-1,0,0,0,1};r.weaponBone=1;
 for(unsigned n=0;n<15;++n){r.names.push_back(std::string(p.bones.fingers[n]));r.parents.push_back(n%3?int(r.names.size()-2):2);}
 r.inverseBind.assign(r.names.size(),Identity());r.evaluatedWorld=r.inverseBind;r.nativeEvaluated.resize(r.names.size());
 for(auto& bytes:r.nativeEvaluated)bytes.fill(std::byte{0x71});return r;}
const MagazineGeometryProfile& SecondGeometry(){static const auto p=[] {
 auto g=Xm8MagazineGeometry();g.asset="fixture_rifle";g.mesh="fixture/mesh/rifle";g.meshKind=SelectedMeshKind::Unknown;
 g.configurationPath=SecondNative().configuration.assetPath;
 g.bones.weapon="FixtureRoot";g.bones.magazine="FixtureMagazine";g.bones.wrist="FixtureWrist";
 g.bones.fingers={"t0","t1","t2","i0","i1","i2","m0","m1","m2","r0","r1","r2","p0","p1","p2"};
 auto& c=g.interaction;auto& q=c.insertion;q.id=0x46495854555245ull;q.revision=7;
 q.itemFromHand=q.itemFromInsertion=q.weaponFromEntry=Identity();q.weaponFromEntry.values[0]={0,0,-1,0};q.weaponFromEntry.values[2]={1,0,0,0};
 q.weaponFromEntry.values[3]={.2f,-.1f,.3f,1};q.travelMeters=.14f;c.pullMeters=.12f;c.maxPullStepMeters=.05f;c.ackTimeoutNs=5000000000ll;
 g.attachedItem=Multiply(TravelPose(q,q.travelMeters),q.weaponFromEntry);
 for(auto& f:g.wristFromFinger)f=Pose(.01f,.02f,.03f);
 const auto rig=Rig(g);g.rigFingerprint=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);return g;}();return p;}
const MagazineEquipmentProfile& Second(){static const MagazineEquipmentProfile p{NativeMagazineProfileId::ScopedXm8,&SecondNative(),&SecondGeometry(),true};return p;}
struct SecondFixture:Fixture {SecondFixture():Fixture(true,Second()){reserve.loaded=33;reserve.capacity=40;reserve.reserve=97;}};
int ActualReplacementAndPendingPresentation(){SecondFixture f;CHECK(Second().Ready()&&f.Insert());
 CHECK(f.starts==1&&f.submits==1&&f.submitted->reservedUnits==7&&f.reserve.loaded==33&&f.reserve.reserve==97);
 CHECK(f.result.tracking.family.binding.launcher==0&&f.result.tracking.family.carried&&f.s.weapon.id==f.s.nativeOwner.weapon);
 CHECK((f.submitted->reservation.claim.item!=f.s.weapon));
 auto rig=Rig(SecondGeometry());rig.identity.soldier=f.s.nativeOwner.soldier;rig.identity.weak=f.s.nativeOwner.weak;
 rig.evaluatedWorld[4]=SecondGeometry().attachedItem;
 const auto binding=magazine_presentation_detail::Derive(rig,SecondGeometry());CHECK(binding&&binding->geometry==&SecondGeometry());
 CHECK(!BindMagazinePresentation(rig,SecondGeometry().asset)); // Fixture is never registered as a production gun.
 const auto before=rig.nativeEvaluated;
 const auto raw=BuildMagazineRawContact(f.result.tracking,rig,f.s.asset,Pose(),Pose(),1,f.now);CHECK(raw.valid&&raw.nativeMagazineAttached);
 for(unsigned n=0;n<180;++n){f.Send(false,false,.7f);CHECK(f.result.completed==0&&f.result.tracking.target);
  CHECK(f.result.tracking.target->role==MagazinePropRole::Attached&&!f.result.tracking.target->handTarget);
  CHECK((f.result.tracking.target->profile==HandInteractionKey{SecondGeometry().interaction.insertion.id,7}));
  const auto plan=BuildMagazinePresentation(rig,*binding,f.result.tracking,Pose(1,2,3),1,f.now);
  CHECK(plan.binding&&plan.writes.size()==1&&plan.writes[0].index==4&&!plan.wristTarget);
  CHECK(Same(plan.writes[0].transform,Multiply(SecondGeometry().attachedItem,Pose(1,2,3))));
  auto wrong=*binding;wrong.geometry=&Xm8MagazineGeometry();CHECK(!BuildMagazinePresentation(rig,wrong,f.result.tracking,Pose(),1,f.now).binding);
  CHECK(rig.nativeEvaluated==before);
 }
 f.Complete();f.Send();CHECK(f.result.completed==1&&f.reserve.loaded==40&&f.reserve.reserve==90);
 f.allowRetire=true;f.Send();f.Send();CHECK(!f.policy->BlocksEquipment());return 0;}
bool ReachOriginalReturn(SecondFixture& f){f.Send();f.Send(true);if(f.starts!=1||!f.result.interaction.original)return false;
 f.held=true;f.reserve.reloadInputReady=false;
 for(float z:{.115f,.09f,.065f,.04f,.015f,-.01f,-.035f,-.035f,0.f,.025f,.05f,.075f,.1f,.12f,.14f,.14f,.14f,.14f,.14f}){
  f.Send(true,false,z);if(f.result.interaction.originalSeat)return true;}return false;
}
int ActualOriginalReturnsNoRefill(){SecondFixture f;CHECK(ReachOriginalReturn(f));
 CHECK(f.result.interaction.originalSeat&&f.result.interaction.phase==DetachableMagazinePhase::AwaitingOriginalReturn);
 CHECK(f.result.interaction.original->rounds==33&&f.result.interaction.original->capacity==40&&f.submits==0);
 CHECK(f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Attached&&MagazineTargetFresh(f.result.tracking,f.now));
 f.allowRetire=true;f.Send();CHECK(f.policy->BlocksEquipment());f.reserve.reloadInputReady=true;f.Send();
 CHECK(!f.policy->BlocksEquipment()&&f.policy->ProbeState(f.now).originalReturns==1&&f.reserve.loaded==33&&f.reserve.reserve==97);return 0;}
int RetiringOriginalRejectsSamePointerReplacement(){for(unsigned changed=0;changed<2;++changed){SecondFixture f;CHECK(ReachOriginalReturn(f));
 const auto old=f.result.tracking;f.allowRetire=true;f.reserve.reloadInputReady=true;
 if(changed==0){++f.equipment.data;f.meshes->weaponData=f.equipment.data;}else ++f.equipment.persistence;
 f.Send();f.Send();CHECK(f.policy->ProbeState(f.now).originalReturns==0&&!f.policy->ProbeState(f.now).originalReceipt);
 CHECK(!MagazineTargetRetained(old,f.result.tracking,f.now)&&f.submits==0&&f.reserve.loaded==33&&f.reserve.reserve==97);
 }return 0;}
int SameAddressNewEquipmentCancelsHeldMagazine(){SecondFixture f;CHECK(f.Eject());f.Send();f.s.bodyFromHand=Pose();f.Send(true,false,-.1f);
 CHECK(f.policy->BlocksEquipment()&&f.hands.Current(InteractionHand::Left));const auto saved=f.result.tracking;
 ++f.equipment.data;f.meshes->weaponData=f.equipment.data;f.Send(true,false,-.075f);
 CHECK(f.cancels==1&&f.submits==0&&!f.result.tracking.target&&f.policy->BlocksEquipment());
 CHECK(!MagazineTargetRetained(saved,f.result.tracking,f.now));CHECK(f.reserve.loaded==33&&f.reserve.reserve==97);return 0;}
int IdentityMeshAndProfileGuards(){SecondFixture f;f.Send();CHECK(MagazineTrackingFresh(f.result.tracking,f.now));
 auto t=f.result.tracking;const auto now=f.now;
 for(unsigned bad=0;bad<9;++bad){auto wrong=t;
  if(bad==0)wrong.family.binding.launcher=0xc0000;
  if(bad==1)wrong.family.carried.reset();
  if(bad==2)++wrong.family.binding.equipment.persistence;
  if(bad==3)wrong.family.deadlineNs=now;
  if(bad==4)wrong.family.binding.profile=&Xm8MagazineEquipment();
  if(bad==5)++wrong.family.binding.weapon.id;
  if(bad==6)++wrong.family.carried->physical.space;
  if(bad==7)++wrong.family.carried->equipment.data;
  if(bad==8)wrong.family.binding.profile=FindMagazineEquipment("AEK971_sp");
  CHECK(!MagazineTrackingFresh(wrong,now));
 }
 for(unsigned bad=0;bad<5;++bad){auto selected=*f.meshes;
  if(bad==0)selected.stateCount=2;
  if(bad==1){selected.states[0].count=2;selected.states[0].meshes[1]=selected.states[0].meshes[0];}
  if(bad==2)selected.deadlineNs=now;
  if(bad==3)++selected.owner.equipGeneration;
  if(bad==4)selected.states[0].meshes[0].assetPath.fill('x');
  CHECK(!MagazineSelected(selected,f.s.nativeOwner,f.s.asset,Second(),now));
 }
 ++f.meshes->weaponData;CHECK(!MagazineTrackingFresh(t,now));return 0;}
int CandidateNeverSuppressesOrdinaryReload(){auto native=SecondNative();native.configuration.assetName="unregistered_candidate";
 native.cycleAdmission=MagazineCycleAdmission::Candidate;
 const MagazineEquipmentProfile candidate{NativeMagazineProfileId::ScopedXm8,&native,nullptr,false};const auto p=&candidate;
 CHECK(!p->Ready()&&!p->geometry&&!FindMagazineEquipment(native.configuration.assetName));
 ActionOutput out;out.held=out.pressed=Fire|Reload;ApplyMagazinePhysicalActions(out,native.configuration.assetName,{});CHECK(out.held==(Fire|Reload)&&out.pressed==(Fire|Reload));
 SecondFixture f;f.profile=p;f.s.asset=native.configuration.assetName;f.Sync();CHECK(!f.result.blocksWeaponActions&&!f.result.tracking.enabled&&f.starts==0);
 return 0;}
int CancelFocusAndWrongRawCannotSeat(){for(unsigned bad=0;bad<3;++bad){SecondFixture f;CHECK(f.Eject());f.Send();f.s.bodyFromHand=Pose();f.Send(true,false,-.1f);
 if(bad==0)f.s.cancel=true;if(bad==1)f.s.input.focused=false;if(bad==2)++f.s.raw.rigFingerprint;
 for(float z:{-.075f,-.04f,0.f,.025f,.05f,.075f,.1f,.12f,.14f,.14f,.14f,.14f})f.Send(true,false,z);
 CHECK(f.submits==0&&f.result.completed==0&&f.reserve.loaded==33&&f.reserve.reserve==97);
 if(bad<2)CHECK(f.cancels==1&&!f.result.tracking.target);
 }return 0;}
int FullAndZeroReserveGateUsesOrdinaryIdentity(){for(const auto counts:{std::pair{40,97},std::pair{33,0}}){SecondFixture f;f.reserve.loaded=counts.first;f.reserve.reserve=counts.second;f.reserve.allThreeIdle=true;f.Send();
 Bc2MagazineDetachGate gate;MagazineDetachGateSample s{f.s.family,f.reserve,f.s.input,*f.gun,f.meshes,1,0x310000,true};
 CHECK(gate.Begin(s)&&gate.Original()->rounds==unsigned(counts.first)&&gate.Original()->capacity==40);
 CHECK((gate.Original()->profile==HandInteractionKey{SecondGeometry().interaction.insertion.id,7}));
 const auto demand=gate.Demand(s);CHECK(demand);std::array<std::byte,InputBytes> cache{};HolsterInputOverride patch;
 HolsterInputOwner owner{&s,[](void* context,const HolsterSuppressionRequest& r)noexcept{const auto& v=*static_cast<MagazineDetachGateSample*>(context);
  return r.owner==v.reserve.identity.owner&&r.cache==v.cache&&r.nativeTick==v.nativeTick;}};
 CHECK(patch.Apply(cache,*demand,owner));const auto receipt=patch.Commit();CHECK(receipt&&gate.Commit(s,*receipt,s.input.nowNs));
 CHECK(gate.Authorization()&&gate.Authorization()->original.profile==gate.Original()->profile);
 auto changed=s;changed.family.binding.profile=&Xm8MagazineEquipment();CHECK(!gate.Demand(changed));
 changed=s;++changed.family.binding.equipment.data;changed.family.carried->equipment=changed.family.binding.equipment;CHECK(!gate.Demand(changed));
 s.input.nowNs=s.input.deadlineNs;CHECK(!gate.Demand(s));CHECK(f.starts==0&&f.submits==0&&f.reserve.loaded==counts.first&&f.reserve.reserve==counts.second);
 }return 0;}
struct DetachedFixture:SecondFixture {
 Bc2MagazineDetached detached{true};MagazinePhysicalResult detachedResult{};
 std::optional<MagazineDetachPairReceipt> pair;std::array<std::byte,InputBytes> cache{};std::uint64_t tick=0;
 DetachedFixture(int loaded=40,int remaining=97){reserve.loaded=loaded;reserve.reserve=remaining;reserve.allThreeIdle=true;}
 void Step(bool grip=false,float z=-100.f){Send(grip,false,z);++tick;
  detachedResult=detached.Prepare(s,reserve,tick,0x310000,hands);const auto request=detached.Demand();std::optional<HolsterSuppressionReceipt> receipt;
  if(request){HolsterInputOverride patch;HolsterInputOwner current{this,[](void* ptr,const HolsterSuppressionRequest& r)noexcept{
    const auto& f=*static_cast<DetachedFixture*>(ptr);return r.owner==f.s.nativeOwner&&r.nativeTick==f.tick&&r.cache==0x310000;}};
   if(patch.Apply(cache,*request,current))receipt=patch.Commit();}
  detachedResult=detached.Commit(receipt,pair,reserve,hands,intent,now,now);
  if(detachedResult.tracking.detach&&detachedResult.tracking.target&&detachedResult.tracking.target->role==MagazinePropRole::Attached){
   const auto& auth=*detachedResult.tracking.detach;pair=MagazineDetachPackedPair(auth,auth,true,tick,3,true,now);}
 }
};
int ActualDetachedReturnWithAlternateGeometry(){for(const auto counts:{std::pair{40,97},std::pair{33,0}}){DetachedFixture f(counts.first,counts.second);
 f.Step();f.Step(true);CHECK(f.detached.Grabs()==1&&f.detachedResult.tracking.target&&f.detachedResult.tracking.target->role==MagazinePropRole::Removed);
 const auto original=f.detached.Original();
 for(float z:{.115f,.09f,.065f,.04f,.015f,-.01f,-.035f,-.035f,0.f,.025f,.05f,.075f,.1f,.12f,.14f,.14f,.14f,.14f,.14f,.14f})f.Step(true,z);
 CHECK(f.detached.Returned()==1&&!f.detached.BlocksActions()&&!f.hands.Current(InteractionHand::Left));
 CHECK(f.starts==0&&f.submits==0&&f.reserve.loaded==counts.first&&f.reserve.reserve==counts.second);
 CHECK(original&&original->profile.id==SecondGeometry().interaction.insertion.id&&original->rounds==unsigned(counts.first));
 }return 0;}
int IdleRecognitionRequiresNeutralForNewEquipment(){DetachedFixture f;f.Step();
 ++f.equipment.data;f.meshes->weaponData=f.equipment.data;f.Step(true);CHECK(f.detached.Grabs()==0&&!f.detached.BlocksActions());
 f.Step();f.Step(true);CHECK(f.detached.Grabs()==1&&f.detached.BlocksActions());return 0;}

void SelectFixtureEquipment(Fixture& f,const MagazineEquipmentProfile& p,int loaded,int capacity){
 f.profile=&p;f.s.asset=p.geometry->asset;f.s.raw.rigFingerprint=p.geometry->rigFingerprint;
 ++f.s.nativeOwner.equipGeneration;f.reserve.identity.owner=f.s.nativeOwner;f.s.raw.owner=f.s.nativeOwner;f.meshes->owner=f.s.nativeOwner;
 ++f.s.input.owner.equipGeneration;f.s.weapon={p.native->identityRoute==MagazineIdentityRoute::SelectedCarriedItem?f.s.nativeOwner.weapon:0xb0000,f.s.input.owner.equipGeneration};
 f.reserve.loaded=loaded;f.reserve.capacity=capacity;f.reserve.reserve=97;f.reserve.reloadInputReady=true;
 auto& mesh=f.meshes->states[0].meshes[0];mesh.kind=p.geometry->meshKind;mesh.assetPath={};std::memcpy(mesh.assetPath.data(),p.geometry->mesh.data(),p.geometry->mesh.size());
 f.equipment={};f.meshes->weaponData=0;
 if(p.native->identityRoute==MagazineIdentityRoute::SelectedCarriedItem){f.equipment.weapon=f.s.nativeOwner.weapon;f.equipment.data=0x210000;f.equipment.persistence=0x220000;
  std::memcpy(f.equipment.asset.data(),f.s.asset.data(),f.s.asset.size());f.meshes->weaponData=f.equipment.data;}
 f.hands.Reset();f.gun.reset();f.held=f.gateSent=f.allowRetire=false;f.submitted.reset();f.ack.reset();f.unseat.reset();f.s.bodyFromHand=Pose(2);
}
int OneConsumerChangesGeometryWithoutRecyclingRequestsOrSeats(){Fixture f;std::uint64_t lastRequest=0,lastSeat=0,lastCycle=0;
 for(const auto* profile:{&Xm8MagazineEquipment(),&Second(),&Xm8MagazineEquipment()}){
  SelectFixtureEquipment(f,*profile,profile==&Second()?33:27,profile==&Second()?40:30);
  const auto priorStarts=f.starts,priorSubmits=f.submits;f.Send();f.Send(false,true);CHECK(f.starts==priorStarts+1&&f.unseat);
  CHECK(f.unseat->id>lastRequest&&f.cycle>lastCycle);lastRequest=f.unseat->id;lastCycle=f.cycle;
  f.held=true;f.Send(false,true);CHECK(f.result.interaction.phase==DetachableMagazinePhase::WellEmpty);f.Send();
  f.s.bodyFromHand=Pose();f.Send(true,false,-.1f);
  for(float z:{-.075f,-.04f,0.f,.025f,.05f,.075f,.1f,.12f,.14f,.14f,.14f,.14f,.14f,.14f}){
   f.Send(true,false,z);if(f.submits>priorSubmits)break;}
  CHECK(f.submits==priorSubmits+1&&f.submitted&&f.submitted->request.id>lastRequest&&f.submitted->reservation.seat>lastSeat);
  lastRequest=f.submitted->request.id;lastSeat=f.submitted->reservation.seat;
  f.Complete();f.Send();f.allowRetire=true;f.Send();f.Send();CHECK(!f.policy->BlocksEquipment()&&f.reserve.loaded==f.reserve.capacity);
 }
 CHECK(f.starts==3&&f.submits==3&&f.policy->ProbeState(f.now).completed==3);return 0;}
int PortableReconfigurationCannotAbandonPendingWork(){
 const auto config=Xm8MagazineConfig();DetachableMagazine magazine(config);CHECK(magazine.Reconfigure(SecondGeometry().interaction));
 auto invalid=SecondGeometry().interaction;invalid.insertion.travelMeters=0;CHECK(!magazine.Reconfigure(invalid));
 ManualReloadConfig c;c.stepCount=1;c.steps[0]={ReloadOperation::SeatMagazine,1};c.maxSampleGapNs=100000000;c.ackTimeoutNs=1000000000;c.transactionTimeoutNs=5000000000ll;
 ManualReload manual(c);ManualReloadSample sample;sample.owner={1,2,3,4,5};sample.sequence=1;sample.nowNs=1000000000;
 sample.focused=sample.tracked=sample.bindingsVerified=sample.neutral=true;manual.Update(sample);
 ++sample.sequence;sample.nowNs+=1000000;sample.neutral=false;sample.gesture={1,ReloadOperation::SeatMagazine};const auto result=manual.Update(sample);CHECK(result.request);
 CHECK(!manual.Reconfigure(c));sample.acknowledgement={result.request->id,sample.owner,ReloadOperation::SeatMagazine,ReloadAcknowledgement::Applied};
 ++sample.sequence;sample.nowNs+=1000000;CHECK(manual.Update(sample).completed);CHECK(!manual.Reconfigure(c));
 manual.Reset();CHECK(manual.Reconfigure(c));sample.acknowledgement={};sample.neutral=true;sample.gesture={};++sample.sequence;sample.nowNs+=1000000;manual.Update(sample);
 sample.neutral=false;sample.gesture={2,ReloadOperation::SeatMagazine};++sample.sequence;sample.nowNs+=1000000;
 const auto second=manual.Update(sample);CHECK(second.request&&second.request->id>result.request->id);return 0;}

}
int main(){for(auto fn:{ActualReplacementAndPendingPresentation,ActualOriginalReturnsNoRefill,RetiringOriginalRejectsSamePointerReplacement,SameAddressNewEquipmentCancelsHeldMagazine,
 IdentityMeshAndProfileGuards,CandidateNeverSuppressesOrdinaryReload,CancelFocusAndWrongRawCannotSeat,FullAndZeroReserveGateUsesOrdinaryIdentity,ActualDetachedReturnWithAlternateGeometry,IdleRecognitionRequiresNeutralForNewEquipment,OneConsumerChangesGeometryWithoutRecyclingRequestsOrSeats,PortableReconfigurationCannotAbandonPendingWork})if(const auto n=fn())return n;
 std::puts("12 shared magazine consumer profile groups passed; synthetic ordinary rifle has no production registration/native acceptance");}
