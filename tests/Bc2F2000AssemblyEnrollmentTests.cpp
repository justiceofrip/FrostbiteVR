#include "Bc2MagazineEquipmentProfile.h"
#include "Bc2MagazineAssembly.h"
#include "Bc2BodyAmmoGeometryCache.h"
#include "Bc2BodyAmmoAssetProfiles.h"
#include "Bc2BodyEquipmentProfiles.h"
#include "Test.h"
#include <iostream>
using namespace fvr::bc2;
int main(int argc,char** argv){
 const auto* p=FindMagazineEquipment("F2000_sp");CHECK(p&&p->Ready());const auto& g=*p->geometry;
 CHECK(g.configurationPath=="Objects/Weapons/Handheld/BU_rif_F2000/SP_rif_F2000"&&g.assemblyCount==3&&MagazineAssemblyShape(g));
 CHECK(g.bones.magazine=="jntWpnwpnJnt_16"&&g.assembly[0].parent==g.bones.magazine);
 CHECK(p->native->configuration.values.baseCapacity==30&&p->native->configuration.values.boltTime==0);
 unsigned detached=0,body=0;for(const auto& row:BodyAmmoAssetProfiles)if(row.asset==g.asset&&row.mesh==g.mesh){
  if(row.part==g.bones.magazine){++detached;CHECK(row.sections.size()==2);unsigned triangles=0;
   for(const auto& section:row.sections)triangles+=section.geometry.expectedPartTriangles;CHECK(triangles==294);}}
 for(const auto& row:BodyEquipmentProfiles)if(row.configurationPath&&row.asset==g.asset){++body;CHECK(row.configurationPath==g.configurationPath&&row.rig==g.rigFingerprint);}
 CHECK(detached==1&&body==1);
 CHECK(argc<=2);if(argc==2){const auto loaded=LoadBodyAmmoGeometryCache(argv[1]);CHECK(loaded.status==BodyAmmoCacheStatus::Loaded&&loaded.catalog->size()==44);
  unsigned found=0;for(const auto& row:*loaded.catalog)if(row->asset==g.asset&&row->mesh==g.mesh&&row->part==g.bones.magazine){++found;
   unsigned triangles=0;for(const auto& section:row->sections)triangles+=unsigned(section.mesh.vertices.size()/3);CHECK(triangles==294);}
  CHECK(found==1);}
 std::cout<<"F2000 exact reviewed configuration, complete three-member assembly, separate whole body and 294-triangle installed cache passed; no native/GPU actions.\n";
}
