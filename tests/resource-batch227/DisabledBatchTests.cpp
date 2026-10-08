#include "Test.h"
#include "Bc2MagazineEquipmentProfile.h"
#include "../../profiles/resource-batch227/zero-bolt-batch/CombinedGeometry.h"
using namespace fvr::bc2;
int main(){unsigned enabled=0,disabled=0;
 for(const auto& row:RegisteredMagazineNativeProfiles()){
  if(row.enabled){++enabled;CHECK(std::uint64_t(row.id)<2);CHECK(FindMagazineEquipment(row.id)->Ready());}
  else {++disabled;CHECK(!ResolveMagazineNativeProfile(row.id)&&!FindMagazineEquipment(row.id));
   CHECK(!FindMagazineGeometry(*row.profile));unsigned matches=0;
   for(const auto& g:generated::ExperimentalMagazineGeometry)matches+=MagazineGeometryMatchesConfiguration(g,*row.profile);
   CHECK(matches==1);
  }
 }
 CHECK(enabled==2&&disabled==12);CHECK(generated::PreservedBaselineMagazineGeometry.size()==1);
 CHECK(generated::PreparedZeroBoltMagazineGeometry.size()==12&&generated::ExperimentalMagazineGeometry.size()==13);
 CHECK(FindMagazineEquipment("AEK971_sp")->Ready()&&Xm8MagazineEquipment().Ready());
 for(const auto asset:{"M416","MG36","XM8 LMG","XM8C"})CHECK(!FindMagazineEquipment(asset));
 return 0;
}
