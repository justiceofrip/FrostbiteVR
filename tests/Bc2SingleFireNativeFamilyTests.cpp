#include "Bc2MagazineEmptyControl.h"
#include "Bc2MagazineReloadCycle.h"
#include "Bc2MagazineNativeFixture.h"
#include "singlefire243/ReviewedFixture.h"
#include "fvr/interaction/WeaponActionGate.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace fvr::bc2;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
namespace {
ReloadObservedConfig Config(const MagazineNativeProfile& p){auto c=test::Input().config;const auto& v=p.configuration.values;
 std::memset(c.assetName.data(),0,c.assetName.size());std::memset(c.assetPath.data(),0,c.assetPath.size());
 std::memcpy(c.assetName.data(),p.configuration.assetName.data(),p.configuration.assetName.size());
 std::memcpy(c.assetPath.data(),p.configuration.assetPath.data(),p.configuration.assetPath.size());
 c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
 c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;
 c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
 c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}
MagazineEmptyStep Step(const ReloadHoldInput& i,NativeMagazineProfileId id,unsigned branch){MagazineEmptyStep s;
 s.identity=i.identity;s.profile=id;s.ownerRevision=8;s.sourceSequence=77;s.observedNs=i.nowNs;s.deadlineNs=i.leaseDeadlineNs;
 s.branch=branch;s.before=i.branches[branch];s.before.currentState=s.before.nextState=2;s.before.phaseTimer=0;s.before.loaded=0;return s;
}
void EmptyAndReceipts(const MagazineNativeRegistration& row){auto i=test::Input();i.config=Config(*row.profile);
 MagazineEmptyControlReceipts receipts;MagazineEmptyByteOverride patch;patch.applied=patch.restored=true;
 for(unsigned n=0;n<3;++n){const auto s=Step(i,row.id,n);
  CHECK(ManualEmptyFamilyConfig(ReloadNativeFamily::Xm8Magazine,row.id,i.config));
  CHECK(MagazineEmptyStepEligible(s,i.config,ReloadRequestCyclePhase::Idle,i.context,i.nowNs+1));
  auto deliberate=i.context;deliberate.inputFlags=4;deliberate.reloadRequested=true;
  CHECK(!MagazineEmptyStepEligible(s,i.config,ReloadRequestCyclePhase::Arming,deliberate,i.nowNs+1));
  CHECK(!receipts.EmptyDeadline(i.identity,row.id,8,s.before.reserve,i.nowNs+1));
  CHECK(receipts.Observe(s,s.before,patch,true,i.nowNs+1));
 }
 const auto s=Step(i,row.id,0);CHECK(receipts.EmptyDeadline(i.identity,row.id,8,s.before.reserve,i.nowNs+2));
 CHECK(!receipts.EmptyDeadline(i.identity,row.id,8,s.before.reserve,i.leaseDeadlineNs));
 auto wrong=s.identity;++wrong.owner.equipGeneration;CHECK(!receipts.EmptyDeadline(wrong,row.id,8,s.before.reserve,i.nowNs+2));
 for(unsigned bad=0;bad<14;++bad){auto changed=i.config;
  switch(bad){case 0:++changed.fireLogicType;break;case 1:++changed.reloadType;break;case 2:++changed.fireInputAction;break;
  case 3:++changed.reloadInputAction;break;case 4:++changed.baseCapacity;break;case 5:++changed.numberOfMagazines;break;
  case 6:changed.reloadDelay+=1;break;case 7:changed.reloadTime+=1;break;case 8:changed.reloadThreshold=.75f;break;
  case 9:changed.postReloadTime+=1;break;case 10:changed.boltDelay+=1;break;case 11:changed.boltTime+=1;break;
  case 12:changed.holdBoltUntilFireRelease=true;break;case 13:changed.holdBoltUntilZoomRelease=true;break;}
  CHECK(!ManualEmptyFamilyConfig(ReloadNativeFamily::Xm8Magazine,row.id,changed));
  CHECK(!MagazineEmptyStepEligible(s,changed,ReloadRequestCyclePhase::Idle,i.context,i.nowNs+1));
 }
 auto changed=s;++changed.identity.firing[0];CHECK(!MagazineEmptyStepEligible(changed,i.config,ReloadRequestCyclePhase::Idle,i.context,i.nowNs+1));
 changed=s;changed.deadlineNs=i.nowNs;CHECK(!MagazineEmptyStepEligible(changed,i.config,ReloadRequestCyclePhase::Idle,i.context,i.nowNs+1));
}
void OriginalReload(const MagazineNativeRegistration& row,int loaded){
 const auto& p=*row.profile;test::Simulation f(loaded,51,p);f.input.config=Config(p);
 for(unsigned n=0;n<3;++n){f.input.capacities[n]=17;f.input.branches[n].phaseTimer=1.8f;}
 f.tailTimer=1.8f;
 if(loaded==17){CHECK(!f.Arm()&&!f.Lease());return;} // Full magazine has no transfer deficit.
 CHECK(f.Arm());CHECK(f.Lease()&&f.Lease()->capacity==17&&f.Lease()->loaded==loaded&&f.Unseat());
 auto request=f.Request();request.reservedUnits=17-loaded;CHECK(f.policy.Submit(request,f.input.nowNs));
 for(unsigned n=0;n<3;++n)CHECK(f.Transfer(n,17-loaded,n!=1));
 CHECK(f.Settle());const auto ack=f.policy.TakeAcknowledgement(f.input.identity,f.control.cycle,f.input.nowNs);CHECK(ack&&ack->verified);
 for(const auto& b:f.input.branches)CHECK(b.loaded==17&&b.reserve==51-(17-loaded));
}
}
int main(){unsigned groups=0;
 for(const auto& row:generated::MagazineNativeRegistrations){
  CHECK(row.enabled&&row.profile&&row.profile->Reviewed()&&ResolveMagazineNativeProfile(row.id)==row.profile);
  CHECK(row.profile->cycleAdmission==MagazineCycleAdmission::ReviewedSingleFireReload11Transfer12);
  auto forged=*row.profile;forged.cycleAdmission=MagazineCycleAdmission::ReviewedReload11Transfer12;CHECK(!forged.Reviewed());
  forged=*row.profile;forged.configuration.values.fireLogicType=2;CHECK(!forged.Reviewed());
  forged=*row.profile;forged.configuration.admission=ReloadDescriptorAdmission::Candidate;CHECK(!forged.Reviewed());
  CHECK(Bc2MagazineReloadCycle(true,forged).Phase()==ReloadRequestCyclePhase::Disabled);++groups;
  EmptyAndReceipts(row);++groups;
  for(int loaded:{0,8,17}){OriginalReload(row,loaded);++groups;}
 }
 // Existing AutomaticFire dispatch remains independent, including exact config.
 CHECK(Xm8MagazineNativeProfile.Reviewed()&&AekMagazineNativeProfile.Reviewed());
 auto wrong=Xm8MagazineNativeProfile;wrong.cycleAdmission=MagazineCycleAdmission::ReviewedSingleFireReload11Transfer12;
 CHECK(!wrong.Reviewed());++groups;
 std::printf("PASS %u SingleFire family groups; synthetic original callbacks, no native/slide/chamber admission claim\n",groups);
}
