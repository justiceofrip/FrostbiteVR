#pragma once
#include "Bc2MagazineNativeProfile.h"
namespace fvr::bc2::generated {
inline constexpr std::array<ReloadTimingWord,6> RegistryTiming0{{{0x10,0x3f400000u},{0x14,0x00000000u},{0x18,0x40466666u},{0x20,0x00000001u},{0x24,0x00000002u},{0x2c,0x00000000u}}};
inline constexpr MagazineNativeProfile RegistryProfile0{{"F2000_sp","Objects/Weapons/Handheld/BU_rif_F2000/SP_rif_F2000",{2,1,8,29,30,4,std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x40466666u),std::bit_cast<float>(0x3f400000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),false,false},RegistryTiming0,ReloadDescriptorAdmission::ReviewedNative},MagazineIdentityRoute::SelectedCarriedItem,MagazineCycleAdmission::ReviewedReload11Transfer12,3799999905ll};
inline constexpr std::array<MagazineNativeRegistration,1> MagazineNativeRegistrations{{
 {static_cast<NativeMagazineProfileId>(0xa83a3c5288dee2d4ull),&RegistryProfile0,"a83a3c5288dee2d4f5c7d1ddc7033380f5e935cc15574d23f29e43a27a047a17",true},
}};
} // namespace fvr::bc2::generated
