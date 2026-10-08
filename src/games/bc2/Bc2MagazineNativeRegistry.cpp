#include "Bc2MagazineNativeProfile.h"
#include <algorithm>
#ifdef FVR_BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER
#include FVR_BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER
#endif
namespace fvr::bc2 {
namespace {
constexpr std::array<MagazineNativeRegistration,2> Builtins{{
 {NativeMagazineProfileId::ScopedXm8,&Xm8MagazineNativeProfile,{},true},
 {NativeMagazineProfileId::AuthoredAek,&AekMagazineNativeProfile,{},true}}};
bool SamePath(const MagazineNativeRegistration& a,const MagazineNativeRegistration& b)noexcept {
 return a.profile&&b.profile&&a.profile->configuration.assetName==b.profile->configuration.assetName&&
  a.profile->configuration.assetPath==b.profile->configuration.assetPath;
}
bool BuiltinConflict(const MagazineNativeRegistration& row)noexcept {
 for(const auto& b:Builtins)if(row.id==b.id||SamePath(row,b))return true;
 return false;
}
}
std::optional<NativeMagazineProfileId> MagazineProfileKey(std::string_view digest)noexcept {
 if(digest.size()!=64)return {};
 std::uint64_t id=0;
 for(std::size_t i=0;i<digest.size();++i){const auto c=digest[i];unsigned nibble;
  if(c>='0'&&c<='9')nibble=unsigned(c-'0');else if(c>='a'&&c<='f')nibble=10+unsigned(c-'a');else return {};
  if(i<16)id=(id<<4)|nibble;
 }
 if(id<=1)return {};return static_cast<NativeMagazineProfileId>(id);
}
const MagazineNativeRegistration* SelectGeneratedMagazineRegistration(
 std::span<const MagazineNativeRegistration> entries,NativeMagazineProfileId id)noexcept {
 if(entries.size()>MaxGeneratedMagazineProfiles)return nullptr;
 const MagazineNativeRegistration* found=nullptr;
 for(const auto& row:entries)if(row.id==id){if(found)return nullptr;found=&row;}
 if(!found||!found->enabled||!found->profile||!found->profile->Reviewed()||BuiltinConflict(*found))return nullptr;
 const auto key=MagazineProfileKey(found->descriptorDigest);if(!key||*key!=id)return nullptr;
 for(const auto& row:entries)if(&row!=found&&(row.id==id||row.descriptorDigest==found->descriptorDigest||SamePath(row,*found)))return nullptr;
 return found;
}
std::span<const MagazineNativeRegistration> RegisteredMagazineNativeProfiles()noexcept {
 struct Table {std::array<MagazineNativeRegistration,MaxGeneratedMagazineProfiles+2> rows{};std::size_t count=2;};
 static const Table table=[] {
  Table out;std::copy(Builtins.begin(),Builtins.end(),out.rows.begin());
#ifdef FVR_BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER
  // Compiling a generated row does not enable it; each lookup validates its
  // explicit enabled state, exact family descriptor and collision-free key.
  if(generated::MagazineNativeRegistrations.size()<=MaxGeneratedMagazineProfiles){
   for(const auto& row:generated::MagazineNativeRegistrations)out.rows[out.count++]=row;
  }
#endif
  return out;
 }();
 return {table.rows.data(),table.count};
}
const MagazineNativeProfile* ResolveMagazineNativeProfile(NativeMagazineProfileId id)noexcept {
 for(const auto& row:Builtins)if(row.id==id)return row.profile;
 const auto rows=RegisteredMagazineNativeProfiles();
 const auto row=SelectGeneratedMagazineRegistration(rows.subspan(2),id);return row?row->profile:nullptr;
}
const MagazineNativeRegistration* FindMagazineNativeProfile(std::string_view asset)noexcept {
 const MagazineNativeRegistration* found=nullptr;
 for(const auto& row:RegisteredMagazineNativeProfiles())if(row.profile&&row.profile->configuration.assetName==asset&&ResolveMagazineNativeProfile(row.id)==row.profile){
  if(found)return nullptr;found=&row;
 }
 return found;
}
const MagazineNativeRegistration* FindMagazineNativeProfile(const ReloadObservedConfig& config)noexcept {
 const MagazineNativeRegistration* found=nullptr;
 for(const auto& row:RegisteredMagazineNativeProfiles())if(ResolveMagazineNativeProfile(row.id)==row.profile&&row.profile&&row.profile->Matches(config)){
  if(found)return nullptr;found=&row;
 }
 return found;
}
} // namespace fvr::bc2
