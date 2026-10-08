#include "ResourceEnrollmentBodyFixture.h"
#include "Bc2VisibilityDescriptorData.h"
#include "Bc2MagazineEquipmentProfile.h"
#include "Bc2BodyAmmoGeometryCache.h"
#include "Bc2BodyAmmoAssetProfiles.h"
#include <iostream>
namespace {
template<std::size_t N> void Put(std::array<char,N>& out,std::string_view text){out.fill(0);std::copy(text.begin(),text.end(),out.begin());}
void Configure(Fixture& f,const BodyEquipmentProfile& p){
 auto s=std::make_shared<SelectedMeshesSnapshot>(*f.s.selected);Put(s->weaponName,p.asset);
 s->configurationPathVerified=true;Put(s->configurationPath,p.configurationPath);
 s->operationBinding=NativeOperationCapabilities[0].binding;s->stateTypeInfo=0x160000;s->meshTypeInfo=0x170000;
 s->states[0].count=std::uint8_t(p.configuredMeshes.size());s->states[0].meshes={};
 for(unsigned i=0;i<s->states[0].count;++i){auto& m=s->states[0].meshes[i];m.address=0x90000+0x100*i;m.namePointer=0xb0000+0x100*i;
  m.typeInfo=s->meshTypeInfo;Put(m.assetPath,p.configuredMeshes[i]);}
 f.s.selected=s;
}
void ExactCycle(Fixture& f,const BodyEquipmentProfile& p){auto request=f.out.visibility;f.Advance();Configure(f,p);f.Render(request);f.Suppress();f.Tick();}
int ExactBodyAndVisibility(){unsigned count=0;
 for(const auto& p:BodyEquipmentProfiles){if(!p.configurationPath)continue;++count;
  Fixture f;f.adapter=Bc2BodyHolster{BodyInventoryHolsterAcceptance};Configure(f,p);f.Tick();
  CHECK(f.adapter.AcceptsProfile(f.s.nativeOwner,f.s.selected,f.s.hand.nowNs));
  const auto& snapshot=*f.s.selected;const auto* descriptor=ResolveVisibilityDescriptor(snapshot,f.s.nativeOwner,VisibilityDescriptors,f.s.hand.nowNs);
  CHECK(descriptor&&descriptor->configurationPath==p.configurationPath&&descriptor->weightedSectionDataVerified);
  CHECK(!descriptor->nativeAdmitted&&!descriptor->hideShowVerified&&!descriptor->inputSuppressionVerified);
  CHECK(ResolveVisibilityDescriptor(snapshot,f.s.nativeOwner,VisibilityDescriptors,f.s.hand.nowNs,true)==descriptor);
  unsigned resources=0;for(const auto& row:RegisteredMagazineNativeProfiles()){
   const auto* equipment=FindMagazineEquipment(row.id);if(!equipment||equipment->native->configuration.assetPath!=p.configurationPath)continue;
   ++resources;CHECK(row.enabled&&equipment->Ready()&&equipment->geometry->asset==p.asset&&equipment->geometry->mesh==p.mesh);
   CHECK(equipment->geometry->rigFingerprint==p.rig);}
  CHECK(resources==1&&body_equipment_detail::ProfileMatchesSelected(p,snapshot));
  for(unsigned fault=0;fault<12;++fault){auto s=std::make_shared<SelectedMeshesSnapshot>(snapshot);
   if(fault==0)s->configurationPathVerified=false;if(fault==1)s->configurationPath[0]='x';
   if(fault==2)--s->states[0].count;if(fault==3)++s->states[0].count;
   if(fault==4)s->states[0].meshes[0].typeInfo++;if(fault==5)s->states[0].meshes[0].address=0;
   if(fault==6)s->states[0].meshes[0].namePointer=0;if(fault==7)s->states[0].meshes[0].assetPath[0]='x';
   if(fault==8)s->weaponName[0]='x';if(fault==9)s->stateCount=2;
   if(fault==10)s->soleConfiguredArray++;if(fault==11)s->configurationPath.fill('x');
   CHECK(!body_equipment_detail::ProfileMatchesSelected(p,*s));
   CHECK(!f.adapter.AcceptsProfile(f.s.nativeOwner,s,f.s.hand.nowNs));
  }
  for(unsigned fault=0;fault<5;++fault){auto s=std::make_shared<SelectedMeshesSnapshot>(snapshot);
   if(fault==0)s->operationBinding.codeFingerprint[0]++;if(fault==1)s->operationBinding.executableFingerprint++;
   if(fault==2)s->deadlineNs=f.s.hand.nowNs;if(fault==3)s->owner.equipGeneration++;if(fault==4)s->observedNs=f.s.hand.nowNs+1;
   CHECK(!f.adapter.AcceptsProfile(f.s.nativeOwner,s,f.s.hand.nowNs));
  }
  f.Advance();Configure(f,p);f.s.body.intent={++f.intent,BodyInventoryOperation::Holster,1,f.items[0].key};f.Tick();
  CHECK(f.out.phase==BodyHolsterPhase::HidePending);ExactCycle(f,p);ExactCycle(f,p);
  CHECK(f.out.phase==BodyHolsterPhase::Empty&&f.out.freeRight&&!f.hands.Current(InteractionHand::Right));
  const auto source=Source(f);CHECK(source&&BodyHolsteredHostInstance(*source,*source,f.s.hand.nowNs));
  CHECK(source->prop.geometry==MakeBodyAmmoGeometryKey(p.asset,p.mesh,p.part,p.rig));
  ExactCycle(f,p); // A neutral input after committed holster rearms the ordinary policy.
  f.Advance();Configure(f,p);f.Suppress();f.Render(f.out.visibility);f.s.body.intent={++f.intent,BodyInventoryOperation::Draw,1,f.items[0].key};f.Tick();
  CHECK(!Source(f));ExactCycle(f,p);ExactCycle(f,p);
  CHECK(f.out.phase==BodyHolsterPhase::Held&&f.hands.Current(InteractionHand::Right)&&!f.out.blockWeaponActions);
 }
 CHECK(count==15);return 0;
}
int InstalledCatalog(const char* path){auto result=LoadBodyAmmoGeometryCache(path);
 CHECK(result.status==BodyAmmoCacheStatus::Loaded&&result.catalog&&result.catalog->size()==44);
 for(const auto& p:BodyEquipmentProfiles){if(!p.configurationPath)continue;unsigned matches=0;
  for(const auto& g:*result.catalog)if(g->asset==p.asset&&g->mesh==p.mesh&&g->part==p.part&&g->rigFingerprint==p.rig)++matches;
  CHECK(matches==1);}
 CHECK(BodyAmmoAssetProfiles.size()==44);return 0;
}
}
int main(int argc,char** argv){CHECK(argc<=2&&NativeOperationCapabilities.size()==1);
 if(ExactBodyAndVisibility()||ActualHiddenSelectedProfiles()||SourceIdentityAndReceiptFailures()||OriginalDeadlinesAndDrawCancellation()||
    PoseUsesBodyShoulderAndScale()||RecenterRequiresNewTypedSource()||(argc==2&&InstalledCatalog(argv[1])))return 1;
 std::cout<<"15 exact resource/visibility/body joins; 255 malformed bindings; actual hide/draw consumers; 44 installed cache profiles passed. Native/GPU receipts are CPU fixtures.\n";}
