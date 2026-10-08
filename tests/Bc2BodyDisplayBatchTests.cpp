#include "BodyDisplayBatchFixture.inc"
#include "Bc2BodyAmmoGeometryCache.h"
#include "Bc2BodyAmmoAssetProfiles.h"
#include <fstream>
int main(int argc,char** argv){CHECK(argc<=2);if(PersistentRegressionMain())return 1;
 Run r;r.Tick();const auto d=r.adapter.Display(r.s.hand.nowNs);CHECK(d);
 unsigned i=0;while(i<d->count&&d->slots[i].native.weapon!=Fixture::b)++i;CHECK(i<d->count);
 unsigned checked=0;
 for(const auto& profile:BodyEquipmentProfiles){
  auto c=std::make_shared<CarriedMeshesSnapshot>(*Mesh(*d,i));
  strcpy_s(c->configured.weaponName.data(),128,profile.asset);
  strcpy_s(c->configured.states[0].meshes[0].assetPath.data(),512,profile.mesh);
  std::array<std::shared_ptr<const CarriedMeshesSnapshot>,8> configs{};configs[i]=c;
  auto b=BuildBodyCarriedBatch(std::make_shared<const BodyInventoryDisplay>(*d),configs,r.s.input,Identity(),{},r.s.hand.nowNs);
  CHECK(b&&b->count==1);CHECK(b->instances[0].geometry==MakeBodyAmmoGeometryKey(profile.asset,profile.mesh,profile.part,profile.rig));
  graphics::BodyPropEye eye{};CHECK(AppendBodyCarriedHostInstances(*b,*b,r.s.hand.nowNs,d->physicalOwner.space,eye)&&eye.count==1);
  auto stale=*b;auto changed=std::make_shared<CarriedMeshesSnapshot>(*c);++changed->persistence;stale.configured[0]=changed;
  CHECK(!BodyCarriedBatchFresh(stale,r.s.hand.nowNs));CHECK(!AppendBodyCarriedHostInstances(*b,stale,r.s.hand.nowNs,d->physicalOwner.space,eye)&&eye.count==1);
  CHECK(!AppendBodyCarriedHostInstances(*b,*b,d->deadlineNs,d->physicalOwner.space,eye));
  ++checked;
 }
 if(argc==2){
 std::ifstream file(argv[1],std::ios::binary);
 std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});CHECK(!bytes.empty());
 auto loaded=ParseBodyAmmoGeometryCache({reinterpret_cast<const std::byte*>(bytes.data()),bytes.size()});
 CHECK(loaded.status==BodyAmmoCacheStatus::Loaded&&loaded.catalog&&loaded.catalog->size()==BodyAmmoAssetProfiles.size());
 for(const auto& p:BodyEquipmentProfiles){const auto key=MakeBodyAmmoGeometryKey(p.asset,p.mesh,p.part,p.rig);
  CHECK(std::count_if(loaded.catalog->begin(),loaded.catalog->end(),[&](const auto& g){return g->key==key&&g->asset==p.asset&&g->mesh==p.mesh&&g->part==p.part&&g->rigFingerprint==p.rig;})==1);}
 }
 std::cout<<checked<<" exact display rows: carried source/host identity, stale persistence and expiry passed"<<(argc==2?"; private cache parser passed":"; private cache not supplied")<<"\n";return 0;}

