#pragma once
#include "Bc2ReloadState.h"
#include <bit>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

namespace fvr::bc2 {
// Reviewed native configuration data, separate from interaction geometry and
// current callback/owner authority. Authored DBX output is always Candidate.
// No resource/name/category match can promote a candidate to native admission.
enum class ReloadDescriptorAdmission : std::uint8_t { Candidate, ReviewedNative };
struct ReloadConfigValues {
 std::int32_t fireLogicType=0,reloadType=0,fireInputAction=0,reloadInputAction=0;
 std::int32_t baseCapacity=0,numberOfMagazines=0;
 float reloadDelay=0,reloadTime=0,reloadThreshold=0,postReloadTime=0,boltDelay=0,boltTime=0;
 bool holdBoltUntilFireRelease=false,holdBoltUntilZoomRelease=false;
};
enum class ReloadTimingComparison : std::uint8_t { Bits, Float };
struct ReloadTimingWord {
 std::uint32_t offset=0,expected=0;
 ReloadTimingComparison comparison=ReloadTimingComparison::Bits;
};
struct ReloadConfigDescriptor {
 std::string_view assetName,assetPath;
 ReloadConfigValues values{};
 std::span<const ReloadTimingWord> timing{};
 ReloadDescriptorAdmission admission=ReloadDescriptorAdmission::Candidate;
};
inline constexpr std::array<ReloadTimingWord,2> SpasReloadTiming{{
 {0x10,std::bit_cast<std::uint32_t>(1.f),ReloadTimingComparison::Float},
 {0x18,std::bit_cast<std::uint32_t>(.72f),ReloadTimingComparison::Float}}};
inline constexpr std::array<ReloadTimingWord,6> Xm8ReloadTiming{{
 {0x10,std::bit_cast<std::uint32_t>(.75f)},{0x14,0},
 {0x18,std::bit_cast<std::uint32_t>(2.8f)},{0x20,1},{0x24,2},{0x2c,0}}};
inline constexpr ReloadConfigDescriptor SpasReloadDescriptor{
 "SPAS12_sp","Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12",
 {1,0,8,29,4,4,.06f,.72f,1,1,.5f,0,false,false},SpasReloadTiming,
 ReloadDescriptorAdmission::ReviewedNative};
inline constexpr ReloadConfigDescriptor Xm8ReloadDescriptor{
 "XM8_sp_s","Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped",
 {2,1,8,29,30,4,0,2.8f,.75f,0,0,0,false,false},Xm8ReloadTiming,
 ReloadDescriptorAdmission::ReviewedNative};

template<std::size_t N>
inline bool ReloadDescriptorText(const std::array<char,N>& actual,std::string_view expected)noexcept {
 return !expected.empty()&&expected.size()<N&&
  !std::memcmp(actual.data(),expected.data(),expected.size())&&actual[expected.size()]=='\0';
}
inline bool MatchesReloadDescriptor(const ReloadObservedConfig& c,const ReloadConfigDescriptor& d)noexcept {
 const auto& v=d.values;
 return d.admission==ReloadDescriptorAdmission::ReviewedNative&&
  ReloadDescriptorText(c.assetName,d.assetName)&&ReloadDescriptorText(c.assetPath,d.assetPath)&&
  c.weaponData>=0x10000&&c.firingData>=0x10000&&c.primaryFire>=0x10000&&
  std::uint64_t(c.primaryFire)+0x170==c.ammoAddress&&
  c.fireLogicType==v.fireLogicType&&c.reloadType==v.reloadType&&
  c.fireInputAction==v.fireInputAction&&c.reloadInputAction==v.reloadInputAction&&
  c.baseCapacity==v.baseCapacity&&c.numberOfMagazines==v.numberOfMagazines&&
  c.reloadDelay==v.reloadDelay&&c.reloadTime==v.reloadTime&&c.reloadThreshold==v.reloadThreshold&&
  c.postReloadTime==v.postReloadTime&&c.boltDelay==v.boltDelay&&c.boltTime==v.boltTime&&
  c.holdBoltUntilFireRelease==v.holdBoltUntilFireRelease&&c.holdBoltUntilZoomRelease==v.holdBoltUntilZoomRelease;
}
inline bool ReadReloadDescriptorTiming(const ReloadStateMemory& m,const ReloadObservedConfig& c,
 const ReloadConfigDescriptor& d)noexcept {
 if(!m.read||!MatchesReloadDescriptor(c,d)||d.timing.empty()||d.timing.size()>6)return false;
 for(const auto& word:d.timing)
  if(word.offset>0x1000||std::uint64_t(c.primaryFire)+word.offset+4>UINT32_MAX)return false;
 std::array<std::uint32_t,6> first{},second{};
 for(unsigned pass=0;pass<2;++pass)for(std::size_t n=0;n<d.timing.size();++n)
  if(!m.read(m.context,c.primaryFire+d.timing[n].offset,&(pass?second:first)[n],4))return false;
 for(std::size_t n=0;n<d.timing.size();++n){
  const auto& w=d.timing[n];
  if(w.comparison==ReloadTimingComparison::Bits){
   if(first[n]!=w.expected||second[n]!=first[n])return false;
  }else if(w.comparison==ReloadTimingComparison::Float){
   const float a=std::bit_cast<float>(first[n]),b=std::bit_cast<float>(second[n]);
   if(a!=std::bit_cast<float>(w.expected)||a!=b)return false;
  }else return false;
 }
 return true;
}
} // namespace fvr::bc2
