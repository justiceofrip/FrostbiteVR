#include "Bc2MagazineConsumerFixture.h"
#include <cstdio>

using namespace magazine_consumer_fixture;
namespace {
// Synthetic configuration data, deliberately never registered for gameplay.
// The real consumer/hand arbiter persists for the entire sequence; only native
// memory observations and acknowledgements are supplied by this CPU fixture.
struct Profile {
 MagazineNativeProfile native=Xm8MagazineNativeProfile;
 MagazineGeometryProfile geometry=Xm8MagazineGeometry();
 MagazineEquipmentProfile equipment{};
 Profile(){
  native.identityRoute=MagazineIdentityRoute::SelectedCarriedItem;
  geometry.asset="transition_fixture";geometry.mesh="fixture/transition/mesh";
  native.configuration.assetName=geometry.asset;native.configuration.assetPath="fixture/transition/config";
  native.configuration.values.baseCapacity=20;
  geometry.configurationPath=native.configuration.assetPath;
  geometry.meshKind=SelectedMeshKind::Unknown;geometry.rigFingerprint+=123;
  geometry.interaction.insertion.id+=123;
  equipment={NativeMagazineProfileId::ScopedXm8,&native,&geometry,true};
 }
};
struct Run:Fixture {
 unsigned retirementFault=0;
 std::optional<ReloadHoldIdentity> retiredIdentity;
 std::uint64_t retiredCycle=0;
 Run():Fixture(true,Xm8MagazineEquipment(),true){
  // A native start owns a new request cycle; cumulative counters are retained.
  api.start=[](void* p,const ReloadCycleControl& c,const ManualReloadRequest& r,const ReloadMagazineStartupPulse& pulse)noexcept{
   auto& f=*static_cast<Fixture*>(p);if(!pulse.ValidFor(c)||pulse.endNs<=f.now)return MagazineCycleStartResult::NotStarted;
   ++f.starts;f.cycle=c.cycle;f.unseat=r;f.gateSent=false;f.submitted.reset();f.ack.reset();
   return MagazineCycleStartResult::Started;
  };
  api.retire=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadCycleRetirement>{
   auto& f=*static_cast<Run*>(static_cast<Fixture*>(p));if(!f.allowRetire)return {};
   f.retiredIdentity=id;f.retiredCycle=cycle;
   ReloadCycleRetirement r{id,cycle,++f.event,f.now,f.now+200*Ms,true};
   if(f.retirementFault==1)++r.identity.owner.weapon;
   if(f.retirementFault==2)++r.cycle;
   if(f.retirementFault==3)r.verified=false;
   return r;
  };
  // Copying an API into a consumer is fixture setup, never per-transition repair.
  policy.emplace(true,api,AmmoSupplyConfig{InteractionHand::Left,1000,{1001,1},{0,0,0},.15f,200*Ms});
 }
 bool Begin(){const auto before=starts;Send();Send(false,true);if(starts!=before+1)return false;
  held=true;Send(false,true);return result.interaction.phase==DetachableMagazinePhase::WellEmpty;}
 bool Seat(){const auto before=submits;Send();s.bodyFromHand=Pose();Send(true,false,-.1f);
  for(float z:{-.075f,-.04f,0.f,.025f,.05f,.075f,.1f,.1f,.1f,.1f,.1f,.1f}){
   Send(true,false,z);if(submits==before+1)return true;
  }return false;
 }
 void Select(const MagazineEquipmentProfile& next,unsigned nativeItem,int loaded,int capacity,int remaining){
  profile=&next;++s.nativeOwner.equipGeneration;s.nativeOwner.weapon=nativeItem;
  ++s.input.owner.equipGeneration;s.weapon={nativeItem,s.input.owner.equipGeneration};
  reserve.identity.owner=s.nativeOwner;reserve.identity.firing={nativeItem+0x1000,nativeItem+0x2000,nativeItem+0x3000};
  reserve.identity.serverItem=nativeItem+0x4000;reserve.loaded=loaded;reserve.capacity=capacity;
  reserve.reserve=remaining;reserve.reloadInputReady=true;s.asset=next.geometry->asset;
  equipment={};equipment.weapon=nativeItem;equipment.data=nativeItem+0x5000;equipment.persistence=nativeItem+0x6000;
  std::memcpy(equipment.asset.data(),s.asset.data(),s.asset.size());
  // A new immutable selected-mesh observation, not mutation of retained evidence.
  meshes=std::make_shared<SelectedMeshesSnapshot>(*meshes);s.meshes=meshes;
  meshes->owner=s.nativeOwner;meshes->weaponData=equipment.data;
  auto& mesh=meshes->states[0].meshes[0];mesh.kind=next.geometry->meshKind;mesh.assetPath.fill(0);
  std::memcpy(mesh.assetPath.data(),next.geometry->mesh.data(),next.geometry->mesh.size());
  s.raw.owner=s.nativeOwner;s.raw.rigFingerprint=next.geometry->rigFingerprint;s.raw.nativeMagazineAttached=true;
  s.bodyFromHand=Pose(2);held=false;
 }
};
int ChangedProfileAfterInterruptedReload(){
 for(unsigned phase=0;phase<3;++phase){Profile other;Run f;CHECK(other.equipment.Ready());
  CHECK(f.Begin());
  if(phase>=1){f.Send();f.s.bodyFromHand=Pose();f.Send(true,false,-.1f);CHECK(f.result.ownsLeftHand);}
  if(phase==2)CHECK(f.Seat());
  const auto oldCycle=f.cycle;const auto oldNative=f.reserve.identity;const auto oldStarts=f.starts,oldSubmits=f.submits;
  f.Select(other.equipment,0x140000,13,20,67);
  f.Send();CHECK(f.cancels==1&&f.policy->ProbeState(f.now).retiring);
  for(unsigned n=0;n<5;++n)f.Send(false,true);
  CHECK(f.starts==oldStarts&&f.submits==oldSubmits&&f.policy->BlocksEquipment());
  f.allowRetire=true;
  for(unsigned n=0;n<6;++n)f.Send();
  if(f.policy->ProbeState(f.now).retiring)std::cerr<<"Profile transition stage "<<phase<<": "<<f.Report()<<'\n';
  CHECK(!f.policy->ProbeState(f.now).retiring&&!f.policy->BlocksEquipment());
  CHECK(f.retiredIdentity==oldNative&&f.retiredCycle==oldCycle);
  CHECK(!f.hands.Current(InteractionHand::Left)&&f.reserve.loaded==13&&f.reserve.reserve==67);
  CHECK(f.policy->ProbeState(f.now).completed==0);
  CHECK(f.Begin()&&f.cycle>oldCycle&&f.Seat());
  f.Complete();f.Send();f.Send();f.Send();
  CHECK(f.policy->ProbeState(f.now).completed==1&&f.reserve.loaded==20&&f.reserve.reserve==60);
  CHECK(!f.policy->BlocksEquipment()&&f.starts==2&&f.submits==oldSubmits+1);
 }
 return 0;
}
int CurrentAttachmentAndOldRetirementAreBothRequired(){
 for(unsigned bad=0;bad<7;++bad){Profile other;Run f;CHECK(f.Begin()&&f.Seat());
  const auto oldRig=f.s.raw.rigFingerprint;
  f.Select(other.equipment,0x140000,13,20,67);f.allowRetire=true;
  if(bad<3)f.retirementFault=bad+1;
  if(bad==3)f.s.raw.rigFingerprint=oldRig;
  if(bad==4)f.s.raw.nativeMagazineAttached=false;
  if(bad==5)f.s.raw.owner.weapon++;
  if(bad==6)f.s.input.focused=false;
  for(unsigned n=0;n<6;++n)f.Send();
  CHECK(f.policy->ProbeState(f.now).retiring&&f.policy->ProbeState(f.now).pending);
  CHECK(f.starts==1&&f.submits==1&&f.policy->ProbeState(f.now).completed==0);
  CHECK(!f.result.tracking.target&&!f.hands.Current(InteractionHand::Left));
  CHECK(f.reserve.loaded==13&&f.reserve.reserve==67);
  f.retirementFault=0;f.s.raw.rigFingerprint=other.geometry.rigFingerprint;
  f.s.raw.nativeMagazineAttached=true;f.s.raw.owner=f.s.nativeOwner;f.s.input.focused=true;
  // An invalid receipt is not replaced until its ORIGINAL lease expires.
  for(unsigned n=0;n<16;++n)f.Send();
  CHECK(!f.policy->ProbeState(f.now).retiring&&!f.policy->ProbeState(f.now).pending);
  CHECK(f.Begin()&&f.Seat());f.Complete();f.Send();f.Send();f.Send();
  CHECK(f.policy->ProbeState(f.now).completed==1&&f.reserve.loaded==20&&f.reserve.reserve==60);
 }return 0;
}
int PersistentAlternatingProfilesAndFocusRecovery(){
 Profile other;Run f;f.allowRetire=true;
 const auto* originalConsumer=&*f.policy;
 std::uint64_t previousCycle=0,previousRequest=0;
 // Reuse the same transaction IDs, native-cycle counters, hand arbiter, supply
 // and consumer through repeated profile/ownership changes. No Reset or emplace.
 for(unsigned round=0;round<12;++round){
  const bool alternate=(round%2)==0;const auto& p=alternate?other.equipment:Xm8MagazineEquipment();
  const int capacity=alternate?20:30,loaded=capacity-int(1+round%7);
  f.Select(p,alternate?0x140000:0x40000,loaded,capacity,100-int(round*3));
  f.Send();f.s.input.focused=false;f.Send();f.s.input.focused=true;f.Send();
  const int total=f.reserve.loaded+f.reserve.reserve;
  CHECK(f.Begin()&&f.Seat());CHECK(f.cycle>previousCycle&&f.submitted->request.id>previousRequest);
  previousCycle=f.cycle;previousRequest=f.submitted->request.id;
  f.Complete();f.Send();f.Send();f.Send();
  CHECK(&*f.policy==originalConsumer&&f.policy->ProbeState(f.now).completed==round+1);
  CHECK(!f.policy->BlocksEquipment()&&!f.hands.Current(InteractionHand::Left));
  CHECK(f.reserve.loaded==capacity&&f.reserve.loaded+f.reserve.reserve==total);
 }
 CHECK(f.starts==12&&f.submits==12);return 0;
}
}
int main(){CHECK(ChangedProfileAfterInterruptedReload()==0);
 CHECK(CurrentAttachmentAndOldRetirementAreBothRequired()==0);
 CHECK(PersistentAlternatingProfilesAndFocusRecovery()==0);
 std::puts("Persistent reload profile-transition sequences passed (mock native API; no game/headset).");return 0;}
