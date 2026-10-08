#pragma once
#include "Bc2MagazineGeometryProfile.h"
#include "Bc2MagazineNativeProfile.h"

namespace fvr::bc2 {
// Immutable compiled registration joins independent native and geometry data.
// Experimental authored/design estimates stay distinct from measured calibration.
// Neither kind infers native ammunition or ownership evidence from a gun class.
struct MagazineEquipmentProfile {
 NativeMagazineProfileId nativeId=NativeMagazineProfileId::ScopedXm8;
 const MagazineNativeProfile* native=nullptr;
 const MagazineGeometryProfile* geometry=nullptr;
 bool geometryVerified=false;
 bool experimentalGeometry=false; // Explicit generated build; never measured/headset verified.
 bool Ready()const noexcept {
  return native&&native->Reviewed()&&(geometryVerified||experimentalGeometry)&&geometry&&geometry->rigFingerprint&&
   MagazineGeometryMatchesConfiguration(*geometry,*native)&&!geometry->mesh.empty()&&
   geometry->interaction.insertion.id&&geometry->interaction.insertion.revision;
 }
};
inline const MagazineEquipmentProfile& Xm8MagazineEquipment()noexcept {
 static const MagazineEquipmentProfile profile{NativeMagazineProfileId::ScopedXm8,
  ResolveMagazineNativeProfile(NativeMagazineProfileId::ScopedXm8),&Xm8MagazineGeometry(),true};
 return profile;
}
inline const MagazineEquipmentProfile* FindMagazineEquipment(NativeMagazineProfileId id)noexcept {
 if(id==NativeMagazineProfileId::ScopedXm8)return &Xm8MagazineEquipment();
 // One shared immutable join for every registered native family. Without the
 // optional generated header, missing geometry remains unavailable as before.
 static const auto profiles=[] {
  std::array<MagazineEquipmentProfile,MaxGeneratedMagazineProfiles+2> result{};
  const auto registeredProfiles=RegisteredMagazineNativeProfiles();
  for(std::size_t n=0;n<registeredProfiles.size();++n){const auto& registered=registeredProfiles[n];
   const auto geometry=ResolveMagazineNativeProfile(registered.id)?
    FindMagazineGeometry(*registered.profile):nullptr;
   result[n]={registered.id,registered.profile,geometry,false,geometry!=nullptr};
  }
  return result;
 }();
 const auto registered=ResolveMagazineNativeProfile(id);
 if(!registered)return nullptr;
 for(const auto& profile:profiles)if(profile.nativeId==id&&profile.native==registered)return &profile;
 return nullptr;
}
inline const MagazineEquipmentProfile* FindMagazineEquipment(std::string_view asset)noexcept {
 const auto registered=FindMagazineNativeProfile(asset);
 return registered?FindMagazineEquipment(registered->id):nullptr;
}
inline const MagazineEquipmentProfile* FindMagazineEquipment(const ReloadObservedConfig& config)noexcept {
 const auto registered=FindMagazineNativeProfile(config);
 return registered?FindMagazineEquipment(registered->id):nullptr;
}
} // namespace fvr::bc2
