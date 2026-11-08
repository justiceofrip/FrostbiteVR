// Variant source SHA256 3a37f0f779caac7464957c1c55becbe60766d4057a66a2ebe5cdb80a0a6519d4
#pragma once
#include "Bc2CampaignProvisioningPolicy.h"
namespace fvr::bc2::provisioning_candidate {
struct ProvisionWord {unsigned offset=0,bytes=0,expected=0;};
struct ExactProvisionVariant {unsigned variant=0;std::uint64_t descriptorProfileId=0,nativeProfileId=0;bool nativeProfileKnown=false;Selector selector{};std::array<ProvisionWord,16> words{};unsigned count=0;};
inline const std::array<ExactProvisionVariant,23> ExactProvisionVariants{{
{0,0x0ull,0ull,false,{"M95_sp","Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95",5,1,1,0x0.0p+0f,0x1.2666666666666p+1f,true,false},{{{12,4,0x0u},{16,4,0x3f2b851fu},{20,4,0x0u},{24,4,0x40dccccdu},{32,4,0x1u},{36,4,0x1u},{76,4,0x8u},{80,4,0x40133333u},{84,4,0x0u},{88,1,0x0u},{89,1,0x1u},{140,4,0x1du},{384,4,0x5u},{388,4,0x5u}}},14},
{1,0x0ull,0ull,false,{"M95_sp_s","Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95_Scoped",5,1,1,0x0.0p+0f,0x1.2666666666666p+1f,true,false},{{{12,4,0x0u},{16,4,0x3f2b851fu},{20,4,0x0u},{24,4,0x40dccccdu},{32,4,0x1u},{36,4,0x1u},{76,4,0x8u},{80,4,0x40133333u},{84,4,0x0u},{88,1,0x0u},{89,1,0x1u},{140,4,0x1du},{384,4,0x5u},{388,4,0x5u}}},14},
{2,0xabac4ca3208fe701ull,0ull,false,{"MG36","Objects/Weapons/Handheld/BU_lmg_MG36/BU_lmg_MG36",100,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f147ae1u},{20,4,0x0u},{24,4,0x40cccccdu},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x3u},{388,4,0x64u}}},15},
{3,0x8fd9d1d1a7c10538ull,0ull,false,{"MG36","Objects/Weapons/Handheld/BU_lmg_MG36/BU_lmg_MG36_Kobra",100,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f147ae1u},{20,4,0x0u},{24,4,0x40cccccdu},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x3u},{388,4,0x64u}}},15},
{4,0xc8d1702eddc33cc6ull,0ull,false,{"MG36","Objects/Weapons/Handheld/BU_lmg_MG36/BU_lmg_MG36_Scope",100,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f147ae1u},{20,4,0x0u},{24,4,0x40cccccdu},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x3u},{388,4,0x64u}}},15},
{5,0x8bb5e63dbaaebf97ull,0ull,false,{"M416","Objects/Weapons/Handheld/UL_rif_HK416/UL_rif_HK416",30,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f333333u},{20,4,0x0u},{24,4,0x4039999au},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x4u},{388,4,0x1eu}}},15},
{6,0xe801a3c6d44d9fadull,0ull,false,{"M416","Objects/Weapons/Handheld/UL_rif_HK416/UL_rif_HK416_Kobra",30,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f333333u},{20,4,0x0u},{24,4,0x4039999au},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x4u},{388,4,0x1eu}}},15},
{7,0xe709f4794b278af9ull,0ull,false,{"M416","Objects/Weapons/Handheld/UL_rif_HK416/UL_rif_HK416_Scoped",30,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f333333u},{20,4,0x0u},{24,4,0x4039999au},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x4u},{388,4,0x1eu}}},15},
{8,0x529f0b745a0b4edeull,0ull,false,{"XM8C","Objects/Weapons/Handheld/US_rif_XM8c/US_rif_XM8c",30,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f400000u},{20,4,0x0u},{24,4,0x402ccccdu},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x4u},{388,4,0x1eu}}},15},
{9,0xef134a49b04b447eull,0ull,false,{"XM8C","Objects/Weapons/Handheld/US_rif_XM8c/US_rif_XM8c_Kobra",30,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f400000u},{20,4,0x0u},{24,4,0x402ccccdu},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x4u},{388,4,0x1eu}}},15},
{10,0xc31431276522dd49ull,0ull,false,{"XM8C","Objects/Weapons/Handheld/US_rif_XM8c/US_rif_XM8c_Scoped",30,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f400000u},{20,4,0x0u},{24,4,0x402ccccdu},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x4u},{388,4,0x1eu}}},15},
{11,0x62233fb7106ed7bfull,0ull,false,{"XM8 LMG","Objects/Weapons/Handheld/US_rif_XM8lmg/US_rif_XM8lmg",100,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f23d70au},{20,4,0x0u},{24,4,0x40cccccdu},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x3u},{388,4,0x64u}}},15},
{12,0x9019ab03e6002f09ull,0ull,false,{"XM8 LMG","Objects/Weapons/Handheld/US_rif_XM8lmg/US_rif_XM8lmg_Kobra",100,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f23d70au},{20,4,0x0u},{24,4,0x40cccccdu},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x3u},{388,4,0x64u}}},15},
{13,0x7e0f35786784c13cull,0ull,false,{"XM8 LMG","Objects/Weapons/Handheld/US_rif_XM8lmg/US_rif_XM8lmg_Scoped",100,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f23d70au},{20,4,0x0u},{24,4,0x40cccccdu},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x3u},{388,4,0x64u}}},15},
{14,0xa83a3c5288dee2d4ull,0ull,false,{"F2000_sp","Objects/Weapons/Handheld/BU_rif_F2000/SP_rif_F2000",30,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f400000u},{20,4,0x0u},{24,4,0x40466666u},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x4u},{388,4,0x1eu}}},15},
{15,0x861f5754604d1e0cull,0ull,false,{"F2000_sp_k","Objects/Weapons/Handheld/BU_rif_F2000/SP_rif_F2000_Kobra",30,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f400000u},{20,4,0x0u},{24,4,0x40466666u},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x4u},{388,4,0x1eu}}},15},
{16,0x98d56d8977fde387ull,1ull,true,{"AEK971_sp","Objects/Weapons/Handheld/RU_rgl_AEK971/SP_rgl_AEK971",30,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f400000u},{20,4,0x0u},{24,4,0x404ccccdu},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x4u},{388,4,0x1eu}}},15},
{17,0x2c7db5ddffa18a2bull,0ull,false,{"AEK971_sp_k","Objects/Weapons/Handheld/RU_rgl_AEK971/SP_rgl_AEK971_Kobra",30,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f400000u},{20,4,0x0u},{24,4,0x404ccccdu},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x4u},{388,4,0x1eu}}},15},
{18,0xc73120ea25cc6946ull,0ull,false,{"SCAR_sp","Objects/Weapons/Handheld/UL_rif_FNSCARL/SP_rif_FNSCARL",30,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f400000u},{20,4,0x0u},{24,4,0x40533333u},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x4u},{388,4,0x1eu}}},15},
{19,0x924645a13919d598ull,0ull,false,{"SCAR_sp_s","Objects/Weapons/Handheld/UL_rif_FNSCARL/SP_rif_FNSCARL_Scoped",30,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f400000u},{20,4,0x0u},{24,4,0x40533333u},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x4u},{388,4,0x1eu}}},15},
{20,0x00c1e4398d18f3f2ull,0ull,false,{"XM8_sp","Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8",30,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f400000u},{20,4,0x0u},{24,4,0x40333333u},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x4u},{388,4,0x1eu}}},15},
{21,0x45b0c2a9583f5058ull,0ull,true,{"XM8_sp_s","Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped",30,2,1,0x0.0p+0f,0x0.0p+0f,false,false},{{{12,4,0x0u},{16,4,0x3f400000u},{20,4,0x0u},{24,4,0x40333333u},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x0u},{84,4,0x0u},{88,1,0x0u},{89,1,0x0u},{140,4,0x1du},{384,4,0x4u},{388,4,0x1eu}}},15},
{22,0xcc2291caf02e41d3ull,0ull,false,{"Nambu100","Objects/Weapons/Handheld/Beach/Nambu100/Nambu100",30,2,1,0x1.0000000000000p-1f,0x1.0000000000000p+0f,true,false},{{{12,4,0x0u},{16,4,0x3f4ccccdu},{20,4,0x0u},{24,4,0x406ccccdu},{32,4,0x1u},{36,4,0x2u},{44,4,0x0u},{76,4,0x8u},{80,4,0x3f800000u},{84,4,0x3f000000u},{88,1,0x0u},{89,1,0x1u},{140,4,0x1du},{384,4,0x270fu},{388,4,0x1eu}}},15}
}};
inline const ExactProvisionVariant* FindProvisionVariant(unsigned variant)noexcept {
    const auto i=std::find_if(ExactProvisionVariants.begin(),ExactProvisionVariants.end(),[&](const auto& v){return v.variant==variant;});
    return i==ExactProvisionVariants.end()?nullptr:&*i;
}
inline bool ExactProvisionConfiguration(const Resource& resource,const ExactProvisionVariant& variant)noexcept {
    if(!variant.count||variant.count>variant.words.size())return false;
    for(unsigned n=0;n<variant.count;++n){const auto& w=variant.words[n];unsigned value=0;
        if(w.bytes!=1&&w.bytes!=4)return false;
        if(w.offset>=0xc&&std::uint64_t(w.offset)+w.bytes<=0xc+resource.logic.size())std::memcpy(&value,resource.logic.data()+w.offset-0xc,w.bytes);
        else if(w.offset>=0x170&&std::uint64_t(w.offset)+w.bytes<=0x170+resource.ammo.size())std::memcpy(&value,resource.ammo.data()+w.offset-0x170,w.bytes);
        else return false;
        if(value!=w.expected)return false;
    }return true;
}
inline std::optional<Resource> SelectExactProvisionResource(const Memory& memory,std::uint32_t data,std::uint32_t asset,unsigned variant)noexcept {
    const auto descriptor=FindProvisionVariant(variant);if(!descriptor)return {};
    const auto resource=SelectProvisioningResource(memory,data,asset,descriptor->selector);
    if(!resource||!ExactProvisionConfiguration(*resource,*descriptor))return {};return resource;
}
}
