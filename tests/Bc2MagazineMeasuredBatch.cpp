// Standalone OFFLINE probe: measured profiles + actual consumer, mocked native API.
// Fixture registration exists only inside this executable; no game feature grant.
#include "Bc2MagazineConsumerFixture.h"
#include "Bc2ReloadNativePolicy.h"
#include "Bc2MagazineNativeAdapter.h"
using namespace magazine_consumer_fixture;
namespace {
ReloadObservedConfig Config(const MagazineNativeProfile& p){const auto& d=p.configuration;const auto& v=d.values;
 ReloadObservedConfig c;c.weaponData=0x210000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
 std::memcpy(c.assetName.data(),d.assetName.data(),d.assetName.size());std::memcpy(c.assetPath.data(),d.assetPath.data(),d.assetPath.size());
 c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
 c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;
 c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
 c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}
struct Consumer:Fixture {
 Bc2ReloadNativePolicy selector;ReloadObservedConfig observed;
 Consumer(const MagazineEquipmentProfile& p,bool empty):Fixture(true,p,true),observed(Config(*p.native)){
  reserve.capacity=p.native->configuration.values.baseCapacity;reserve.loaded=empty?0:reserve.capacity-3;reserve.reserve=reserve.capacity+11;
  reserve.emptyReloadControlled=empty; // Explicit MOCK of all-three native receipts; no native evidence claim.
  if(!selector.SelectMagazineProfile(p.nativeId,true,false))throw std::runtime_error("profile selection failed");
  api.reserve=[](void* context)noexcept->std::optional<Bc2AmmoReserveLease>{auto& f=*static_cast<Consumer*>(context);
   return f.source&&f.selector.MatchesSelectedConfig(f.observed)?std::optional{f.reserve}:std::nullopt;};
  api.context=this;policy.reset();policy.emplace(true,api,AmmoSupplyConfig{InteractionHand::Left,1000,{1001,1},{0,0,0},.15f,200*Ms});
 }
 bool Seat(){if(!Eject())return false;Send();s.bodyFromHand=Pose();
  const auto travel=profile->geometry->interaction.insertion.travelMeters;
  for(float z=-.1f;z<=travel+.0001f;z+=.02f){Send(true,false,z);if(submits)return true;}
  for(unsigned n=0;n<20&&!submits;++n)Send(true,false,travel);
  return submits==1;
 }
};
int Timing(const MagazineNativeProfile& p){auto c=Config(p);struct Memory{const MagazineNativeProfile* p;std::uint32_t primary;bool bad=false;} m{&p,c.primaryFire};
 ReloadStateMemory memory{&m,[](void* context,std::uint32_t at,void* out,std::size_t bytes)noexcept{
  auto& m=*static_cast<Memory*>(context);if(bytes!=4)return false;for(auto w:m.p->configuration.timing)if(at==m.primary+w.offset){auto value=w.expected+(m.bad?1:0);std::memcpy(out,&value,4);return true;}return false;}};
 CHECK(p.ReadTiming(memory,c));m.bad=true;CHECK(!p.ReadTiming(memory,c));return 0;
}
int Run(){unsigned rows=0;
 for(const auto& row:RegisteredMagazineNativeProfiles()){
  if(std::uint64_t(row.id)<2)continue;++rows;CHECK(row.profile);auto c=Config(*row.profile);
#ifdef BATCH_DISABLED
  CHECK(!row.enabled&&!ResolveMagazineNativeProfile(row.id)&&!FindMagazineNativeProfile(c)&&!FindMagazineEquipment(c));
  CHECK(!FindMagazineGeometry(*row.profile));
  std::cout<<"{\"asset\":\""<<row.profile->configuration.assetName<<"\",\"path\":\""<<row.profile->configuration.assetPath<<"\",\"mode\":\"production_disabled\",\"pass\":true}\n";
#else
  CHECK(row.enabled&&FindMagazineNativeProfile(c)==&row);const auto p=FindMagazineEquipment(c);CHECK(p&&p->Ready()&&p->experimentalGeometry&&!p->geometryVerified);
  CHECK(p->geometry->asset==row.profile->configuration.assetName);
  unsigned sameName=0;for(const auto& other:RegisteredMagazineNativeProfiles())if(other.profile&&other.profile->configuration.assetName==row.profile->configuration.assetName)++sameName;
  if(sameName>1)CHECK(!FindMagazineNativeProfile(row.profile->configuration.assetName)); // Multiple exact paths must never collapse to name.
  else CHECK(FindMagazineNativeProfile(row.profile->configuration.assetName)==&row);
  CHECK(!Timing(*row.profile));
  for(bool empty:{false,true}){
   Consumer f(*p,empty);const auto loaded=f.reserve.loaded,remaining=f.reserve.reserve;const auto seated=f.Seat();
   if(!seated)std::cerr<<"SEAT_FAILURE "<<row.profile->configuration.assetName<<" "<<row.profile->configuration.assetPath<<" empty="<<empty<<" starts="<<f.starts<<" submits="<<f.submits<<" phase="<<unsigned(f.result.interaction.phase)<<"\n"<<f.Report()<<"\n";
   CHECK(seated);
   CHECK(f.result.tracking.family.carried&&!f.result.tracking.family.binding.launcher&&f.result.tracking.family.binding.profile==p);
   CHECK(f.submitted&&f.submitted->reservedUnits==unsigned(f.reserve.capacity-loaded));
   f.Complete();f.Send();CHECK(f.result.completed==1&&f.reserve.loaded==f.reserve.capacity&&f.reserve.reserve==remaining-(f.reserve.capacity-loaded));
   f.allowRetire=true;f.Send();f.Send();CHECK(!f.policy->BlocksEquipment());
   Consumer denied(*p,empty);denied.observed.assetPath[0]='x';denied.Send();denied.Send(true);CHECK(!denied.starts&&!denied.result.tracking.enabled);
   denied.observed=Config(*p->native);++denied.observed.baseCapacity;denied.Send();denied.Send(true);CHECK(!denied.starts);
   denied.observed=Config(*p->native);denied.Send();CHECK(MagazineTrackingFresh(denied.result.tracking,denied.now));
   if(empty){Consumer uncontrolled(*p,true);uncontrolled.reserve.emptyReloadControlled=false;uncontrolled.Send();uncontrolled.Send(false,true);CHECK(!uncontrolled.starts&&uncontrolled.result.ordinaryReloadAllowed);}
  }
  std::cout<<"{\"asset\":\""<<row.profile->configuration.assetName<<"\",\"path\":\""<<row.profile->configuration.assetPath<<"\",\"mode\":\"mock_native_measured_geometry\",\"partial_reload\":true,\"empty_reload\":true,\"exact_identity_denials\":true,\"pass\":true}\n";
#endif
 }
 CHECK(rows>1);return 0;
}
}
int main(){return Run();}
