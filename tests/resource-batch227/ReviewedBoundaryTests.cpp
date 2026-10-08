#include "Test.h"
#include "Bc2MagazineEquipmentProfile.h"
#include "Bc2MagazineResourceAdapter.h"
#include <cstring>
using namespace fvr::bc2;
ReloadObservedConfig Config(const MagazineNativeProfile& profile){const auto& d=profile.configuration;const auto& v=d.values;
 ReloadObservedConfig c;c.weaponData=0x210000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
 std::memcpy(c.assetName.data(),d.assetName.data(),d.assetName.size());std::memcpy(c.assetPath.data(),d.assetPath.data(),d.assetPath.size());
 c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
 c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;
 c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
 c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}
int main(){unsigned rows=0;
 for(const auto& row:RegisteredMagazineNativeProfiles()){
  if(std::uint64_t(row.id)<2)continue;++rows;CHECK(row.enabled&&row.profile->Reviewed());
  const auto cfg=Config(*row.profile);const auto p=FindMagazineEquipment(cfg);
  CHECK(p&&p->nativeId==row.id&&p->Ready()&&p->geometry->configurationPath==row.profile->configuration.assetPath);
  CHECK(!FindMagazineEquipment(row.profile->configuration.assetName));
  for(unsigned bad=0;bad<12;++bad){auto c=cfg;
   if(bad==0)c.assetPath[0]='x';if(bad==1)++c.baseCapacity;if(bad==2)++c.numberOfMagazines;
   if(bad==3)++c.fireLogicType;if(bad==4)++c.reloadType;if(bad==5)++c.fireInputAction;if(bad==6)++c.reloadInputAction;
   if(bad==7)c.boltTime=.1f;if(bad==8)c.holdBoltUntilFireRelease=true;if(bad==9)c.reloadThreshold=.1f;
   if(bad==10)++c.ammoAddress;if(bad==11)c.reloadDelay=.1f;
   CHECK(!FindMagazineEquipment(c));
  }
  // Compiled prerequisite availability does not grant an observed runtime owner.
  CHECK(!BindMagazineResourceOwners({},MagazineFamilyEvidence{}, {},{},1000000000));
 }
 CHECK(rows==12&&Xm8MagazineEquipment().Ready()&&FindMagazineEquipment("AEK971_sp")->Ready());return 0;
}
