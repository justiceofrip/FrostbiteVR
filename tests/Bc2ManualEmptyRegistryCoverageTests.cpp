// Actual frozen generated registry descriptors; standalone fixtures, never runtime admission.
#include "Bc2MagazineEmptyControl.h"
#include "Test.h"
#include <cstring>
#include <iostream>
#include <map>
#include <set>
using namespace fvr::bc2;
namespace {
constexpr std::int64_t Now=1000000000ll;
ReloadObservedConfig Config(const MagazineNativeProfile& p){
 const auto& d=p.configuration;const auto& v=d.values;ReloadObservedConfig c;
 c.weaponData=0xb0000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
 std::memcpy(c.assetName.data(),d.assetName.data(),d.assetName.size());std::memcpy(c.assetPath.data(),d.assetPath.data(),d.assetPath.size());
 c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
 c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;
 c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
 c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}
MagazineEmptyStep Sample(NativeMagazineProfileId id,unsigned branch){
 MagazineEmptyStep s;s.profile=id;s.branch=branch;s.ownerRevision=5;s.sourceSequence=70;s.observedNs=Now;s.deadlineNs=Now+100000000;
 s.identity.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};s.identity.firing={0x50000,0x60000,0x70000};
 s.identity.serverPlayer=0x80000;s.identity.serverSoldier=0x90000;s.identity.serverItem=0xa0000;
 s.before.address=s.identity.firing[branch];s.before.wrapperOffset=branch==0?0x3c:branch==1?0x40:0x10;
 s.before.currentState=s.before.nextState=2;s.before.loaded=0;s.before.reserve=50;return s;
}
ReloadUpdateContext Context(){ReloadUpdateContext c;c.deltaSeconds=.005f;c.reloadTimeMultiplier=1;c.flags24Through28[0]=true;return c;}
struct TimingMemory {
 const MagazineNativeProfile* source=nullptr;unsigned primary=0xd0000,corruptOffset=UINT32_MAX;
 bool mutateSecond=false;std::map<unsigned,unsigned> reads;
 ReloadStateMemory Access(){return {this,[](void* ctx,unsigned address,void* dst,std::size_t bytes){
  auto& m=*static_cast<TimingMemory*>(ctx);if(bytes!=4||address<m.primary)return false;const auto offset=address-m.primary;
  for(auto word:m.source->configuration.timing)if(word.offset==offset){auto value=word.expected;
   const auto count=++m.reads[offset];if(offset==m.corruptOffset&&(!m.mutateSecond||count>1))value^=1;
   std::memcpy(dst,&value,4);return true;}return false;}};}
};
int Supported(const MagazineNativeRegistration& row){
 const auto config=Config(*row.profile);const auto context=Context();CHECK(ResolveMagazineNativeProfile(row.id)==row.profile);
 CHECK(FindMagazineNativeProfile(config)==&row);CHECK(ManualEmptyFamilyConfig(ReloadNativeFamily::Xm8Magazine,row.id,config));
 for(unsigned branch=0;branch<3;++branch){const auto sample=Sample(row.id,branch);
  for(auto phase:{ReloadRequestCyclePhase::Idle,ReloadRequestCyclePhase::Finished,ReloadRequestCyclePhase::Cancelled})
   CHECK(ManualEmptyFamilyStepEligible(sample,ReloadNativeFamily::Xm8Magazine,config,phase,context,Now+1));
  for(auto phase:{ReloadRequestCyclePhase::Disabled,ReloadRequestCyclePhase::Holding,ReloadRequestCyclePhase::Advancing})
   CHECK(!ManualEmptyFamilyStepEligible(sample,ReloadNativeFamily::Xm8Magazine,config,phase,context,Now+1));
  // Every admitted registry descriptor shares the same intent distinction;
  // neutral/fire callbacks cannot auto-enter, deliberate Reload remains allowed.
  for(unsigned flags:{0u,1u,4u,5u}){
   auto intent=context;intent.inputFlags=flags;intent.fireRequested=bool(flags&1u);intent.reloadRequested=bool(flags&4u);
   CHECK(ManualEmptyFamilyStepEligible(sample,ReloadNativeFamily::Xm8Magazine,config,ReloadRequestCyclePhase::Arming,intent,Now+1)==!(flags&4u));
  }
  auto stale=sample;stale.deadlineNs=Now;CHECK(!ManualEmptyFamilyStepEligible(stale,ReloadNativeFamily::Xm8Magazine,config,ReloadRequestCyclePhase::Idle,context,Now+1));
  auto changed=sample;++changed.identity.owner.equipGeneration; // coherent generation is an external owner proof; no invented lookup.
  changed.before.address=0x123456;CHECK(!ManualEmptyFamilyStepEligible(changed,ReloadNativeFamily::Xm8Magazine,config,ReloadRequestCyclePhase::Idle,context,Now+1));
 }
 const auto sample=Sample(row.id,0);const auto gate=[&](const ReloadObservedConfig& c){return ManualEmptyFamilyStepEligible(sample,ReloadNativeFamily::Xm8Magazine,c,ReloadRequestCyclePhase::Idle,context,Now+1);};
 for(unsigned logic:{0u,1u,3u,4u}){auto c=config;c.fireLogicType=logic;CHECK(!gate(c));}
 for(unsigned type:{0u,2u,3u}){auto c=config;c.reloadType=type;CHECK(!gate(c));}
 CHECK(!ManualEmptyFamilyStepEligible(sample,static_cast<ReloadNativeFamily>(255),config,ReloadRequestCyclePhase::Idle,context,Now+1));
 for(const auto& other:RegisteredMagazineNativeProfiles())if(other.id!=row.id&&ResolveMagazineNativeProfile(other.id)){
  CHECK(!gate(Config(*other.profile)));auto wrong=sample;wrong.profile=other.id;
  CHECK(!ManualEmptyFamilyStepEligible(wrong,ReloadNativeFamily::Xm8Magazine,config,ReloadRequestCyclePhase::Idle,context,Now+1));
 }
 TimingMemory memory{row.profile};auto access=memory.Access();CHECK(ReadManualEmptyFamilyTiming(access,ReloadNativeFamily::Xm8Magazine,row.id,config));
 for(auto word:row.profile->configuration.timing)CHECK(memory.reads[word.offset]==2);
 for(auto word:row.profile->configuration.timing){
  TimingMemory bad{row.profile};bad.corruptOffset=word.offset;auto a=bad.Access();CHECK(!ReadManualEmptyFamilyTiming(a,ReloadNativeFamily::Xm8Magazine,row.id,config));
  TimingMemory race{row.profile};race.corruptOffset=word.offset;race.mutateSecond=true;auto r=race.Access();CHECK(!ReadManualEmptyFamilyTiming(r,ReloadNativeFamily::Xm8Magazine,row.id,config));
 }
 // Genuine cross-descriptor timing mismatch, not a fabricated catalog.
 bool swapped=false;for(const auto& other:RegisteredMagazineNativeProfiles())if(ResolveMagazineNativeProfile(other.id)){
  bool different=false;for(auto word:row.profile->configuration.timing)for(auto candidate:other.profile->configuration.timing)if(word.offset==candidate.offset&&word.expected!=candidate.expected)different=true;
  if(!different)continue;TimingMemory m{other.profile};auto a=m.Access();CHECK(!ReadManualEmptyFamilyTiming(a,ReloadNativeFamily::Xm8Magazine,row.id,config));swapped=true;break;
 }CHECK(swapped);
 return 0;
}
int Run(){unsigned generated=0,supported=0,disabled=0;std::set<std::string> names;
 for(const auto& row:RegisteredMagazineNativeProfiles()){
  CHECK(row.profile);const bool generatedRow=std::uint64_t(row.id)>1;if(generatedRow){++generated;names.insert(std::string(row.profile->configuration.assetName));}
#ifdef COVERAGE_DISABLED
  if(generatedRow){++disabled;auto c=Config(*row.profile);CHECK(!row.enabled&&!ResolveMagazineNativeProfile(row.id));
   CHECK(!ManualEmptyFamilyStepEligible(Sample(row.id,0),ReloadNativeFamily::Xm8Magazine,c,ReloadRequestCyclePhase::Idle,Context(),Now+1));
   TimingMemory m{row.profile};auto a=m.Access();CHECK(!ReadManualEmptyFamilyTiming(a,ReloadNativeFamily::Xm8Magazine,row.id,c));CHECK(m.reads.empty());continue;}
#endif
  CHECK(row.enabled);CHECK(Supported(row)==0);++supported;
 }
 CHECK(generated==21&&names.size()==7);
#ifdef COVERAGE_DISABLED
 CHECK(disabled==21&&supported==2);
#else
 CHECK(disabled==0&&supported==23);
#endif
 std::cout<<"actual generated rows="<<generated<<" names="<<names.size()<<" supported-in-fixture="<<supported<<" disabled="<<disabled<<"; native game acceptance unproven\n";
 return 0;
}
}
int main(){return Run();}
