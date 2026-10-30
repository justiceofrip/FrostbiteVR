#pragma once
#include "Bc2ReloadConfigDescriptor.h"
namespace fvr::bc2 {
inline constexpr std::array<ReloadTimingWord,6> FixedContainerTiming0{{{0x10,0x3f266666u},{0x14,0x00000000u},{0x18,0x40833333u},{0x20,0x00000001u},{0x24,0x00000000u},{0x2c,0x00000000u}}};
inline constexpr ReloadConfigDescriptor FixedContainerConfig0{"Garand","Objects/Weapons/Handheld/Beach/Jap_Type5/Type5",{0,1,8,29,8,9999,std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x40833333u),std::bit_cast<float>(0x3f266666u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x3f000000u),false,false},FixedContainerTiming0,ReloadDescriptorAdmission::ReviewedNative};
inline constexpr std::array<ReloadTimingWord,6> FixedContainerTiming1{{{0x10,0x3f266666u},{0x14,0x00000000u},{0x18,0x40833333u},{0x20,0x00000001u},{0x24,0x00000000u},{0x2c,0x00000000u}}};
inline constexpr ReloadConfigDescriptor FixedContainerConfig1{"Garand","Objects/Weapons/Handheld/Beach/M1Garand/M1Garand",{0,1,8,29,8,6,std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x40833333u),std::bit_cast<float>(0x3f266666u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x3f000000u),false,false},FixedContainerTiming1,ReloadDescriptorAdmission::ReviewedNative};
inline constexpr std::array<ReloadTimingWord,6> FixedContainerTiming2{{{0x10,0x3f266666u},{0x14,0x00000000u},{0x18,0x40833333u},{0x20,0x00000001u},{0x24,0x00000000u},{0x2c,0x00000000u}}};
inline constexpr ReloadConfigDescriptor FixedContainerConfig2{"Garand","Objects/Weapons/Handheld/Beach/M1Garand/M1Garand_Assault",{0,1,8,29,8,6,std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x40833333u),std::bit_cast<float>(0x3f266666u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x3f000000u),false,false},FixedContainerTiming2,ReloadDescriptorAdmission::ReviewedNative};
inline constexpr std::array<ReloadTimingWord,6> FixedContainerTiming3{{{0x10,0x3f59999au},{0x14,0x00000000u},{0x18,0x40833333u},{0x20,0x00000001u},{0x24,0x00000000u},{0x2c,0x00000000u}}};
inline constexpr ReloadConfigDescriptor FixedContainerConfig3{"MP412","Objects/Weapons/Handheld/MEC_hg_MP412rex/MEC_hg_MP412rex",{0,1,8,29,6,4,std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x40833333u),std::bit_cast<float>(0x3f59999au),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),std::bit_cast<float>(0x00000000u),false,false},FixedContainerTiming3,ReloadDescriptorAdmission::ReviewedNative};
inline constexpr std::array<const ReloadConfigDescriptor*,4> FixedContainerConfigs{{&FixedContainerConfig0,&FixedContainerConfig1,&FixedContainerConfig2,&FixedContainerConfig3}};
inline const ReloadConfigDescriptor* ResolveFixedContainerNative(const ReloadObservedConfig& c)noexcept {
 const ReloadConfigDescriptor* found=nullptr;for(const auto* row:FixedContainerConfigs)if(MatchesReloadDescriptor(c,*row)){if(found)return nullptr;found=row;}return found;
}
inline bool FixedContainerNativeConfig(const ReloadObservedConfig& c)noexcept{return ResolveFixedContainerNative(c)!=nullptr;}
inline bool ReadFixedContainerNativeConfig(const ReloadStateMemory& m,const ReloadObservedConfig& c)noexcept {
 const auto* row=ResolveFixedContainerNative(c);return row&&ReadReloadDescriptorTiming(m,c,*row);
}
} // namespace fvr::bc2
