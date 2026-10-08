#include "Bc2ReloadConfigDescriptor.h"
#include "Bc2ReloadHold.h"
#include "Bc2ReloadRoundGate.h"
#include "Bc2MagazineReloadCycle.h"
#include "Test.h"
#include <vector>
#include <limits>
using namespace fvr::bc2;
namespace {
ReloadObservedConfig Observed(const ReloadConfigDescriptor& d){
 ReloadObservedConfig c;c.weaponData=0xb0000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
 std::memcpy(c.assetName.data(),d.assetName.data(),d.assetName.size());
 std::memcpy(c.assetPath.data(),d.assetPath.data(),d.assetPath.size());
 const auto& v=d.values;c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;
 c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
 c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;
 c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;c.reloadThreshold=v.reloadThreshold;
 c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
 c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}
// Frozen predecessor predicates: this refactor must preserve the existing
// configuration acceptance set, not just accept a new descriptor's happy path.
bool Legacy(const ReloadObservedConfig& c,bool xm8){
 const char* name=xm8?"XM8_sp_s":"SPAS12_sp";
 const char* path=xm8?"Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped":"Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12";
 return !std::strncmp(c.assetName.data(),name,c.assetName.size())&&
 !std::strncmp(c.assetPath.data(),path,c.assetPath.size())&&c.weaponData>=0x10000&&c.firingData>=0x10000&&c.primaryFire>=0x10000&&
 std::uint64_t(c.primaryFire)+0x170==c.ammoAddress&&c.fireLogicType==(xm8?2:1)&&c.reloadType==(xm8?1:0)&&
 c.fireInputAction==8&&c.reloadInputAction==29&&c.baseCapacity==(xm8?30:4)&&c.numberOfMagazines==4&&
 c.reloadDelay==(xm8?0.f:.06f)&&c.reloadTime==(xm8?2.8f:.72f)&&c.reloadThreshold==(xm8?.75f:1.f)&&
 c.postReloadTime==(xm8?0.f:1.f)&&c.boltDelay==(xm8?0.f:.5f)&&c.boltTime==0&&!c.holdBoltUntilFireRelease&&!c.holdBoltUntilZoomRelease;
}
bool Wrapper(const ReloadObservedConfig& c,bool xm8){return xm8?IsXm8MagazineConfig(c):IsDiagnosticSpasConfig(c);}
int AcceptanceAndIdentityRemainExact(){
 for(bool xm8:{false,true}){
  const auto& d=xm8?Xm8ReloadDescriptor:SpasReloadDescriptor;const auto original=Observed(d);
  CHECK(Wrapper(original,xm8));CHECK(!Wrapper(original,!xm8));
  std::vector<ReloadObservedConfig> cases{original};
  for(std::size_t n=0;n<original.assetName.size();++n){auto c=original;c.assetName[n]^=1;cases.push_back(c);}
  for(std::size_t n=0;n<original.assetPath.size();++n){auto c=original;c.assetPath[n]^=1;cases.push_back(c);}
  for(auto field:{&ReloadObservedConfig::weaponData,&ReloadObservedConfig::firingData,&ReloadObservedConfig::primaryFire,&ReloadObservedConfig::ammoAddress})
   for(std::uint32_t v:{0u,0xffffu,0x10000u,UINT32_MAX}){auto c=original;c.*field=v;cases.push_back(c);}
  for(auto field:{&ReloadObservedConfig::fireLogicType,&ReloadObservedConfig::reloadType,&ReloadObservedConfig::fireInputAction,&ReloadObservedConfig::reloadInputAction,&ReloadObservedConfig::baseCapacity,&ReloadObservedConfig::numberOfMagazines})
   for(int v:{-1,0,1,2,4,8,29,30}){auto c=original;c.*field=v;cases.push_back(c);}
  for(auto field:{&ReloadObservedConfig::reloadDelay,&ReloadObservedConfig::reloadTime,&ReloadObservedConfig::reloadThreshold,&ReloadObservedConfig::postReloadTime,&ReloadObservedConfig::boltDelay,&ReloadObservedConfig::boltTime})
   for(float v:{-0.f,0.f,.06f,.72f,1.f,2.8f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){auto c=original;c.*field=v;cases.push_back(c);}
  for(auto field:{&ReloadObservedConfig::holdBoltUntilFireRelease,&ReloadObservedConfig::holdBoltUntilZoomRelease}){auto c=original;c.*field=true;cases.push_back(c);}
  for(const auto& c:cases)CHECK(Wrapper(c,xm8)==Legacy(c,xm8));
 }
 return 0;
}
struct Memory {
 std::uint32_t base=0xd0000;std::vector<ReloadTimingWord> words;std::vector<std::uint32_t> reads;
 int fail=-1,corrupt=-1;bool drift=false;
 static bool Read(void* context,std::uint32_t address,void* out,std::size_t size){
  auto& m=*static_cast<Memory*>(context);const auto call=int(m.reads.size());m.reads.push_back(address);
  if(call==m.fail||size!=4)return false;
  for(const auto& w:m.words)if(address==m.base+w.offset){auto value=w.expected;
   if(call==m.corrupt||(m.drift&&call>=int(m.words.size())))value^=1;
   std::memcpy(out,&value,4);return true;}return false;
 }
 ReloadStateMemory Adapter(){return {this,&Read,nullptr};}
};
int RealTimingWrappersReadTheSameWordsTwice(){
 for(bool xm8:{false,true}){
  const auto& d=xm8?Xm8ReloadDescriptor:SpasReloadDescriptor;const auto c=Observed(d);
  Memory m;m.words.assign(d.timing.begin(),d.timing.end());
  const auto read=[&](){return xm8?ReadXm8MagazineTiming(m.Adapter(),c):ReadReloadRoundTiming(m.Adapter(),c);};
  CHECK(read());CHECK(m.reads.size()==2*d.timing.size());
  for(std::size_t n=0;n<m.reads.size();++n)CHECK(m.reads[n]==c.primaryFire+d.timing[n%d.timing.size()].offset);
  for(int n=0;n<int(2*d.timing.size());++n){m.reads.clear();m.fail=n;CHECK(!read());CHECK(m.reads.size()==std::size_t(n+1));}
  m.fail=-1;for(int n=0;n<int(2*d.timing.size());++n){m.reads.clear();m.corrupt=n;CHECK(!read());}
  m.corrupt=-1;m.drift=true;m.reads.clear();CHECK(!read());
 }
 return 0;
}
int CandidateOrWrongIdentityNeverReadsNativeMemory(){
 auto d=Xm8ReloadDescriptor;auto c=Observed(d);Memory m;m.words.assign(d.timing.begin(),d.timing.end());
 d.admission=ReloadDescriptorAdmission::Candidate;CHECK(!MatchesReloadDescriptor(c,d));CHECK(!ReadReloadDescriptorTiming(m.Adapter(),c,d));CHECK(m.reads.empty());
 d=Xm8ReloadDescriptor;c.assetName[0]='A';CHECK(!ReadReloadDescriptorTiming(m.Adapter(),c,d));CHECK(m.reads.empty());
 c=Observed(d);c.primaryFire=UINT32_MAX-0x100;c.ammoAddress=c.primaryFire+0x170;
 CHECK(!ReadReloadDescriptorTiming(m.Adapter(),c,d));CHECK(m.reads.empty());
 c=Observed(d);d.timing={};CHECK(!ReadReloadDescriptorTiming(m.Adapter(),c,d));CHECK(m.reads.empty());return 0;
}
int AuthoredAndCurrentCapacityStaySeparate(){
 auto c=Observed(SpasReloadDescriptor);CHECK(IsDiagnosticSpasConfig(c));
 ReloadFiringObservation native;native.effectiveCapacity=8;
 CHECK(c.baseCapacity==4&&native.effectiveCapacity==8);
 c.baseCapacity=8;CHECK(!IsDiagnosticSpasConfig(c));
 c=Observed(Xm8ReloadDescriptor);c.authoredStateCount=1;c.authoredPumpHandling[0]=true;
 CHECK(IsXm8MagazineConfig(c)); // Existing matcher never admitted a pump mechanism from this field.
 return 0;
}
}
int main(){
 CHECK(!AcceptanceAndIdentityRemainExact());CHECK(!RealTimingWrappersReadTheSameWordsTwice());
 CHECK(!CandidateOrWrongIdentityNeverReadsNativeMemory());CHECK(!AuthoredAndCurrentCapacityStaySeparate());
 std::puts("4 shared native reload descriptor groups passed");return 0;
}
