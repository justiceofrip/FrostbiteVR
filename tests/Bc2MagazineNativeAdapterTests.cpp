#include "Bc2MagazineNativeAdapter.h"
#include "Bc2MagazineEquipmentProfile.h"
#include "Test.h"
#include <cstdio>
#include "Bc2MagazineNativeFixture.h"
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
struct AdapterFixture {
 ReloadHoldInput native=test::Input();
 HandInteractionOwner physical{(std::uint64_t(native.identity.owner.weak)<<32)|native.identity.owner.soldier,1,17,3};
 HandInteractionKey key{0xe0000,17};
 WeaponEquipmentIdentity equipment{native.identity.owner.weapon,native.config.weaponData,unsigned(key.id),{}};
 MagazineNativeIdentityEvidence evidence;
 AdapterFixture(){std::memcpy(equipment.asset.data(),"XM8_sp_s",9);
  evidence=MagazineFamilyEvidence{{native.identity.owner,key,0xf0000,0xf1000,3},native.nowNs,native.leaseDeadlineNs,true};}
 void Aek(){
  native.config.assetName={};native.config.assetPath={};
  const auto& c=AekMagazineNativeProfile.configuration;
  std::memcpy(native.config.assetName.data(),c.assetName.data(),c.assetName.size());
  std::memcpy(native.config.assetPath.data(),c.assetPath.data(),c.assetPath.size());
  native.config.reloadTime=c.values.reloadTime;
  equipment.asset=native.config.assetName;key.id=native.identity.owner.weapon;
  evidence=SelectedCarriedWeaponEvidence{native.identity.owner,physical,key,equipment,0xf0000,0xf1000,0,0,1,native.nowNs,native.leaseDeadlineNs,true};
 }
 MagazineAdapterAssessment Assess(const MagazineNativeProfile& p=Xm8MagazineNativeProfile){
  return AssessMagazineNativeAdapter(p,evidence,native,physical,key,equipment,native.nowNs);
 }
};
int ExistingAliasRemainsExact(){
 AdapterFixture f;CHECK(f.Assess().ConfigurationReady());
 const auto& alias=std::get<MagazineFamilyEvidence>(f.evidence);
 CHECK(MagazineFamilyFresh(alias,f.native.identity.owner,f.physical,f.key,f.native.nowNs));
 ++f.equipment.persistence;CHECK(f.Assess().status==MagazineAdapterStatus::EquipmentMismatch);
 f=AdapterFixture{};std::get<MagazineFamilyEvidence>(f.evidence).binding.launcher=0;CHECK(f.Assess().status==MagazineAdapterStatus::IdentityMismatch);
 f=AdapterFixture{};f.native.config.reloadTime=3.2f;CHECK(f.Assess().status==MagazineAdapterStatus::ConfigurationMismatch);return 0;
}
int OrdinaryCandidateDoesNotFabricateALauncher(){
 AdapterFixture f;f.Aek();auto candidate=AekMagazineNativeProfile;candidate.cycleAdmission=MagazineCycleAdmission::Candidate;
 const auto r=f.Assess(candidate);
 CHECK(r.identityMatched&&r.status==MagazineAdapterStatus::CandidateDisabled&&!r.ConfigurationReady());
 CHECK(std::holds_alternative<SelectedCarriedWeaponEvidence>(f.evidence));
 Bc2MagazineReloadCycle cycle(true,candidate);CHECK(cycle.Phase()==ReloadRequestCyclePhase::Disabled);
 ReloadCycleControl control{f.native.identity,1,1,f.native.nowNs,f.native.leaseDeadlineNs,true};
 CHECK(!cycle.Start(control,{1,test::Owner(f.native.identity),ReloadOperation::UnseatMagazine,0,0},f.native.nowNs));
 CHECK(!cycle.Evaluate(f.native,true,true,1).tracked);return 0;
}
int IdentityRouteAndCurrentEquipmentCannotBeSubstituted(){
 AdapterFixture linked;CHECK(linked.Assess(AekMagazineNativeProfile).status==MagazineAdapterStatus::IdentityMismatch);
 AdapterFixture ordinary;ordinary.Aek();CHECK(ordinary.Assess().status==MagazineAdapterStatus::EquipmentMismatch);
 for(unsigned mode=0;mode<5;++mode){AdapterFixture f;f.Aek();switch(mode){
  case 0:++f.equipment.persistence;break;case 1:++f.equipment.data;break;case 2:f.equipment.asset[0]='X';break;
  case 3:++f.native.config.weaponData;break;case 4:++f.native.identity.owner.equipGeneration;break;}
  CHECK(!f.Assess(AekMagazineNativeProfile).ConfigurationReady());
  CHECK(f.Assess(AekMagazineNativeProfile).status!=MagazineAdapterStatus::CandidateDisabled);
 }return 0;
}
int BothObservationDeadlinesRemainOriginal(){
 AdapterFixture f;f.native.nowNs=f.native.leaseDeadlineNs;CHECK(f.Assess().status==MagazineAdapterStatus::IdentityMismatch);
 f=AdapterFixture{};std::get<MagazineFamilyEvidence>(f.evidence).deadlineNs+=10000000;f.native.leaseDeadlineNs=f.native.nowNs;
 CHECK(f.Assess().status==MagazineAdapterStatus::NativeObservationUnavailable);
 f=AdapterFixture{};f.native.verified=false;CHECK(f.Assess().status==MagazineAdapterStatus::NativeObservationUnavailable);
 f=AdapterFixture{};f.Aek();++f.physical.space;CHECK(f.Assess(AekMagazineNativeProfile).status==MagazineAdapterStatus::IdentityMismatch);return 0;
}
int ExistingCycleCompletesAndCancelsThroughSharedProfile(){
 {
  test::Simulation f(20,90,Xm8MagazineNativeProfile);
  CHECK(f.Arm()&&f.Unseat());const auto request=f.Request();CHECK(f.policy.Submit(request,f.input.nowNs));
  for(unsigned n=0;n<3;++n)CHECK(f.Transfer(n,10));CHECK(f.Settle());
  const auto ack=f.policy.TakeAcknowledgement(f.input.identity,f.control.cycle,f.input.nowNs);CHECK(ack);
  CHECK(ack->acknowledgement.loadedAfter==30&&ack->acknowledgement.reserveAfter==80);
 }
 test::Simulation cancelled;CHECK(cancelled.Arm());const auto counts=cancelled.input.branches;
 cancelled.policy.Cancel(ReloadRequestCycleFailure::Stopped);CHECK(!cancelled.Lease());
 CHECK(!cancelled.policy.Submit({},cancelled.input.nowNs));CHECK(!cancelled.Begin(0).tracked);
 for(unsigned n=0;n<3;++n)CHECK(cancelled.input.branches[n].loaded==counts[n].loaded&&cancelled.input.branches[n].reserve==counts[n].reserve);
 return 0;
}
int ZeroReserveDoesNotCreateRefillAuthority(){
 test::Simulation f(20,0);CHECK(f.Start());
 for(unsigned n=0;n<3;++n){CHECK(f.Tick());CHECK(!f.Begin(n).tracked);}
 CHECK(!f.Lease()&&!f.Unseat());CHECK(f.policy.Phase()==ReloadRequestCyclePhase::Arming);
 f.policy.Cancel();CHECK(f.policy.Phase()==ReloadRequestCyclePhase::Cancelled);return 0;
}
int SharedTimingUsesReviewedDataNotAnXm8Literal(){
 // Synthetic reviewed fixture tests the parameter path only; no production
 // candidate is promoted and no native execution or admission is claimed.
 auto profile=Xm8MagazineNativeProfile;profile.configuration.values.reloadTime=3.2f;
 test::Simulation f(20,90,profile);f.input.config.reloadTime=3.2f;
 for(auto& b:f.input.branches)b.phaseTimer=2.2f;f.tailTimer=.78f;
 CHECK(f.Arm()&&f.Unseat());CHECK(f.policy.Submit(f.Request(),f.input.nowNs));
 for(unsigned n=0;n<3;++n)CHECK(f.Transfer(n,10));CHECK(f.Settle());
 CHECK(f.policy.TakeAcknowledgement(f.input.identity,f.control.cycle,f.input.nowNs));
 test::Simulation original;for(auto& b:original.input.branches)b.phaseTimer=2.2f;
 CHECK(!original.Arm());return 0;
}
int InvalidAndCandidateProfilesCannotStart(){
 for(unsigned mode=0;mode<7;++mode){auto p=Xm8MagazineNativeProfile;
  switch(mode){case 0:p.configuration.admission=ReloadDescriptorAdmission::Candidate;break;
   case 1:p.cycleAdmission=MagazineCycleAdmission::Candidate;break;case 2:p.completionDeadlineNs=0;break;
   case 3:p.configuration.values.reloadThreshold=0;break;case 4:p.configuration.values.reloadTime=std::numeric_limits<float>::infinity();break;
   case 5:p.configuration=SpasReloadDescriptor;break;case 6:p.configuration.timing={};break;}
  Bc2MagazineReloadCycle cycle(true,p);CHECK(cycle.Phase()==ReloadRequestCyclePhase::Disabled);
 }return 0;
}

struct AekSimulation:test::Simulation {
 AekSimulation(int loaded=20,int reserve=90):Simulation(loaded,reserve,AekMagazineNativeProfile){
  AdapterFixture a;a.Aek();input.config=a.native.config;
  for(auto& b:input.branches)b.phaseTimer=2.2f;tailTimer=.78f;
 }
};
int FamilyReviewedOrdinaryConfigurationNeedsNoLauncher(){
 AdapterFixture f;f.Aek();CHECK(f.Assess(AekMagazineNativeProfile).ConfigurationReady());
 CHECK(std::holds_alternative<SelectedCarriedWeaponEvidence>(f.evidence));
 const auto* equipment=FindMagazineEquipment("AEK971_sp");CHECK(equipment&&!equipment->geometryVerified);
 CHECK(equipment->geometry==FindMagazineGeometry("AEK971_sp"));
 CHECK(equipment->Ready()==bool(equipment->geometry));
 CHECK(equipment->experimentalGeometry==bool(equipment->geometry));
 CHECK(!f.Assess(Xm8MagazineNativeProfile).ConfigurationReady());
 for(unsigned bad=0;bad<12;++bad){auto changed=f;
  switch(bad){case 0:changed.native.config.fireLogicType=4;break;case 1:changed.native.config.reloadType=0;break;
   case 2:changed.native.config.reloadTime=2.8f;break;case 3:changed.native.config.reloadThreshold=1;break;
   case 4:changed.native.config.reloadDelay=.1f;break;case 5:changed.native.config.postReloadTime=.1f;break;
   case 6:changed.native.config.boltTime=.1f;break;case 7:changed.native.config.holdBoltUntilFireRelease=true;break;
   case 8:changed.native.config.baseCapacity=36;break;case 9:changed.native.config.numberOfMagazines=5;break;
   case 10:changed.native.config.fireInputAction=1;break;case 11:changed.native.config.reloadInputAction=1;break;}
  CHECK(!changed.Assess(AekMagazineNativeProfile).ConfigurationReady());
 }return 0;
}
int FamilyCycleUsesCurrentCapacityAndExactReceipts(){
 for(int capacity:{30,36}){AekSimulation f;f.input.capacities.fill(capacity);
  CHECK(f.Arm()&&f.Unseat());const auto held=f.Lease();CHECK(held&&held->capacity==capacity&&held->allThreeHeld);
  auto request=f.Request();request.reservedUnits=unsigned(capacity-20);CHECK(f.policy.Submit(request,f.input.nowNs));
  for(unsigned branch=0;branch<3;++branch){CHECK(f.Transfer(branch,capacity-20));
   CHECK(!f.policy.TakeAcknowledgement(f.input.identity,f.control.cycle,f.input.nowNs));}
  CHECK(f.Settle());const auto ack=f.policy.TakeAcknowledgement(f.input.identity,f.control.cycle,f.input.nowNs);
  CHECK(ack&&ack->acknowledgement.loadedAfter==capacity&&ack->acknowledgement.reserveAfter==90-(capacity-20));
 }
 for(unsigned bad=0;bad<5;++bad){AekSimulation f;
  if(bad==0)f.input.capacities[2]=36;
  if(bad==1)f.input.context.reloadTimeMultiplier=.5f;
  if(bad==2)f.input.branches[2].nextState=11;
  if(bad==3)f.input.branches[1].flagsA8=8;
  if(bad==4)f.input.branches[2].reserve=89;
  CHECK(!f.Arm()&&!f.Lease()&&!f.Unseat());
 }
 AekSimulation missing;CHECK(missing.Arm()&&missing.Unseat());CHECK(missing.policy.Submit(missing.Request(),missing.input.nowNs));
 CHECK(missing.Transfer(0,10));CHECK(missing.Transfer(1,10));CHECK(missing.Transfer(2,10,false));
 CHECK(!missing.Settle()&&!missing.policy.TakeAcknowledgement(missing.input.identity,missing.control.cycle,missing.input.nowNs));
 return 0;
}
int FamilyDeadlineIsFiniteWithoutChangingNativeClock(){
 AekSimulation f;CHECK(f.Arm()&&f.Unseat());CHECK(f.policy.Submit(f.Request(),f.input.nowNs));
 const auto submitted=f.input.nowNs;const auto delta=f.input.context.deltaSeconds;
 for(unsigned n=0;n<77;++n)CHECK(f.Tick(50000000));
 CHECK(f.input.nowNs<submitted+AekMagazineNativeProfile.completionDeadlineNs);
 CHECK(!f.Tick(50000000));CHECK(f.policy.Phase()==ReloadRequestCyclePhase::Cancelled);
 CHECK(f.input.context.deltaSeconds==delta);for(const auto& b:f.input.branches)CHECK(b.loaded==20&&b.reserve==90);
 return 0;
}
struct TimingMemory {
 unsigned primary=0xd0000,reads=0,bad=99;bool unstable=false;
 static bool Read(void* ptr,std::uint32_t address,void* dst,std::size_t bytes){auto& m=*static_cast<TimingMemory*>(ptr);
  if(bytes!=4||address<m.primary)return false;const auto off=address-m.primary;
  for(unsigned n=0;n<AekReloadTiming.size();++n)if(AekReloadTiming[n].offset==off){auto word=AekReloadTiming[n].expected;
   if(n==m.bad&&(!m.unstable||m.reads>=AekReloadTiming.size()))word^=1;
   ++m.reads;std::memcpy(dst,&word,4);return true;}return false;
 }
 ReloadStateMemory Adapter(){return {this,Read,nullptr};}
};
int FamilyTimingDoubleReadsTheSharedAbiWords(){
 AdapterFixture f;f.Aek();TimingMemory memory;CHECK(AekMagazineNativeProfile.ReadTiming(memory.Adapter(),f.native.config)&&memory.reads==12);
 for(unsigned n=0;n<6;++n)for(bool unstable:{false,true}){TimingMemory changed;changed.bad=n;changed.unstable=unstable;
  CHECK(!AekMagazineNativeProfile.ReadTiming(changed.Adapter(),f.native.config));}
 TimingMemory candidateMemory;auto candidate=AekMagazineNativeProfile;candidate.configuration.admission=ReloadDescriptorAdmission::Candidate;
 CHECK(!candidate.ReadTiming(candidateMemory.Adapter(),f.native.config)&&!candidateMemory.reads);
 return 0;
}


int CompletionDurationRequiresExplicitBoundedProfile(){
 AekSimulation f;CHECK(f.Arm()&&f.Unseat());const auto request=f.Request();
 for(auto bound:{0ll,-1ll,10000000001ll}){ReloadMagazineCompletion completion(true);
  CHECK(!completion.Begin(request.request,request.heldLease,f.input.nowNs,f.input.nowNs+3900000000ll,bound));}
 ReloadMagazineCompletion original(true);CHECK(!original.Begin(request.request,request.heldLease,f.input.nowNs,f.input.nowNs+3900000000ll));
 CHECK(original.Begin(request.request,request.heldLease,f.input.nowNs,f.input.nowNs+3500000000ll));
 ReloadMagazineCompletion reviewed(true);CHECK(reviewed.Begin(request.request,request.heldLease,f.input.nowNs,f.input.nowNs+3900000000ll,3900000000ll));
 ReloadMagazineCompletion tooLong(true);CHECK(!tooLong.Begin(request.request,request.heldLease,f.input.nowNs,f.input.nowNs+4000000000ll,3900000000ll));
 return 0;
}

}
int main(){CHECK(!ExistingAliasRemainsExact());CHECK(!OrdinaryCandidateDoesNotFabricateALauncher());
 CHECK(!IdentityRouteAndCurrentEquipmentCannotBeSubstituted());CHECK(!BothObservationDeadlinesRemainOriginal());
 CHECK(!ExistingCycleCompletesAndCancelsThroughSharedProfile());CHECK(!ZeroReserveDoesNotCreateRefillAuthority());
 CHECK(!SharedTimingUsesReviewedDataNotAnXm8Literal());CHECK(!InvalidAndCandidateProfilesCannotStart());
 CHECK(!FamilyReviewedOrdinaryConfigurationNeedsNoLauncher());CHECK(!FamilyCycleUsesCurrentCapacityAndExactReceipts());
 CHECK(!FamilyDeadlineIsFiniteWithoutChangingNativeClock());CHECK(!FamilyTimingDoubleReadsTheSharedAbiWords());
 CHECK(!CompletionDurationRequiresExplicitBoundedProfile());
 std::puts("13 shared magazine native adapter groups passed; family review is separate from geometry and headset acceptance");return 0;}
