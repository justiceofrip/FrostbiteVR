#pragma once
#include "Bc2M95StockShotDiagnostic.h"
#include "Bc2ReloadState.h"
#include "Bc2ReloadConfigDescriptor.h"
#include <bit>
namespace fvr::bc2 {
// Read-only original-shot experiment. This grants no manual cycle, reload,
// state, ammunition or animation-handle authority.
inline bool M95StockShotConfig(const ReloadObservedConfig& c)noexcept {
 return ReloadDescriptorText(c.assetName,"M95_sp")&&
  ReloadDescriptorText(c.assetPath,"Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95")&&
  c.weaponData>=0x10000&&c.firingData>=0x10000&&c.primaryFire>=0x10000&&
  std::uint64_t(c.primaryFire)+0x170==c.ammoAddress&&
  c.fireLogicType==1&&c.reloadType==1&&c.fireInputAction==8&&c.reloadInputAction==29&&
  c.baseCapacity==5&&c.numberOfMagazines==5&&c.reloadDelay==0&&c.reloadTime==6.9f&&
  c.reloadThreshold==.67f&&c.postReloadTime==0&&c.boltDelay==0&&c.boltTime==2.3f&&
  c.holdBoltUntilFireRelease&&!c.holdBoltUntilZoomRelease;
}
inline bool ReadM95StockShotConfig(const ReloadStateMemory& m,const ReloadObservedConfig& c)noexcept {
 if(!m.read||!M95StockShotConfig(c))return false;
 // FiringFunctionData.FireLogic starts at +0xc. Compare twice, retaining
 // exact authored words. The canonical owner reader verifies the types.
 constexpr std::array<std::uint32_t,12> offsets{0x10,0x14,0x18,0x20,0x24,0xc,0x50,0x54,0x180,0x184,0x4c,0x8c};
 const std::array<std::uint32_t,12> wanted{std::bit_cast<std::uint32_t>(.67f),0,std::bit_cast<std::uint32_t>(6.9f),1,1,0,
  std::bit_cast<std::uint32_t>(2.3f),0,5,5,8,29};
 std::array<std::uint32_t,12> first{},second{};
 for(unsigned pass=0;pass<2;++pass)for(std::size_t n=0;n<offsets.size();++n){
  if(std::uint64_t(c.primaryFire)+offsets[n]+4>UINT32_MAX||
     !m.read(m.context,c.primaryFire+offsets[n],&(pass?second:first)[n],4))return false;
 }
 std::array<unsigned char,2> flags{},again{};
 return m.read(m.context,c.primaryFire+0x58,flags.data(),flags.size())&&
  m.read(m.context,c.primaryFire+0x58,again.data(),again.size())&&flags==again&&
  flags[0]==0&&flags[1]==1&&first==wanted&&second==first;
}
}
