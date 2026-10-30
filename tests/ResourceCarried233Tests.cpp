#include "ResourceEnrollmentCarriedFixture.h"
namespace {
auto ExactCarried(const BodyInventoryDisplay& d,const BodyEquipmentProfile& p){auto configs=Configs(d);
 for(auto& config:configs){if(!config)continue;auto c=std::make_shared<CarriedMeshesSnapshot>(*config);auto& s=c->configured;
  s.weaponName={};std::copy_n(p.asset,std::strlen(p.asset),s.weaponName.begin());s.configurationPathVerified=true;
  s.configurationPath={};
  std::copy_n(p.configurationPath,std::strlen(p.configurationPath),s.configurationPath.begin());
  s.stateTypeInfo=0x160000;s.meshTypeInfo=0x170000;s.states[0].count=std::uint8_t(p.configuredMeshes.size());s.states[0].meshes={};
  for(unsigned i=0;i<s.states[0].count;++i){auto& m=s.states[0].meshes[i];m.address=0x90000+0x100*i;m.namePointer=0xb0000+0x100*i;m.typeInfo=s.meshTypeInfo;
   std::copy(p.configuredMeshes[i].begin(),p.configuredMeshes[i].end(),m.assetPath.begin());}config=c;
 }return configs;}
int ExactCarriedCatalog(){unsigned count=0,nambu=0,type5=0;
 for(const auto& p:BodyEquipmentProfiles){CHECK(p.configurationPath);++count;FixturePair f;CHECK(f.valid);
  auto d=f.b.inventory;
  if(std::string_view(p.configurationPath)=="Objects/Weapons/Handheld/Beach/Jap_Type5/Type5"){
   ++type5;CHECK(std::string_view(p.asset)=="Garand");
   CHECK(std::string_view(p.mesh)=="Objects/Weapons/Handheld/Beach/Jap_Type5/Jap_Type5_Mesh"&&
    std::string_view(p.part)=="body_holstered_42f20447f5130950"&&p.rig==0xa7f219a1426216abull);
   CHECK(p.configuredMeshes.size()==1&&p.configuredMeshes[0]==p.mesh);
  }
  if(std::string_view(p.asset)=="Nambu100"){
   ++nambu;CHECK(std::string_view(p.configurationPath)=="Objects/Weapons/Handheld/Beach/Nambu100/Nambu100");
   // This campaign item has valid null persistence. Preserve every other
   // native item/container field while the ordinary carried consumer reads it.
   auto nullable=std::make_shared<BodyInventoryDisplay>(*d);
   for(unsigned i=0;i<nullable->count;++i){auto& slot=nullable->slots[i];
    if(slot.native.weapon==nullable->selectedOwner.weapon)continue;
    slot.native.persistence=0;nullable->carried.items[slot.native.slot]=slot.native;}
   CHECK(BodyInventoryDisplayFresh(*nullable,f.Now()));d=nullable;
  }
  auto configs=ExactCarried(*d,p);auto b=BuildBodyCarriedBatch(d,configs,f.r.s.input,Identity(),{},f.Now());
  CHECK(b&&b->count==1&&BodyCarriedBatchFresh(*b,f.Now()));BodyCarriedPair pair;auto key=CarriedPairKey(f.lease);
  const auto first=pair.Read(key,0,*b,f.Now());CHECK(first&&pair.Read(key,1,*b,f.Now()));
  graphics::BodyPropEye eye;CHECK(AppendBodyCarriedPair(*first,key,*b,f.Now(),eye)&&eye.count==1);
  for(unsigned fault=0;fault<6;++fault){auto changed=configs;
   for(auto& ptr:changed){if(!ptr)continue;auto c=std::make_shared<CarriedMeshesSnapshot>(*ptr);
    if(fault==0)c->configured.configurationPathVerified=false;if(fault==1)c->configured.configurationPath[0]='x';
    if(fault==2)c->configured.states[0].count++;if(fault==3)c->configured.states[0].count--;
    if(fault==4)c->configured.states[0].meshes[0].namePointer=0;if(fault==5)c->persistence++;
    CHECK(!body_carried_detail::SameConfig(*ptr,*c)||fault==2||fault==3||fault==4);ptr=c;}
   CHECK(!BuildBodyCarriedBatch(d,changed,f.r.s.input,Identity(),{},f.Now()));}
 }
 CHECK(count==181&&count==BodyEquipmentProfiles.size()&&nambu==1&&type5==1);return 0;
}
}
int main(){if(ExactCarriedCatalog()||ActualPairAndDistinctCounters()||FrameAndOwnerChanges()||OriginalExpiryAndNoNewLease()||ResetAndConcurrentSeed()||ActualInventoryReplacementAndRecenter())return 1;
 std::cout<<"181 exact carried body configurations including null-persistence Nambu and display-only Type5; 1086 invalid sources passed through actual paired-body consumer; CPU only\n";}
