#include "Bc2MagazineEquipmentProfile.h"
#include "Bc2BodyHolster.h"
#include "Bc2VisibilityDescriptorData.h"
#include "Bc2ReloadState.h"
#include <iostream>
#include <algorithm>
#include <iomanip>
using namespace fvr::bc2;
static void text(std::string_view s){std::cout<<'"';for(char c:s){if(c=='"'||c=='\\')std::cout<<'\\';if(static_cast<unsigned char>(c)<32){std::cout<<'?';}else std::cout<<c;}std::cout<<'"';}
int main(){
 std::cout<<"{\"schema\":\"fvr.bc2.compiled-capabilities.v1\",\"runtime_enabled\":false,\"native_registrations\":[";
 bool comma=false;for(const auto&r:RegisteredMagazineNativeProfiles()){
  if(comma)std::cout<<',';comma=true;std::cout<<"{\"id\":"<<static_cast<unsigned long long>(r.id)<<",\"asset\":";text(r.profile?r.profile->configuration.assetName:std::string_view{});
  std::cout<<",\"asset_path\":";text(r.profile?r.profile->configuration.assetPath:std::string_view{});
  const auto resolved=ResolveMagazineNativeProfile(r.id);const auto equipment=FindMagazineEquipment(r.id);
  std::cout<<",\"enabled\":"<<(r.enabled?"true":"false")<<",\"resolved\":"<<(resolved==r.profile&&resolved?"true":"false")
   <<",\"reviewed\":"<<(r.profile&&r.profile->Reviewed()?"true":"false")<<",\"magazine_ready\":"<<(equipment&&equipment->Ready()?"true":"false")<<'}';
 }
 std::cout<<"],\"configured_visibility\":[";comma=false;
 const ReloadStateOwner owner{0x10000,0x20000,0x30000,0x40000,1,1,1};
 Bc2BodyHolster holster(BodyInventoryHolsterAcceptance);
 for(const auto&d:VisibilityDescriptors){
  if(d.asset.empty()||d.asset.size()>=128||d.meshes.empty()||d.meshes.size()>8)return 2;
  for(const auto path:d.meshes)if(path.empty()||path.size()>=512)return 2;
  auto s=std::make_shared<SelectedMeshesSnapshot>();s->owner=owner;s->sequence=1;s->observedNs=1;s->deadlineNs=100000001;
  s->weaponData=0x50000;s->stateTypeInfo=0x60000;s->meshTypeInfo=0x70000;s->stateCount=1;s->soleConfiguredArray=0x80000;
  s->states[0].array=s->soleConfiguredArray;s->states[0].count=static_cast<unsigned>(d.meshes.size());std::copy(d.asset.begin(),d.asset.end(),s->weaponName.begin());
  if(d.configurationPath.size()>=s->configurationPath.size())return 2;
  // Synthetic compile-policy input, not ReadSelectedMeshes1p/native evidence.
  std::copy(d.configurationPath.begin(),d.configurationPath.end(),s->configurationPath.begin());
  s->configurationPathVerified=!d.configurationPath.empty();
  for(unsigned n=0;n<d.meshes.size();++n){auto&m=s->states[0].meshes[n];m.address=0x90000+n*0x100;m.typeInfo=s->meshTypeInfo;m.namePointer=0xa0000+n*0x100;
   std::copy(d.meshes[n].begin(),d.meshes[n].end(),m.assetPath.begin());
   m.kind=ClassifySelectedMeshPath(d.meshes[n]);
  }
  if(comma)std::cout<<',';comma=true;std::cout<<"{\"asset\":";text(d.asset);std::cout<<",\"meshes\":[";
  for(unsigned n=0;n<d.meshes.size();++n){if(n)std::cout<<',';text(d.meshes[n]);}
  std::cout<<"],\"configuration_path\":";text(d.configurationPath);
  std::cout<<",\"synthetic_snapshot_path_verified\":"<<(s->configurationPathVerified?"true":"false")<<",\"native_configuration_capture_tested\":false,\"hide_show_verified\":"<<(d.hideShowVerified?"true":"false")<<",\"input_suppression_verified\":"<<(d.inputSuppressionVerified?"true":"false");
  std::cout<<",\"rig_fingerprint\":\""<<std::hex<<std::setw(16)<<std::setfill('0')<<d.rigFingerprint<<std::dec<<"\",\"native_admitted\":"<<(d.nativeAdmitted?"true":"false")<<",\"production_stow_admitted\":"<<(holster.AcceptsProfile(owner,s,2)?"true":"false")<<'}';
 }
 std::cout<<"],\"mechanisms\":{\"chamber_known\":"<<(ReloadStateSnapshot::chamberKnown?"true":"false")<<",\"manual_cycle_integration\":\"unknown\"}}\n";
}
