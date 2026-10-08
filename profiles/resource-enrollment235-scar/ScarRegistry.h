#pragma once
#include "Bc2MagazineNativeProfile.h"
namespace fvr::bc2::generated {
inline constexpr std::array<ReloadTimingWord,6> RegistryTiming0{{{0x10,0x3f400000u},{0x14,0x00000000u},{0x18,0x40533333u},{0x20,0x00000001u},{0x24,0x00000002u},{0x2c,0x00000000u}}};
inline constexpr MagazineNativeProfile RegistryProfile0{{"SCAR_sp_s","Objects/Weapons/Handheld/UL_rif_FNSCARL/SP_rif_FNSCARL_Scoped",{2,1,8,29,30,4,std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x40533333u),std::bit_cast<float>(0x3f400000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),false,false},RegistryTiming0,ReloadDescriptorAdmission::ReviewedNative},MagazineIdentityRoute::SelectedCarriedItem,MagazineCycleAdmission::ReviewedReload11Transfer12,3999999953ll};
inline constexpr std::array<ReloadTimingWord,6> RegistryTiming1{{{0x10,0x3f400000u},{0x14,0x00000000u},{0x18,0x40533333u},{0x20,0x00000001u},{0x24,0x00000002u},{0x2c,0x00000000u}}};
inline constexpr MagazineNativeProfile RegistryProfile1{{"SCAR_sp","Objects/Weapons/Handheld/UL_rif_FNSCARL/SP_rif_FNSCARL",{2,1,8,29,30,4,std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x40533333u),std::bit_cast<float>(0x3f400000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),false,false},RegistryTiming1,ReloadDescriptorAdmission::ReviewedNative},MagazineIdentityRoute::SelectedCarriedItem,MagazineCycleAdmission::ReviewedReload11Transfer12,3999999953ll};
inline constexpr std::array<MagazineNativeRegistration,2> MagazineNativeRegistrations{{
 {static_cast<NativeMagazineProfileId>(0x924645a13919d598ull),&RegistryProfile0,"924645a13919d598e325db8cac9cadff5e9996171cb87bf765d39de39e9a9fd0",true},
 {static_cast<NativeMagazineProfileId>(0xc73120ea25cc6946ull),&RegistryProfile1,"c73120ea25cc6946e03ecaa270c96fe81fc89bf3878d9269a03f60131406c2f7",true},
}};
} // namespace fvr::bc2::generated
