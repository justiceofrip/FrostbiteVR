#pragma once
#include "Bc2MagazineNativeProfile.h"
namespace fvr::bc2::generated {
inline constexpr std::array<ReloadTimingWord,6> RegistryTiming0{{{0x10,0x3f000000u},{0x14,0x00000000u},{0x18,0x40700000u},{0x20,0x00000001u},{0x24,0x00000000u},{0x2c,0x00000000u}}};
inline constexpr MagazineNativeProfile RegistryProfile0{{"MP443","Objects/Weapons/Handheld/RU_hg_MP443Grach/RU_hg_MP443Grach",{0,1,8,29,17,3,std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x40700000u),std::bit_cast<float>(0x3f000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),false,false},RegistryTiming0,ReloadDescriptorAdmission::ReviewedNative},MagazineIdentityRoute::SelectedCarriedItem,MagazineCycleAdmission::ReviewedSingleFireReload11Transfer12,4450000000ll};
inline constexpr std::array<ReloadTimingWord,6> RegistryTiming1{{{0x10,0x3f000000u},{0x14,0x00000000u},{0x18,0x40700000u},{0x20,0x00000001u},{0x24,0x00000000u},{0x2c,0x00000000u}}};
inline constexpr MagazineNativeProfile RegistryProfile1{{"MP443_sp","Objects/Weapons/Handheld/RU_hg_MP443Grach/SP_hg_MP443Grach",{0,1,8,29,17,3,std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x40700000u),std::bit_cast<float>(0x3f000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),false,false},RegistryTiming1,ReloadDescriptorAdmission::ReviewedNative},MagazineIdentityRoute::SelectedCarriedItem,MagazineCycleAdmission::ReviewedSingleFireReload11Transfer12,4450000000ll};
inline constexpr std::array<MagazineNativeRegistration,2> MagazineNativeRegistrations{{
 {static_cast<NativeMagazineProfileId>(0x2f97016d0246f462ull),&RegistryProfile0,"2f97016d0246f46271f24ebed552289fd6e89162ef16d891488d64bb265cfa0c",true},
 {static_cast<NativeMagazineProfileId>(0x22e837a6b3b2f22cull),&RegistryProfile1,"22e837a6b3b2f22c55587d7a31a1381556b5f253e11fb7b24d7e212077aada48",true},
}};
} // namespace fvr::bc2::generated
