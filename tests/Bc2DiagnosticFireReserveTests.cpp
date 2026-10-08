#include "Bc2DiagnosticFireReserve.h"
#include "Bc2DiagnosticFireAmmo.h"
#include "Test.h"
#include <cstring>
#include <iostream>
using namespace fvr::bc2;
struct Fixture {
 ReloadObservedConfig config{};std::array<std::uint32_t,1025> words{};unsigned reads=0;bool fail=false,change=false;
 explicit Fixture(const ReloadConfigDescriptor& d){auto& c=config;const auto&v=d.values;
 c.weaponData=0x10000;c.firingData=0x20000;c.primaryFire=0x30000;c.ammoAddress=c.primaryFire+0x170;
 std::copy(d.assetName.begin(),d.assetName.end(),c.assetName.begin());std::copy(d.assetPath.begin(),d.assetPath.end(),c.assetPath.begin());
 c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;
 c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;
 for(const auto&w:d.timing)words[w.offset/4]=w.expected;
 }
 ReloadStateMemory Memory(){return {this,[](void*p,std::uint32_t at,void*dst,std::size_t n)noexcept{auto& f=*static_cast<Fixture*>(p);if(f.fail||n!=4||at<f.config.primaryFire||at>=f.config.primaryFire+4096)return false;
 auto word=f.words[(at-f.config.primaryFire)/4];if(f.change&&f.reads++)word^=1;std::memcpy(dst,&word,4);return true;},nullptr};}
};
int main(){
 const ReloadConfigDescriptor* descriptors[]={&SpasReloadDescriptor,&Xm8ReloadDescriptor,&AekMagazineNativeProfile.configuration};
 for(const auto*d:descriptors){Fixture f(*d);CHECK(DiagnosticFireReserveConfig(f.Memory(),f.config));
  auto c=f.config;c.assetPath[0]='X';CHECK(!DiagnosticFireReserveConfig(f.Memory(),c));c=f.config;c.reloadTime+=.01f;CHECK(!DiagnosticFireReserveConfig(f.Memory(),c));
  f.fail=true;CHECK(!DiagnosticFireReserveConfig(f.Memory(),f.config));f.fail=false;f.change=true;CHECK(!DiagnosticFireReserveConfig(f.Memory(),f.config));
 }
 Fixture candidate(Xm8ReloadDescriptor);candidate.config.assetName[0]='Z';CHECK(!DiagnosticFireReserveConfig(candidate.Memory(),candidate.config));
 std::cout<<"Diagnostic reserve observer: actual registry/timing accepts 3 exact configurations, rejects wrong identity/values/unreadable/changing timing; no request family selection\n";
}
