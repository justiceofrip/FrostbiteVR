#pragma once
#include "Bc2MagazineNativeProfile.h"
namespace fvr::bc2::generated {
inline constexpr std::array<ReloadTimingWord,6> RegistryTiming0{{{0x10,0x3f400000u},{0x14,0x00000000u},{0x18,0x40cccccdu},{0x20,0x00000001u},{0x24,0x00000002u},{0x2c,0x00000000u}}};
inline constexpr MagazineNativeProfile RegistryProfile0{{"registry_drum","Objects/Weapons/Drum",{2,1,8,29,100,4,std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x40cccccdu),std::bit_cast<float>(0x3f400000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),false,false},RegistryTiming0,ReloadDescriptorAdmission::ReviewedNative},MagazineIdentityRoute::SelectedCarriedItem,MagazineCycleAdmission::ReviewedReload11Transfer12,7100000096ll};
inline constexpr std::array<ReloadTimingWord,6> RegistryTiming1{{{0x10,0x3f400000u},{0x14,0x00000000u},{0x18,0x402ccccdu},{0x20,0x00000001u},{0x24,0x00000002u},{0x2c,0x00000000u}}};
inline constexpr MagazineNativeProfile RegistryProfile1{{"registry_rifle","Objects/Weapons/A",{2,1,8,29,20,4,std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x402ccccdu),std::bit_cast<float>(0x3f400000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),false,false},RegistryTiming1,ReloadDescriptorAdmission::ReviewedNative},MagazineIdentityRoute::SelectedCarriedItem,MagazineCycleAdmission::ReviewedReload11Transfer12,3400000048ll};
inline constexpr std::array<ReloadTimingWord,6> RegistryTiming2{{{0x10,0x3f400000u},{0x14,0x00000000u},{0x18,0x404ccccdu},{0x20,0x00000001u},{0x24,0x00000002u},{0x2c,0x00000000u}}};
inline constexpr MagazineNativeProfile RegistryProfile2{{"registry_disabled","Objects/Weapons/Disabled",{2,1,8,29,30,4,std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x404ccccdu),std::bit_cast<float>(0x3f400000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),false,false},RegistryTiming2,ReloadDescriptorAdmission::Candidate},MagazineIdentityRoute::SelectedCarriedItem,MagazineCycleAdmission::Candidate,3900000048ll};
inline constexpr std::array<ReloadTimingWord,6> RegistryTiming3{{{0x10,0x3f400000u},{0x14,0x00000000u},{0x18,0x40466666u},{0x20,0x00000001u},{0x24,0x00000002u},{0x2c,0x00000000u}}};
inline constexpr MagazineNativeProfile RegistryProfile3{{"registry_rifle","Objects/Weapons/B",{2,1,8,29,32,4,std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x40466666u),std::bit_cast<float>(0x3f400000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),false,false},RegistryTiming3,ReloadDescriptorAdmission::ReviewedNative},MagazineIdentityRoute::SelectedCarriedItem,MagazineCycleAdmission::ReviewedReload11Transfer12,3799999905ll};
inline constexpr std::array<MagazineNativeRegistration,4> MagazineNativeRegistrations{{
 {static_cast<NativeMagazineProfileId>(0xe5d98dcf9f809e11ull),&RegistryProfile0,"e5d98dcf9f809e119437ca1de6882cc8389322043eccb3589e6524c3775c542f",true},
 {static_cast<NativeMagazineProfileId>(0x2234ed5baad6c0cdull),&RegistryProfile1,"2234ed5baad6c0cdc40a7503d7cd8f1aead824609a72a9e7ae73f383283ab953",true},
 {static_cast<NativeMagazineProfileId>(0xcb6f6a91db3ed5b4ull),&RegistryProfile2,"cb6f6a91db3ed5b4e8d8f7f24f026459498cb2b1ef03f121e5732df3bdf8f47b",false},
 {static_cast<NativeMagazineProfileId>(0x8b5fb768e6c23447ull),&RegistryProfile3,"8b5fb768e6c234475dfc247a4fbe9d6ab1408dd1e041cd582b05f74033a4459c",true},
}};
} // namespace fvr::bc2::generated
