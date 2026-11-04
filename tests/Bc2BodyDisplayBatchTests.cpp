#include "BodyDisplayBatchFixture.inc"
#include "Bc2BodyAmmoGeometryCache.h"
#include "Bc2BodyAmmoAssetProfiles.h"
#include <fstream>
namespace {
int ChestKnifeDisplay(){
 Run r;
 r.f.Word(Fixture::array+4,0);r.f.Word(Fixture::array+7*4,Fixture::b);
 r.f.Word(Fixture::bd+0x84,11);r.f.Word(Fixture::bd+0x64,0x31000);r.f.Text(0x2b100,"KNV-1");
 r.Tick();auto d=r.adapter.RenderDisplay(r.s.hand.nowNs);CHECK(d&&d->count==1);
 CHECK(!r.adapter.AssignedSlot(Fixture::b)&&d->knifeCandidate==r.adapter.ItemKey(Fixture::b).value());
 const auto equip=ReadWeaponEquipmentIdentity(r.f.Memory(),Fixture::a);CHECK(equip);
 const auto slot=body_carried_detail::StowedKnifeSlot(*d,*equip,false);CHECK(slot&&slot->native.slot==7);
 CHECK(!body_carried_detail::StowedKnifeSlot(*d,*equip,true));
 auto other=*equip;++other.data;CHECK(!body_carried_detail::StowedKnifeSlot(*d,other,false));
 other=*equip;strcpy_s(other.asset.data(),other.asset.size(),"KNV-1");
 CHECK(!body_carried_detail::StowedKnifeSlot(*d,other,false));
 auto c=std::make_shared<CarriedMeshesSnapshot>(*Mesh(*d,0));
 c->weapon=slot->native.weapon;c->nativeSlot=7;c->persistence=slot->native.persistence;
 auto& s=c->configured;s.weaponData=slot->native.data;
 const auto& p=body_carried_detail::ChestKnifeProfile();
 strcpy_s(s.weaponName.data(),s.weaponName.size(),p.asset);
 strcpy_s(s.configurationPath.data(),s.configurationPath.size(),p.configurationPath);
 strcpy_s(s.states[0].meshes[0].assetPath.data(),s.states[0].meshes[0].assetPath.size(),p.mesh);
 std::array<std::shared_ptr<const CarriedMeshesSnapshot>,8> configs{};configs[d->count]=c;
 const auto batch=[&](const auto& snapshot,const auto& meshes){
  return BuildBodyCarriedBatch(std::make_shared<const BodyInventoryDisplay>(snapshot),meshes,r.s.input,Identity(),{},r.s.hand.nowNs);};
 const auto stowed=batch(*d,configs);CHECK(stowed&&stowed->count==1);
 CHECK(stowed->instances[0].geometry==MakeBodyAmmoGeometryKey(p.asset,p.mesh,p.part,p.rig));
 CHECK(interaction::BodyAnchorContains(private_knife::KnifeChest,stowed->instances[0].world));
 const auto& w=stowed->instances[0].world.values;
 CHECK(std::fabs(w[3][0]-private_knife::KnifeChest.center.x)<.00001f&&
       std::fabs(w[3][1]-private_knife::KnifeChest.center.y)<.00001f&&
       std::fabs(w[3][2]-private_knife::KnifeChest.center.z)<.00001f&&w[2][1]>.999f);
 graphics::BodyPropEye eye{};CHECK(AppendBodyCarriedHostInstances(*stowed,*stowed,r.s.hand.nowNs,d->physicalOwner.space,eye)&&eye.count==1);
 auto wrong=std::make_shared<CarriedMeshesSnapshot>(*c);
 strcpy_s(wrong->configured.configurationPath.data(),wrong->configured.configurationPath.size(),"Objects/Weapons/Handheld/Knife_Equipable/Knife_Equipable");
 CHECK(!body_carried_detail::Profile(*d,*slot,*wrong,r.s.hand.nowNs));
 wrong=std::make_shared<CarriedMeshesSnapshot>(*c);++wrong->configured.states[0].count;
 CHECK(!body_carried_detail::Profile(*d,*slot,*wrong,r.s.hand.nowNs));
 const auto oldKey=d->knifeCandidate;
 r.f.Word(Fixture::bd+0x64,0x32000);r.Advance();r.Tick();d=r.adapter.RenderDisplay(r.s.hand.nowNs);
 CHECK(d&&d->knifeCandidate!=oldKey&&!batch(*d,configs)&&!SameBodyInventoryDisplayCohort(*stowed->inventory,*d));
 CHECK(!BodyCarriedBatchFresh(*stowed,stowed->instances[0].deadlineNs));
 r.f.Select(7);r.s.owner=r.f.owner;r.s.gun={Fixture::b,100};++r.s.hand.owner.equipGeneration;r.Advance();r.Gun();r.Tick();
 d=r.adapter.RenderDisplay(r.s.hand.nowNs);CHECK(d&&!d->knifeCandidate.id&&!body_carried_detail::RenderSlot(*d,d->count));
 const auto drawn=batch(*d,configs);CHECK(drawn&&drawn->count==0);
 CHECK(!AppendBodyCarriedHostInstances(*stowed,*drawn,r.s.hand.nowNs,d->physicalOwner.space,eye)&&eye.count==1);
 r.s.input.focused=false;r.Advance();r.Tick();CHECK(!r.adapter.RenderDisplay(r.s.hand.nowNs));
 return 0;
}
}
int main(int argc,char** argv){CHECK(argc<=2);if(PersistentRegressionMain())return 1;
 if(ChestKnifeDisplay())return 1;
 Run r;r.Tick();const auto d=r.adapter.Display(r.s.hand.nowNs);CHECK(d);
 unsigned i=0;while(i<d->count&&d->slots[i].native.weapon!=Fixture::b)++i;CHECK(i<d->count);
 unsigned checked=0;
 for(const auto& profile:BodyEquipmentProfiles){
  auto c=std::make_shared<CarriedMeshesSnapshot>(*Mesh(*d,i));
  strcpy_s(c->configured.weaponName.data(),128,profile.asset);
  strcpy_s(c->configured.states[0].meshes[0].assetPath.data(),512,profile.mesh);
  if(profile.configurationPath){auto& s=c->configured;s.configurationPathVerified=true;
   strcpy_s(s.configurationPath.data(),512,profile.configurationPath);s.stateTypeInfo=0x160000;s.meshTypeInfo=0x170000;
   s.states[0].count=std::uint8_t(profile.configuredMeshes.size());s.states[0].meshes={};
   for(unsigned n=0;n<s.states[0].count;++n){auto& m=s.states[0].meshes[n];m.address=0x90000+n*0x100;m.namePointer=0xb0000+n*0x100;
    m.typeInfo=s.meshTypeInfo;std::copy(profile.configuredMeshes[n].begin(),profile.configuredMeshes[n].end(),m.assetPath.begin());}}
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
 const auto& knife=body_carried_detail::ChestKnifeProfile();
 const auto knifeKey=MakeBodyAmmoGeometryKey(knife.asset,knife.mesh,knife.part,knife.rig);
 const auto actual=std::find_if(loaded.catalog->begin(),loaded.catalog->end(),[&](const auto& g){return g->key==knifeKey;});
 CHECK(actual!=loaded.catalog->end()&&(*actual)->sections.size()==2);
 float tip=1.f,pommel=-1.f;unsigned vertices=0;
 for(const auto& section:(*actual)->sections)for(const auto& v:section.mesh.vertices){
    tip=std::min(tip,v.z);pommel=std::max(pommel,v.z);++vertices;
 }
 CHECK(vertices==3u*(1118+950)&&std::fabs(tip-private_knife::BladeTipInKnife.z)<.0001f&&std::fabs(pommel-.0739133f)<.0001f);
 for(const auto& p:BodyEquipmentProfiles){const auto key=MakeBodyAmmoGeometryKey(p.asset,p.mesh,p.part,p.rig);
  CHECK(std::count_if(loaded.catalog->begin(),loaded.catalog->end(),[&](const auto& g){return g->key==key&&g->asset==p.asset&&g->mesh==p.mesh&&g->part==p.part&&g->rigFingerprint==p.rig;})==1);}
 }
 std::cout<<checked<<" exact display rows: carried source/host identity, stale persistence and expiry passed"<<(argc==2?"; private cache parser passed":"; private cache not supplied")<<"\n";return 0;}

