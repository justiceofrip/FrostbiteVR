#include "ResourceEnrollmentCarriedFixture.h"
namespace {
auto ExactCarried(const BodyInventoryDisplay& d,const BodyEquipmentProfile& p){auto configs=Configs(d);
 for(auto& config:configs){if(!config)continue;auto c=std::make_shared<CarriedMeshesSnapshot>(*config);auto& s=c->configured;
  s.weaponName={};std::copy_n(p.asset,std::strlen(p.asset),s.weaponName.begin());s.configurationPathVerified=true;
  std::copy_n(p.configurationPath,std::strlen(p.configurationPath),s.configurationPath.begin());
  s.stateTypeInfo=0x160000;s.meshTypeInfo=0x170000;s.states[0].count=std::uint8_t(p.configuredMeshes.size());s.states[0].meshes={};
  for(unsigned i=0;i<s.states[0].count;++i){auto& m=s.states[0].meshes[i];m.address=0x90000+0x100*i;m.namePointer=0xb0000+0x100*i;m.typeInfo=s.meshTypeInfo;
   std::copy(p.configuredMeshes[i].begin(),p.configuredMeshes[i].end(),m.assetPath.begin());}config=c;
 }return configs;}
int TwelveCarried(){unsigned count=0;
 for(const auto& p:BodyEquipmentProfiles){if(!p.configurationPath)continue;++count;FixturePair f;
  auto d=f.b.inventory;auto configs=ExactCarried(*d,p);auto b=BuildBodyCarriedBatch(d,configs,f.r.s.input,Identity(),{},f.Now());
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
 CHECK(count==15);return 0;
}
}
int main(){if(TwelveCarried()||ActualPairAndDistinctCounters()||FrameAndOwnerChanges()||OriginalExpiryAndNoNewLease()||ResetAndConcurrentSeed()||ActualInventoryReplacementAndRecenter())return 1;
 std::cout<<"15 exact carried body configurations and 90 invalid sources passed through actual paired-body consumer; CPU only\n";}
