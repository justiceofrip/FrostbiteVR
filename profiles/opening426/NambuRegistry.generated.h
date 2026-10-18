#pragma once
#include "Bc2MagazineNativeProfile.h"
namespace fvr::bc2::generated {
inline constexpr std::array<ReloadTimingWord,6> RegistryTiming0{{{0x10,0x3f4ccccdu},{0x14,0x00000000u},{0x18,0x406ccccdu},{0x20,0x00000001u},{0x24,0x00000002u},{0x2c,0x00000000u}}};
inline constexpr MagazineNativeProfile RegistryProfile0{{"Nambu100","Objects/Weapons/Handheld/Beach/Nambu100/Nambu100",{2,1,8,29,30,9999,std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x406ccccdu),std::bit_cast<float>(0x3f4ccccdu),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x3f000000u),std::bit_cast<float>(0x3f800000u),true,false},RegistryTiming0,ReloadDescriptorAdmission::ReviewedNative},MagazineIdentityRoute::SelectedCarriedItem,MagazineCycleAdmission::ReviewedReload11Transfer12,4400000048ll};
inline constexpr std::array<MagazineNativeRegistration,1> MagazineNativeRegistrations{{
 {static_cast<NativeMagazineProfileId>(0xcc2291caf02e41d3ull),&RegistryProfile0,"cc2291caf02e41d3a08d26c1a4142f43a21ab10773c989d6edd872ab56a0b3e6",true},
}};
} // namespace fvr::bc2::generated
