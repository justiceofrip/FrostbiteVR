#pragma once
#include "fvr/graphics/RigidPropGeometry.h"
#include <algorithm>
#include <array>
#include <string>
#include <span>
#include <tuple>
namespace fvr::bc2 {
// Derived asset metadata only. These records identify submitted geometry bytes;
// none proves the active native item, animation clip, GPU skin remap or rig owner.
struct WeaponDrawSection {
 std::string resource,variant,section;
 unsigned lod=0,sectionIndex=0;
 graphics::RigidPropSection signature{};
 unsigned legacyReloadSection=0;
 bool operator==(const WeaponDrawSection&)const=delete;
};
inline constexpr unsigned MaxWeaponDrawSections=2048;
inline bool DrawCatalogText(const std::string& text,std::size_t maximum)noexcept {
 return !text.empty()&&text.size()<=maximum&&std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=32&&c<=126;});
}
inline bool WeaponDrawSectionValid(const WeaponDrawSection& s)noexcept {
 const auto& p=s.signature;
 return DrawCatalogText(s.resource,512)&&DrawCatalogText(s.variant,128)&&DrawCatalogText(s.section,256)&&
  s.lod<4&&s.sectionIndex<32&&s.legacyReloadSection<=3&&
  p.indexCount&&p.indexCount%3==0&&p.indexCount<=100000&&p.vertexSkinHash&&p.positionHash&&
  (p.position==graphics::RigidPropPosition::Half4||p.position==graphics::RigidPropPosition::Float3)&&
  p.stride>=(p.position==graphics::RigidPropPosition::Half4?16u:20u)&&p.stride<=256&&p.paletteSize&&p.paletteSize<=256&&p.partPaletteIndex<p.paletteSize;
}
inline bool WeaponDrawCatalogValid(std::span<const WeaponDrawSection> catalog)noexcept {
 if(catalog.empty()||catalog.size()>MaxWeaponDrawSections)return false;
 std::array<unsigned,MaxWeaponDrawSections> order{};
 for(unsigned n=0;n<catalog.size();++n){if(!WeaponDrawSectionValid(catalog[n]))return false;order[n]=n;}
 const auto key=[&](unsigned n){const auto& s=catalog[n];return std::tie(s.resource,s.variant,s.lod,s.sectionIndex);};
 std::sort(order.begin(),order.begin()+catalog.size(),[&](unsigned a,unsigned b){return key(a)<key(b);});
 for(std::size_t n=1;n<catalog.size();++n)if(key(order[n-1])==key(order[n]))return false;
 return true;
}
inline bool WeaponDrawCountCandidate(std::span<const WeaponDrawSection> catalog,unsigned count)noexcept {
 for(const auto& s:catalog)if(s.signature.indexCount==count)return true;return false;
}
inline bool WeaponDrawLayoutCandidate(std::span<const WeaponDrawSection> catalog,unsigned count,unsigned stride)noexcept {
 if(catalog.size()>MaxWeaponDrawSections)return false;
 for(const auto& s:catalog)if(WeaponDrawSectionValid(s)&&s.signature.indexCount==count&&s.signature.stride==stride)return true;
 return false;
}
enum class WeaponDrawMatchStatus:unsigned {NotCandidate,NoMatch,Unique,Ambiguous,MalformedCatalog};
struct WeaponDrawMatch {
 WeaponDrawMatchStatus status=WeaponDrawMatchStatus::NotCandidate;
 unsigned index=UINT32_MAX,matches=0;graphics::RigidPropFingerprint fingerprint{};bool fingerprintValid=false;
 std::array<unsigned,16> matchingIndices{};
 // This value cannot promote selected labels or same-frame proximity to proof.
 static constexpr bool nativeAssociationVerified=false;
};
inline WeaponDrawMatch MatchWeaponDrawSections(std::span<const WeaponDrawSection> catalog,
 unsigned count,unsigned stride,const graphics::RigidPropDrawBytes& bytes)noexcept {
 WeaponDrawMatch out;
 if(!WeaponDrawCatalogValid(catalog)){out.status=WeaponDrawMatchStatus::MalformedCatalog;return out;}
 // Count/stride are fixed for this draw. At most two interpretations are hashed,
 // even when many different resources have an identical layout/signature.
 std::array<std::optional<graphics::RigidPropFingerprint>,2> hashes{};std::array<bool,2> tried{};
 for(unsigned n=0;n<catalog.size();++n){const auto& s=catalog[n];const auto& p=s.signature;
  if(p.indexCount!=count||p.stride!=stride)continue;out.status=WeaponDrawMatchStatus::NoMatch;
  const auto format=static_cast<unsigned>(p.position);
  if(!tried[format]){hashes[format]=graphics::FingerprintRigidPropDraw(p,bytes);tried[format]=true;}
  const auto& hash=hashes[format];if(!hash)continue;
  if(!out.fingerprintValid){out.fingerprint=*hash;out.fingerprintValid=true;}
  if(hash->skin!=p.vertexSkinHash||hash->positions!=p.positionHash)continue;
  if(out.matches<out.matchingIndices.size())out.matchingIndices[out.matches]=n;
  ++out.matches;out.index=n;
 }
 if(out.matches==1)out.status=WeaponDrawMatchStatus::Unique;
 else if(out.matches>1){out.status=WeaponDrawMatchStatus::Ambiguous;out.index=UINT32_MAX;}
 return out;
}
inline std::span<const WeaponDrawSection> LegacyReloadDrawCatalog(){
 static const WeaponDrawSection data[]{
  {"Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh.res","legacy-shell-draw-proof-20261001","jntWpn_7_Ammo_Brass",0,3,
   {270,48,1,0,graphics::RigidPropPosition::Half4,0xe05c8e35ec306bd2ull,0x5fd96edbcded25baull,90},1},
  {"Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh.res","legacy-shell-draw-proof-20261001","jntWpn_7_ammo_plastic",0,4,
   {90,48,1,0,graphics::RigidPropPosition::Half4,0xce6e99d2c3cd267eull,0xa9888be789a5c902ull,30},2},
  {"Objects/Weapons/Unlock/ACOG_4X/US_ACOG_4X_Mesh.res","801361ce3160ef3392a7bb3317d77ed70b8631fb9abc661b67517314059a36a9","jntWpn_10_ACOG_RedDot",0,2,
   {12,68,1,0,graphics::RigidPropPosition::Float3,0x1b481b02516969b5ull,0x9ad7a905a35a527dull,4},3}};
 return data;
}
} // namespace fvr::bc2
